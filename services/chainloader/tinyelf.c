/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Library for parsing and reading ELF files.
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "tinyelf.h"


const char *tinyelf_class_str[3] = {"", "ELF32", "ELF64"};
const char *tinyelf_data_str[3] = {"", "little", "big"};
const char *tinyelf_phdr_type_str[8] = {"NULL", "LOAD", "DYNAMIC", "INTERP", "NOTE", "SHLIB", "PHDR", "TLS"};


static uint8_t load_uint8(Elf *self) {
	if (self->read == NULL) {
		return 0;
	}
	uint8_t r = 0;
	size_t rbytes = 0;
	if (self->read(self, self->cursor, &r, sizeof(r), &rbytes) == TINYELF_RET_OK && rbytes == sizeof(r)) {
		self->cursor++;
		return r;
	}
	return 0;
}


static uint16_t load_uint16(Elf *self) {
	uint16_t r = 0;
	if (self->elf_header.data == TINYELF_DATA_BIG) { 
		r |= load_uint8(self) << 8;
		r |= load_uint8(self);
	} else {
		r |= load_uint8(self);
		r |= load_uint8(self) << 8;
	}
	return r;
}


static uint32_t load_uint32(Elf *self) {
	uint32_t r = 0;
	if (self->elf_header.data == TINYELF_DATA_BIG) {
		r |= load_uint8(self) << 24;
		r |= load_uint8(self) << 16;
		r |= load_uint8(self) << 8;
		r |= load_uint8(self);
	} else {
		r |= load_uint8(self);
		r |= load_uint8(self) << 8;
		r |= load_uint8(self) << 16;
		r |= load_uint8(self) << 24;
	}
	return r;
}


static uint32_t load_ptr(Elf *self) {
	/** @todo ELF64 class parsing */
	if (self->elf_header.class == TINYELF_ELF32) {
		uint32_t r = 0;
		if (self->elf_header.data == TINYELF_DATA_BIG) {
			r |= load_uint8(self) << 24;
			r |= load_uint8(self) << 16;
			r |= load_uint8(self) << 8;
			r |= load_uint8(self);
			return r;
		} else {
			r |= load_uint8(self);
			r |= load_uint8(self) << 8;
			r |= load_uint8(self) << 16;
			r |= load_uint8(self) << 24;
			return r;
		}
	}
	return 0;
}


tinyelf_ret_t tinyelf_init(Elf *self, tinyelf_ret_t (*read)(Elf *self, size_t pos, void *buf, size_t len, size_t *read), void *read_ctx) {
	memset(self, 0, sizeof(Elf));
	self->read = read;
	self->read_ctx = read_ctx;

	return TINYELF_RET_OK;
}


tinyelf_ret_t tinyelf_parse(Elf *self) {
	/* Check the ELF header, return error if the buffer is not an ELF. Set format to BIG endian
	   to parse the magic correctly. */
	self->elf_header.data = TINYELF_DATA_BIG;
	self->cursor = 0;
	if (load_uint32(self) != TINYELF_ELF_MAGIC) {
		return TINYELF_RET_NOELF;
	}
	self->elf_header.class = load_uint8(self);
	self->elf_header.data = load_uint8(self);
	self->elf_header.version = load_uint8(self);
	self->elf_header.osabi = load_uint8(self);
	self->elf_header.abiversion = load_uint8(self);
	/* Padding */
	self->cursor += 7;
	self->elf_header.type = load_uint16(self);
	self->elf_header.machine = load_uint16(self);
	self->elf_header.version2 = load_uint32(self);
	self->elf_header.entry = load_ptr(self);
	self->elf_header.phoff = load_ptr(self);
	self->elf_header.shoff = load_ptr(self);
	self->elf_header.flags = load_uint32(self);
	self->elf_header.ehsize = load_uint16(self);
	self->elf_header.phentsize = load_uint16(self);
	self->elf_header.phnum = load_uint16(self);
	self->elf_header.shentsize = load_uint16(self);
	self->elf_header.shnum = load_uint16(self);
	self->elf_header.shstrndx = load_uint16(self);

	return TINYELF_RET_OK;
}


tinyelf_ret_t tinyelf_phdr_get(Elf *self, struct tinyelf_program_header *phdr, uint32_t idx) {
	if (idx >= self->elf_header.phnum) {
		return TINYELF_RET_FAILED;
	}
	self->cursor = self->elf_header.phoff + self->elf_header.phentsize * idx;

	phdr->type = load_uint32(self);
	if (self->elf_header.class == TINYELF_ELF64) {
		phdr->flags = load_uint32(self);		
	}
	phdr->offset = load_ptr(self);
	phdr->vaddr = load_ptr(self);
	phdr->paddr = load_ptr(self);
	phdr->filesz = load_ptr(self);
	phdr->memsz = load_ptr(self);
	if (self->elf_header.class == TINYELF_ELF32) {
		phdr->flags = load_uint32(self);		
	}
	phdr->align = load_ptr(self);
	
	return TINYELF_RET_OK;
}


tinyelf_ret_t tinyelf_shdr_get(Elf *self, struct tinyelf_section_header *shdr, uint32_t idx) {
	if (idx >= self->elf_header.shnum) {
		return TINYELF_RET_FAILED;
	}
	self->cursor = self->elf_header.shoff + self->elf_header.shentsize * idx;

	shdr->name = load_ptr(self);
	shdr->type = load_uint32(self);
	shdr->flags = load_uint32(self); 
	shdr->addr = load_ptr(self);
	shdr->offset = load_ptr(self);
	shdr->size = load_ptr(self);
	shdr->link = load_uint32(self); 
	shdr->info = load_uint32(self); 
	shdr->align = load_uint32(self); 
	shdr->entsize = load_uint32(self); 
	
	return TINYELF_RET_OK;
}


tinyelf_ret_t tinyelf_section_get_name(Elf *self, uint32_t offset, char *buf, size_t size) {
	/* Get the string section header first. */
	struct tinyelf_section_header str_hdr;
	if (tinyelf_shdr_get(self, &str_hdr, self->elf_header.shstrndx) != TINYELF_RET_OK) {
		return TINYELF_RET_FAILED;
	}
	
	self->cursor = str_hdr.offset + offset;
	char c = 0;
	while ((c = (char)load_uint8(self)) && (size > 1)) {
		*buf = c;
		buf++;
		size--;
	}
	*buf = '\0';

	return TINYELF_RET_OK;
}


tinyelf_ret_t tinyelf_section_find_by_name(Elf *self, const char *name, struct tinyelf_section_header *shdr) {
	for (uint32_t i = 0; tinyelf_shdr_get(self, shdr, i) == TINYELF_RET_OK; i++) {
		char fname[TINYELF_MAX_SECTION_NAME_LEN] = {0};
		tinyelf_section_get_name(self, shdr->name, fname, sizeof(fname));
		if (!strcmp(name, fname)) {
			return TINYELF_RET_OK;
		}
	}
	return TINYELF_RET_FAILED;
}
