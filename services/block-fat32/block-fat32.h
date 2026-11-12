/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FAT32 filesystem on a block device
 *
 * Copyright (c) 2025, Bjørn Brodtkorb
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/block.h>
#include <interfaces/clock.h>
#include <interfaces/fs.h>


typedef enum {
	BLOCK_FAT32_RET_OK = 0,
	BLOCK_FAT32_RET_FAILED,
	BLOCK_FAT32_RET_NULL,
	BLOCK_FAT32_RET_NOMEM,
	BLOCK_FAT32_RET_IO,
	BLOCK_FAT32_RET_NO_FS,
	BLOCK_FAT32_RET_CORRUPT,
	BLOCK_FAT32_RET_BAD_ARG,
	BLOCK_FAT32_RET_NOT_FOUND,
	BLOCK_FAT32_RET_EXISTS,
	BLOCK_FAT32_RET_DENIED,
	BLOCK_FAT32_RET_FULL,
	BLOCK_FAT32_RET_EOF,
} block_fat32_ret_t;

/* Service configuration passed to block_fat32_init(). */
struct block_fat32_conf {
	/** Block device holding the filesystem. It must have 512 byte blocks. */
	Block *block;
	/** Wall clock used to timestamp files. NULL to use a fixed time (1980-01-01 00:00:00). */
	Clock *clock;
};

/* Open file state, private to the service. */
struct block_fat32_file;

typedef struct {
	struct block_fat32_conf conf;

	/* Filesystem interface of the volume. */
	Fs fs;

	/* Serializes all filesystem operations. */
	SemaphoreHandle_t lock;

	/* Volume geometry, valid while mounted. */
	bool mounted;
	/* No valid volume was found on the current media. Mounting is not retried until the media changes. */
	bool mount_failed;
	/* Incremented on every mount. File and directory handles opened during a previous mount are stale. */
	uint32_t generation;
	/* Log2 of the number of sectors per cluster. */
	uint8_t cluster_shift;
	/* Number of FAT copies kept in sync, 1 if FAT mirroring is disabled. */
	uint8_t fat_count;
	uint32_t fat_lba;
	uint32_t fat_sectors;
	uint32_t data_lba;
	uint32_t cluster_count;
	uint32_t root_cluster;
	/* FSInfo sector, 0 if the volume has none. */
	uint32_t fsinfo_lba;
	/* The FSInfo free cluster hints are invalidated on the first modification after mount. */
	bool fsinfo_invalidated;
	/* Cluster to start the next free cluster search from. */
	uint32_t free_hint;
	/* Number of free clusters, counted on the first info() call and kept up to date afterwards. */
	uint32_t free_count;
	bool free_count_valid;

	/* Single sector cache shared by all operations. */
	uint32_t cache_lba;
	bool cache_valid;
	bool cache_dirty;
	uint8_t cache[512];

	/* List of open files. */
	struct block_fat32_file *files;
} BlockFat32;


/**
 * @brief Initialise the FAT32 filesystem service
 *
 * The volume is mounted on the first filesystem access. It is either the first FAT32 partition of a MBR partition
 * table or a partitionless volume starting at the first block. If the block device reports a media change or
 * removal, the volume is unmounted and mounted again on the next access. Handles opened before are stale.
 */
block_fat32_ret_t block_fat32_init(BlockFat32 *self, const struct block_fat32_conf *conf);
block_fat32_ret_t block_fat32_free(BlockFat32 *self);

/**
 * @brief Format the whole block device with a FAT32 filesystem
 *
 * A MBR partition table with a single FAT32 partition starting at 4 MiB is created, replacing any previous content
 * of the device. The cluster size is selected according to the partition size. The device must be at least about
 * 40 MB large. All files must be closed. The new volume is mounted afterwards.
 */
block_fat32_ret_t block_fat32_format(BlockFat32 *self);
