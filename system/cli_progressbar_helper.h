#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#pragma once

void cli_progressbar_render(char *buf, size_t buf_size, size_t bar_size, size_t progress, size_t total);
void cli_progressbar_render_simple(char *buf, size_t buf_size, size_t bar_size, size_t progress, size_t total);

