/**
 * uMeshFw HAL module
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
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

#ifndef _HAL_MODULE_H_
#define _HAL_MODULE_H_

#include <stdint.h>
#include <stdlib.h>

/**
 * uMeshFw modules are used to logically split different parts of the uMeshFw
 * firmware into self-contained groups. Each module may contain:
 *   * module API. It is used to create and initialize module instances,
 *     initialize all processes and module interfaces. Usually, module initializer
 *     accepts a module instance name as a parameter. Other parameters may be added
 *     if they are required at the time of initialization.
 *   * module interfaces. Each module can expose multiple different interfaces
 *     which are initialized in the module initializer. Each interface is then
 *     connected to functions inside the module implementing the required
 *     interface functionality.
 *   * module processes. If a module needs a "service process" (ie. a long-lived
 *     process which handles all module logic), it can create a process descriptor
 *     and publish it. Global uMeshFw process manager will then manage this process.
 *     Process will be started/stopped, respawned (in case of an error, stack overflow,
 *     failed allocation, etc) automatically.
 */


/**
 * HAL module API virtual methods table. Methods are all initialized to NULL by
 * default. Module functionality can be implemented by setting the pointers
 * to point to proper functions. All functions will be called with a context
 * pointer passed as an argument.
 *
 * TODO: remove from here.
 */
struct hal_module_vmt {

	void *context;
};

/**
 * This structure contains all required things to describe a single uMeshFw module.
 */
struct hal_module_descriptor {
	const char *name;

	/**
	 * This should point to the structure with all shared resources that the
	 * module uses. It includes this structure, all process descriptors,
	 * interface descriptors, shared buffers, process stacks and other things.
	 * This information is used when the MPU restricts accesses of the module
	 * to its own data only.
	 */
	void *module_shm;
	size_t module_shm_size;
};


/**
 * @brief Initialize a module descriptor.
 *
 * This function initializes just the module descriptor, not the whole module
 * itself. The module must implement its own initialization function which
 * calls the hal_module_descriptor_init function.
 *
 * Module descriptor must not be used without initialization. It must be called
 * before any other action with the descriptor can be done (eg. module registration).
 *
 * @param d A module descriptor to initialize. Cannot be NULL.
 * @param name Name of the module instance.
 *
 * @return HAL_MODULE_DESCRIPTOR_INIT_OK if the descriptor was properly initialized
 *                                       and all required resources were allocated or
 *         HAL_MODULE_DESCRIPTOR_INIT_FAILED otherwise.
 */
int32_t hal_module_descriptor_init(struct hal_module_descriptor *d, const char *name);
#define HAL_MODULE_DESCRIPTOR_INIT_OK 0
#define HAL_MODULE_DESCRIPTOR_INIT_FAILED -1

/**
 * @brief Set module shared memory region.
 *
 * Each module has associated a dedicated memory region which it should use to
 * store all required variables, interface/module/process descriptors, etc.
 * This information is used by the process manager if it is configured to to run
 * the process in an unprivileged mode. Memory protection is then enforced by
 * the MPU and the process is not allowed to read or write memory allocated to
 * other modules or the uMeshFw system memory.
 *
 * Each module must call this function during its initialization to inform the
 * module descriptor about its shared memory region.
 *
 * @param d A module descriptor to set shared memory region information on.
 * @param addr Starting address of the shared memory region. Cannot be NULL.
 * @param len Length of the memory region. Must be nonzero. Constraints on
 *            the addr/length are enforced according to MPU requirements.
 *
 * @return MODULE_DESCRIPTOR_SET_SHM_OK if the shared memory region was set
 *                                      successfully or
 *         MODULE_DESCRIPTOR_SET_SHM_FAILED otherwise.
 */
int32_t hal_module_descriptor_set_shm(struct hal_module_descriptor *d, void *addr, size_t len);
#define HAL_MODULE_DESCRIPTOR_SET_SHM_OK 0
#define HAL_MODULE_DESCRIPTOR_SET_SHM_FAILED -1


#endif

