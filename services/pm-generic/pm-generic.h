/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic power management service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <interfaces/pm.h>


typedef enum {
	PM_GENERIC_RET_OK = 0,
	PM_GENERIC_RET_FAILED,
	PM_GENERIC_RET_NULL,
} pm_generic_ret_t;


/*
 * A single power state the service supports. The user provides an array of these in the
 * configuration, terminated with an entry whose @p state is PM_STATE_NONE. Not typedef'd per
 * project policy.
 */
struct pm_generic_state_conf {
	enum pm_state state;

	/* Milliseconds the state must stay lock-free before the manager falls back from it to the next
	 * lower-power configured state. Zero falls back without delay. Resolution is the service tick. */
	uint32_t release_timeout;
};


/*
 * A transition between two adjacent power states and the callback performing it. The manager always
 * moves one configured power level at a time, so a transition is only ever needed between neighbours.
 * Not typedef'd per project policy.
 */
struct pm_generic_transition_conf {
	enum pm_state from;
	enum pm_state to;

	/* Performs the @p from -> @p to transition. May be NULL for a transition that needs no action.
	 * The @p ctx pointer is passed back verbatim. */
	pm_generic_ret_t (*callback)(void *ctx);
	void *ctx;
};


/* Service configuration. Not typedef'd per project policy. */
struct pm_generic_conf {
	/* Array of supported power states, terminated with a PM_STATE_NONE entry. */
	const struct pm_generic_state_conf *states;

	/* Array of transitions between adjacent states, terminated with a PM_STATE_NONE from entry. */
	const struct pm_generic_transition_conf *transitions;

	/* Power state the manager starts in after initialization. */
	enum pm_state default_state;
};


/*
 * Runtime copy of a single configured power state, built in init(). Not typedef'd per project
 * policy.
 */
struct pm_generic_state {
	struct pm_generic_state_conf conf;

	/* Bitmap of acquired locks holding this state. Each set bit is one lock; while any bit is
	 * set the state is locked and must not be left. A PmLock handed to a client records which
	 * bit it owns here. */
	uint32_t locks;

	/* Milliseconds this state has been continuously lock-free, counted by the service task. Reset
	 * whenever a lock is held. Drives the release_timeout. */
	uint32_t free_time;

	/* Scratch predecessor pointer used while searching the transition graph for a path. */
	struct pm_generic_state *came_from;
};


typedef struct pm_generic {
	/* Power manager interface exposed to clients. */
	Pm pm;

	struct pm_generic_conf conf;

	/* Runtime copy of the configured states, allocated in init(). */
	struct pm_generic_state *states;
	size_t state_count;

	/* The state the manager is currently in. */
	enum pm_state current_state;

	/* Serializes the state transitions and the lock bitmap updates. */
	SemaphoreHandle_t mutex;

	/* Task counting the per-state lock-free time and taking the release timeouts. */
	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;
} PmGeneric;


/**
 * @brief Initialize the generic power management service
 *
 * The configuration and the state array it references are copied into a runtime representation, so
 * the caller's configuration need not outlive this call. The transition table, the allowed lists and
 * the callback @p ctx pointers are retained by reference and must stay valid for the service lifetime.
 *
 * @param self The instance to initialize. Must be allocated beforehand.
 * @param conf Service configuration. Copied into the instance.
 *
 * @return PM_GENERIC_RET_NULL if a required argument is NULL, PM_GENERIC_RET_FAILED on error or
 *         PM_GENERIC_RET_OK otherwise.
 */
pm_generic_ret_t pm_generic_init(PmGeneric *self, const struct pm_generic_conf *conf);


pm_generic_ret_t pm_generic_free(PmGeneric *self);


/**
 * @brief Get the power manager interface exposed by the service
 *
 * @param pm Returns the Pm interface clients use to request power states and locks.
 */
pm_generic_ret_t pm_generic_get_pm(PmGeneric *self, Pm **pm);
