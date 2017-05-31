/**
 * umesh custom assert
 *
 * Copyright (C) 2014, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _U_ASSERT_H_
#define _U_ASSERT_H_

#define assert u_assert

/* standard system assertion support */
#define u_assert(e) ((e) ? (0) : u_assert_func(#e, __FILE__, __LINE__))
#define u_assert_ret(e, r) if (u_assert(e)) return r;

int u_assert_func(const char *expr, const char *fname, int line);

#endif
