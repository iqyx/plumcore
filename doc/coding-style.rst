===============================
Coding style guide
===============================


Variable, type and function names
===================================

Use PascalCase:

- when you are declaring a class with ``typedef struct {} ClassName``, eg. for interfaces, services.
- for any structs resembling classes when your intention is to hide implementation details

Use snake_case:

- for member variable names inside ``struct`` and ``union``
- for ``typedef`` types when the user is not required to understand or access the value. Append ``_t`` in this case.
- for function names, eg. ``spi_nor_flash_init``
- for struct and enum names

Use ALL_CAPS_WITH_UNDERSCORE:

- for macro names
- for enum items

Never use Hungarian notation for function and variable names. As the linux kernel coding style says, it is brain damaged.

Never hide anything behind a typedef. Use ``struct``, ``enum``, ``union``, etc. directly.

Examples:

.. code-block:: c
	:linenos:

	typedef enum {
		MODULE_RET_OK = 0,
		MODULE_RET_FAILED,
	} module_ret_t;

	struct something {

	};

	enum state {
		STATE_ONE,
		STATE_TWO,
	};

	module_ret_t module_function_name(int i);
	

Types
================

Use stdint types for numbers, like ``uint32_t``, ``uint8_t``.

Use stdbool type for boolean ``bool``.


Macros
==============

Use macros to define compile-time constants. Do not use multi-line macros, please.
We have functions for this purpose. Multi-line macros are allowed only in special circumstances.


Return values
============================

All functions shall have return values unless there are good reasons for omitting them.
The return value must be an ``enum``, usually with a ``typedef`` to a name like ``module_ret_t``
for return values for all function within a module ``module``. The first enum item shall be
the default non-failing return value, generally an ``OK``.

Other types of return values, such as ``bool``, ``int``, etc., are allowed only for simple
getter functions which cannot fail by design.


Spaces
=========================

Always put spaces around infix operators like ``a + b``, ``a >> 2``, etc. Never put a space before a suffix
or after a prefix operator, like ``a++``, ``++a``.

Never put space around structure member access operators ``.`` and ``->``.

Always put spaces between keywords and the opening brace ``while (``, ``if (``. The only exception is
``sizeof()``, ``typeof()``, ``alignof()``, ``__attribute__()``,

Never put space between a function name and the opening brace ``int function(params);``. Never put
spaces inside of parentheses.

Always put ``*`` adjacent to the variable or function name, not the type name.

Never keep any trailing whitespace at the end of lines. The same applies to empty lines.


Indentation
=======================

Always indent with tabs as all gods intended. Never indent with spaces. Set tab width
to whatever size you like.

Always align text with spaces. Never align text with tabs, it will not be aligned when you
set your tab width to whatever size you like.

Rationale: please, there is no real reason for using spaces to indent code. "Oh but my code
breaks if the tab width is different" - you are doing it wrong. Read previous lines again.


Line length
==================

Try to maintain maximum line length about 120 characters but never wrap them even if longer.

Rationale: it is not 1990 anymore, we are not using 80 character wide terminals nowadays.
If you really consider this problematic, setup your editor to automatically wrap long lines.


Code blocks, curly braces
===========================

Put opening curly brace ``{`` at the end of the line. Put closing curly brace ``}`` at the new line.
The closing brace is the only one on the line except when the previous statement continues, such
as ``if``, ``do .. while``.


if-then-else
====================

The form is as following:

.. code-block:: c
	:linenos:

	if (cond) {
		/* something */
	} else if (cond2) {
		/* something */
	} else {
		/* something other */
	}

Rationale: more readable code


switch
==============

.. code-block:: c
	:linenos:

	switch (var) {
		case 1:
			/* something */
			break;
		case 2:
		case 3:
			/* something */
			break;
		case 4: {
			/* We need curly braces in order to define a variable inside. */
			int i;
		}
		default:
			/* something */
			break;
	}


Comments
================

Do not use double-slash ``//`` comments. Use ``/* .. */`` instead.

Examples:

.. code-block:: c
	:linenos:

	/* A single line comment. Starts with a capital letter, ends with a period. */

	/*
	 * Important single line comment.
	 */
	

	/* 
	 * A multi line comment is usually
	 * a bit longer, it is indented from the left,
	 * opening and closing is on same lines as the text.
	 */

	/**
	 * @brief Doxygen multi line comment
	 *
	 * Use double asterisk at the beginning.
	 */


Copyright headers
==========================

.. code-block:: c
	:linenos:

	/* SPDX-License-Identifier: BSD-2-Clause
	 *
	 * Description of the file
	 *
	 * Copyright (c) 2023, My Name (name@example.org)
	 * All rights reserved.
	 */

Includes
========================

.. code-block:: c
	:linenos:

	/* Put libc includes first. */
	#include <stdio.h>
	#include <stdint.h>

	/* Generic system includes follow. */
	#include <main.h>
	#include <config.h>

	/* Interface declarations follow. */
	#include <interface/flash.h>

	/* Library includes. */
	#include <spiffs.h>
	#include <heatshrink.h>

	/* Local includes. */
	#include "something.h"
