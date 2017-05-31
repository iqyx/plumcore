#include "FreeRTOS.h"
#include "task.h"

static int32_t ucli_system_processes_print_table(struct treecli_parser *parser, void *exec_context, uint32_t *lines) {
	(void)exec_context;

	uint32_t task_cnt = uxTaskGetNumberOfTasks();
	TaskStatus_t *task_status = malloc(task_cnt * sizeof(TaskStatus_t));
	if (task_status == NULL) {
		return -1;
	}

	task_cnt = uxTaskGetSystemState(task_status, task_cnt, NULL);

	uint32_t total_runtime = 0;
	for (uint32_t i = 0; i < task_cnt; i++) {
		total_runtime += task_status[i].ulRunTimeCounter;
	}

	if (total_runtime > 0) {

		module_cli_output(
			ESC_ERASE_LINE_END "\r\n"
			ESC_ERASE_LINE_END "    handle                name   task_id     state  prio bprio   stack     counter  cpu%\r\n"
			ESC_ERASE_LINE_END "----------------------------------------------------------------------------------------\r\n",
			parser->context
		);
		(*lines) += 3;

		for (uint32_t i = 0; i < task_cnt; i++) {
			char *state = "";
			char *task_esc = "";
			switch (task_status[i].eCurrentState) {
				case eReady:
					state = "ready";
					task_esc = ESC_BOLD;
					break;

				case eRunning:
					state = "running";
					task_esc = ESC_BOLD;
					break;

				case eBlocked:
					state = "blocked";
					break;

				case eSuspended:
					state = "suspended";
					break;

				case eDeleted:
					state = "deleted";
					task_esc = ESC_COLOR_FG_RED;
					break;

				default:
					state = "?";
					break;
			}

			uint8_t percent = (task_status[i].ulRunTimeCounter * 100) / total_runtime;
			char line[160];
			snprintf(line, sizeof(line),
				ESC_ERASE_LINE_END "%s0x%08x%20s%10u%10s%6u%6u%8u%12u%5u%%%s\r\n",
				task_esc,
				(unsigned int)task_status[i].xHandle,
				task_status[i].pcTaskName,
				(unsigned int)task_status[i].xTaskNumber,
				state,
				(unsigned int)task_status[i].uxCurrentPriority,
				(unsigned int)task_status[i].uxBasePriority,
				(unsigned int)task_status[i].usStackHighWaterMark,
				(unsigned int)task_status[i].ulRunTimeCounter,
				(unsigned int)percent,
				ESC_DEFAULT
			);
			module_cli_output(line, parser->context);
			(*lines)++;
		}
	}

	free(task_status);
	return 0;
}

static int32_t ucli_system_processes_print(struct treecli_parser *parser, void *exec_context) {

	uint32_t lines = 0;

	if (ucli_system_processes_print_table(parser, exec_context, &lines) != 0) {
		return -1;
	}

	return 0;
}

static int32_t ucli_system_processes_monitor(struct treecli_parser *parser, void *exec_context) {

	ServiceCli *cli = (ServiceCli *)parser->context;
	uint32_t lines = 0;

	while (1) {
		/* Go to the start of the table. */
		for (uint32_t j = 0; j < lines; j++) {
			module_cli_output(ESC_CURSOR_UP, parser->context);
		}

		lines = 0;
		if (ucli_system_processes_print_table(parser, exec_context, &lines) != 0) {
			return -1;
		}

		uint8_t chr = 0;
		int16_t read = interface_stream_read_timeout(cli->stream, &chr, 1, 1000);
		if (read != 0) {
			break;
		}
	}

	return 0;
}


