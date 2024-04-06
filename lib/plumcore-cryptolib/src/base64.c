/*
 * Base64 encoding/decoding
 *
 * Code taken from https://en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64,
 * suspected public domain as the site states.
 */

#include <stdint.h>

#include "base64.h"
#include <stddef.h>


const char base64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

base64_ret_t base64encode(const void *buf, size_t len, char *result, size_t resultSize) {
	const uint8_t *data = (const uint8_t *)buf;
	size_t resultIndex = 0;
	size_t x = 0;
	uint32_t n = 0;
	int padCount = len % 3;
	uint8_t n0 = 0;
	uint8_t n1 = 0;
	uint8_t n2 = 0;
	uint8_t n3 = 0;

	/* Increment over the length of the string, three characters at a time. */
	for (x = 0; x < len; x += 3) {
		/* These three 8-bit (ASCII) characters become one 24-bit number.
		 * Parenthesis needed, compiler depending on flags can do the shifting before conversion to uint32_t,
		 * resulting to 0 */
		n = ((uint32_t)data[x]) << 16;

		if((x + 1) < len) {
			/* Parenthesis needed, compiler depending on flags can do the shifting before conversion
			 * to uint32_t, resulting to 0 */
			n += ((uint32_t)data[x + 1]) << 8;
		}

		if((x + 2) < len) {
			n += data[x + 2];
		}

		/* This 24-bit number gets separated into four 6-bit numbers. */
		n0 = (uint8_t)(n >> 18) & 63;
		n1 = (uint8_t)(n >> 12) & 63;
		n2 = (uint8_t)(n >> 6) & 63;
		n3 = (uint8_t)n & 63;

		/* If we have one byte available, then its encoding is spread out over two characters. */
		if(resultIndex >= resultSize) {
			return BASE64_RET_BUF_SMALL;
		}
		result[resultIndex++] = base64chars[n0];
		if(resultIndex >= resultSize) {
			return BASE64_RET_BUF_SMALL;
		}
		result[resultIndex++] = base64chars[n1];

		/* if we have only two bytes available, then their encoding is spread out over three chars */
		if((x+1) < len) {
			if (resultIndex >= resultSize) {
				return BASE64_RET_BUF_SMALL;
			}
			result[resultIndex++] = base64chars[n2];
		}

		/* if we have all three bytes available, then their encoding is spread out over four characters */
		if((x+2) < len) {
			if (resultIndex >= resultSize) {
				return BASE64_RET_BUF_SMALL;
			}
			result[resultIndex++] = base64chars[n3];
		}
	}

	/* create and add padding that is required if we did not have a multiple of 3
	 * number of characters available */
	if (padCount > 0) {
		for (; padCount < 3; padCount++) {
			if (resultIndex >= resultSize) {
				return BASE64_RET_BUF_SMALL;
			}
			result[resultIndex++] = '=';
		}
	}

	if (resultIndex >= resultSize) {
		/* indicate failure: buffer too small */
		return BASE64_RET_BUF_SMALL;
	}

	result[resultIndex] = 0;
	return BASE64_RET_OK;
}


#define B64_WHITESPACE 64
#define B64_EQUALS 65
#define B64_INVALID 66

static const unsigned char d[] = {
	52,53,54,55,56,57,58,59,60,61,66,66,66,65,66,66,
	66, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
	15,16,17,18,19,20,21,22,23,24,25,66,66,66,66,66,
	66,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
	41,42,43,44,45,46,47,48,49,50,51,66,66,66,66,66,
};


static unsigned char b64fromchar(unsigned char c) {
	if (c >= 128) {
		return B64_INVALID;
	} else if (c == '\n') {
		return B64_WHITESPACE;
	} else if (c == 43) {
		return 62;
	} else if (c == 47) {
		return 63;
	} else if (c < 48) {
		return B64_INVALID;
	} else {
		return d[c - 48];
	}
}


base64_ret_t base64decode(const char *in, size_t inLen, unsigned char *out, size_t *outLen) {
	const char *end = in + inLen;
	char iter = 0;
	uint32_t buf = 0;
	size_t len = 0;

	while (in < end) {
		unsigned char c = b64fromchar(*in++);

		switch (c) {
			case B64_WHITESPACE:
				/* Skip whitespace. */
				continue;
			case B64_INVALID:
				/* Invalid input, return error. */
				return BASE64_RET_INVALID;
			case B64_EQUALS:
				/* Pad character, end of data. */
				in = end;
				continue;
			default:
				buf = buf << 6 | c;
				iter++;
				/* If the buffer is full, split it into bytes. */
				if (iter == 4) {
					if ((len += 3) > *outLen) {
						return BASE64_RET_BUF_OVERFLOW;
					}
					*(out++) = (buf >> 16) & 255;
					*(out++) = (buf >> 8) & 255;
					*(out++) = buf & 255;
					buf = 0; iter = 0;
				}
		}
	}

	if (iter == 3) {
		if ((len += 2) > *outLen) {
			return BASE64_RET_BUF_OVERFLOW;
		}
		*(out++) = (buf >> 10) & 255;
		*(out++) = (buf >> 2) & 255;
	} else if (iter == 2) {
		if (++len > *outLen) {
			return BASE64_RET_BUF_OVERFLOW;
		}
		*(out++) = (buf >> 4) & 255;
	}

	/* Modify to reflect the actual output size. */
	*outLen = len;
	return BASE64_RET_OK;
}

