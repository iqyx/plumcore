/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NXP SJA1105 automotive Ethernet switch driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * The SJA1105P/Q/R/S register map, static-configuration table layout (block ids, entry sizes,
 * field bit positions), table/header/global CRC algorithm and the CGU clocking sequence used here
 * were cross-checked against the NXP SJA1105P/Q/R/S manual (UM11040) and the mainline Linux DSA
 * driver drivers/net/dsa/sja1105 (GPL-2.0, Copyright NXP Semiconductors, Vladimir Oltean
 * <vladimir.oltean@nxp.com>), which served as the reference for these hardware constants.
 *
 * This driver was assembled with the assistance of Claude (Anthropic) used as a coding tool; the
 * hardware constants were transcribed and verified against the references above.
 */

/**
 * @file
 *
 * Minimal driver for the second-generation NXP SJA1105P/Q/R/S 5-port automotive Ethernet switch.
 * It implements just enough to bring the device up as a transparent layer-2 switch with every port
 * in RMII mode and the switch sourcing the 50 MHz reference clock to the external PHYs.
 *
 * The device has no persistent configuration of its own: the host uploads a complete *static
 * configuration* image (a sequence of CRC-protected tables) over SPI into the configuration area,
 * after which the clock-generation unit (CGU) is programmed to drive the xMII interfaces.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <main.h>
#include <interfaces/spi.h>

#include "sja1105.h"

#define MODULE_NAME "sja1105"


/*********************************************************************************************************************
 * SPI access layer
 *
 * Every SPI access starts with a 32-bit command word (MSB first on the wire):
 *   bit 31     : access, 0 = read, 1 = write
 *   bits 30:25 : read_count, number of 32-bit words to read (writes set 0)
 *   bits 24:4  : 21-bit register/config word address
 * Data words follow big-endian; CS stays asserted for the whole transaction.
 *********************************************************************************************************************/

#define SJA1105_CMD_WRITE (1u << 31)
#define SJA1105_READ_COUNT_SHIFT 25
#define SJA1105_ADDR_SHIFT 4
#define SJA1105_ADDR_MASK 0x1FFFFFu

/* Configuration area base address (sja1105pqrs_regs.config). */
#define SJA1105_CONFIG_BASE_ADDR 0x020000u
/* Device-id register (sja1105pqrs_regs.device_id). */
#define SJA1105_REG_DEVICE_ID 0x000000u
/* Configuration status register (sja1105pqrs_regs.status). */
#define SJA1105_REG_STATUS 0x000001u
#define SJA1105_STATUS_CONFIGS (1u << 31) /* set when a valid configuration is active */
#define SJA1105_STATUS_CRCCHKL (1u << 30) /* local (per-block) CRC error               */
#define SJA1105_STATUS_CRCCHKG (1u << 28) /* global CRC error                          */


static void sja1105_put_be32(uint8_t *buf, uint32_t word) {
	buf[0] = (uint8_t)(word >> 24);
	buf[1] = (uint8_t)(word >> 16);
	buf[2] = (uint8_t)(word >> 8);
	buf[3] = (uint8_t)(word >> 0);
}


static uint32_t sja1105_get_be32(const uint8_t *buf) {
	return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
}


/* Write @p words 32-bit words starting at config-word address @p addr. The device auto-increments
 * its internal address, so a long image is split into bursts that each carry a fresh command word. */
static sja1105_ret_t sja1105_spi_write_burst(Sja1105 *self, uint32_t addr, const uint8_t *data, size_t words) {
	const size_t max_words = 64;

	while (words > 0) {
		size_t chunk = (words < max_words) ? words : max_words;
		uint8_t hdr[4];
		sja1105_put_be32(hdr, SJA1105_CMD_WRITE | ((addr & SJA1105_ADDR_MASK) << SJA1105_ADDR_SHIFT));

		self->spi->vmt->select(self->spi);
		if (self->spi->vmt->send(self->spi, hdr, sizeof(hdr)) != SPI_RET_OK ||
		    self->spi->vmt->send(self->spi, data, chunk * 4) != SPI_RET_OK) {
			self->spi->vmt->deselect(self->spi);
			return SJA1105_RET_FAILED;
		}
		self->spi->vmt->deselect(self->spi);

		addr += chunk;
		data += chunk * 4;
		words -= chunk;
	}
	return SJA1105_RET_OK;
}


sja1105_ret_t sja1105_read_reg(Sja1105 *self, uint32_t addr, uint32_t *value) {
	if (self == NULL || value == NULL) {
		return SJA1105_RET_NULL;
	}

	uint8_t hdr[4];
	uint8_t rx[4] = {0};
	sja1105_put_be32(hdr, (1u << SJA1105_READ_COUNT_SHIFT) | ((addr & SJA1105_ADDR_MASK) << SJA1105_ADDR_SHIFT));

	self->spi->vmt->select(self->spi);
	if (self->spi->vmt->send(self->spi, hdr, sizeof(hdr)) != SPI_RET_OK ||
	    self->spi->vmt->receive(self->spi, rx, sizeof(rx)) != SPI_RET_OK) {
		self->spi->vmt->deselect(self->spi);
		return SJA1105_RET_FAILED;
	}
	self->spi->vmt->deselect(self->spi);

	*value = sja1105_get_be32(rx);
	return SJA1105_RET_OK;
}


sja1105_ret_t sja1105_write_reg(Sja1105 *self, uint32_t addr, uint32_t value) {
	if (self == NULL) {
		return SJA1105_RET_NULL;
	}
	uint8_t data[4];
	sja1105_put_be32(data, value);
	return sja1105_spi_write_burst(self, addr, data, 1);
}


sja1105_ret_t sja1105_read_device_id(Sja1105 *self, uint32_t *device_id) {
	return sja1105_read_reg(self, SJA1105_REG_DEVICE_ID, device_id);
}


/*********************************************************************************************************************
 * Per-port high-level diagnostic counters
 *
 * Each port exposes a HL1 area (frame/byte counters) and a HL2 area (drop counters). The per-port
 * area base addresses and the in-area word offsets are taken from the Linux sja1105 driver
 * (sja1105pqrs_regs.stats[] and sja1105_ethtool.c, see file header). Offsets are in 32-bit words; the
 * HL1 frame/byte counters are 64-bit and stored least-significant word first (QUIRK_LSW32_IS_FIRST).
 *********************************************************************************************************************/

/* Per-port HL1/HL2 area base addresses (stride 0x10 words between ports). */
#define SJA1105_HL1_BASE(port) (0x400u + (port) * 0x10u)
#define SJA1105_HL2_BASE(port) (0x600u + (port) * 0x10u)

/* HL1 area word offsets. The N_*BYTE/N_*FRM counters are 64-bit (two words); the rest are 32-bit. */
#define SJA1105_HL1_N_TXBYTE    0x0u
#define SJA1105_HL1_N_TXFRM     0x2u
#define SJA1105_HL1_N_RXBYTE    0x4u
#define SJA1105_HL1_N_RXFRM     0x6u
#define SJA1105_HL1_N_POLERR    0x8u
#define SJA1105_HL1_N_VLNOTFOUND 0xAu
#define SJA1105_HL1_N_CRCERR    0xBu
#define SJA1105_HL1_N_SIZEERR   0xCu
#define SJA1105_HL1_N_VLANERR   0xEu

/* HL2 area word offsets (all 32-bit). */
#define SJA1105_HL2_N_NOT_REACH    0x0u
#define SJA1105_HL2_N_EGR_DISABLED 0x1u
#define SJA1105_HL2_N_PART_DROP    0x2u
#define SJA1105_HL2_N_QFULL        0x3u


/* Read a 64-bit counter held in two consecutive 32-bit words, least-significant word first. */
static sja1105_ret_t sja1105_read_counter64(Sja1105 *self, uint32_t addr, uint64_t *value) {
	uint32_t lo = 0;
	uint32_t hi = 0;
	if (sja1105_read_reg(self, addr, &lo) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, addr + 1, &hi) != SJA1105_RET_OK) {
		return SJA1105_RET_FAILED;
	}
	*value = ((uint64_t)hi << 32) | lo;
	return SJA1105_RET_OK;
}


sja1105_ret_t sja1105_read_port_counters(Sja1105 *self, unsigned int port, struct sja1105_port_counters *counters) {
	if (self == NULL || counters == NULL) {
		return SJA1105_RET_NULL;
	}
	if (port >= SJA1105_NUM_PORTS) {
		return SJA1105_RET_FAILED;
	}
	memset(counters, 0, sizeof(*counters));

	if (sja1105_read_counter64(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_RXFRM, &counters->n_rxframe) != SJA1105_RET_OK ||
	    sja1105_read_counter64(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_RXBYTE, &counters->n_rxbyte) != SJA1105_RET_OK ||
	    sja1105_read_counter64(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_TXFRM, &counters->n_txframe) != SJA1105_RET_OK ||
	    sja1105_read_counter64(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_TXBYTE, &counters->n_txbyte) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_POLERR, &counters->n_polerr) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_VLNOTFOUND, &counters->n_vlnotfound) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_CRCERR, &counters->n_crcerr) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_SIZEERR, &counters->n_sizeerr) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL1_BASE(port) + SJA1105_HL1_N_VLANERR, &counters->n_vlanerr) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL2_BASE(port) + SJA1105_HL2_N_QFULL, &counters->n_qfull) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL2_BASE(port) + SJA1105_HL2_N_PART_DROP, &counters->n_part_drop) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL2_BASE(port) + SJA1105_HL2_N_EGR_DISABLED, &counters->n_egr_disabled) != SJA1105_RET_OK ||
	    sja1105_read_reg(self, SJA1105_HL2_BASE(port) + SJA1105_HL2_N_NOT_REACH, &counters->n_not_reach) != SJA1105_RET_OK) {
		return SJA1105_RET_FAILED;
	}
	return SJA1105_RET_OK;
}


sja1105_ret_t sja1105_log_port_counters(Sja1105 *self) {
	if (self == NULL) {
		return SJA1105_RET_NULL;
	}
	for (unsigned int p = 0; p < SJA1105_NUM_PORTS; p++) {
		struct sja1105_port_counters c;
		if (sja1105_read_port_counters(self, p, &c) != SJA1105_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("port %u: counter read failed"), p);
			continue;
		}
		/* Counters are 64-bit on the wire but stay well within 32 bits during bring-up; the embedded
		 * printf has no 64-bit format support, so the values are truncated for the log line. */
		/* Compact one-liner (rxf/rxb = rx frames/bytes, txf/txb = tx; then the drop counters:
		 * pol=policer vnf=vlan-not-found crc sz=size vln=vlan-member qf=qfull pt=part egr=egr-disabled
		 * nr=not-reachable) so the line survives the console without truncation. */
		u_log(system_log, LOG_TYPE_INFO,
		      U_LOG_MODULE_PREFIX("p%u rxf%lu rxb%lu txf%lu txb%lu pol%lu vnf%lu crc%lu sz%lu vln%lu qf%lu pt%lu egr%lu nr%lu"),
		      p,
		      (unsigned long)c.n_rxframe, (unsigned long)c.n_rxbyte,
		      (unsigned long)c.n_txframe, (unsigned long)c.n_txbyte,
		      (unsigned long)c.n_polerr, (unsigned long)c.n_vlnotfound,
		      (unsigned long)c.n_crcerr, (unsigned long)c.n_sizeerr,
		      (unsigned long)c.n_vlanerr, (unsigned long)c.n_qfull,
		      (unsigned long)c.n_part_drop, (unsigned long)c.n_egr_disabled,
		      (unsigned long)c.n_not_reach);
	}
	return SJA1105_RET_OK;
}


/*********************************************************************************************************************
 * Bit packing and CRC helpers
 *********************************************************************************************************************/

/* Pack @p val into @p buf between bit positions @p lsb..@p msb (inclusive), using the SJA1105 bit
 * convention: the buffer is a sequence of big-endian 32-bit words with the least-significant word
 * stored first (QUIRK_LSW32_IS_FIRST). Bit b therefore lives in word (b / 32) at byte offset
 * word*4 + (3 - (b % 32) / 8). */
static void sja1105_pack(uint8_t *buf, uint64_t val, unsigned int msb, unsigned int lsb) {
	unsigned int width = msb - lsb + 1;
	for (unsigned int pos = 0; pos < width; pos++) {
		if ((val & ((uint64_t)1 << pos)) == 0) {
			continue;
		}
		unsigned int bit = lsb + pos;
		unsigned int word = bit / 32;
		unsigned int bit_in_word = bit % 32;
		size_t byte = word * 4 + (3 - bit_in_word / 8);
		buf[byte] |= (uint8_t)(1u << (bit_in_word % 8));
	}
}


/* Standard reflected (LSB-first) CRC32, polynomial 0xEDB88320. */
static uint32_t crc32_le_bytes(uint32_t crc, const uint8_t *data, size_t len) {
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
		}
	}
	return crc;
}


/* SJA1105 image CRC: each big-endian 32-bit word is byte-reversed to little-endian and fed through
 * the reflected CRC32, seeded with all ones and finally inverted. Matches the kernel sja1105_crc32. */
static uint32_t sja1105_crc32(const uint8_t *buf, size_t len) {
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i + 4 <= len; i += 4) {
		uint8_t le[4] = {buf[i + 3], buf[i + 2], buf[i + 1], buf[i + 0]};
		crc = crc32_le_bytes(crc, le, sizeof(le));
	}
	return ~crc;
}


/*********************************************************************************************************************
 * Static configuration assembly
 *
 * Image layout:
 *   [device-id word]
 *   for each table:
 *       [header: block-id (31:24) | length-in-words (55:32) | header CRC (95:64)]   (12 bytes)
 *       [entry data words ...]
 *       [data CRC word]
 *   [terminating header: block-id 0 | length 0 | GLOBAL CRC (over everything before this word)]
 *********************************************************************************************************************/

/* Table block identifiers (sja1105_static_config.h). */
#define SJA1105_BLKID_L2_POLICING          0x06
#define SJA1105_BLKID_VLAN_LOOKUP          0x07
#define SJA1105_BLKID_L2_FORWARDING        0x08
#define SJA1105_BLKID_MAC_CONFIG           0x09
#define SJA1105_BLKID_L2_LOOKUP_PARAMS     0x0D
#define SJA1105_BLKID_L2_FORWARDING_PARAMS 0x0E
#define SJA1105_BLKID_GENERAL_PARAMS       0x11
#define SJA1105_BLKID_XMII_PARAMS          0x4E

/* Table entry sizes (bytes) and counts for the P/Q/R/S parts (sja1105_static_config.h). */
#define SJA1105_SZ_L2_POLICING           8
#define SJA1105_CNT_L2_POLICING          45
#define SJA1105_SZ_VLAN_LOOKUP           8
#define SJA1105_SZ_L2_FORWARDING         8
#define SJA1105_CNT_L2_FORWARDING        13 /* 5 per-port + 8 per-traffic-class entries */
#define SJA1105_SZ_MAC_CONFIG            32
#define SJA1105_SZ_L2_LOOKUP_PARAMS      16
#define SJA1105_SZ_L2_FORWARDING_PARAMS  12
#define SJA1105_SZ_GENERAL_PARAMS        44
#define SJA1105_SZ_XMII_PARAMS           4
#define SJA1105_SIZE_TABLE_HEADER        12

#define SJA1105_NUM_TC 8 /* traffic classes */

/* Bitmask of all switched ports. */
#define SJA1105_ALL_PORTS ((1u << SJA1105_NUM_PORTS) - 1)

/* MAC speed field encoding (sja1105_speed_t). */
#define SJA1105_SPEED_1000MBPS 1
#define SJA1105_SPEED_100MBPS  2
#define SJA1105_SPEED_10MBPS   3


struct cfg_builder {
	uint8_t *buf;
	size_t cap;
	size_t len;
	bool overflow;
};


static void cfg_put_word(struct cfg_builder *b, uint32_t word) {
	if (b->len + 4 > b->cap) {
		b->overflow = true;
		return;
	}
	sja1105_put_be32(b->buf + b->len, word);
	b->len += 4;
}


/* Emit a complete table: header (block-id, length, header CRC over the first 8 header bytes), the
 * entry data, and the data CRC over the entry bytes. */
static void cfg_table(struct cfg_builder *b, uint8_t block_id, const uint8_t *entries, size_t entry_bytes) {
	uint8_t hdr[SJA1105_SIZE_TABLE_HEADER] = {0};

	sja1105_pack(hdr, block_id, 31, 24);
	sja1105_pack(hdr, entry_bytes / 4, 55, 32);
	sja1105_pack(hdr, sja1105_crc32(hdr, 8), 95, 64);

	if (b->len + sizeof(hdr) + entry_bytes + 4 > b->cap) {
		b->overflow = true;
		return;
	}
	memcpy(b->buf + b->len, hdr, sizeof(hdr));
	b->len += sizeof(hdr);
	memcpy(b->buf + b->len, entries, entry_bytes);
	b->len += entry_bytes;
	cfg_put_word(b, sja1105_crc32(entries, entry_bytes));
}


/*
 * Per-table builders. All bit positions are taken from the mainline Linux sja1105 driver packing
 * functions (see file header). The "dumb switch" behaviour is expressed by the field *values*:
 * every port RMII + MAC role, one shared flood/broadcast domain, no VLAN segmentation, no policing.
 */

/* xMII Mode Parameters: one entry; per-port interface mode (RMII) and MAC role. */
static void build_xmii_params(struct cfg_builder *b) {
	uint8_t e[SJA1105_SZ_XMII_PARAMS];
	memset(e, 0, sizeof(e));
	for (unsigned int p = 0; p < SJA1105_NUM_PORTS; p++) {
		unsigned int off = 17 + 3 * p;
		sja1105_pack(e, SJA1105_XMII_MODE_RMII, off + 1, off); /* xmii_mode[p] */
		sja1105_pack(e, SJA1105_PORT_MAC, off + 2, off + 2);   /* phy_mac[p]   */
	}
	cfg_table(b, SJA1105_BLKID_XMII_PARAMS, e, sizeof(e));
}


/* MAC Configuration: one entry per port. 100 Mbps, ingress/egress and learning enabled, with the 8
 * priority queues laid out across the per-port memory (8 x 128 blocks). */
static void build_mac_config(struct cfg_builder *b) {
	uint8_t all[SJA1105_SZ_MAC_CONFIG * SJA1105_NUM_PORTS];
	memset(all, 0, sizeof(all));

	for (unsigned int p = 0; p < SJA1105_NUM_PORTS; p++) {
		uint8_t *e = all + p * SJA1105_SZ_MAC_CONFIG;

		for (unsigned int q = 0; q < SJA1105_NUM_TC; q++) {
			unsigned int off = 104 + 19 * q;
			unsigned int base = q * 128;
			unsigned int top = base + 127;
			sja1105_pack(e, 1, off, off);                 /* enabled[q] */
			sja1105_pack(e, base, off + 9, off + 1);      /* base[q]    */
			sja1105_pack(e, top, off + 18, off + 10);     /* top[q]     */
		}

		sja1105_pack(e, SJA1105_SPEED_10MBPS, 98, 97); /* speed: LAN8671 is 10BASE-T1S (10 Mbps) */
		sja1105_pack(e, 1, 33, 33);                     /* dyn_learn */
		sja1105_pack(e, 1, 32, 32);                     /* egress    */
		sja1105_pack(e, 1, 31, 31);                     /* ingress   */
	}
	cfg_table(b, SJA1105_BLKID_MAC_CONFIG, all, sizeof(all));
}


/* VLAN Lookup: a single VLAN (id 0) spanning every port, untagged on egress -> no segmentation. */
static void build_vlan_lookup(struct cfg_builder *b) {
	uint8_t e[SJA1105_SZ_VLAN_LOOKUP];
	memset(e, 0, sizeof(e));

	sja1105_pack(e, SJA1105_ALL_PORTS, 53, 49); /* vmemb_port : members        */
	sja1105_pack(e, SJA1105_ALL_PORTS, 48, 44); /* vlan_bc    : broadcast dom   */
	sja1105_pack(e, 0, 43, 39);                 /* tag_port   : egress untagged */
	sja1105_pack(e, 0, 38, 27);                 /* vlanid     : VLAN 0          */
	cfg_table(b, SJA1105_BLKID_VLAN_LOOKUP, e, sizeof(e));
}


/* L2 Forwarding: per-ingress-port reach/broadcast/flood domains (entries 0..4), then the per-traffic
 * class priority map entries (5..12). Every port reaches every other port and unknown/broadcast/
 * multicast traffic floods to all other ports. */
static void build_l2_forwarding(struct cfg_builder *b) {
	uint8_t all[SJA1105_SZ_L2_FORWARDING * SJA1105_CNT_L2_FORWARDING];
	memset(all, 0, sizeof(all));

	for (unsigned int p = 0; p < SJA1105_NUM_PORTS; p++) {
		uint8_t *e = all + p * SJA1105_SZ_L2_FORWARDING;
		unsigned int others = SJA1105_ALL_PORTS & ~(1u << p);
		sja1105_pack(e, others, 63, 59); /* bc_domain  */
		sja1105_pack(e, others, 58, 54); /* reach_port */
		sja1105_pack(e, others, 53, 49); /* fl_domain  */
	}
	for (unsigned int tc = 0; tc < SJA1105_NUM_TC; tc++) {
		uint8_t *e = all + (SJA1105_NUM_PORTS + tc) * SJA1105_SZ_L2_FORWARDING;
		for (unsigned int port = 0; port < SJA1105_NUM_PORTS; port++) {
			unsigned int off = 25 + 3 * port;
			sja1105_pack(e, tc, off + 2, off); /* vlan_pmap[port] = traffic class */
		}
	}
	cfg_table(b, SJA1105_BLKID_L2_FORWARDING, all, sizeof(all));
}


/* L2 Forwarding Parameters: give the single shared frame-memory partition the whole 1000 blocks. */
static void build_l2_forwarding_params(struct cfg_builder *b) {
	uint8_t e[SJA1105_SZ_L2_FORWARDING_PARAMS];
	memset(e, 0, sizeof(e));
	sja1105_pack(e, 1000, 22, 13); /* part_spc[0] */
	cfg_table(b, SJA1105_BLKID_L2_FORWARDING_PARAMS, e, sizeof(e));
}


/* L2 Lookup Parameters: enable a single shared learning table with a sane ageing time. */
static void build_l2_lookup_params(struct cfg_builder *b) {
	uint8_t e[SJA1105_SZ_L2_LOOKUP_PARAMS];
	memset(e, 0, sizeof(e));
	sja1105_pack(e, 0xFF, 57, 43); /* maxage                                  */
	sja1105_pack(e, 1, 27, 27);    /* shared_learn : one shared address table */
	cfg_table(b, SJA1105_BLKID_L2_LOOKUP_PARAMS, e, sizeof(e));
}


/* L2 Policing leaky-bucket parameters for a fully-permissive per-port policer. `smax` is the bucket
 * depth (burst), `rate` its drain rate (SJA1105_RATE_MBPS(1000) = 64000, i.e. 1 Gbps) and `maxlen`
 * covers a full VLAN-tagged frame plus FCS (1518 + 4). */
#define SJA1105_POLICING_SMAX   0xFFFFu
#define SJA1105_POLICING_RATE   64000u
#define SJA1105_POLICING_MAXLEN 1522u

/* L2 Policing: 45 entries = 8 per-(port, traffic-class) policers per port (indices port*8 + tc) plus
 * one broadcast policer per port (indices 40 + port). Following the mainline driver, every entry of a
 * port shares a single per-port policer by pointing its sharindx at the matchall entry (index ==
 * port); only those NUM_PORTS matchall entries carry the actual bucket parameters. Giving each entry
 * its own private policer instead makes the switch drop every frame on the policer (N_POLERR), so the
 * shared layout is required, not just cosmetic. */
static void build_l2_policing(struct cfg_builder *b) {
	static uint8_t all[SJA1105_SZ_L2_POLICING * SJA1105_CNT_L2_POLICING];
	memset(all, 0, sizeof(all));

	/* Point every per-(port, tc) and per-port broadcast policer at the port's matchall policer. */
	for (unsigned int port = 0; port < SJA1105_NUM_PORTS; port++) {
		for (unsigned int tc = 0; tc < SJA1105_NUM_TC; tc++) {
			uint8_t *e = all + (port * SJA1105_NUM_TC + tc) * SJA1105_SZ_L2_POLICING;
			sja1105_pack(e, port, 63, 58); /* sharindx -> matchall policer for this port */
		}
		uint8_t *bc = all + (SJA1105_NUM_PORTS * SJA1105_NUM_TC + port) * SJA1105_SZ_L2_POLICING;
		sja1105_pack(bc, port, 63, 58);        /* sharindx -> matchall policer for this port */
	}

	/* The matchall entries (indices 0..NUM_PORTS-1) hold the permissive bucket parameters that every
	 * sharing entry of the corresponding port inherits. Their own sharindx is left from the loop
	 * above (they belong to port 0's traffic classes), matching the mainline layout. */
	for (unsigned int port = 0; port < SJA1105_NUM_PORTS; port++) {
		uint8_t *e = all + port * SJA1105_SZ_L2_POLICING;
		sja1105_pack(e, SJA1105_POLICING_SMAX, 57, 42);   /* smax   : burst  */
		sja1105_pack(e, SJA1105_POLICING_RATE, 41, 26);   /* rate   : drain  */
		sja1105_pack(e, SJA1105_POLICING_MAXLEN, 25, 15); /* maxlen          */
		sja1105_pack(e, 0, 14, 12);                       /* partition       */
	}
	cfg_table(b, SJA1105_BLKID_L2_POLICING, all, sizeof(all));
}


/* Management/link-local MAC address filters. The general-parameters table always runs two address
 * filters: a frame whose destination matches mac_fltresN under mask mac_fltN is trapped as a
 * management frame and sent to the host port. A zero mask means "no bits need to match", so the
 * comparison (dmac & mask) == (fltres & mask) reduces to 0 == 0 and *every* frame is trapped instead
 * of switched. Restrict the two filters to the standard link-local multicast ranges (01:80:C2:xx:xx:xx
 * and 01:1B:19:xx:xx:xx) like the mainline driver so only those control frames are trapped and all
 * ordinary unicast/broadcast traffic is switched normally. */
#define SJA1105_LINKLOCAL_FILTER_A      0x0180C2000000ull
#define SJA1105_LINKLOCAL_FILTER_A_MASK 0xFFFFFF000000ull
#define SJA1105_LINKLOCAL_FILTER_B      0x011B19000000ull
#define SJA1105_LINKLOCAL_FILTER_B_MASK 0xFFFFFF000000ull

/* General Parameters: no CPU/host port, no cascading, no mirroring (port index == SJA1105_NUM_PORTS
 * means "no such port"), with the management filters restricted to the link-local ranges. */
static void build_general_params(struct cfg_builder *b) {
	uint8_t e[SJA1105_SZ_GENERAL_PARAMS];
	memset(e, 0, sizeof(e));
	sja1105_pack(e, SJA1105_LINKLOCAL_FILTER_A, 343, 296);      /* mac_fltres1 */
	sja1105_pack(e, SJA1105_LINKLOCAL_FILTER_B, 295, 248);      /* mac_fltres0 */
	sja1105_pack(e, SJA1105_LINKLOCAL_FILTER_A_MASK, 247, 200); /* mac_flt1    */
	sja1105_pack(e, SJA1105_LINKLOCAL_FILTER_B_MASK, 199, 152); /* mac_flt0    */
	sja1105_pack(e, SJA1105_NUM_PORTS, 147, 145);              /* casc_port : none */
	sja1105_pack(e, SJA1105_NUM_PORTS, 144, 142);              /* host_port : none */
	sja1105_pack(e, SJA1105_NUM_PORTS, 141, 139);              /* mirr_port : none */
	cfg_table(b, SJA1105_BLKID_GENERAL_PARAMS, e, sizeof(e));
}


/* Module-level buffer for the assembled image (kept off the task stack). */
static uint8_t sja1105_config_buf[2048];


static sja1105_ret_t sja1105_build_static_config(Sja1105 *self, size_t *out_len) {
	struct cfg_builder b = {
		.buf = sja1105_config_buf,
		.cap = sizeof(sja1105_config_buf),
		.len = 0,
		.overflow = false,
	};

	/* Device id leads the image. */
	cfg_put_word(&b, self->device_id);

	/* Tables, in ascending block-id order. */
	build_l2_policing(&b);
	build_vlan_lookup(&b);
	build_l2_forwarding(&b);
	build_mac_config(&b);
	build_l2_lookup_params(&b);
	build_l2_forwarding_params(&b);
	build_general_params(&b);
	build_xmii_params(&b);

	/* Terminating header: block-id 0, length 0, and the global CRC over everything before it. */
	cfg_put_word(&b, 0);
	cfg_put_word(&b, 0);
	cfg_put_word(&b, sja1105_crc32(b.buf, b.len));

	if (b.overflow) {
		return SJA1105_RET_FAILED;
	}
	*out_len = b.len;
	return SJA1105_RET_OK;
}


/*********************************************************************************************************************
 * Clock generation unit (CGU) — RMII reference clock output
 *
 * After a valid static configuration is loaded, the CGU is programmed so that PLL1 produces 50 MHz
 * (from the device's 25 MHz reference) and each port drives its RMII reference clock from it, making
 * REF_CLK an output towards the external PHYs. Register addresses and control words are taken from
 * the Linux sja1105 driver (sja1105pqrs_regs and sja1105_clocking.c).
 *********************************************************************************************************************/

/* CGU register addresses (sja1105pqrs_regs). The per-port MII clock block starts at 0x100013 with a
 * stride of 6 registers per port. */
#define SJA1105_CGU_RMII_PLL1 0x10000Au
#define SJA1105_CGU_IDIV(port) (0x10000Bu + (port))
#define SJA1105_CGU_RMII_REF_CLK(port) (0x100015u + (port) * 6u)
#define SJA1105_CGU_RMII_EXT_TX_CLK(port) (0x100017u + (port) * 6u)

/* PLL1 control words for a 50 MHz RMII reference (pllclksrc=0xA, msel=1, psel=1, fbsel=1,
 * autoblock=1); first with PD=1, then PD=0 to enable. */
#define SJA1105_CGU_PLL1_PD 0x0A010941u
#define SJA1105_CGU_PLL1_ON 0x0A010940u

/* IDIV control word disabling the integer divider (clksrc=0xA, autoblock=1, idiv=0, pd=1). */
#define SJA1105_CGU_IDIV_DISABLE 0x0A000801u

/* CGU MII clock-control word: clksrc[28:24], autoblock[11], pd[0]. */
#define SJA1105_CGU_MII_CTRL(clksrc) (((uint32_t)(clksrc) << 24) | (1u << 11))
/* Clock sources (CLKSRC_*): PLL1, and the per-port MIIn_TX_CLK used to drive the RMII ref clock. */
#define SJA1105_CLKSRC_PLL1 0x0Eu
#define SJA1105_CLKSRC_MII_TX_CLK(port) (2u * (port))


static sja1105_ret_t sja1105_setup_rmii_clocking(Sja1105 *self) {
	/* Bring PLL1 up at 50 MHz: configure powered down, then enable. */
	if (sja1105_write_reg(self, SJA1105_CGU_RMII_PLL1, SJA1105_CGU_PLL1_PD) != SJA1105_RET_OK ||
	    sja1105_write_reg(self, SJA1105_CGU_RMII_PLL1, SJA1105_CGU_PLL1_ON) != SJA1105_RET_OK) {
		return SJA1105_RET_FAILED;
	}
	/* Allow the PLL to lock before routing it to the port clocks. */
	vTaskDelay(pdMS_TO_TICKS(2));

	for (unsigned int p = 0; p < SJA1105_NUM_PORTS; p++) {
		if (sja1105_write_reg(self, SJA1105_CGU_IDIV(p), SJA1105_CGU_IDIV_DISABLE) != SJA1105_RET_OK ||
		    sja1105_write_reg(self, SJA1105_CGU_RMII_REF_CLK(p),
		                      SJA1105_CGU_MII_CTRL(SJA1105_CLKSRC_MII_TX_CLK(p))) != SJA1105_RET_OK ||
		    sja1105_write_reg(self, SJA1105_CGU_RMII_EXT_TX_CLK(p),
		                      SJA1105_CGU_MII_CTRL(SJA1105_CLKSRC_PLL1)) != SJA1105_RET_OK) {
			return SJA1105_RET_FAILED;
		}
	}
	return SJA1105_RET_OK;
}


/*********************************************************************************************************************
 * Public API
 *********************************************************************************************************************/

sja1105_ret_t sja1105_init(Sja1105 *self, SpiDev *spi) {
	if (self == NULL || spi == NULL) {
		return SJA1105_RET_NULL;
	}
	memset(self, 0, sizeof(*self));
	self->spi = spi;

	if (sja1105_read_device_id(self, &self->device_id) != SJA1105_RET_OK) {
		return SJA1105_RET_FAILED;
	}
	if (self->device_id == 0x00000000u || self->device_id == 0xFFFFFFFFu) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no device responding (id 0x%08x)"),
		      self->device_id);
		return SJA1105_RET_NO_DEVICE;
	}
	if (self->device_id != SJA1105PR_DEVICE_ID && self->device_id != SJA1105QS_DEVICE_ID) {
		u_log(system_log, LOG_TYPE_ERROR,
		      U_LOG_MODULE_PREFIX("unsupported device id 0x%08x (only P/Q/R/S supported)"),
		      self->device_id);
		return SJA1105_RET_UNSUPPORTED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialised, device id 0x%08x"), self->device_id);
	return SJA1105_RET_OK;
}


sja1105_ret_t sja1105_free(Sja1105 *self) {
	if (self == NULL) {
		return SJA1105_RET_NULL;
	}
	self->spi = NULL;
	return SJA1105_RET_OK;
}


sja1105_ret_t sja1105_setup_dumb_switch(Sja1105 *self) {
	if (self == NULL) {
		return SJA1105_RET_NULL;
	}

	size_t len = 0;
	if (sja1105_build_static_config(self, &len) != SJA1105_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("static config assembly failed"));
		return SJA1105_RET_FAILED;
	}

	if (sja1105_spi_write_burst(self, SJA1105_CONFIG_BASE_ADDR, sja1105_config_buf, len / 4) != SJA1105_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("config upload failed"));
		return SJA1105_RET_FAILED;
	}

	/* Give the device a moment to validate the freshly uploaded image. */
	vTaskDelay(pdMS_TO_TICKS(2));

	uint32_t status = 0;
	if (sja1105_read_reg(self, SJA1105_REG_STATUS, &status) != SJA1105_RET_OK) {
		return SJA1105_RET_FAILED;
	}
	if ((status & SJA1105_STATUS_CONFIGS) == 0) {
		u_log(system_log, LOG_TYPE_ERROR,
		      U_LOG_MODULE_PREFIX("device rejected static config (status 0x%08x%s%s)"), status,
		      (status & SJA1105_STATUS_CRCCHKL) ? ", local CRC error" : "",
		      (status & SJA1105_STATUS_CRCCHKG) ? ", global CRC error" : "");
		return SJA1105_RET_CONFIG_INVALID;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("static config accepted (%u bytes)"),
	      (unsigned int)len);

	if (sja1105_setup_rmii_clocking(self) != SJA1105_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("RMII clocking setup failed"));
		return SJA1105_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("configured as transparent RMII switch"));
	return SJA1105_RET_OK;
}
