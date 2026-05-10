#include "diff.h"
#include "defs.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "command_execution.h"
#include "others.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

void start_interactive_diff(EditorState *state) {
    char file1[PATH_MAX] = {0};
    char file2[PATH_MAX] = {0};
    
    // firts file
    if (!ui_ask_input("Diff File 1:", file1, sizeof(file1))) {
        editor_set_status_msg(state, "Diff cancelled.");
        return;
    }
    
    // Verify if the first file exists
    if (access(file1, F_OK) != 0) {
        editor_set_status_msg(state, "Error: File '%s' not found.", file1);
        return;
    }

    // seconde one
    if (!ui_ask_input("Diff File 2:", file2, sizeof(file2))) {
        editor_set_status_msg(state, "Diff cancelled.");
        return;
    }

    // verify the second one
    if (access(file2, F_OK) != 0) {
        editor_set_status_msg(state, "Error: File '%s' not found.", file2);
        return;
    }
    
    char diff_cmd_str[2048];
    snprintf(diff_cmd_str, sizeof(diff_cmd_str), "git diff --no-index -- %s %s", file1, file2);
    
    editor_set_status_msg(state, "Running diff...");
    
    run_and_display_command(diff_cmd_str, "--- DIFF RESULT ---");
}
