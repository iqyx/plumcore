
/*
 * Interface for querying system services (see service locator pattern)
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

/* Do not edit this file directly, it is autogenerated. */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interface.h"


typedef enum {
	ISERVICELOCATOR_RET_OK = 0,
	ISERVICELOCATOR_RET_FAILED,
	ISERVICELOCATOR_RET_NOT_IMPLEMENTED,
} iservicelocator_ret_t;

enum iservicelocator_type {
	ISERVICELOCATOR_TYPE_NONE = 0,
	ISERVICELOCATOR_TYPE_SERVICE_LOCATOR,
	ISERVICELOCATOR_TYPE_LED,
	ISERVICELOCATOR_TYPE_RTC,
	ISERVICELOCATOR_TYPE_RNG,
	ISERVICELOCATOR_TYPE_PROFILER,
	ISERVICELOCATOR_TYPE_STREAM,
	ISERVICELOCATOR_TYPE_TCPIP,
	ISERVICELOCATOR_TYPE_MQTT,
	ISERVICELOCATOR_TYPE_SENSOR,
	ISERVICELOCATOR_TYPE_CELLULAR,
	ISERVICELOCATOR_TYPE_UXBBUS,
	ISERVICELOCATOR_TYPE_UXBDEVICE,
	ISERVICELOCATOR_TYPE_UXBSLOT,
	ISERVICELOCATOR_TYPE_CAN,
	ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER,
	ISERVICELOCATOR_TYPE_PLOG_ROUTER,
	ISERVICELOCATOR_TYPE_CLOCK,
	ISERVICELOCATOR_TYPE_FLASH,
	ISERVICELOCATOR_TYPE_FS,
	ISERVICELOCATOR_TYPE_RADIO_MAC,
	ISERVICELOCATOR_TYPE_PLOG_RELAY,
	ISERVICELOCATOR_TYPE_SPIDEV,
	ISERVICELOCATOR_TYPE_WAVEFORM_SOURCE,
	ISERVICELOCATOR_TYPE_BLOCK,
	ISERVICELOCATOR_TYPE_WORN,
	ISERVICELOCATOR_TYPE_MQ,
	ISERVICELOCATOR_TYPE_LORA,
	ISERVICELOCATOR_TYPE_APPLET,
};

struct iservicelocator_vmt {
	iservicelocator_ret_t (*set_name)(void *context, Interface * interface, const char * name);
	iservicelocator_ret_t (*add)(void *context, enum iservicelocator_type type, Interface * interface, char * name);
	iservicelocator_ret_t (*query_name)(void *context, char * name, Interface ** result);
	iservicelocator_ret_t (*query_type_id)(void *context, enum iservicelocator_type type, size_t index, Interface ** result);
	iservicelocator_ret_t (*query_type_next)(void *context, enum iservicelocator_type type, Interface * start, Interface ** result);
	iservicelocator_ret_t (*query_name_type)(void *context, char * name, enum iservicelocator_type type, Interface ** result);
	iservicelocator_ret_t (*get_name)(void *context, Interface * interface, const char ** result);
	void *context;
};

typedef struct {
	Interface interface;
	struct iservicelocator_vmt vmt;
} IServiceLocator;


iservicelocator_ret_t iservicelocator_set_name(IServiceLocator *self, Interface * interface, const char * name);
iservicelocator_ret_t iservicelocator_add(IServiceLocator *self, enum iservicelocator_type type, Interface * interface, char * name);
iservicelocator_ret_t iservicelocator_query_name(IServiceLocator *self, char * name, Interface ** result);
iservicelocator_ret_t iservicelocator_free(IServiceLocator *self);
iservicelocator_ret_t iservicelocator_query_type_id(IServiceLocator *self, enum iservicelocator_type type, size_t index, Interface ** result);
iservicelocator_ret_t iservicelocator_query_type_next(IServiceLocator *self, enum iservicelocator_type type, Interface * start, Interface ** result);
iservicelocator_ret_t iservicelocator_init(IServiceLocator *self);
iservicelocator_ret_t iservicelocator_query_name_type(IServiceLocator *self, char * name, enum iservicelocator_type type, Interface ** result);
iservicelocator_ret_t iservicelocator_get_name(IServiceLocator *self, Interface * interface, const char ** result);
