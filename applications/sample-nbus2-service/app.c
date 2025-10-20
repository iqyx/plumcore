#include <main.h>
#include <blake2s.h>
#include <services/nbus-flash/nbus-flash.h>
#include "app.h"

#define MODULE_NAME "app"


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	self->nbus_stream = NULL;
	if (iservicelocator_query_name_type(locator, "nbus2_stream", ISERVICELOCATOR_TYPE_STREAM, (Interface **)&self->nbus_stream) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot find nbus2 stream interface"));
		return APP_RET_FAILED;
	}

	const uint8_t local_ep = 1;
	uint8_t local_id[4];
	blake2s(local_id, sizeof(local_id), UNIQUE_ID_REG, UNIQUE_ID_REG_LEN, "", 0);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("nbus2 service at %02x%02x%02x%02x"), local_id[0], local_id[1], local_id[2], local_id[3]);

	nbus_init(&self->nbus, self->nbus_stream);
	nbus_set_mac_key(&self->nbus, (uint8_t *)"abcd", 4);

	self->rpc_socket = nbus_socket_allocate(&self->nbus);
	nbus_socket_bind(self->rpc_socket, local_id, local_ep);
	self->rpc_datagram = &self->rpc_socket->datagram;

	nbus_flash_init(&self->flash_proto, self->rpc_datagram);

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	return APP_RET_OK;
}
