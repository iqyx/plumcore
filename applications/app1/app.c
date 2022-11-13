#include <main.h>
#include "app.h"

#define MODULE_NAME "app"


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));
	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	return APP_RET_OK;
}
