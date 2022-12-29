#pragma once

#include "config.h"
#include <interfaces/mq.h>
#include <services/adc-composite/adc-composite.h>
#include <services/mq-batch/mq-batch.h>
#include <services/mq-stats/mq-stats.h>


typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

typedef struct {
	Mq *mq;
	AdcComposite adc;
	MqBatch mq_batch_1;
	MqStats mq_stats_1;
} App;


app_ret_t app_init(App *self);
app_ret_t app_free(App *self);

