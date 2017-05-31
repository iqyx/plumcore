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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "hal_module.h"
#include "interface_stream.h"
#include "module_crypto_test.h"

#include "sha2.h"
#include "sha3.h"
#include "poly1305.h"
#include "chacha20.h"
#include "ed25519.h"
#include "curve25519-donna.h"


/* Sample data for our benchmark tests. */
static const uint8_t test_message[256] = {0};
static const uint8_t ed25519_test_pk[] = {
	0x77, 0xf4, 0x8b, 0x59, 0xca, 0xed, 0xa7, 0x77, 0x51, 0xed, 0x13, 0x8b, 0x0e, 0xc6, 0x67, 0xff,
	0x50, 0xf8, 0x76, 0x8c, 0x25, 0xd4, 0x83, 0x09, 0xa8, 0xf3, 0x86, 0xa2, 0xba, 0xd1, 0x87, 0xfb,
};
static const uint8_t ed25519_test_sk[] = {
	0xb1, 0x8e, 0x1d, 0x00, 0x45, 0x99, 0x5e, 0xc3, 0xd0, 0x10, 0xc3, 0x87, 0xcc, 0xfe, 0xb9, 0x84,
	0xd7, 0x83, 0xaf, 0x8f, 0xbb, 0x0f, 0x40, 0xfa, 0x7d, 0xb1, 0x26, 0xd8, 0x89, 0xf6, 0xda, 0xdd,
};

static void crypto_print(struct module_crypto_test *crypto, const char *s) {
	if (u_assert(crypto != NULL && crypto->stream != NULL && s != NULL)) {
		return;
	}

	interface_stream_write(crypto->stream, (const uint8_t *)s, strlen(s));
}


static void sha512_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha512_ctx ctx;
	uint8_t result[64];
	while (1) {
		sha512_init(&ctx);
		sha512_update(&ctx, test_message, 256);
		sha512_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha512_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha512_ctx ctx;
	uint8_t result[64];
	while (1) {
		sha512_init(&ctx);
		sha512_update(&ctx, test_message, 32);
		sha512_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha256_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha256_ctx ctx;
	uint8_t result[32];
	while (1) {
		sha256_init(&ctx);
		sha256_update(&ctx, test_message, 256);
		sha256_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha256_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha256_ctx ctx;
	uint8_t result[32];
	while (1) {
		sha256_init(&ctx);
		sha256_update(&ctx, test_message, 32);
		sha256_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha3_512_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha3_ctx ctx;
	uint8_t result[64];
	while (1) {
		rhash_sha3_512_init(&ctx);
		rhash_sha3_update(&ctx, test_message, 256);
		rhash_sha3_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha3_512_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha3_ctx ctx;
	uint8_t result[64];
	while (1) {
		rhash_sha3_512_init(&ctx);
		rhash_sha3_update(&ctx, test_message, 32);
		rhash_sha3_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha3_256_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha3_ctx ctx;
	uint8_t result[32];
	while (1) {
		rhash_sha3_256_init(&ctx);
		rhash_sha3_update(&ctx, test_message, 256);
		rhash_sha3_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void sha3_256_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	sha3_ctx ctx;
	uint8_t result[32];
	while (1) {
		rhash_sha3_256_init(&ctx);
		rhash_sha3_update(&ctx, test_message, 32);
		rhash_sha3_final(&ctx, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void poly1305_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	uint8_t result[16];
	while (1) {
		poly1305(result, test_message, 32, test_message);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void poly1305_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	uint8_t result[16];
	while (1) {
		poly1305(result, test_message, 256, test_message);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void chacha20_test(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	uint8_t result[64];
	chacha20_context c;
	chacha20_keysetup(&c, test_message, 256);
	chacha20_nonce(&c, test_message);
	while (1) {
		chacha20_counter(&c, crypto->counter);
		chacha20_keystream(&c, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void ed25519_public_key_test(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	uint8_t result[32];
	while (1) {
		ed25519_publickey(ed25519_test_sk, result);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void ed25519_sign_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	uint8_t result[64];
	while (1) {
		ed25519_sign(result, ed25519_test_sk, ed25519_test_pk, test_message, 32);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void ed25519_sign_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	uint8_t result[64];
	while (1) {
		ed25519_sign(result, ed25519_test_sk, ed25519_test_pk, test_message, 256);
		crypto->test_result += result[0];
		crypto->counter++;
	}
}


static void ed25519_verify_test_256(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	while (1) {
		if (ed25519_verify(test_message, ed25519_test_pk, test_message, 256)) {
			crypto->test_result++;
		}
		crypto->counter++;
	}
}


static void ed25519_verify_test_32(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	while (1) {
		if (ed25519_verify(test_message, ed25519_test_pk, test_message, 32)) {
			crypto->test_result++;
		}
		crypto->counter++;
	}
}


static void curve25519_scalarmult_test(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	static const uint8_t basepoint[32] = {9};
	uint8_t pk[32];
	uint8_t sk[32];

	/* Prepare a secret key. We are using some random key here
	 * (Ed25519 key) */
	memcpy(sk, ed25519_test_sk, 32);
	sk[0] &= 248;
	sk[31] &= 127;
	sk[31] |= 64;

	while (1) {
		curve25519_donna(pk, sk, basepoint);
		crypto->test_result += pk[0];
		crypto->counter++;
	}
}


static void run_test_task(struct module_crypto_test *crypto, const char *name, void (*task)(void *p), uint32_t time, uint32_t stack) {

	TaskHandle_t xHandle = NULL;
	char s[100];
	crypto->counter = 0;

	xTaskCreate(task, "crypto_test_algo", configMINIMAL_STACK_SIZE + stack, (void *)crypto, 1, &xHandle);
	if (xHandle == NULL) {
		snprintf(s, sizeof(s), "error running test '%s'\r\n", name);
		crypto_print(crypto, s);
	} else {
		vTaskDelay(time);
		vTaskDelete(xHandle);

		snprintf(
			s,
			sizeof(s),
			"test '%s': time %ums, %u computations, %u per second\r\n",
			name,
			(unsigned int)time,
			(unsigned int)crypto->counter,
			(unsigned int)(crypto->counter * 1000 / time)
		);
		crypto_print(crypto, s);

		/* Wait a bit to allow idle task to run. It is needed to free
		 * the previously allocated test task space. */
		vTaskDelay(100);
	}
}


static void module_crypto_task(void *p) {
	struct module_crypto_test *crypto = (struct module_crypto_test *)p;

	crypto_print(crypto, "Crypto benchmarking and test suite started.\r\n");

	run_test_task(crypto, "sha256, 32 byte blocks", sha256_test_32, MODULE_CRYPTO_TEST_LENGTH, 160);
	run_test_task(crypto, "sha256, 256 byte blocks", sha256_test_256, MODULE_CRYPTO_TEST_LENGTH, 160);
	run_test_task(crypto, "sha512, 32 byte blocks", sha512_test_32, MODULE_CRYPTO_TEST_LENGTH, 300);
	run_test_task(crypto, "sha512, 256 byte blocks", sha512_test_256, MODULE_CRYPTO_TEST_LENGTH, 300);
	run_test_task(crypto, "sha3-512, 32 byte blocks", sha3_512_test_32, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "sha3-512, 256 byte blocks", sha3_512_test_256, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "sha3-256, 32 byte blocks", sha3_256_test_32, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "sha3-256, 256 byte blocks", sha3_256_test_256, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "poly1305, 32 byte blocks", poly1305_test_32, MODULE_CRYPTO_TEST_LENGTH, 160);
	run_test_task(crypto, "poly1305, 256 byte blocks", poly1305_test_256, MODULE_CRYPTO_TEST_LENGTH, 160);
	run_test_task(crypto, "chacha20, 64 byte keystream output", chacha20_test, MODULE_CRYPTO_TEST_LENGTH, 160);
	run_test_task(crypto, "ed25519, get public key from private key", ed25519_public_key_test, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "ed25519, sign, 32 byte block", ed25519_sign_test_32, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "ed25519, sign, 256 byte block", ed25519_sign_test_256, MODULE_CRYPTO_TEST_LENGTH, 600);
	run_test_task(crypto, "ed25519, verify, 32 byte block", ed25519_verify_test_32, MODULE_CRYPTO_TEST_LENGTH, 1024);
	run_test_task(crypto, "ed25519, verify, 256 byte block", ed25519_verify_test_256, MODULE_CRYPTO_TEST_LENGTH, 1024);
	run_test_task(crypto, "curve25519, scalarmult", curve25519_scalarmult_test, MODULE_CRYPTO_TEST_LENGTH, 1024);

	crypto->running = false;

	/* Commit a suicide. */
	vTaskDelete(NULL);
}


int32_t module_crypto_test_init(struct module_crypto_test *crypto, const char *name, struct interface_stream *stream) {
	if (u_assert(crypto != NULL)) {
		return MODULE_CRYPTO_TEST_INIT_FAILED;
	}

	memset(crypto, 0, sizeof(struct module_crypto_test));
	hal_module_descriptor_init(&(crypto->module), name);
	hal_module_descriptor_set_shm(&(crypto->module), (void *)crypto, sizeof(struct module_crypto_test));
	crypto->stream = stream;

	return MODULE_CRYPTO_TEST_INIT_OK;
}


int32_t module_crypto_test_run(struct module_crypto_test *crypto) {
	if (u_assert(crypto != NULL)) {
		return MODULE_CRYPTO_TEST_RUN_FAILED;
	}

	crypto->running = true;
	xTaskCreate(module_crypto_task, "crypto_test_task", configMINIMAL_STACK_SIZE + 256, (void *)crypto, 1, NULL);

	return MODULE_CRYPTO_TEST_RUN_OK;
}


bool module_crypto_test_running(struct module_crypto_test *crypto) {
	if (u_assert(crypto != NULL)) {
		return false;
	}

	return crypto->running;
}



