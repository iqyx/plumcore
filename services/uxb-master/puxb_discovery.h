#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "module.h"
#include "libuxb.h"
#include "interfaces/uxbbus.h"
#include "interfaces/uxbdevice.h"
#include "interfaces/uxbslot.h"


#define PXUB_DISCOVERY_DSECRIPTOR_LENGTH 16

struct puxb_device;

typedef enum {
	PUXB_DISCOVERY_RET_OK = 0,
	PUXB_DISCOVERY_RET_FAILED,
} puxb_discovery_ret_t;


typedef struct puxb_discovery {
	Module module;

	bool discovery_can_run;
	TaskHandle_t discovery_task;
	IUxbBus *bus;

	struct puxb_discovery_device *devices;

} PUxbDiscovery;


typedef struct puxb_discovery_device {

	bool active;
	IUxbDevice *device;
	uint8_t id[8];

	struct puxb_discovery *parent;
	struct puxb_discovery_device *next;
} PUxbDiscoveryDevice;



puxb_discovery_ret_t puxb_discovery_init(PUxbDiscovery *self, IUxbBus *bus);


