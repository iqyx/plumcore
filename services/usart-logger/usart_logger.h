/*
 * Copyright (c) 2016, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "interfaces/log.h"


#define ESC_CURSOR_UP "\x1b[A"
#define ESC_CURSOR_DOWN "\x1b[B"
#define ESC_CURSOR_RIGHT "\x1b[C"
#define ESC_CURSOR_LEFT "\x1b[D"
#define ESC_DEFAULT "\x1b[0m"
#define ESC_BOLD "\x1b[1m"
#define ESC_CURSOR_SAVE "\x1b[s"
#define ESC_CURSOR_RESTORE "\x1b[u"
#define ESC_ERASE_LINE_END "\x1b[K"
#define ESC_COLOR_FG_BLACK "\x1b[30m"
#define ESC_COLOR_FG_RED "\x1b[31m"
#define ESC_COLOR_FG_GREEN "\x1b[32m"
#define ESC_COLOR_FG_YELLOW "\x1b[33m"
#define ESC_COLOR_FG_BLUE "\x1b[34m"
#define ESC_COLOR_FG_MAGENTA "\x1b[35m"
#define ESC_COLOR_FG_CYAN "\x1b[36m"
#define ESC_COLOR_FG_WHITE "\x1b[37m"

typedef struct usart_logger {
	ILog log_iface;
	uint32_t port;
} UsartLogger;


void usart_logger_init(UsartLogger *self, uint32_t port);
ILog *usart_logger_iface(UsartLogger *self);

