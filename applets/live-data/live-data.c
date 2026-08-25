/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Live data applet skeleton
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/applet.h>
#include <interfaces/event.h>
#include <interfaces/fb.h>
#include <interfaces/led.h>
#include <interfaces/mq.h>
#include <interfaces/painter.h>
#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <services/fb-painter/fb-painter.h>

/* The applet requires the fb-painter service (see its Kconfig depends), so its assets are always available. */
#include <services/fb-painter/assets/assets.h>

#define MODULE_NAME "live-data"

/* Height of the tab bar drawn along the bottom edge of the window, in pixels. */
#define LIVE_DATA_TAB_BAR_H 16

/* List tab layout: the height of a single sensor row, the width of the scroll bar drawn along the right
 * edge, and the upper bound on the number of sensors the list holds. All dimensions are in pixels. */
#define LIVE_DATA_ROW_H 12
#define LIVE_DATA_SCROLLBAR_W 8
#define LIVE_DATA_MAX_SENSORS 32

/* Number of top LEDs (led0..led11) the applet discovers through the service locator. */
#define LIVE_DATA_MAX_LEDS 12

/* Side of the check_small image drawn in the rightmost list column marking a sensor as selected, in pixels. */
#define LIVE_DATA_SEL_BOX 10

/* Thickness of a seven-segment stroke and the vertical padding of the big value on the detail tab. */
#define LIVE_DATA_SEG_THICK 4
#define LIVE_DATA_DETAIL_PAD 8

/* Period at which the polling task refreshes the cached sensor values, in milliseconds. */
#define LIVE_DATA_POLL_INTERVAL_MS 200

/* Maximum length of an MQ topic string kept as a live value's name, including the terminator. */
#define LIVE_DATA_MAX_TOPIC_LEN 48

/* The MQ listener wakes at least this often to notice a stop request while blocked on receive. */
#define LIVE_DATA_MQ_RX_TIMEOUT_MS 500

/* Size of the receive buffer backing the MQ listener's ndarray, in bytes. Large enough for the small
 * scalar messages the applet cares about; longer arrays are received truncated and only the first
 * element is used. */
#define LIVE_DATA_MQ_BUF_BYTES 128

/* The applet presents its data through several tabs selected with the F1..F4 keys. The list, detail and
 * graph tabs are known; the fourth is a placeholder until its purpose is decided. */
enum live_data_tab {
	LIVE_DATA_TAB_LIST = 0,
	LIVE_DATA_TAB_DETAIL,
	LIVE_DATA_TAB_GRAPH,
	LIVE_DATA_TAB_TBD,
	LIVE_DATA_TAB_COUNT,
};

/* Short label shown for each tab in the tab bar, indexed by enum live_data_tab. */
static const char *live_data_tab_labels[LIVE_DATA_TAB_COUNT] = {
	[LIVE_DATA_TAB_LIST] = "List",
	[LIVE_DATA_TAB_DETAIL] = "Detail",
	[LIVE_DATA_TAB_GRAPH] = "Graph",
	[LIVE_DATA_TAB_TBD] = "?",
};

/* A single live value shown in the list: either a Sensor discovered through the service locator or a
 * topic seen on the message queue (sensor == NULL). For a sensor the name is kept as returned by the
 * locator (the pointer is stable for the sensor's lifetime) and the value is cached by the polling task;
 * for an MQ topic the name points at the owned topic buffer and the value is cached by the MQ listener.
 * Either way redraws only read the cached value, never the sensor interface. */
struct live_data_sensor {
	Sensor *sensor;                  /* discovered sensor, or NULL for an MQ topic */
	const char *name;
	char topic[LIVE_DATA_MAX_TOPIC_LEN]; /* owned storage the name points at for MQ topics */
	float value;
	bool valid;                      /* false until the first successful read */
	bool selected;                   /* toggled by ENTER, shown as a filled box in the list tab */
};

typedef struct live_data_applet {
	Fb *fb;                          /* window framebuffer the applet draws into */
	Event *event;                    /* input event source delivering key presses */
	Window *window;                  /* window the applet runs in (may be NULL), used to set the title */
	FbPainter painter;              /* painter bound to the framebuffer */
	uint16_t w;                      /* framebuffer dimensions in pixels */
	uint16_t h;
	enum live_data_tab tab;          /* currently selected tab */

	/* Sensors advertised through the service locator, captured once at init, with their cached values. */
	struct live_data_sensor sensors[LIVE_DATA_MAX_SENSORS];
	size_t sensor_count;
	size_t selected;                 /* index of the highlighted sensor in the list tab */
	size_t list_top;                 /* index of the first sensor row visible in the list tab */

	/* Top LEDs (led0..led11) discovered through the service locator, captured once at init. */
	Led *led[LIVE_DATA_MAX_LEDS];
	size_t led_count;

	/* Background task refreshing the cached sensor values once a second. */
	TaskHandle_t poll_task;
	volatile bool poll_can_run;      /* cleared to ask the task to stop */
	volatile bool poll_running;      /* set while the task is alive */

	/* Message queue discovered through the service locator, the subscriber opened on it and the buffer
	 * incoming messages are received into. All NULL/unused when no queue is available. */
	Mq *mq;
	MqClient *mqc;
	NdArray mq_buf;

	/* Background task subscribing to every topic and mirroring published values into the sensor list. */
	TaskHandle_t mq_task;
	volatile bool mq_can_run;        /* cleared to ask the task to stop */
	volatile bool mq_running;        /* set while the task is alive */
} LiveDataApplet;


/* Draw the tab bar along the bottom LIVE_DATA_TAB_BAR_H pixels of the window, its width split evenly into
 * LIVE_DATA_TAB_COUNT cells. The selected tab is drawn as black text on a white background, the others as
 * white text on a black, white-bordered rectangle. The caller must have an active painter frame. */
static void live_data_draw_tabs(LiveDataApplet *self) {
	Painter *painter = &self->painter.painter;
	int16_t bar_y = (int16_t)(self->h - LIVE_DATA_TAB_BAR_H);
	uint16_t cell_w = (uint16_t)(self->w / LIVE_DATA_TAB_COUNT);

	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
	for (enum live_data_tab i = 0; i < LIVE_DATA_TAB_COUNT; i++) {
		int16_t cell_x = (int16_t)(i * cell_w);
		/* The last cell takes the remaining width so rounding never leaves a gap on the right. */
		uint16_t this_w = (i == LIVE_DATA_TAB_COUNT - 1) ? (uint16_t)(self->w - cell_x) : cell_w;
		bool selected = (i == self->tab);

		/* Cell background: a solid white fill for the selected tab, a black fill with a white 1px border
		 * for the others. */
		painter->vmt->set_pen(painter, 0xffffffff, 1);
		painter->vmt->set_brush(painter, selected ? 0xffffffff : 0xff000000);
		painter->vmt->rect(painter, cell_x, bar_y, this_w, LIVE_DATA_TAB_BAR_H);

		/* Label centred within the cell, black on the selected tab and white on the others. */
		const char *label = live_data_tab_labels[i];
		uint16_t tw = 0;
		uint16_t th = 0;
		painter->vmt->text_size(painter, label, &tw, &th);
		int16_t tx = (int16_t)(cell_x + (this_w - tw) / 2);
		int16_t ty = (int16_t)(bar_y + (LIVE_DATA_TAB_BAR_H - th) / 2);
		painter->vmt->set_pen(painter, selected ? 0xff000000 : 0xffffffff, 1);
		painter->vmt->text(painter, tx, ty, label);
	}
}


/* Height of the content area above the tab bar, in pixels. */
static uint16_t live_data_content_h(LiveDataApplet *self) {
	return (uint16_t)(self->h - LIVE_DATA_TAB_BAR_H);
}


/* Number of sensor rows that fit in the list tab content area at once. */
static size_t live_data_visible_rows(LiveDataApplet *self) {
	return live_data_content_h(self) / LIVE_DATA_ROW_H;
}


/* Draw the vertical scroll bar along the right edge of the content area, exactly like the gui-launcher one:
 * a black-filled, white-bordered track with a white-filled scroller whose height and position reflect how
 * much of the sensor list is visible and how far it is scrolled. When everything fits the scroller fills
 * the whole track. The caller must have an active painter frame. */
static void live_data_draw_scrollbar(LiveDataApplet *self) {
	Painter *painter = &self->painter.painter;
	int16_t bar_x = (int16_t)(self->w - LIVE_DATA_SCROLLBAR_W);
	uint16_t bar_h = live_data_content_h(self);

	/* Track: black fill, white 1px border. */
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, bar_x, 0, LIVE_DATA_SCROLLBAR_W, bar_h);

	/* Total list height (all rows) versus the content height visible at once. */
	int32_t content_h = (int32_t)self->sensor_count * LIVE_DATA_ROW_H;
	int32_t inner_h = (int32_t)bar_h - 2;
	int32_t y_scroll = (int32_t)self->list_top * LIVE_DATA_ROW_H;

	/* Scroller sized and positioned proportionally within the track's 1px inset. */
	int32_t thumb_h = inner_h;
	int32_t thumb_y = 1;
	if (content_h > bar_h) {
		thumb_h = inner_h * bar_h / content_h;
		thumb_y = 1 + (inner_h - thumb_h) * y_scroll / (content_h - bar_h);
	}

	painter->vmt->set_pen(painter, 0xffffffff, 0);
	painter->vmt->set_brush(painter, 0xffffffff);
	painter->vmt->rect(painter, (int16_t)(bar_x + 1), (int16_t)thumb_y,
	                   (uint16_t)(LIVE_DATA_SCROLLBAR_W - 2), (uint16_t)thumb_h);
}


/* Format a sensor value into buf with up to three decimal places, dropping any trailing zeros and a trailing
 * decimal point so a whole number renders without a fractional part (e.g. 1.500 -> "1.5", 2.000 -> "2"). */
static void live_data_format_value(char *buf, size_t size, float value) {
	snprintf(buf, size, "%.3f", value);
	if (strchr(buf, '.') != NULL) {
		char *end = buf + strlen(buf) - 1;
		while (end > buf && *end == '0') {
			*end-- = '\0';
		}
		if (*end == '.') {
			*end = '\0';
		}
	}
}


/* Draw the sensor list into the content area: one row per sensor with the Name, Value and Unit columns,
 * scrolled to self->list_top. The selected row is inverted (a white rectangle with black text), the others
 * are white text on the black background. A scroll bar is drawn along the right edge. The caller must have
 * an active painter frame. */
static void live_data_draw_list(LiveDataApplet *self) {
	Painter *painter = &self->painter.painter;
	uint16_t list_w = (uint16_t)(self->w - LIVE_DATA_SCROLLBAR_W);

	/* Rightmost column: the selection box, with the text columns laid out in the space left of it. */
	int16_t sel_x = (int16_t)(list_w - LIVE_DATA_SEL_BOX - 2);
	uint16_t text_w = (uint16_t)(sel_x - 2);

	/* Columns: the name takes the left half, the value the next 30% and the unit the rightmost 20%. */
	int16_t name_x = 2;
	int16_t value_x = (int16_t)(text_w * 50 / 100);
	int16_t unit_x = (int16_t)(text_w * 80 / 100);
	uint16_t name_w = (uint16_t)(value_x - name_x - 2);
	uint16_t value_w = (uint16_t)(unit_x - value_x - 2);
	uint16_t unit_w = (uint16_t)(sel_x - unit_x - 2);

	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);

	size_t visible = live_data_visible_rows(self);
	for (size_t row = 0; row < visible; row++) {
		size_t i = self->list_top + row;
		if (i >= self->sensor_count) {
			break;
		}
		struct live_data_sensor *s = &self->sensors[i];
		int16_t y = (int16_t)(row * LIVE_DATA_ROW_H);
		bool selected = (i == self->selected);

		/* Invert the selected row: a white rectangle spanning the list width, with black text on top. */
		if (selected) {
			painter->vmt->set_pen(painter, 0xffffffff, 0);
			painter->vmt->set_brush(painter, 0xffffffff);
			painter->vmt->rect(painter, 0, y, list_w, LIVE_DATA_ROW_H);
		}
		painter->vmt->set_pen(painter, selected ? 0xff000000 : 0xffffffff, 1);

		/* Name, cropped to its column so it never spills into the value column. */
		char name[32];
		strncpy(name, (s->name != NULL) ? s->name : "", sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		fb_painter_crop_text(&self->painter, name, name_w);
		painter->vmt->text(painter, name_x, (int16_t)(y + 1), name);

		/* Cached value, shown as three dashes until the first successful read. */
		char value[16];
		if (s->valid) {
			live_data_format_value(value, sizeof(value), s->value);
		} else {
			strcpy(value, "---");
		}
		fb_painter_crop_text(&self->painter, value, value_w);
		painter->vmt->text(painter, value_x, (int16_t)(y + 1), value);

		/* Unit abbreviation from the sensor info; MQ topics carry no unit. */
		char unit[12];
		strncpy(unit, (s->sensor != NULL && s->sensor->info != NULL && s->sensor->info->unit != NULL) ?
		        s->sensor->info->unit : "", sizeof(unit) - 1);
		unit[sizeof(unit) - 1] = '\0';
		fb_painter_crop_text(&self->painter, unit, unit_w);
		painter->vmt->text(painter, unit_x, (int16_t)(y + 1), unit);

		/* Selection mark in the rightmost column, centred in the row: the check_small image drawn only for a
		 * selected sensor. The source image is stored inverted, so it is blitted inverted on the normal black
		 * background and straight through on the highlighted white row to flip colour along with the row. */
		if (s->selected) {
			int16_t box_y = (int16_t)(y + (LIVE_DATA_ROW_H - LIVE_DATA_SEL_BOX) / 2);
			painter->vmt->image(painter, sel_x, box_y, &check_small_data,
			                    selected ? PAINTER_MODE_NORMAL : PAINTER_MODE_INVERTED);
		}
	}

	live_data_draw_scrollbar(self);
}


/* Seven-segment layout, one bit per segment:
 *
 *      aaa
 *     f   b
 *     f   b
 *      ggg
 *     e   c
 *     e   c
 *      ddd
 */
#define LIVE_DATA_SEG_A 0x01
#define LIVE_DATA_SEG_B 0x02
#define LIVE_DATA_SEG_C 0x04
#define LIVE_DATA_SEG_D 0x08
#define LIVE_DATA_SEG_E 0x10
#define LIVE_DATA_SEG_F 0x20
#define LIVE_DATA_SEG_G 0x40

/* Segments lit for each supported glyph: the digits 0..9 and a dash. Anything else is blank. */
static uint8_t live_data_digit_segments(char c) {
	switch (c) {
		case '0': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_B | LIVE_DATA_SEG_C | LIVE_DATA_SEG_D | LIVE_DATA_SEG_E | LIVE_DATA_SEG_F;
		case '1': return LIVE_DATA_SEG_B | LIVE_DATA_SEG_C;
		case '2': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_B | LIVE_DATA_SEG_G | LIVE_DATA_SEG_E | LIVE_DATA_SEG_D;
		case '3': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_B | LIVE_DATA_SEG_G | LIVE_DATA_SEG_C | LIVE_DATA_SEG_D;
		case '4': return LIVE_DATA_SEG_F | LIVE_DATA_SEG_G | LIVE_DATA_SEG_B | LIVE_DATA_SEG_C;
		case '5': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_F | LIVE_DATA_SEG_G | LIVE_DATA_SEG_C | LIVE_DATA_SEG_D;
		case '6': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_F | LIVE_DATA_SEG_G | LIVE_DATA_SEG_E | LIVE_DATA_SEG_C | LIVE_DATA_SEG_D;
		case '7': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_B | LIVE_DATA_SEG_C;
		case '8': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_B | LIVE_DATA_SEG_C | LIVE_DATA_SEG_D | LIVE_DATA_SEG_E | LIVE_DATA_SEG_F | LIVE_DATA_SEG_G;
		case '9': return LIVE_DATA_SEG_A | LIVE_DATA_SEG_B | LIVE_DATA_SEG_C | LIVE_DATA_SEG_D | LIVE_DATA_SEG_F | LIVE_DATA_SEG_G;
		case '-': return LIVE_DATA_SEG_G;
		default: return 0;
	}
}


/* Stroke thickness for a seven-segment glyph of cell width w: a quarter of the width so the strokes shrink
 * to 3 or 2 pixels for small digits (which stay legible only with thin strokes), capped at LIVE_DATA_SEG_THICK
 * for large ones and never below 2. */
static uint16_t live_data_seg_thick(uint16_t w) {
	uint16_t t = (uint16_t)(w / 4);
	if (t > LIVE_DATA_SEG_THICK) {
		t = LIVE_DATA_SEG_THICK;
	}
	if (t < 2) {
		t = 2;
	}
	return t;
}


/* Draw one seven-segment glyph (a digit 0..9 or a dash) as white rectangles inside the w x h box with its
 * top-left corner at (x, y). The segment thickness scales with the digit width; the horizontal segments are
 * inset by the thickness at both ends so the corners read as a real seven-segment display. The caller must
 * have an active painter frame. */
static void live_data_draw_digit(Painter *painter, char c, int16_t x, int16_t y, uint16_t w, uint16_t h) {
	uint8_t seg = live_data_digit_segments(c);
	uint16_t t = live_data_seg_thick(w);
	int16_t half = (int16_t)((h - t) / 2);   /* y offset of the middle segment */
	uint16_t hseg_w = (uint16_t)(w - 2 * t); /* horizontal segment length */
	uint16_t vseg_h = (uint16_t)(half - t);  /* vertical segment length in each half */

	painter->vmt->set_pen(painter, 0xffffffff, 0);
	painter->vmt->set_brush(painter, 0xffffffff);

	/* Horizontal segments: top, middle and bottom. */
	if (seg & LIVE_DATA_SEG_A) {
		painter->vmt->rect(painter, (int16_t)(x + t), y, hseg_w, t);
	}
	if (seg & LIVE_DATA_SEG_G) {
		painter->vmt->rect(painter, (int16_t)(x + t), (int16_t)(y + half), hseg_w, t);
	}
	if (seg & LIVE_DATA_SEG_D) {
		painter->vmt->rect(painter, (int16_t)(x + t), (int16_t)(y + h - t), hseg_w, t);
	}

	/* Vertical segments: the two in the top half then the two in the bottom half. */
	if (seg & LIVE_DATA_SEG_F) {
		painter->vmt->rect(painter, x, (int16_t)(y + t), t, vseg_h);
	}
	if (seg & LIVE_DATA_SEG_B) {
		painter->vmt->rect(painter, (int16_t)(x + w - t), (int16_t)(y + t), t, vseg_h);
	}
	if (seg & LIVE_DATA_SEG_E) {
		painter->vmt->rect(painter, x, (int16_t)(y + half + t), t, vseg_h);
	}
	if (seg & LIVE_DATA_SEG_C) {
		painter->vmt->rect(painter, (int16_t)(x + w - t), (int16_t)(y + half + t), t, vseg_h);
	}
}


/* Horizontal advance of one glyph in a seven-segment string: a decimal point is a narrow column, every
 * other glyph is a full w-wide cell, and each is followed by spacing. */
static uint16_t live_data_digit_advance(char c, uint16_t w, uint16_t spacing) {
	if (c == '.') {
		return (uint16_t)(w / 4 + spacing);
	}
	return (uint16_t)(w + spacing);
}


/* Total width a seven-segment string occupies when drawn with digit cell width w and the given spacing,
 * without the trailing gap after the last glyph. */
static uint16_t live_data_digits_width(const char *s, uint16_t w, uint16_t spacing) {
	uint16_t total = 0;
	for (const char *p = s; *p != '\0'; p++) {
		total = (uint16_t)(total + live_data_digit_advance(*p, w, spacing));
	}
	if (total >= spacing) {
		total = (uint16_t)(total - spacing);
	}
	return total;
}


/* Draw a string of seven-segment glyphs left to right starting at (x, y): the digits 0..9 and a dash are
 * full glyphs, a '.' is a small square on the baseline, and any other character advances a cell without
 * drawing. The caller must have an active painter frame. */
static void live_data_draw_digits(Painter *painter, const char *s, int16_t x, int16_t y, uint16_t w,
                                  uint16_t h, uint16_t spacing) {
	for (const char *p = s; *p != '\0'; p++) {
		if (*p == '.') {
			/* Decimal point: a small square sitting on the glyph baseline. */
			uint16_t dot = (uint16_t)(w / 4);
			uint16_t min_dot = live_data_seg_thick(w);
			if (dot < min_dot) {
				dot = min_dot;
			}
			painter->vmt->set_pen(painter, 0xffffffff, 0);
			painter->vmt->set_brush(painter, 0xffffffff);
			painter->vmt->rect(painter, x, (int16_t)(y + h - dot), dot, dot);
		} else {
			live_data_draw_digit(painter, *p, x, y, w, h);
		}
		x = (int16_t)(x + live_data_digit_advance(*p, w, spacing));
	}
}


/* Draw one sensor's detail view into the cell at (cx, cy) of size cw x ch: the current value as big
 * seven-segment digits centred in the cell with its unit just to the right. The caller must have an active
 * painter frame. */
static void live_data_draw_detail_cell(LiveDataApplet *self, struct live_data_sensor *s, int16_t cx,
                                       int16_t cy, uint16_t cw, uint16_t ch, bool show_name) {
	Painter *painter = &self->painter.painter;

	/* The current value as big seven-segment digits, shown as three dashes until the first successful read. */
	char value[16];
	if (s->valid) {
		live_data_format_value(value, sizeof(value), s->value);
	} else {
		strcpy(value, "---");
	}

	/* Measure the unit first (bold font) so the digits can be sized to leave room for it on the right. MQ
	 * topics carry no unit. */
	const char *unit = (s->sensor != NULL && s->sensor->info != NULL && s->sensor->info->unit != NULL) ?
	                   s->sensor->info->unit : "";
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	uint16_t unit_w = 0;
	uint16_t unit_h = 0;
	painter->vmt->text_size(painter, unit, &unit_w, &unit_h);

	/* Fill the cell height leaving LIVE_DATA_DETAIL_PAD above and below; digits are half as wide as they
	 * are tall. If the value would not fit the width remaining after the unit, scale the digits down to fit
	 * while keeping the aspect ratio. */
	uint16_t reserve = (unit_w > 0) ? (uint16_t)(unit_w + 4) : 0;
	uint16_t avail = (uint16_t)(cw - 2 * LIVE_DATA_DETAIL_PAD - reserve);
	uint16_t digit_h = (uint16_t)(ch - 2 * LIVE_DATA_DETAIL_PAD);
	uint16_t digit_w = (uint16_t)(digit_h / 2);
	uint16_t spacing = (uint16_t)(digit_w / 6);
	uint16_t total = live_data_digits_width(value, digit_w, spacing);
	if (total > avail && total > 0) {
		digit_w = (uint16_t)((uint32_t)digit_w * avail / total);
		digit_h = (uint16_t)(digit_w * 2);
		spacing = (uint16_t)(digit_w / 6);
		total = live_data_digits_width(value, digit_w, spacing);
	}

	/* Centre the value + unit group horizontally, and the digits vertically within the cell. */
	int16_t value_x = (int16_t)(cx + (cw - (total + reserve)) / 2);
	int16_t value_y = (int16_t)(cy + (ch - digit_h) / 2);
	live_data_draw_digits(painter, value, value_x, value_y, digit_w, digit_h, spacing);

	/* Unit to the right of the value, in the bold font (still selected from the measurement above), sitting
	 * on the digits' baseline. */
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->text(painter, (int16_t)(value_x + total + 4), (int16_t)(value_y + digit_h - unit_h), unit);

	/* When the area is split across several sensors, label each cell with its sensor name in the top-left
	 * corner using the normal font, cropped to the cell width. */
	if (show_name) {
		char name[32];
		strncpy(name, (s->name != NULL) ? s->name : "", sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
		fb_painter_crop_text(&self->painter, name, (uint16_t)(cw - 4));
		painter->vmt->set_pen(painter, 0xffffffff, 1);
		painter->vmt->text(painter, (int16_t)(cx + 2), (int16_t)(cy + 1), name);
	}
}


/* Fill sel[] with the indices of up to max selected sensors, i.e. those with their box ticked in the list.
 * When none are ticked the highlighted sensor stands in as a single entry. Returns the number written, which
 * is zero only when there are no sensors at all. */
static size_t live_data_selected_list(LiveDataApplet *self, size_t *sel, size_t max) {
	size_t n = 0;
	for (size_t i = 0; i < self->sensor_count && n < max; i++) {
		if (self->sensors[i].selected) {
			sel[n++] = i;
		}
	}
	if (n == 0 && self->sensor_count > 0) {
		sel[0] = self->selected;
		n = 1;
	}
	return n;
}


/* Drive the top LEDs from the currently selected sensors: three LEDs per selected sensor (up to four sensors,
 * twelve LEDs) addressed as left, middle and right. As a simple test, a value below 4000000 lights the trio's
 * left LED blue, a value above 6000000 its right LED red, and anything in between its middle LED green; the
 * rest of the trio stays dark. The final colour of every LED is computed first and then written once, so a
 * LED keeping its colour is never briefly turned off (which would show as a blink). Called from the redraw so
 * the LEDs track the display. */
static void live_data_update_leds(LiveDataApplet *self) {
	if (self->led_count == 0) {
		return;
	}

	/* Target colour of every LED, defaulting to off; trios not backing a selected sensor stay dark. */
	led_color_t color[LIVE_DATA_MAX_LEDS];
	for (size_t i = 0; i < self->led_count; i++) {
		color[i] = LED_COLOR_RGB(0, 0, 0);
	}

	size_t sel[4];
	size_t n = live_data_selected_list(self, sel, 4);
	for (size_t k = 0; k < n; k++) {
		struct live_data_sensor *s = &self->sensors[sel[k]];
		size_t base = k * 3;
		if (!s->valid || base + 2 >= self->led_count) {
			continue;
		}
		if (s->value < 4000000.0f) {
			color[base] = LED_COLOR_RGB(0, 0, 255);
		} else if (s->value > 6000000.0f) {
			color[base + 2] = LED_COLOR_RGB(255, 0, 0);
		} else {
			color[base + 1] = LED_COLOR_RGB(0, 255, 0);
		}
	}

	for (size_t i = 0; i < self->led_count; i++) {
		self->led[i]->vmt->set(self->led[i], color[i]);
	}
}


/* Draw the detail tab, one cell per selected sensor. The selected sensors are those with their box ticked in
 * the list, capped at four; when none are ticked the highlighted sensor stands in as a single selection. The
 * content area is split by that count: the whole area for one, two stacked rows for two, and a 2x2 grid for
 * three or four (any selection beyond the fourth is ignored). The caller must have an active painter frame. */
static void live_data_draw_detail(LiveDataApplet *self) {
	Painter *painter = &self->painter.painter;

	if (self->sensor_count == 0) {
		return;
	}

	/* Collect up to four selected sensors; fall back to the highlighted one when nothing is ticked. */
	size_t sel[4];
	size_t n = live_data_selected_list(self, sel, 4);

	/* One column for one or two sensors, two for three or four; one row for a single sensor, two otherwise. */
	uint16_t cols = (n >= 3) ? 2 : 1;
	uint16_t rows = (n >= 2) ? 2 : 1;
	uint16_t area_h = live_data_content_h(self);
	uint16_t cell_w = (uint16_t)(self->w / cols);
	uint16_t cell_h = (uint16_t)(area_h / rows);

	/* Thin white dividers between the cells so the split areas read apart. */
	painter->vmt->set_pen(painter, 0xffffffff, 0);
	painter->vmt->set_brush(painter, 0xffffffff);
	if (cols > 1) {
		painter->vmt->rect(painter, (int16_t)cell_w, 0, 1, area_h);
	}
	if (rows > 1) {
		painter->vmt->rect(painter, 0, (int16_t)cell_h, self->w, 1);
	}

	for (size_t k = 0; k < n; k++) {
		uint16_t col = (uint16_t)(k % cols);
		uint16_t row = (uint16_t)(k / cols);
		int16_t cx = (int16_t)(col * cell_w);
		int16_t cy = (int16_t)(row * cell_h);
		/* The last column/row takes the remaining space so rounding never leaves a gap on the edge. */
		uint16_t cw = (col == cols - 1) ? (uint16_t)(self->w - cx) : cell_w;
		uint16_t ch = (row == rows - 1) ? (uint16_t)(area_h - cy) : cell_h;
		live_data_draw_detail_cell(self, &self->sensors[sel[k]], cx, cy, cw, ch, n > 1);
	}
}


/* Fully repaint the window: clear the content area above the tab bar, draw the active tab's content and
 * draw the tab bar reflecting the currently selected tab. */
static void live_data_redraw(LiveDataApplet *self) {
	Painter *painter = &self->painter.painter;

	painter->vmt->begin(painter);

	/* Clear the content area above the tab bar to a black background. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, self->w, live_data_content_h(self));

	switch (self->tab) {
		case LIVE_DATA_TAB_LIST:
			live_data_draw_list(self);
			break;
		case LIVE_DATA_TAB_DETAIL:
			live_data_draw_detail(self);
			break;
		default:
			/* The other tabs have no content yet. */
	}

	live_data_draw_tabs(self);

	painter->vmt->end(painter);

	/* Mirror the selected sensors onto the top LEDs so they track whatever the display shows. */
	live_data_update_leds(self);
}


/* Snapshot all sensors advertised through the service locator into self->sensors, keeping each sensor's
 * name as returned by the locator. The cached values start invalid until the polling task fills them. */
static void live_data_capture_sensors(LiveDataApplet *self) {
	self->sensor_count = 0;
	Sensor *sensor = NULL;
	for (size_t i = 0; self->sensor_count < LIVE_DATA_MAX_SENSORS &&
	     iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_SENSOR, i, (Interface **)&sensor) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = "";
		iservicelocator_get_name(locator, (Interface *)sensor, &name);
		self->sensors[self->sensor_count].sensor = sensor;
		self->sensors[self->sensor_count].name = name;
		self->sensors[self->sensor_count].value = 0.0f;
		self->sensors[self->sensor_count].valid = false;
		self->sensor_count++;
	}
}


/* Snapshot the top LEDs advertised through the service locator (led0..led11) into self->led, up to
 * LIVE_DATA_MAX_LEDS. A port that advertises no LEDs simply leaves the array empty. */
static void live_data_capture_leds(LiveDataApplet *self) {
	self->led_count = 0;
	Led *led = NULL;
	for (size_t i = 0; self->led_count < LIVE_DATA_MAX_LEDS &&
	     iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_LED, i, (Interface **)&led) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		self->led[self->led_count] = led;
		self->led_count++;
	}
}


/* Polling task: every LIVE_DATA_POLL_INTERVAL_MS refresh every sensor's cached value and, while the list
 * tab is on screen, repaint it so the displayed values keep up. Runs until asked to stop by live_data_free. */
static void live_data_poll_task(void *p) {
	LiveDataApplet *self = (LiveDataApplet *)p;

	self->poll_running = true;
	while (self->poll_can_run) {
		for (size_t i = 0; i < self->sensor_count; i++) {
			struct live_data_sensor *s = &self->sensors[i];
			/* MQ topics keep the value cached by the listener task; only real sensors are polled. */
			if (s->sensor == NULL) {
				continue;
			}
			float v = 0.0f;
			if (s->sensor->vmt->value_f != NULL && s->sensor->vmt->value_f(s->sensor, &v) == SENSOR_RET_OK) {
				s->value = v;
				s->valid = true;
			} else {
				s->valid = false;
			}
		}
		/* Repaint the tabs that show live values so the refreshed readings appear. */
		if (self->tab == LIVE_DATA_TAB_LIST || self->tab == LIVE_DATA_TAB_DETAIL) {
			live_data_redraw(self);
		}
		vTaskDelay(pdMS_TO_TICKS(LIVE_DATA_POLL_INTERVAL_MS));
	}
	self->poll_running = false;
	vTaskDelete(NULL);
}


/* Read the first element of a received ndarray as a float, converting from whatever numeric dtype the
 * publisher used. Returns false for an empty or non-numeric array. */
static bool live_data_ndarray_to_float(const NdArray *a, float *out) {
	if (a->buf == NULL || a->asize == 0) {
		return false;
	}
	switch (a->dtype) {
		case DTYPE_INT8:
			*out = (float)((int8_t *)a->buf)[0];
			return true;
		case DTYPE_BYTE:
		case DTYPE_UINT8:
			*out = (float)((uint8_t *)a->buf)[0];
			return true;
		case DTYPE_INT16:
			*out = (float)((int16_t *)a->buf)[0];
			return true;
		case DTYPE_UINT16:
			*out = (float)((uint16_t *)a->buf)[0];
			return true;
		case DTYPE_INT32:
			*out = (float)((int32_t *)a->buf)[0];
			return true;
		case DTYPE_UINT32:
			*out = (float)((uint32_t *)a->buf)[0];
			return true;
		case DTYPE_INT64:
			*out = (float)((int64_t *)a->buf)[0];
			return true;
		case DTYPE_UINT64:
			*out = (float)((uint64_t *)a->buf)[0];
			return true;
		case DTYPE_FLOAT:
			*out = ((float *)a->buf)[0];
			return true;
		case DTYPE_DOUBLE:
			*out = (float)((double *)a->buf)[0];
			return true;
		default:
			return false;
	}
}


/* Record a value received under topic into the sensor list: refresh the cached value of the matching MQ
 * entry, or append a new one when the topic is not yet known and the list still has room. Sensor entries
 * are never matched, so a topic sharing a sensor's name still gets its own row. A newly appended entry is
 * fully populated before sensor_count is bumped so the poll task and the drawing code, which only iterate
 * up to sensor_count, never observe a half-built row. Called only from the MQ listener task. */
static void live_data_record_mq_value(LiveDataApplet *self, const char *topic, float value) {
	for (size_t i = 0; i < self->sensor_count; i++) {
		struct live_data_sensor *s = &self->sensors[i];
		if (s->sensor == NULL && !strcmp(s->name, topic)) {
			s->value = value;
			s->valid = true;
			return;
		}
	}
	if (self->sensor_count >= LIVE_DATA_MAX_SENSORS) {
		return;
	}
	struct live_data_sensor *s = &self->sensors[self->sensor_count];
	s->sensor = NULL;
	snprintf(s->topic, sizeof(s->topic), "%s", topic);
	s->name = s->topic;
	s->value = value;
	s->valid = true;
	self->sensor_count++;
}


/* MQ listener task: subscribe to every topic and block waiting for values. Each received value refreshes
 * the cached reading of its topic in the sensor list, adding a new row for a previously unseen topic while
 * the list has room. Those cached values are what the list and detail tabs show for MQ topics (the poll
 * task never queries them); the periodic poll-task redraw is what brings them on screen. Runs until asked
 * to stop by live_data_free. */
static void live_data_mq_task(void *p) {
	LiveDataApplet *self = (LiveDataApplet *)p;

	self->mq_running = true;

	/* Wake often enough to notice a stop request even when no messages arrive, and receive everything. */
	self->mqc->vmt->set_timeout(self->mqc, LIVE_DATA_MQ_RX_TIMEOUT_MS);
	self->mqc->vmt->subscribe(self->mqc, "#");

	while (self->mq_can_run) {
		struct timespec ts = {0};
		char topic[LIVE_DATA_MAX_TOPIC_LEN] = {0};
		self->mq_buf.asize = 0;
		mq_ret_t ret = self->mqc->vmt->receive(self->mqc, topic, sizeof(topic), &self->mq_buf, &ts);
		if (ret != MQ_RET_OK && ret != MQ_RET_OK_TRUNCATED) {
			continue;
		}
		float value = 0.0f;
		if (!live_data_ndarray_to_float(&self->mq_buf, &value)) {
			continue;
		}
		live_data_record_mq_value(self, topic, value);
	}
	self->mq_running = false;
	vTaskDelete(NULL);
}


/* Discover a message queue through the service locator and start the listener task that mirrors its
 * published values into the sensor list. A missing queue is not an error: the applet then shows only the
 * sensors it discovered. On any failure after a queue was found the partially acquired resources are
 * released so the caller can ignore the result. */
static applet_ret_t live_data_start_mq(LiveDataApplet *self) {
	self->mq = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&self->mq) !=
	    ISERVICELOCATOR_RET_OK) {
		return APPLET_RET_OK;
	}

	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		return APPLET_RET_FAILED;
	}
	if (ndarray_init_empty(&self->mq_buf, DTYPE_DOUBLE, LIVE_DATA_MQ_BUF_BYTES / sizeof(double)) !=
	    NDARRAY_RET_OK) {
		self->mqc->vmt->close(self->mqc);
		self->mqc = NULL;
		return APPLET_RET_FAILED;
	}

	self->mq_can_run = true;
	if (xTaskCreate(live_data_mq_task, "live-data-mq", configMINIMAL_STACK_SIZE + 512, (void *)self, 1,
	    &self->mq_task) != pdPASS) {
		self->mq_can_run = false;
		ndarray_free(&self->mq_buf);
		self->mqc->vmt->close(self->mqc);
		self->mqc = NULL;
		return APPLET_RET_FAILED;
	}

	return APPLET_RET_OK;
}


/* Number of sensors with their selection box ticked in the list. */
static size_t live_data_selected_count(LiveDataApplet *self) {
	size_t n = 0;
	for (size_t i = 0; i < self->sensor_count; i++) {
		if (self->sensors[i].selected) {
			n++;
		}
	}
	return n;
}


/* Set the window title to "Live data - <highlighted sensor name>", reflecting the current selection. When
 * the detail tab shows several selected sensors at once no single name applies, so the title is just
 * "Live data". Does nothing when the applet has no window (no compositor) or no sensors. */
static void live_data_update_title(LiveDataApplet *self) {
	if (self->window == NULL || self->sensor_count == 0) {
		return;
	}
	char title[48];
	if (self->tab == LIVE_DATA_TAB_DETAIL && live_data_selected_count(self) > 1) {
		snprintf(title, sizeof(title), "Live data");
	} else {
		const char *name = self->sensors[self->selected].name;
		snprintf(title, sizeof(title), "Live data - %s", (name != NULL) ? name : "");
	}
	self->window->vmt->set_title(self->window, title);
}


/* Set up the applet instance from the runner-provided arguments: bind the framebuffer and event source,
 * start the painter, capture the framebuffer dimensions, discover the sensors, select the initial tab and
 * start the polling task refreshing the cached sensor values. */
static applet_ret_t live_data_init(LiveDataApplet *self, struct applet_args *args) {
	memset(self, 0, sizeof(LiveDataApplet));

	if (args->fb == NULL || args->event == NULL) {
		return APPLET_RET_FAILED;
	}
	self->fb = args->fb;
	self->event = args->event;
	self->window = args->window;
	self->tab = LIVE_DATA_TAB_LIST;

	struct fb_stat stat = {0};
	if (self->fb->vmt->stat(self->fb, &stat) != FB_RET_OK) {
		return APPLET_RET_FAILED;
	}
	self->w = (uint16_t)stat.w;
	self->h = (uint16_t)stat.h;

	if (fb_painter_init(&self->painter, self->fb) != FB_PAINTER_RET_OK) {
		return APPLET_RET_FAILED;
	}

	live_data_capture_sensors(self);
	live_data_capture_leds(self);
	live_data_update_title(self);

	self->poll_can_run = true;
	if (xTaskCreate(live_data_poll_task, "live-data-poll", configMINIMAL_STACK_SIZE + 512, (void *)self, 1,
	    &self->poll_task) != pdPASS) {
		self->poll_can_run = false;
		fb_painter_free(&self->painter);
		return APPLET_RET_FAILED;
	}

	/* Best-effort: mirror message-queue topics into the list too. A missing queue leaves the applet
	 * showing only the discovered sensors, and any partial failure cleans up after itself. */
	live_data_start_mq(self);

	return APPLET_RET_OK;
}


/* Release everything live_data_init acquired: stop the background tasks, close the MQ subscriber and free
 * its buffer, then free the painter. */
static void live_data_free(LiveDataApplet *self) {
	self->poll_can_run = false;
	self->mq_can_run = false;
	while (self->poll_running || self->mq_running) {
		vTaskDelay(pdMS_TO_TICKS(100));
	}
	/* The tasks have stopped, so nothing will re-light the LEDs: turn them all off as we leave. */
	for (size_t i = 0; i < self->led_count; i++) {
		self->led[i]->vmt->set(self->led[i], LED_COLOR_RGB(0, 0, 0));
	}
	if (self->mqc != NULL) {
		self->mqc->vmt->close(self->mqc);
		self->mqc = NULL;
		ndarray_free(&self->mq_buf);
	}
	fb_painter_free(&self->painter);
}


/* Move the list selection by delta rows (positive = down, negative = up), clamped to the list, keeping the
 * newly selected row in view by scrolling list_top as needed. The window title tracks the selection. */
static void live_data_move_selection(LiveDataApplet *self, int32_t delta) {
	if (self->sensor_count == 0) {
		return;
	}
	int32_t target = (int32_t)self->selected + delta;
	if (target < 0) {
		target = 0;
	}
	if (target >= (int32_t)self->sensor_count) {
		target = (int32_t)self->sensor_count - 1;
	}
	if ((size_t)target == self->selected) {
		return;
	}
	self->selected = (size_t)target;
	live_data_update_title(self);

	/* Scroll just enough to bring the selected row fully into view at either end. */
	size_t visible = live_data_visible_rows(self);
	if (self->selected < self->list_top) {
		self->list_top = self->selected;
	}
	if (visible > 0 && self->selected >= self->list_top + visible) {
		self->list_top = self->selected - visible + 1;
	}
}


/* Block for and process a single input event: F1..F4 select the corresponding tab, a relative REL_X count
 * moves the list selection up or down, and both are reflected by a redraw. Only key presses (and relative
 * moves) are acted on. Returns false when the user requested to quit (ESC), true to keep running. */
static bool live_data_process_event(LiveDataApplet *self) {
	enum event_type type = EV_TYPE_NONE;
	enum event_code code = EV_CODE_NONE;
	int32_t value = 0;
	if (self->event->vmt->listen(self->event, &type, &code, &value) != EV_RET_OK) {
		return true;
	}

	/* Relative movement (a counter knob/keypad) changes the selected sensor: + next, - previous. Both the
	 * list and detail tabs reflect the selection, so repaint either of them. */
	if (type == EV_TYPE_REL && code == EV_REL_X) {
		live_data_move_selection(self, value);
		if (self->tab == LIVE_DATA_TAB_LIST || self->tab == LIVE_DATA_TAB_DETAIL) {
			live_data_redraw(self);
		}
		return true;
	}

	if (type != EV_TYPE_KEY || value != 1) {
		return true;
	}

	if (code == EV_KEY_ESC) {
		return false;
	}

	/* ENTER toggles the selected flag of the highlighted sensor and repaints the list to show the change. */
	if (code == EV_KEY_ENTER) {
		if (self->tab == LIVE_DATA_TAB_LIST && self->sensor_count > 0) {
			self->sensors[self->selected].selected = !self->sensors[self->selected].selected;
			live_data_redraw(self);
		}
		return true;
	}

	enum live_data_tab tab = self->tab;
	switch (code) {
		case EV_KEY_F1:
			tab = LIVE_DATA_TAB_LIST;
			break;
		case EV_KEY_F2:
			tab = LIVE_DATA_TAB_DETAIL;
			break;
		case EV_KEY_F3:
			tab = LIVE_DATA_TAB_GRAPH;
			break;
		case EV_KEY_F4:
			tab = LIVE_DATA_TAB_TBD;
			break;
		default:
			/* passthrough */
	}
	if (tab != self->tab) {
		self->tab = tab;
		live_data_update_title(self);
		live_data_redraw(self);
	}
	return true;
}


static applet_ret_t live_data_main(Applet *self, struct applet_args *args) {
	(void)self;
	if (args->logger != NULL) {
		u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("live data applet started"));
	}

	LiveDataApplet app;
	if (live_data_init(&app, args) != APPLET_RET_OK) {
		return APPLET_RET_FAILED;
	}
	live_data_redraw(&app);

	while (live_data_process_event(&app)) {
		/* Keep processing input until the user quits. */
	}

	live_data_free(&app);
	return APPLET_RET_OK;
}


const Applet live_data = {
	.executable.native = {
		.main = live_data_main
	},
	.name = "Live data",
	.help = "Display live measurement data",
	.stack_size = 1536,
	.icon = &graph_data,
};
