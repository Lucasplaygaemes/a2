#include "diff.h"
#include "defs.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "command_execution.h"
#include "others.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static bool get_filename_input(const char *prompt, char *buffer, int max_len) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    
    int win_h = 3;
    int win_w = 60;
    if (win_w > cols) win_w = cols - 2;
    
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;
    
    WINDOW *input_win = newwin(win_h, win_w, win_y, win_x);
    keypad(input_win, TRUE);
    wbkgd(input_win, COLOR_PAIR(9));
    box(input_win, 0 ,0);
    
    mvwprintw(input_win, 1, 2, "%s", prompt);
    wrefresh(input_win);
    
    curs_set(1); 
    echo();
    
    // clean the buffer
    buffer[0] = '\0';
    
    // Move the cursor to after the prompt
    wmove(input_win, 1, strlen(prompt) + 3);
    wgetnstr(input_win, buffer, max_len - 1);
    
    noecho();
    curs_set(0);
    delwin(input_win);
    
    // Restore main windows
    touchwin(stdscr);
    redesenhar_todas_as_janelas();
    
    return strlen(buffer) > 0;
}

void start_interactive_diff(EditorState *state) {
    char file1[PATH_MAX] = {0};
    char file2[PATH_MAX] = {0};
    
    // firts file
    if (!get_filename_input("Diff File 1:", file1, sizeof(file1))) {
        editor_set_status_msg(state, "Diff cancelled.");
        return;
    }
    
    // Verify if the first file exists
    if (access(file1, F_OK) != 0) {
        editor_set_status_msg(state, "Error: File '%s' not found.", file1);
        return;
    }

    // seconde one
    if (!get_filename_input("Diff File 2:", file2, sizeof(file2))) {
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
