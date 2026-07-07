/* ROM origin and size is adjusted for uBLoad usage (32K offset + header offset) */
/* RAM memory origin is adjusted for circular system log initialized by the bootloader */

/* STM32G491CCU6 has 112 KiB of contiguous SRAM (SRAM1 80K + SRAM2 16K + CCM 16K, the CCM being
 * aliased right after SRAM2 at 0x20018000), i.e. 0x20000000 - 0x2001C000. Expose all of it; the
 * bottom 2 KiB is still reserved for the bootloader-initialized circular system log. */
MEMORY
{
	rom (rx) : ORIGIN = LOAD_ADDRESS, LENGTH = 256K
	ram (rwx) : ORIGIN = 0x20000000 + 2K, LENGTH = 112K - 2K
}

INCLUDE cortex-m4f.ld


