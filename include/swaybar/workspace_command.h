#ifndef _SWAYBAR_WORKSPACE_COMMAND_H
#define _SWAYBAR_WORKSPACE_COMMAND_H
#include <json.h>
#include <stdbool.h>
#include <sys/types.h>

struct swaybar;

// A running workspace_command (i3bar workspace buttons protocol)
struct workspace_command {
	struct swaybar *bar;
	char *cmd;
	pid_t pid;
	int read_fd;
	char *buffer;
	size_t buffer_size, buffer_index;
	json_object *workspaces; // last valid array printed by the command
};

struct workspace_command *workspace_command_init(struct swaybar *bar, const char *cmd);
void workspace_command_free(struct workspace_command *wscmd);
// Returns true if the workspace list changed
bool workspace_command_handle_readable(struct workspace_command *wscmd);
void workspace_command_in(int fd, short mask, void *data);

#endif
