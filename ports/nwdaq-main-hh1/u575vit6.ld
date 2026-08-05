/* RAM memory origin is adjusted for the circular system log placed at the start of SRAM1. */

MEMORY
{
	rom (rx) : ORIGIN = LOAD_ADDRESS, LENGTH = 1024K
	ram (rwx) : ORIGIN = 0x20000000 + 2K, LENGTH = 192K - 2K
}

INCLUDE cortex-m33.ld
