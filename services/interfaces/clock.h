/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic Clock interface
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file clock.h
 * @brief Generic clock provider interface
 *
 * This interface abstracts hardware and software clocks following the Linux
 * Common Clock Framework (CCF) conventions. It covers two orthogonal concerns:
 *
 *  - **Timekeeping** — reading and adjusting wall-clock time (@ref clock_vmt::get,
 *    @ref clock_vmt::set, @ref clock_vmt::shift, @ref clock_vmt::adjust).
 *
 *  - **Clock tree management** — controlling oscillators, PLLs and clock gates
 *    in a parent/child hierarchy (@ref clock_vmt::prepare, @ref clock_vmt::enable,
 *    @ref clock_vmt::set_rate, @ref clock_vmt::set_parent, …).
 *
 * Not every implementation is required to support all operations. Unsupported
 * operations should leave the corresponding VMT pointer NULL; callers must
 * check for NULL before invoking.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>


/**
 * @brief Return codes for all clock interface operations.
 */
typedef enum {
	/** Operation completed successfully. */
	CLOCK_RET_OK = 0,
	/** Operation failed (generic error). */
	CLOCK_RET_FAILED,
	/** An argument was invalid (e.g. an unsupported parent clock). */
	CLOCK_RET_BAD_ARG,
} clock_ret_t;


typedef struct clock Clock;

/**
 * @brief Virtual method table for the Clock interface.
 *
 * Each function pointer corresponds to a single clock operation. A NULL pointer
 * means the operation is not implemented by the driver.
 */
struct clock_vmt {
	/**
	 * @brief Set the absolute wall-clock time.
	 *
	 * Analogous to POSIX @c clock_settime(). The caller provides the new
	 * time as a @c struct @c timespec. The change takes effect immediately.
	 *
	 * @note Not all clocks maintain a counter or expose a way to latch an
	 *       absolute time value. Pure frequency sources (oscillators, PLLs,
	 *       clock gates) fall into this category and cannot serve as a wall
	 *       clock. For such clocks this pointer is NULL.
	 *
	 * @param self  Clock instance.
	 * @param time  New time to set. Must not be NULL.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*set)(Clock *self, const struct timespec *time);

	/**
	 * @brief Read the current wall-clock time.
	 *
	 * Analogous to POSIX @c clock_gettime(). Fills @p time with the current
	 * value of the clock.
	 *
	 * @note See the note on @ref clock_vmt::set — the same restriction applies.
	 *
	 * @param self  Clock instance.
	 * @param time  Output buffer for the current time. Must not be NULL.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*get)(Clock *self, struct timespec *time);

	/**
	 * @brief Apply a one-shot time offset.
	 *
	 * Steps the clock by the given number of nanoseconds without affecting
	 * the frequency. Equivalent to @c adjtimex() with @c ADJ_OFFSET and
	 * @c OFFSET_UNKNOWN mode for a single-step correction.
	 *
	 * @param self     Clock instance.
	 * @param time_ns  Signed offset in nanoseconds. Positive values move the
	 *                 clock forward; negative values move it back. Pass 0 to
	 *                 cancel a pending shift.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*shift)(Clock *self, int32_t time_ns);

	/**
	 * @brief Continuously adjust the clock frequency.
	 *
	 * Trims the oscillator frequency by periodically skipping or inserting
	 * clock cycles, keeping the long-term rate accurate. Equivalent to
	 * @c adjtimex() with @c ADJ_FREQUENCY.
	 *
	 * @param self        Clock instance.
	 * @param adjust_ppb  Frequency correction in parts-per-billion (ppb).
	 *                    Positive values speed the clock up; negative values
	 *                    slow it down. Pass 0 to remove the correction.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*adjust)(Clock *self, int32_t adjust_ppb);

	/**
	 * @brief Gate the clock on (after it has been prepared).
	 *
	 * Enables the clock signal. May be called only after @ref prepare has
	 * succeeded. Analogous to Linux CCF @c clk_enable(). This operation must
	 * be atomic and may not sleep.
	 *
	 * @param self  Clock instance.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*enable)(Clock *self);

	/**
	 * @brief Gate the clock off.
	 *
	 * Disables the clock signal. The clock may still be re-enabled without
	 * calling @ref prepare again. Analogous to Linux CCF @c clk_disable().
	 * This operation must be atomic and may not sleep.
	 *
	 * @param self  Clock instance.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*disable)(Clock *self);

	/**
	 * @brief Assert and then deassert the clock hardware reset.
	 *
	 * Resets the clock generator to its power-on state. Analogous to Linux
	 * CCF @c clk_reset().
	 *
	 * @param self  Clock instance.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*reset)(Clock *self);

	/**
	 * @brief Prepare the clock for use.
	 *
	 * Performs any slow, potentially sleeping initialization required before
	 * the clock can be enabled — power-domain setup, PLL lock wait, etc.
	 * Must be called before @ref enable. Analogous to Linux CCF
	 * @c clk_prepare().
	 *
	 * @param self  Clock instance.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*prepare)(Clock *self);

	/**
	 * @brief Release resources allocated by @ref prepare.
	 *
	 * Undoes the work of @ref prepare. The clock must be disabled before
	 * this is called. Analogous to Linux CCF @c clk_unprepare().
	 *
	 * @param self  Clock instance.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*unprepare)(Clock *self);

	/**
	 * @brief Request an exact output frequency.
	 *
	 * Configures the clock to run at @p rate_hz. The hardware will be
	 * programmed to the closest achievable rate; use @ref round_rate first
	 * to determine what that rate will be. Analogous to Linux CCF
	 * @c clk_set_rate().
	 *
	 * @param self     Clock instance.
	 * @param rate_hz  Desired output frequency in Hz.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*set_rate)(Clock *self, uint32_t rate_hz);

	/**
	 * @brief Constrain the clock to a minimum frequency.
	 *
	 * Sets a lower bound on the clock rate. The clock framework will not
	 * configure the clock below this value. Analogous to Linux CCF
	 * @c clk_set_min_rate().
	 *
	 * @param self     Clock instance.
	 * @param rate_hz  Minimum acceptable frequency in Hz.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*set_min_rate)(Clock *self, uint32_t rate_hz);

	/**
	 * @brief Constrain the clock to a maximum frequency.
	 *
	 * Sets an upper bound on the clock rate. The clock framework will not
	 * configure the clock above this value. Analogous to Linux CCF
	 * @c clk_set_max_rate().
	 *
	 * @param self     Clock instance.
	 * @param rate_hz  Maximum acceptable frequency in Hz.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*set_max_rate)(Clock *self, uint32_t rate_hz);

	/**
	 * @brief Query the current output frequency.
	 *
	 * Returns the actual rate at which the clock is currently running.
	 * Analogous to Linux CCF @c clk_get_rate().
	 *
	 * @param self     Clock instance.
	 * @param rate_hz  Output: current frequency in Hz. Must not be NULL.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*get_rate)(Clock *self, uint32_t *rate_hz);

	/**
	 * @brief Round a requested rate to the nearest achievable rate.
	 *
	 * Returns the actual frequency the hardware would produce if
	 * @ref set_rate were called with @p rate_hz, without changing the
	 * hardware state. Analogous to Linux CCF @c clk_round_rate().
	 *
	 * @param self          Clock instance.
	 * @param rate_hz       Requested frequency in Hz.
	 * @param real_rate_hz  Output: nearest achievable frequency in Hz.
	 *                      Must not be NULL.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*round_rate)(Clock *self, uint32_t rate_hz, uint32_t *real_rate_hz);

	/**
	 * @brief Recalculate the output rate from the parent rate.
	 *
	 * Re-reads hardware divider/multiplier registers and recomputes the
	 * effective output frequency, propagating the result to any child clocks.
	 * Corresponds to the @c recalc_rate callback in the Linux CCF
	 * @c clk_ops structure.
	 *
	 * @param self  Clock instance.
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*recalc_rate)(Clock *self);

	/**
	 * @brief Reparent this clock to a different source.
	 *
	 * Changes the clock's input to @p parent, reprograms the mux, and
	 * triggers a rate recalculation. Analogous to Linux CCF
	 * @c clk_set_parent().
	 *
	 * @note Not all parent clocks are valid inputs for a given clock. The
	 *       driver may restrict the set of acceptable parents (e.g. to the
	 *       inputs wired to the hardware mux). Passing an unsupported parent
	 *       returns @ref CLOCK_RET_BAD_ARG without modifying the clock tree.
	 *
	 * @param self    Clock instance.
	 * @param parent  New parent clock. Must not be NULL.
	 * @return CLOCK_RET_OK      on success.
	 *         CLOCK_RET_BAD_ARG if @p parent is not a valid input for this clock.
	 */
	clock_ret_t (*set_parent)(Clock *self, Clock *parent);

	/**
	 * @brief Retrieve the current parent clock.
	 *
	 * Returns a pointer to the clock that currently feeds this clock's input
	 * mux. Analogous to Linux CCF @c clk_get_parent().
	 *
	 * @param self    Clock instance.
	 * @param parent  Output: pointer to the parent Clock. Must not be NULL.
	 *                Set to NULL by the implementation if the clock has no
	 *                parent (e.g. a fixed oscillator).
	 * @return CLOCK_RET_OK on success.
	 */
	clock_ret_t (*get_parent)(Clock *self, Clock **parent);
};


/**
 * @brief Generic clock instance.
 *
 * Every concrete clock driver embeds or points to one of these. The @p vmt
 * field selects the driver implementation; @p parent is an opaque pointer
 * reserved for driver-private data (not the clock-tree parent — that is
 * accessed via @ref clock_vmt::get_parent).
 */
typedef struct clock {
	/** Pointer to the driver's virtual method table. */
	const struct clock_vmt *vmt;
	/** Driver-private context pointer. */
	void *parent;
	const char *name;
} Clock;

