# Jobs

A job is a long-running task that is expected to finish, such as a firmware update, a flash erase or a measurement
campaign. A job runs for seconds to hours, reports its progress and can be suspended and resumed. Tasks that never
finish are daemons and are implemented as services instead.

Jobs implement the `Job` interface (`<interfaces/job.h>`). They are managed by a job manager rather than advertised
in the service locator, because they come and go during runtime.

## States and results

A job is always in one of three states:

| State | Meaning |
|---|---|
| `JOB_STATE_SCHEDULED` | Not running, ready to be started. |
| `JOB_STATE_RUNNING` | Being executed. |
| `JOB_STATE_SUSPENDED` | Suspended, can be resumed without losing its state. |

```
            start               pause
SCHEDULED ---------> RUNNING ----------> SUSPENDED
    ^                 |    ^               |
    |  completed,     |    +---------------+
    |  failed or      |         resume
    |  cancelled      |
    +-----------------+
```

When a run ends, the job returns to `JOB_STATE_SCHEDULED` and is ready for the next run.
The outcome is kept as the job result until another run ends:

| Result | Meaning |
|---|---|
| `JOB_RESULT_NONE` | No run has ended yet. |
| `JOB_RESULT_SUCCESSFUL` | The last run completed successfully. |
| `JOB_RESULT_FAILED` | The last run failed, an error message may be available. |
| `JOB_RESULT_CANCELLED` | The last run was cancelled. |

## Methods

- `start` starts a new run of a scheduled job.
- `pause` and `resume` suspend and resume a running job.
- `cancel` ends a running or suspended job prematurely, its execution state is discarded.
- `progress` returns the progress as `done` out of `total`.
- `get_state` returns the current state.
- `get_result` returns the result of the last ended run and fills a buffer with an error message (an empty string if
  there is none).

## Running a job

The job must be fully prepared (its parameters set) before it is started. The caller then starts it, watches the
progress until the job returns to the scheduled state and reads the result:

```c
static job_ret_t run_job(Job *job) {
	if (job->vmt->start(job) != JOB_RET_OK) {
		return JOB_RET_FAILED;
	}

	enum job_state state = JOB_STATE_RUNNING;
	while (job->vmt->get_state(job, &state) == JOB_RET_OK && state != JOB_STATE_SCHEDULED) {
		uint32_t total = 0;
		uint32_t done = 0;
		if (job->vmt->progress(job, &total, &done) == JOB_RET_OK) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("progress %lu/%lu"), done, total);
		}
		vTaskDelay(1000);
	}

	enum job_result result = JOB_RESULT_NONE;
	char msg[64];
	if (job->vmt->get_result(job, &result, msg, sizeof(msg)) != JOB_RET_OK) {
		return JOB_RET_FAILED;
	}
	if (result != JOB_RESULT_SUCCESSFUL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("job did not succeed: %s"), msg);
		return JOB_RET_FAILED;
	}
	return JOB_RET_OK;
}
```

A recurring job is run simply by calling `start` again once it is back in the scheduled state. The result
of the previous run stays available until the new run ends.
