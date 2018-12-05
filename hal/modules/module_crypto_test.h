/**
 * uMeshFw crypto benchmarking and test suite
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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

#ifndef _MODULE_CRYPTO_TEST_H_
#define _MODULE_CRYPTO_TEST_H_

#include <stdint.h>
#include <stdbool.h>
#include "interface_spibus.h"

#define MODULE_CRYPTO_TEST_LENGTH 3000

struct module_crypto_test {
	/**
	 * Test results are normally displayed in a readable form on a selected
	 * stream interface.
	 */
	struct interface_stream *stream;

	/**
	 * Counter for computed test results during the test period. It is
	 * cleared before running each test routine.
	 */
	volatile uint32_t counter;

	/**
	 * This volatile variable is used to aggregate results from each test run.
	 * It is used only to avoid the compiler optimizing out our useless results.
	 */
	volatile uint8_t test_result;

	bool running;
};


int32_t module_crypto_test_init(struct module_crypto_test *crypto, const char *name, struct interface_stream *stream);
#define MODULE_CRYPTO_TEST_INIT_OK 0
#define MODULE_CRYPTO_TEST_INIT_FAILED -1

int32_t module_crypto_test_run(struct module_crypto_test *crypto);
#define MODULE_CRYPTO_TEST_RUN_OK 0
#define MODULE_CRYPTO_TEST_RUN_FAILED -1

bool module_crypto_test_running(struct module_crypto_test *crypto);


#endif
