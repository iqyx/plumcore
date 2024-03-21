/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Library for parsing and reading ELF files.
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


#define TINYELF_ELF_MAGIC 0x7f454c46
#define TINYELF_MAX_SECTION_NAME_LEN 32

typedef enum {
	TINYELF_RET_OK = 0,
	TINYELF_RET_FAILED,
	TINYELF_RET_NOELF,
} tinyelf_ret_t;

/* If the header members are enumerable, they are defined by enums.
 * If applicable, a corresponding const array is created to access
 * the names uring runtime. */
enum tinyelf_class {
	TINYELF_ELF32 = 1,
	TINYELF_ELF64 = 2,
};
extern const char *tinyelf_class_str[3];

enum tinyelf_data {
	TINYELF_DATA_LITTLE = 1,
	TINYELF_DATA_BIG = 2,
};
extern const char *tinyelf_data_str[3];

enum tinyelf_type {
	TINYELF_ET_NONE = 0x00,
	TINYELF_ET_REL = 0x01,
	TINYELF_ET_EXEC = 0x02,
	/** @todo */
};

enum tinyelf_machine {
	TINYELF_EM_NONE = 0x00,
	/** @todo */
};

enum tinyelf_version {
	TINYELF_ELF_VERSION_1 = 0x01,
};

struct tinyelf_elf_header {
	enum tinyelf_class class;
	enum tinyelf_data data;
	enum tinyelf_version version;
	uint8_t osabi;
	uint8_t abiversion;
	enum tinyelf_type type;
	uint16_t machine;

	enum tinyelf_version version2;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
	
};

enum tinyelf_phdr_type {
	TINYELF_PHDR_TYPE_NULL = 0,
	TINYELF_PHDR_TYPE_LOAD = 1,
	TINYELF_PHDR_TYPE_DYNAMIC = 2,
	TINYELF_PHDR_TYPE_INTERP = 3,
	TINYELF_PHDR_TYPE_NOTE = 4,
	TINYELF_PHDR_TYPE_SHLIB = 5,	
	TINYELF_PHDR_TYPE_PHDR = 6,
	TINYELF_PHDR_TYPE_TLS = 7,
};
extern const char *tinyelf_phdr_type_str[8];

struct tinyelf_program_header {
	uint32_t type;
	uint32_t flags;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t align;
};

enum tinyelf_shdr_type {
	TINYELF_SHDR_TYPE_NULL = 0x0,
	TINYELF_SHDR_TYPE_PROGBITS = 0x1,
	TINYELF_SHDR_TYPE_SYMTAB = 0x2,
	TINYELF_SHDR_TYPE_STRTAB = 0x3,
	TINYELF_SHDR_TYPE_RELA = 0x4,
	TINYELF_SHDR_TYPE_HASH = 0x5,
	TINYELF_SHDR_TYPE_DYNAMIC = 0x6,
	TINYELF_SHDR_TYPE_NOTE = 0x7,
	TINYELF_SHDR_TYPE_NOBITS = 0x8,
	TINYELF_SHDR_TYPE_REL = 0x9,
	TINYELF_SHDR_TYPE_SHLIB = 0xa,
	TINYELF_SHDR_TYPE_DYNSYM = 0xb,
	TINYELF_SHDR_TYPE_INIT_ARRAY = 0xe,
	TINYELF_SHDR_TYPE_FINI_ARRAY = 0xf,
	TINYELF_SHDR_TYPE_PREINIT_ARRAY = 0x10,
	TINYELF_SHDR_TYPE_GROUP = 0x11,
	TINYELF_SHDR_TYPE_SYMTAB_SHNDX = 0x12,
	TINYELF_SHDR_TYPE_NUM = 0x13,
	TINYELF_SHDR_TYPE_LOOS = 0,
};

struct tinyelf_section_header {
	uint32_t name;
	enum tinyelf_shdr_type type;
	uint32_t flags;
	uint32_t addr;
	uint32_t offset;
	uint32_t size;
	uint32_t link;
	uint32_t info;
	uint32_t align;
	uint32_t entsize;
};


typedef struct tinyelf_elf Elf;
typedef struct tinyelf_elf {
	/**
	 * @brief Read callback for ELF data access.
	 *
	 * @param self Tinyelf instance. It can be used to get the calling context (@p self->read_ctx)
	 * @param pos Read data from the @p pos position
	 * @param buf Buffer to read data to
	 * @param len Length of the data to read
	 * @param read Length of the data actually read (eg. during a file read, EOF may be encountered)
	 */
	tinyelf_ret_t (*read)(Elf *self, size_t pos, void *buf, size_t len, size_t *read);
	void *read_ctx;

	struct tinyelf_elf_header elf_header;

	/** @todo consider moving the cursor to the flash access layer, that is getting rid of it. */
	size_t cursor;

} Elf;


/**
 * @brief Initialise a tinyelf library instance
 *
 * @param @read Callback used to read the underlying data storage (eg. flash)
 */
tinyelf_ret_t tinyelf_init(Elf *self, tinyelf_ret_t (*read)(Elf *self, size_t pos, void *buf, size_t len, size_t *read), void *read_ctx);

/*-
 * @brief Parse the ELF image header
 *
 * Try to parse the ELF. Check the ELF magic first. If it matches, ELF header is
 * read. The cursor remains at the end of the header. No more actions are taken.
 */
tinyelf_ret_t tinyelf_parse(Elf *self);

/**
 * @brief Get a program header with number @p idx
 */
tinyelf_ret_t tinyelf_phdr_get(Elf *self, struct tinyelf_program_header *phdr, uint32_t idx);

/**
 * @brieg Get a section header with number @p idx
 */
tinyelf_ret_t tinyelf_shdr_get(Elf *self, struct tinyelf_section_header *shdr, uint32_t idx);

/**
 * @brief Get a section name
 *
 * @param offset Offset of the section name string in the text string section
 *               (get it from the @p name field of the section header struct).
 */
tinyelf_ret_t tinyelf_section_get_name(Elf *self, uint32_t offset, char *buf, size_t size);

/**
 * @brief Try to find section by its name
 *
 * Linearly search all sections and compare if the name matches. Return the section header if yes.
 */
tinyelf_ret_t tinyelf_section_find_by_name(Elf *self, const char *name, struct tinyelf_section_header *shdr);

