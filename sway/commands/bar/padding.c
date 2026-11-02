#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include "sway/commands.h"
#include "log.h"

static const char expected_syntax[] =
	"Expected 'bar [<bar-id>] padding <all> | <vertical> <horizontal> | "
	"<top> <horizontal> <bottom> | <top> <right> <bottom> <left>', "
	"each a non-negative number of pixels with an optional 'px'";

// A non-negative integer with an optional px suffix, like i3bar
static bool parse_padding_value(const char *arg, int *value) {
	if (*arg < '0' || *arg > '9') {
		return false;
	}
	char *end;
	errno = 0;
	long parsed = strtol(arg, &end, 10);
	if (errno != 0 || parsed > INT_MAX ||
			(*end != '\0' && strcasecmp(end, "px") != 0)) {
		return false;
	}
	*value = (int)parsed;
	return true;
}

struct cmd_results *bar_cmd_padding(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "padding", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if ((error = checkarg(argc, "padding", EXPECTED_AT_MOST, 4))) {
		return error;
	}

	int values[4];
	for (int i = 0; i < argc; i++) {
		if (!parse_padding_value(argv[i], &values[i])) {
			return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
		}
	}

	// The same shorthand as CSS and i3bar. At run time, cmd_bar sends the
	// barconfig_update event.
	struct side_gaps *padding = &config->current_bar->padding;
	padding->top = values[0];
	padding->right = argc > 1 ? values[1] : values[0];
	padding->bottom = argc > 2 ? values[2] : values[0];
	padding->left = argc > 3 ? values[3] : padding->right;

	sway_log(SWAY_DEBUG, "Setting bar padding to %d %d %d %d on bar: %s",
			padding->top, padding->right, padding->bottom, padding->left,
			config->current_bar->id);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
