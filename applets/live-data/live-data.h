/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Live data applet skeleton
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <interfaces/applet.h>
#include <configlib.h>

/* The applet presents its data through several tabs selected with the F1..F4 keys. The list, detail and
 * graph tabs are known; the fourth is a placeholder until its purpose is decided. */
enum live_data_tab {
	LIVE_DATA_TAB_LIST = 0,
	LIVE_DATA_TAB_DETAIL,
	LIVE_DATA_TAB_GRAPH,
	LIVE_DATA_TAB_TBD,
	LIVE_DATA_TAB_COUNT,
};

/* Maximum length of an MQ topic string kept as a live value's name, including the terminator. */
#define LIVE_DATA_MAX_TOPIC_LEN 48

/* Applet static configuration, its root exposed as live_data.conf. Built in conf.c, the nodes map the values in
 * settings. It outlives the applet instances, so the values persist between runs. */
struct live_data_conf {
	struct {
		ConfiglibValue live_data;
		ConfiglibValue default_tab;
		ConfiglibValue layout;
		ConfiglibValue layout_detail;
		ConfiglibValue layout_detail_sections;
		ConfiglibValue layout_detail_section[4];
		ConfiglibValue layout_detail_int_digits;
		ConfiglibValue layout_detail_frac_digits;
		ConfiglibValue layout_detail_leading_zeros;
		ConfiglibValue layout_detail_trailing_zeros;
		ConfiglibValue leds;
		ConfiglibValue leds_enable;
	} tree;

	struct {
		uint32_t default_tab;
		struct {
			struct {
				/* Number of detail tab sections, 0 = auto (one per selected sensor). */
				uint32_t sections;
				/* Sensor or topic name shown in each section instead of the selection, empty = no override. */
				char section[4][LIVE_DATA_MAX_TOPIC_LEN];
				/* Value formatting: integer digits padded to (0 = no padding), fractional digits, padding with
				 * zeros instead of blanks and keeping the trailing fractional zeros. */
				uint8_t int_digits;
				uint8_t frac_digits;
				bool leading_zeros;
				bool trailing_zeros;
			} detail;
		} layout;
		struct {
			/* Show the test pattern on the top LEDs. */
			bool enable;
		} leds;
	} settings;
};

extern const Applet live_data;
extern struct live_data_conf live_data_conf;

/* Set the applet settings to their defaults and build the configuration tree, used as live_data.conf_init. */
applet_ret_t live_data_conf_init(Applet *self);
