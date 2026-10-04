/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic power management service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>

#include "pm-generic.h"

#define MODULE_NAME "pm-generic"

#define PM_GENERIC_TASK_STACK (configMINIMAL_STACK_SIZE + 256)
#define PM_GENERIC_TASK_PRIORITY 1
#define PM_GENERIC_TICK_MS 1000


/**********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

/* The power-state-to-name mapping declared in the interface, materialized into our own const storage. */
static const struct pm_state_name pm_state_names[] = PM_STATE_NAMES;


/* Human-readable name of a power state, or "?" for one not in the table (never NULL, so it is always safe to
 * pass to a log format string). */
static const char *pm_generic_state_name(enum pm_state state) {
	for (const struct pm_state_name *n = pm_state_names; n->name != NULL; n++) {
		if (n->state == state) {
			return n->name;
		}
	}
	return "?";
}


static struct pm_generic_state *pm_generic_find_state(PmGeneric *self, enum pm_state state) {
	for (size_t i = 0; i < self->state_count; i++) {
		if (self->states[i].conf.state == state) {
			return &self->states[i];
		}
	}
	return NULL;
}


/*
 * States are ordered by their enum value in pm.h: a lower value means a higher power level (PM_STATE_D0
 * is the highest power one). The manager keeps the system in the highest-power state that is currently
 * needed, which these helpers derive from the lock bitmaps.
 */

/* The state the manager should hold: the highest-power locked state (lowest enum value), or the
 * lowest-power configured state (highest enum value) when nothing is locked. */
static struct pm_generic_state *pm_generic_target(PmGeneric *self) {
	struct pm_generic_state *locked = NULL;
	struct pm_generic_state *lowest = NULL;
	for (size_t i = 0; i < self->state_count; i++) {
		struct pm_generic_state *s = &self->states[i];
		if (s->locks != 0 && (locked == NULL || s->conf.state < locked->conf.state)) {
			locked = s;
		}
		if (lowest == NULL || s->conf.state > lowest->conf.state) {
			lowest = s;
		}
	}
	return locked != NULL ? locked : lowest;
}


/* The configured state one step lower in power than @p from: the smallest enum value still greater
 * than @p from. NULL when @p from is already the lowest configured state. */
static struct pm_generic_state *pm_generic_next_lower(PmGeneric *self, enum pm_state from) {
	struct pm_generic_state *next = NULL;
	for (size_t i = 0; i < self->state_count; i++) {
		struct pm_generic_state *s = &self->states[i];
		if (s->conf.state > from && (next == NULL || s->conf.state < next->conf.state)) {
			next = s;
		}
	}
	return next;
}


/* The configured transition from @p from to @p to, or NULL when the table defines none. */
static const struct pm_generic_transition_conf *pm_generic_find_transition(PmGeneric *self, enum pm_state from,
                                                                           enum pm_state to) {
	if (self->conf.transitions == NULL) {
		return NULL;
	}
	for (const struct pm_generic_transition_conf *t = self->conf.transitions; t->from != PM_STATE_NONE; t++) {
		if (t->from == from && t->to == to) {
			return t;
		}
	}
	return NULL;
}


/* Perform one adjacent transition @p from -> @p to, running its table callback. */
static pm_ret_t pm_generic_step(PmGeneric *self, struct pm_generic_state *from, struct pm_generic_state *to) {
	const struct pm_generic_transition_conf *t = pm_generic_find_transition(self, from->conf.state, to->conf.state);
	if (t != NULL && t->callback != NULL && t->callback(t->ctx) != PM_GENERIC_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("transition from %s to %s failed"),
			pm_generic_state_name(from->conf.state), pm_generic_state_name(to->conf.state));
		return PM_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("transition from %s to %s"),
		pm_generic_state_name(from->conf.state), pm_generic_state_name(to->conf.state));
	self->current_state = to->conf.state;

	/* The entered state starts its lock-free countdown from zero so its own release_timeout is
	 * honoured before it is left in turn. */
	to->free_time = 0;
	return PM_RET_OK;
}


/* Walk and execute the path found in the came_from predecessors, from the source up to @p node, in
 * forward order. Recursion depth is bounded by the number of states (the path is acyclic). */
static pm_ret_t pm_generic_walk(PmGeneric *self, struct pm_generic_state *node) {
	if (node->came_from == node) {
		/* The source state: nothing precedes it. */
		return PM_RET_OK;
	}
	pm_ret_t ret = pm_generic_walk(self, node->came_from);
	if (ret != PM_RET_OK) {
		return ret;
	}
	return pm_generic_step(self, node->came_from, node);
}


/*
 * Move to @p to, consulting only the transition table. If no direct transition exists, a path of
 * several transitions is searched for and taken. Returns PM_RET_FAILED when @p to is unknown or not
 * reachable through the table. Must be called with the mutex held.
 */
static pm_ret_t pm_generic_transition(PmGeneric *self, enum pm_state to) {
	if (self->current_state == to) {
		return PM_RET_OK;
	}

	struct pm_generic_state *src = pm_generic_find_state(self, self->current_state);
	struct pm_generic_state *dest = pm_generic_find_state(self, to);
	if (src == NULL || dest == NULL) {
		return PM_RET_FAILED;
	}

	/* Breadth-first search over the transition graph, recording each reached state's predecessor.
	 * The source points to itself as a sentinel; an unreached state keeps a NULL predecessor. */
	for (size_t i = 0; i < self->state_count; i++) {
		self->states[i].came_from = NULL;
	}
	src->came_from = src;

	bool changed = true;
	while (changed && dest->came_from == NULL) {
		changed = false;
		for (const struct pm_generic_transition_conf *t = self->conf.transitions;
		     t != NULL && t->from != PM_STATE_NONE; t++) {
			struct pm_generic_state *f = pm_generic_find_state(self, t->from);
			struct pm_generic_state *g = pm_generic_find_state(self, t->to);
			if (f != NULL && g != NULL && f->came_from != NULL && g->came_from == NULL) {
				g->came_from = f;
				changed = true;
			}
		}
	}

	if (dest->came_from == NULL) {
		/* No path of transitions leads to the requested state. */
		return PM_RET_FAILED;
	}

	return pm_generic_walk(self, dest);
}


/* Step the active state down one configured level at a time towards the target, resting at each level
 * for its release_timeout, until it reaches the target. Must be called with the mutex held. */
static void pm_generic_settle(PmGeneric *self) {
	while (true) {
		struct pm_generic_state *current = pm_generic_find_state(self, self->current_state);
		struct pm_generic_state *target = pm_generic_target(self);
		if (current == NULL || target == NULL || current->conf.state >= target->conf.state) {
			/* Already at or below the target power level. */
			break;
		}
		if (current->free_time < current->conf.release_timeout) {
			/* Not yet rested long enough at this level. */
			break;
		}
		struct pm_generic_state *next = pm_generic_next_lower(self, current->conf.state);
		if (next == NULL) {
			break;
		}
		if (pm_generic_transition(self, next->conf.state) != PM_RET_OK) {
			/* The transition table has no path down to the next level; stay here. */
			break;
		}
	}
}


/* Called once per second. Accounts the lock-free time of every state, then lets the active state settle
 * down towards the target as the release timeouts expire. */
static void pm_generic_tick(PmGeneric *self) {
	xSemaphoreTake(self->mutex, portMAX_DELAY);

	for (size_t i = 0; i < self->state_count; i++) {
		if (self->states[i].locks == 0) {
			self->states[i].free_time += PM_GENERIC_TICK_MS;
		} else {
			self->states[i].free_time = 0;
		}
	}

	pm_generic_settle(self);

	xSemaphoreGive(self->mutex);
}


static void pm_generic_task(void *param) {
	PmGeneric *self = param;
	while (self->can_run) {
		vTaskDelay(pdMS_TO_TICKS(PM_GENERIC_TICK_MS));
		pm_generic_tick(self);
	}
	self->running = false;
	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Power manager interface implementation
 **********************************************************************************************************************/

static pm_ret_t pm_set_state(Pm *self, enum pm_state state) {
	PmGeneric *pmg = self->parent;

	xSemaphoreTake(pmg->mutex, portMAX_DELAY);
	pm_ret_t ret = pm_generic_transition(pmg, state);
	xSemaphoreGive(pmg->mutex);
	return ret;
}


static pm_ret_t pm_get_state(Pm *self, enum pm_state *state) {
	PmGeneric *pmg = self->parent;

	if (state == NULL) {
		return PM_RET_NULL;
	}

	/* A single aligned word; a lock-free snapshot is enough for a caller that only samples the state. */
	*state = pmg->current_state;
	return PM_RET_OK;
}


static pm_ret_t pm_lock_state(Pm *self, enum pm_state state, PmLock *lock) {
	PmGeneric *pmg = self->parent;

	if (lock == NULL) {
		return PM_RET_NULL;
	}

	struct pm_generic_state *s = pm_generic_find_state(pmg, state);
	if (s == NULL) {
		return PM_RET_FAILED;
	}

	xSemaphoreTake(pmg->mutex, portMAX_DELAY);

	/* Claim the lowest free bit in the state's lock bitmap and describe it in the handle. */
	int bit = __builtin_ffs(~s->locks);
	if (bit == 0) {
		/* No free lock slot left in this state. */
		xSemaphoreGive(pmg->mutex);
		return PM_RET_FAILED;
	}
	lock->mask = (uint32_t)1 << (bit - 1);
	lock->state = state;
	s->locks |= lock->mask;
	s->free_time = 0;

	/* A lock holds the system at least at this power level. Move up to the highest-power locked
	 * state at once, taking whatever path the transition table provides. */
	struct pm_generic_state *target = pm_generic_target(pmg);
	if (target != NULL && target->conf.state < pmg->current_state) {
		pm_generic_transition(pmg, target->conf.state);
	}

	xSemaphoreGive(pmg->mutex);
	return PM_RET_OK;
}


static pm_ret_t pm_release_state(Pm *self, PmLock *lock) {
	PmGeneric *pmg = self->parent;

	if (lock == NULL) {
		return PM_RET_NULL;
	}

	/* The handle names its own state and bit, so clearing the lock is a direct lookup. */
	struct pm_generic_state *s = pm_generic_find_state(pmg, lock->state);
	if (s == NULL) {
		return PM_RET_FAILED;
	}

	xSemaphoreTake(pmg->mutex, portMAX_DELAY);

	/* A zero mask (already released by us) or a bit that is no longer set both mean a double
	 * release. */
	if (lock->mask == 0 || (s->locks & lock->mask) == 0) {
		xSemaphoreGive(pmg->mutex);
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("releasing an already released lock"));
		return PM_RET_FAILED;
	}

	s->locks &= ~lock->mask;

	/* Invalidate the handle so a subsequent release is detected. */
	lock->mask = 0;

	/* Releasing a lock may lower the target. Settle down towards it: a state with a zero
	 * release_timeout is left at once, a non-zero one waits for the service task. */
	pm_generic_settle(pmg);

	xSemaphoreGive(pmg->mutex);
	return PM_RET_OK;
}


static const struct pm_vmt pm_generic_vmt = {
	.pm_set_state = pm_set_state,
	.pm_get_state = pm_get_state,
	.pm_lock_state = pm_lock_state,
	.pm_release_state = pm_release_state,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

pm_generic_ret_t pm_generic_init(PmGeneric *self, const struct pm_generic_conf *conf) {
	if (self == NULL || conf == NULL || conf->states == NULL) {
		return PM_GENERIC_RET_NULL;
	}

	memset(self, 0, sizeof(PmGeneric));
	memcpy(&self->conf, conf, sizeof(struct pm_generic_conf));

	/* Count the configured states up to the PM_STATE_NONE terminator. */
	while (conf->states[self->state_count].state != PM_STATE_NONE) {
		self->state_count++;
	}

	/* Copy the configured states into a runtime representation carrying the per-state lock bitmap. */
	self->states = malloc(self->state_count * sizeof(struct pm_generic_state));
	if (self->states == NULL) {
		return PM_GENERIC_RET_FAILED;
	}
	for (size_t i = 0; i < self->state_count; i++) {
		memset(&self->states[i], 0, sizeof(struct pm_generic_state));
		memcpy(&self->states[i].conf, &conf->states[i], sizeof(struct pm_generic_state_conf));
	}

	self->mutex = xSemaphoreCreateMutex();
	if (self->mutex == NULL) {
		free(self->states);
		self->states = NULL;
		return PM_GENERIC_RET_FAILED;
	}

	self->pm.parent = self;
	self->pm.vmt = &pm_generic_vmt;

	self->current_state = conf->default_state;

	/* Start the task accounting lock-free time and taking the release timeouts. */
	self->can_run = true;
	self->running = true;
	if (xTaskCreate(pm_generic_task, "pm-generic", PM_GENERIC_TASK_STACK, self, PM_GENERIC_TASK_PRIORITY,
	                &self->task) != pdPASS) {
		self->running = false;
		vSemaphoreDelete(self->mutex);
		self->mutex = NULL;
		free(self->states);
		self->states = NULL;
		return PM_GENERIC_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized with %u power state(s)"),
		(unsigned int)self->state_count);

	return PM_GENERIC_RET_OK;
}


pm_generic_ret_t pm_generic_free(PmGeneric *self) {
	if (self == NULL) {
		return PM_GENERIC_RET_NULL;
	}

	/* Ask the task to stop and wait until it has left before freeing the resources it uses. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	vSemaphoreDelete(self->mutex);
	self->mutex = NULL;

	free(self->states);
	self->states = NULL;
	self->state_count = 0;

	return PM_GENERIC_RET_OK;
}


pm_generic_ret_t pm_generic_get_pm(PmGeneric *self, Pm **pm) {
	if (self == NULL || pm == NULL) {
		return PM_GENERIC_RET_NULL;
	}

	*pm = &self->pm;
	return PM_GENERIC_RET_OK;
}
