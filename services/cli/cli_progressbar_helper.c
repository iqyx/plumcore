#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cli_progressbar_helper.h"

static const char *progressbar_chars[] = {
	" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"
};

void cli_progressbar_render(char *buf, size_t buf_size, size_t bar_size, size_t progress, size_t total) {
	strlcpy(buf, "", buf_size);
	for (size_t i = 0; i < (progress * bar_size / total); i++) {
		strlcat(buf, progressbar_chars[8], buf_size);
	}

}


void cli_progressbar_render_simple(char *buf, size_t buf_size, size_t bar_size, size_t progress, size_t total) {
	size_t chars = progress * bar_size / total;
	snprintf(buf, buf_size, "%3u%% [", progress * 100 / total);

	for (size_t i = 0; i < chars; i++) {
		strlcat(buf, "#", buf_size);
	}
	for (size_t i = 0; i < (bar_size - chars); i++) {
		strlcat(buf, " ", buf_size);
	}
	strlcat(buf, "]", buf_size);
}
