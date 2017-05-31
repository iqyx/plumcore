/**
 * uMeshFw uMesh layer 2 receive task & protocol parsing tests
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

#include "umesh_l2_receive.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_receive_tests.h"


/**
 * Test if TID parsing works for TIDs lower than 128. Buffer is long enough.
 */
static bool umesh_l2_parse_tid_test_short_if_ok(void) {
	const uint8_t buf[1] = {0x23};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 1, &tid);

	return (tid == 35) && (pos = buf + 1) && (res == UMESH_L2_PARSE_TID_OK);
}


/**
 * Test if TID parsing works for TIDs greater than 127. Buffer is long enough.
 */
static bool umesh_l2_parse_tid_test_longer_if_ok(void) {
	const uint8_t buf[2] = {0x92, 0x34};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 2, &tid);

	return (tid == 2356) && (pos = buf + 2) && (res == UMESH_L2_PARSE_TID_OK);
}


/**
 * Test if TID parsing works for very big (and still valid) TIDs. Buffer is long
 * enough. TID tested is 3675869435. Bytes: 13, 88, 101, 33, 123
 */
static bool umesh_l2_parse_tid_test_long_if_ok(void) {
	const uint8_t buf[5] = {0x8d, 0xd8, 0xe5, 0xa1, 0x7b};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 5, &tid);

	return (tid == 3675869435) && (pos = buf + 5) && (res == UMESH_L2_PARSE_TID_OK);
}


/**
 * Test if TID parsing fails for too long (but otherwise valid) TIDs.
 */
static bool umesh_l2_parse_tid_test_too_long_if_fails(void) {
	const uint8_t buf[7] = {0x8d, 0xd8, 0xe5, 0xf2, 0xc7, 0xa1, 0x7b};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 7, &tid);

	return (res == UMESH_L2_PARSE_TID_TOO_BIG);
}


/**
 * Test if TID parsing fails for valid 5B long TIDs which would overflow 32bit.
 */
static bool umesh_l2_parse_tid_test_longer_than_32bit_if_fails(void) {
	const uint8_t buf[5] = {0xcd, 0xd8, 0xe5, 0xa1, 0x7b};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 5, &tid);

	return (res != UMESH_L2_PARSE_TID_OK);
}


/**
 * Test if TID parsing fails with 0B of data.
 */
static bool umesh_l2_parse_tid_test_no_data_if_fails(void) {
	const uint8_t buf[1] = {0x23};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 0, &tid);

	return (res == UMESH_L2_PARSE_TID_NO_DATA);
}


/**
 * Test if TID parsing fails for 2B TID truncated to 1B.
 */
static bool umesh_l2_parse_tid_test_truncated_data_if_fails(void) {
	const uint8_t buf[2] = {0x92, 0x34};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 1, &tid);

	return (res == UMESH_L2_PARSE_TID_NO_DATA);
}


/**
 * Test if TID parsing fails for 5B TID truncated to 4B.
 */
static bool umesh_l2_parse_tid_test_longer_truncated_data_if_fails(void) {
	const uint8_t buf[5] = {0x8d, 0xd8, 0xe5, 0xa1, 0x7b};
	const uint8_t *pos = buf;
	uint32_t tid = 0;
	int32_t res = umesh_l2_parse_tid(&pos, 4, &tid);

	return (res == UMESH_L2_PARSE_TID_NO_DATA);
}


/**
 * Helper function to compare two L2 packet buffers. All struct members are
 * compared except the pointer to the packet data (the packet is also compared).
 */
static bool umesh_l2_pbuf_compare(struct umesh_l2_pbuf *pbuf1, struct umesh_l2_pbuf *pbuf2) {
	return
		(pbuf1->stid == pbuf2->stid) &&
		(pbuf1->dtid == pbuf2->dtid) &&
		(pbuf1->flags == pbuf2->flags) &&
		(pbuf1->sec_type == pbuf2->sec_type) &&
		(pbuf1->sec_algo == pbuf2->sec_algo) &&
		(pbuf1->proto == pbuf2->proto) &&
		(pbuf1->prio == pbuf2->prio) &&
		(pbuf1->size == pbuf2->size) &&
		(pbuf1->len == pbuf2->len) &&
		(pbuf1->iface == pbuf2->iface) &&
		(pbuf1->timeout == pbuf2->timeout) &&
		(pbuf1->rssi == pbuf2->rssi) &&
		(pbuf1->bit_errors == pbuf2->bit_errors) &&
		(!memcmp(pbuf1->data, pbuf2->data, pbuf1->len));
}


/**
 * Test if single byte control field parsing works. 0x28 control field should
 * set L3 proto to 2 and security algo to 2.
 */
static bool umesh_l2_control_test_short_if_ok(void) {
	const uint8_t buf[1] = {0x28};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf orig;
	umesh_l2_pbuf_init(&orig);
	orig.proto = 2;
	orig.sec_algo = 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 1, &test);

	bool ret = (pos == buf + 1) && (res == UMESH_L2_PARSE_CONTROL_OK) && umesh_l2_pbuf_compare(&orig, &test);

	umesh_l2_pbuf_free(&orig);
	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if single byte control field parsing works. 0x68 control field should
 * set L3 proto to 2, security algo to 2 and the broadcast flag should be set.
 */
static bool umesh_l2_control_test_broadcast_if_ok(void) {
	const uint8_t buf[1] = {0x68};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf orig;
	umesh_l2_pbuf_init(&orig);
	orig.proto = 2;
	orig.sec_algo = 2;
	orig.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 1, &test);

	bool ret = (pos == buf + 1) && (res == UMESH_L2_PARSE_CONTROL_OK) && umesh_l2_pbuf_compare(&orig, &test);

	umesh_l2_pbuf_free(&orig);
	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if single byte control field parsing works. Set all bits to 1 (except
 * the Ext bit).
 */
static bool umesh_l2_control_test_short_all_1_if_ok(void) {
	const uint8_t buf[1] = {0x7f};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf orig;
	umesh_l2_pbuf_init(&orig);
	orig.proto = 3;
	orig.sec_algo = 3;
	orig.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 1, &test);

	bool ret = (pos == buf + 1) && (res == UMESH_L2_PARSE_CONTROL_OK) && umesh_l2_pbuf_compare(&orig, &test);

	umesh_l2_pbuf_free(&orig);
	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if two byte control field parsing works. All bits set to 1 except the
 * Ext bit in the second byte.
 */
static bool umesh_l2_control_test_long_all_1_if_ok(void) {
	const uint8_t buf[2] = {0xff, 0x7f};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf orig;
	umesh_l2_pbuf_init(&orig);
	orig.proto = 15;
	orig.sec_algo = 7;
	orig.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 2, &test);

	bool ret = (pos = buf + 2) && (res == UMESH_L2_PARSE_CONTROL_OK) && umesh_l2_pbuf_compare(&orig, &test);

	umesh_l2_pbuf_free(&orig);
	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if two byte control field parsing fails when the Ext bit in the second
 * byte is set.
 */
static bool umesh_l2_control_test_second_ext_set_if_fails(void) {
	const uint8_t buf[2] = {0xff, 0xff};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 2, &test);

	bool ret = (res == UMESH_L2_PARSE_CONTROL_UNFINISHED);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if two byte control field parsing fails when it is cropped to 1B.
 */
static bool umesh_l2_control_test_long_cropped_if_fails(void) {
	const uint8_t buf[2] = {0xff, 0x7f};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 1, &test);

	bool ret = (res == UMESH_L2_PARSE_CONTROL_NO_DATA);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if single byte control field parsing fails when no data is available.
 */
static bool umesh_l2_control_test_short_cropped_if_fails(void) {
	const uint8_t buf[1] = {0x7f};
	const uint8_t *pos = buf;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);

	int32_t res = umesh_l2_parse_control(&pos, 0, &test);

	bool ret = (res == UMESH_L2_PARSE_CONTROL_NO_DATA);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if data parsing of an empty packet works.
 */
static bool umesh_l2_parse_data_empty_packet_if_works(void) {
	/* Packet header with no data. */
	const uint8_t buf[2] = {0xff, 0x7f};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_NONE;

	int32_t res = umesh_l2_parse_data(buf, &pos, 0, &test, NULL, 0);

	bool ret = (pos = buf + 2) && (res == UMESH_L2_PARSE_DATA_OK) && (test.len == 0);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if data parsing of a small plaintext packet works.
 */
static bool umesh_l2_parse_data_plaintext_small_packet_if_works(void) {
	const uint8_t buf[7] = {0xff, 0x7f, 0x12, 0x34, 0x56, 0x78, 0x90};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_NONE;

	int32_t res = umesh_l2_parse_data(buf, &pos, 5, &test, NULL, 0);

	bool ret = (pos = buf + 7) && (res == UMESH_L2_PARSE_DATA_OK) && (test.len == 5) && !memcmp(buf + 2, test.data, 5);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if data parsing of a too big packet fails.
 */
static bool umesh_l2_parse_data_too_big_packet_if_fails(void) {
	const uint8_t buf[2 + UMESH_L2_PACKET_SIZE + 1] = {0xff, 0x7f};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_NONE;

	int32_t res = umesh_l2_parse_data(buf, &pos, UMESH_L2_PACKET_SIZE + 1, &test, NULL, 0);

	bool ret = (res == UMESH_L2_PARSE_DATA_TOO_BIG);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if data parsing of a packet encoded with an unsupported method fails.
 */
static bool umesh_l2_parse_data_unsupported_if_fails(void) {
	const uint8_t buf[3] = {0xff, 0x7f, 0x12};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	/* Set manually to some unsupported method. */
	test.sec_algo = 7;

	int32_t res = umesh_l2_parse_data(buf, &pos, 1, &test, NULL, 0);

	bool ret = (res == UMESH_L2_PARSE_DATA_UNSUPPORTED);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if CRC16 works (5 byte packet).
 * Returned CRC for this packet should be 0x6d08.
 */
static bool umesh_l2_parse_data_crc16_if_works(void) {
	const uint8_t buf[9] = {0xff, 0x7f, 0x12, 0x34, 0x56, 0x78, 0x90, 0x6d, 0x08};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_CRC16_CCITT;

	int32_t res = umesh_l2_parse_data(buf, &pos, 7, &test, NULL, 0);

	bool ret = (res == UMESH_L2_PARSE_DATA_OK) && (test.len == 5);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if CRC16 fails on bad CRC (5 byte packet).
 * Returned CRC for this packet should be 0x6d08, it is modified to 0x7d08.
 */
static bool umesh_l2_parse_data_crc16_bad_crc_if_fails(void) {
	const uint8_t buf[9] = {0xff, 0x7f, 0x12, 0x34, 0x56, 0x78, 0x90, 0x7d, 0x08};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_CRC16_CCITT;

	int32_t res = umesh_l2_parse_data(buf, &pos, 7, &test, NULL, 0);

	bool ret = (res == UMESH_L2_PARSE_DATA_AE_FAILED);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if CRC16 fails on modified header (5 byte packet).
 * Returned CRC for this packet should be 0x6d08.
 */
static bool umesh_l2_parse_data_crc16_bad_header_if_fails(void) {
	const uint8_t buf[9] = {0xf3, 0x7f, 0x12, 0x34, 0x56, 0x78, 0x90, 0x6d, 0x08};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_CRC16_CCITT;

	int32_t res = umesh_l2_parse_data(buf, &pos, 7, &test, NULL, 0);

	bool ret = (res == UMESH_L2_PARSE_DATA_AE_FAILED);

	umesh_l2_pbuf_free(&test);

	return ret;
}


/**
 * Test if CRC16 fails on modified packet data (5 byte packet).
 * Returned CRC for this packet should be 0x6d08.
 */
static bool umesh_l2_parse_data_crc16_bad_data_if_fails(void) {
	const uint8_t buf[9] = {0xff, 0x7f, 0x12, 0x34, 0x66, 0x78, 0x90, 0x6d, 0x08};
	const uint8_t *pos = buf + 2;

	struct umesh_l2_pbuf test;
	umesh_l2_pbuf_init(&test);
	test.sec_algo = UMESH_L2_SECURITY_ALGO_CRC16_CCITT;

	int32_t res = umesh_l2_parse_data(buf, &pos, 7, &test, NULL, 0);

	bool ret = (res == UMESH_L2_PARSE_DATA_AE_FAILED);

	umesh_l2_pbuf_free(&test);

	return ret;
}


bool umesh_l2_receive_tests(void) {
	bool res = true;

	/* TID parser tests. */
	res &= u_test(umesh_l2_parse_tid_test_short_if_ok());
	res &= u_test(umesh_l2_parse_tid_test_longer_if_ok());
	res &= u_test(umesh_l2_parse_tid_test_long_if_ok());
	res &= u_test(umesh_l2_parse_tid_test_too_long_if_fails());
	res &= u_test(umesh_l2_parse_tid_test_longer_than_32bit_if_fails());
	res &= u_test(umesh_l2_parse_tid_test_no_data_if_fails());
	res &= u_test(umesh_l2_parse_tid_test_truncated_data_if_fails());
	res &= u_test(umesh_l2_parse_tid_test_longer_truncated_data_if_fails());

	/* Control field parser tests. */
	res &= u_test(umesh_l2_control_test_short_if_ok());
	res &= u_test(umesh_l2_control_test_broadcast_if_ok());
	res &= u_test(umesh_l2_control_test_short_all_1_if_ok());
	res &= u_test(umesh_l2_control_test_long_all_1_if_ok());
	res &= u_test(umesh_l2_control_test_second_ext_set_if_fails());
	res &= u_test(umesh_l2_control_test_long_cropped_if_fails());
	res &= u_test(umesh_l2_control_test_short_cropped_if_fails());

	/* Data parser tests - generic tests and plaintext data parsing. */
	res &= u_test(umesh_l2_parse_data_empty_packet_if_works());
	res &= u_test(umesh_l2_parse_data_plaintext_small_packet_if_works());
	res &= u_test(umesh_l2_parse_data_too_big_packet_if_fails());
	res &= u_test(umesh_l2_parse_data_unsupported_if_fails());

	/* Data parser tests - CRC16. */
	res &= u_test(umesh_l2_parse_data_crc16_if_works());
	res &= u_test(umesh_l2_parse_data_crc16_bad_crc_if_fails());
	res &= u_test(umesh_l2_parse_data_crc16_bad_header_if_fails());
	res &= u_test(umesh_l2_parse_data_crc16_bad_data_if_fails());

	return res;
}
