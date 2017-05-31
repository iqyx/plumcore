/* ROM origin and size is adjusted for uBLoad usage (32K offset + header offset) */
/* RAM memory origin is adjusted for circular system log initialized by the bootloader */

MEMORY
{
	rom (rx) : ORIGIN = 0x08000000 + 0x10400, LENGTH = 256K - 0x10400
	ram (rwx) : ORIGIN = 0x20000000 + 2K, LENGTH = 64K - 2K
}

INCLUDE libopencm3_stm32f4.ld


