/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Blake2s-SIV implementation
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define B2S_KE_LEN 16
#define B2S_KM_LEN 16

/**
 * @brief Defive Ke and Km keys from an arbitrary-legnth key
 */
void b2s_derive_keys(const uint8_t *key, size_t len, uint32_t ke[B2S_KE_LEN], uint32_t km[B2S_KM_LEN]);


/**
 * @brief Encrypt a message
 *
 * The encryption is reversible. This single function can be used bot for
 * encryption and decryption.
 */
void b2s_crypt(uint8_t *buf, size_t len, const uint8_t *siv, size_t siv_len, const uint8_t ke[B2S_KE_LEN]);


/**
 * @brief Produce a message SIV
 */
void b2s_siv(const uint8_t *buf, size_t len, uint8_t *siv, size_t siv_len, const uint8_t km[B2S_KM_LEN]);

