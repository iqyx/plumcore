/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Service for chainloading another firmware
 *
 * Copyright (c) 2023-2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/cortex.h>
#include <cbor.h>
#include <blake2.h>
#include <ed25519.h>

#include "chainloader.h"
#include "tinyelf.h"

#define MODULE_NAME "chainloader"


/* Chainloader requires the ELF to reside in the internal flash memory. It must be created
 * in a way to be able to run in place (XIP). That means the structure of the ELF as written
 * in the flash must correspond to the structure during linking (when PIC is not used).
 * The only callback implemented can only read from a memory buffer. */
static tinyelf_ret_t tinyelf_read(Elf *elf, size_t pos, void *buf, size_t len, size_t *read) {
	ChainLoader *self = elf->read_ctx;

	/** @todo range check */
	memcpy(buf, self->membuf + pos, len);
	*read = len;

	return TINYELF_RET_OK;
}


static chainloader_ret_t print_elf(Elf *elf) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  type %s, %s endian, version %d"),
		tinyelf_class_str[elf->elf_header.class],
		tinyelf_data_str[elf->elf_header.data],
		elf->elf_header.version
	);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  entry 0x%08x, phdr offset %p, shdr offset %p"),
		elf->elf_header.entry,
		elf->elf_header.phoff,
		elf->elf_header.shoff
	);
	return CHAINLOADER_RET_OK;
}


static chainloader_ret_t print_phdr(Elf *elf) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("program headers:"));
	struct tinyelf_program_header hdr = {0};
	for (uint32_t i = 0; tinyelf_phdr_get(elf, &hdr, i) == TINYELF_RET_OK; i++) {
		const char flags[4] = {
			hdr.flags & 4 ? 'R':'-',
			hdr.flags & 2 ? 'W':'-',
			hdr.flags & 1 ? 'E':'-',
			'\0'
		};
		const char *type = "?";
		if (hdr.type <= 7) {
			type = tinyelf_phdr_type_str[hdr.type];
		}
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  type %s, flags %s, offset %p, paddr 0x%08x, size %p"),
			type,
			flags,
			hdr.offset,
			hdr.paddr,
			hdr.filesz
		);
	}
	return CHAINLOADER_RET_OK;
}


static chainloader_ret_t print_shdr(Elf *elf) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("section headers:"));
	struct tinyelf_section_header hdr = {0};
	for (uint32_t i = 0; tinyelf_shdr_get(elf, &hdr, i) == TINYELF_RET_OK; i++) {
		char name[32] = {0};
		tinyelf_section_get_name(elf, hdr.name, name, sizeof(name));
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  %s, type %d, addr 0x%08x, offset %p, size %p"),
			name,
			hdr.type,
			hdr.addr,
			hdr.offset,
			hdr.size
		);
	}
	return CHAINLOADER_RET_OK;
}


/**
 * @brief Find the application vector table position
 *
 * Iterate over all ELF headers and find the one with the vector table. Vector table always
 * starts with a stack pointer load address followed by a reset vector address. The reset
 * vector address must be the same as the entry point in the ELF header.
 *
 * @todo This functionality is specific to chainloading and even then it is used only when
 * a static vector table is found. If the main firmware creates the vector table dynamically,
 * no vector table is found in the .text section and this function fails.
 */
static chainloader_ret_t chainloader_find_vector_table(ChainLoader *self, uint32_t *addr) {
	struct tinyelf_program_header hdr = {0};
	chainloader_ret_t ret = CHAINLOADER_RET_FAILED;
	for (uint32_t i = 0; tinyelf_phdr_get(&self->elf, &hdr, i) == TINYELF_RET_OK; i++) {
		uint32_t reset_vector = *(uint32_t *)(hdr.paddr + 4);
		if (self->elf.elf_header.entry == reset_vector && addr != NULL) {
			*addr = hdr.paddr;
			ret = CHAINLOADER_RET_OK;
		}
	}
	return ret;
}


/**
 * ELF signature is always in a single section. For Ed25519 signatures it is a raw 64 byte string.
 * @todo This functionality should be moved to the tinyelf library asi it is not specific for
 *       ELF chainloading. ELF signatures can be checked even in ELFs residing in files.
 */
chainloader_ret_t chainloader_find_signature(ChainLoader *self, uint8_t **addr, size_t *size) {
	struct tinyelf_section_header hdr = {0};
	if (tinyelf_section_find_by_name(&self->elf, ".sign.ed25519", &hdr) != TINYELF_RET_OK) {
		return CHAINLOADER_RET_FAILED;
	}
	*addr = self->membuf + hdr.offset;
	*size = hdr.size;
	#if defined(CONFIG_CHAINLOADER_INFO_LOGGING)
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Ed25519 signature found at 0x%08x, size %p"), *addr, *size);
	#endif

	return CHAINLOADER_RET_OK;
}


/**
 * @todo  - move to the tinyelf library
 *        - exclusion hardcoded to 64 bytes
 *        - probably replace the exclusion with a single section instead of the more generic addr & len
 */
static chainloader_ret_t chainloader_elf_b2s(ChainLoader *self, uint8_t *exclude, size_t exclude_size, uint8_t h[32]) {
	size_t elf_size = self->elf.elf_header.shoff + self->elf.elf_header.shentsize * self->elf.elf_header.shnum;

	/* Part before the exclude region. */
	blake2s_state s;
	blake2s_init(&s, 32);
	blake2s_update(&s, self->membuf, exclude - self->membuf);

	/* Exclusion is included as a zero buffer. */
	uint8_t hm[32] = {0};
	blake2s_update(&s, hm, sizeof(hm));
	blake2s_update(&s, hm, sizeof(hm));

	/* Part immediately following the excluded range to the end of the ELF. */
	blake2s_update(&s, exclude + exclude_size, elf_size - (exclude - self->membuf) - exclude_size);

	blake2s_final(&s, h, 32);

	return CHAINLOADER_RET_OK;
}


/**
 * @todo  - move to the tinyelf library
 *        - name it using ed25519
 */
chainloader_ret_t chainloader_check_signature(ChainLoader *self, const uint8_t pubkey[32]) {
	/* Content of the .sign.ed25519 ELF section. */
	uint8_t *addr = NULL;
	size_t size = 0;
	/* Blake2s hash of the ELF file must be computed with the signature section
	 * excluded and replaced with zeros. */
	uint8_t h[32] = {0};

	if (chainloader_find_signature(self, &addr, &size) == CHAINLOADER_RET_OK &&
	    chainloader_elf_b2s(self, addr, size, h) == CHAINLOADER_RET_OK &&
	    ed25519_verify(addr, pubkey, h, 32) == ED25519_VERIFY_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Signature verified OK"));
		return CHAINLOADER_RET_OK;
	}

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("Signature verification failed"));
	return CHAINLOADER_RET_FAILED;
}


chainloader_ret_t chainloader_find_elf(ChainLoader *self, uint8_t *mstart, size_t msize, size_t mstep) {
	for (size_t offset = 0; offset < msize; offset += mstep) {
		if (chainloader_set_elf(self, mstart + offset, msize - offset) == CHAINLOADER_RET_OK) {
			return CHAINLOADER_RET_OK;
		}
	}
	/* No ELF found during the scan, membuf is clear now. */
	return CHAINLOADER_RET_FAILED;
}


chainloader_ret_t chainloader_init(ChainLoader *self) {
	memset(self, 0, sizeof(ChainLoader));

	return CHAINLOADER_RET_OK;
}


chainloader_ret_t chainloader_set_elf(ChainLoader *self, uint8_t *buf, size_t size) {
	self->membuf = buf;
	self->memsize = size;

	/* Now the callback can be used to init and load an ELF. */
	tinyelf_init(&self->elf, tinyelf_read, self);
	if (tinyelf_parse(&self->elf) == TINYELF_RET_OK) {
		#if defined(CONFIG_CHAINLOADER_INFO_LOGGING)
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ELF image found at 0x%08x"), self->membuf);
		#endif
		#if defined(CONFIG_CHAINLOADER_DEBUG_LOGGING)
			print_elf(&self->elf);
			print_phdr(&self->elf);	
			print_shdr(&self->elf);
		#endif
		return CHAINLOADER_RET_OK;
	}

	/* Not properly parsed, set membuf back. */
	self->membuf = NULL;
	self->memsize = 0;
	return CHAINLOADER_RET_FAILED;
}


chainloader_ret_t chainloader_free(ChainLoader *self) {
	(void)self;

	return CHAINLOADER_RET_OK;
}


chainloader_ret_t chainloader_boot(ChainLoader *self) {
	/* Cannot boot when the ELF address is not found. */
	if (self->membuf == NULL) {
		return CHAINLOADER_RET_FAILED;
	}
	if (chainloader_find_vector_table(self, &self->vector_table) != CHAINLOADER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no vector table found"));
		return CHAINLOADER_RET_FAILED;
	}

	#if defined(CONFIG_CHAINLOADER_INFO_LOGGING)
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("jumping to 0x%08x, MSP 0x%08x, vector table 0x%08x --------->\n\n"), self->elf.elf_header.entry, *(uint32_t *)self->vector_table, self->vector_table);
	#endif

	/* This delay is really needed, otherwise FreeRTOS freezes here. */
	vTaskDelay(100);

	/* Suspend all tasks, disable all interrupts and clear all pending interrupts. */
	vTaskSuspendAll();
	for (uint32_t i = 0; i < 8; i++) {
		NVIC_ICER(i) = 0xffffffffU;
		NVIC_ICPR(i) = 0xffffffffU;
	}

	/* Switch back to MSP. */	
	__asm volatile("mov r0, #0");
	__asm volatile("msr control, r0");
	__asm volatile("isb");

	register uint32_t msp __asm("msp");
	typedef void (*app_entry_t)(void);
	app_entry_t app_entry = (app_entry_t)self->elf.elf_header.entry;

	/* Set stack pointer, relocate the vector table to the new position and run the application firmware. */
	msp = *(uint32_t *)self->vector_table;
	SCB_VTOR = self->vector_table;
	app_entry();

	(void)msp;
	return CHAINLOADER_RET_OK;
}
