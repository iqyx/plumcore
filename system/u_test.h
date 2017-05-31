/**
 * uMeshFw simple unit testing framework
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

#ifndef _U_TEST_H_
#define _U_TEST_H_

#include <stdbool.h>
/** @todo Should the lineedit dependency be removed? Tests cannot be run without
 *        the CLI which needs lineedit. */
#include "lineedit.h"


#define U_TEST_OK ESC_COLOR_FG_GREEN ESC_BOLD "OK" ESC_DEFAULT
#define U_TEST_FAILED ESC_COLOR_FG_RED ESC_BOLD "Failed" ESC_DEFAULT

/**
 * Call the u_test_check function with the test invocation function name as the
 * first argument (used as a name of the test) and its result as the second
 * argument.
 */
#define u_test(test) (u_test_check(#test, test))

/**
 * The testing function prints the result to the debug log and returns it as a
 * bool value. This allows tests to be grouped together.
 */
bool u_test_check(const char *name, bool result);


#endif
