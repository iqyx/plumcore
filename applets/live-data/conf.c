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
 *     /applet/live-data/window-title                   title of the applet window
 *     /applet/live-data/top-caption                    text of the caption bar at the top, empty = no bar
 *     /applet/live-data/default-tab                    tab shown when the applet is started (list, detail or graph)
 *     /applet/live-data/layout/detail/sections         number of detail tab sections (auto, 1, 2, 3 or 4)
 *     /applet/live-data/layout/detail/section0/topic   topic filter of the sensor shown instead of the selection
 *     /applet/live-data/layout/detail/section0/caption sed substitution (or plain text) making the shown caption
 *     ...
 *     /applet/live-data/layout/detail/section3/...
 *     /applet/live-data/layout/detail/int-digits       integer digits the value is padded to (0 = no padding)
 *     /applet/live-data/layout/detail/frac-digits      fractional digits of the value
 *     /applet/live-data/layout/detail/leading-zeros    pad the integer part with zeros instead of blanks
 *     /applet/live-data/layout/detail/trailing-zeros   keep trailing zeros of the fractional part
 *     /applet/live-data/layout/detail/ul-min           lowest valid value, below it ul-text is shown
 *     /applet/live-data/layout/detail/ul-text          text shown instead of a value below ul-min (underload)
 *     /applet/live-data/layout/detail/ol-max           highest valid value, above it ol-text is shown
 *     /applet/live-data/layout/detail/ol-text          text shown instead of a value above ol-max (overload)
 *     /applet/live-data/leds/enable                    show the test pattern on the top LEDs
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

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
	strcpy(live_data_conf.settings.window_title, "Live data");
	live_data_conf.settings.top_caption[0] = '\0';
	live_data_conf.settings.default_tab = LIVE_DATA_TAB_LIST;
	live_data_conf.settings.layout.detail.sections = 0;
	for (size_t i = 0; i < 4; i++) {
		live_data_conf.settings.layout.detail.section[i].topic[0] = '\0';
		live_data_conf.settings.layout.detail.section[i].caption[0] = '\0';
	}
	live_data_conf.settings.layout.detail.int_digits = 0;
	live_data_conf.settings.layout.detail.frac_digits = 3;
	live_data_conf.settings.layout.detail.leading_zeros = false;
	live_data_conf.settings.layout.detail.trailing_zeros = false;
	live_data_conf.settings.layout.detail.ul_min = -INFINITY;
	strcpy(live_data_conf.settings.layout.detail.ul_text, "UL");
	live_data_conf.settings.layout.detail.ol_max = INFINITY;
	strcpy(live_data_conf.settings.layout.detail.ol_text, "OL");
	live_data_conf.settings.leds.enable = false;

	configlib_init_map(&live_data_conf.tree.live_data, "live-data", NULL, CONF_SUBTREE);

	configlib_init(&live_data_conf.tree.window_title, "window-title");
	configlib_map_string(&live_data_conf.tree.window_title, live_data_conf.settings.window_title,
		sizeof(live_data_conf.settings.window_title));
	configlib_append(&live_data_conf.tree.window_title, &live_data_conf.tree.live_data, CONF_DIR_CHILD);
	live_data_conf.tree.window_title.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.window_title,
		(union conf_val){.str = {.buf = (char *)"Live data", .len = 9}});
	configlib_set_constraint(&live_data_conf.tree.window_title, (union conf_constraint){
		.str.max_len = LIVE_DATA_MAX_CAPTION_LEN - 1,
	});
	configlib_set_description(&live_data_conf.tree.window_title, "Title of the applet window",
		"Text shown in the title bar of the applet window, followed by the name of the highlighted sensor. "
		"Changing it while the applet is running updates the title immediately.");

	configlib_init(&live_data_conf.tree.top_caption, "top-caption");
	configlib_map_string(&live_data_conf.tree.top_caption, live_data_conf.settings.top_caption,
		sizeof(live_data_conf.settings.top_caption));
	configlib_append(&live_data_conf.tree.top_caption, &live_data_conf.tree.window_title, CONF_DIR_NEXT);
	live_data_conf.tree.top_caption.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.top_caption, (union conf_val){.str = {.buf = (char *)"", .len = 0}});
	configlib_set_constraint(&live_data_conf.tree.top_caption, (union conf_constraint){
		.str.max_len = LIVE_DATA_MAX_CAPTION_LEN - 1,
	});
	configlib_set_description(&live_data_conf.tree.top_caption, "Caption bar at the top of the window",
		"Text drawn in a bar along the top of the applet window, above the tab content which is shrunk to make "
		"room for it. Empty hides the bar and the content takes the whole window.");

	configlib_init_map_append(&live_data_conf.tree.default_tab, "default-tab", &live_data_conf.settings.default_tab,
		CONF_ENUM, &live_data_conf.tree.top_caption, CONF_DIR_NEXT);
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

	/* Section subtrees chained after sections, in order. */
	ConfiglibValue *prev = &live_data_conf.tree.layout_detail_sections;
	for (size_t i = 0; i < 4; i++) {
		configlib_init_map_append(&live_data_conf.tree.layout_detail_section[i].section,
			live_data_conf_section_names[i], NULL, CONF_SUBTREE, prev, CONF_DIR_NEXT);

		configlib_init(&live_data_conf.tree.layout_detail_section[i].topic, "topic");
		configlib_map_string(&live_data_conf.tree.layout_detail_section[i].topic,
			live_data_conf.settings.layout.detail.section[i].topic,
			sizeof(live_data_conf.settings.layout.detail.section[i].topic));
		configlib_append(&live_data_conf.tree.layout_detail_section[i].topic,
			&live_data_conf.tree.layout_detail_section[i].section, CONF_DIR_CHILD);
		live_data_conf.tree.layout_detail_section[i].topic.flags = CONF_READ | CONF_WRITE;
		configlib_set_default(&live_data_conf.tree.layout_detail_section[i].topic,
			(union conf_val){.str = {.buf = (char *)"", .len = 0}});
		configlib_set_constraint(&live_data_conf.tree.layout_detail_section[i].topic, (union conf_constraint){
			.str.max_len = LIVE_DATA_MAX_TOPIC_LEN - 1,
		});
		configlib_set_description(&live_data_conf.tree.layout_detail_section[i].topic,
			"Sensor or topic shown in the section",
			"Topic filter (with the + and # wildcards) of the sensor or MQ topic drawn in this detail tab section, "
			"eg. math/force0/# matching both math/force0 and math/force0/<sn>. The most recently updated matching "
			"name in the list is shown with its value, replacing the selected sensor the section would show "
			"otherwise. Until a matching topic is received the section shows dashes. Empty shows the selection.");

		configlib_init(&live_data_conf.tree.layout_detail_section[i].caption, "caption");
		configlib_map_string(&live_data_conf.tree.layout_detail_section[i].caption,
			live_data_conf.settings.layout.detail.section[i].caption,
			sizeof(live_data_conf.settings.layout.detail.section[i].caption));
		configlib_append(&live_data_conf.tree.layout_detail_section[i].caption,
			&live_data_conf.tree.layout_detail_section[i].topic, CONF_DIR_NEXT);
		live_data_conf.tree.layout_detail_section[i].caption.flags = CONF_READ | CONF_WRITE;
		configlib_set_default(&live_data_conf.tree.layout_detail_section[i].caption,
			(union conf_val){.str = {.buf = (char *)"", .len = 0}});
		configlib_set_constraint(&live_data_conf.tree.layout_detail_section[i].caption, (union conf_constraint){
			.str.max_len = LIVE_DATA_MAX_SECTION_CAPTION_LEN - 1,
		});
		configlib_set_description(&live_data_conf.tree.layout_detail_section[i].caption,
			"Caption of the section",
			"Text shown in the section label instead of the sensor or topic name. A sed substitution command "
			"s/regex/replacement/[g] is applied to the shown name, eg. s/.*\\/\\([^/]*\\)$/Force \\1/ turns "
			"math/force0/2026.abcd into Force 2026.abcd. Basic regular expressions are supported, with groups "
			"referenced as \\1 to \\9 and & in the replacement. Any other text is shown verbatim. Empty shows the "
			"name as is.");

		prev = &live_data_conf.tree.layout_detail_section[i].section;
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

	configlib_init_map_append(&live_data_conf.tree.layout_detail_ul_min, "ul-min",
		&live_data_conf.settings.layout.detail.ul_min, CONF_F, &live_data_conf.tree.layout_detail_trailing_zeros,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_ul_min.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_ul_min, (union conf_val){.f = -INFINITY});
	configlib_set_description(&live_data_conf.tree.layout_detail_ul_min, "Underload threshold",
		"Lowest valid value. Values below it are an underload and the detail tab shows ul-text instead of them. "
		"Applies to all sections alike, a simplified substitute for the valid range of each sensor stated by its "
		"calibration certificate. Defaults to -inf (no lower limit).");

	configlib_init(&live_data_conf.tree.layout_detail_ul_text, "ul-text");
	configlib_map_string(&live_data_conf.tree.layout_detail_ul_text, live_data_conf.settings.layout.detail.ul_text,
		sizeof(live_data_conf.settings.layout.detail.ul_text));
	configlib_append(&live_data_conf.tree.layout_detail_ul_text, &live_data_conf.tree.layout_detail_ul_min,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_ul_text.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_ul_text,
		(union conf_val){.str = {.buf = (char *)"UL", .len = 2}});
	configlib_set_constraint(&live_data_conf.tree.layout_detail_ul_text, (union conf_constraint){
		.str.max_len = LIVE_DATA_MAX_RANGE_TEXT_LEN - 1,
	});
	configlib_set_description(&live_data_conf.tree.layout_detail_ul_text, "Text shown for underload values",
		"Shown on the detail tab instead of a value below ul-min (underload), in the same font as the value.");

	configlib_init_map_append(&live_data_conf.tree.layout_detail_ol_max, "ol-max",
		&live_data_conf.settings.layout.detail.ol_max, CONF_F, &live_data_conf.tree.layout_detail_ul_text,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_ol_max.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_ol_max, (union conf_val){.f = INFINITY});
	configlib_set_description(&live_data_conf.tree.layout_detail_ol_max, "Overload threshold",
		"Highest valid value. Values above it are an overload and the detail tab shows ol-text instead of them. "
		"Applies to all sections alike, a simplified substitute for the valid range of each sensor stated by its "
		"calibration certificate. Defaults to inf (no upper limit).");

	configlib_init(&live_data_conf.tree.layout_detail_ol_text, "ol-text");
	configlib_map_string(&live_data_conf.tree.layout_detail_ol_text, live_data_conf.settings.layout.detail.ol_text,
		sizeof(live_data_conf.settings.layout.detail.ol_text));
	configlib_append(&live_data_conf.tree.layout_detail_ol_text, &live_data_conf.tree.layout_detail_ol_max,
		CONF_DIR_NEXT);
	live_data_conf.tree.layout_detail_ol_text.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&live_data_conf.tree.layout_detail_ol_text,
		(union conf_val){.str = {.buf = (char *)"OL", .len = 2}});
	configlib_set_constraint(&live_data_conf.tree.layout_detail_ol_text, (union conf_constraint){
		.str.max_len = LIVE_DATA_MAX_RANGE_TEXT_LEN - 1,
	});
	configlib_set_description(&live_data_conf.tree.layout_detail_ol_text, "Text shown for overload values",
		"Shown on the detail tab instead of a value above ol-max (overload), in the same font as the value.");

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
