/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal sed-like substitution
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A backtracking matcher working directly on the regex text, nothing is compiled. Each regex position is matched
 * by a recursive call, a starred atom tries the longest run first and gives back one character at a time. Group
 * numbers are not stored, they are recomputed by walking the regex from its start whenever a group marker is
 * reached. As the subset has no alternation and groups cannot be repeated, every successful path passes all the
 * group markers in order, so the captures left behind by a successful match are always the right ones.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sed.h"

/* Group 0 is the whole match, 1 to 9 the \( \) groups. */
#define SED_GROUPS 10

struct sed_match {
	const char *re;
	const char *re_end;
	const char *group[SED_GROUPS][2];
};


/* Return the end of the regex atom starting at re: an escape, a bracket expression or a single character. NULL
 * for a trailing backslash or an unterminated bracket expression. A "]" right after the opening "[" or "[^" is
 * literal, a backslash inside the brackets is a literal character too. */
static const char *sed_atom_end(const char *re, const char *re_end) {
	if (*re == '\\') {
		return (re + 1 < re_end) ? re + 2 : NULL;
	}
	if (*re == '[') {
		const char *p = re + 1;
		if (p < re_end && *p == '^') {
			p++;
		}
		if (p < re_end && *p == ']') {
			p++;
		}
		while (p < re_end && *p != ']') {
			p++;
		}
		return (p < re_end) ? p + 1 : NULL;
	}
	return re + 1;
}


/* Match c against the bracket expression at re, already known to be terminated. */
static bool sed_match_bracket(const char *re, char c) {
	const char *p = re + 1;
	bool negate = false;
	if (*p == '^') {
		negate = true;
		p++;
	}
	bool found = false;
	do {
		if (p[1] == '-' && p[2] != ']') {
			if ((unsigned char)c >= (unsigned char)p[0] && (unsigned char)c <= (unsigned char)p[2]) {
				found = true;
			}
			p += 3;
		} else {
			if (c == *p) {
				found = true;
			}
			p++;
		}
	} while (*p != ']');
	return found != negate;
}


/* Match a single character against the atom at re. The string terminator never matches. */
static bool sed_match_atom(const char *re, char c) {
	if (c == '\0') {
		return false;
	}
	switch (*re) {
		case '.':
			return true;
		case '[':
			return sed_match_bracket(re, c);
		case '\\':
			return c == re[1];
		default:
			return c == *re;
	}
}


/* Number of the group whose \( or \) marker is at p, found by walking the regex from its start. */
static size_t sed_group_index(struct sed_match *m, const char *p) {
	uint8_t stack[SED_GROUPS];
	size_t depth = 0;
	size_t count = 0;
	for (const char *r = m->re; r < p; r = sed_atom_end(r, m->re_end)) {
		if (r[0] == '\\' && r[1] == '(') {
			stack[depth++] = (uint8_t)++count;
		} else if (r[0] == '\\' && r[1] == ')') {
			depth--;
		}
	}
	if (p[1] == '(') {
		return count + 1;
	}
	return stack[depth - 1];
}


/* Match the regex from re on against text, returning the end of the match or NULL when it does not match. */
static const char *sed_match_here(struct sed_match *m, const char *re, const char *text) {
	if (re == m->re_end) {
		return text;
	}
	if (re[0] == '\\' && (re[1] == '(' || re[1] == ')')) {
		m->group[sed_group_index(m, re)][(re[1] == '(') ? 0 : 1] = text;
		return sed_match_here(m, re + 2, text);
	}
	if (re[0] == '$' && re + 1 == m->re_end) {
		return (*text == '\0') ? text : NULL;
	}
	const char *next = sed_atom_end(re, m->re_end);
	if (next < m->re_end && *next == '*') {
		size_t n = 0;
		while (sed_match_atom(re, text[n])) {
			n++;
		}
		do {
			const char *end = sed_match_here(m, next + 1, text + n);
			if (end != NULL) {
				return end;
			}
		} while (n-- > 0);
		return NULL;
	}
	if (sed_match_atom(re, *text)) {
		return sed_match_here(m, next, text + 1);
	}
	return NULL;
}


/* Append n characters of src to the output at o, dropping whatever does not fit before o_end. */
static char *sed_append(char *o, char *o_end, const char *src, size_t n) {
	while (n > 0 && o < o_end) {
		*o++ = *src++;
		n--;
	}
	return o;
}


/* Append the replacement with "&" and "\1" to "\9" expanded from the groups of the last match. */
static char *sed_replace(struct sed_match *m, char *o, char *o_end, const char *rep, const char *rep_end) {
	for (const char *r = rep; r < rep_end; r++) {
		size_t g = SED_GROUPS;
		if (*r == '&') {
			g = 0;
		} else if (*r == '\\') {
			r++;
			if (*r >= '1' && *r <= '9') {
				g = (size_t)(*r - '0');
			}
		}
		if (g == SED_GROUPS) {
			o = sed_append(o, o_end, r, 1);
		} else if (m->group[g][0] != NULL && m->group[g][1] != NULL) {
			o = sed_append(o, o_end, m->group[g][0], (size_t)(m->group[g][1] - m->group[g][0]));
		}
	}
	return o;
}


sed_ret_t sed_subst(const char *expr, const char *in, char *out, size_t out_size) {
	if (out_size == 0 || expr[0] != 's' || expr[1] == '\0' || expr[1] == '\\' || expr[1] == '\n') {
		return SED_RET_FAILED;
	}
	const char *end = expr + strlen(expr);
	char d = expr[1];
	struct sed_match m = {.re = expr + 2};

	/* Find the regex end, skipping escapes and bracket expressions, which may contain the delimiter. The groups
	 * must be balanced and at most 9. */
	size_t groups = 0;
	size_t depth = 0;
	const char *r = m.re;
	while (r < end && *r != d) {
		if (r[0] == '\\' && r[1] == '(') {
			groups++;
			depth++;
		} else if (r[0] == '\\' && r[1] == ')') {
			if (depth == 0) {
				return SED_RET_FAILED;
			}
			depth--;
		}
		r = sed_atom_end(r, end);
		if (r == NULL) {
			return SED_RET_FAILED;
		}
	}
	if (r == end || depth > 0 || groups >= SED_GROUPS) {
		return SED_RET_FAILED;
	}
	m.re_end = r;

	/* Find the replacement end, rejecting references to groups the regex does not have. */
	const char *rep = r + 1;
	for (r = rep; r < end && *r != d; r++) {
		if (*r == '\\') {
			r++;
			if (r == end || (*r >= '1' && *r <= '9' && (size_t)(*r - '0') > groups)) {
				return SED_RET_FAILED;
			}
		}
	}
	if (r == end) {
		return SED_RET_FAILED;
	}
	const char *rep_end = r;

	bool global = false;
	for (r = rep_end + 1; r < end; r++) {
		if (*r != 'g') {
			return SED_RET_FAILED;
		}
		global = true;
	}

	bool anchored = (m.re < m.re_end && *m.re == '^');
	const char *re = anchored ? m.re + 1 : m.re;
	char *o = out;
	char *o_end = out + out_size - 1;
	const char *t = in;
	bool prev_nonempty = false;
	while (!anchored || t == in) {
		/* Leftmost match from t on. */
		const char *s = t;
		const char *e = NULL;
		while ((e = sed_match_here(&m, re, s)) == NULL && *s != '\0' && !anchored) {
			s++;
		}
		if (e == NULL) {
			break;
		}
		/* Like sed, an empty match right after the previous match is skipped. */
		if (e == s && s == t && prev_nonempty) {
			if (*t == '\0') {
				break;
			}
			o = sed_append(o, o_end, t++, 1);
			prev_nonempty = false;
			continue;
		}
		m.group[0][0] = s;
		m.group[0][1] = e;
		o = sed_append(o, o_end, t, (size_t)(s - t));
		o = sed_replace(&m, o, o_end, rep, rep_end);
		t = e;
		prev_nonempty = (e != s);
		if (!global) {
			break;
		}
		/* An empty match does not advance, copy the next character to move past it. */
		if (e == s) {
			if (*e == '\0') {
				break;
			}
			o = sed_append(o, o_end, t++, 1);
		}
	}
	o = sed_append(o, o_end, t, strlen(t));
	*o = '\0';
	return SED_RET_OK;
}
