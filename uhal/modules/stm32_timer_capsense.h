/*
 * uMeshFw HAL capacitive sensing/measurement using STM32 timers
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * There are various ways of doing capacitance measurement (capacitive
 * sensing) - all with their pros and cons. One of the simplest methods
 * is using a single electrode (with ground acting as a second electrode
 * of the capacitor) which is being charged with a constant current source
 * and measuring the time it takes to charge to a predefined voltage.
 * Even a simple resistor connected to a constant voltage source can be
 * used (in this case - VDD).
 *
 * This code uses a single timer with two channels. The first channel is
 * configured as a compare output with its GPIO mode set to open-drain.
 * This GPIO is used to continuously reset the electrode potential back
 * to ground. The second channel is configured as a capture input
 * (sensitive to the signal leading edge) and it is used to measure the
 * time it takes to charge the electrode to a constant, but mostly unknown
 * value (somewhere between GND and VDD, it depends on the MCU input pin
 * circuitry). If required, an external comparator can be used to accurately
 * trigger the input capture when a known voltage is reached. The captured
 * value is used to compute the time and capacitance between the electrode
 * and ground.
 */

#pragma once

#include <stdint.h>

#include "module.h"
#include "interfaces/sensor.h"


typedef enum {
	STM32_TIMER_CAPSENSE_RET_OK = 0,
	STM32_TIMER_CAPSENSE_RET_FAILED = -1,
} stm32_timer_capsense_ret_t;


typedef struct {

	Module module;
	ISensor probe;

	uint32_t probe_reset_oc;
	uint32_t probe_ic;
	uint32_t timer;
	uint32_t timer_period;

} Stm32TimerCapsense;


/**
 * @brief Initialize the STM32 timer capacitive sensing module
 *
 * @param self Pointer to a preallocated Stm32TimerCapsense instance
 * @param timer libopencm3 timer (eg. TIM2, TIM3..)
 * @param probe_reset_oc libopencm3 timer compare output for probe reset (eg. TIM_OC1)
 * @param probe_ic libopencm3 timer capture input for probe voltage sensing (eg. TIM_IC2)
 *
 * @return STM32_TIMER_CAPSENSE_RET_OK if the initialization was successful or
 *         STM32_TIMER_CAPSENSE_RET_FAILED otherwise.
 */
stm32_timer_capsense_ret_t stm32_timer_capsense_init(Stm32TimerCapsense *self, uint32_t timer, uint32_t probe_reset_oc, uint32_t probe_ic);

/**
 * @brief Free the Stm32TimerCapsense instance
 *
 * @param self Stm32TimerCapsense instance (previously initialized)
 *
 * @return STM32_TIMER_CAPSENSE_RET_OK if the action succeeded or
 *         STM32_TIMER_CAPSENSE_RET_FAILED otherwise.
 */
stm32_timer_capsense_ret_t stm32_timer_capsense_free(Stm32TimerCapsense *self);

/**
 * @brief Set timer prescaler
 *
 * Set the prescaler value to change the timer counter frequency.
 *
 * @param self Stm32TimerCapsense instance
 * @param prescaler Prescaler value
 *
 * @return STM32_TIMER_CAPSENSE_RET_OK on success or
 *         STM32_TIMER_CAPSENSE_RET_FAILED otherwise.
 */
stm32_timer_capsense_ret_t stm32_timer_capsense_set_prescaler(Stm32TimerCapsense *self, uint32_t prescaler);

/**
 * @brief Set the capacitance measurement range
 *
 * The range is set by changing the timer period. If the measurement is out of range, the pullup resistor
 * value must be changed (see the intro).
 * Also note that the frequency of measurement changes when the period is changed.
 *
 * @param self Stm32TimerCapsense instance
 * @param range Range of the measurement in bits (the period is 2^range respectively)
 *
 * @return STM32_TIMER_CAPSENSE_RET_OK on success or
 *         STM32_TIMER_CAPSENSE_RET_FAILED otherwise.
 */
stm32_timer_capsense_ret_t stm32_timer_capsense_set_range(Stm32TimerCapsense *self, uint32_t range_bits);

/**
 * @brief Set length of the reset pulse
 *
 * On the beginning of the measurement the probe needs to be reset to GND. It is done
 * with a open-drain compare output with appropriate length, which must be set beforehand.
 *
 * @param self Stm32TimerCapsense instance
 * @param time_ticks Length of the reset pulse in timer ticks.
 *
 * @return STM32_TIMER_CAPSENSE_RET_OK on success or
 *         STM32_TIMER_CAPSENSE_RET_FAILED otherwise.
 */
stm32_timer_capsense_ret_t stm32_timer_capsense_set_reset_time(Stm32TimerCapsense *self, uint32_t time_ticks);

/**
 * @brief Return the ISensor interface
 */
ISensor *stm32_timer_capsense_probe(Stm32TimerCapsense *self);
