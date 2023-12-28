==========================================
On position independent firmware images
==========================================

Compiling firmware images as a position independent code is *hard* as stated by various sources, eg.
https://mcuoneclipse.com/2021/06/05/position-independent-code-with-gcc-for-arm-cortex-m/ or
https://www.eevblog.com/forum/microcontrollers/bare-metal-arm-gcc-position-independent-code-possible/.
Some even claim they are the only on the Earth to have solved the issue - but many things still don't work -
https://techblog.paalijarvi.fi/2022/01/16/portable-position-independent-code-pic-bootloader-and-firmware-for-arm-cortex-m0-and-cortex-m4/

As per the eevblog thread, the following flags are required:

.. code-block:: text
	:linenos:

	CFLAGS += -fPIC -mno-pic-data-is-text-relative -msingle-pic-base -mpic-register=r9
	LFLAGS += -fPIC

R9 value has to be initialised on startup:

.. code-block:: c
	:linenos:

	asm("ldr r9, =_data");

Oh but we are using FreeRTOS which trashes R9 content when a new thread is created. It needs a modification:

.. code-block:: diff
	:linenos:

	diff --git a/lib/freertos/portable/GCC/ARM_CM4F/port.c b/lib/freertos/portable/GCC/ARM_CM4F/port.c
	index d5cbef4..85dec97 100644
	--- a/lib/freertos/portable/GCC/ARM_CM4F/port.c
	+++ b/lib/freertos/portable/GCC/ARM_CM4F/port.c
	@@ -208,7 +208,13 @@ StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t px
		pxTopOfStack--;
		*pxTopOfStack = portINITIAL_EXC_RETURN;

	-       pxTopOfStack -= 8;      /* R11, R10, R9, R8, R7, R6, R5 and R4. */
	+       pxTopOfStack -= 2; /* R11, R10. */
	+
	+       extern uint32_t _data;
	+       pxTopOfStack--; /* R9 */
	+       *pxTopOfStack = (StackType_t)&_data;
	+
	+       pxTopOfStack -= 5;      /* R8, R7, R6, R5 and R4. */

		return pxTopOfStack;
	 }

Linker script modification (using libopencm3):

.. code-block:: diff
	:linenos:

	diff --git a/lib/cortex-m-generic.ld b/lib/cortex-m-generic.ld
	index f7b1da01..819d6bd9 100644
	--- a/lib/cortex-m-generic.ld
	+++ b/lib/cortex-m-generic.ld
	@@ -100,6 +100,8 @@ SECTIONS
			*(.data*)       /* Read-write initialized data */
			*(.ramtext*)    /* "text" functions to run in ram */
			. = ALIGN(4);
	+               _got = .;
	+               *(.got)
			_edata = .;
		} >ram AT >rom
		_data_loadaddr = LOADADDR(.data);

Now it starts to behave until it goes to a Newlib function compiled without PIC support.

TODO: Newlib has to be recompiled with PIC support
