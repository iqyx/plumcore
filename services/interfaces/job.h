/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Job definition/interface
 *
 * Copyright (c) 2018-2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/*
 * Various long running tasks in plumCore, whether one shot or recurring,
 * share some common attributes:
 *   - execution time in the range of several seconds to hours
 *   - a steadily progressing state, stopping the job when reaching 100%
 *   - ability to be paused without losing the execution state
 *   - start/restart functionality for recurring jobs
 *
 * Tasks which never complete during runtime are considered daemons, not jobs.
 * They are implemented as services.
 *
 * As jobs are designed to be started and completed during runtime, managing
 * and discovering them using a service locator service is not optimal and
 * discouraged, although ossible if the service locator implements removing
 * interfaces from the list during runtime. Instead, a job manager service
 * exists for this purpose.
 */

#pragma once

#include <stdint.h>


typedef enum job_ret {
	JOB_RET_OK = 0,
	JOB_RET_FAILED,
	JOB_RET_NULL,
} job_ret_t;

typedef enum job_state {
	JOB_STATE_SCHEDULED = 0,
	JOB_STATE_RUNNING,
	JOB_STATE_COMPLETED,
	JOB_STATE_CANCELLED,
	JOB_STATE_SUSPENDED,
} job_state_t;

typedef struct job Job;
struct job_vmt {
	/**
	 * @brief Start a new job
	 *
	 * The job must be sufficiently prepared, it must know its parameters
	 * before the start method is called. Either by setting them beforehand
	 * or the job must discover them on start.
	 *
	 * The purpose of this function is to allow the job manager to start
	 * scheduled job when there are enough resources available or when
	 * it is defined as recurring.
	 */
	job_ret_t (*start)(Job *self);

	/**
	 * @brief Cancal a running job
	 *
	 * A job cannot be stopped. It can be left runnng until it completes or
	 * cancelled prematurely forgetting its state.
	 */
	job_ret_t (*cancel)(Job *self);

	/**
	 * @brief Pause a running job
	 */
	job_ret_t (*pause)(Job *self);

	/**
	 * @brief Resume a previously paused job
	 */
	job_ret_t (*resume)(Job *self);

	/**
	 * @brief Get the current job progress
	 *
	 * A job progress is computed as @p done / @p total. The @p done and
	 * @p total values must be provided by the job.
	 */
	job_ret_t (*progress)(Job *self, uint32_t *total, uint32_t *done);
};

typedef struct job {
	const struct job_vmt *vmt;
	void *parent;
} Job;


