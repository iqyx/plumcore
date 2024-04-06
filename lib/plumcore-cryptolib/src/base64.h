/*
 * Base64 encoding/decoding
 *
 * Code taken from https://en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64,
 * suspected public domain as the site states.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


typedef enum {
	BASE64_RET_OK = 0,
	BASE64_RET_BUF_SMALL,
	BASE64_RET_INVALID,
	BASE64_RET_BUF_OVERFLOW,
} base64_ret_t;


base64_ret_t base64encode(const void *buf, size_t len, char *result, size_t resultSize);
base64_ret_t base64decode(const char *in, size_t inLen, unsigned char *out, size_t *outLen);

