/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: 2D painter interface (`Painter`) — cross-module producer helper
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "wren.h"
#include <interfaces/painter.h>

/* Wrap a borrowed Painter interface pointer in a fresh `Painter` Wren instance and leave it in slot 0
 * (the return slot of the calling foreign method). A concrete painter module (e.g. fb-painter) calls
 * this from its `.painter` getter to hand the interface to the script, mirroring how C returns
 * &self->painter. The wrapper only borrows the pointer, so the object that owns it must outlive the
 * returned Painter. The caller's Wren module must have imported "painter" so the class is loaded. */
void wren_module_painter_wrap(WrenVM *vm, Painter *painter);
