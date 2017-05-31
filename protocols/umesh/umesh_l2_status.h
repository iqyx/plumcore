#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "umesh_l3_protocol.h"
#include "uhal/interfaces/sensor.h"
#include "interface_power.h"


#define UMESH_L2_STATUS_STEP_INTERVAL_MS 5000
#define UMESH_L2_STATUS_BROADCAST_INTERVAL_MS 1000
#define UMESH_L2_STATUS_SENSORS_MAX 16
#define UMESH_L2_STATUS_POWER_DEVICES_MAX 4

struct status_sensor {
	char name[16];
	ISensor *sensor;
};

struct status_power_device {
	char name[16];
	struct interface_power *power;
};

typedef struct {
	struct umesh_l3_protocol protocol;
	TaskHandle_t status_task;


	struct umesh_l2_pbuf pbuf;
	uint32_t last_broadcast_ms;

} UmeshL2Status;


void umesh_l2_status_send_broadcast(UmeshL2Status *self);
void umesh_l2_status_init(UmeshL2Status *self);
void umesh_l2_status_free(UmeshL2Status *self);
void umesh_l2_status_add_sensor(ISensor *sensor, const char *name);
void umesh_l2_status_add_power_device(struct interface_power *power, const char *name);
