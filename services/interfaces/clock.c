#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "interfaces/clock.h"


clock_ret_t clock_init(Clock *self) {
	memset(self, 0, sizeof(Clock));

	return CLOCK_RET_OK;
}


clock_ret_t clock_free(Clock *self) {
	(void)self;
	return CLOCK_RET_OK;
}
