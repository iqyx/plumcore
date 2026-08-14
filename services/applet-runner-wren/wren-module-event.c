/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: input events (`Event`)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Preamble module exposing the input event source as an `Event` foreign class. Like the logger and the
 * framebuffer, the applet does not construct one directly; it receives the instance wrapping its own
 * event source (args->event, the compositor window's event source when running windowed) through
 * `args.event`. The wait() method mirrors the C listen() vmt call: it blocks until the next event and
 * caches its type, code and value on the instance, which the getters then expose. The event type and
 * key codes are surfaced as static constants named exactly like the C enums (EV_TYPE_KEY, EV_KEY_ESC,
 * ...) and backed by them, so scripts stay in sync with the enum definitions.
 */

#include <stdbool.h>
#include <string.h>

#include "wren.h"
#include "wren-module.h"

#include <interfaces/event.h>

/* The foreign data stored with an Event instance: a borrowed pointer to the applet's event source plus
 * the last event wait() received, exposed through the type/code/value getters. */
struct wren_module_event {
	Event *event;
	enum event_type type;
	enum event_code code;
	int32_t value;
};


/* Event.new(): wrap the running applet's own event source (args->event). The constructor takes no
 * arguments; the source always comes from the applet's arguments. */
static void wren_module_event_allocate(WrenVM *vm) {
	struct wren_module_event *self = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*self));
	memset(self, 0, sizeof(*self));
	self->event = wren_module_state(vm)->args->event;
}


/* Event.wait(): block until the next event, cache it and return true; return false when there is no
 * event source or the listen call fails. */
static void wren_module_event_wait(WrenVM *vm) {
	struct wren_module_event *self = wrenGetSlotForeign(vm, 0);
	self->type = EV_TYPE_NONE;
	self->code = EV_CODE_NONE;
	self->value = 0;
	if (self->event == NULL ||
	    self->event->vmt->listen(self->event, &self->type, &self->code, &self->value) != EV_RET_OK) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	wrenSetSlotBool(vm, 0, true);
}


static void wren_module_event_type(WrenVM *vm) {
	wrenSetSlotDouble(vm, 0, (double)((struct wren_module_event *)wrenGetSlotForeign(vm, 0))->type);
}


static void wren_module_event_code(WrenVM *vm) {
	wrenSetSlotDouble(vm, 0, (double)((struct wren_module_event *)wrenGetSlotForeign(vm, 0))->code);
}


static void wren_module_event_value(WrenVM *vm) {
	wrenSetSlotDouble(vm, 0, (double)((struct wren_module_event *)wrenGetSlotForeign(vm, 0))->value);
}


/* Static getters, one per exposed enum constant, so the Wren-side names stay backed by the C enums
 * instead of hard-coded literals that would drift when the enum is reordered. */
static void wren_module_event_ev_type_key(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_TYPE_KEY); }
static void wren_module_event_ev_type_rel(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_TYPE_REL); }
static void wren_module_event_ev_key_enter(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_ENTER); }
static void wren_module_event_ev_key_esc(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_ESC); }
static void wren_module_event_ev_key_up(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_UP); }
static void wren_module_event_ev_key_down(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_DOWN); }
static void wren_module_event_ev_key_left(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_LEFT); }
static void wren_module_event_ev_key_right(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_RIGHT); }
static void wren_module_event_ev_key_f1(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_F1); }
static void wren_module_event_ev_key_f2(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_F2); }
static void wren_module_event_ev_key_f3(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_F3); }
static void wren_module_event_ev_key_f4(WrenVM *vm) { wrenSetSlotDouble(vm, 0, (double)EV_KEY_F4); }


static WrenForeignMethodFn wren_module_event_bind_method(const char *class_name, bool is_static,
    const char *signature) {
	if (strcmp(class_name, "Event")) {
		return NULL;
	}
	if (!is_static) {
		if (!strcmp(signature, "wait()")) {
			return wren_module_event_wait;
		}
		if (!strcmp(signature, "type")) {
			return wren_module_event_type;
		}
		if (!strcmp(signature, "code")) {
			return wren_module_event_code;
		}
		if (!strcmp(signature, "value")) {
			return wren_module_event_value;
		}
		return NULL;
	}
	if (!strcmp(signature, "EV_TYPE_KEY")) {
		return wren_module_event_ev_type_key;
	}
	if (!strcmp(signature, "EV_TYPE_REL")) {
		return wren_module_event_ev_type_rel;
	}
	if (!strcmp(signature, "EV_KEY_ENTER")) {
		return wren_module_event_ev_key_enter;
	}
	if (!strcmp(signature, "EV_KEY_ESC")) {
		return wren_module_event_ev_key_esc;
	}
	if (!strcmp(signature, "EV_KEY_UP")) {
		return wren_module_event_ev_key_up;
	}
	if (!strcmp(signature, "EV_KEY_DOWN")) {
		return wren_module_event_ev_key_down;
	}
	if (!strcmp(signature, "EV_KEY_LEFT")) {
		return wren_module_event_ev_key_left;
	}
	if (!strcmp(signature, "EV_KEY_RIGHT")) {
		return wren_module_event_ev_key_right;
	}
	if (!strcmp(signature, "EV_KEY_F1")) {
		return wren_module_event_ev_key_f1;
	}
	if (!strcmp(signature, "EV_KEY_F2")) {
		return wren_module_event_ev_key_f2;
	}
	if (!strcmp(signature, "EV_KEY_F3")) {
		return wren_module_event_ev_key_f3;
	}
	if (!strcmp(signature, "EV_KEY_F4")) {
		return wren_module_event_ev_key_f4;
	}
	return NULL;
}


static bool wren_module_event_bind_class(const char *class_name, WrenForeignClassMethods *methods) {
	if (!strcmp(class_name, "Event")) {
		methods->allocate = wren_module_event_allocate;
		methods->finalize = NULL;
		return true;
	}
	return false;
}


const struct wren_module wren_module_event = {
	.name = NULL,
	.source =
		"foreign class Event {\n"
		"  construct new() {}\n"
		"  foreign wait()\n"
		"  foreign type\n"
		"  foreign code\n"
		"  foreign value\n"
		"  foreign static EV_TYPE_KEY\n"
		"  foreign static EV_TYPE_REL\n"
		"  foreign static EV_KEY_ENTER\n"
		"  foreign static EV_KEY_ESC\n"
		"  foreign static EV_KEY_UP\n"
		"  foreign static EV_KEY_DOWN\n"
		"  foreign static EV_KEY_LEFT\n"
		"  foreign static EV_KEY_RIGHT\n"
		"  foreign static EV_KEY_F1\n"
		"  foreign static EV_KEY_F2\n"
		"  foreign static EV_KEY_F3\n"
		"  foreign static EV_KEY_F4\n"
		"}\n",
	.bind_method = wren_module_event_bind_method,
	.bind_class = wren_module_event_bind_class,
};
