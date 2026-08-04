/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 SPI driver service
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/gpio.h>

#if defined(STM32G4)
	#include <stm32g4xx.h>
#elif defined(STM32H7)
	#include <stm32h7xx.h>
#elif defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-spi service is not compatible with this MCU family"
#endif

#include "stm32-spi.h"

#define MODULE_NAME "stm32-spi"


/***************************************************************************************************
 * SpiBus interface implementation
 ***************************************************************************************************/


static spi_ret_t stm32_spibus_send(SpiBus *spibus, const uint8_t *txbuf, size_t txlen) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	SPI_TypeDef *base = (SPI_TypeDef *)self->base;

	for (size_t i = 0; i < txlen; i++) {
		#if defined(STM32G4)
			while (!(base->SR & SPI_SR_TXE)) {
				continue;
			}
			volatile uint8_t *dr = (volatile uint8_t *)&base->DR;
			*dr = txbuf[i];
			while (!(base->SR & SPI_SR_RXNE)) {
				continue;
			}
			(void)*dr;
		#elif defined(STM32H7) || defined(STM32U5)
			while (!(base->SR & SPI_SR_TXC)) {
				continue;
			}
			volatile uint8_t *txdr = (volatile uint8_t *)&base->TXDR;
			*txdr = txbuf[i];
			base->CR1 |= SPI_CR1_CSTART;
			while (!(base->SR & SPI_SR_RXP)) {
				continue;
			}
			volatile uint8_t *rxdr = (volatile uint8_t *)&base->RXDR;
			uint8_t read = *rxdr;
			(void)read;
		#endif
	}

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_receive(SpiBus *spibus, uint8_t *rxbuf, size_t rxlen) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	SPI_TypeDef *base = (SPI_TypeDef *)self->base;

	for (size_t i = 0; i < rxlen; i++) {
		#if defined(STM32G4)
			while (!(base->SR & SPI_SR_TXE)) {
				continue;
			}
			volatile uint8_t *dr = (volatile uint8_t *)&base->DR;
			*dr = 0x00;
			while (!(base->SR & SPI_SR_RXNE)) {
				continue;
			}
			rxbuf[i] = *dr;
		#elif defined(STM32H7) || defined(STM32U5)
			while (!(base->SR & SPI_SR_TXC)) {
				continue;
			}
			volatile uint8_t *txdr = (volatile uint8_t *)&base->TXDR;
			*txdr = 0x00;
			base->CR1 |= SPI_CR1_CSTART;
			while (!(base->SR & SPI_SR_RXP)) {
				continue;
			}
			volatile uint8_t *rxdr = (volatile uint8_t *)&base->RXDR;
			rxbuf[i] = *rxdr;
		#endif
	}

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_exchange(SpiBus *spibus, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;

	if (self->per_type == STM32_SPI_PER_TYPE_UART) {
		#if defined(STM32H7) || defined(STM32U5)
			USART_TypeDef *base = (USART_TypeDef *)self->base;
			for (size_t i = 0; i < len; i++) {
				while (!(base->ISR & USART_ISR_TXE_TXFNF)) {
					continue;
				}
				volatile uint8_t *txdr = (volatile uint8_t *)&base->TDR;
				*txdr = txbuf[i];

				while (!(base->ISR & USART_ISR_RXNE_RXFNE)) {
					continue;
				}
				volatile uint8_t *rxdr = (volatile uint8_t *)&base->RDR;
				rxbuf[i] = *rxdr;
			}
		#else
			return SPI_RET_FAILED;
		#endif

	} else {
		SPI_TypeDef *base = (SPI_TypeDef *)self->base;
		#if defined(STM32G4)
			for (size_t i = 0; i < len; i++) {
				while (!(base->SR & SPI_SR_TXE)) {
					continue;
				}
				volatile uint8_t *dr = (volatile uint8_t *)&base->DR;
				*dr = txbuf[i];
				while (!(base->SR & SPI_SR_RXNE)) {
					continue;
				}
				rxbuf[i] = *dr;
			}

		#elif defined(STM32H7)

			base->CR1 &= ~SPI_CR1_SPE;
			base->IFCR = 0xfffffffful;
			base->CR2 = len;
			base->CR1 |= SPI_CR1_SPE;
			base->IER |= SPI_IER_EOTIE;
			base->CR1 |= SPI_CR1_CSTART;

			for (size_t i = 0; i < len; i++) {
				volatile uint8_t *txdr = (volatile uint8_t *)&base->TXDR;
				*txdr = txbuf[i];
			}

			xSemaphoreTake(self->eot_wait, portMAX_DELAY);
			base->IER &= ~SPI_IER_EOTIE;

			for (size_t i = 0; i < len; i++) {
				volatile uint8_t *rxdr = (volatile uint8_t *)&base->RXDR;
				rxbuf[i] = *rxdr;
			}

			base->CR1 &= ~SPI_CR1_SPE;
		#elif defined(STM32U5)

			/* Polled full-duplex, byte by byte, matching send()/receive(). The TSIZE/interrupt-driven
			 * continuous transfer (used on H7 above) loses the first received bit on U5 because of its
			 * SPE off/on and continuous-clock kickoff, shifting the whole frame; the per-byte CSTART
			 * path keeps SPE enabled and clocks correctly, and needs no end-of-transfer interrupt. */
			for (size_t i = 0; i < len; i++) {
				while (!(base->SR & SPI_SR_TXC)) {
					continue;
				}
				volatile uint8_t *txdr = (volatile uint8_t *)&base->TXDR;
				*txdr = txbuf[i];
				base->CR1 |= SPI_CR1_CSTART;
				while (!(base->SR & SPI_SR_RXP)) {
					continue;
				}
				volatile uint8_t *rxdr = (volatile uint8_t *)&base->RXDR;
				rxbuf[i] = *rxdr;
			}
		#endif
	}

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_lock(SpiBus *spibus) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	if (xSemaphoreTake(self->bus_lock, portMAX_DELAY) != pdTRUE) {
		return SPI_RET_NOT_AVAILABLE;
	}
	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_unlock(SpiBus *spibus) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	xSemaphoreGive(self->bus_lock);
	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_set_sck_freq(SpiBus *spibus, uint32_t freq_hz) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	SPI_TypeDef *base = (SPI_TypeDef *)self->base;

	/** @todo get the correct APB frequency properly */
	uint32_t spi_freq = SystemCoreClock;

	base->CR1 &= ~SPI_CR1_SPE;
	uint32_t prescaler = 0;
	uint8_t i = 0;
	for (i = 0; i < 8; i++) {
		prescaler = 2 << i;
		if (spi_freq / prescaler <= freq_hz) {
			/* Already reached the target frequency, or lower. */
			break;
		}
	}
	/* Clamp to the maximum prescaler (/256) if the requested frequency is below the minimum achievable. */
	if (i > 7) {
		i = 7;
		prescaler = 256;
	}

	#if defined(STM32G4)
		base->CR1 = (base->CR1 & ~SPI_CR1_BR_Msk) | (i << SPI_CR1_BR_Pos);
	#elif defined(STM32H7) || defined(STM32U5)
		base->CFG1 = (base->CFG1 & ~SPI_CFG1_MBR_Msk) | (i << SPI_CFG1_MBR_Pos);
	#endif

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SCK freq requested = %lu kHz, prescaler = %u, real = %lu kHz"), freq_hz / 1000, prescaler, spi_freq / prescaler / 1000);
	base->CR1 |= SPI_CR1_SPE;

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_set_mode(SpiBus *spibus, uint8_t cpol, uint8_t cpha) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	SPI_TypeDef *base = (SPI_TypeDef *)self->base;

	base->CR1 &= ~SPI_CR1_SPE;
	#if defined(STM32G4)
		base->CR1 = (base->CR1 & ~(SPI_CR1_CPOL | SPI_CR1_CPHA))
		           | (cpol ? SPI_CR1_CPOL : 0)
		           | (cpha ? SPI_CR1_CPHA : 0);
	#elif defined(STM32H7) || defined(STM32U5)
		base->CFG2 = (base->CFG2 & ~(SPI_CFG2_CPOL | SPI_CFG2_CPHA))
		            | (cpol ? SPI_CFG2_CPOL : 0)
		            | (cpha ? SPI_CFG2_CPHA : 0);
	#endif
	base->CR1 |= SPI_CR1_SPE;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("mode set to CPOL=%u CPHA=%u"), cpol, cpha);
	return SPI_RET_OK;
}


static const struct spibus_vmt stm32_spibus_vmt = {
	.send = stm32_spibus_send,
	.receive = stm32_spibus_receive,
	.exchange = stm32_spibus_exchange,
	.lock = stm32_spibus_lock,
	.unlock = stm32_spibus_unlock,
	.set_sck_freq = stm32_spibus_set_sck_freq,
	.set_mode = stm32_spibus_set_mode,
};


static stm32_spi_ret_t stm32_spibus_port_init(Stm32SpiBus *self) {
	if (self->per_type == STM32_SPI_PER_TYPE_UART) {
		USART_TypeDef *base = (USART_TypeDef *)self->base;

		/* Initialize UART/USART peripheral in a synchronous mode, acting as a SPI master. */
		base->CR1 &= ~USART_CR1_UE;
		#if defined(STM32H7) || defined(STM32U5)
			/* set baudrate here */
			base->CR1 = USART_CR1_RE | USART_CR1_TE;
			base->CR2 = USART_CR2_CLKEN | USART_CR2_MSBFIRST | USART_CR2_LBCL;
			base->CR3 = USART_CR3_OVRDIS;
			base->BRR = 32 - 1;
		#else
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("driver does not support SPI master mode using UART peripheral"));
			return STM32_SPI_RET_FAILED;
		#endif
		base->CR1 |= USART_CR1_UE;

	} else {
		SPI_TypeDef *base = (SPI_TypeDef *)self->base;

		/* Use the standard SPI peripheral. */
		base->CR1 &= ~SPI_CR1_SPE;
		#if defined(STM32G4)
			/* Master, SSM+SSI (software CS), MSB first, full-duplex, CPOL=0/CPHA=0, /32 default prescaler, 8-bit data. */
			base->CR1 = 4 << SPI_CR1_BR_Pos;
			base->CR1 |= SPI_CR1_SSI;
			base->CR1 |= SPI_CR1_SSM;
			base->CR1 |= SPI_CR1_MSTR;
			base->CR2 = SPI_CR2_FRXTH | ((8 - 1) << SPI_CR2_DS_Pos);
			base->CR1 |= SPI_CR1_SPE;
		#elif defined(STM32H7) || defined(STM32U5)
			base->CFG1 = (4 << SPI_CFG1_MBR_Pos) | ((8 - 1) << SPI_CFG1_DSIZE_Pos) | ((8 - 1) << SPI_CFG1_CRCSIZE_Pos);
			base->CR2 = 0;
			base->CR1 = SPI_CR1_SSI;
			base->CFG2 = SPI_CFG2_SSM;
			base->CFG2 |= SPI_CFG2_MASTER;
		#endif
	}

	return STM32_SPI_RET_OK;
}

stm32_spi_ret_t stm32_spibus_init(Stm32SpiBus *self, void *base, enum stm32_spi_per_type per_type) {
	memset(self, 0, sizeof(Stm32SpiBus));
	self->base = base;
	self->per_type = per_type;

	if (stm32_spibus_port_init(self) != STM32_SPI_RET_OK) {
		goto err;
	}

	self->bus_lock = xSemaphoreCreateMutex();
	if (self->bus_lock == NULL) {
		goto err;
	}

	self->eot_wait = xSemaphoreCreateBinary();
	if (self->eot_wait == NULL) {
		goto err;
	}


	self->bus.parent = self;
	self->bus.vmt = &stm32_spibus_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SPI bus %p initialized"), self->base);
	return STM32_SPI_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("SPI bus %p init failed"), self->base);
	return STM32_SPI_RET_FAILED;
}


stm32_spi_ret_t stm32_spibus_free(Stm32SpiBus *self) {
	vSemaphoreDelete(self->bus_lock);
	return STM32_SPI_RET_OK;
}


stm32_spi_ret_t stm32_spibus_irq_handler(Stm32SpiBus *self) {
	#if defined(STM32H7) || defined(STM32U5)
		SPI_TypeDef *base = (SPI_TypeDef *)self->base;

		if (base->SR & SPI_SR_TXC) {
			base->IFCR |= SPI_IFCR_EOTC;

			if (self->eot_wait != NULL) {
				BaseType_t woken = pdFALSE;
				xSemaphoreGiveFromISR(self->eot_wait, &woken);
				portYIELD_FROM_ISR(woken);
			}
		}
	#elif defined(STM32G4)
		(void)self;
	#endif

	return STM32_SPI_RET_OK;
}


/***************************************************************************************************
 * SpiBus interface implementation
 ***************************************************************************************************/

static spi_ret_t stm32_spidev_send(SpiDev *spidev, const uint8_t *txbuf, size_t txlen) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;
	if (!self->selected) {
		return SPI_RET_FAILED;
	}
	return self->bus->vmt->send(self->bus, txbuf, txlen);
}


static spi_ret_t stm32_spidev_receive(SpiDev *spidev, uint8_t *rxbuf, size_t rxlen) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;
	if (!self->selected) {
		return SPI_RET_FAILED;
	}

	return self->bus->vmt->receive(self->bus, rxbuf, rxlen);
}


static spi_ret_t stm32_spidev_exchange(SpiDev *spidev, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;
	if (!self->selected) {
		return SPI_RET_FAILED;
	}

	return self->bus->vmt->exchange(self->bus, txbuf, rxbuf, len);
}


static spi_ret_t stm32_spidev_select(SpiDev *spidev) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;

	if (self->bus->vmt->lock(self->bus) != SPI_RET_OK) {
		return SPI_RET_FAILED;
	}
	self->cs->vmt->set(self->cs, self->cs_inverted);
	self->selected = true;

	return SPI_RET_OK;
}


static spi_ret_t stm32_spidev_deselect(SpiDev *spidev) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;

	if (!self->selected) {
		return SPI_RET_FAILED;
	}
	self->cs->vmt->set(self->cs, !self->cs_inverted);
	self->selected = false;
	self->bus->vmt->unlock(self->bus);

	return SPI_RET_OK;
}


static const struct spidev_vmt stm32_spidev_vmt = {
	.send = stm32_spidev_send,
	.receive = stm32_spidev_receive,
	.exchange = stm32_spidev_exchange,
	.select = stm32_spidev_select,
	.deselect = stm32_spidev_deselect,
};



stm32_spi_ret_t stm32_spidev_init(Stm32SpiDev *self, SpiBus *bus, Gpio *cs) {
	memset(self, 0, sizeof(Stm32SpiDev));
	self->bus = bus;
	self->cs = cs;
	self->cs_inverted = false;
	self->cs->vmt->set_mode(self->cs, MODE_OUTPUT);
	self->cs->vmt->set(self->cs, !self->cs_inverted);

	self->dev.parent = self;
	self->dev.vmt = &stm32_spidev_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SPI device initialized"));

	return STM32_SPI_RET_OK;
}


stm32_spi_ret_t stm32_spidev_free(Stm32SpiDev *self) {
	(void)self;
	return STM32_SPI_RET_OK;
}


stm32_spi_ret_t stm32_spidev_set_cs_inverted(Stm32SpiDev *self, bool inverted) {
	self->cs_inverted = inverted;
	/* Re-assert the idle (deselected) level with the new polarity. */
	if (!self->selected) {
		self->cs->vmt->set(self->cs, !self->cs_inverted);
	}
	return STM32_SPI_RET_OK;
}

