#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "module.h"
#include "libuxb.h"
#include "interfaces/uxbbus.h"

#define PUXB_DESCRIPTOR_BUFFER_SIZE 64
#define PUXB_DESCRIPTOR_KEY_SIZE 16
#define PUXB_DESCRIPTOR_VALUE_SIZE 48
#define PUXB_DESCRIPTOR_VERSION_SIZE 32
#define PUXB_DESCRIPTOR_DEVICE_NAME_SIZE 32

struct puxb_device;
struct puxb_slot;

typedef enum {
	PUXB_RET_OK = 0,
	PUXB_RET_FAILED,
} puxb_ret_t;


typedef struct puxb_bus {
	Module module;

	LibUxbBus bus;
	IUxbBus iface;
	SemaphoreHandle_t bus_lock;

	struct puxb_device *devices;
} PUxbBus;


typedef struct puxb_device {
	IUxbDevice device_iface;
	LibUxbDevice device;
	struct puxb_slot *slots;
	LibUxbSlot *descriptor;
	SemaphoreHandle_t descriptor_lock;
	uint8_t descriptor_buffer[PUXB_DESCRIPTOR_BUFFER_SIZE];
	uint8_t id[8];

	char hw_version[PUXB_DESCRIPTOR_VERSION_SIZE];
	char fw_version[PUXB_DESCRIPTOR_VERSION_SIZE];
	char device_name[PUXB_DESCRIPTOR_DEVICE_NAME_SIZE];

	struct puxb_bus *parent;
	struct puxb_device *next;
} PUxbDevice;


typedef struct puxb_slot {
	IUxbSlot slot_iface;
	LibUxbSlot slot;
	SemaphoreHandle_t read_lock;
	struct puxb_device *parent;
	struct puxb_slot *next;
} PUxbSlot;


puxb_ret_t puxb_bus_init(PUxbBus *self, const struct uxb_master_locm3_config *config);
IUxbBus *puxb_bus_interface(PUxbBus *self);

