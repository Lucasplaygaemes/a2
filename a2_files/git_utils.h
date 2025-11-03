#ifndef GIT_UTILS_H
#define GIT_UTILS_H

#include "defs.h"
#include <stdio.h>
#include <string.h>

// Helper function to run a command and capture its output
static inline int run_command_and_get_output(const char *cmd, char *buffer, size_t size) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        return 0;
    }
    char *line = NULL;
    if (fgets(buffer, size, pipe) != NULL) {
        line = buffer;
    }
    pclose(pipe);

    if (line) {
        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;
        return 1;
    }
    return 0;
}

// Updates the git branch name in the editor state
static inline void editor_update_git_branch(EditorState *state) {
    // Robust command to get the current branch. Redirects stderr to null.
    const char *cmd = "git rev-parse --abbrev-ref HEAD 2>/dev/null";
    if (run_command_and_get_output(cmd, state->git_branch, sizeof(state->git_branch))) {
        // Success, branch name is in state->git_branch
    } else {
        // Failure (likely not a git repo), clear the branch name
        state->git_branch[0] = '\0';
    }
}

#endif // GIT_UTILS_H
