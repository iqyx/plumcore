/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Protocol service for remote configuration tree access
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/conf.h>
#include <interfaces/datagram.h>

/*
 * Protocol overview
 * =================
 * All messages are CBOR maps. Requests must include a "c" key (command string)
 * and a "p" key (path array of strings from root, may be absent/empty for root).
 * Responses are CBOR maps; on error they contain an "err" text string key.
 *
 * Path encoding
 * -------------
 * "p": ["level0_name", "level1_name", ...]   -- [] or absent means root
 *
 * Commands and their additional request/response fields
 * -----------------------------------------------------
 *  stat             p → {n, t, f}
 *  read             p → {t, val}
 *  write            p, t, val → {ret:"ok"}
 *  walk    p, dir, [limit] → {nodes:[{n,t,f},...]}
 *                            dir: 0=next 1=prev 2=child 3=up
 *                            next/prev: up to min(limit, MAX_SIBLINGS) consecutive nodes
 *                            child/up: at most 1 node
 *  create           p, name → {n, t, f}
 *  destroy          p → {ret:"ok"}
 *  get_default      p → {t, val}
 *  get_desc         p → {brief, detail}
 *  get_constraints  p → type-specific map
 *
 * Field names
 * -----------
 *  n     node name string
 *  t     conf_type integer
 *  f     conf_flag bitmask integer
 *  val   typed CBOR value (float/int/bool/bytes/text)
 *  dir   conf_dir integer
 *  limit max nodes to return in walk next/prev (optional, default MAX_SIBLINGS)
 *  name  new child name for create
 *  nodes array of {n,t,f} maps in walk response
 */

typedef enum {
	PROTO_CONF_RET_OK = 0,
	PROTO_CONF_RET_FAILED,
} proto_conf_ret_t;


typedef struct proto_conf {
	Datagram *d;
	Conf *root;
	TaskHandle_t task;
	uint8_t rx_buf[CONFIG_SERVICE_PROTO_CONF_MAX_DATAGRAM_LEN];
	uint8_t tx_buf[CONFIG_SERVICE_PROTO_CONF_MAX_DATAGRAM_LEN];

	uint8_t src_addr[4];
	uint32_t src_port;
} ProtoConf;


proto_conf_ret_t proto_conf_init(ProtoConf *self, Datagram *d, Conf *root);
proto_conf_ret_t proto_conf_free(ProtoConf *self);
