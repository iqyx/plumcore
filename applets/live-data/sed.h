/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal sed-like substitution
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stddef.h>

typedef enum sed_ret {
	SED_RET_OK = 0,
	SED_RET_FAILED,
} sed_ret_t;

/* Apply a single sed substitution command expr, s<d>regex<d>replacement<d>[g] with any delimiter d, to the input
 * string in and write the result to out (always terminated, truncated to out_size). When the regex does not match,
 * the input is copied unchanged, like sed does. Returns SED_RET_FAILED when expr is not a valid command.
 *
 * The regex is a subset of POSIX basic regular expressions: literal characters, ".", bracket expressions with
 * ranges and "^" negation (without character classes), "*" after a single character atom, "^" and "$" anchors,
 * "\(" "\)" groups (up to 9, nested allowed, not repeatable) and "\c" escapes making c literal. The replacement
 * may contain "&" (the whole match), "\1" to "\9" (groups) and "\c" escapes. */
sed_ret_t sed_subst(const char *expr, const char *in, char *out, size_t out_size);
