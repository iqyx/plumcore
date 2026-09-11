/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TEDS (Transducer Electronic Data Sheet) discovery over a 1-Wire bus
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "config.h"

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/ow.h>
#include <interfaces/mux.h>
#include <interfaces/flash.h>

#include <services/tmp1826/tmp1826.h>

/*
 * TEDS support scans a 1-Wire bus fanned out through a mux for calibration-data EEPROMs. A background task cycles the
 * mux through its channels and probes each one for a known EEPROM kind. Discovery is edge-triggered: a device is
 * reported once when a channel transitions from empty to populated, at which point the matching EEPROM driver is
 * instantiated for that channel.
 *
 * The driver's own Flash interface cannot be exported directly because every access must first steer the mux to the
 * right channel. Each channel therefore exposes a Flash interface proxy (advertised on the service locator) that locks
 * the shared bus, selects the channel on the mux and delegates to the underlying driver.
 */

/* Length of a probed device unique id (the 64-bit 1-Wire ROM address). */
#define TEDS_ID_SIZE 8

/* Upper bound on the number of mux channels the discovery task keeps state for. */
#define TEDS_MAX_CHANNELS CONFIG_APP_MEAS_GENERIC_TEDS_MAX_CHANNELS

typedef enum {
	TEDS_RET_OK = 0,
	TEDS_RET_FAILED,
} teds_ret_t;

/* Kind of EEPROM discovered on a channel, selecting the valid member of struct teds_channel::eeprom. */
enum teds_eeprom_type {
	TEDS_EEPROM_NONE = 0,
	TEDS_EEPROM_TMP1826,
};

typedef struct teds Teds;

/* One mux channel scanned for a TEDS EEPROM. When a device is present the matching driver instance lives in the union
 * and @p target points at its Flash interface; @p flash is the mux-switching proxy exported for the channel. */
struct teds_channel {
	/* Owning instance and the mux channel index, both needed by the Flash proxy to steer the mux. */
	Teds *parent;
	uint32_t index;

	/* Edge-triggered presence and the discovered device. */
	bool present;
	enum teds_eeprom_type type;
	uint8_t id[TEDS_ID_SIZE];

	/* Driver instance of the discovered EEPROM. The valid member is selected by @p type. */
	union {
		Tmp1826 tmp1826;
	} eeprom;

	/* Underlying Flash interface of the discovered EEPROM driver, or NULL if none is present. */
	Flash *target;

	/* Flash interface proxy exported for this channel. Every access steers the mux to @p index and delegates to
	 * @p target. */
	Flash flash;

	/* Service locator name the proxy Flash is advertised under, and whether it already has been. */
	char name[16];
	bool advertised;
};

typedef struct teds {
	/* 1-Wire bus master and the mux fanning it out to the physical connectors. */
	Ow *ow;
	Mux *mux;

	/* Mux channels cycled through, clamped to TEDS_MAX_CHANNELS. */
	uint32_t channel_count;
	struct teds_channel channels[TEDS_MAX_CHANNELS];

	/* Channel the mux is currently steered at, so a redundant select (and its settle delay) can be skipped.
	 * UINT32_MAX means the selection is not yet known. Guarded by @p lock. */
	uint32_t current_channel;

	/* Interval between two full probe cycles in milliseconds. */
	uint32_t probe_interval_ms;

	/* Serialises access to the shared mux and 1-Wire bus between the probe task and the Flash proxies. */
	SemaphoreHandle_t lock;

	TaskHandle_t task;
} Teds;


/**
 * @brief Initialise the TEDS discovery instance and start the background discovery task
 *
 * Discovers the "teds" 1-Wire bus and the "teds-mux" fanning it out from the service locator, queries the mux channel
 * count and starts the probe task.
 *
 * @param self Preallocated memory for the instance
 * @return TEDS_RET_FAILED on error, TEDS_RET_OK otherwise.
 */
teds_ret_t teds_init(Teds *self);

/**
 * @brief Free the TEDS discovery instance and release all allocated resources
 *
 * @param self Instance of the TEDS discovery
 * @return TEDS_RET_FAILED on error, TEDS_RET_OK otherwise.
 */
teds_ret_t teds_free(Teds *self);
