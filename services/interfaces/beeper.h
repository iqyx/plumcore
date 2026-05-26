/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Beeper manipulation interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

typedef enum {
	BEEPER_RET_OK = 0,
	BEEPER_RET_FAILED,
} beeper_ret_t;

/**
 * @brief Single item of a beeper sequence
 *
 * Beeper sequences are simple arrays of 32 bit words allowing creating different
 * beep patterns. Each sequence item/step is defined with a command (end, wait,
 * beep), a duration time and a frequency.
 *
 * The following figure depicts the packed structure:
 * 32bit: FFFFFFFFFFFFFF TTTTTTTTTTTTTTTT CC
 *
 * F - frequency in Hz (14 bits, range 1-16383 Hz, only valid for BEEP command)
 * T - time in ms (16 bits, range 0-65535 ms)
 * C - command (BEEPER_SEQ_END, BEEPER_SEQ_WAIT, BEEPER_SEQ_BEEP)
 *
 * Examples of some common sequences can be found in beeper-sequences.h, eg.
 * a simple short beep would be:
 *
 * beeper_seq_item_t alert[] = {
 * 	BEEPER_SEQ_BEEP | BEEPER_SEQ_FREQ_HZ(1000) | BEEPER_SEQ_TIME_MS(100),
 * 	BEEPER_SEQ_WAIT | BEEPER_SEQ_TIME_MS(900),
 * 	BEEPER_SEQ_END
 * };
 */
typedef uint32_t beeper_seq_item_t;

/**
 * @brief Helper macro for inline sequences
 */
#define BEEPER_SEQ (static const beeper_seq_item_t[])

/**
 * @brief Beeper sequence item commands/types
 *
 * Each item must have a command defined. The last item of a sequence must be END
 * (or simply, zero).
 */
#define BEEPER_SEQ_END  0x0u
#define BEEPER_SEQ_WAIT 0x1u
#define BEEPER_SEQ_BEEP 0x2u

/**
 * @brief Duration for WAIT (silence) and BEEP commands, in milliseconds.
 *
 * Valid range is 0-65535 ms.
 */
#define BEEPER_SEQ_TIME_MS(ms) (((uint32_t)(ms) & 0xffffu) << 2)

/**
 * @brief Frequency for the BEEP command, in Hz.
 *
 * Valid range is 1-16383 Hz. Only meaningful when combined with BEEPER_SEQ_BEEP.
 */
#define BEEPER_SEQ_FREQ_HZ(hz) (((uint32_t)(hz) & 0x3fffu) << 18)


typedef struct beeper Beeper;
struct beeper_vmt {
	/**
	 * @brief Set beeping sequence
	 *
	 * Set a custom beeping sequence. The last item of the @p sequence array
	 * must be the @p BEEPER_SEQ_END value.
	 *
	 * NULL if not implemented. If implemented, BEEPER_SEQ_WAIT and
	 * BEEPER_SEQ_BEEP are mandatory.
	 *
	 * @return BEEPER_RET_OK on success, BEEPER_RET_FAILED on any error.
	 */
	beeper_ret_t (*sequence)(Beeper *self, const beeper_seq_item_t *sequence);
};

typedef struct beeper {
	const struct beeper_vmt *vmt;
	void *parent;
} Beeper;
