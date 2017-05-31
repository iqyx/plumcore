/**
 * uMeshFw uMesh layer 2 protocol building tests
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
#include <stdbool.h>

#include "u_assert.h"
#include "u_log.h"
#include "u_test.h"

#include "umesh_l2_send.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_send_tests.h"


/**
 * Test if TID building works for TIDs lower than 128. Buffer is long enough.
 */
static bool umesh_l2_build_tid_short_test_if_ok(void) {
	uint32_t tid = 35;
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 1);

	return (UMESH_L2_BUILD_TID_OK == res) && (buf + 1 == pos) && (0x23 == buf[0]);
}


/**
 * Test if TID building works for TID equal to 0. Buffer is long enough.
 */
static bool umesh_l2_build_tid_zero_test_if_ok(void) {
	uint32_t tid = 0;
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 1);

	return (UMESH_L2_BUILD_TID_OK == res) && (buf + 1 == pos) && (0x00 == buf[0]);
}


/**
 * Test if TID building works for TIDs greater than 127. Buffer is long enough.
 */
static bool umesh_l2_build_tid_longer_test_if_ok(void) {
	uint32_t tid = 2356;
	uint8_t buf[2] = {0, 0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 2);

	return (UMESH_L2_BUILD_TID_OK == res) && (buf + 2 == pos) && (0x92 == buf[0]) && (0x34 == buf[1]);
}


/**
 * Test if TID building works for very big (and still valid) TIDs. Buffer is long
 * enough. TID tested is 3675869435. Bytes: 13, 88, 101, 33, 123
 */
static bool umesh_l2_build_tid_long_test_if_ok(void) {
	uint32_t tid = 3675869435;
	uint8_t buf[5] = {0, 0, 0, 0, 0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 5);

	return
		(UMESH_L2_BUILD_TID_OK == res) &&
		(buf + 5 == pos) &&
		(0x8d == buf[0]) &&
		(0xd8 == buf[1]) &&
		(0xe5 == buf[2]) &&
		(0xa1 == buf[3]) &&
		(0x7b == buf[4]);
}


/**
 * Test if TID building fails with small output buffer.
 */
static bool umesh_l2_build_tid_test_small_buffer_if_fails(void) {
	uint32_t tid = 2356;
	uint8_t buf[2] = {0, 0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 1);

	return (UMESH_L2_BUILD_TID_BUFFER_SMALL == res);
}


/**
 * Test if TID building fails with zero output buffer.
 */
static bool umesh_l2_build_tid_test_zero_buffer_if_fails(void) {
	uint32_t tid = 2356;
	uint8_t buf[2] = {0, 0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 0);

	return (UMESH_L2_BUILD_TID_BUFFER_SMALL == res);
}


/**
 * Test if big TID building fails with 4B output buffer.
 */
static bool umesh_l2_build_tid_test_small_buffer_2_if_fails(void) {
	uint32_t tid = 3675869435;
	uint8_t buf[5] = {0, 0, 0, 0, 0};
	uint8_t *pos = buf;

	int32_t res = umesh_l2_build_tid(tid, &pos, 4);

	return (UMESH_L2_BUILD_TID_BUFFER_SMALL == res);
}


/**
 * Test if single byte control field building works. L3 proto set to 2 and
 * security algo set to 2 should create a 0x28 single byte control field.
 */
static bool umesh_l2_build_control_test_short_if_ok(void) {
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.proto = 2;
	pbuf.sec_algo = 2;

	int32_t res = umesh_l2_build_control(&pbuf, &pos, 1);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_CONTROL_OK == res) && (0x28 == buf[0]) && (buf + 1 == pos);
}


/**
 * Test if single byte control field building works. L3 proto set to 2,
 * security algo set to 2 and broadcast flag set should create a 0x68 single
 * byte control field.
 */
static bool umesh_l2_build_control_test_broadcast_if_ok(void) {
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.proto = 2;
	pbuf.sec_algo = 2;
	pbuf.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;

	int32_t res = umesh_l2_build_control(&pbuf, &pos, 1);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_CONTROL_OK == res) && (0x68 == buf[0]) && (buf + 1 == pos);
}


/**
 * Tesk if two byte control field building works. Set L3 proto to 15, security
 * algo to 7, broadcast flag to 1. Control bytes should be 0xfc 0x43.
 */
static bool umesh_l2_build_control_test_long_if_ok(void) {
	uint8_t buf[2] = {0, 0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.proto = 15;
	pbuf.sec_algo = 7;
	pbuf.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;

	int32_t res = umesh_l2_build_control(&pbuf, &pos, 2);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_CONTROL_OK == res) && (0xfc == buf[0])  && (0x43 == buf[1]) && (buf + 2 == pos);
}


/**
 * Tesk if two byte control field building fails for 1 byte buffer.
 */
static bool umesh_l2_build_control_test_long_small_buffer_if_fails(void) {
	uint8_t buf[2] = {0, 0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.proto = 15;
	pbuf.sec_algo = 7;
	pbuf.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;

	int32_t res = umesh_l2_build_control(&pbuf, &pos, 1);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_CONTROL_BUFFER_SMALL == res);
}


/**
 * Test if single byte control field building fails for zero length buffer.
 */
static bool umesh_l2_build_control_test_short_zero_buffer_if_fails(void) {
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.proto = 2;
	pbuf.sec_algo = 2;

	int32_t res = umesh_l2_build_control(&pbuf, &pos, 0);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_CONTROL_BUFFER_SMALL == res);
}


/**
 * Test if building an empty packet works.
 */
static bool umesh_l2_build_data_test_empty_packet_if_works(void) {
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.len = 0;
	pbuf.sec_algo = UMESH_L2_SECURITY_ALGO_NONE;

	int32_t res = umesh_l2_build_data(&pbuf, buf, &pos, 1, NULL, 0);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_DATA_OK == res) && (pos == buf);
}


/**
 * Test if building a small plaintext packet works.
 */
static bool umesh_l2_build_data_test_small_packet_if_works(void) {
	uint8_t buf[3] = {0, 0, 0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.len = 3;
	pbuf.data[0] = 0x12;
	pbuf.data[1] = 0x34;
	pbuf.data[2] = 0x56;
	pbuf.sec_algo = UMESH_L2_SECURITY_ALGO_NONE;

	int32_t res = umesh_l2_build_data(&pbuf, buf, &pos, 3, NULL, 0);

	umesh_l2_pbuf_free(&pbuf);

	return
		(UMESH_L2_BUILD_DATA_OK == res) &&
		(buf + 3 == pos) &&
		(0x12 == buf[0]) &&
		(0x34 == buf[1]) &&
		(0x56 == buf[2]);
}


/**
 * Test if building a small plaintext packet fails when a buffer of
 * insufficient length is given..
 */
static bool umesh_l2_build_data_test_small_packet_small_buffer_if_fails(void) {
	uint8_t buf[3] = {0, 0, 0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.len = 3;
	pbuf.data[0] = 0x12;
	pbuf.data[1] = 0x34;
	pbuf.data[2] = 0x56;
	pbuf.sec_algo = UMESH_L2_SECURITY_ALGO_NONE;

	int32_t res = umesh_l2_build_data(&pbuf, buf, &pos, 2, NULL, 0);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_DATA_BUFFER_SMALL == res);
}


/**
 * Test if building a plaintext packet with unsupported security algo fails.
 */
static bool umesh_l2_build_data_test_unsupported_if_fails(void) {
	uint8_t buf[1] = {0};
	uint8_t *pos = buf;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.sec_algo = 36;

	int32_t res = umesh_l2_build_data(&pbuf, buf, &pos, 1, NULL, 0);

	umesh_l2_pbuf_free(&pbuf);

	return (UMESH_L2_BUILD_DATA_UNSUPPORTED == res);
}


/**
 * Test if CRC16 works (5 byte packet).
 * Returned CRC for this packet should be 0x6d08.
 */
static bool umesh_l2_build_data_test_crc16_if_works(void) {
	uint8_t buf[9] = {0xff, 0x7f, 0, 0, 0, 0, 0, 0, 0};
	uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf pbuf;
	umesh_l2_pbuf_init(&pbuf);
	pbuf.sec_algo = UMESH_L2_SECURITY_ALGO_CRC16_CCITT;
	pbuf.len = 5;
	memcpy(pbuf.data, (uint8_t[]){0x12, 0x34, 0x56, 0x78, 0x90}, 5);

	int32_t res = umesh_l2_build_data(&pbuf, buf, &pos, 7, NULL, 0);

	umesh_l2_pbuf_free(&pbuf);

	return
		(UMESH_L2_BUILD_DATA_OK == res) &&
		(buf + 9 == pos) &&
		(!memcmp(buf, (uint8_t[]){0xff, 0x7f, 0x12, 0x34, 0x56, 0x78, 0x90, 0x6d, 0x08}, 9));
}


bool umesh_l2_send_tests(void) {
	bool res = true;

	/* TID builder tests. */
	res &= u_test(umesh_l2_build_tid_short_test_if_ok());
	res &= u_test(umesh_l2_build_tid_zero_test_if_ok());
	res &= u_test(umesh_l2_build_tid_longer_test_if_ok());
	res &= u_test(umesh_l2_build_tid_long_test_if_ok());
	res &= u_test(umesh_l2_build_tid_test_small_buffer_if_fails());
	res &= u_test(umesh_l2_build_tid_test_zero_buffer_if_fails());
	res &= u_test(umesh_l2_build_tid_test_small_buffer_2_if_fails());

	/* Control field builder tests. */
	res &= u_test(umesh_l2_build_control_test_short_if_ok());
	res &= u_test(umesh_l2_build_control_test_broadcast_if_ok());
	res &= u_test(umesh_l2_build_control_test_long_if_ok());
	res &= u_test(umesh_l2_build_control_test_long_small_buffer_if_fails());
	res &= u_test(umesh_l2_build_control_test_short_zero_buffer_if_fails());

	/* Data builder tests - generic tests and plaintext data building. */
	res &= u_test(umesh_l2_build_data_test_empty_packet_if_works());
	res &= u_test(umesh_l2_build_data_test_small_packet_if_works());
	res &= u_test(umesh_l2_build_data_test_small_packet_small_buffer_if_fails());
	res &= u_test(umesh_l2_build_data_test_unsupported_if_fails());

	/* Data builder tests - CRC16. */
	res &= u_test(umesh_l2_build_data_test_crc16_if_works());

	return res;
}
