/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-s2-main-g4 main module for the S2 platform
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <stm32g4xx.h>

#include <main.h>
#include "port.h"

#include <interfaces/servicelocator.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>

/* Low level drivers for the STM32G4 family */
#include <services/stm32-clock/stm32-clock.h>
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-watchdog/watchdog.h>

#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>

#include <interfaces/led-sequences.h>
#include <services/gpio-led/gpio-led.h>

/* For computing nbus2 service identifiers. */
#include <blake2s.h>

#include <services/nbus2/nbus2.h>
#include <services/proto-dgstream/proto-dgstream.h>
#include <services/proto-dgtext/proto-dgtext.h>
#include <services/nbus-flash/nbus-flash.h>
#include <services/proto-conf/proto-conf.h>
#include <services/flash-cbor-mib/flash-cbor-mib.h>

#include <services/stm32-spi/stm32-spi.h>
#include <services/ncn26010/ncn26010.h>


#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;

Stm32Clock cmgr;
Watchdog watchdog;

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;


int32_t port_early_init(void) {
	SystemCoreClock = 16e6;

	/* Enable the HSE crystal oscillator and run the system clock from it until the clock manager
	 * takes over and reconfigures the PLL for the target SYSCLK. */
	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY)) {
		;
	}
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
		;
	}

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart1;
static void console_init(void) {
	/* USART1 RX/TX on PA9/PA10, AF7. */
	gpioa.pin[9].vmt->set_mode(&(gpioa.pin[9]), MODE_ALTERNATE);
	gpioa.pin[9].vmt->set_pull(&(gpioa.pin[9]), PULL_UP);
	gpioa.pin[9].vmt->set_pinmux(&(gpioa.pin[9]), 7);
	gpioa.pin[10].vmt->set_mode(&(gpioa.pin[10]), MODE_ALTERNATE);
	gpioa.pin[10].vmt->set_pull(&(gpioa.pin[10]), PULL_UP);
	gpioa.pin[10].vmt->set_pinmux(&(gpioa.pin[10]), 7);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart1, (void *)USART1);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 115200);

	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_SetPriority(USART1_IRQn, 7);

	/* Advertise the console stream output and set it as default for log output. */
	Stream *console = &uart1.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}


void usart1_isr(void);
void usart1_isr(void) {
	stm32_uart_interrupt_handler(&uart1);
}


/**********************************************************************************************************************
 * Stacking connector nbus2 port init
 *
 * Disabled: USART3 is repurposed for the proto-dgstream <-> UDP bridge below. Kept for reference.
 **********************************************************************************************************************/

#if 0
Stm32Uart nbus2_uart;

/* MIB configuration tree read from the "mib" flash partition (populated by port_flash_init). mib_root
 * is the synthesized Conf tree served over nbus2 by proto-conf; it stays NULL if no valid MIB is
 * present. */
FlashCborMib mib;
Conf *mib_root;

/* nbus2 on the stacking connector (USART3, framed with proto-dgstream). */
ProtoDgstream nbus2_dgstream;
Nbus nbus;
struct nbus_socket *nbus_flash_socket;
NbusFlash nbus_flash;
struct nbus_socket *nbus_conf_socket;
ProtoConf nbus_conf;

/* A second nbus2 instance on the serial console (USART1, framed as printable text with proto-dgtext). */
ProtoDgtext console_dgtext;
Nbus console_nbus;
struct nbus_socket *console_flash_socket;
NbusFlash console_flash;
struct nbus_socket *console_conf_socket;
ProtoConf console_conf;

static void nbus2_init(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;

	/* USART3 TX on PB10, AF7, open-drain half-duplex. */
	gpiob.pin[10].vmt->set_mode(&(gpiob.pin[10]), MODE_ALTERNATE);
	gpiob.pin[10].vmt->set_otype(&(gpiob.pin[10]), OTYPE_OD);
	gpiob.pin[10].vmt->set_pull(&(gpiob.pin[10]), PULL_UP);
	gpiob.pin[10].vmt->set_pinmux(&(gpiob.pin[10]), 7);

	stm32_uart_init(&nbus2_uart, (void *)USART3);
	stm32_uart_set_swmode(&nbus2_uart);
	stm32_uart_set_rto(&nbus2_uart, true);
	nbus2_uart.uart.vmt->set_bitrate(&nbus2_uart.uart, 500000);

	NVIC_EnableIRQ(USART3_IRQn);
	NVIC_SetPriority(USART3_IRQn, 7);

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&nbus2_uart.stream, "nbus2_stream");

	const uint8_t local_ep = 1;
	uint8_t local_id[4];
	blake2s(local_id, sizeof(local_id), "", 0, UNIQUE_ID_REG, UNIQUE_ID_REG_LEN);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("nbus2 service at %02x%02x%02x%02x, endpoint %d"), local_id[0], local_id[1], local_id[2], local_id[3], local_ep);

	/* Descriptors periodically advertised on the flash and configuration sockets. */
	const struct nbus_socket_descriptor flash_descriptor = {
		.protocol = "flash",
		.protocol_version = NBUS_FLASH_INTERFACE_VERSION,
	};
	const struct nbus_socket_descriptor conf_descriptor = {
		.protocol = "conf",
		.protocol_version = "1.0.0",
	};

	/* Frame nbus2 packets onto the USART3 byte stream and run nbus2 on top of the resulting Datagram
	 * interface. The proto-dgstream service handles medium access (framing and the inter-frame gaps);
	 * nbus2 only deals with the protocol itself. */
	Datagram *nbus_dgram = NULL;
	proto_dgstream_init(&nbus2_dgstream, &nbus2_uart.stream);
	proto_dgstream_get_datagram(&nbus2_dgstream, &nbus_dgram);

	/* Transmit the current ChaCha20+HalfSipHash scheme; accept both schemes on receive. */
	const struct nbus_config nbus_config = {
		.dgram = nbus_dgram,
		.tx_crypto = NBUS_CRYPTO_CHACHA20_HALFSIPHASH,
		.rx_crypto = NBUS_CRYPTO_BLAKE2S_SIV | NBUS_CRYPTO_CHACHA20_HALFSIPHASH,
	};
	nbus_init(&nbus, &nbus_config);
	nbus_set_mac_key(&nbus, (uint8_t *)"abcd", 4);

	/* nbus-flash serves any advertised flash partition. Address it at base+1 as in v35v-app. */
	local_id[3] += 1;
	nbus_flash_socket = nbus_socket_allocate(&nbus);
	nbus_socket_bind(nbus_flash_socket, local_id, local_ep);
	nbus_flash_init(&nbus_flash, &nbus_flash_socket->datagram);
	nbus_socket_set_descriptor(nbus_flash_socket, &flash_descriptor);

	/* proto-conf serves the MIB configuration tree at base+2. */
	local_id[3] += 1;
	nbus_conf_socket = nbus_socket_allocate(&nbus);
	nbus_socket_bind(nbus_conf_socket, local_id, local_ep);
	proto_conf_init(&nbus_conf, &nbus_conf_socket->datagram, mib_root);
	nbus_socket_set_descriptor(nbus_conf_socket, &conf_descriptor);

	/* Expose the same nbus-flash service on the serial console for testing with the host tools.
	 * Datagrams are framed as printable text lines (proto-dgtext) so they survive the text-only
	 * console, and a second nbus2 instance runs on top. This link uses BLAKE2s-SIV in both directions
	 * to match the host-side pynbus2 dgtext+serial transport. */
	Datagram *console_dgram = NULL;
	proto_dgtext_init(&console_dgtext, &uart1.stream);
	proto_dgtext_get_datagram(&console_dgtext, &console_dgram);

	const struct nbus_config console_nbus_config = {
		.dgram = console_dgram,
		.tx_crypto = NBUS_CRYPTO_BLAKE2S_SIV,
		.rx_crypto = NBUS_CRYPTO_BLAKE2S_SIV,
	};
	nbus_init(&console_nbus, &console_nbus_config);
	nbus_set_mac_key(&console_nbus, (uint8_t *)"abcd", 4);

	uint8_t console_id[4];
	blake2s(console_id, sizeof(console_id), "", 0, UNIQUE_ID_REG, UNIQUE_ID_REG_LEN);
	console_id[3] += 1;
	console_flash_socket = nbus_socket_allocate(&console_nbus);
	nbus_socket_bind(console_flash_socket, console_id, local_ep);
	nbus_flash_init(&console_flash, &console_flash_socket->datagram);
	nbus_socket_set_descriptor(console_flash_socket, &flash_descriptor);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("nbus-flash on serial console at %02x%02x%02x%02x, endpoint %d"), console_id[0], console_id[1], console_id[2], console_id[3], local_ep);

	console_id[3] += 1;
	console_conf_socket = nbus_socket_allocate(&console_nbus);
	nbus_socket_bind(console_conf_socket, console_id, local_ep);
	proto_conf_init(&console_conf, &console_conf_socket->datagram, mib_root);
	nbus_socket_set_descriptor(console_conf_socket, &conf_descriptor);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("proto-conf on serial console at %02x%02x%02x%02x, endpoint %d"), console_id[0], console_id[1], console_id[2], console_id[3], local_ep);
}


void usart3_isr(void);
void usart3_isr(void) {
	stm32_uart_interrupt_handler(&nbus2_uart);
}
#endif


/**********************************************************************************************************************
 * Ethernet init
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)
Stm32SpiBus spi3;
Stm32SpiDev spi3_ncn;
Ncn26010 ncn;

static void ethernet_init(void) {
	/* SPI3 SCK/MISO/MOSI on PC10/PC11/PB5, AF6. */
	gpioc.pin[10].vmt->set_mode(&(gpioc.pin[10]), MODE_ALTERNATE);
	gpioc.pin[10].vmt->set_pinmux(&(gpioc.pin[10]), 6);
	gpioc.pin[11].vmt->set_mode(&(gpioc.pin[11]), MODE_ALTERNATE);
	gpioc.pin[11].vmt->set_pinmux(&(gpioc.pin[11]), 6);
	gpiob.pin[5].vmt->set_mode(&(gpiob.pin[5]), MODE_ALTERNATE);
	gpiob.pin[5].vmt->set_pinmux(&(gpiob.pin[5]), 6);

	/* ETH_CS pin on PB4, idle high (deselected). */
	gpiob.pin[4].vmt->set(&(gpiob.pin[4]), true);
	gpiob.pin[4].vmt->set_mode(&(gpiob.pin[4]), MODE_OUTPUT);

	/* ETH_IRQ pin on PB6. */
	gpiob.pin[6].vmt->set_mode(&(gpiob.pin[6]), MODE_INPUT);
	gpiob.pin[6].vmt->set_pull(&(gpiob.pin[6]), PULL_UP);

	/* ETH_RST pin on PB7. Pulse the controller reset low before talking to it. */
	gpiob.pin[7].vmt->set_mode(&(gpiob.pin[7]), MODE_OUTPUT);
	gpiob.pin[7].vmt->set(&(gpiob.pin[7]), false);
	vTaskDelay(2);
	gpiob.pin[7].vmt->set(&(gpiob.pin[7]), true);
	vTaskDelay(2);

	RCC->APB1ENR1 |= RCC_APB1ENR1_SPI3EN;
	stm32_spibus_init(&spi3, (void *)SPI3, STM32_SPI_PER_TYPE_SPI);
	spi3.bus.vmt->set_sck_freq(&spi3.bus, 10e6);
	spi3.bus.vmt->set_mode(&spi3.bus, 0, 0);
	stm32_spidev_init(&spi3_ncn, &spi3.bus, &(gpiob.pin[4]));

	ncn26010_init(&ncn, &spi3_ncn.dev);
}
#endif


/**********************************************************************************************************************
 * USART3 proto-dgstream <-> UDP/IPv6 bridge
 *
 * USART3 runs half-duplex (single wire) on PB10, AF7, at 4 Mbaud. Datagrams framed on the wire by
 * proto-dgstream (inter-frame gaps) are received and forwarded verbatim as UDP/IPv6 payloads over the
 * NCN26010 10BASE-T1S segment.
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)
Stm32Uart usart3;
ProtoDgstream dgstream;
Datagram *dgstream_dgram;

/* Running count of datagrams received on USART3 and forwarded as UDP, sampled once a second by
 * port_init() to report the datagram rate. */
volatile uint32_t dgstream_rx_count;

#define BRIDGE_TASK_STACK 1024


/* One's-complement running sum over a byte range, treated as big-endian 16-bit words. Fold and
 * complement with port_inet_csum_fold() once all the parts have been accumulated. */
static uint32_t port_inet_csum_add(uint32_t sum, const uint8_t *data, size_t len) {
	while (len > 1) {
		sum += (uint32_t)((data[0] << 8) | data[1]);
		data += 2;
		len -= 2;
	}
	if (len > 0) {
		sum += (uint32_t)(data[0] << 8);
	}
	return sum;
}


static uint16_t port_inet_csum_fold(uint32_t sum) {
	while (sum >> 16) {
		sum = (sum & 0xffff) + (sum >> 16);
	}
	return (uint16_t)(~sum);
}


/* Hand-craft an Ethernet + IPv6 + UDP datagram carrying "payload" and hand it to the PHY. It is a
 * plain unicast to the host on the T1S segment, so a normal "nc -6 -ul <host> <port>" receives it
 * without having to join a multicast group. */
static void port_udp_send(const uint8_t *payload, size_t payload_len) {
	if (payload_len > 512) {
		payload_len = 512;
	}

	/* Ethernet II header. eth_dst is the host's unicast MAC (nbus-t1s); eth_src is our locally
	 * administered address. No VLAN tag on the direct 10BASE-T1S segment. */
	static const uint8_t eth_dst[6] = {0x96, 0x0e, 0xe0, 0x45, 0xf8, 0x31};
	static const uint8_t eth_src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

	/* Both endpoints sit in the fd00:dead:beee::/96 prefix: the host is ::1, we send from ::3. */
	static const uint8_t ip6_src[16] = {
		0xfd, 0x00, 0xde, 0xad, 0xbe, 0xee, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
	};
	static const uint8_t ip6_dst[16] = {
		0xfd, 0x00, 0xde, 0xad, 0xbe, 0xee, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	};
	const uint16_t udp_src_port = 52001;
	const uint16_t udp_dst_port = 52001;
	const uint16_t udp_len = 8 + payload_len;

	/* Ethernet(18) + IPv6(40) + UDP(8) + payload, zero-padded to the 60-byte minimum frame size. */
	uint8_t frame[600] = {0};
	size_t pos = 0;

	memcpy(frame + pos, eth_dst, 6); pos += 6;
	memcpy(frame + pos, eth_src, 6); pos += 6;
	frame[pos++] = 0x86; frame[pos++] = 0xdd;                    /* EtherType = IPv6 */

	frame[pos++] = 0x60; frame[pos++] = 0x00;                    /* version 6, traffic class 0 */
	frame[pos++] = 0x00; frame[pos++] = 0x00;                    /* flow label 0 */
	frame[pos++] = udp_len >> 8; frame[pos++] = udp_len & 0xff;  /* payload length */
	frame[pos++] = 17;                                           /* next header = UDP */
	frame[pos++] = 255;                                          /* hop limit */
	memcpy(frame + pos, ip6_src, 16); pos += 16;
	memcpy(frame + pos, ip6_dst, 16); pos += 16;

	size_t udp_off = pos;
	frame[pos++] = udp_src_port >> 8; frame[pos++] = udp_src_port & 0xff;  /* source port */
	frame[pos++] = udp_dst_port >> 8; frame[pos++] = udp_dst_port & 0xff;  /* destination port */
	frame[pos++] = udp_len >> 8; frame[pos++] = udp_len & 0xff;    /* UDP length */
	frame[pos++] = 0x00; frame[pos++] = 0x00;                      /* checksum, filled in below */
	memcpy(frame + pos, payload, payload_len); pos += payload_len;

	/* The UDP checksum is mandatory over IPv6. Sum the pseudo-header (src + dst addresses, the
	 * upper-layer length and the next-header value) followed by the UDP header and payload. */
	uint32_t sum = 0;
	sum = port_inet_csum_add(sum, ip6_src, 16);
	sum = port_inet_csum_add(sum, ip6_dst, 16);
	sum += udp_len;
	sum += 17;
	sum = port_inet_csum_add(sum, frame + udp_off, udp_len);
	uint16_t csum = port_inet_csum_fold(sum);
	if (csum == 0) {
		csum = 0xffff;
	}
	frame[udp_off + 6] = csum >> 8;
	frame[udp_off + 7] = csum & 0xff;

	if (pos < 60) {
		pos = 60;
	}
	ncn26010_send(&ncn, frame, pos);
}


/* Receive datagrams framed on the USART3 medium by proto-dgstream and forward each one as a UDP/IPv6
 * payload over the T1S Ethernet segment. */
static void bridge_task(void *p) {
	(void)p;
	while (true) {
		uint8_t buf[512];
		size_t len = sizeof(buf);
		if (dgstream_dgram->vmt->read(dgstream_dgram, buf, &len, NULL) != DATAGRAM_RET_OK) {
			continue;
		}
		port_udp_send(buf, len);
		dgstream_rx_count++;
	}
}


static void bridge_init(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;

	/* USART3 TX on PB10, AF7, open-drain half-duplex. */
	gpiob.pin[10].vmt->set_mode(&(gpiob.pin[10]), MODE_ALTERNATE);
	gpiob.pin[10].vmt->set_otype(&(gpiob.pin[10]), OTYPE_OD);
	gpiob.pin[10].vmt->set_pull(&(gpiob.pin[10]), PULL_UP);
	gpiob.pin[10].vmt->set_pinmux(&(gpiob.pin[10]), 7);

	/* SystemCoreClock is already at its final value here, so the 4 Mbaud divider is computed correctly. */
	stm32_uart_init(&usart3, (void *)USART3);
	stm32_uart_set_swmode(&usart3);
	//stm32_uart_set_rto(&usart3, true);
	usart3.uart.vmt->set_bitrate(&usart3.uart, 1000000);

	NVIC_EnableIRQ(USART3_IRQn);
	NVIC_SetPriority(USART3_IRQn, 7);

	proto_dgstream_init(&dgstream, &usart3.stream);
	proto_dgstream_get_datagram(&dgstream, &dgstream_dgram);
	proto_dgstream_set_rx_timeout(&dgstream, 1);

	xTaskCreate(bridge_task, "dgstream-udp", BRIDGE_TASK_STACK, NULL, 2, NULL);
}


void usart3_isr(void);
void usart3_isr(void) {
	stm32_uart_interrupt_handler(&usart3);
}
#endif


void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	NVIC_SetPriority(SysTick_IRQn, 15);
	SysTick->LOAD = 16000UL - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}


/**********************************************************************************************************************
 * Internal flash memory initialisation
 **********************************************************************************************************************/

Stm32Flash iflash;
FlashVolStatic pv_iflash;
Flash *lv_bl;
Flash *lv_conf;
Flash *lv_mib;
Flash *lv_app;
Flash *lv_update;

/* MIB configuration tree read from the "mib" flash partition. mib_root is the synthesized Conf tree
 * decoded from the CBOR map; it stays NULL if no valid MIB is present. */
FlashCborMib mib;
Conf *mib_root;

static void port_flash_init(void) {
	stm32_flash_init(&iflash);

	flash_vol_static_init(&pv_iflash, &iflash.flash);
	flash_vol_static_create(&pv_iflash, "bootloader", 0,          60 * 1024,  &lv_bl);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_bl, "bootloader");
	flash_vol_static_create(&pv_iflash, "bootconf",   60 * 1024,  2 * 1024,   &lv_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf, "bootconf");
	flash_vol_static_create(&pv_iflash, "mib",        62 * 1024,  2 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");
	flash_vol_static_create(&pv_iflash, "app",        64 * 1024,  128 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");
	flash_vol_static_create(&pv_iflash, "update",     192 * 1024, 64 * 1024,  &lv_update);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_update, "update");

	/* Hand the MIB partition to the flash-cbor-mib service. It detects presence, decodes the CBOR map
	 * and, on success, advertises the resulting "mib" Conf tree used as the configuration tree. */
	const struct flash_cbor_mib_conf mib_conf = {
		.flash = lv_mib,
		.offset = 0,
		.max_size = 0,
		.root_name = "mib",
	};
	if (flash_cbor_mib_init(&mib, &mib_conf) == FLASH_CBOR_MIB_RET_OK) {
		flash_cbor_mib_get_root(&mib, &mib_root);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CONF, (Interface *)mib_root, "mib");
	}
}


/**********************************************************************************************************************
 * LED initialization
 **********************************************************************************************************************/

GpioLed led_stat;
GpioLed led_error;

static void led_init(void) {
	/* Status RGB LED on PC4/PB0/PB1. */
	gpioc.pin[4].vmt->set_mode(&(gpioc.pin[4]), MODE_OUTPUT);
	gpiob.pin[0].vmt->set_mode(&(gpiob.pin[0]), MODE_OUTPUT);
	gpiob.pin[1].vmt->set_mode(&(gpiob.pin[1]), MODE_OUTPUT);

	/* Error RGB LED on PA7/PA6/PA5. */
	gpioa.pin[5].vmt->set_mode(&(gpioa.pin[5]), MODE_OUTPUT);
	gpioa.pin[6].vmt->set_mode(&(gpioa.pin[6]), MODE_OUTPUT);
	gpioa.pin[7].vmt->set_mode(&(gpioa.pin[7]), MODE_OUTPUT);

	gpio_led_init(&led_stat, &(gpioc.pin[4]), &(gpiob.pin[0]), &(gpiob.pin[1]));
	gpio_led_invert(&led_stat, true);
	led_stat.led.vmt->sequence(&led_stat.led, LED_SEQ_HEARTBEAT);

	gpio_led_init(&led_error, &(gpioa.pin[7]), &(gpioa.pin[6]), &(gpioa.pin[5]));
	gpio_led_invert(&led_error, true);
	led_error.led.vmt->set(&led_error.led, LED_COLOR_RGB(0, 255, 255));
}


int32_t port_init(void) {
	//watchdog_init(&watchdog, 8000, 1);

	stm32_gpio_init(&gpioa, (void *)0x48000000);
	stm32_gpio_init(&gpiob, (void *)0x48000400);
	stm32_gpio_init(&gpioc, (void *)0x48000800);

	console_init();
	port_flash_init();
	led_init();

	#if !defined(CONFIG_APP_BL)
		/* Bring the system clock up to full speed before configuring the high-speed peripherals. */
		stm32_clock_init(&cmgr, STM32_CLOCK_LEVEL_MEDIUM_PERF);
		stm32_clock_wait_init_done(&cmgr);
		SystemCoreClock = 128e6;

		ethernet_init();
		bridge_init();

		/* Report the forwarded datagram rate once a second. */
		uint32_t last_count = 0;
		while (true) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			uint32_t count = dgstream_rx_count;
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("datagrams: %lu/s"),
				(unsigned long)(count - last_count));
			last_count = count;
		}
	#endif

	return PORT_INIT_OK;
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM6RST;
	RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM6RST;
	/* The timer should run at 1MHz */
	TIM6->PSC = 16 - 1;
	TIM6->CR1 &= ~TIM_CR1_OPM;
	TIM6->ARR = UINT16_MAX;
	TIM6->CR1 |= TIM_CR1_CEN;
}


uint32_t port_task_timer_get_value(void) {
	return TIM6->CNT;
}
