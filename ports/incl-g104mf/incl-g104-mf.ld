/* ROM origin and size is adjusted for uBLoad usage (32K offset + header offset) */
/* RAM memory origin is adjusted for circular system log initialized by the bootloader */

MEMORY
{
	rom (rx) : ORIGIN = LOAD_ADDRESS, LENGTH = 256K
	ram (rwx) : ORIGIN = 0x20000000 + 2K, LENGTH = 160K - 2K
}

INCLUDE cortex-m-generic.ld


