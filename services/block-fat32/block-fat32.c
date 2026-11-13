/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FAT32 filesystem on a block device
 *
 * Copyright (c) 2025, Bjørn Brodtkorb
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <main.h>

#include <interfaces/block.h>
#include <interfaces/clock.h>
#include <interfaces/fs.h>

#include "block-fat32.h"

#define MODULE_NAME "block-fat32"

#define SECTOR_SIZE 512
#define SECTOR_SHIFT 9
#define DIR_ENTRY_SIZE 32
#define DIR_ENTRIES_PER_SECTOR (SECTOR_SIZE / DIR_ENTRY_SIZE)
/* A directory may contain at most 65536 entries. It also bounds walking a looped directory cluster chain. */
#define DIR_ENTRIES_MAX 65536
#define FAT_ENTRIES_PER_SECTOR (SECTOR_SIZE / 4)

/* FAT entry values */
#define FAT_ENTRY_MASK 0x0fffffffUL
#define FAT_FREE 0
#define FAT_EOC 0x0fffffffUL
#define FAT_EOC_MIN 0x0ffffff8UL
#define FAT_FIRST_CLUSTER 2

/* Boot sector (BPB) fields */
#define BPB_BYTES_PER_SECTOR 11
#define BPB_SECTORS_PER_CLUSTER 13
#define BPB_RESERVED_SECTORS 14
#define BPB_FAT_COUNT 16
#define BPB_ROOT_ENTRIES 17
#define BPB_TOTAL_SECTORS16 19
#define BPB_FAT_SECTORS16 22
#define BPB_TOTAL_SECTORS32 32
#define BPB_FAT_SECTORS32 36
#define BPB_EXT_FLAGS 40
#define BPB_FS_VERSION 42
#define BPB_ROOT_CLUSTER 44
#define BPB_FSINFO_SECTOR 48
#define BPB_SIGNATURE 510
#define BPB_EXT_FLAGS_NO_MIRROR 0x80
#define BPB_EXT_FLAGS_ACTIVE_FAT 0x0f

/* MBR partition table */
#define MBR_PARTITIONS 446
#define MBR_PARTITION_SIZE 16
#define MBR_PARTITION_COUNT 4
#define MBR_PARTITION_TYPE 4
#define MBR_PARTITION_LBA 8
#define MBR_PARTITION_SECTORS 12
#define MBR_TYPE_FAT32_CHS 0x0b
#define MBR_TYPE_FAT32_LBA 0x0c

/* FSInfo sector fields */
#define FSINFO_LEAD_SIG 0
#define FSINFO_STRUC_SIG 484
#define FSINFO_FREE_COUNT 488
#define FSINFO_NEXT_FREE 492
#define FSINFO_TRAIL_SIG 508
#define FSINFO_LEAD_SIG_VALUE 0x41615252UL
#define FSINFO_STRUC_SIG_VALUE 0x61417272UL
#define FSINFO_TRAIL_SIG_VALUE 0xaa550000UL
#define FSINFO_UNKNOWN 0xffffffffUL

/* Format parameters */
#define FORMAT_PART_LBA 8192
#define FORMAT_RESERVED_SECTORS 32
#define FORMAT_FAT_COUNT 2
#define FORMAT_BACKUP_BOOT_SECTOR 6
#define FORMAT_FSINFO_SECTOR 1
#define FORMAT_MEDIA 0xf8
#define FORMAT_ZERO_SECTORS 8
#define FAT_CLUSTERS_MIN 65525

/* Directory entry fields */
#define DE_NAME 0
#define DE_ATTR 11
#define DE_NTRES 12
#define DE_CRT_TIME 14
#define DE_CRT_DATE 16
#define DE_ACC_DATE 18
#define DE_CLUSTER_HI 20
#define DE_WRT_TIME 22
#define DE_WRT_DATE 24
#define DE_CLUSTER_LO 26
#define DE_SIZE 28
#define DE_NAME_LEN 11
#define DE_BASE_LEN 8
#define DE_EXT_LEN 3

#define DE_FREE 0xe5
#define DE_END 0x00
/* A short name starting with 0xe5 is stored with 0x05 in its first byte. */
#define DE_KANJI_E5 0x05

#define ATTR_READ_ONLY 0x01
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LFN 0x0f
#define ATTR_LFN_MASK 0x3f

/* Lowercase base name and extension flags of a short name */
#define NTRES_LOWER_BASE 0x08
#define NTRES_LOWER_EXT 0x10

/* Long name entry fields */
#define LFN_ORD 0
#define LFN_CHECKSUM 13
#define LFN_LAST 0x40
#define LFN_ORD_MASK 0x1f
#define LFN_CHARS 13
#define LFN_ENTRIES_MAX 20
#define LFN_UNITS_MAX 255
#define LFN_PADDING 0xffff

/* Offsets of the 13 UTF-16 code units in a long name entry */
static const uint8_t lfn_slots[LFN_CHARS] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

/* Open file access flags */
#define FILE_READ 0x01
#define FILE_WRITE 0x02
#define FILE_APPEND 0x04


/* Position of an entry within a directory. */
struct fat_dir_pos {
	/* First cluster of the directory. */
	uint32_t start;
	/* Cluster containing the entry. */
	uint32_t cluster;
	/* Index of the entry within the directory. */
	uint32_t index;
};

/* A directory entry set: optional long name entries followed by the short name entry. */
struct fat_entry {
	/* Short name entry. Its start is 0 for the root directory pseudo-entry. */
	struct fat_dir_pos pos;
	/* First entry of the set (the first long name entry or the short name entry itself). */
	struct fat_dir_pos first;
	uint8_t lfn_count;
	uint8_t attr;
	/* The "." or ".." entry. */
	bool dot;
	uint32_t cluster;
	uint32_t size;
};

/* Directory scan state, also used as the open directory handle. */
struct fat_scan {
	/* Next entry to examine. */
	struct fat_dir_pos pos;
	/* First entry of the long name sequence being collected. */
	struct fat_dir_pos first;
	/* Ordinal of the last collected long name entry, 0 if there is no sequence. */
	uint8_t lfn_next;
	uint8_t lfn_count;
	uint8_t lfn_sum;
	/* The long name sequence matches the searched name. */
	bool lfn_match;
	bool end;
};

struct block_fat32_file {
	struct block_fat32_file *next;
	uint32_t generation;
	/* Short name entry of the file. */
	struct fat_dir_pos entry;
	uint32_t start;
	uint32_t size;
	uint32_t pos;
	/* A cluster of the file and its index in the chain, to avoid walking the chain from the start. */
	uint32_t cur_cluster;
	uint32_t cur_index;
	uint8_t access;
	/* The directory entry needs to be updated. */
	bool dirty;
};

struct block_fat32_dir {
	uint32_t generation;
	struct fat_scan scan;
};

/* UTF-8 name decoder producing UTF-16 code units. */
struct fat_name_iter {
	const char *p;
	const char *end;
	uint16_t low;
};

/* UTF-16 code units to a NUL terminated UTF-8 string writer, truncating at a character boundary. */
struct fat_name_out {
	char *buf;
	size_t size;
	size_t len;
	uint16_t high;
	bool full;
};


static const struct fs_vmt block_fat32_fs_vmt;


/**********************************************************************************************************************
 * Little endian field access
 **********************************************************************************************************************/

static uint16_t ld16(const uint8_t *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}


static uint32_t ld32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static void st16(uint8_t *p, uint16_t v) {
	p[0] = v & 0xff;
	p[1] = v >> 8;
}


static void st32(uint8_t *p, uint32_t v) {
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = v >> 24;
}


/**********************************************************************************************************************
 * Block device access and the sector cache
 **********************************************************************************************************************/

static void fat_unmount(BlockFat32 *self) {
	self->mounted = false;
	self->free_count_valid = false;
	self->cache_valid = false;
	self->cache_dirty = false;
}


/* A changed or removed media unmounts the volume, the remaining operations fail until it is mounted again. */
static block_fat32_ret_t fat_dev_ret(BlockFat32 *self, block_ret_t ret) {
	switch (ret) {
		case BLOCK_RET_OK:
			return BLOCK_FAT32_RET_OK;
		case BLOCK_RET_DEVICE_CHANGED:
		case BLOCK_RET_NO_MEDIA:
			fat_unmount(self);
			return BLOCK_FAT32_RET_IO;
		default:
			return BLOCK_FAT32_RET_IO;
	}
}


static block_fat32_ret_t fat_dev_read(BlockFat32 *self, uint32_t lba, void *buf, size_t count) {
	return fat_dev_ret(self, self->conf.block->vmt->read(self->conf.block, lba, buf, count));
}


static block_fat32_ret_t fat_dev_write(BlockFat32 *self, uint32_t lba, const void *buf, size_t count) {
	return fat_dev_ret(self, self->conf.block->vmt->write(self->conf.block, lba, buf, count));
}


/* Write the cached sector back. FAT sectors are written to all FAT copies. */
static block_fat32_ret_t fat_cache_flush(BlockFat32 *self) {
	if (!self->cache_valid || !self->cache_dirty) {
		return BLOCK_FAT32_RET_OK;
	}
	block_fat32_ret_t ret = fat_dev_write(self, self->cache_lba, self->cache, 1);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (self->cache_lba >= self->fat_lba && self->cache_lba - self->fat_lba < self->fat_sectors) {
		for (uint32_t i = 1; i < self->fat_count; i++) {
			ret = fat_dev_write(self, self->cache_lba + i * self->fat_sectors, self->cache, 1);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
		}
	}
	self->cache_dirty = false;
	return BLOCK_FAT32_RET_OK;
}


static block_fat32_ret_t fat_cache_load(BlockFat32 *self, uint32_t lba) {
	if (self->cache_valid && self->cache_lba == lba) {
		return BLOCK_FAT32_RET_OK;
	}
	block_fat32_ret_t ret = fat_cache_flush(self);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	self->cache_valid = false;
	ret = fat_dev_read(self, lba, self->cache, 1);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	self->cache_lba = lba;
	self->cache_valid = true;
	return BLOCK_FAT32_RET_OK;
}


/* Start a sector which is going to be overwritten from a zeroed buffer instead of reading it. */
static block_fat32_ret_t fat_cache_zero(BlockFat32 *self, uint32_t lba) {
	block_fat32_ret_t ret = fat_cache_flush(self);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	memset(self->cache, 0, SECTOR_SIZE);
	self->cache_lba = lba;
	self->cache_valid = true;
	self->cache_dirty = true;
	return BLOCK_FAT32_RET_OK;
}


/* Read sectors directly to a buffer. A dirty cached copy is written first. */
static block_fat32_ret_t fat_direct_read(BlockFat32 *self, uint32_t lba, void *buf, size_t count) {
	if (self->cache_valid && self->cache_lba >= lba && self->cache_lba - lba < count) {
		block_fat32_ret_t ret = fat_cache_flush(self);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
	}
	return fat_dev_read(self, lba, buf, count);
}


/* Write sectors directly from a buffer. A cached copy is dropped as it is overwritten. */
static block_fat32_ret_t fat_direct_write(BlockFat32 *self, uint32_t lba, const void *buf, size_t count) {
	if (self->cache_valid && self->cache_lba >= lba && self->cache_lba - lba < count) {
		self->cache_valid = false;
		self->cache_dirty = false;
	}
	return fat_dev_write(self, lba, buf, count);
}


/* Commit everything to the media. */
static block_fat32_ret_t fat_sync(BlockFat32 *self) {
	block_fat32_ret_t ret = fat_cache_flush(self);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	return fat_dev_ret(self, self->conf.block->vmt->flush(self->conf.block));
}


/**********************************************************************************************************************
 * Mounting
 **********************************************************************************************************************/

static bool fat_bpb_valid(const uint8_t *b) {
	uint8_t spc = b[BPB_SECTORS_PER_CLUSTER];
	return ld16(&b[BPB_SIGNATURE]) == 0xaa55 &&
	       ld16(&b[BPB_BYTES_PER_SECTOR]) == SECTOR_SIZE &&
	       spc != 0 && (spc & (spc - 1)) == 0 &&
	       ld16(&b[BPB_RESERVED_SECTORS]) != 0 &&
	       b[BPB_FAT_COUNT] != 0 &&
	       ld16(&b[BPB_ROOT_ENTRIES]) == 0 &&
	       ld16(&b[BPB_TOTAL_SECTORS16]) == 0 &&
	       ld16(&b[BPB_FAT_SECTORS16]) == 0 &&
	       ld32(&b[BPB_FAT_SECTORS32]) != 0 &&
	       ld32(&b[BPB_TOTAL_SECTORS32]) != 0 &&
	       ld16(&b[BPB_FS_VERSION]) == 0;
}


/* Find the boot sector of the volume, either at the first block or in the first FAT32 MBR partition. */
static block_fat32_ret_t fat_find_volume(BlockFat32 *self, uint32_t block_count, uint32_t *part) {
	*part = 0;
	block_fat32_ret_t ret = fat_cache_load(self, 0);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (fat_bpb_valid(self->cache)) {
		return BLOCK_FAT32_RET_OK;
	}
	if (ld16(&self->cache[BPB_SIGNATURE]) != 0xaa55) {
		return BLOCK_FAT32_RET_NO_FS;
	}
	for (size_t i = 0; i < MBR_PARTITION_COUNT; i++) {
		const uint8_t *p = &self->cache[MBR_PARTITIONS + i * MBR_PARTITION_SIZE];
		uint32_t lba = ld32(&p[MBR_PARTITION_LBA]);
		uint32_t sectors = ld32(&p[MBR_PARTITION_SECTORS]);
		if ((p[MBR_PARTITION_TYPE] == MBR_TYPE_FAT32_CHS || p[MBR_PARTITION_TYPE] == MBR_TYPE_FAT32_LBA) &&
		    lba != 0 && sectors != 0 && lba < block_count && sectors <= block_count - lba) {
			*part = lba;
			ret = fat_cache_load(self, lba);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
			return fat_bpb_valid(self->cache) ? BLOCK_FAT32_RET_OK : BLOCK_FAT32_RET_NO_FS;
		}
	}
	return BLOCK_FAT32_RET_NO_FS;
}


static block_fat32_ret_t fat_mount(BlockFat32 *self) {
	fat_unmount(self);

	/* Reading the geometry also acknowledges a media change. */
	size_t block_size = 0;
	uint32_t block_count = 0;
	block_fat32_ret_t ret = fat_dev_ret(self,
		self->conf.block->vmt->get_geometry(self->conf.block, &block_size, &block_count));
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (block_size != SECTOR_SIZE) {
		return BLOCK_FAT32_RET_NO_FS;
	}

	uint32_t part = 0;
	ret = fat_find_volume(self, block_count, &part);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}

	/* The boot sector is in the cache now. All computations are done in 64 bits to catch overflows. */
	const uint8_t *b = self->cache;
	uint64_t reserved = ld16(&b[BPB_RESERVED_SECTORS]);
	uint64_t fat_sectors = ld32(&b[BPB_FAT_SECTORS32]);
	uint64_t fat_copies = b[BPB_FAT_COUNT];
	uint64_t total = ld32(&b[BPB_TOTAL_SECTORS32]);
	uint64_t meta = reserved + fat_copies * fat_sectors;
	if ((uint64_t)part + total > block_count || meta >= total) {
		return BLOCK_FAT32_RET_CORRUPT;
	}
	uint8_t shift = 0;
	while ((1U << shift) < b[BPB_SECTORS_PER_CLUSTER]) {
		shift++;
	}

	/* The number of clusters is limited both by the data area and by the FAT size. */
	uint64_t clusters = (total - meta) >> shift;
	uint64_t fat_capacity = fat_sectors * FAT_ENTRIES_PER_SECTOR - FAT_FIRST_CLUSTER;
	if (clusters > fat_capacity) {
		clusters = fat_capacity;
	}
	if (clusters == 0 || clusters > FAT_EOC_MIN - 1 - FAT_FIRST_CLUSTER) {
		return BLOCK_FAT32_RET_CORRUPT;
	}

	uint32_t root = ld32(&b[BPB_ROOT_CLUSTER]);
	if (root < FAT_FIRST_CLUSTER || root - FAT_FIRST_CLUSTER >= clusters) {
		return BLOCK_FAT32_RET_CORRUPT;
	}

	/* With mirroring disabled, only the active FAT is used. */
	uint16_t ext_flags = ld16(&b[BPB_EXT_FLAGS]);
	self->fat_lba = part + (uint32_t)reserved;
	self->fat_count = (uint8_t)fat_copies;
	if (ext_flags & BPB_EXT_FLAGS_NO_MIRROR) {
		uint32_t active = ext_flags & BPB_EXT_FLAGS_ACTIVE_FAT;
		if (active >= fat_copies) {
			return BLOCK_FAT32_RET_CORRUPT;
		}
		self->fat_lba += active * (uint32_t)fat_sectors;
		self->fat_count = 1;
	}

	uint16_t fsinfo = ld16(&b[BPB_FSINFO_SECTOR]);
	self->fsinfo_lba = (fsinfo != 0 && fsinfo < reserved) ? part + fsinfo : 0;
	self->fat_sectors = (uint32_t)fat_sectors;
	self->data_lba = part + (uint32_t)meta;
	self->cluster_count = (uint32_t)clusters;
	self->cluster_shift = shift;
	self->root_cluster = root;
	self->free_hint = FAT_FIRST_CLUSTER;
	self->fsinfo_invalidated = false;
	self->generation++;
	self->mounted = true;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("mounted FAT32 volume, %lu MB, %lu B clusters"),
		(uint32_t)((clusters << (shift + SECTOR_SHIFT)) >> 20),
		(uint32_t)SECTOR_SIZE << shift
	);
	return BLOCK_FAT32_RET_OK;
}


/* Mount the volume if it is not mounted yet. Called at the start of every operation. If no valid volume was found
 * on the current media, a single sector read tells whether the media changed since. Mounting is retried only then,
 * the failure is therefore logged once per media. */
static block_fat32_ret_t fat_ready(BlockFat32 *self) {
	if (self->mounted) {
		return BLOCK_FAT32_RET_OK;
	}
	if (self->mount_failed) {
		self->cache_valid = false;
		if (self->conf.block->vmt->read(self->conf.block, 0, self->cache, 1) == BLOCK_RET_OK) {
			return BLOCK_FAT32_RET_NO_FS;
		}
	}
	block_fat32_ret_t ret = fat_mount(self);
	self->mount_failed = (ret == BLOCK_FAT32_RET_NO_FS || ret == BLOCK_FAT32_RET_CORRUPT);
	if (self->mount_failed) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no valid FAT32 volume found"));
	}
	return ret;
}


/* The FSInfo free cluster count and next free hint are not maintained. Mark them unknown on the first modification
 * of the volume so other systems do not trust stale values. */
static block_fat32_ret_t fat_volume_modify(BlockFat32 *self) {
	if (self->fsinfo_invalidated || self->fsinfo_lba == 0) {
		return BLOCK_FAT32_RET_OK;
	}
	block_fat32_ret_t ret = fat_cache_load(self, self->fsinfo_lba);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (ld32(&self->cache[FSINFO_LEAD_SIG]) == FSINFO_LEAD_SIG_VALUE &&
	    ld32(&self->cache[FSINFO_STRUC_SIG]) == FSINFO_STRUC_SIG_VALUE &&
	    ld32(&self->cache[FSINFO_TRAIL_SIG]) == FSINFO_TRAIL_SIG_VALUE) {
		st32(&self->cache[FSINFO_FREE_COUNT], FSINFO_UNKNOWN);
		st32(&self->cache[FSINFO_NEXT_FREE], FSINFO_UNKNOWN);
		self->cache_dirty = true;
	}
	self->fsinfo_invalidated = true;
	return BLOCK_FAT32_RET_OK;
}


/**********************************************************************************************************************
 * File allocation table
 **********************************************************************************************************************/

static bool fat_cluster_valid(BlockFat32 *self, uint32_t cluster) {
	return cluster >= FAT_FIRST_CLUSTER && cluster - FAT_FIRST_CLUSTER < self->cluster_count;
}


static uint32_t fat_cluster_lba(BlockFat32 *self, uint32_t cluster) {
	return self->data_lba + ((cluster - FAT_FIRST_CLUSTER) << self->cluster_shift);
}


static block_fat32_ret_t fat_get(BlockFat32 *self, uint32_t cluster, uint32_t *value) {
	if (!fat_cluster_valid(self, cluster)) {
		return BLOCK_FAT32_RET_CORRUPT;
	}
	block_fat32_ret_t ret = fat_cache_load(self, self->fat_lba + cluster / FAT_ENTRIES_PER_SECTOR);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	*value = ld32(&self->cache[(cluster % FAT_ENTRIES_PER_SECTOR) * 4]) & FAT_ENTRY_MASK;
	return BLOCK_FAT32_RET_OK;
}


/* The upper 4 bits of a FAT entry are reserved and must be preserved. */
static block_fat32_ret_t fat_set(BlockFat32 *self, uint32_t cluster, uint32_t value) {
	if (!fat_cluster_valid(self, cluster)) {
		return BLOCK_FAT32_RET_CORRUPT;
	}
	block_fat32_ret_t ret = fat_volume_modify(self);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	ret = fat_cache_load(self, self->fat_lba + cluster / FAT_ENTRIES_PER_SECTOR);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	uint8_t *p = &self->cache[(cluster % FAT_ENTRIES_PER_SECTOR) * 4];
	st32(p, (ld32(p) & ~FAT_ENTRY_MASK) | (value & FAT_ENTRY_MASK));
	self->cache_dirty = true;
	return BLOCK_FAT32_RET_OK;
}


/* Get the next cluster of a chain. Returns EOF at the end of the chain. Free, bad or out of range clusters linked
 * from a chain are reported as corruption. */
static block_fat32_ret_t fat_next(BlockFat32 *self, uint32_t cluster, uint32_t *next) {
	uint32_t value = 0;
	block_fat32_ret_t ret = fat_get(self, cluster, &value);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (value >= FAT_EOC_MIN) {
		return BLOCK_FAT32_RET_EOF;
	}
	if (!fat_cluster_valid(self, value)) {
		return BLOCK_FAT32_RET_CORRUPT;
	}
	*next = value;
	return BLOCK_FAT32_RET_OK;
}


/* Allocate a free cluster and append it to the chain ending with @p prev (0 to start a new chain). The new cluster
 * is terminated first and linked afterwards, an interrupted allocation therefore only leaks the cluster. */
static block_fat32_ret_t fat_alloc(BlockFat32 *self, uint32_t prev, uint32_t *cluster) {
	for (uint32_t i = 0; i < self->cluster_count; i++) {
		uint32_t c = FAT_FIRST_CLUSTER + (self->free_hint - FAT_FIRST_CLUSTER + i) % self->cluster_count;
		uint32_t value = 0;
		block_fat32_ret_t ret = fat_get(self, c, &value);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (value != FAT_FREE) {
			continue;
		}
		ret = fat_set(self, c, FAT_EOC);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (self->free_count_valid) {
			self->free_count--;
		}
		if (prev != 0) {
			ret = fat_set(self, prev, c);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
		}
		self->free_hint = fat_cluster_valid(self, c + 1) ? c + 1 : FAT_FIRST_CLUSTER;
		*cluster = c;
		return BLOCK_FAT32_RET_OK;
	}
	return BLOCK_FAT32_RET_FULL;
}


/* Free a whole cluster chain. The walk is bounded by the number of clusters to survive a looped chain. */
static block_fat32_ret_t fat_free_chain(BlockFat32 *self, uint32_t cluster) {
	for (uint32_t i = 0; i < self->cluster_count; i++) {
		uint32_t value = 0;
		block_fat32_ret_t ret = fat_get(self, cluster, &value);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		ret = fat_set(self, cluster, FAT_FREE);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (self->free_count_valid) {
			self->free_count++;
		}
		if (value >= FAT_EOC_MIN) {
			return BLOCK_FAT32_RET_OK;
		}
		if (!fat_cluster_valid(self, value)) {
			return BLOCK_FAT32_RET_CORRUPT;
		}
		cluster = value;
	}
	return BLOCK_FAT32_RET_CORRUPT;
}


static block_fat32_ret_t fat_zero_cluster(BlockFat32 *self, uint32_t cluster) {
	uint32_t lba = fat_cluster_lba(self, cluster);
	for (uint32_t i = 0; i < (1UL << self->cluster_shift); i++) {
		block_fat32_ret_t ret = fat_cache_zero(self, lba + i);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
	}
	return BLOCK_FAT32_RET_OK;
}


/**********************************************************************************************************************
 * Names
 **********************************************************************************************************************/

static uint32_t fat_upper(uint32_t c) {
	return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}


static bool fat_is_dot(const char *name, size_t len) {
	return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}


static void fat_name_iter_init(struct fat_name_iter *it, const char *name, size_t len) {
	it->p = name;
	it->end = name + len;
	it->low = 0;
}


/* Return the next UTF-16 code unit of the name, 0 at the end of the name or -1 on invalid UTF-8. */
static int32_t fat_name_next(struct fat_name_iter *it) {
	if (it->low != 0) {
		uint16_t low = it->low;
		it->low = 0;
		return low;
	}
	if (it->p >= it->end) {
		return 0;
	}

	uint8_t c = (uint8_t)*it->p++;
	if (c < 0x80) {
		return c;
	}
	uint32_t cp = 0;
	size_t n = 0;
	if ((c & 0xe0) == 0xc0) {
		cp = c & 0x1f;
		n = 1;
	} else if ((c & 0xf0) == 0xe0) {
		cp = c & 0x0f;
		n = 2;
	} else if ((c & 0xf8) == 0xf0) {
		cp = c & 0x07;
		n = 3;
	} else {
		return -1;
	}
	if ((size_t)(it->end - it->p) < n) {
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		c = (uint8_t)*it->p++;
		if ((c & 0xc0) != 0x80) {
			return -1;
		}
		cp = (cp << 6) | (c & 0x3f);
	}

	/* Reject overlong encodings, UTF-16 surrogates and code points out of the Unicode range. */
	static const uint32_t cp_min[] = {0, 0x80, 0x800, 0x10000};
	if (cp < cp_min[n] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
		return -1;
	}
	if (cp >= 0x10000) {
		cp -= 0x10000;
		it->low = (uint16_t)(0xdc00 | (cp & 0x3ff));
		return (int32_t)(0xd800 | (cp >> 10));
	}
	return (int32_t)cp;
}


static void fat_name_out_init(struct fat_name_out *o, char *buf, size_t size) {
	memset(o, 0, sizeof(struct fat_name_out));
	o->buf = buf;
	o->size = size;
	o->buf[0] = '\0';
}


/* Append an UTF-16 code unit. Unpaired surrogates are replaced with '?'. */
static void fat_name_put(struct fat_name_out *o, uint16_t u) {
	if (u >= 0xd800 && u <= 0xdbff) {
		o->high = u;
		return;
	}
	uint32_t cp = u;
	if (u >= 0xdc00 && u <= 0xdfff) {
		cp = (o->high != 0) ? 0x10000 + (((uint32_t)o->high - 0xd800) << 10) + (u - 0xdc00) : '?';
	}
	o->high = 0;

	uint8_t enc[4];
	size_t n = 0;
	if (cp < 0x80) {
		enc[n++] = (uint8_t)cp;
	} else if (cp < 0x800) {
		enc[n++] = (uint8_t)(0xc0 | (cp >> 6));
		enc[n++] = (uint8_t)(0x80 | (cp & 0x3f));
	} else if (cp < 0x10000) {
		enc[n++] = (uint8_t)(0xe0 | (cp >> 12));
		enc[n++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
		enc[n++] = (uint8_t)(0x80 | (cp & 0x3f));
	} else {
		enc[n++] = (uint8_t)(0xf0 | (cp >> 18));
		enc[n++] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
		enc[n++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
		enc[n++] = (uint8_t)(0x80 | (cp & 0x3f));
	}
	if (o->full || o->len + n >= o->size) {
		o->full = true;
		return;
	}
	memcpy(&o->buf[o->len], enc, n);
	o->len += n;
	o->buf[o->len] = '\0';
}


/* Check a name to be created and count its UTF-16 code units. */
static block_fat32_ret_t fat_name_check(const char *name, size_t len, size_t *units) {
	if (len == 0 || fat_is_dot(name, len)) {
		return BLOCK_FAT32_RET_BAD_ARG;
	}
	struct fat_name_iter it;
	fat_name_iter_init(&it, name, len);
	*units = 0;
	int32_t u = 0;
	while ((u = fat_name_next(&it)) != 0) {
		if (u < 0x20 || (u < 0x80 && strchr("\"*/:<>?\\|", u) != NULL)) {
			return BLOCK_FAT32_RET_BAD_ARG;
		}
		(*units)++;
	}
	return (*units <= LFN_UNITS_MAX) ? BLOCK_FAT32_RET_OK : BLOCK_FAT32_RET_BAD_ARG;
}


/* Get the next component of a path. Trailing dots and spaces are not part of a FAT name, except in the "." and ".."
 * components. Returns false if there are no more components. */
static bool fat_path_next(const char **path, const char **name, size_t *len) {
	const char *p = *path;
	while (*p == '/') {
		p++;
	}
	if (*p == '\0') {
		return false;
	}
	*name = p;
	while (*p != '\0' && *p != '/') {
		p++;
	}
	*len = (size_t)(p - *name);
	*path = p;
	if (!fat_is_dot(*name, *len)) {
		while (*len > 0 && ((*name)[*len - 1] == ' ' || (*name)[*len - 1] == '.')) {
			(*len)--;
		}
	}
	return true;
}


static uint8_t fat_sfn_checksum(const uint8_t *sfn) {
	uint8_t sum = 0;
	for (size_t i = 0; i < DE_NAME_LEN; i++) {
		sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + sfn[i]);
	}
	return sum;
}


/* Format a short name entry as "NAME.EXT" honouring the lowercase flags. Non-ASCII characters are replaced with '_'
 * as their codepage is unknown. */
static size_t fat_sfn_format(const uint8_t *e, char out[DE_NAME_LEN + 2]) {
	size_t n = 0;
	for (size_t i = 0; i < DE_NAME_LEN; i++) {
		if (i == DE_BASE_LEN) {
			if (e[DE_BASE_LEN] == ' ') {
				break;
			}
			out[n++] = '.';
		}
		uint8_t c = e[i];
		if (c == ' ') {
			if (i < DE_BASE_LEN) {
				i = DE_BASE_LEN - 1;
			}
			continue;
		}
		if (i == 0 && c == DE_KANJI_E5) {
			c = DE_FREE;
		}
		bool lower = e[DE_NTRES] & ((i < DE_BASE_LEN) ? NTRES_LOWER_BASE : NTRES_LOWER_EXT);
		if (c >= 0x80) {
			c = '_';
		} else if (lower && c >= 'A' && c <= 'Z') {
			c = (uint8_t)(c - 'A' + 'a');
		}
		out[n++] = (char)c;
	}
	out[n] = '\0';
	return n;
}


static bool fat_sfn_match(const uint8_t *e, const char *name, size_t len) {
	char sfn[DE_NAME_LEN + 2];
	if (fat_sfn_format(e, sfn) != len) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (fat_upper((uint8_t)sfn[i]) != fat_upper((uint8_t)name[i])) {
			return false;
		}
	}
	return true;
}


/* Compare the part of a name stored in a long name entry. ASCII letters are compared case-insensitively. */
static bool fat_lfn_match(const uint8_t *e, const char *name, size_t len) {
	struct fat_name_iter it;
	fat_name_iter_init(&it, name, len);
	for (size_t i = 0; i < (size_t)((e[LFN_ORD] & LFN_ORD_MASK) - 1) * LFN_CHARS; i++) {
		if (fat_name_next(&it) <= 0) {
			return false;
		}
	}
	for (size_t i = 0; i < LFN_CHARS; i++) {
		uint16_t u = ld16(&e[lfn_slots[i]]);
		int32_t c = fat_name_next(&it);
		if (c < 0) {
			return false;
		}
		if (c == 0 || u == 0 || u == LFN_PADDING) {
			/* Both names have to end at the same position. */
			return c == 0 && (u == 0 || u == LFN_PADDING);
		}
		if (fat_upper(u) != fat_upper((uint32_t)c)) {
			return false;
		}
	}
	/* The last entry holds the end of the name. */
	return !(e[LFN_ORD] & LFN_LAST) || fat_name_next(&it) == 0;
}


/* Fill a long name entry with its part of the name. */
static void fat_lfn_fill(uint8_t *e, const char *name, size_t len, uint8_t ord, bool last, uint8_t sum) {
	memset(e, 0, DIR_ENTRY_SIZE);
	e[LFN_ORD] = ord | (last ? LFN_LAST : 0);
	e[DE_ATTR] = ATTR_LFN;
	e[LFN_CHECKSUM] = sum;

	struct fat_name_iter it;
	fat_name_iter_init(&it, name, len);
	for (size_t i = 0; i < (size_t)(ord - 1) * LFN_CHARS; i++) {
		fat_name_next(&it);
	}
	/* The name is terminated with a 0 code unit unless it fills the entry, the rest is padded. */
	bool ended = false;
	for (size_t i = 0; i < LFN_CHARS; i++) {
		uint16_t u = LFN_PADDING;
		if (!ended) {
			u = (uint16_t)fat_name_next(&it);
			ended = (u == 0);
		}
		st16(&e[lfn_slots[i]], u);
	}
}


/* Create the short name basis of a name. Returns true if the short name represents the name exactly, possibly
 * using the lowercase flags in @p ntres. Long name entries are not needed then. */
static bool fat_sfn_basis(const char *name, size_t len, uint8_t sfn[DE_NAME_LEN], uint8_t *ntres) {
	memset(sfn, ' ', DE_NAME_LEN);
	*ntres = 0;

	/* The extension starts after the last dot. A leading dot does not start an extension. */
	size_t dot = len;
	for (size_t i = len; i > 1; i--) {
		if (name[i - 1] == '.') {
			dot = i - 1;
			break;
		}
	}

	bool exact = true;
	for (size_t part = 0; part < 2; part++) {
		size_t from = (part == 0) ? 0 : dot + 1;
		size_t to = (part == 0) ? dot : len;
		size_t max = (part == 0) ? DE_BASE_LEN : DE_EXT_LEN;
		uint8_t *out = (part == 0) ? sfn : &sfn[DE_BASE_LEN];
		bool lower = false;
		bool upper = false;
		size_t n = 0;
		for (size_t i = from; i < to; i++) {
			uint8_t c = (uint8_t)name[i];
			if (c == ' ' || c == '.' || (c & 0xc0) == 0x80) {
				/* Spaces, dots and UTF-8 continuation bytes are dropped. */
				exact = false;
				continue;
			}
			if (c >= 0x80 || strchr("+,;=[]", c) != NULL) {
				c = '_';
				exact = false;
			} else if (c >= 'a' && c <= 'z') {
				c = (uint8_t)(c - 'a' + 'A');
				lower = true;
			} else if (c >= 'A' && c <= 'Z') {
				upper = true;
			}
			if (n < max) {
				out[n++] = c;
			} else {
				exact = false;
			}
		}
		if (lower && upper) {
			exact = false;
		} else if (lower) {
			*ntres |= (part == 0) ? NTRES_LOWER_BASE : NTRES_LOWER_EXT;
		}
		if (part == 0 && n == 0) {
			out[0] = '_';
			exact = false;
		}
	}
	return exact;
}


/**********************************************************************************************************************
 * Directories
 **********************************************************************************************************************/

static void fat_dir_pos_init(struct fat_dir_pos *pos, uint32_t start) {
	pos->start = start;
	pos->cluster = start;
	pos->index = 0;
}


/* Advance to the next entry. At the end of the cluster chain the directory is either extended with a zeroed cluster
 * or EOF is returned. */
static block_fat32_ret_t fat_dir_pos_next(BlockFat32 *self, struct fat_dir_pos *pos, bool extend) {
	if (pos->index + 1 >= DIR_ENTRIES_MAX) {
		return BLOCK_FAT32_RET_FULL;
	}
	if (((pos->index + 1) & ((DIR_ENTRIES_PER_SECTOR << self->cluster_shift) - 1)) != 0) {
		pos->index++;
		return BLOCK_FAT32_RET_OK;
	}
	uint32_t next = 0;
	block_fat32_ret_t ret = fat_next(self, pos->cluster, &next);
	if (ret == BLOCK_FAT32_RET_EOF && extend) {
		ret = fat_alloc(self, pos->cluster, &next);
		if (ret == BLOCK_FAT32_RET_OK) {
			ret = fat_zero_cluster(self, next);
		}
	}
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	pos->cluster = next;
	pos->index++;
	return BLOCK_FAT32_RET_OK;
}


/* Get a pointer to an entry in the cache. It is valid until the next cache operation. */
static block_fat32_ret_t fat_dir_entry(BlockFat32 *self, const struct fat_dir_pos *pos, uint8_t **e) {
	uint32_t sector = (pos->index / DIR_ENTRIES_PER_SECTOR) & ((1UL << self->cluster_shift) - 1);
	block_fat32_ret_t ret = fat_cache_load(self, fat_cluster_lba(self, pos->cluster) + sector);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	*e = &self->cache[(pos->index % DIR_ENTRIES_PER_SECTOR) * DIR_ENTRY_SIZE];
	return BLOCK_FAT32_RET_OK;
}


static void fat_entry_root(BlockFat32 *self, struct fat_entry *ent) {
	memset(ent, 0, sizeof(struct fat_entry));
	ent->attr = ATTR_DIRECTORY;
	ent->cluster = self->root_cluster;
}


static bool fat_entry_is_root(const struct fat_entry *ent) {
	return ent->pos.start == 0;
}


/* First cluster of a directory entry. The ".." entry of a subdirectory of the root directory refers to cluster 0. */
static uint32_t fat_entry_dir_cluster(BlockFat32 *self, const struct fat_entry *ent) {
	return (ent->cluster != 0) ? ent->cluster : self->root_cluster;
}


static void fat_scan_init(struct fat_scan *s, uint32_t start) {
	memset(s, 0, sizeof(struct fat_scan));
	fat_dir_pos_init(&s->pos, start);
}


/* Find the next entry set in a directory. Free entries, orphaned long name entries and the volume label are skipped.
 * If @p name is not NULL, only an entry set matching the name (either its long or short name) is returned. */
static block_fat32_ret_t fat_scan_next(BlockFat32 *self, struct fat_scan *s, const char *name, size_t len,
                                       struct fat_entry *ent) {
	while (!s->end) {
		uint8_t *e = NULL;
		block_fat32_ret_t ret = fat_dir_entry(self, &s->pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (e[DE_NAME] == DE_END) {
			s->end = true;
			break;
		}

		bool found = false;
		uint8_t ord = e[LFN_ORD] & LFN_ORD_MASK;
		if (e[DE_NAME] == DE_FREE) {
			s->lfn_next = 0;
		} else if ((e[DE_ATTR] & ATTR_LFN_MASK) == ATTR_LFN) {
			if ((e[LFN_ORD] & LFN_LAST) && ord >= 1 && ord <= LFN_ENTRIES_MAX) {
				/* The first entry of a sequence holds the last part of the name. */
				s->first = s->pos;
				s->lfn_count = ord;
				s->lfn_next = ord;
				s->lfn_sum = e[LFN_CHECKSUM];
				s->lfn_match = (name == NULL) || fat_lfn_match(e, name, len);
			} else if (!(e[LFN_ORD] & LFN_LAST) && s->lfn_next > 1 && ord == s->lfn_next - 1 &&
			           e[LFN_CHECKSUM] == s->lfn_sum) {
				s->lfn_next = ord;
				s->lfn_match = s->lfn_match && ((name == NULL) || fat_lfn_match(e, name, len));
			} else {
				s->lfn_next = 0;
			}
		} else if (e[DE_ATTR] & ATTR_VOLUME_ID) {
			s->lfn_next = 0;
		} else {
			/* A long name belongs to the short name entry only if the sequence is complete and the checksum
			 * matches. */
			bool lfn = (s->lfn_next == 1 && fat_sfn_checksum(e) == s->lfn_sum);
			ent->pos = s->pos;
			ent->first = lfn ? s->first : s->pos;
			ent->lfn_count = lfn ? s->lfn_count : 0;
			ent->attr = e[DE_ATTR];
			ent->dot = (e[DE_NAME] == '.');
			ent->cluster = ((uint32_t)ld16(&e[DE_CLUSTER_HI]) << 16) | ld16(&e[DE_CLUSTER_LO]);
			ent->size = ld32(&e[DE_SIZE]);
			found = (name == NULL) || (lfn && s->lfn_match) || fat_sfn_match(e, name, len);
			s->lfn_next = 0;
		}

		ret = fat_dir_pos_next(self, &s->pos, false);
		if (ret == BLOCK_FAT32_RET_EOF || ret == BLOCK_FAT32_RET_FULL) {
			s->end = true;
		} else if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (found) {
			return BLOCK_FAT32_RET_OK;
		}
	}
	return BLOCK_FAT32_RET_EOF;
}


/* Find an entry by name in a directory. The root directory has no "." and ".." entries, both refer to itself. */
static block_fat32_ret_t fat_dir_lookup(BlockFat32 *self, uint32_t dir, const char *name, size_t len,
                                        struct fat_entry *ent) {
	if (dir == self->root_cluster && fat_is_dot(name, len)) {
		fat_entry_root(self, ent);
		return BLOCK_FAT32_RET_OK;
	}
	struct fat_scan s;
	fat_scan_init(&s, dir);
	block_fat32_ret_t ret = fat_scan_next(self, &s, name, len, ent);
	return (ret == BLOCK_FAT32_RET_EOF) ? BLOCK_FAT32_RET_NOT_FOUND : ret;
}


/* Find the entry set with the short name entry at @p pos. */
static block_fat32_ret_t fat_entry_at(BlockFat32 *self, const struct fat_dir_pos *pos, struct fat_entry *ent) {
	struct fat_scan s;
	fat_scan_init(&s, pos->start);
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	while ((ret = fat_scan_next(self, &s, NULL, 0, ent)) == BLOCK_FAT32_RET_OK) {
		if (ent->pos.index == pos->index) {
			return BLOCK_FAT32_RET_OK;
		}
	}
	return (ret == BLOCK_FAT32_RET_EOF) ? BLOCK_FAT32_RET_CORRUPT : ret;
}


/* Copy the name of an entry set to a buffer. Long names are collected by walking the long name entries from the
 * one holding the start of the name, no name buffer is needed. */
static block_fat32_ret_t fat_entry_name(BlockFat32 *self, const struct fat_entry *ent, char *buf, size_t size) {
	if (buf == NULL || size == 0) {
		return BLOCK_FAT32_RET_OK;
	}
	if (fat_entry_is_root(ent)) {
		strlcpy(buf, "/", size);
		return BLOCK_FAT32_RET_OK;
	}
	uint8_t *e = NULL;
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	if (ent->lfn_count == 0) {
		ret = fat_dir_entry(self, &ent->pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		char sfn[DE_NAME_LEN + 2];
		fat_sfn_format(e, sfn);
		strlcpy(buf, sfn, size);
		return BLOCK_FAT32_RET_OK;
	}

	struct fat_name_out o;
	fat_name_out_init(&o, buf, size);
	for (uint8_t ord = 1; ord <= ent->lfn_count; ord++) {
		struct fat_dir_pos pos = ent->first;
		for (uint8_t i = 0; i < ent->lfn_count - ord; i++) {
			ret = fat_dir_pos_next(self, &pos, false);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
		}
		ret = fat_dir_entry(self, &pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		for (size_t i = 0; i < LFN_CHARS; i++) {
			uint16_t u = ld16(&e[lfn_slots[i]]);
			if (u == 0 || u == LFN_PADDING) {
				return BLOCK_FAT32_RET_OK;
			}
			fat_name_put(&o, u);
		}
	}
	return BLOCK_FAT32_RET_OK;
}


/* Resolve all components of a path except the last one, which must be directories. Return the first cluster of the
 * directory containing the last component and the last component itself (NULL for the root directory). */
static block_fat32_ret_t fat_path_parent(BlockFat32 *self, const char *path, uint32_t *dir, const char **name,
                                         size_t *len) {
	*dir = self->root_cluster;
	*name = NULL;
	*len = 0;

	const char *comp = NULL;
	size_t comp_len = 0;
	if (!fat_path_next(&path, &comp, &comp_len)) {
		return BLOCK_FAT32_RET_OK;
	}
	while (true) {
		const char *rest = path;
		const char *next = NULL;
		size_t next_len = 0;
		if (!fat_path_next(&rest, &next, &next_len)) {
			*name = comp;
			*len = comp_len;
			return BLOCK_FAT32_RET_OK;
		}
		struct fat_entry ent;
		block_fat32_ret_t ret = fat_dir_lookup(self, *dir, comp, comp_len, &ent);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (!(ent.attr & ATTR_DIRECTORY)) {
			return BLOCK_FAT32_RET_NOT_FOUND;
		}
		*dir = fat_entry_dir_cluster(self, &ent);
		comp = next;
		comp_len = next_len;
		path = rest;
	}
}


static block_fat32_ret_t fat_path_lookup(BlockFat32 *self, const char *path, struct fat_entry *ent) {
	uint32_t dir = 0;
	const char *name = NULL;
	size_t len = 0;
	block_fat32_ret_t ret = fat_path_parent(self, path, &dir, &name, &len);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (name == NULL) {
		fat_entry_root(self, ent);
		return BLOCK_FAT32_RET_OK;
	}
	return fat_dir_lookup(self, dir, name, len, ent);
}


static block_fat32_ret_t fat_sfn_exists(BlockFat32 *self, uint32_t dir, const uint8_t *sfn, bool *exists) {
	struct fat_scan s;
	fat_scan_init(&s, dir);
	struct fat_entry ent;
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	*exists = false;
	while ((ret = fat_scan_next(self, &s, NULL, 0, &ent)) == BLOCK_FAT32_RET_OK) {
		uint8_t *e = NULL;
		ret = fat_dir_entry(self, &ent.pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (memcmp(e, sfn, DE_NAME_LEN) == 0) {
			*exists = true;
			return BLOCK_FAT32_RET_OK;
		}
	}
	return (ret == BLOCK_FAT32_RET_EOF) ? BLOCK_FAT32_RET_OK : ret;
}


/* Generate an unique short name by appending a numeric tail "~N" to the basis. */
static block_fat32_ret_t fat_sfn_unique(BlockFat32 *self, uint32_t dir, uint8_t sfn[DE_NAME_LEN]) {
	size_t base_len = 0;
	while (base_len < DE_BASE_LEN && sfn[base_len] != ' ') {
		base_len++;
	}
	for (uint32_t n = 1; n < 1000000; n++) {
		char tail[DE_BASE_LEN + 1];
		size_t tail_len = (size_t)snprintf(tail, sizeof(tail), "~%lu", n);
		size_t keep = (base_len < DE_BASE_LEN - tail_len) ? base_len : DE_BASE_LEN - tail_len;
		memset(&sfn[keep], ' ', DE_BASE_LEN - keep);
		memcpy(&sfn[keep], tail, tail_len);

		bool exists = false;
		block_fat32_ret_t ret = fat_sfn_exists(self, dir, sfn, &exists);
		if (ret != BLOCK_FAT32_RET_OK || !exists) {
			return ret;
		}
	}
	return BLOCK_FAT32_RET_FULL;
}


/* Find @p count consecutive free entries in a directory, extending it if needed. Entries after the end marker are
 * free regardless of their content. @p after_end is set if the found run reaches past the end marker. */
static block_fat32_ret_t fat_dir_alloc(BlockFat32 *self, uint32_t dir, size_t count, struct fat_dir_pos *first,
                                       bool *after_end) {
	struct fat_dir_pos pos;
	fat_dir_pos_init(&pos, dir);
	size_t run = 0;
	bool end = false;
	while (true) {
		uint8_t *e = NULL;
		block_fat32_ret_t ret = fat_dir_entry(self, &pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		end = end || e[DE_NAME] == DE_END;
		if (end || e[DE_NAME] == DE_FREE) {
			if (run == 0) {
				*first = pos;
			}
			run++;
			if (run == count) {
				*after_end = end;
				return BLOCK_FAT32_RET_OK;
			}
		} else {
			run = 0;
		}
		ret = fat_dir_pos_next(self, &pos, true);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
	}
}


/* Add an entry set for @p name to a directory. The short name entry is initialized from @p tmpl (attributes,
 * timestamps, cluster and size), its name fields are replaced. */
static block_fat32_ret_t fat_dir_add(BlockFat32 *self, uint32_t dir, const char *name, size_t len,
                                     const uint8_t *tmpl, struct fat_entry *ent) {
	size_t units = 0;
	block_fat32_ret_t ret = fat_name_check(name, len, &units);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}

	uint8_t sfn[DIR_ENTRY_SIZE];
	memcpy(sfn, tmpl, DIR_ENTRY_SIZE);
	uint8_t ntres = 0;
	size_t lfn_count = 0;
	if (!fat_sfn_basis(name, len, &sfn[DE_NAME], &ntres)) {
		ntres = 0;
		lfn_count = (units + LFN_CHARS - 1) / LFN_CHARS;
		ret = fat_sfn_unique(self, dir, &sfn[DE_NAME]);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
	}
	sfn[DE_NTRES] = (uint8_t)((sfn[DE_NTRES] & ~(NTRES_LOWER_BASE | NTRES_LOWER_EXT)) | ntres);

	struct fat_dir_pos pos;
	bool after_end = false;
	ret = fat_dir_alloc(self, dir, lfn_count + 1, &pos, &after_end);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	ent->first = pos;

	/* Long name entries are stored in reverse order, the one with the end of the name first. */
	uint8_t sum = fat_sfn_checksum(sfn);
	for (size_t i = 0; i <= lfn_count; i++) {
		if (i > 0) {
			ret = fat_dir_pos_next(self, &pos, false);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
		}
		uint8_t *e = NULL;
		ret = fat_dir_entry(self, &pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (i < lfn_count) {
			fat_lfn_fill(e, name, len, (uint8_t)(lfn_count - i), i == 0, sum);
		} else {
			memcpy(e, sfn, DIR_ENTRY_SIZE);
		}
		self->cache_dirty = true;
	}

	/* The entry following the set past the end marker may contain garbage, terminate the directory there. */
	struct fat_dir_pos next = pos;
	if (after_end && fat_dir_pos_next(self, &next, false) == BLOCK_FAT32_RET_OK) {
		uint8_t *e = NULL;
		ret = fat_dir_entry(self, &next, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (e[DE_NAME] != DE_END) {
			e[DE_NAME] = DE_END;
			self->cache_dirty = true;
		}
	}

	ent->pos = pos;
	ent->lfn_count = (uint8_t)lfn_count;
	ent->attr = sfn[DE_ATTR];
	ent->dot = false;
	ent->cluster = ((uint32_t)ld16(&sfn[DE_CLUSTER_HI]) << 16) | ld16(&sfn[DE_CLUSTER_LO]);
	ent->size = ld32(&sfn[DE_SIZE]);
	return BLOCK_FAT32_RET_OK;
}


/* Mark all entries of an entry set as free. */
static block_fat32_ret_t fat_dir_remove(BlockFat32 *self, const struct fat_entry *ent) {
	struct fat_dir_pos pos = ent->first;
	for (size_t i = 0; i <= ent->lfn_count; i++) {
		block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
		if (i > 0) {
			ret = fat_dir_pos_next(self, &pos, false);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
		}
		uint8_t *e = NULL;
		ret = fat_dir_entry(self, &pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		e[DE_NAME] = DE_FREE;
		self->cache_dirty = true;
	}
	return BLOCK_FAT32_RET_OK;
}


static block_fat32_ret_t fat_dir_empty(BlockFat32 *self, uint32_t dir, bool *empty) {
	struct fat_scan s;
	fat_scan_init(&s, dir);
	struct fat_entry ent;
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	*empty = true;
	while ((ret = fat_scan_next(self, &s, NULL, 0, &ent)) == BLOCK_FAT32_RET_OK) {
		if (!ent.dot) {
			*empty = false;
			return BLOCK_FAT32_RET_OK;
		}
	}
	return (ret == BLOCK_FAT32_RET_EOF) ? BLOCK_FAT32_RET_OK : ret;
}


/* Set the cluster the ".." entry of a directory refers to (0 for the root directory). */
static block_fat32_ret_t fat_dir_set_parent(BlockFat32 *self, uint32_t dir, uint32_t parent) {
	struct fat_dir_pos pos = {.start = dir, .cluster = dir, .index = 1};
	uint8_t *e = NULL;
	block_fat32_ret_t ret = fat_dir_entry(self, &pos, &e);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (e[0] != '.' || e[1] != '.') {
		return BLOCK_FAT32_RET_CORRUPT;
	}
	parent = (parent == self->root_cluster) ? 0 : parent;
	st16(&e[DE_CLUSTER_HI], (uint16_t)(parent >> 16));
	st16(&e[DE_CLUSTER_LO], (uint16_t)parent);
	self->cache_dirty = true;
	return BLOCK_FAT32_RET_OK;
}


/* Check if directory @p dir is @p ancestor or is contained in it by walking the ".." entries up to the root. */
static block_fat32_ret_t fat_dir_within(BlockFat32 *self, uint32_t dir, uint32_t ancestor, bool *within) {
	*within = false;
	for (uint32_t i = 0; i < self->cluster_count; i++) {
		if (dir == ancestor) {
			*within = true;
			return BLOCK_FAT32_RET_OK;
		}
		if (dir == self->root_cluster) {
			return BLOCK_FAT32_RET_OK;
		}
		struct fat_dir_pos pos = {.start = dir, .cluster = dir, .index = 1};
		uint8_t *e = NULL;
		block_fat32_ret_t ret = fat_dir_entry(self, &pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (e[0] != '.' || e[1] != '.') {
			return BLOCK_FAT32_RET_CORRUPT;
		}
		uint32_t parent = ((uint32_t)ld16(&e[DE_CLUSTER_HI]) << 16) | ld16(&e[DE_CLUSTER_LO]);
		dir = (parent != 0) ? parent : self->root_cluster;
	}
	return BLOCK_FAT32_RET_CORRUPT;
}


/**********************************************************************************************************************
 * Timestamps
 **********************************************************************************************************************/

/* Current time in the FAT date and time format. FAT dates span 1980-2107, the time has a 2 second resolution. */
static void fat_timestamp(BlockFat32 *self, uint16_t *date, uint16_t *time) {
	*date = (1 << 5) | 1;
	*time = 0;

	struct timespec ts;
	if (self->conf.clock == NULL || self->conf.clock->vmt->get == NULL ||
	    self->conf.clock->vmt->get(self->conf.clock, &ts) != CLOCK_RET_OK) {
		return;
	}
	time_t t = ts.tv_sec;
	struct tm tm;
	if (gmtime_r(&t, &tm) == NULL || tm.tm_year < 80 || tm.tm_year > 80 + 127) {
		return;
	}
	int sec = (tm.tm_sec > 59) ? 59 : tm.tm_sec;
	*date = (uint16_t)(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
	*time = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (sec / 2));
}


/* Prepare a new short name entry (without a name) with the current time. */
static void fat_entry_template(BlockFat32 *self, uint8_t *e, uint8_t attr, uint32_t cluster) {
	uint16_t date = 0;
	uint16_t time = 0;
	fat_timestamp(self, &date, &time);
	memset(e, 0, DIR_ENTRY_SIZE);
	e[DE_ATTR] = attr;
	st16(&e[DE_CRT_TIME], time);
	st16(&e[DE_CRT_DATE], date);
	st16(&e[DE_ACC_DATE], date);
	st16(&e[DE_WRT_TIME], time);
	st16(&e[DE_WRT_DATE], date);
	st16(&e[DE_CLUSTER_HI], (uint16_t)(cluster >> 16));
	st16(&e[DE_CLUSTER_LO], (uint16_t)cluster);
}


/**********************************************************************************************************************
 * Files
 **********************************************************************************************************************/

static bool fat_file_is_open(BlockFat32 *self, const struct fat_dir_pos *entry, bool write) {
	for (struct block_fat32_file *f = self->files; f != NULL; f = f->next) {
		if (f->generation == self->generation && f->entry.start == entry->start && f->entry.index == entry->index &&
		    (write || (f->access & FILE_WRITE))) {
			return true;
		}
	}
	return false;
}


static block_fat32_ret_t fat_file_get(BlockFat32 *self, File *f, struct block_fat32_file **file) {
	*file = (struct block_fat32_file *)f->ptr;
	if (*file == NULL) {
		return BLOCK_FAT32_RET_NULL;
	}
	if ((*file)->generation != self->generation) {
		return BLOCK_FAT32_RET_IO;
	}
	return BLOCK_FAT32_RET_OK;
}


/* Get the @p index-th cluster of a file, allocating the missing clusters if @p alloc is set. The cluster chain is
 * walked from the last used cluster if possible. */
static block_fat32_ret_t fat_file_cluster(BlockFat32 *self, struct block_fat32_file *file, uint32_t index, bool alloc,
                                          uint32_t *cluster) {
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	if (file->start == 0) {
		if (!alloc) {
			return BLOCK_FAT32_RET_CORRUPT;
		}
		ret = fat_alloc(self, 0, &file->start);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		file->cur_cluster = 0;
		file->dirty = true;
	}
	if (file->cur_cluster == 0 || index < file->cur_index) {
		file->cur_cluster = file->start;
		file->cur_index = 0;
	}
	while (file->cur_index < index) {
		uint32_t next = 0;
		ret = fat_next(self, file->cur_cluster, &next);
		if (ret == BLOCK_FAT32_RET_EOF) {
			/* The file size says there is more data than the cluster chain holds. */
			if (!alloc) {
				return BLOCK_FAT32_RET_CORRUPT;
			}
			ret = fat_alloc(self, file->cur_cluster, &next);
		}
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		file->cur_cluster = next;
		file->cur_index++;
	}
	*cluster = file->cur_cluster;
	return BLOCK_FAT32_RET_OK;
}


/* Write the file size, first cluster and modification time to its directory entry. */
static block_fat32_ret_t fat_file_update(BlockFat32 *self, struct block_fat32_file *file) {
	uint16_t date = 0;
	uint16_t time = 0;
	fat_timestamp(self, &date, &time);
	uint8_t *e = NULL;
	block_fat32_ret_t ret = fat_dir_entry(self, &file->entry, &e);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	st16(&e[DE_CLUSTER_HI], (uint16_t)(file->start >> 16));
	st16(&e[DE_CLUSTER_LO], (uint16_t)file->start);
	st32(&e[DE_SIZE], file->size);
	st16(&e[DE_WRT_TIME], time);
	st16(&e[DE_WRT_DATE], date);
	st16(&e[DE_ACC_DATE], date);
	e[DE_ATTR] |= ATTR_ARCHIVE;
	self->cache_dirty = true;
	file->dirty = false;
	return BLOCK_FAT32_RET_OK;
}


static block_fat32_ret_t fat_file_flush(BlockFat32 *self, struct block_fat32_file *file) {
	if (file->dirty) {
		block_fat32_ret_t ret = fat_file_update(self, file);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
	}
	return fat_sync(self);
}


static void fat_file_release(BlockFat32 *self, struct block_fat32_file *file) {
	for (struct block_fat32_file **p = &self->files; *p != NULL; p = &(*p)->next) {
		if (*p == file) {
			*p = file->next;
			break;
		}
	}
	free(file);
}


static block_fat32_ret_t fat_open(BlockFat32 *self, File *f, const char *path, enum fs_mode mode) {
	uint8_t access = 0;
	if (mode & (FS_MODE_READONLY | FS_MODE_READWRITE)) {
		access |= FILE_READ;
	}
	if (mode & (FS_MODE_WRITEONLY | FS_MODE_READWRITE | FS_MODE_APPEND)) {
		access |= FILE_WRITE;
	}
	if (mode & FS_MODE_APPEND) {
		access |= FILE_APPEND;
	}
	if (access == 0 || ((mode & (FS_MODE_CREATE | FS_MODE_TRUNCATE)) && !(access & FILE_WRITE))) {
		return BLOCK_FAT32_RET_BAD_ARG;
	}

	uint32_t dir = 0;
	const char *name = NULL;
	size_t len = 0;
	block_fat32_ret_t ret = fat_path_parent(self, path, &dir, &name, &len);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (name == NULL) {
		return BLOCK_FAT32_RET_DENIED;
	}

	struct fat_entry ent;
	ret = fat_dir_lookup(self, dir, name, len, &ent);
	if (ret == BLOCK_FAT32_RET_NOT_FOUND && (mode & FS_MODE_CREATE)) {
		uint8_t tmpl[DIR_ENTRY_SIZE];
		fat_entry_template(self, tmpl, ATTR_ARCHIVE, 0);
		ret = fat_dir_add(self, dir, name, len, tmpl, &ent);
		if (ret == BLOCK_FAT32_RET_OK) {
			ret = fat_sync(self);
		}
	}
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if ((ent.attr & ATTR_DIRECTORY) || fat_entry_is_root(&ent) ||
	    ((access & FILE_WRITE) && (ent.attr & ATTR_READ_ONLY))) {
		return BLOCK_FAT32_RET_DENIED;
	}
	/* A file may be opened multiple times for reading but only once if it is written. */
	if (fat_file_is_open(self, &ent.pos, access & FILE_WRITE)) {
		return BLOCK_FAT32_RET_DENIED;
	}

	struct block_fat32_file *file = calloc(1, sizeof(struct block_fat32_file));
	if (file == NULL) {
		return BLOCK_FAT32_RET_NOMEM;
	}
	file->generation = self->generation;
	file->entry = ent.pos;
	file->start = ent.cluster;
	file->size = ent.size;
	file->access = access;

	if ((mode & FS_MODE_TRUNCATE) && (file->start != 0 || file->size != 0)) {
		if (file->start != 0) {
			ret = fat_free_chain(self, file->start);
		}
		file->start = 0;
		file->size = 0;
		file->dirty = true;
		if (ret == BLOCK_FAT32_RET_OK) {
			ret = fat_file_flush(self, file);
		}
		if (ret != BLOCK_FAT32_RET_OK) {
			free(file);
			return ret;
		}
	}

	file->next = self->files;
	self->files = file;
	f->handle = 0;
	f->ptr = file;
	return BLOCK_FAT32_RET_OK;
}


static block_fat32_ret_t fat_read(BlockFat32 *self, struct block_fat32_file *file, uint8_t *buf, size_t len,
                                  size_t *done) {
	*done = 0;
	if (!(file->access & FILE_READ)) {
		return BLOCK_FAT32_RET_DENIED;
	}
	if (file->pos >= file->size) {
		return BLOCK_FAT32_RET_EOF;
	}
	if (len > file->size - file->pos) {
		len = file->size - file->pos;
	}

	uint32_t cluster_bytes_shift = self->cluster_shift + SECTOR_SHIFT;
	while (len > 0) {
		uint32_t cluster = 0;
		block_fat32_ret_t ret = fat_file_cluster(self, file, file->pos >> cluster_bytes_shift, false, &cluster);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		uint32_t sector = (file->pos >> SECTOR_SHIFT) & ((1UL << self->cluster_shift) - 1);
		uint32_t lba = fat_cluster_lba(self, cluster) + sector;
		uint32_t offset = file->pos & (SECTOR_SIZE - 1);
		size_t chunk = 0;
		if (offset == 0 && len >= SECTOR_SIZE) {
			/* Whole sectors are read directly, up to the end of the cluster. */
			size_t sectors = len >> SECTOR_SHIFT;
			if (sectors > (1UL << self->cluster_shift) - sector) {
				sectors = (1UL << self->cluster_shift) - sector;
			}
			chunk = sectors << SECTOR_SHIFT;
			ret = fat_direct_read(self, lba, buf, sectors);
		} else {
			chunk = (len < SECTOR_SIZE - offset) ? len : SECTOR_SIZE - offset;
			ret = fat_cache_load(self, lba);
			if (ret == BLOCK_FAT32_RET_OK) {
				memcpy(buf, &self->cache[offset], chunk);
			}
		}
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		buf += chunk;
		len -= chunk;
		file->pos += chunk;
		*done += chunk;
	}
	return BLOCK_FAT32_RET_OK;
}


static block_fat32_ret_t fat_write(BlockFat32 *self, struct block_fat32_file *file, const uint8_t *buf, size_t len,
                                   size_t *done) {
	*done = 0;
	if (!(file->access & FILE_WRITE)) {
		return BLOCK_FAT32_RET_DENIED;
	}
	if (file->access & FILE_APPEND) {
		file->pos = file->size;
	}
	if (len == 0) {
		return BLOCK_FAT32_RET_OK;
	}
	/* FAT file size is limited to 4 GiB - 1. */
	if (len > UINT32_MAX - file->pos) {
		len = UINT32_MAX - file->pos;
		if (len == 0) {
			return BLOCK_FAT32_RET_FULL;
		}
	}

	uint32_t cluster_bytes_shift = self->cluster_shift + SECTOR_SHIFT;
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	while (len > 0) {
		uint32_t cluster = 0;
		ret = fat_file_cluster(self, file, file->pos >> cluster_bytes_shift, true, &cluster);
		if (ret != BLOCK_FAT32_RET_OK) {
			break;
		}
		uint32_t sector = (file->pos >> SECTOR_SHIFT) & ((1UL << self->cluster_shift) - 1);
		uint32_t lba = fat_cluster_lba(self, cluster) + sector;
		uint32_t offset = file->pos & (SECTOR_SIZE - 1);
		size_t chunk = 0;
		if (offset == 0 && len >= SECTOR_SIZE) {
			size_t sectors = len >> SECTOR_SHIFT;
			if (sectors > (1UL << self->cluster_shift) - sector) {
				sectors = (1UL << self->cluster_shift) - sector;
			}
			chunk = sectors << SECTOR_SHIFT;
			ret = fat_direct_write(self, lba, buf, sectors);
		} else {
			chunk = (len < SECTOR_SIZE - offset) ? len : SECTOR_SIZE - offset;
			/* A sector entirely past the end of the file holds no data, it does not need to be read. */
			if ((file->pos & ~(uint32_t)(SECTOR_SIZE - 1)) >= file->size) {
				ret = fat_cache_zero(self, lba);
			} else {
				ret = fat_cache_load(self, lba);
			}
			if (ret == BLOCK_FAT32_RET_OK) {
				memcpy(&self->cache[offset], buf, chunk);
				self->cache_dirty = true;
			}
		}
		if (ret != BLOCK_FAT32_RET_OK) {
			break;
		}
		buf += chunk;
		len -= chunk;
		file->pos += chunk;
		*done += chunk;
		if (file->pos > file->size) {
			file->size = file->pos;
		}
		file->dirty = true;
	}
	/* A partial write is reported as successful, the caller sees the written length. */
	return (*done > 0) ? BLOCK_FAT32_RET_OK : ret;
}


static block_fat32_ret_t fat_seek(struct block_fat32_file *file, size_t offset, enum fs_seek whence) {
	uint64_t pos = 0;
	switch (whence) {
		case FS_SEEK_SET:
			pos = offset;
			break;
		case FS_SEEK_CUR:
			pos = (uint64_t)file->pos + offset;
			break;
		case FS_SEEK_END:
			pos = (uint64_t)file->size + offset;
			break;
		default:
			return BLOCK_FAT32_RET_BAD_ARG;
	}
	/* Seeking past the end of the file is not supported, there are no sparse files on FAT. */
	if (pos > file->size) {
		return BLOCK_FAT32_RET_BAD_ARG;
	}
	file->pos = (uint32_t)pos;
	return BLOCK_FAT32_RET_OK;
}


/* Remove an entry set and free its clusters. Directories must be empty. */
static block_fat32_ret_t fat_remove_entry(BlockFat32 *self, const struct fat_entry *ent) {
	if (fat_entry_is_root(ent) || ent->dot) {
		return BLOCK_FAT32_RET_DENIED;
	}
	block_fat32_ret_t ret = BLOCK_FAT32_RET_OK;
	if (ent->attr & ATTR_DIRECTORY) {
		bool empty = false;
		ret = fat_dir_empty(self, fat_entry_dir_cluster(self, ent), &empty);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (!empty) {
			return BLOCK_FAT32_RET_DENIED;
		}
	}
	ret = fat_dir_remove(self, ent);
	if (ret == BLOCK_FAT32_RET_OK && ent->cluster != 0) {
		ret = fat_free_chain(self, ent->cluster);
	}
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	return fat_sync(self);
}


static block_fat32_ret_t fat_remove(BlockFat32 *self, const char *path) {
	struct fat_entry ent;
	block_fat32_ret_t ret = fat_path_lookup(self, path, &ent);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (fat_file_is_open(self, &ent.pos, true)) {
		return BLOCK_FAT32_RET_DENIED;
	}
	return fat_remove_entry(self, &ent);
}


static block_fat32_ret_t fat_rename(BlockFat32 *self, const char *old_path, const char *new_path) {
	struct fat_entry old;
	block_fat32_ret_t ret = fat_path_lookup(self, old_path, &old);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (fat_entry_is_root(&old) || old.dot || fat_file_is_open(self, &old.pos, true)) {
		return BLOCK_FAT32_RET_DENIED;
	}

	uint32_t dir = 0;
	const char *name = NULL;
	size_t len = 0;
	ret = fat_path_parent(self, new_path, &dir, &name, &len);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (name == NULL || fat_is_dot(name, len)) {
		return BLOCK_FAT32_RET_EXISTS;
	}
	/* The new name may only match the old entry itself (a change of letter case). */
	struct fat_entry existing;
	ret = fat_dir_lookup(self, dir, name, len, &existing);
	if (ret == BLOCK_FAT32_RET_OK) {
		if (existing.pos.start != old.pos.start || existing.pos.index != old.pos.index) {
			return BLOCK_FAT32_RET_EXISTS;
		}
	} else if (ret != BLOCK_FAT32_RET_NOT_FOUND) {
		return ret;
	}

	bool moved_dir = (old.attr & ATTR_DIRECTORY) && dir != old.pos.start;
	if (moved_dir) {
		bool within = false;
		ret = fat_dir_within(self, dir, fat_entry_dir_cluster(self, &old), &within);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (within) {
			return BLOCK_FAT32_RET_DENIED;
		}
	}

	/* The new entry set is created before the old one is removed, an interrupted rename leaves both. */
	uint8_t *e = NULL;
	ret = fat_dir_entry(self, &old.pos, &e);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	uint8_t tmpl[DIR_ENTRY_SIZE];
	memcpy(tmpl, e, DIR_ENTRY_SIZE);
	struct fat_entry new_ent;
	ret = fat_dir_add(self, dir, name, len, tmpl, &new_ent);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dir_remove(self, &old);
	}
	if (ret == BLOCK_FAT32_RET_OK && moved_dir) {
		ret = fat_dir_set_parent(self, fat_entry_dir_cluster(self, &old), dir);
	}
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	return fat_sync(self);
}


static block_fat32_ret_t fat_mkdir(BlockFat32 *self, const char *path) {
	uint32_t dir = 0;
	const char *name = NULL;
	size_t len = 0;
	block_fat32_ret_t ret = fat_path_parent(self, path, &dir, &name, &len);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (name == NULL || fat_is_dot(name, len)) {
		return BLOCK_FAT32_RET_EXISTS;
	}
	struct fat_entry ent;
	ret = fat_dir_lookup(self, dir, name, len, &ent);
	if (ret == BLOCK_FAT32_RET_OK) {
		return BLOCK_FAT32_RET_EXISTS;
	}
	if (ret != BLOCK_FAT32_RET_NOT_FOUND) {
		return ret;
	}

	/* Prepare the new directory with its "." and ".." entries first, then link it to the parent. */
	uint32_t cluster = 0;
	ret = fat_alloc(self, 0, &cluster);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	ret = fat_zero_cluster(self, cluster);
	uint8_t tmpl[DIR_ENTRY_SIZE];
	fat_entry_template(self, tmpl, ATTR_DIRECTORY, cluster);
	for (uint32_t i = 0; i < 2 && ret == BLOCK_FAT32_RET_OK; i++) {
		struct fat_dir_pos pos = {.start = cluster, .cluster = cluster, .index = i};
		uint8_t *e = NULL;
		ret = fat_dir_entry(self, &pos, &e);
		if (ret == BLOCK_FAT32_RET_OK) {
			memcpy(e, tmpl, DIR_ENTRY_SIZE);
			memset(&e[DE_NAME], ' ', DE_NAME_LEN);
			memset(&e[DE_NAME], '.', i + 1);
			self->cache_dirty = true;
		}
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dir_set_parent(self, cluster, dir);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dir_add(self, dir, name, len, tmpl, &ent);
	}
	if (ret != BLOCK_FAT32_RET_OK) {
		fat_free_chain(self, cluster);
		return ret;
	}
	return fat_sync(self);
}


/* Copy the volume label stored in the root directory. Characters outside of ASCII are replaced with '_' as their
 * codepage is unknown, trailing spaces are removed. An empty string is returned if the volume has no label. */
static block_fat32_ret_t fat_volume_label(BlockFat32 *self, char *buf, size_t size) {
	if (buf == NULL || size == 0) {
		return BLOCK_FAT32_RET_OK;
	}
	buf[0] = '\0';
	struct fat_dir_pos pos;
	fat_dir_pos_init(&pos, self->root_cluster);
	while (true) {
		uint8_t *e = NULL;
		block_fat32_ret_t ret = fat_dir_entry(self, &pos, &e);
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
		if (e[DE_NAME] == DE_END) {
			return BLOCK_FAT32_RET_OK;
		}
		if (e[DE_NAME] != DE_FREE && (e[DE_ATTR] & ATTR_LFN_MASK) != ATTR_LFN &&
		    (e[DE_ATTR] & (ATTR_VOLUME_ID | ATTR_DIRECTORY)) == ATTR_VOLUME_ID) {
			size_t len = DE_NAME_LEN;
			while (len > 0 && e[DE_NAME + len - 1] == ' ') {
				len--;
			}
			size_t n = 0;
			for (; n < len && n + 1 < size; n++) {
				uint8_t c = e[DE_NAME + n];
				buf[n] = (c >= 0x20 && c < 0x80) ? (char)c : '_';
			}
			buf[n] = '\0';
			return BLOCK_FAT32_RET_OK;
		}
		ret = fat_dir_pos_next(self, &pos, false);
		if (ret == BLOCK_FAT32_RET_EOF || ret == BLOCK_FAT32_RET_FULL) {
			return BLOCK_FAT32_RET_OK;
		}
		if (ret != BLOCK_FAT32_RET_OK) {
			return ret;
		}
	}
}


/* Free clusters are counted by scanning the whole FAT once after mount, the FSInfo free count is not trusted.
 * Allocations and frees keep the count up to date afterwards. */
static block_fat32_ret_t fat_info(BlockFat32 *self, struct fs_info *info) {
	if (!self->free_count_valid) {
		uint32_t free_clusters = 0;
		for (uint32_t s = 0; s < self->fat_sectors; s++) {
			block_fat32_ret_t ret = fat_cache_load(self, self->fat_lba + s);
			if (ret != BLOCK_FAT32_RET_OK) {
				return ret;
			}
			for (uint32_t i = 0; i < FAT_ENTRIES_PER_SECTOR; i++) {
				uint32_t cluster = s * FAT_ENTRIES_PER_SECTOR + i;
				if (fat_cluster_valid(self, cluster) && (ld32(&self->cache[i * 4]) & FAT_ENTRY_MASK) == FAT_FREE) {
					free_clusters++;
				}
			}
		}
		self->free_count = free_clusters;
		self->free_count_valid = true;
	}
	uint32_t shift = self->cluster_shift + SECTOR_SHIFT;
	info->size_total = (uint64_t)self->cluster_count << shift;
	info->size_used = (uint64_t)(self->cluster_count - self->free_count) << shift;
	return fat_volume_label(self, info->label, info->label_size);
}


/**********************************************************************************************************************
 * Fs interface implementation
 **********************************************************************************************************************/

static fs_ret_t fat_fs_ret(block_fat32_ret_t ret) {
	switch (ret) {
		case BLOCK_FAT32_RET_OK:
			return FS_RET_OK;
		case BLOCK_FAT32_RET_NULL:
			return FS_RET_NULL;
		case BLOCK_FAT32_RET_EOF:
			return FS_RET_EOF;
		default:
			return FS_RET_FAILED;
	}
}


/* Lock the filesystem and mount the volume if needed. */
static block_fat32_ret_t fat_begin(BlockFat32 *self) {
	xSemaphoreTake(self->lock, portMAX_DELAY);
	return fat_ready(self);
}


static fs_ret_t fat_end(BlockFat32 *self, block_fat32_ret_t ret) {
	xSemaphoreGive(self->lock);
	return fat_fs_ret(ret);
}


static fs_ret_t block_fat32_open(Fs *fs, File *f, const char *path, enum fs_mode mode) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL || path == NULL) {
		return FS_RET_NULL;
	}
	f->ptr = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_open(self, f, path, mode);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_read(Fs *fs, File *f, void *buf, size_t len, size_t *read) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL || buf == NULL || read == NULL) {
		return FS_RET_NULL;
	}
	*read = 0;
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_read(self, file, buf, len, read);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_write(Fs *fs, File *f, const void *buf, size_t len, size_t *written) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL || buf == NULL || written == NULL) {
		return FS_RET_NULL;
	}
	*written = 0;
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_write(self, file, buf, len, written);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_lseek(Fs *fs, File *f, size_t offset, enum fs_seek whence) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL) {
		return FS_RET_NULL;
	}
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_seek(file, offset, whence);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_remove(Fs *fs, const char *path) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (path == NULL) {
		return FS_RET_NULL;
	}
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_remove(self, path);
	}
	return fat_end(self, ret);
}


/* Remove an open file. The handle is closed. */
static fs_ret_t block_fat32_fremove(Fs *fs, File *f) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL) {
		return FS_RET_NULL;
	}
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	struct fat_entry ent;
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_entry_at(self, &file->entry, &ent);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		/* The entry may not be updated with the file state yet, free the chain the handle knows about. */
		ent.cluster = file->start;
		ret = fat_remove_entry(self, &ent);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		fat_file_release(self, file);
		f->ptr = NULL;
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_stat(Fs *fs, const char *path, struct fs_stat *s) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (path == NULL || s == NULL) {
		return FS_RET_NULL;
	}
	struct fat_entry ent;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_path_lookup(self, path, &ent);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		s->handle = 0;
		s->size = ent.size;
		ret = fat_entry_name(self, &ent, s->name, s->name_size);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_fstat(Fs *fs, File *f, struct fs_stat *s) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL || s == NULL) {
		return FS_RET_NULL;
	}
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	struct fat_entry ent;
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_entry_at(self, &file->entry, &ent);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		s->handle = 0;
		s->size = file->size;
		ret = fat_entry_name(self, &ent, s->name, s->name_size);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_fflush(Fs *fs, File *f) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL) {
		return FS_RET_NULL;
	}
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_flush(self, file);
	}
	return fat_end(self, ret);
}


/* The handle is always released, even if the volume was unmounted or the final flush failed. */
static fs_ret_t block_fat32_close(Fs *fs, File *f) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (f == NULL || f->ptr == NULL) {
		return FS_RET_NULL;
	}
	struct block_fat32_file *file = NULL;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_file_get(self, f, &file);
	}
	if (ret == BLOCK_FAT32_RET_OK && (file->access & FILE_WRITE)) {
		ret = fat_file_flush(self, file);
	}
	fat_file_release(self, (struct block_fat32_file *)f->ptr);
	f->ptr = NULL;
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_rename(Fs *fs, const char *old_path, const char *new_path) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (old_path == NULL || new_path == NULL) {
		return FS_RET_NULL;
	}
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_rename(self, old_path, new_path);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_info(Fs *fs, struct fs_info *info) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (info == NULL) {
		return FS_RET_NULL;
	}
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_info(self, info);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_opendir(Fs *fs, Dir *d, const char *path) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (d == NULL || path == NULL) {
		return FS_RET_NULL;
	}
	d->ptr = NULL;
	struct fat_entry ent;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_path_lookup(self, path, &ent);
	}
	if (ret == BLOCK_FAT32_RET_OK && !(ent.attr & ATTR_DIRECTORY)) {
		ret = BLOCK_FAT32_RET_NOT_FOUND;
	}
	struct block_fat32_dir *dir = NULL;
	if (ret == BLOCK_FAT32_RET_OK) {
		dir = malloc(sizeof(struct block_fat32_dir));
		if (dir == NULL) {
			ret = BLOCK_FAT32_RET_NOMEM;
		}
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		dir->generation = self->generation;
		fat_scan_init(&dir->scan, fat_entry_dir_cluster(self, &ent));
		d->ptr = dir;
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_closedir(Fs *fs, Dir *d) {
	(void)fs;
	if (d == NULL || d->ptr == NULL) {
		return FS_RET_NULL;
	}
	free(d->ptr);
	d->ptr = NULL;
	return FS_RET_OK;
}


/* Return the next directory entry. The "." and ".." entries are skipped. Returns FS_RET_EOF at the end. */
static fs_ret_t block_fat32_readdir(Fs *fs, Dir *d, DirEntry *e) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (d == NULL || d->ptr == NULL || e == NULL) {
		return FS_RET_NULL;
	}
	struct block_fat32_dir *dir = (struct block_fat32_dir *)d->ptr;
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK && dir->generation != self->generation) {
		ret = BLOCK_FAT32_RET_IO;
	}
	struct fat_entry ent;
	while (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_scan_next(self, &dir->scan, NULL, 0, &ent);
		if (ret == BLOCK_FAT32_RET_OK && !ent.dot) {
			break;
		}
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		e->handle = 0;
		e->ptr = NULL;
		e->size = ent.size;
		ret = fat_entry_name(self, &ent, e->name, e->name_size);
	}
	return fat_end(self, ret);
}


static fs_ret_t block_fat32_mkdir(Fs *fs, const char *path) {
	BlockFat32 *self = (BlockFat32 *)fs->parent;
	if (path == NULL) {
		return FS_RET_NULL;
	}
	block_fat32_ret_t ret = fat_begin(self);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_mkdir(self, path);
	}
	return fat_end(self, ret);
}


static const struct fs_vmt block_fat32_fs_vmt = {
	.open = block_fat32_open,
	.read = block_fat32_read,
	.write = block_fat32_write,
	.lseek = block_fat32_lseek,
	.remove = block_fat32_remove,
	.fremove = block_fat32_fremove,
	.stat = block_fat32_stat,
	.fstat = block_fat32_fstat,
	.fflush = block_fat32_fflush,
	.close = block_fat32_close,
	.rename = block_fat32_rename,
	.info = block_fat32_info,
	.opendir = block_fat32_opendir,
	.closedir = block_fat32_closedir,
	.readdir = block_fat32_readdir,
	.mkdir = block_fat32_mkdir,
};


/**********************************************************************************************************************
 * Formatting
 **********************************************************************************************************************/

/* Cluster size by partition size as commonly used for FAT32 (512 B up to 260 MB, 4 KiB up to 8 GiB, 8 KiB up to
 * 16 GiB, 16 KiB up to 32 GiB and 32 KiB above). */
static uint8_t fat_format_cluster_sectors(uint32_t sectors) {
	if (sectors <= 532480UL) {
		return 1;
	} else if (sectors <= 16777216UL) {
		return 8;
	} else if (sectors <= 33554432UL) {
		return 16;
	} else if (sectors <= 67108864UL) {
		return 32;
	}
	return 64;
}


static void fat_format_boot_sector(uint8_t *b, uint32_t part_lba, uint32_t part_sectors, uint8_t spc,
                                   uint16_t reserved, uint32_t fat_sectors, uint32_t volume_id) {
	memset(b, 0, SECTOR_SIZE);
	memcpy(&b[0], "\xeb\x58\x90" "MSWIN4.1", 11);
	st16(&b[BPB_BYTES_PER_SECTOR], SECTOR_SIZE);
	b[BPB_SECTORS_PER_CLUSTER] = spc;
	st16(&b[BPB_RESERVED_SECTORS], reserved);
	b[BPB_FAT_COUNT] = FORMAT_FAT_COUNT;
	/* Media descriptor, CHS geometry (sectors per track, heads) and hidden sectors */
	b[21] = FORMAT_MEDIA;
	st16(&b[24], 63);
	st16(&b[26], 255);
	st32(&b[28], part_lba);
	st32(&b[BPB_TOTAL_SECTORS32], part_sectors);
	st32(&b[BPB_FAT_SECTORS32], fat_sectors);
	st32(&b[BPB_ROOT_CLUSTER], FAT_FIRST_CLUSTER);
	st16(&b[BPB_FSINFO_SECTOR], FORMAT_FSINFO_SECTOR);
	st16(&b[50], FORMAT_BACKUP_BOOT_SECTOR);
	/* Drive number, extended boot signature, volume ID, label and filesystem type */
	b[64] = 0x80;
	b[66] = 0x29;
	st32(&b[67], volume_id);
	memcpy(&b[71], "NO NAME    " "FAT32   ", 19);
	st16(&b[BPB_SIGNATURE], 0xaa55);
}


static block_fat32_ret_t fat_format(BlockFat32 *self) {
	size_t block_size = 0;
	uint32_t block_count = 0;
	block_fat32_ret_t ret = fat_dev_ret(self,
		self->conf.block->vmt->get_geometry(self->conf.block, &block_size, &block_count));
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}
	if (block_size != SECTOR_SIZE || block_count <= FORMAT_PART_LBA) {
		return BLOCK_FAT32_RET_BAD_ARG;
	}

	/* Size the FAT for the whole partition (the result is slightly larger than needed), then grow the reserved
	 * area to align the data area to the cluster size. */
	uint32_t part_sectors = block_count - FORMAT_PART_LBA;
	uint8_t spc = fat_format_cluster_sectors(part_sectors);
	uint32_t fat_sectors = (part_sectors - FORMAT_RESERVED_SECTORS + 128UL * spc) / (128UL * spc + 1);
	uint32_t reserved = FORMAT_RESERVED_SECTORS;
	uint32_t data_lba = FORMAT_PART_LBA + reserved + FORMAT_FAT_COUNT * fat_sectors;
	reserved += (spc - data_lba % spc) % spc;
	data_lba = FORMAT_PART_LBA + reserved + FORMAT_FAT_COUNT * fat_sectors;
	if (data_lba - FORMAT_PART_LBA >= part_sectors) {
		return BLOCK_FAT32_RET_BAD_ARG;
	}
	uint32_t clusters = (block_count - data_lba) / spc;
	if (clusters < FAT_CLUSTERS_MIN || clusters + FAT_FIRST_CLUSTER > fat_sectors * FAT_ENTRIES_PER_SECTOR) {
		return BLOCK_FAT32_RET_BAD_ARG;
	}

	/* Zero the reserved area, both FATs and the root directory cluster. The old boot sector is cleared first, an
	 * interrupted format therefore never leaves a valid looking volume. */
	uint8_t *zero = calloc(FORMAT_ZERO_SECTORS, SECTOR_SIZE);
	if (zero == NULL) {
		return BLOCK_FAT32_RET_NOMEM;
	}
	for (uint32_t lba = FORMAT_PART_LBA; lba < data_lba + spc && ret == BLOCK_FAT32_RET_OK; ) {
		uint32_t n = data_lba + spc - lba;
		if (n > FORMAT_ZERO_SECTORS) {
			n = FORMAT_ZERO_SECTORS;
		}
		ret = fat_dev_write(self, lba, zero, n);
		lba += n;
	}
	free(zero);
	if (ret != BLOCK_FAT32_RET_OK) {
		return ret;
	}

	/* The sector cache is used as a scratch buffer, it is not valid while unmounted. */
	uint8_t *b = self->cache;
	self->cache_valid = false;

	/* FAT entries 0 and 1 are reserved, entry 2 terminates the root directory. */
	memset(b, 0, SECTOR_SIZE);
	st32(&b[0], 0x0fffff00UL | FORMAT_MEDIA);
	st32(&b[4], FAT_EOC);
	st32(&b[8], FAT_EOC);
	for (uint32_t i = 0; i < FORMAT_FAT_COUNT && ret == BLOCK_FAT32_RET_OK; i++) {
		ret = fat_dev_write(self, FORMAT_PART_LBA + reserved + i * fat_sectors, b, 1);
	}

	/* FSInfo and its backup. The root directory uses the first cluster. */
	memset(b, 0, SECTOR_SIZE);
	st32(&b[FSINFO_LEAD_SIG], FSINFO_LEAD_SIG_VALUE);
	st32(&b[FSINFO_STRUC_SIG], FSINFO_STRUC_SIG_VALUE);
	st32(&b[FSINFO_FREE_COUNT], clusters - 1);
	st32(&b[FSINFO_NEXT_FREE], FAT_FIRST_CLUSTER + 1);
	st32(&b[FSINFO_TRAIL_SIG], FSINFO_TRAIL_SIG_VALUE);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dev_write(self, FORMAT_PART_LBA + FORMAT_FSINFO_SECTOR, b, 1);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dev_write(self, FORMAT_PART_LBA + FORMAT_BACKUP_BOOT_SECTOR + FORMAT_FSINFO_SECTOR, b, 1);
	}

	/* Use the time as the volume serial number if available. */
	uint16_t date = 0;
	uint16_t time = 0;
	fat_timestamp(self, &date, &time);
	uint32_t volume_id = ((uint32_t)date << 16) ^ time ^ (uint32_t)xTaskGetTickCount();

	/* The backup boot sector, the partition table and the boot sector itself as the last one. */
	fat_format_boot_sector(b, FORMAT_PART_LBA, part_sectors, spc, (uint16_t)reserved, fat_sectors, volume_id);
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dev_write(self, FORMAT_PART_LBA + FORMAT_BACKUP_BOOT_SECTOR, b, 1);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		memset(b, 0, SECTOR_SIZE);
		uint8_t *p = &b[MBR_PARTITIONS];
		/* CHS addresses are not used, they are set to the "LBA only" marker. */
		memcpy(&p[1], "\xfe\xff\xff", 3);
		p[MBR_PARTITION_TYPE] = MBR_TYPE_FAT32_LBA;
		memcpy(&p[5], "\xfe\xff\xff", 3);
		st32(&p[MBR_PARTITION_LBA], FORMAT_PART_LBA);
		st32(&p[MBR_PARTITION_SECTORS], part_sectors);
		st16(&b[BPB_SIGNATURE], 0xaa55);
		ret = fat_dev_write(self, 0, b, 1);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		fat_format_boot_sector(b, FORMAT_PART_LBA, part_sectors, spc, (uint16_t)reserved, fat_sectors, volume_id);
		ret = fat_dev_write(self, FORMAT_PART_LBA, b, 1);
	}
	if (ret == BLOCK_FAT32_RET_OK) {
		ret = fat_dev_ret(self, self->conf.block->vmt->flush(self->conf.block));
	}
	return ret;
}


/**********************************************************************************************************************
 * Service API
 **********************************************************************************************************************/

block_fat32_ret_t block_fat32_init(BlockFat32 *self, const struct block_fat32_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL) ||
	    u_assert(conf->block != NULL)) {
		return BLOCK_FAT32_RET_NULL;
	}
	memset(self, 0, sizeof(BlockFat32));
	memcpy(&self->conf, conf, sizeof(struct block_fat32_conf));

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return BLOCK_FAT32_RET_NOMEM;
	}

	self->fs.vmt = &block_fat32_fs_vmt;
	self->fs.parent = self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return BLOCK_FAT32_RET_OK;
}


/* All files must be closed before. Unwritten data of open files is lost. */
block_fat32_ret_t block_fat32_free(BlockFat32 *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->lock != NULL)) {
		return BLOCK_FAT32_RET_NULL;
	}
	if (self->files != NULL) {
		return BLOCK_FAT32_RET_FAILED;
	}
	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->mounted) {
		fat_sync(self);
	}
	fat_unmount(self);
	xSemaphoreGive(self->lock);
	vSemaphoreDelete(self->lock);
	self->lock = NULL;
	return BLOCK_FAT32_RET_OK;
}


block_fat32_ret_t block_fat32_format(BlockFat32 *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->lock != NULL)) {
		return BLOCK_FAT32_RET_NULL;
	}
	xSemaphoreTake(self->lock, portMAX_DELAY);
	block_fat32_ret_t ret = BLOCK_FAT32_RET_DENIED;
	if (self->files == NULL) {
		/* Stale handles of the previous volume must not match the new one. */
		fat_unmount(self);
		self->mount_failed = false;
		self->generation++;
		ret = fat_format(self);
		if (ret == BLOCK_FAT32_RET_OK) {
			ret = fat_ready(self);
		}
	}
	xSemaphoreGive(self->lock);

	if (ret == BLOCK_FAT32_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("formatted"));
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("format failed, ret = %d"), ret);
	}
	return ret;
}
