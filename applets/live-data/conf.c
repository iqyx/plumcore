/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Live data applet configuration tree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * All live data applet configuration lives in a single Conf tree whose root is the "live-data" node, exposed as
 * the applet's static configuration (live_data.conf). Advertised as "applet live-data", it is mounted at
 * /applet/live-data:
 *
 *     /applet/live-data/default-tab                    tab shown when the applet is started (list, detail or graph)
 *     /applet/live-data/layout/detail/sections         number of detail tab sections (auto, 1, 2, 3 or 4)
 *     /applet/live-data/layout/detail/section0         sensor or topic shown in the section instead of the selection
 *     ...
 *     /applet/live-data/layout/detail/section3
 *     /applet/live-data/layout/detail/int-digits       integer digits the value is padded to (0 = no padding)
 *     /applet/live-data/layout/detail/frac-digits      fractional digits of the value
 *     /applet/live-data/layout/detail/leading-zeros    pad the integer part with zeros instead of blanks
 *     /applet/live-data/layout/detail/trailing-zeros   keep trailing zeros of the fractional part
 *     /applet/live-data/leds/enable                    show the test pattern on the top LEDs
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include <interfaces/conf.h>
#include <configlib.h>

#include "live-data.h"

/* Values of the default-tab enum, indexed by enum live_data_tab. */
static const char * const live_data_conf_tab_names[] = {
	[LIVE_DATA_TAB_LIST] = "list",
	[LIVE_DATA_TAB_DETAIL] = "detail",
	[LIVE_DATA_TAB_GRAPH] = "graph",
};

/* Values of the layout/detail/sections enum, indexed by the number of sections (0 = auto). */
static const char * const live_data_conf_sections_names[] = {
	"auto",
	"1",
	"2",
	"3",
	"4",
};

/* Names of the per-section override nodes, indexed by the section. */
static const char * const live_data_conf_section_names[] = {
	"section0",
	"section1",
	"section2",
	"section3",
};

struct live_data_conf live_data_conf;


applet_ret_t live_data_conf_init(Applet *self) {
	(void)self;

	/* Defaults, overridden once the stored configuration is loaded. */
	live_data_conf.settings.default_tab = LIVE_DATA_TAB_LIST;
	live_data_conf.settings.layout.detail.sections = 0;
	for (size_t i = 0; i < 4; i++) {
		live_data_conf.settings.layout.detail.section[i][0] = '\0';
	}
	live_data_conf.settings.layout.detail.int_digits = 0;
	live_data_conf.settings.layout.detail.frac_digits = 3;
	live_data_conf.settings.layout.detail.leading_zeros = false;
	live_data_conf.settings.layout.detail.trailing_zeros = false;
	live_data_conf.settings.leds.enable = false;

	configlib_init_map(&live_data_conf.tree.live_data, "live-data", NULL, CONF_SUBTREE);

	configlib_init_map_append(&live_data_conf.tree.default_tab, "default-tab", &live_data_conf.settings.default_tab,
		CONF_ENUM, &live_data_conf.tree.live_data, CONF_DIR_CHILD);
	live_data_conf.tree.default_tab.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.default_tab, (union conf_val){.u32 = LIVE_DATA_TAB_LIST});
	configlib_set_constraint(&live_data_conf.tree.default_tab, (union conf_constraint){
		.enumeration = {
			.values = live_data_conf_tab_names,
			.count = sizeof(live_data_conf_tab_names) / sizeof(live_data_conf_tab_names[0]),
		},
	});
	configlib_set_description(&live_data_conf.tree.default_tab, "Tab shown when the applet is started",
		"The tab the applet opens with: the sensor list, the detail view of the selected sensors or the graph. "
		"Changing it while the applet is running switches to the chosen tab immediately.");

	configlib_init_map_append(&live_data_conf.tree.layout, "layout", NULL, CONF_SUBTREE,
		&live_data_conf.tree.default_tab, CONF_DIR_NEXT);
	configlib_init_map_append(&live_data_conf.tree.layout_detail, "detail", NULL, CONF_SUBTREE,
		&live_data_conf.tree.layout, CONF_DIR_CHILD);

	configlib_init_map_append(&live_data_conf.tree.layout_detail_sections, "sections",
		&live_data_conf.settings.layout.detail.sections, CONF_ENUM, &live_data_conf.tree.layout_detail,
		CONF_DIR_CHILD);
	live_data_conf.tree.layout_detail_sections.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_sections, (union conf_val){.u32 = 0});
	configlib_set_constraint(&live_data_conf.tree.layout_detail_sections, (union conf_constraint){
		.enumeration = {
			.values = live_data_conf_sections_names,
			.count = sizeof(live_data_conf_sections_names) / sizeof(live_data_conf_sections_names[0]),
		},
	});
	configlib_set_description(&live_data_conf.tree.layout_detail_sections, "Number of detail tab sections",
		"The number of sections the detail tab is split into, one sensor value drawn in each. With auto there is "
		"one section per selected sensor (up to four). With a fixed number the layout stays the same regardless "
		"of the selection: the selected sensors fill the sections in order and any spare section is left empty.");

	/* Section overrides chained after sections, in order. */
	ConfiglibValue *prev = &live_data_conf.tree.layout_detail_sections;
	for (size_t i = 0; i < 4; i++) {
		ConfiglibValue *node = &live_data_conf.tree.layout_detail_section[i];
		configlib_init(node, live_data_conf_section_names[i]);
		configlib_map_string(node, live_data_conf.settings.layout.detail.section[i],
			sizeof(live_data_conf.settings.layout.detail.section[i]));
		configlib_append(node, prev, CONF_DIR_NEXT);
		node->flags = CONF_READ | CONF_WRITE;
		configlib_set_default(node, (union conf_val){.str = {.buf = (char *)"", .len = 0}});
		configlib_set_constraint(node, (union conf_constraint){
			.str.max_len = LIVE_DATA_MAX_TOPIC_LEN - 1,
		});
		configlib_set_description(node, "Sensor or topic shown in the section",
			"Name of the sensor or MQ topic drawn in this detail tab section, exactly as it is shown in the list, "
			"replacing the selected sensor the section would show otherwise. Until a topic of that name is "
			"received the section shows dashes. Empty shows the selection.");
		prev = node;
	}

	configlib_init_map_append(&live_data_conf.tree.layout_detail_int_digits, "int-digits",
		&live_data_conf.settings.layout.detail.int_digits, CONF_U8, prev, CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_int_digits.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_int_digits, (union conf_val){.u8 = 0});
	configlib_set_constraint(&live_data_conf.tree.layout_detail_int_digits, (union conf_constraint){
		.numeric = {.min = {.u8 = 0}, .max = {.u8 = 9}},
	});
	configlib_set_description(&live_data_conf.tree.layout_detail_int_digits, "Integer digits of the value",
		"Minimum number of integer digits the detail tab value is drawn with. A shorter integer part is padded "
		"on the left (with zeros or blanks, see leading-zeros) so the digits keep their positions, a longer one "
		"is drawn in full. 0 disables the padding.");

	configlib_init_map_append(&live_data_conf.tree.layout_detail_frac_digits, "frac-digits",
		&live_data_conf.settings.layout.detail.frac_digits, CONF_U8, &live_data_conf.tree.layout_detail_int_digits,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_frac_digits.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_frac_digits, (union conf_val){.u8 = 3});
	configlib_set_constraint(&live_data_conf.tree.layout_detail_frac_digits, (union conf_constraint){
		.numeric = {.min = {.u8 = 0}, .max = {.u8 = 6}},
	});
	configlib_set_description(&live_data_conf.tree.layout_detail_frac_digits, "Fractional digits of the value",
		"Number of digits after the decimal point the detail tab value is rounded to. 0 draws the value as an "
		"integer without the decimal point.");

	configlib_init_map_append(&live_data_conf.tree.layout_detail_leading_zeros, "leading-zeros",
		&live_data_conf.settings.layout.detail.leading_zeros, CONF_B, &live_data_conf.tree.layout_detail_frac_digits,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_leading_zeros.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_leading_zeros, (union conf_val){.b = false});
	configlib_set_description(&live_data_conf.tree.layout_detail_leading_zeros, "Pad the integer part with zeros",
		"When set, the integer part of the detail tab value is padded to int-digits with leading zeros, otherwise "
		"with blank digit positions. No effect when int-digits is 0.");

	configlib_init_map_append(&live_data_conf.tree.layout_detail_trailing_zeros, "trailing-zeros",
		&live_data_conf.settings.layout.detail.trailing_zeros, CONF_B, &live_data_conf.tree.layout_detail_leading_zeros,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_trailing_zeros.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_trailing_zeros, (union conf_val){.b = false});
	configlib_set_description(&live_data_conf.tree.layout_detail_trailing_zeros, "Keep trailing zeros",
		"When set, the detail tab value always shows all frac-digits fractional digits. When cleared, trailing "
		"zeros of the fractional part are dropped, together with the decimal point if nothing is left after it "
		"(1.500 is drawn as 1.5, 2.000 as 2).");

	configlib_init_map_append(&live_data_conf.tree.leds, "leds", NULL, CONF_SUBTREE, &live_data_conf.tree.layout,
		CONF_DIR_NEXT);

	configlib_init_map_append(&live_data_conf.tree.leds_enable, "enable", &live_data_conf.settings.leds.enable,
		CONF_B, &live_data_conf.tree.leds, CONF_DIR_CHILD);
	live_data_conf.tree.leds_enable.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.leds_enable, (union conf_val){.b = false});
	configlib_set_description(&live_data_conf.tree.leds_enable, "Show the LED test pattern",
		"When set, the top LEDs discovered by the applet show a repeating blue, green, red test pattern while the "
		"applet is running. When cleared, they stay off.");

	return APPLET_RET_OK;
}
