/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Job definition/interface
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/*
 * Various long-running tasks in plumCore, whether one-shot or recurring, share some common attributes:
 *   - execution time ranging from several seconds to hours
 *   - steady progress, the job completes when it reaches 100 %
 *   - the ability to be suspended without losing the execution state
 *   - the ability to be started again (recurring jobs)
 *
 * Tasks which never complete during runtime are considered daemons, not jobs. They are implemented as services.
 *
 * A job is always in one of the following states:
 *   - scheduled: the job is not running and is ready to be started
 *   - running: the job is being executed
 *   - suspended: the job execution is suspended, it can be resumed later without losing its state
 *
 * When a run ends, either by completing the work or by being cancelled, the job returns to the scheduled state
 * and is ready for the next run. The outcome of the run is kept as the job result, together with an optional
 * error message, until another run ends.
 *
 * As jobs are designed to be started and completed during runtime, managing and discovering them using
 * the service locator is not optimal and is discouraged, although it is possible if the service locator
 * supports removing interfaces during runtime. A job manager service exists for this purpose instead.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


typedef enum job_ret {
	JOB_RET_OK = 0,
	JOB_RET_FAILED,
	JOB_RET_NULL,
} job_ret_t;

enum job_state {
	/** Not running, ready to be started. A job returns to this state after each run. */
	JOB_STATE_SCHEDULED = 0,
	/** The job is being executed. */
	JOB_STATE_RUNNING,
	/** The execution is suspended and can be resumed without losing the job state. */
	JOB_STATE_SUSPENDED,
};

enum job_result {
	/** No run has ended yet. */
	JOB_RESULT_NONE = 0,
	/** The last run completed successfully. */
	JOB_RESULT_SUCCESSFUL,
	/** The last run failed. An error message describing the failure may be available. */
	JOB_RESULT_FAILED,
	/** The last run was cancelled before completion. */
	JOB_RESULT_CANCELLED,
};

typedef struct job Job;
struct job_vmt {
	/**
	 * @brief Start a new run of a scheduled job
	 *
	 * The job must be sufficiently prepared before the start method is called: its parameters must be either set
	 * beforehand or discovered by the job on start.
	 *
	 * The purpose of this method is to allow the job manager to start a scheduled job when enough resources
	 * are available or when the job is defined as recurring.
	 *
	 * @return JOB_RET_OK if the job was started and is now running,
	 *         JOB_RET_FAILED if the job is not in the scheduled state or cannot be started.
	 */
	job_ret_t (*start)(Job *self);

	/**
	 * @brief Cancel a running or suspended job
	 *
	 * A job cannot be stopped. It can either be left running until it completes or be cancelled prematurely,
	 * discarding its execution state. A cancelled job returns to the scheduled state with
	 * the JOB_RESULT_CANCELLED result.
	 */
	job_ret_t (*cancel)(Job *self);

	/**
	 * @brief Suspend a running job
	 *
	 * The job moves to the suspended state, keeping its execution state.
	 */
	job_ret_t (*pause)(Job *self);

	/**
	 * @brief Resume a previously suspended job
	 *
	 * The job moves back to the running state and continues where it was suspended.
	 */
	job_ret_t (*resume)(Job *self);

	/**
	 * @brief Get the current job progress
	 *
	 * The job progress is computed as @p done / @p total. Both values must be provided by the job.
	 */
	job_ret_t (*progress)(Job *self, uint32_t *total, uint32_t *done);

	/**
	 * @brief Get the current job state
	 *
	 * @param state Current state of the job (scheduled, running or suspended)
	 */
	job_ret_t (*get_state)(Job *self, enum job_state *state);

	/**
	 * @brief Get the result of the last ended run
	 *
	 * The result is JOB_RESULT_NONE until the first run ends. It is kept unchanged while a new run is in
	 * progress and is replaced when that run ends.
	 *
	 * @param result Outcome of the last ended run
	 * @param msg Buffer filled with a null-terminated error message, truncated to fit. An empty string is
	 *            returned if there is no message. May be NULL if the message is not required.
	 * @param msg_size Size of the @p msg buffer in bytes
	 */
	job_ret_t (*get_result)(Job *self, enum job_result *result, char *msg, size_t msg_size);
};

typedef struct job {
	const struct job_vmt *vmt;
	void *parent;
} Job;
