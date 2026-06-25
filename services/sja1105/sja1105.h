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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <interfaces/spi.h>


/* The SJA1105 family has 5 switched Ethernet ports (0..4). */
#define SJA1105_NUM_PORTS 5

/* Device identification words returned by the device-id register (address 0x0). The driver only
 * supports the second-generation P/Q/R/S parts; the first-generation E/T parts use a different
 * static-configuration and clocking layout. */
#define SJA1105E_DEVICE_ID  0x9C00000Cu
#define SJA1105T_DEVICE_ID  0x9E00030Eu
#define SJA1105PR_DEVICE_ID 0xAF00030Eu
#define SJA1105QS_DEVICE_ID 0xAE00030Eu


typedef enum {
	SJA1105_RET_OK = 0,
	SJA1105_RET_FAILED,
	SJA1105_RET_NULL,
	SJA1105_RET_UNSUPPORTED,
	SJA1105_RET_NO_DEVICE,
	SJA1105_RET_CONFIG_INVALID,
} sja1105_ret_t;


/* xMII per-port interface mode. Encoding matches the xMII Mode Parameters table field. */
enum sja1105_xmii_mode {
	SJA1105_XMII_MODE_MII   = 0,
	SJA1105_XMII_MODE_RMII  = 1,
	SJA1105_XMII_MODE_RGMII = 2,
	SJA1105_XMII_MODE_SGMII = 3, /* port 4 only, R/S parts */
};

/* Whether a port behaves as a MAC or as a PHY on its xMII interface. In RMII, MAC mode is used
 * when the switch sources the 50 MHz reference clock towards an external PHY. */
enum sja1105_phy_mac {
	SJA1105_PORT_MAC = 0,
	SJA1105_PORT_PHY = 1,
};


/* Driver instance. Not typedef'd-from-anonymous per project policy; opaque handle so typedef. */
typedef struct sja1105 {
	SpiDev *spi;
	uint32_t device_id;
} Sja1105;


/**
 * Bind the driver to an already-initialised SPI device and read back the device id. Fails with
 * SJA1105_RET_NO_DEVICE if the part does not answer and SJA1105_RET_UNSUPPORTED for a non-P/Q/R/S
 * device.
 */
sja1105_ret_t sja1105_init(Sja1105 *self, SpiDev *spi);
sja1105_ret_t sja1105_free(Sja1105 *self);

/* Low level register access. Addresses are 21-bit; values are single 32-bit words. */
sja1105_ret_t sja1105_read_reg(Sja1105 *self, uint32_t addr, uint32_t *value);
sja1105_ret_t sja1105_write_reg(Sja1105 *self, uint32_t addr, uint32_t value);
sja1105_ret_t sja1105_read_device_id(Sja1105 *self, uint32_t *device_id);

/* Per-port high-level diagnostic counters read back from the switch. The frame/byte counters are the
 * 64-bit HL1 counters; the drop counters are the 32-bit HL2 counters. Together they answer the basic
 * question "are frames passing through this port and is any of the traffic being dropped". */
struct sja1105_port_counters {
	uint64_t n_rxframe;     /* frames received on the port (N_RXFRM)                          */
	uint64_t n_rxbyte;      /* bytes received on the port (N_RXBYTE)                          */
	uint64_t n_txframe;     /* frames transmitted on the port (N_TXFRM)                       */
	uint64_t n_txbyte;      /* bytes transmitted on the port (N_TXBYTE)                       */
	uint32_t n_polerr;      /* frames dropped by a policer (N_POLERR)                         */
	uint32_t n_vlnotfound;  /* frames dropped, frame VLAN not in the VLAN table (N_VLNOTFOUND)*/
	uint32_t n_crcerr;      /* frames dropped due to a CRC error (N_CRCERR)                   */
	uint32_t n_sizeerr;     /* frames dropped due to an invalid size (N_SIZEERR)              */
	uint32_t n_vlanerr;     /* frames dropped, ingress port not a VLAN member (N_VLANERR)     */
	uint32_t n_qfull;       /* frames dropped due to a full output queue (N_QFULL)            */
	uint32_t n_part_drop;   /* frames dropped due to a full memory partition (N_PART_DROP)    */
	uint32_t n_egr_disabled;/* frames dropped, egress disabled on the destination (N_EGR_DISABLED) */
	uint32_t n_not_reach;   /* frames dropped, destination port not reachable (N_NOT_REACH)   */
};

/**
 * Read the high-level diagnostic counters of a single switch port (0..SJA1105_NUM_PORTS-1) into
 * @p counters.
 */
sja1105_ret_t sja1105_read_port_counters(Sja1105 *self, unsigned int port, struct sja1105_port_counters *counters);

/**
 * Read the counters of every switch port and log a one-line summary per port through the system log.
 * Intended as a quick way to check whether any frames are flowing through the switch.
 */
sja1105_ret_t sja1105_log_port_counters(Sja1105 *self);

/**
 * Build a minimal static configuration that turns the device into a transparent ("dumb") layer-2
 * switch: every port is configured for RMII, the switch sources the 50 MHz reference clock to the
 * external PHYs, all ports share a single broadcast/flood domain and there is no VLAN segmentation,
 * policing or ACL filtering. The configuration is assembled in RAM, uploaded over SPI and verified,
 * then the clock-generation unit is programmed for RMII.
 */
sja1105_ret_t sja1105_setup_dumb_switch(Sja1105 *self);
