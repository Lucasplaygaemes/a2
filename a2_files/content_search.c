#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "editor_utils.h"
#include "screen_ui.h"
#include "window_management.h"

void search_in_file(const char *file_path, const char *pattern, ContentSearchResult **results, int *count, int *capacity) {
    FILE *f = fopen(file_path, "r");
    if (!f) return;

    char line[MAX_LINE_LEN];
    int line_num = 1;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, pattern)) {
            if (*count >= *capacity) {
                *capacity = (*capacity == 0) ? 128 : *capacity * 2;
                *results = realloc(*results, sizeof(ContentSearchResult) * *capacity);
                if (!*results) { fclose(f); return; }
            }

            line[strcspn(line, "\n")] = 0; // Remove newline

            (*results)[*count].file_path = strdup(file_path);
            (*results)[*count].line_number = line_num;
            (*results)[*count].line_content = strdup(trim_whitespace(line));
            (*count)++;
        }
        line_num++;
    }
    fclose(f);
}

void recursive_content_search(const char *base_path, const char *pattern, ContentSearchResult **results, int *count, int *capacity) {
    DIR *d = opendir(base_path);
    if (!d) return;

    struct dirent *dir;
    char path[PATH_MAX];

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 ||
            strcmp(dir->d_name, ".git") == 0 || strcmp(dir->d_name, "build") == 0 ||
            strcmp(dir->d_name, "output") == 0 || strcmp(dir->d_name, ".cache") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", base_path, dir->d_name);

        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                recursive_content_search(path, pattern, results, count, capacity);
            } else if (S_ISREG(st.st_mode)) {
                // Simple check to avoid searching in binary files
                FILE *f = fopen(path, "r");
                if (f) {
                    char buffer[1024];
                    size_t bytes_read = fread(buffer, 1, 1024, f);
                    fclose(f);
                    if (memchr(buffer, 0, bytes_read) == NULL) { // No null bytes? Likely text.
                        search_in_file(path, pattern, results, count, capacity);
                    }
                }
            }
        }
    }
    closedir(d);
}

void display_content_search(EditorState *state, const char* prefilled_term) {
    pthread_mutex_lock(&global_grep_state.mutex);

    if (global_grep_state.is_running) {
        editor_set_status_msg(state, "Grep is already running in the background");
        pthread_mutex_unlock(&global_grep_state.mutex);
        return;
    }
    pthread_mutex_unlock(&global_grep_state.mutex);

    char search_term[100] = {0};

    if (prefilled_term && prefilled_term[0] != '\0') {
        strncpy(search_term, prefilled_term, sizeof(search_term) - 1);
    } else {
        int rows, cols; getmaxyx(stdscr, rows, cols);
        int win_h = 3; int win_w = cols / 2;
        int win_y = (rows - win_h) / 2; int win_x = (cols - win_w) / 2;
        WINDOW *input_win = newwin(win_h, win_w, win_y, win_x);
        keypad(input_win, TRUE);
        wbkgd(input_win, COLOR_PAIR(9));
        box(input_win, 0, 0);
        mvwprintw(input_win, 1, 2, "Grep for: ");
        wrefresh(input_win);
        curs_set(1); echo();
        wgetnstr(input_win, search_term, sizeof(search_term) - 1);
        noecho(); curs_set(0);
        delwin(input_win);
        touchwin(stdscr);
        redraw_all_windows();
    }

    if (strlen(search_term) == 0) return;

    pthread_mutex_lock(&global_grep_state.mutex);
    global_grep_state.is_running = true;
    global_grep_state.results_ready = false;
    snprintf(global_grep_state.search_term, sizeof(global_grep_state.search_term), "%s", search_term);
    pthread_mutex_unlock(&global_grep_state.mutex);

    if (pthread_create(&global_grep_state.thread, NULL, background_grep_worker, NULL) != 0) {
        editor_set_status_msg(state, " Error creating the thread of grep.");
        global_grep_state.is_running = false;
    } else {
        pthread_detach(global_grep_state.thread);
        editor_set_status_msg(state, "Searching for '%s' in the background.", search_term);
    }
}