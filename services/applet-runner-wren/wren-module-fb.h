/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: framebuffer (`Fb`) — cross-module accessor
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "wren.h"
#include <interfaces/fb.h>

/* Extract the borrowed framebuffer pointer wrapped by the `Fb` Wren instance sitting in [slot]. Other
 * modules (e.g. painter) use this to build on a framebuffer the script hands them, without knowing the
 * Fb wrapper's private foreign layout. Returns NULL when the instance wraps no framebuffer. The slot
 * must actually hold an Fb foreign instance. */
Fb *wren_module_fb_unwrap(WrenVM *vm, int slot);
