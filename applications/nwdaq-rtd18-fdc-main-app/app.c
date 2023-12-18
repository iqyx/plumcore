#include <main.h>
#include "app.h"

#define MODULE_NAME "app"

extern NbusChannel *nbus_root_channel;

struct adc_composite_channel adc_channels[] = {
	{
		.name = "channel/0",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 0},
			{.mux = NULL},
		},
		/* Normally the result is in ppm of the ADC reference. In our case we are measuring
		 * ratiometric to the Rref = 1.2k. Set pregain to 833 to get results in Ohms. */
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/1",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 1},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/2",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 2},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/3",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 3},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/4",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 4},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/5",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 5},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/6",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 6},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
	}, {
		.name = "channel/7",
		.muxes = {
			{.mux = &input_mux.mux, .channel = 7},
			{.mux = NULL},
		},
		.pregain = -500.0f,
		.gain = 1.0f,
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

	self->fifo_fs = NULL;
	if (iservicelocator_query_name_type(locator, "fifo", ISERVICELOCATOR_TYPE_FS, (Interface **)&self->fifo_fs) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("app: no FIFO filesystem found"));
		return APP_RET_FAILED;
	}

	adc_composite_init(&self->adc, NULL, self->mq);
	self->adc.adc = &mcp;
	self->adc.channels = &adc_channels;
	self->adc.interval_ms = 5000;
	adc_composite_set_clock(&self->adc, &rtc.clock);
	adc_composite_set_device_temp(&self->adc, &pcb_temp.iface, "channel/temp");

	adc_composite_start_cont(&self->adc);

	mq_batch_init(&self->mq_batch_1, self->mq);
	mq_batch_start(&self->mq_batch_1, DTYPE_FLOAT, 32, "channel/0", "batch/0");

	mq_stats_init(&self->mq_stats_1, self->mq);
	mq_stats_start(&self->mq_stats_1, "batch/0", DTYPE_FLOAT, 32);
	mq_stats_enable(&self->mq_stats_1, MQ_STATS_MEAN | MQ_STATS_NRMS);

	// mq_sensor_source_init(&self->pcb_temp_source, &pcb_temp.iface, "sensor/pcb-temp", self->mq, &rtc.clock, 1000);

	plog_packager_init(&self->raw_data_packager, self->mq);
	plog_packager_add_filter(&self->raw_data_packager, "channel/#");
	plog_packager_add_dst_mq(&self->raw_data_packager, "pkg/channel");
	plog_packager_add_dst_file(&self->raw_data_packager, self->fifo_fs, "fifo");
	plog_packager_set_nonce(&self->raw_data_packager, (uint8_t *)"nonce", 5);
	plog_packager_set_key(&self->raw_data_packager, (uint8_t *)"key", 3);
	plog_packager_start(&self->raw_data_packager, 2048, 3072);

	nbus_mq_init(&self->nbus_mq, self->mq, nbus_root_channel, "mq");
	nbus_mq_start(&self->nbus_mq, "channel/#");

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	return APP_RET_OK;
}
