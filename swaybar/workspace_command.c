#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "swaybar/bar.h"
#include "swaybar/ipc.h"
#include "swaybar/workspace_command.h"
#include "log.h"
#include "loop.h"

struct workspace_command *workspace_command_init(struct swaybar *bar, const char *cmd) {
	struct workspace_command *wscmd = calloc(1, sizeof(*wscmd));
	if (!wscmd) {
		return NULL;
	}
	wscmd->bar = bar;
	wscmd->read_fd = -1;
	wscmd->cmd = strdup(cmd);
	wscmd->buffer_size = 8192;
	wscmd->buffer = malloc(wscmd->buffer_size);

	int fds[2];
	if (pipe(fds) != 0) {
		sway_log_errno(SWAY_ERROR, "Unable to create pipe for workspace_command");
		workspace_command_free(wscmd);
		return NULL;
	}
	wscmd->pid = fork();
	if (wscmd->pid < 0) {
		sway_log_errno(SWAY_ERROR, "fork failed");
		close(fds[0]);
		close(fds[1]);
		workspace_command_free(wscmd);
		return NULL;
	} else if (wscmd->pid == 0) {
		setpgid(0, 0);
		dup2(fds[1], STDOUT_FILENO);
		close(fds[0]);
		close(fds[1]);
		char *const argv[] = { "sh", "-c", wscmd->cmd, NULL, };
		execvp(argv[0], argv);
		exit(1);
	}
	close(fds[1]);
	wscmd->read_fd = fds[0];
	fcntl(wscmd->read_fd, F_SETFL, O_NONBLOCK);
	fcntl(wscmd->read_fd, F_SETFD, FD_CLOEXEC);
	return wscmd;
}

void workspace_command_free(struct workspace_command *wscmd) {
	if (!wscmd) {
		return;
	}
	if (wscmd->read_fd >= 0) {
		loop_remove_fd(wscmd->bar->eventloop, wscmd->read_fd);
		close(wscmd->read_fd);
	}
	if (wscmd->pid > 0) {
		kill(-wscmd->pid, SIGTERM);
		waitpid(wscmd->pid, NULL, 0);
	}
	if (wscmd->workspaces) {
		json_object_put(wscmd->workspaces);
	}
	free(wscmd->buffer);
	free(wscmd->cmd);
	free(wscmd);
}

static bool handle_line(struct workspace_command *wscmd, const char *line) {
	json_object *array = json_tokener_parse(line);
	if (!array) {
		sway_log(SWAY_ERROR, "workspace_command: invalid JSON: %s", line);
		return false;
	}
	if (!json_object_is_type(array, json_type_array)) {
		sway_log(SWAY_ERROR, "workspace_command: expected a JSON array");
		json_object_put(array);
		return false;
	}
	if (wscmd->workspaces) {
		json_object_put(wscmd->workspaces);
	}
	wscmd->workspaces = array;
	return true;
}

bool workspace_command_handle_readable(struct workspace_command *wscmd) {
	bool changed = false;
	while (true) {
		if (wscmd->buffer_index + 1 >= wscmd->buffer_size) {
			wscmd->buffer_size *= 2;
			wscmd->buffer = realloc(wscmd->buffer, wscmd->buffer_size);
		}
		ssize_t n = read(wscmd->read_fd, wscmd->buffer + wscmd->buffer_index,
				wscmd->buffer_size - wscmd->buffer_index - 1);
		if (n == 0 && wscmd->buffer_index > 0) {
			// End of output: the last line may lack its newline
			wscmd->buffer[wscmd->buffer_index] = '\0';
			changed |= handle_line(wscmd, wscmd->buffer);
			wscmd->buffer_index = 0;
		}
		if (n <= 0) {
			break;
		}
		wscmd->buffer_index += n;
		wscmd->buffer[wscmd->buffer_index] = '\0';
		char *start = wscmd->buffer, *nl;
		while ((nl = strchr(start, '\n'))) {
			*nl = '\0';
			if (*start) {
				changed |= handle_line(wscmd, start);
			}
			start = nl + 1;
		}
		size_t rest = wscmd->buffer + wscmd->buffer_index - start;
		memmove(wscmd->buffer, start, rest);
		wscmd->buffer_index = rest;
	}
	return changed;
}

void workspace_command_in(int fd, short mask, void *data) {
	struct swaybar *bar = data;
	if (!bar->workspace_command) {
		return;
	}
	if (workspace_command_handle_readable(bar->workspace_command)) {
		ipc_get_workspaces(bar);
		set_bar_dirty(bar);
	}
	if (mask & (POLLHUP | POLLERR)) {
		loop_remove_fd(bar->eventloop, fd);
	}
}
