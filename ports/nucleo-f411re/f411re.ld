/* ROM origin and size is adjusted for uBLoad usage (32K offset + header offset) */
/* RAM memory origin is adjusted for circular system log initialized by the bootloader */

MEMORY
{
	rom (rx) : ORIGIN = 0x08000000, LENGTH = 512K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

INCLUDE libopencm3_stm32f4.ld


