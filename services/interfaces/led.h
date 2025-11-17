/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LED manipulation interface
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

typedef enum {
	LED_RET_OK = 0,
	LED_RET_FAILED,
} led_ret_t;

/**
 * @brief Type holding a 24bit color in a RGB color space
 *
 * The color is saved in a sequence of three 8 bit values (red, green, blue)
 * right-aligned in a 32 bit word, MSB first. For each channel, value 0x00
 * corresponds to a complete off (dark), 0xff corresponds to a complete on
 * (full illumination).
 *
 * 0x000000ff -> full blue
 * 0x0000ff00 -> full green
 * 0x00ff0000 -> full red
 */
typedef uint32_t led_color_t;
#define LED_COLOR_RGB(r, g, b) (((r) << 16 | (g) << 8 | (b)) & 0x00ffffffu)

/**
 * @brief Single item of a LED sequence
 *
 * LED sequences are simple arrays of 32 bit words allowing creating different
 * blink and fade patterns. Each sequence item/step is defined with a command
 * (end, wait, set, fade), a fade/delay time and a color.
 *
 * The following figure depicts the packed structure:
 * 32bit: RRRRRRRR GGGGGGGG BBBBBBBB TTTTTTCC
 *
 * R - red channel value
 * G - green channel value
 * B - blue channel value
 * T - time (fade/delay) in multiples of 16 ms (range 0 to 1008 ms)
 * C - command (LED_SEQ_END, WAIT, SET, FADE)
 *
 * Note that R, G and B color components are saved in the same format as in
 * @p led_color_t
 *
 * Examples of some common sequences can be found in led-sequences.h, eg.
 * a simple heartbeat-like sequence would be:
 *
 * led_seq_item_t heartbeat[] = {
 * 	LED_SEQ_SET | LED_SEQ_ON | LED_SEQ_TIME_MS(64),
 * 	LED_SEQ_SET | LED_SEQ_OFF | LED_SEQ_TIME_MS(128),
 * 	LED_SEQ_SET | LED_SEQ_ON | LED_SEQ_TIME_MS(64),
 * 	LED_SEQ_SET | LED_SEQ_OFF | LED_SEQ_TIME_MS(1008),
 * 	LED_SEQ_END
 * };
 */
typedef uint32_t led_seq_item_t;

/**
 * @brief Helper macro for inline sequences
 */
#define LED_SEQ (static const led_seq_item_t[])

/**
 * @brief LED blink sequence item commands/types
 *
 * Each item must have a type defined. The last item of a sequence must be END
 * (or simply, zero).
 */
#define LED_SEQ_END 0x0u
#define LED_SEQ_WAIT 0x1u
#define LED_SEQ_SET 0x2u
#define LED_SEQ_FADE 0x3u

/**
 * Time to wait for WAIT and SET types, fade transition time for the FADE type.
 */
#define LED_SEQ_TIME_MS(x) (((x / 16) & 0x0000003fu) << 2)

/**
 * State/brightness/color of the LED. Use only a single one of these.
 */
#define LED_SEQ_RGB(r, g, b) (LED_COLOR_RGB(r, g, b) << 8)
#define LED_SEQ_BRIGHTNESS(x) LED_SEQ_RGB(x, x, x)
#define LED_SEQ_ON LED_SEQ_BRIGHTNESS(255u)
#define LED_SEQ_OFF LED_SEQ_BRIGHTNESS(0u)


typedef struct led Led;
struct led_vmt {
	/**
	 * @brief Set LED static color
	 *
	 * Set a static color on the LED interface. Break any currently running
	 * sequences or other effects.
	 *
	 * NULL if not implemented.
	 *
	 * @return LED_RET_OK on success, LED_RET_FAILED on any error.
	 */
	led_ret_t (*set)(Led *self, led_color_t color);

	/**
	 * @brief Set blink/effect LED sequence
	 *
	 * Set a custom effect/blink sequence. The last item of the @p sequence
	 * array must be the @p LED_SEQ_END value.
	 *
	 * NULL if not implemented. May be partially implemented as a simple
	 * blink pattern if the implementation is constrained. If implemented,
	 * LED_SEQ_WAIT and LED_SEQ_SET are mandatory. LED_SEQ_FADE may be implemented
	 * as a simple set and wait if the implementation is constrained.
	 *
	 * @return LED_RET_OK on success, LED_RET_FAILED on any error.
	 */
	led_ret_t (*sequence)(Led *self, const led_seq_item_t *sequence);
};

typedef struct led {
	const struct led_vmt *vmt;
	void *parent;
} Led;

