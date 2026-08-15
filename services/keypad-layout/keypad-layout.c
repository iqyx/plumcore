/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keypad layout event translator
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Translates raw keypad events (EV_TYPE_RAW with EV_RAW_x codes) coming from a low-level keypad
 * driver into logical key events (EV_TYPE_KEY) or counting events (EV_TYPE_REL), according to a
 * configured layout table. Each layout item maps one raw input code, optionally qualified by a
 * modifier key that must be held down at press time, to an output code. The output event type is
 * inferred from the output code: EV_REL_* codes are emitted as EV_TYPE_REL, everything else as
 * EV_TYPE_KEY.
 *
 * The service uses two tasks:
 *   - The receive task blocks on the upstream Event interface and forwards every raw event onto an
 *     internal input queue. It does nothing else, so the indefinite blocking of listen() never
 *     stalls the timing logic.
 *   - The processing task ticks at a fixed period (KEYPAD_LAYOUT_TICK_MS). Each cycle it drains the
 *     input queue to update the down-key set and the set of held presses, then revisits every held
 *     press and decides, from the number of cycles it has been held, whether an output event is due
 *     this cycle. Counter (EV_TYPE_REL) keys auto-repeat (first count, then a slow rate accelerating
 *     to a fast rate); key (EV_TYPE_KEY) presses emit their long/very-long code live when the hold
 *     crosses the configured thresholds, and their short code on release if neither fired.
 *
 * Generated events are queued and delivered through the blocking listen() of the implemented
 * downstream Event interface.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/event.h>

#include "keypad-layout.h"

#define MODULE_NAME "keypad-layout"


/* Derive the output event type from the output code. The relative axes are the only EV_TYPE_REL
 * codes; every other output code is a key event. */
static enum event_type keypad_layout_out_type(enum event_code code) {
	if (code == EV_REL_X || code == EV_REL_Y) {
		return EV_TYPE_REL;
	}
	return EV_TYPE_KEY;
}


static bool keypad_layout_is_down(KeypadLayout *self, enum event_code code) {
	for (size_t i = 0; i < self->down_count; i++) {
		if (self->down_keys[i] == code) {
			return true;
		}
	}
	return false;
}


static void keypad_layout_set_down(KeypadLayout *self, enum event_code code) {
	if (keypad_layout_is_down(self, code)) {
		return;
	}
	if (self->down_count >= KEYPAD_LAYOUT_MAX_KEYS) {
		return;
	}
	self->down_keys[self->down_count] = code;
	self->down_count++;
}


static void keypad_layout_clear_down(KeypadLayout *self, enum event_code code) {
	for (size_t i = 0; i < self->down_count; i++) {
		if (self->down_keys[i] == code) {
			self->down_keys[i] = self->down_keys[self->down_count - 1];
			self->down_count--;
			return;
		}
	}
}


/* True if any layout item for in_code requires a modifier that is currently held. Such items then take
 * precedence: only they fire, and unqualified items for the same key are suppressed. */
static bool keypad_layout_modified_match(KeypadLayout *self, enum event_code in_code) {
	for (const struct keypad_layout_item *item = self->conf.layout; item->in_code != EV_CODE_NONE; item++) {
		if (item->in_code == in_code && item->in_modifier != EV_CODE_NONE &&
		    keypad_layout_is_down(self, item->in_modifier)) {
			return true;
		}
	}
	return false;
}


static void keypad_layout_emit(KeypadLayout *self, enum event_code code, int32_t value) {
	struct keypad_layout_event ev = {
		.type = keypad_layout_out_type(code),
		.code = code,
		.value = value,
	};
	xQueueSend(self->event_queue, &ev, 0);
}


/*
 * Decide whether a counter (EV_TYPE_REL) key that has been held for @p cycles processing cycles must
 * emit a count this cycle. The schedule, expressed in cycles (one cycle = KEYPAD_LAYOUT_TICK_MS):
 *   - a single count once the first-count delay is reached,
 *   - then, from the start delay, a count every slow period,
 *   - then, once the fast delay is reached, a count every fast period.
 */
static bool keypad_layout_count_due(KeypadLayout *self, uint32_t cycles) {
	uint32_t first = self->conf.count_first_ms / KEYPAD_LAYOUT_TICK_MS;
	uint32_t start = self->conf.count_start_ms / KEYPAD_LAYOUT_TICK_MS;
	uint32_t period = self->conf.count_period_ms / KEYPAD_LAYOUT_TICK_MS;
	uint32_t fast_delay = self->conf.count_fast_delay_ms / KEYPAD_LAYOUT_TICK_MS;
	uint32_t fast_period = self->conf.count_fast_period_ms / KEYPAD_LAYOUT_TICK_MS;
	if (period == 0) {
		period = 1;
	}
	if (fast_period == 0) {
		fast_period = 1;
	}

	if (cycles < first) {
		return false;
	}
	if (cycles == first) {
		return true;
	}
	if (cycles < start) {
		return false;
	}
	if (cycles < fast_delay) {
		return ((cycles - start) % period) == 0;
	}
	return ((cycles - fast_delay) % fast_period) == 0;
}


/* Record a matched press so that its timing can be tracked from the next processing cycle on. */
static void keypad_layout_press_add(KeypadLayout *self, enum event_code in_code, const struct keypad_layout_item *item) {
	if (self->press_count >= KEYPAD_LAYOUT_MAX_KEYS) {
		return;
	}
	memset(&self->presses[self->press_count], 0, sizeof(struct keypad_layout_press));
	self->presses[self->press_count].in_code = in_code;
	self->presses[self->press_count].item = item;
	self->press_count++;
}


/* Add a press record for every layout item matched by a just-pressed input code. All matching items
 * fire, so one key can drive several outputs (e.g. a key event and a counter event at once). When any
 * item for the key is qualified by a currently-held modifier, only the modifier-qualified items fire
 * (see keypad_layout_modified_match), so a modifier still overrides the plain mapping. */
static void keypad_layout_add_presses(KeypadLayout *self, enum event_code in_code) {
	bool modified = keypad_layout_modified_match(self, in_code);
	for (const struct keypad_layout_item *item = self->conf.layout; item->in_code != EV_CODE_NONE; item++) {
		if (item->in_code != in_code) {
			continue;
		}
		bool qualified = (item->in_modifier != EV_CODE_NONE) && keypad_layout_is_down(self, item->in_modifier);
		if (modified ? qualified : (item->in_modifier == EV_CODE_NONE)) {
			keypad_layout_press_add(self, in_code, item);
		}
	}
}


/* Finalise every held press for a released input code: a key with no long/very-long already emitted
 * produces its short code, a counter that never reached its first count still produces a single count. */
static void keypad_layout_release_presses(KeypadLayout *self, enum event_code in_code) {
	size_t i = 0;
	while (i < self->press_count) {
		struct keypad_layout_press *press = &self->presses[i];
		if (press->in_code != in_code) {
			i++;
			continue;
		}
		const struct keypad_layout_item *item = press->item;
		if (keypad_layout_out_type(item->out_code) == EV_TYPE_REL) {
			if (!press->emitted_count) {
				keypad_layout_emit(self, item->out_code, item->counter);
			}
		} else if (!press->emitted_long && !press->emitted_very_long) {
			keypad_layout_emit(self, item->out_code, 1);
		}

		/* Swap-remove and re-examine the entry moved into this slot. */
		*press = self->presses[self->press_count - 1];
		self->press_count--;
	}
}


/* Apply one raw input event: update the down-key set and add/remove the matched presses. */
static void keypad_layout_handle_input(KeypadLayout *self, enum event_code code, int32_t value) {
	if (value != 0) {
		keypad_layout_set_down(self, code);
		keypad_layout_add_presses(self, code);
	} else {
		keypad_layout_release_presses(self, code);
		keypad_layout_clear_down(self, code);
	}
}


/* Advance every held press by one cycle and emit any output that is due this cycle. */
static void keypad_layout_process_cycle(KeypadLayout *self) {
	for (size_t i = 0; i < self->press_count; i++) {
		struct keypad_layout_press *press = &self->presses[i];
		const struct keypad_layout_item *item = press->item;
		press->cycles++;

		if (keypad_layout_out_type(item->out_code) == EV_TYPE_REL) {
			if (keypad_layout_count_due(self, press->cycles)) {
				keypad_layout_emit(self, item->out_code, item->counter);
				press->emitted_count = true;
			}
			continue;
		}

		/* Key press: emit the long/very-long code live once its hold crosses the threshold. */
		uint32_t held_ms = press->cycles * KEYPAD_LAYOUT_TICK_MS;
		if (item->out_code_very_long != EV_CODE_NONE && self->conf.very_long_press_ms > 0 &&
		    !press->emitted_very_long && held_ms >= self->conf.very_long_press_ms) {
			keypad_layout_emit(self, item->out_code_very_long, 1);
			press->emitted_very_long = true;
			press->emitted_long = true;
		} else if (item->out_code_long != EV_CODE_NONE && self->conf.long_press_ms > 0 &&
		           !press->emitted_long && held_ms >= self->conf.long_press_ms) {
			keypad_layout_emit(self, item->out_code_long, 1);
			press->emitted_long = true;
		}
	}
}


static void keypad_layout_rx_task(void *p) {
	KeypadLayout *self = (KeypadLayout *)p;

	self->rx_running = true;
	while (self->can_run) {
		struct keypad_layout_event ev = {0};
		if (self->conf.source->vmt->listen(self->conf.source, &ev.type, &ev.code, &ev.value) != EV_RET_OK) {
			continue;
		}

		xQueueSend(self->input_queue, &ev, portMAX_DELAY);
	}
	self->rx_running = false;
	vTaskDelete(NULL);
}


static void keypad_layout_proc_task(void *p) {
	KeypadLayout *self = (KeypadLayout *)p;

	self->proc_running = true;
	TickType_t last_wake = xTaskGetTickCount();
	while (self->can_run) {
		/* Drain all pending raw input, then run one timing cycle over the held keys. */
		struct keypad_layout_event ev = {0};
		while (xQueueReceive(self->input_queue, &ev, 0) == pdTRUE) {
			keypad_layout_handle_input(self, ev.code, ev.value);
		}
		keypad_layout_process_cycle(self);

		vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(KEYPAD_LAYOUT_TICK_MS));
	}
	self->proc_running = false;
	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Event interface implementation
 **********************************************************************************************************************/

static event_ret_t keypad_layout_event_listen(Event *event, enum event_type *type, enum event_code *code, int32_t *value) {
	KeypadLayout *self = event->parent;
	struct keypad_layout_event ev = {0};
	xQueueReceive(self->event_queue, &ev, portMAX_DELAY);
	if (type != NULL) {
		*type = ev.type;
	}
	if (code != NULL) {
		*code = ev.code;
	}
	if (value != NULL) {
		*value = ev.value;
	}

	return EV_RET_OK;
}


static const struct event_vmt keypad_layout_event_vmt = {
	.subscribe = NULL,
	.listen = &keypad_layout_event_listen,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

/* Fill in any counter timing left at 0 with its compile-time default. */
static void keypad_layout_apply_defaults(KeypadLayout *self) {
	if (self->conf.count_first_ms == 0) {
		self->conf.count_first_ms = KEYPAD_LAYOUT_COUNT_FIRST_MS;
	}
	if (self->conf.count_start_ms == 0) {
		self->conf.count_start_ms = KEYPAD_LAYOUT_COUNT_START_MS;
	}
	if (self->conf.count_period_ms == 0) {
		self->conf.count_period_ms = KEYPAD_LAYOUT_COUNT_PERIOD_MS;
	}
	if (self->conf.count_fast_delay_ms == 0) {
		self->conf.count_fast_delay_ms = KEYPAD_LAYOUT_COUNT_FAST_DELAY_MS;
	}
	if (self->conf.count_fast_period_ms == 0) {
		self->conf.count_fast_period_ms = KEYPAD_LAYOUT_COUNT_FAST_PERIOD_MS;
	}
}


keypad_layout_ret_t keypad_layout_init(KeypadLayout *self, const struct keypad_layout_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL) ||
	    u_assert(conf->source != NULL) ||
	    u_assert(conf->layout != NULL)) {
		return KEYPAD_LAYOUT_RET_NULL;
	}
	memset(self, 0, sizeof(KeypadLayout));
	memcpy(&self->conf, conf, sizeof(struct keypad_layout_conf));
	keypad_layout_apply_defaults(self);

	self->event_queue = xQueueCreate(KEYPAD_LAYOUT_EVENT_QUEUE_SIZE, sizeof(struct keypad_layout_event));
	self->input_queue = xQueueCreate(KEYPAD_LAYOUT_EVENT_QUEUE_SIZE, sizeof(struct keypad_layout_event));
	if (self->event_queue == NULL || self->input_queue == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate queues"));
		goto err;
	}

	self->event.parent = self;
	self->event.vmt = &keypad_layout_event_vmt;

	/* Constrain the upstream to raw events if it supports filtering. */
	if (self->conf.source->vmt->subscribe != NULL) {
		enum event_type raw = EV_TYPE_RAW;
		self->conf.source->vmt->subscribe(self->conf.source, &raw);
	}

	self->can_run = true;
	xTaskCreate(keypad_layout_proc_task, "keypad-layout-proc", configMINIMAL_STACK_SIZE + 128, (void *)self, 1,
	            &(self->proc_task));
	xTaskCreate(keypad_layout_rx_task, "keypad-layout-rx", configMINIMAL_STACK_SIZE + 128, (void *)self, 1,
	            &(self->rx_task));
	if (self->proc_task == NULL || self->rx_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create tasks"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return KEYPAD_LAYOUT_RET_OK;
err:
	keypad_layout_free(self);
	return KEYPAD_LAYOUT_RET_FAILED;
}


keypad_layout_ret_t keypad_layout_free(KeypadLayout *self) {
	if (u_assert(self != NULL)) {
		return KEYPAD_LAYOUT_RET_FAILED;
	}

	/* Stop both tasks. The processing task ticks and stops promptly; the receive task only observes
	 * can_run after the next upstream event. */
	/** @todo timeout */
	self->can_run = false;
	while (self->proc_running || self->rx_running) {
		vTaskDelay(100);
	}

	if (self->input_queue != NULL) {
		vQueueDelete(self->input_queue);
		self->input_queue = NULL;
	}
	if (self->event_queue != NULL) {
		vQueueDelete(self->event_queue);
		self->event_queue = NULL;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return KEYPAD_LAYOUT_RET_OK;
}
