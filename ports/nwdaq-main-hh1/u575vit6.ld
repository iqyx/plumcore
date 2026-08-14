/* RAM memory origin is adjusted for the circular system log placed at the start of SRAM1.
 * SRAM1 (192K), SRAM2 (64K) and SRAM3 (512K) are contiguous from 0x20000000, forming a single
 * 768K block; SRAM4 lives in a separate domain and is not included here. */

MEMORY
{
	rom (rx) : ORIGIN = LOAD_ADDRESS, LENGTH = 1024K
	ram (rwx) : ORIGIN = 0x20000000 + 2K, LENGTH = 768K - 2K
}

INCLUDE cortex-m33.ld
