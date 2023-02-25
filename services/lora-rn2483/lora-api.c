static lora_ret_t lora_modem_set_mode(LoRa *self, enum lora_mode mode) {
	LoraModem *lora = (LoraModem *)self->parent;

	/** @TODO proper error checking */
	lora->lora_mode = mode;

	return LORA_RET_OK;
}


static lora_ret_t lora_modem_get_mode(LoRa *self, enum lora_mode *mode) {
	PARAM_CHECK(mode != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	/** @TODO proper error checking */
	*mode = lora->lora_mode;

	return LORA_RET_OK;
}


static lora_ret_t lora_modem_set_appkey(LoRa *self, const char *appkey) {
	PARAM_CHECK(appkey != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	if (lora->lora_mode != LORA_MODE_OTAA) {
		return LORA_RET_MODE;
	}

	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set appkey %s", appkey);
	strlcpy(lora->appkey, appkey, 32 + 1);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_get_appkey(LoRa *self, char *appkey, size_t size) {
	PARAM_CHECK(appkey != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	if (lora->lora_mode != LORA_MODE_OTAA) {
		return LORA_RET_MODE;
	}
	strlcpy(appkey, lora->appkey, size);

	return LORA_RET_OK;
}


static lora_ret_t lora_modem_set_appeui(LoRa *self, const char *appeui) {
	PARAM_CHECK(appeui != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set appeui %s", appeui);
	strlcpy(lora->appeui, appeui, 16 + 1);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_get_appeui(LoRa *self, char *appeui, size_t size) {
	PARAM_CHECK(appeui != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, "mac get appeui");
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = LORA_RET_OK;
	if (lora->response_len != 16) {
		ret = LORA_RET_FAILED;
	}
	strlcpy(appeui, lora->response_str, size);
	command_unlock(lora);

	return ret;
}



static lora_ret_t lora_modem_set_deveui(LoRa *self, const char *deveui) {
	PARAM_CHECK(deveui != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set deveui %s", deveui);
	strlcpy(lora->deveui, deveui, 16 + 1);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_get_deveui(LoRa *self, char *deveui) {
	PARAM_CHECK(deveui != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, "mac get deveui");
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	strcpy(deveui, lora->response_str);
	command_unlock(lora);

	return LORA_RET_OK;
}


static lora_ret_t lora_modem_set_devaddr(LoRa *self, const uint8_t devaddr[4]) {
	char cmd[50] = {0};
	LoraModem *lora = (LoraModem *)self->parent;

	if (lora->lora_mode != LORA_MODE_ABP) {
		return LORA_RET_FAILED;
	}

	snprintf(cmd, sizeof(cmd), "mac set devaddr %02x%02x%02x%02x", devaddr[0], devaddr[1], devaddr[2], devaddr[3]);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_set_nwkskey(LoRa *self, const char *nwkskey) {
	PARAM_CHECK(nwkskey != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	if (lora->lora_mode != LORA_MODE_ABP) {
		return LORA_RET_FAILED;
	}

	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set nwkskey %s", nwkskey);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_set_appskey(LoRa *self, const char *appskey) {
	PARAM_CHECK(appskey != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	if (lora->lora_mode != LORA_MODE_ABP) {
		return LORA_RET_FAILED;
	}

	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set appskey %s", appskey);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_set_datarate(LoRa *self, uint8_t datarate) {
	char cmd[50] = {0};
	LoraModem *lora = (LoraModem *)self->parent;

	snprintf(cmd, sizeof(cmd), "mac set dr %u", datarate);

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, cmd);
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_get_datarate(LoRa *self, uint8_t *datarate) {
	LoraModem *lora = (LoraModem *)self->parent;

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write_line(lora, "mac get dr");
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	*datarate = atoi(lora->response_str);
	lora_ret_t ret = LORA_RET_FAILED;
	if (*datarate <= 5) {
		ret = LORA_RET_OK;
	}
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_set_adr(LoRa *self, bool adr) {
	LoraModem *lora = (LoraModem *)self->parent;

	command_lock(lora);
	lora_modem_clear_input(lora);
	if (adr) {
		lora_modem_write_line(lora, "mac set adr on");
	} else {
		lora_modem_write_line(lora, "mac set adr off");
	}
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_ret_t ret = lora_response(lora, response_check(lora->response_str));
	command_unlock(lora);

	return ret;
}


static lora_ret_t lora_modem_join(LoRa *self) {
	LoraModem *lora = (LoraModem *)self->parent;

	command_lock(lora);
	lora_modem_clear_input(lora);

	if (lora->lora_mode == LORA_MODE_ABP) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("join (ABP method)"));
		lora_modem_write_line(lora, "mac join abp");
	} else if (lora->lora_mode == LORA_MODE_OTAA) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("join (OTAA method)"));
		lora_modem_write_line(lora, "mac join otaa");
	} else {
		command_unlock(lora);
		return LORA_RET_FAILED;
	}

	/* The command is sent, we are waiting for the response now.
	   Command lock is already acquired. */
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_modem_ret_t ret = response_check(lora->response_str);

	if (ret != LORA_MODEM_RET_OK) {
		command_unlock(lora);
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("network join failed"));
		return lora_response(lora, ret);
	}

	/* Wait for "accepted" or "denied" */
	for (int i = 0; i < 100; i++) {
		lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
		if (lora->response_len == 0) {
			continue;
		}

		/* A response was received, return immediatelly */
		ret = response_check(lora->response_str);
		command_unlock(lora);

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("network join ret = %d '%s'"), ret, lora->response_str);
		return lora_response(lora, ret);
	}

	/* Nothing was received so far. */
	command_unlock(lora);
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("network join timeout"));
	return LORA_RET_FAILED;
}


/* Such code. It is 4 AM. */
static lora_modem_ret_t parse_rx_data(LoraModem *self) {
	char *s = self->response_str;
	size_t len = self->response_len;
	if (len < 11) {
		/* There is at least "mac_rx N XX". No need to control remaining chars anymore. */
		return LORA_MODEM_RET_FAILED;
	}

	if (strncmp(self->response_str, "mac_rx", 6)) {
		return LORA_MODEM_RET_FAILED;
	}

	/* Eat "mac_rx" and the following space. */
	s += 7; len -= 7;

	/* A decimal port number follows. */
	self->rx_data_port = (*s) - '0';
	s++; len--;

	if (*s != ' ') {
		/* Not finished yet, there's a second digit. */
		self->rx_data_port = self->rx_data_port * 10 + (*s) - '0';
		s++; len--;
	}

	/* No more digits. There should be a space. Check. */
	if (*s != ' ') {
		return LORA_MODEM_RET_FAILED;
	}

	/* Eat the space */
	s++; len--;

	if (len % 2) {
		/* Odd number of chars remains. The hex string is corrupted. */
		return LORA_MODEM_RET_FAILED;
	}
	self->rx_data_len = len / 2;

	for (size_t i = 0; i < self->rx_data_len; i++) {
		self->rx_data[i] = (uint8_t)hex2uint(s, 2);
		s += 2;
	}

	return LORA_MODEM_RET_OK;
}


static lora_ret_t lora_modem_send(LoRa *self, uint8_t port, const uint8_t *data, size_t len) {
	PARAM_CHECK(data != NULL, LORA_RET_BAD_PARAM);
	LoraModem *lora = (LoraModem *)self->parent;

	(void)len;
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac tx uncnf %lu ", (uint32_t)port);

/** @todo no LED support yet */
/*
	if (lora->led) {
		lora->led->pause = 100;
		lora->led->status_code = 0x4;
	}
*/

	command_lock(lora);
	lora_modem_clear_input(lora);
	lora_modem_write(lora, cmd);
	lora_modem_write_line(lora, (const char *)data);

	/* Check the first response. Theres a couple of possible responses meaning errors.
	 * Continue if the response is "ok". */
	lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
	lora_modem_ret_t ret = response_check(lora->response_str);
	if (ret != LORA_MODEM_RET_OK) {
		command_unlock(lora);
		return lora_response(lora, ret);
	}

	/* Continue if the packet was forwarded to the MAC */
	uint32_t timeout = LORA_TX_OK_TIMEOUT;
	while (true) {
		/* If the packet from the server is sent without an ACK request, RN2483 never answers. */
		lora_modem_read_line(lora, lora->response_str, LORA_RESPONSE_LEN, &lora->response_len);
		if (lora->response_len == 0) {
			if (timeout == 0) {
				command_unlock(lora);
				return LORA_RET_FAILED;
			} else {
				timeout--;
				continue;
			}
		}
		ret = response_check(lora->response_str);

		if (ret == LORA_MODEM_RET_RX) {
			/* Downlink data received. */
			if (parse_rx_data(lora) == LORA_MODEM_RET_OK) {
				if (lora->rx_lock[lora->rx_data_port] != NULL) {
					xSemaphoreGive(lora->rx_lock[lora->rx_data_port]);
					/* Wait with a timeout to avoid waiting forever
					 * if the receiver stopped calling receive. */
					xSemaphoreTake(lora->rx_data_release, 100);
				}
			}
			/* We can safely read another line here. */
			continue;
		} else {
			/* Return in all other cases. Handle OK transmission. */
			if (ret == LORA_MODEM_RET_TX_OK) {
				ret = LORA_MODEM_RET_OK;
			}
			command_unlock(lora);
			return lora_response(lora, ret);
		}
	}
	/* Unreachable */
	command_unlock(lora);
	return lora_response(lora, ret);
}


static lora_ret_t lora_modem_receive(LoRa *self, uint8_t port, uint8_t *data, size_t size, size_t *len, uint32_t timeout) {
	LoraModem *lora = (LoraModem *)self->parent;

	if (port > 15) {
		return LORA_RET_FAILED;
	}

	/* Calling RX on this port for the first time. Create the semaphore first. */
	if (lora->rx_lock[port] == NULL) {
		lora->rx_lock[port] = xSemaphoreCreateBinary();
		if (lora->rx_lock[port] == pdFALSE) {
			return LORA_RET_FAILED;
		}
	}

	/* Wait on a semaphore corresponding to this port. */
	if (xSemaphoreTake(lora->rx_lock[port], timeout) == pdFALSE) {
		return LORA_RET_TIMEOUT;
	}

	/* The semaphore was signalled. It means the full response is saved in the
	 * rx_data and we may process it. Signal rx_data_release to continue. */
	if (lora->rx_data_len > size) {
		lora->rx_data_len = size;
	}
	*len = lora->rx_data_len;
	memcpy(data, lora->rx_data, lora->rx_data_len);

	xSemaphoreGive(lora->rx_data_release);
	return LORA_RET_OK;
}


