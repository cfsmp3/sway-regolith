#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <json.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/criteria.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/tree/view.h"
#include "stringop.h"
#include "log.h"

// Returns error object, or NULL if check succeeds.
struct cmd_results *checkarg(int argc, const char *name, enum expected_args type, int val) {
	const char *error_name = NULL;
	switch (type) {
	case EXPECTED_AT_LEAST:
		if (argc < val) {
			error_name = "at least ";
		}
		break;
	case EXPECTED_AT_MOST:
		if (argc > val) {
			error_name = "at most ";
		}
		break;
	case EXPECTED_EQUAL_TO:
		if (argc != val) {
			error_name = "";
		}
	}
	return error_name ?
		cmd_results_new(CMD_INVALID, "Invalid %s command "
				"(expected %s%d argument%s, got %d)",
				name, error_name, val, val != 1 ? "s" : "", argc)
		: NULL;
}

/* Keep alphabetized */
static const struct cmd_handler handlers[] = {
	{ "assign", cmd_assign },
	{ "bar", cmd_bar },
	{ "bindcode", cmd_bindcode },
	{ "bindgesture", cmd_bindgesture },
	{ "bindswitch", cmd_bindswitch },
	{ "bindsym", cmd_bindsym },
	{ "client.background", cmd_client_noop },
	{ "client.focused", cmd_client_focused },
	{ "client.focused_inactive", cmd_client_focused_inactive },
	{ "client.focused_tab_title", cmd_client_focused_tab_title },
	{ "client.placeholder", cmd_client_noop },
	{ "client.unfocused", cmd_client_unfocused },
	{ "client.urgent", cmd_client_urgent },
	{ "default_border", cmd_default_border },
	{ "default_floating_border", cmd_default_floating_border },
	{ "exec", cmd_exec },
	{ "exec_always", cmd_exec_always },
	{ "floating_maximum_size", cmd_floating_maximum_size },
	{ "floating_minimum_size", cmd_floating_minimum_size },
	{ "floating_modifier", cmd_floating_modifier },
	{ "focus", cmd_focus },
	{ "focus_follows_mouse", cmd_focus_follows_mouse },
	{ "focus_on_window_activation", cmd_focus_on_window_activation },
	{ "focus_wrapping", cmd_focus_wrapping },
	{ "font", cmd_font },
	{ "for_window", cmd_for_window },
	{ "force_display_urgency_hint", cmd_force_display_urgency_hint },
	{ "force_focus_wrapping", cmd_force_focus_wrapping },
	{ "fullscreen", cmd_fullscreen },
	{ "gaps", cmd_gaps },
	{ "hide_edge_borders", cmd_hide_edge_borders },
	{ "input", cmd_input },
	{ "mode", cmd_mode },
	{ "mouse_warping", cmd_mouse_warping },
	{ "new_float", cmd_new_float },
	{ "new_window", cmd_new_window },
	{ "no_focus", cmd_no_focus },
	{ "output", cmd_output },
	{ "popup_during_fullscreen", cmd_popup_during_fullscreen },
	{ "seat", cmd_seat },
	{ "set", cmd_set },
	{ "show_marks", cmd_show_marks },
	{ "smart_borders", cmd_smart_borders },
	{ "smart_gaps", cmd_smart_gaps },
	{ "tiling_drag", cmd_tiling_drag },
	{ "tiling_drag_threshold", cmd_tiling_drag_threshold },
	{ "title_align", cmd_title_align },
	{ "titlebar_border_thickness", cmd_titlebar_border_thickness },
	{ "titlebar_padding", cmd_titlebar_padding },
	{ "unbindcode", cmd_unbindcode },
	{ "unbindgesture", cmd_unbindgesture },
	{ "unbindswitch", cmd_unbindswitch },
	{ "unbindsym", cmd_unbindsym },
	{ "workspace", cmd_workspace },
	{ "workspace_auto_back_and_forth", cmd_ws_auto_back_and_forth },
};

/* Config-time only commands. Keep alphabetized */
static const struct cmd_handler config_handlers[] = {
	{ "default_orientation", cmd_default_orientation },
	{ "include", cmd_include },
	{ "primary_selection", cmd_primary_selection },
	{ "swaybg_command", cmd_swaybg_command },
	{ "swaynag_command", cmd_swaynag_command },
	{ "workspace_layout", cmd_workspace_layout },
	{ "xwayland", cmd_xwayland },
};

/* Runtime-only commands. Keep alphabetized */
static const struct cmd_handler command_handlers[] = {
	{ "allow_tearing", cmd_allow_tearing },
	{ "border", cmd_border },
	{ "create_output", cmd_create_output },
	{ "exit", cmd_exit },
	{ "floating", cmd_floating },
	{ "fullscreen", cmd_fullscreen },
	{ "inhibit_idle", cmd_inhibit_idle },
	{ "kill", cmd_kill },
	{ "layout", cmd_layout },
	{ "mark", cmd_mark },
	{ "max_render_time", cmd_max_render_time },
	{ "move", cmd_move },
	{ "nop", cmd_nop },
	{ "opacity", cmd_opacity },
	{ "reload", cmd_reload },
	{ "rename", cmd_rename },
	{ "resize", cmd_resize },
	{ "scratchpad", cmd_scratchpad },
	{ "shortcuts_inhibitor", cmd_shortcuts_inhibitor },
	{ "split", cmd_split },
	{ "splith", cmd_splith },
	{ "splitt", cmd_splitt },
	{ "splitv", cmd_splitv },
	{ "sticky", cmd_sticky },
	{ "swap", cmd_swap },
	{ "title_format", cmd_title_format },
	{ "unmark", cmd_unmark },
	{ "urgent", cmd_urgent },
};

static int handler_compare(const void *_a, const void *_b) {
	const struct cmd_handler *a = _a;
	const struct cmd_handler *b = _b;
	return strcasecmp(a->command, b->command);
}

const struct cmd_handler *find_handler(const char *line,
		const struct cmd_handler *handlers, size_t handlers_size) {
	if (!handlers || !handlers_size) {
		return NULL;
	}
	const struct cmd_handler query = { .command = line };
	return bsearch(&query, handlers,
			handlers_size / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
}

static const struct cmd_handler *find_handler_ex(char *line,
		const struct cmd_handler *config_handlers, size_t config_handlers_size,
		const struct cmd_handler *command_handlers, size_t command_handlers_size,
		const struct cmd_handler *handlers, size_t handlers_size) {
	const struct cmd_handler *handler = NULL;
	if (config->reading) {
		handler = find_handler(line, config_handlers, config_handlers_size);
	} else if (config->active) {
		handler = find_handler(line, command_handlers, command_handlers_size);
	}
	return handler ? handler : find_handler(line, handlers, handlers_size);
}

static const struct cmd_handler *find_core_handler(char *line) {
	return find_handler_ex(line, config_handlers, sizeof(config_handlers),
			command_handlers, sizeof(command_handlers),
			handlers, sizeof(handlers));
}

static void set_config_node(struct sway_node *node, bool node_overridden) {
	config->handler_context.node = node;
	config->handler_context.container = NULL;
	config->handler_context.workspace = NULL;
	config->handler_context.node_overridden = node_overridden;

	if (node == NULL) {
		return;
	}

	switch (node->type) {
	case N_CONTAINER:
		config->handler_context.container = node->sway_container;
		config->handler_context.workspace = node->sway_container->pending.workspace;
		break;
	case N_WORKSPACE:
		config->handler_context.workspace = node->sway_workspace;
		break;
	case N_ROOT:
	case N_OUTPUT:
		break;
	}
}

/*
 * Launch commands (exec and exec_always) read the quotes and backslashes in
 * their argument the way i3 does, with two readings: one for config file
 * lines, and one for commands run at runtime (over IPC, or stored by a binding
 * or window rule and run when it fires).
 */

static const char whitespace[] = " \f\n\r\t\v";
static const char launch_blanks[] = " \t";
// Characters that end the name of a launch command, or its option
static const char launch_word_ends[] = " \f\n\r\t\v;,";
// Separators that end a launch command at runtime
static const char launch_separators[] = ";,\n";

struct launch_args {
	char *name; // exec or exec_always, as written
	bool no_startup_id;
	char *command; // the command handed to the shell; NULL if there is none
	bool empty; // the argument is an empty pair of double quotes
	char *tail; // text after the closing double quote of the argument
};

static void launch_args_finish(struct launch_args *args) {
	free(args->name);
	free(args->command);
	free(args->tail);
}

/**
 * Returns whether the len bytes at name are the name of a launch command.
 */
static bool is_launch_name_n(const char *name, size_t len) {
	return (len == strlen("exec") && strncasecmp(name, "exec", len) == 0)
		|| (len == strlen("exec_always")
			&& strncasecmp(name, "exec_always", len) == 0);
}

static bool is_launch_name(const char *name) {
	return is_launch_name_n(name, strlen(name));
}

/**
 * Returns the length of the launch command name that str starts with, or 0 if
 * str does not start with one.
 */
static size_t launch_name_length(const char *str) {
	size_t len = strcspn(str, launch_word_ends);
	return is_launch_name_n(str, len) ? len : 0;
}

/**
 * Skips the blanks after the name of a launch command, and an optional
 * --no-startup-id followed by blanks. Returns the start of the argument.
 */
static const char *launch_argument(const char *str, bool *no_startup_id) {
	static const char option[] = "--no-startup-id";
	str += strspn(str, launch_blanks);
	*no_startup_id = has_prefix(str, option)
		&& strchr(launch_word_ends, str[strlen(option)]);
	if (*no_startup_id) {
		str += strlen(option);
		str += strspn(str, launch_blanks);
	}
	return str;
}

/**
 * Returns whether the len bytes at str are exactly one single-quoted stretch.
 */
static bool is_single_quoted(const char *str, size_t len) {
	return len >= 2 && str[0] == '\''
		&& memchr(str + 1, '\'', len - 1) == str + len - 1;
}

/**
 * Copies len bytes of str, removing a backslash directly in front of a double
 * quote and, with fold_pairs, one backslash of each pair. Every other
 * backslash is kept.
 */
static char *launch_unescape(const char *str, size_t len, bool fold_pairs) {
	char *res = malloc(len + 1);
	if (!res) {
		return NULL;
	}
	size_t n = 0;
	for (size_t i = 0; i < len; ++i) {
		if (str[i] == '\\' && i + 1 < len && (str[i + 1] == '"'
					|| (fold_pairs && str[i + 1] == '\\'))) {
			++i;
		}
		res[n++] = str[i];
	}
	res[n] = '\0';
	return res;
}

/**
 * Returns the first character of str that is one of delims and is not inside
 * quotes, or the end of str. Quotes and backslashes are tracked like argsep()
 * does.
 */
static const char *find_separator(const char *str, const char *delims) {
	bool in_string = false;
	bool in_char = false;
	bool escaped = false;
	for (; *str; ++str) {
		if (*str != '"' && *str != '\'' && *str != '\\'
				&& !strchr(delims, *str)) {
			escaped = false;
		} else if (*str == '"' && !in_char && !escaped) {
			in_string = !in_string;
		} else if (*str == '\'' && !in_string && !escaped) {
			in_char = !in_char;
		} else if (*str == '\\') {
			escaped = !escaped;
		} else if (!in_string && !in_char && !escaped) {
			break;
		}
	}
	return str;
}

/**
 * Returns the closing double quote of the argument starting at str, or the end
 * of str, using the config reading: the argument ends at the next double quote
 * that does not have a backslash directly in front of it.
 */
static const char *config_quote_end(const char *str) {
	const char *end = str + 1;
	while (*end && (*end != '"' || end[-1] == '\\')) {
		++end;
	}
	return end;
}

/**
 * Returns the closing double quote of the argument starting at str, or the end
 * of str, using the runtime reading: a backslash always takes the next
 * character along with it.
 */
static const char *runtime_quote_end(const char *str) {
	const char *end = str + 1;
	while (*end && *end != '"') {
		if (*end == '\\' && end[1]) {
			++end;
		}
		++end;
	}
	return end;
}

/**
 * Reads arg, the argument of a launch command, into args. With runtime set,
 * this is the runtime reading, otherwise the config reading. Both handle a
 * double-quoted argument and an argument that is exactly one single-quoted
 * stretch; anything else is the command as it is, minus its escapes.
 */
static void read_launch_argument(struct launch_args *args, const char *arg,
		bool runtime) {
	size_t len = strlen(arg);
	if (len == 0) {
		return;
	}
	if (*arg == '"') {
		const char *end = runtime ? runtime_quote_end(arg) : config_quote_end(arg);
		if (end == arg + 1) {
			args->empty = true;
			return;
		}
		if (*end) {
			const char *tail = end + 1 + strspn(end + 1, launch_blanks);
			if (*tail) {
				args->tail = strdup(tail);
			}
		}
		args->command = launch_unescape(arg + 1, end - arg - 1, runtime);
		return;
	}
	size_t trimmed = len;
	while (trimmed > 0 && strchr(launch_blanks, arg[trimmed - 1])) {
		--trimmed;
	}
	if (is_single_quoted(arg, trimmed)) {
		args->command = strndup(arg + 1, trimmed - 2);
	} else {
		args->command = launch_unescape(arg, len, runtime);
	}
}

/**
 * Reads a launch command written in a config file. line starts with the name
 * of the command and runs to the end of the line, and has had its variables
 * replaced.
 */
static void read_config_launch(struct launch_args *args, const char *line) {
	line += strspn(line, whitespace);
	size_t name_len = strcspn(line, whitespace);
	args->name = strndup(line, name_len);
	read_launch_argument(args,
		launch_argument(line + name_len, &args->no_startup_id), false);
}

/**
 * Reads a launch command at runtime. *stringp points at the name of the
 * command; afterwards it points past the separator that ends the command, or
 * is NULL if nothing follows. *matched is set to that separator, with a line
 * break counting as a semicolon. Variables are replaced in the argument before
 * it is read.
 */
static void read_runtime_launch(struct launch_args *args, const char **stringp,
		char *matched) {
	const char *start = *stringp;
	size_t name_len = launch_name_length(start);
	args->name = strndup(start, name_len);
	const char *arg = launch_argument(start + name_len, &args->no_startup_id);

	const char *arg_end, *end;
	if (*arg == '"') {
		arg_end = runtime_quote_end(arg);
		if (*arg_end) {
			++arg_end;
		}
		end = arg_end + strspn(arg_end, launch_blanks);
		if (*end && !strchr(launch_separators, *end)) {
			// Only blanks may follow the closing quote
			args->tail = strndup(end, find_separator(end, launch_separators) - end);
			*stringp = NULL;
			*matched = '\0';
		}
	} else {
		arg_end = end = find_separator(arg, launch_separators);
	}
	if (!args->tail) {
		*stringp = *end ? end + 1 : NULL;
		*matched = *end == '\n' ? ';' : *end;
	}

	char *text = do_var_replacement(strndup(arg, arg_end - arg));
	if (text) {
		char *tail = args->tail;
		args->tail = NULL;
		read_launch_argument(args, text, true);
		if (tail) {
			free(args->tail);
			args->tail = tail;
		}
		free(text);
	}
}

/**
 * Returns the arguments for the handler of a launch command: its name, the
 * --no-startup-id option if given, and the command.
 */
static char **launch_argv(const struct launch_args *args, int *argc) {
	char **argv = calloc(4, sizeof(char *));
	if (!argv) {
		*argc = 0;
		return NULL;
	}
	*argc = 0;
	argv[(*argc)++] = strdup(args->name);
	// The option only goes along with a command, so that a command that is
	// itself "--no-startup-id" is not taken for the option
	if (args->command) {
		if (args->no_startup_id) {
			argv[(*argc)++] = strdup("--no-startup-id");
		}
		argv[(*argc)++] = strdup(args->command);
	}
	return argv;
}

static struct cmd_results *launch_empty_error(const struct launch_args *args) {
	return cmd_results_new(CMD_INVALID,
			"Invalid %s command (the quoted command is empty)", args->name);
}

static struct cmd_results *launch_tail_error(const struct launch_args *args,
		enum cmd_status status) {
	return cmd_results_new(status, "Invalid %s command (expected only "
			"blanks after the closing quote, got '%s')", args->name, args->tail);
}

/**
 * Prepares the commands stored by a binding or window rule in a config file,
 * starting at its first launch command. Launch arguments get the config
 * reading, but keep their quotes so that they get the runtime reading when the
 * binding or rule fires; other commands are unescaped as before.
 */
static char *store_commands(const char *text) {
	char *res = calloc(strlen(text) + 1, 1);
	if (!res) {
		return NULL;
	}
	char *out = res;
	while (true) {
		size_t blanks = strspn(text, whitespace);
		memcpy(out, text, blanks);
		out += blanks;
		text += blanks;

		const char *end;
		size_t name_len = launch_name_length(text);
		if (name_len) {
			bool no_startup_id;
			const char *arg = launch_argument(text + name_len, &no_startup_id);
			memcpy(out, text, arg - text);
			out += arg - text;
			if (*arg == '"') {
				const char *close = config_quote_end(arg);
				end = find_separator(*close ? close + 1 : close, ";,");
			} else {
				end = find_separator(arg, ";,");
			}
			size_t trimmed = end - arg;
			while (trimmed > 0 && strchr(launch_blanks, arg[trimmed - 1])) {
				--trimmed;
			}
			if (is_single_quoted(arg, trimmed)) {
				memcpy(out, arg, end - arg);
				out += end - arg;
			} else {
				char *unescaped = launch_unescape(arg, end - arg, false);
				if (unescaped) {
					out = stpcpy(out, unescaped);
					free(unescaped);
				}
			}
		} else {
			end = find_separator(text, ";,");
			char *command = strndup(text, end - text);
			int argc = 0;
			char **argv = command ? split_args(command, &argc) : NULL;
			for (int i = 0; i < argc; ++i) {
				unescape_string(argv[i]);
			}
			if (argc) {
				char *joined = join_args(argv, argc);
				out = stpcpy(out, joined);
				free(joined);
			}
			if (argv) {
				free_argv(argc, argv);
			}
			free(command);
		}

		if (!*end) {
			break;
		}
		*out++ = *end;
		text = end + 1;
	}
	*out = '\0';
	return res;
}

/**
 * Executes the commands in exec. sent is the text as it was received, of the
 * same length as exec: launch commands read their arguments from it.
 */
static list_t *execute_commands(char *exec, const char *sent,
		struct sway_seat *seat, struct sway_container *con) {
	char *cmd;
	char matched_delim = ';';
	list_t *containers = NULL;
	bool using_criteria = false;

	if (seat == NULL) {
		// passing a NULL seat means we just pick the default seat
		seat = input_manager_get_default_seat();
		if (!sway_assert(seat, "could not find a seat to run the command on")) {
			return NULL;
		}
	}

	char *head = exec;
	list_t *res_list = create_list();

	if (!res_list) {
		return NULL;
	}

	config->handler_context.seat = seat;

	do {
		for (; isspace(*head); ++head) {}
		// Extract criteria (valid for this command list only).
		if (matched_delim == ';') {
			using_criteria = false;
			if (*head == '[') {
				char *error = NULL;
				struct criteria *criteria = criteria_parse(head, &error);
				if (!criteria) {
					list_add(res_list,
							cmd_results_new(CMD_INVALID, "%s", error));
					free(error);
					goto cleanup;
				}
				list_free(containers);
				containers = criteria_get_containers(criteria);
				head += strlen(criteria->raw);
				criteria_destroy(criteria);
				using_criteria = true;
				// Skip leading whitespace
				for (; isspace(*head); ++head) {}
			}
		}
		int argc;
		char **argv;
		bool launch = launch_name_length(head) > 0;
		struct launch_args launch_args = {0};
		if (launch) {
			// Launch commands are split off by their own reading, from the
			// text as it was received
			const char *text = sent + (head - exec);
			read_runtime_launch(&launch_args, &text, &matched_delim);
			head = text ? exec + (text - sent) : NULL;
			sway_log(SWAY_INFO, "Handling launch command '%s' with '%s'",
					launch_args.name, launch_args.command ? launch_args.command : "");
			if (launch_args.empty) {
				list_add(res_list, launch_empty_error(&launch_args));
				launch_args_finish(&launch_args);
				goto cleanup;
			}
			argv = launch_argv(&launch_args, &argc);
			if (!argv) {
				launch_args_finish(&launch_args);
				goto cleanup;
			}
		} else {
			// Split command list
			cmd = argsep(&head, ";,", &matched_delim);
			for (; isspace(*cmd); ++cmd) {}

			if (strcmp(cmd, "") == 0) {
				sway_log(SWAY_INFO, "Ignoring empty command.");
				continue;
			}
			sway_log(SWAY_INFO, "Handling command '%s'", cmd);
			//TODO better handling of argv
			argv = split_args(cmd, &argc);
			if (strcmp(argv[0], "mode") != 0) {
				for (int i = 1; i < argc; ++i) {
					if (*argv[i] == '\"' || *argv[i] == '\'') {
						strip_quotes(argv[i]);
					}
				}
			}
		}
		const struct cmd_handler *handler = find_core_handler(argv[0]);
		if (!handler) {
			list_add(res_list, cmd_results_new(CMD_INVALID,
					"Unknown/invalid command '%s'", argv[0]));
			free_argv(argc, argv);
			launch_args_finish(&launch_args);
			goto cleanup;
		}

		// Var replacement, for all but first argument of set. Launch commands
		// have had theirs replaced before their argument was read.
		for (int i = handler->handle == cmd_set ? 2 : 1; !launch && i < argc; ++i) {
			argv[i] = do_var_replacement(argv[i]);
		}


		if (!using_criteria) {
			if (con) {
				set_config_node(&con->node, true);
			} else {
				set_config_node(seat_get_focus_inactive(seat, &root->node),
						false);
			}
			struct cmd_results *res = handler->handle(argc-1, argv+1);
			list_add(res_list, res);
			if (res->status == CMD_INVALID) {
				free_argv(argc, argv);
				launch_args_finish(&launch_args);
				goto cleanup;
			}
		} else if (containers->length == 0) {
			list_add(res_list,
					cmd_results_new(CMD_FAILURE, "No matching node."));
		} else {
			struct cmd_results *fail_res = NULL;
			for (int i = 0; i < containers->length; ++i) {
				struct sway_container *container = containers->items[i];
				set_config_node(&container->node, true);
				struct cmd_results *res = handler->handle(argc-1, argv+1);
				if (res->status == CMD_SUCCESS) {
					free_cmd_results(res);
				} else {
					// last failure will take precedence
					if (fail_res) {
						free_cmd_results(fail_res);
					}
					fail_res = res;
					if (res->status == CMD_INVALID) {
						list_add(res_list, fail_res);
						free_argv(argc, argv);
						launch_args_finish(&launch_args);
						goto cleanup;
					}
				}
			}
			list_add(res_list,
					fail_res ? fail_res : cmd_results_new(CMD_SUCCESS, NULL));
		}
		free_argv(argc, argv);
		if (launch_args.tail) {
			// The launch command has run, but nothing after it does
			list_add(res_list, launch_tail_error(&launch_args, CMD_INVALID));
			launch_args_finish(&launch_args);
			goto cleanup;
		}
		launch_args_finish(&launch_args);
	} while(head);
cleanup:
	list_free(containers);
	return res_list;
}

list_t *execute_command(char *_exec, struct sway_seat *seat,
		struct sway_container *con) {
	char *exec = strdup(_exec);
	if (!exec) {
		return NULL;
	}
	list_t *res_list = execute_commands(exec, exec, seat, con);
	free(exec);
	return res_list;
}

list_t *execute_ipc_command(const char *payload, size_t length) {
	char *exec = malloc(length + 1);
	char *sent = malloc(length + 1);
	if (!exec || !sent) {
		free(exec);
		free(sent);
		return NULL;
	}
	memcpy(exec, payload, length);
	exec[length] = '\0';
	memcpy(sent, exec, length + 1);

	// Line breaks separate commands like semicolons. Launch commands read
	// their arguments from the message as sent, where a line break inside
	// quotes is kept.
	char *line = strtok(exec, "\n");
	while (line) {
		size_t line_length = strlen(line);
		if (line + line_length >= exec + length) {
			break;
		}
		line[line_length] = ';';
		line = strtok(NULL, "\n");
	}

	list_t *res_list = execute_commands(exec, sent, NULL, NULL);
	free(exec);
	free(sent);
	return res_list;
}

list_t *execute_deferred_command(char *line) {
	// Launch lines get the config reading, as they do on reload
	bool launch = false;
	int argc;
	char **argv = split_args(line, &argc);
	if (argc) {
		char *name = do_var_replacement(strdup(argv[0]));
		launch = name && launch_name_length(name) > 0;
		free(name);
	}
	free_argv(argc, argv);
	if (!launch) {
		return execute_command(line, NULL, NULL);
	}

	list_t *res_list = create_list();
	if (!res_list) {
		return NULL;
	}
	char *new_block = NULL;
	list_add(res_list, config_command(line, &new_block));
	free(new_block);
	return res_list;
}

// this is like execute_command above, except:
// 1) it ignores empty commands (empty lines)
// 2) it does variable substitution
// 3) it doesn't split commands (because the multiple commands are supposed to
//	  be chained together)
// 4) execute_command handles all state internally while config_command has
// some state handled outside (notably the block mode, in read_config)
struct cmd_results *config_command(char *exec, char **new_block) {
	struct cmd_results *results = NULL;
	char *line = exec;
	char *joined_line = NULL;
	int argc;
	char **argv = split_args(exec, &argc);

	// Check for empty lines
	if (!argc) {
		results = cmd_results_new(CMD_SUCCESS, NULL);
		goto cleanup;
	}

	// Check for the start of a block
	if (argc > 1 && strcmp(argv[argc - 1], "{") == 0) {
		*new_block = join_args(argv, argc - 1);
		results = cmd_results_new(CMD_BLOCK, NULL);
		goto cleanup;
	}

	// Check for the end of a block
	if (strcmp(argv[argc - 1], "}") == 0) {
		results = cmd_results_new(CMD_BLOCK_END, NULL);
		goto cleanup;
	}

	// Make sure the command is not stored in a variable
	if (*argv[0] == '$') {
		argv[0] = do_var_replacement(argv[0]);
		line = joined_line = join_args(argv, argc);
		free_argv(argc, argv);
		argv = split_args(line, &argc);
		if (!argc) {
			results = cmd_results_new(CMD_SUCCESS, NULL);
			goto cleanup;
		}
	}

	// Determine the command handler
	sway_log(SWAY_INFO, "Config command: %s", exec);
	const struct cmd_handler *handler = find_core_handler(argv[0]);
	if (!handler || !handler->handle) {
		if (handler) {
			results = cmd_results_new(CMD_INVALID,
				"Command '%s' is shimmed, but unimplemented", argv[0]);
		} else {
			results = cmd_results_new(CMD_INVALID,
				"Unknown/invalid command '%s'", argv[0]);
		}
		goto cleanup;
	}

	// Launch commands, and the commands stored by bindings and window rules,
	// are read from the line as written, after variable replacement
	bool launch = handler->handle == cmd_exec
		|| handler->handle == cmd_exec_always;
	bool stores_commands = handler->handle == cmd_bindsym
		|| handler->handle == cmd_bindcode
		|| handler->handle == cmd_bindswitch
		|| handler->handle == cmd_bindgesture
		|| handler->handle == cmd_for_window
		|| handler->handle == cmd_mode;

	// Do variable replacement
	if (handler->handle == cmd_set && argc > 1 && *argv[1] == '$') {
		// Escape the variable name so it does not get replaced by one shorter
		char *temp = calloc(1, strlen(argv[1]) + 2);
		temp[0] = '$';
		strcpy(&temp[1], argv[1]);
		free(argv[1]);
		argv[1] = temp;
	}
	char *command = do_var_replacement(launch || stores_commands
			? strdup(line) : join_args(argv, argc));
	sway_log(SWAY_INFO, "After replacement: %s", command);
	free_argv(argc, argv);

	if (launch) {
		argv = NULL;
		argc = 0;
		struct launch_args launch_args = {0};
		read_config_launch(&launch_args, command);
		free(command);
		if (launch_args.empty) {
			results = launch_empty_error(&launch_args);
			launch_args_finish(&launch_args);
			goto cleanup;
		}
		argv = launch_argv(&launch_args, &argc);
		if (!argv) {
			launch_args_finish(&launch_args);
			results = cmd_results_new(CMD_FAILURE, "Unable to allocate arguments");
			goto cleanup;
		}
		results = handler->handle(argc - 1, argv + 1);
		if (launch_args.tail) {
			// A deferred command still runs later, and one run now has run,
			// but the line is reported as an error either way
			if (results->status == CMD_DEFER && !results->error) {
				free_cmd_results(results);
				results = launch_tail_error(&launch_args, CMD_DEFER);
			} else if (results->status == CMD_SUCCESS) {
				free_cmd_results(results);
				results = launch_tail_error(&launch_args, CMD_INVALID);
			}
		}
		launch_args_finish(&launch_args);
		goto cleanup;
	}

	argv = split_args(command, &argc);
	char *stored = NULL;
	if (stores_commands) {
		// The stored commands are prepared from their first launch command on
		const char *pos = command;
		for (int i = 0; i < argc; ++i) {
			pos += strspn(pos, whitespace);
			if (i > 0 && is_launch_name(argv[i])) {
				stored = store_commands(pos);
				if (!stored) {
					break;
				}
				for (int j = i; j < argc; ++j) {
					free(argv[j]);
					argv[j] = NULL;
				}
				argv[i] = stored;
				argc = i + 1;
				break;
			}
			pos += strlen(argv[i]);
		}
	}
	free(command);

	// Strip quotes and unescape the string
	for (int i = handler->handle == cmd_set ? 2 : 1; i < argc; ++i) {
		if (argv[i] == stored) {
			continue;
		}
		if (handler->handle != cmd_exec && handler->handle != cmd_exec_always
				&& handler->handle != cmd_mode
				&& handler->handle != cmd_bindsym
				&& handler->handle != cmd_bindcode
				&& handler->handle != cmd_bindswitch
				&& handler->handle != cmd_bindgesture
				&& handler->handle != cmd_set
				&& handler->handle != cmd_for_window
				&& (*argv[i] == '\"' || *argv[i] == '\'')) {
			strip_quotes(argv[i]);
		}
		unescape_string(argv[i]);
	}

	// Run command
	results = handler->handle(argc - 1, argv + 1);

cleanup:
	free_argv(argc, argv);
	free(joined_line);
	return results;
}

struct cmd_results *config_subcommand(char **argv, int argc,
		const struct cmd_handler *handlers, size_t handlers_size) {
	char *command = join_args(argv, argc);
	sway_log(SWAY_DEBUG, "Subcommand: %s", command);
	free(command);

	const struct cmd_handler *handler = find_handler(argv[0], handlers,
			handlers_size);
	if (!handler) {
		return cmd_results_new(CMD_INVALID,
				"Unknown/invalid command '%s'", argv[0]);
	}
	if (handler->handle) {
		return handler->handle(argc - 1, argv + 1);
	}
	return cmd_results_new(CMD_INVALID,
			"The command '%s' is shimmed, but unimplemented", argv[0]);
}

struct cmd_results *config_commands_command(char *exec) {
	struct cmd_results *results = NULL;
	int argc;
	char **argv = split_args(exec, &argc);
	if (!argc) {
		results = cmd_results_new(CMD_SUCCESS, NULL);
		goto cleanup;
	}

	// Find handler for the command this is setting a policy for
	char *cmd = argv[0];

	if (strcmp(cmd, "}") == 0) {
		results = cmd_results_new(CMD_BLOCK_END, NULL);
		goto cleanup;
	}

	const struct cmd_handler *handler = find_handler(cmd, NULL, 0);
	if (!handler && strcmp(cmd, "*") != 0) {
		results = cmd_results_new(CMD_INVALID,
			"Unknown/invalid command '%s'", cmd);
		goto cleanup;
	}

	results = cmd_results_new(CMD_SUCCESS, NULL);

cleanup:
	free_argv(argc, argv);
	return results;
}

struct cmd_results *cmd_results_new(enum cmd_status status,
		const char *format, ...) {
	struct cmd_results *results = malloc(sizeof(struct cmd_results));
	if (!results) {
		sway_log(SWAY_ERROR, "Unable to allocate command results");
		return NULL;
	}
	results->status = status;
	if (format) {
		va_list args;
		va_start(args, format);
		results->error = vformat_str(format, args);
		va_end(args);
	} else {
		results->error = NULL;
	}
	return results;
}

void free_cmd_results(struct cmd_results *results) {
	if (results->error) {
		free(results->error);
	}
	free(results);
}

char *cmd_results_to_json(list_t *res_list) {
	json_object *result_array = json_object_new_array();
	for (int i = 0; i < res_list->length; ++i) {
		struct cmd_results *results = res_list->items[i];
		json_object *root = json_object_new_object();
		json_object_object_add(root, "success",
				json_object_new_boolean(results->status == CMD_SUCCESS));
		if (results->error) {
			json_object_object_add(root, "parse_error",
					json_object_new_boolean(results->status == CMD_INVALID));
			json_object_object_add(
					root, "error", json_object_new_string(results->error));
		}
		json_object_array_add(result_array, root);
	}
	const char *json = json_object_to_json_string(result_array);
	char *res = strdup(json);
	json_object_put(result_array);
	return res;
}
