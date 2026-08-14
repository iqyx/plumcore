/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keypad layout event translator
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/event.h>


/* Maximum number of physical keys tracked simultaneously (down-state and in-flight presses). */
#define KEYPAD_LAYOUT_MAX_KEYS 32
#define KEYPAD_LAYOUT_EVENT_QUEUE_SIZE 8

/* Period of the processing task cycle. Held keys are revisited once per cycle to run counter
 * auto-repeat and long/very-long key classification. All counter timings are quantised to this. */
#define KEYPAD_LAYOUT_TICK_MS 100

/* Counting defaults used when the corresponding conf field is left 0. See struct keypad_layout_conf. */
#define KEYPAD_LAYOUT_COUNT_FIRST_MS 100
#define KEYPAD_LAYOUT_COUNT_START_MS 1000
#define KEYPAD_LAYOUT_COUNT_PERIOD_MS 500
#define KEYPAD_LAYOUT_COUNT_FAST_DELAY_MS 3000
#define KEYPAD_LAYOUT_COUNT_FAST_PERIOD_MS 100

typedef enum {
	KEYPAD_LAYOUT_RET_OK = 0,
	KEYPAD_LAYOUT_RET_FAILED,
	KEYPAD_LAYOUT_RET_NULL,
} keypad_layout_ret_t;


/**
 * A single layout entry mapping one raw input key (optionally qualified by a modifier) to an output
 * event. The output type is derived from the output code: EV_KEY_* codes are emitted as EV_TYPE_KEY
 * (discrete key events), EV_REL_* codes as EV_TYPE_REL (counting/relative events). Not typedef'd per
 * project policy.
 */
struct keypad_layout_item {
	/** Raw input event code that must be pressed to match this item (e.g. EV_RAW_0). */
	enum event_code in_code;
	/** Modifier raw code that must be held down when @p in_code is pressed. EV_CODE_NONE = no modifier. */
	enum event_code in_modifier;

	/** Output event code emitted on a short press. */
	enum event_code out_code;
	/** Output event code emitted on a long press. EV_CODE_NONE = fall back to @p out_code. */
	enum event_code out_code_long;
	/** Output event code emitted on a very long press. EV_CODE_NONE = fall back to long/short. */
	enum event_code out_code_very_long;

	/** Value carried by each emitted counter (EV_TYPE_REL) event, e.g. +1 to count up, -1 down. Ignored
	 *  for key (EV_TYPE_KEY) outputs, which always carry a press value of 1. */
	int32_t counter;
};


/* Service configuration passed to keypad_layout_init(). Not typedef'd per project policy. */
struct keypad_layout_conf {
	/** Upstream event source producing raw key events (EV_TYPE_RAW). */
	Event *source;
	/** Layout table, terminated by an item with @p in_code set to EV_CODE_NONE. */
	const struct keypad_layout_item *layout;

	/*
	 * Key (EV_TYPE_KEY) output timing. A key press is classified on release by how long it was held.
	 */
	/** Hold duration above which a press is classified as long. 0 disables long presses. */
	uint32_t long_press_ms;
	/** Hold duration above which a press is classified as very long. 0 disables very long presses. */
	uint32_t very_long_press_ms;

	/*
	 * Counter (EV_TYPE_REL) output timing. While a counter key is held, output events are emitted
	 * repeatedly: once shortly after the press, then at a slow rate, accelerating to a fast rate once
	 * held long enough. Any field left 0 falls back to its KEYPAD_LAYOUT_COUNT_* default.
	 */
	/** Delay from press to the first emitted count. */
	uint32_t count_first_ms;
	/** Delay from press to the second count, where the auto-repeat begins. */
	uint32_t count_start_ms;
	/** Interval between counts during slow (initial) auto-repeat. */
	uint32_t count_period_ms;
	/** Hold duration above which counting switches to the fast rate. */
	uint32_t count_fast_delay_ms;
	/** Interval between counts during fast auto-repeat. */
	uint32_t count_fast_period_ms;
};


/* An event carried on a queue: raw input from the receive task, or output to the Event interface. */
struct keypad_layout_event {
	enum event_type type;
	enum event_code code;
	int32_t value;
};

/* An input key currently held down that matched a layout item on its press. */
struct keypad_layout_press {
	enum event_code in_code;
	const struct keypad_layout_item *item;
	/** Number of processing cycles elapsed since the press; drives all output timing. */
	uint32_t cycles;
	/** At least one counter (EV_TYPE_REL) event has been emitted for this press. */
	bool emitted_count;
	/** The long-press key event has been emitted. */
	bool emitted_long;
	/** The very-long-press key event has been emitted. */
	bool emitted_very_long;
};

typedef struct {
	struct keypad_layout_conf conf;

	/* Codes currently observed to be down, used to evaluate modifier conditions. Touched only by the
	 * processing task. */
	enum event_code down_keys[KEYPAD_LAYOUT_MAX_KEYS];
	size_t down_count;

	/* Layout items currently matched by a held key. Touched only by the processing task. */
	struct keypad_layout_press presses[KEYPAD_LAYOUT_MAX_KEYS];
	size_t press_count;

	/* Downstream Event interface and the queue feeding its listen(). */
	Event event;
	QueueHandle_t event_queue;

	/* Raw input events forwarded from the receive task to the processing task. */
	QueueHandle_t input_queue;

	/* rx_task turns the blocking upstream into queued events; proc_task ticks and produces output. */
	TaskHandle_t rx_task;
	TaskHandle_t proc_task;
	volatile bool can_run;
	volatile bool rx_running;
	volatile bool proc_running;
} KeypadLayout;


keypad_layout_ret_t keypad_layout_init(KeypadLayout *self, const struct keypad_layout_conf *conf);
keypad_layout_ret_t keypad_layout_free(KeypadLayout *self);
