/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TMP1826 1-Wire temperature sensor and EEPROM driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/ow.h>
#include <interfaces/flash.h>
#include <interfaces/sensor.h>

/*
 * The TMP1826 is a 1-Wire(R) temperature sensor with an integrated 2-Kbit user EEPROM. This driver speaks the
 * TMP1826 device protocol on top of a generic 1-Wire bus master provided through the Ow interface, which it uses to
 * reset and address the device and to exchange the command and data bytes.
 *
 * It exposes a Sensor interface reading the internal die temperature and a Flash interface providing block access
 * to the on-chip user EEPROM.
 */

typedef enum {
	TMP1826_RET_OK = 0,
	TMP1826_RET_FAILED,
} tmp1826_ret_t;

typedef struct tmp1826 {
	/* 1-Wire bus master the device sits on. */
	Ow *ow;

	/* Provided interfaces. */
	Flash flash;
	Sensor temp;

	/* Serializes access to the shared 1-Wire bus. */
	SemaphoreHandle_t lock;
} Tmp1826;


tmp1826_ret_t tmp1826_init(Tmp1826 *self, Ow *ow);
tmp1826_ret_t tmp1826_free(Tmp1826 *self);

/* Obtain the Flash interface for the on-chip user EEPROM. */
tmp1826_ret_t tmp1826_get_flash(Tmp1826 *self, Flash **flash);

/* Obtain the Sensor interface for the internal temperature sensor. */
tmp1826_ret_t tmp1826_get_sensor(Tmp1826 *self, Sensor **sensor);
