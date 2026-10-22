/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EP-7d3 calibration certificate parser of the mq-compensation service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>
#include <cbor.h>

#include <interfaces/flash.h>

#include "mq-compensation.h"
#include "mq-compensation-cert.h"

#define MODULE_NAME "mq-compensation"

#if MQ_COMPENSATION_MAX_ORDER < 1
	#error "MQ_COMPENSATION_MAX_ORDER must be at least 1 to represent the identity polynomial"
#endif

/* Indefinite-length map head followed by the "CBCAL": 1 version pair. */
static const uint8_t cert_magic[] = {0xbf, 0x65, 'C', 'B', 'C', 'A', 'L', 0x01};


/*********************************************************************************************************************
 * tinycbor flash reader callbacks
 *********************************************************************************************************************/

static bool cert_reader_can_read(void *token, size_t len) {
	struct mq_compensation_cert *self = token;
	return (len <= self->len) && (self->pos <= self->len - len);
}


static void *cert_reader_read(void *token, void *dst, size_t offset, size_t len) {
	struct mq_compensation_cert *self = token;
	if (offset > self->len - self->pos || len > self->len - self->pos - offset) {
		return NULL;
	}
	if (self->flash->vmt->read(self->flash, self->pos + offset, dst, len) != FLASH_RET_OK) {
		return NULL;
	}
	return dst;
}


static void cert_reader_advance(void *token, size_t len) {
	struct mq_compensation_cert *self = token;
	self->pos += len;
}


static CborError cert_reader_transfer_string(void *token, const void **userptr, size_t offset, size_t len) {
	struct mq_compensation_cert *self = token;
	self->pos += offset;
	if (len > self->len || self->pos > self->len - len) {
		return CborErrorUnexpectedEOF;
	}
	/* Strings are copied only after checking they fit the scratch buffer, a longer one is being skipped (eg. by
	 * cbor_value_advance). Its contents are not used, the string is passed without reading it. */
	if (len > sizeof(self->scratch)) {
		*userptr = self->scratch;
		self->pos += len;
		return CborNoError;
	}
	if (self->flash->vmt->read(self->flash, self->pos, self->scratch, len) != FLASH_RET_OK) {
		return CborErrorIO;
	}
	*userptr = self->scratch;
	self->pos += len;
	return CborNoError;
}


static const struct CborParserOperations cert_reader_ops = {
	.can_read_bytes = cert_reader_can_read,
	.read_bytes = cert_reader_read,
	.advance_bytes = cert_reader_advance,
	.transfer_string = cert_reader_transfer_string,
};


/*********************************************************************************************************************
 * Value helpers
 *********************************************************************************************************************/

static mq_compensation_ret_t cert_get_float(CborValue *it, float *f);


/* Parse the certificate from its beginning, return @p it positioned at the first key after the version pair. */
static mq_compensation_ret_t cert_open(struct mq_compensation_cert *self, CborValue *it) {
	self->pos = 0;
	if (cbor_parser_init_reader(&cert_reader_ops, &self->parser, &self->root, self) != CborNoError ||
	    !cbor_value_is_map(&self->root) ||
	    cbor_value_enter_container(&self->root, it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	/* Skip the version pair, already checked in init. */
	if (cbor_value_advance(it) != CborNoError ||
	    cbor_value_advance(it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Copy a text string to @p buf and advance past it. Fails if it is not a text string or does not fit. */
static mq_compensation_ret_t cert_copy_text(CborValue *it, char *buf, size_t size) {
	size_t len = 0;
	if (!cbor_value_is_text_string(it) ||
	    cbor_value_get_string_length(it, &len) != CborNoError ||
	    len >= size) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	len = size;
	if (cbor_value_copy_text_string(it, buf, &len, it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Compare a value with the text @p s and advance past it. A text string is compared directly, a number and a bool
 * with @p s parsed as a number or "true"/"false". Any other value is not equal. */
static mq_compensation_ret_t cert_value_equals(CborValue *it, const char *s, bool *eq) {
	*eq = false;
	if (cbor_value_is_text_string(it)) {
		size_t len = 0;
		if (cbor_value_get_string_length(it, &len) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		if (len == strlen(s)) {
			char buf[MQ_COMPENSATION_CERT_STR_LEN];
			if (cert_copy_text(it, buf, sizeof(buf)) != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
			*eq = !strcmp(buf, s);
			return MQ_COMPENSATION_RET_OK;
		}
	} else if (cbor_value_is_integer(it)) {
		int64_t v = 0;
		char *end = NULL;
		long long parsed = strtoll(s, &end, 0);
		if (cbor_value_get_int64_checked(it, &v) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		*eq = (*s != '\0') && (*end == '\0') && (v == parsed);
	} else if (cbor_value_is_float(it) || cbor_value_is_double(it)) {
		float f = 0.0f;
		char *end = NULL;
		float parsed = strtof(s, &end);
		if (cert_get_float(it, &f) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		*eq = (*s != '\0') && (*end == '\0') && (f == parsed);
		return MQ_COMPENSATION_RET_OK;
	} else if (cbor_value_is_boolean(it)) {
		bool b = false;
		if (cbor_value_get_boolean(it, &b) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		*eq = !strcmp(s, b ? "true" : "false");
	}
	if (cbor_value_advance(it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Advance the map iterator @p it to the value of @p key. @p found is false if the map has no such key. */
static mq_compensation_ret_t cert_find_key(CborValue *it, const char *key, bool *found) {
	*found = false;
	while (!cbor_value_at_end(it)) {
		if (cert_value_equals(it, key, found) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		if (*found) {
			return MQ_COMPENSATION_RET_OK;
		}
		if (cbor_value_advance(it) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Parse the certificate from its beginning and enter the map or array of the top-level @p key into @p it. */
static mq_compensation_ret_t cert_enter_top(struct mq_compensation_cert *self, const char *key, CborValue *it) {
	CborValue top;
	bool found = false;
	if (cert_open(self, &top) != MQ_COMPENSATION_RET_OK ||
	    cert_find_key(&top, key, &found) != MQ_COMPENSATION_RET_OK ||
	    !found ||
	    !cbor_value_is_container(&top) ||
	    cbor_value_enter_container(&top, it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Read a number (float, double or integer) as a float and advance past it. */
static mq_compensation_ret_t cert_get_float(CborValue *it, float *f) {
	if (cbor_value_is_float(it)) {
		if (cbor_value_get_float(it, f) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	} else if (cbor_value_is_double(it)) {
		double d = 0.0;
		if (cbor_value_get_double(it, &d) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		*f = (float)d;
	} else if (cbor_value_is_integer(it)) {
		int64_t i = 0;
		if (cbor_value_get_int64_checked(it, &i) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		*f = (float)i;
	} else {
		return MQ_COMPENSATION_RET_FAILED;
	}
	if (cbor_value_advance(it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Read an array of up to @p n numbers into @p a and advance past it. Fails if the array is longer. */
static mq_compensation_ret_t cert_get_float_array(CborValue *it, float *a, size_t n) {
	CborValue e;
	if (!cbor_value_is_array(it) ||
	    cbor_value_enter_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	for (size_t i = 0; !cbor_value_at_end(&e); i++) {
		if (i >= n || cert_get_float(&e, &a[i]) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	if (cbor_value_leave_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Copy a text string truncated to fit @p buf and advance past it. The string is read directly from the flash as it may
 * be longer than the scratch buffer, it is used for logging only. */
static mq_compensation_ret_t cert_copy_text_trunc(struct mq_compensation_cert *self, CborValue *it, char *buf, size_t size) {
	size_t len = 0;
	if (!cbor_value_is_text_string(it) ||
	    cbor_value_get_string_length(it, &len) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	/* The reader cursor is at the head of the string, its size depends on the length. */
	size_t head = (len < 24) ? 1 : (len < 0x100) ? 2 : (len < 0x10000) ? 3 : 5;
	size_t n = (len < size - 1) ? len : size - 1;
	if (self->flash->vmt->read(self->flash, self->pos + head, buf, n) != FLASH_RET_OK ||
	    cbor_value_advance(it) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	buf[n] = '\0';
	return MQ_COMPENSATION_RET_OK;
}


/*********************************************************************************************************************
 * Certificate information
 *********************************************************************************************************************/

/* Log the name of a party ("lab", "cst") of the administrative data and advance past it. */
static mq_compensation_ret_t cert_log_party(struct mq_compensation_cert *self, CborValue *it, const char *what) {
	CborValue e;
	if (!cbor_value_is_map(it) ||
	    cbor_value_enter_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&e)) {
		bool found = false;
		if (cert_value_equals(&e, "n", &found) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		if (found) {
			char name[64];
			if (cert_copy_text_trunc(self, &e, name, sizeof(name)) != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("certificate %s: %s"), what, name);
		} else if (cbor_value_advance(&e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	if (cbor_value_leave_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Log the laboratory, the customer and the issue date of the administrative data. */
static mq_compensation_ret_t cert_log_adm(struct mq_compensation_cert *self) {
	CborValue e;
	if (cert_enter_top(self, "adm", &e) != MQ_COMPENSATION_RET_OK) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&e)) {
		char key[MQ_COMPENSATION_CERT_STR_LEN];
		if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		mq_compensation_ret_t ret = MQ_COMPENSATION_RET_OK;
		if (!strcmp(key, "lab")) {
			ret = cert_log_party(self, &e, "laboratory");
		} else if (!strcmp(key, "cst")) {
			ret = cert_log_party(self, &e, "customer");
		} else if (!strcmp(key, "di")) {
			char date[MQ_COMPENSATION_CERT_STR_LEN];
			ret = cert_copy_text_trunc(self, &e, date, sizeof(date));
			if (ret == MQ_COMPENSATION_RET_OK) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("certificate issued: %s"), date);
			}
		} else if (cbor_value_advance(&e) != CborNoError) {
			ret = MQ_COMPENSATION_RET_FAILED;
		}
		if (ret != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Log the part number and the serial number of the top-level targets (with no parent). */
static mq_compensation_ret_t cert_log_targets(struct mq_compensation_cert *self) {
	CborValue tgts;
	if (cert_enter_top(self, "tgt", &tgts) != MQ_COMPENSATION_RET_OK) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&tgts)) {
		char id[MQ_COMPENSATION_CERT_STR_LEN];
		CborValue e;
		if (cert_copy_text(&tgts, id, sizeof(id)) != MQ_COMPENSATION_RET_OK ||
		    !cbor_value_is_map(&tgts) ||
		    cbor_value_enter_container(&tgts, &e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}

		char pn[64] = "";
		char sn[64] = "";
		bool has_parent = false;
		while (!cbor_value_at_end(&e)) {
			char key[MQ_COMPENSATION_CERT_STR_LEN];
			if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
			mq_compensation_ret_t ret = MQ_COMPENSATION_RET_OK;
			if (!strcmp(key, "pn")) {
				ret = cert_copy_text_trunc(self, &e, pn, sizeof(pn));
			} else if (!strcmp(key, "sn")) {
				ret = cert_copy_text_trunc(self, &e, sn, sizeof(sn));
			} else {
				has_parent = has_parent || !strcmp(key, "p");
				if (cbor_value_advance(&e) != CborNoError) {
					ret = MQ_COMPENSATION_RET_FAILED;
				}
			}
			if (ret != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
		}
		if (cbor_value_leave_container(&tgts, &e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}

		if (!has_parent) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("certificate target '%s': part number %s, serial number %s"), id, pn, sn);
		}
	}
	return MQ_COMPENSATION_RET_OK;
}


/*********************************************************************************************************************
 * Compensation model
 *********************************************************************************************************************/

/* Parse the "poly" step: the centering offset "x0" and the coefficients "c". */
static mq_compensation_ret_t cert_parse_poly(CborValue *it, struct mq_compensation_channel_conf *comp) {
	CborValue e;
	if (!cbor_value_is_map(it) ||
	    cbor_value_enter_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	memset(comp->c, 0, sizeof(comp->c));
	while (!cbor_value_at_end(&e)) {
		char key[MQ_COMPENSATION_CERT_STR_LEN];
		if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		mq_compensation_ret_t ret = MQ_COMPENSATION_RET_FAILED;
		if (!strcmp(key, "x0")) {
			ret = cert_get_float(&e, &comp->x_ref);
		} else if (!strcmp(key, "c")) {
			ret = cert_get_float_array(&e, comp->c, MQ_COMPENSATION_MAX_ORDER + 1);
		}
		if (ret != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	if (cbor_value_leave_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Parse the "tc" step: the temperature reference "t0" and up to two coefficients "tc". */
static mq_compensation_ret_t cert_parse_tc(CborValue *it, struct mq_compensation_channel_conf *comp) {
	CborValue e;
	if (!cbor_value_is_map(it) ||
	    cbor_value_enter_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&e)) {
		char key[MQ_COMPENSATION_CERT_STR_LEN];
		if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		mq_compensation_ret_t ret = MQ_COMPENSATION_RET_FAILED;
		if (!strcmp(key, "t0")) {
			ret = cert_get_float(&e, &comp->t_ref);
		} else if (!strcmp(key, "tc")) {
			float tc[2] = {0.0f, 0.0f};
			ret = cert_get_float_array(&e, tc, 2);
			comp->tc1 = tc[0];
			comp->tc2 = tc[1];
		}
		if (ret != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	if (cbor_value_leave_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/* Parse a "comp" model into the coefficients of @p comp. Absent steps are identity, an unknown step fails as the
 * model cannot be applied correctly without it. */
static mq_compensation_ret_t cert_parse_comp(CborValue *it, struct mq_compensation_channel_conf *comp) {
	comp->reciprocal = false;
	comp->x_ref = 0.0f;
	memset(comp->c, 0, sizeof(comp->c));
	comp->c[1] = 1.0f;
	comp->exp = 1.0f;
	comp->t_ref = 0.0f;
	comp->tc1 = 0.0f;
	comp->tc2 = 0.0f;

	CborValue e;
	if (!cbor_value_is_map(it) ||
	    cbor_value_enter_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&e)) {
		char key[MQ_COMPENSATION_CERT_STR_LEN];
		if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		mq_compensation_ret_t ret = MQ_COMPENSATION_RET_FAILED;
		if (!strcmp(key, "recip")) {
			if (cbor_value_is_boolean(&e) &&
			    cbor_value_get_boolean(&e, &comp->reciprocal) == CborNoError &&
			    cbor_value_advance(&e) == CborNoError) {
				ret = MQ_COMPENSATION_RET_OK;
			}
		} else if (!strcmp(key, "poly")) {
			ret = cert_parse_poly(&e, comp);
		} else if (!strcmp(key, "exp")) {
			ret = cert_get_float(&e, &comp->exp);
			if (comp->exp == 0.0f) {
				ret = MQ_COMPENSATION_RET_FAILED;
			}
		} else if (!strcmp(key, "tc")) {
			ret = cert_parse_tc(&e, comp);
		}
		if (ret != MQ_COMPENSATION_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot parse compensation step '%s'"), key);
			return MQ_COMPENSATION_RET_FAILED;
		}
	}
	if (cbor_value_leave_container(it, &e) != CborNoError) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


/*********************************************************************************************************************
 * Public functions
 *********************************************************************************************************************/

mq_compensation_ret_t mq_compensation_cert_init(struct mq_compensation_cert *self, Flash *flash) {
	memset(self, 0, sizeof(struct mq_compensation_cert));
	self->flash = flash;

	flash_block_ops_t ops;
	if (flash->vmt->get_size(flash, 0, &self->len, &ops) != FLASH_RET_OK ||
	    self->len < sizeof(cert_magic)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	uint8_t magic[sizeof(cert_magic)];
	if (flash->vmt->read(flash, 0, magic, sizeof(magic)) != FLASH_RET_OK ||
	    memcmp(magic, cert_magic, sizeof(magic))) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_cert_log_info(struct mq_compensation_cert *self) {
	mq_compensation_ret_t adm = cert_log_adm(self);
	mq_compensation_ret_t targets = cert_log_targets(self);
	if (adm != MQ_COMPENSATION_RET_OK || targets != MQ_COMPENSATION_RET_OK) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_cert_find_cfg(struct mq_compensation_cert *self, const struct mq_compensation_cert_kv *kv, char *id, size_t size) {
	size_t kv_count = 0;
	while (kv[kv_count].key != NULL) {
		kv_count++;
	}

	CborValue cfgs;
	if (cert_enter_top(self, "cfg", &cfgs) != MQ_COMPENSATION_RET_OK) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&cfgs)) {
		CborValue e;
		if (cert_copy_text(&cfgs, id, size) != MQ_COMPENSATION_RET_OK ||
		    !cbor_value_is_map(&cfgs) ||
		    cbor_value_enter_container(&cfgs, &e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}

		/* Every pair of kv must be found in the configuration. Pairs of the configuration not in kv are ignored. */
		bool match = true;
		size_t count = 0;
		while (!cbor_value_at_end(&e)) {
			char key[MQ_COMPENSATION_CERT_STR_LEN];
			if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}

			const struct mq_compensation_cert_kv *p = kv;
			while (p->key != NULL && strcmp(p->key, key)) {
				p++;
			}

			if (p->key == NULL) {
				if (cbor_value_advance(&e) != CborNoError) {
					return MQ_COMPENSATION_RET_FAILED;
				}
				continue;
			}
			bool eq = false;
			if (cert_value_equals(&e, p->value, &eq) != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
			match = match && eq;
			count++;
		}
		if (cbor_value_leave_container(&cfgs, &e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}

		if (match && count == kv_count) {
			return MQ_COMPENSATION_RET_OK;
		}
	}
	return MQ_COMPENSATION_RET_FAILED;
}


mq_compensation_ret_t mq_compensation_cert_check_path(struct mq_compensation_cert *self, const char *const *path, const char **tgt) {
	if (path[0] == NULL) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	/* Re-walk the targets for each element of the path, checking its parent is the preceding one. */
	for (size_t i = 0; path[i] != NULL; i++) {
		CborValue tgts;
		CborValue e;
		bool found = false;
		if (cert_enter_top(self, "tgt", &tgts) != MQ_COMPENSATION_RET_OK ||
		    cert_find_key(&tgts, path[i], &found) != MQ_COMPENSATION_RET_OK ||
		    !found ||
		    !cbor_value_is_map(&tgts) ||
		    cbor_value_enter_container(&tgts, &e) != CborNoError ||
		    cert_find_key(&e, "p", &found) != MQ_COMPENSATION_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("target '%s' not found"), path[i]);
			return MQ_COMPENSATION_RET_FAILED;
		}

		/* The first target is the root without a parent, the others are children of the preceding one. */
		bool parent_ok = !found;
		if (i > 0 && found && cert_value_equals(&e, path[i - 1], &parent_ok) != MQ_COMPENSATION_RET_OK) {
			return MQ_COMPENSATION_RET_FAILED;
		}
		if (i > 0 && !found) {
			parent_ok = false;
		}
		if (!parent_ok) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("target '%s' has a different parent"), path[i]);
			return MQ_COMPENSATION_RET_FAILED;
		}
		*tgt = path[i];
	}
	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_cert_find_comp(struct mq_compensation_cert *self, const char *tgt, const char *cfg, struct mq_compensation_channel_conf *comp) {
	CborValue groups;
	if (cert_enter_top(self, "res", &groups) != MQ_COMPENSATION_RET_OK) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	while (!cbor_value_at_end(&groups)) {
		CborValue e;
		if (!cbor_value_is_map(&groups) ||
		    cbor_value_enter_container(&groups, &e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}

		/* The keys come in any order, the model is parsed before knowing whether the group matches. */
		struct mq_compensation_channel_conf group_comp;
		bool tgt_match = false;
		bool cfg_match = true;
		bool has_comp = false;
		while (!cbor_value_at_end(&e)) {
			char key[MQ_COMPENSATION_CERT_STR_LEN];
			if (cert_copy_text(&e, key, sizeof(key)) != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
			mq_compensation_ret_t ret = MQ_COMPENSATION_RET_OK;
			if (!strcmp(key, "tgt")) {
				ret = cert_value_equals(&e, tgt, &tgt_match);
			} else if (!strcmp(key, "cfg")) {
				ret = cert_value_equals(&e, (cfg != NULL) ? cfg : "", &cfg_match);
				cfg_match = cfg_match && (cfg != NULL);
			} else if (!strcmp(key, "comp")) {
				ret = cert_parse_comp(&e, &group_comp);
				has_comp = true;
			} else if (cbor_value_advance(&e) != CborNoError) {
				ret = MQ_COMPENSATION_RET_FAILED;
			}
			if (ret != MQ_COMPENSATION_RET_OK) {
				return MQ_COMPENSATION_RET_FAILED;
			}
		}
		if (cbor_value_leave_container(&groups, &e) != CborNoError) {
			return MQ_COMPENSATION_RET_FAILED;
		}

		if (tgt_match && cfg_match && has_comp) {
			comp->reciprocal = group_comp.reciprocal;
			comp->x_ref = group_comp.x_ref;
			memcpy(comp->c, group_comp.c, sizeof(comp->c));
			comp->exp = group_comp.exp;
			comp->t_ref = group_comp.t_ref;
			comp->tc1 = group_comp.tc1;
			comp->tc2 = group_comp.tc2;
			return MQ_COMPENSATION_RET_OK;
		}
	}
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no compensation model of target '%s'"), tgt);
	return MQ_COMPENSATION_RET_FAILED;
}
