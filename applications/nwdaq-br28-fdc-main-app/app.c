#include <main.h>
#include "app.h"

#define MODULE_NAME "app"

const struct adc_composite_channel adc_channels[] = {
	{
		.name = "channel/1",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 1},
			{.mux = NULL},
		},
		.ac_excitation = true,
	}, {
		.name = "channel/3",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 3},
			{.mux = NULL},
		},
		.ac_excitation = true,
	}, {
		.name = NULL,
	},
};

app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	/* Discover all required dependencies */
	self->mq = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&self->mq) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("app: no MQ found"));
		return APP_RET_FAILED;
	}

	adc_composite_init(&self->adc, NULL, self->mq);
	self->adc.adc = &mcp;
	self->adc.channels = &adc_channels;
	self->adc.interval_ms = 20;
	adc_composite_set_exc_power(&self->adc, &exc_power.power);

	adc_composite_start_cont(&self->adc);

	mq_batch_init(&self->mq_batch_1, self->mq);
	mq_batch_start(&self->mq_batch_1, DTYPE_INT32, 32, "channel/1", "channel/batch/1");

	mq_stats_init(&self->mq_stats_1, self->mq);
	mq_stats_start(&self->mq_stats_1, "channel/batch/1", DTYPE_INT32, 32);
	mq_stats_enable(&self->mq_stats_1, MQ_STATS_MEAN);


	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	return APP_RET_OK;
}
