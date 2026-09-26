#include "local_history.h"
#include "fileio.h"
#include "editor_utils.h"
#include "undo_redo.h"
#include "diff.h"
#include "window_managment.h"
#include "cache.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static time_t g_last_snapshot_time = 0;
static char g_last_snapshot_file[PATH_MAX] = {0};

static unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

static void get_history_base_dir(char *buf, size_t size) {
    char config_dir[PATH_MAX];
    ensure_a2_config_dir(config_dir, sizeof(config_dir));
    snprintf(buf, size, "%s/history", config_dir);
    mkdir(buf, 0755);
}

static void get_file_history_dir(const char *filepath, char *dir_out, size_t size) {
    char base[PATH_MAX];
    get_history_base_dir(base, sizeof(base));
    
    char store_dir[PATH_MAX];
    snprintf(store_dir, sizeof(store_dir), "%s/store", base);
    mkdir(store_dir, 0755);

    unsigned long h = hash_string(filepath);
    snprintf(dir_out, size, "%s/%lu", store_dir, h);
    mkdir(dir_out, 0755);
}

static char *get_buffer_text(EditorState *state, size_t *out_len) {
    size_t total = 0;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        if (state->buffer.lines[i]) {
            total += strlen(state->buffer.lines[i]) + 1; // +1 for \n
        }
    }
    char *text = malloc(total + 1);
    if (!text) return NULL;
    
    size_t offset = 0;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        if (state->buffer.lines[i]) {
            size_t len = strlen(state->buffer.lines[i]);
            memcpy(text + offset, state->buffer.lines[i], len);
            offset += len;
            text[offset++] = '\n';
        }
    }
    text[offset] = '\0';
    if (out_len) *out_len = offset;
    return text;
}

void local_history_init(void) {
    char base[PATH_MAX];
    get_history_base_dir(base, sizeof(base));
}

void local_history_take_snapshot(EditorState *state, const char *reason) {
    if (!state || !state->buffer.filename[0]) return;
    
    char history_dir[PATH_MAX];
    get_file_history_dir(state->buffer.filename, history_dir, sizeof(history_dir));

    // Save info.txt with the original file path
    char info_path[PATH_MAX];
    snprintf(info_path, sizeof(info_path), "%s/info.txt", history_dir);
    if (access(info_path, F_OK) != 0) {
        FILE *fi = fopen(info_path, "w");
        if (fi) {
            fprintf(fi, "%s\n", state->buffer.filename);
            fclose(fi);
        }
    }

    size_t text_len = 0;
    char *current_text = get_buffer_text(state, &text_len);
    if (!current_text) return;

    time_t now = time(NULL);

    // Read index to check deduplication against last snapshot
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index.txt", history_dir);

    char last_snap_file[PATH_MAX] = {0};
    FILE *fidx = fopen(index_path, "r");
    if (fidx) {
        char line[512];
        while (fgets(line, sizeof(line), fidx)) {
            char *last_field = strrchr(line, '|');
            if (last_field) {
                char *newline = strchr(last_field, '\n');
                if (newline) *newline = '\0';
                snprintf(last_snap_file, sizeof(last_snap_file), "%s/%s", history_dir, last_field + 1);
            }
        }
        fclose(fidx);
    }

    // Check if duplicate of last snapshot
    if (last_snap_file[0] != '\0') {
        FILE *flast = fopen(last_snap_file, "r");
        if (flast) {
            fseek(flast, 0, SEEK_END);
            long flast_size = ftell(flast);
            fseek(flast, 0, SEEK_SET);

            if ((size_t)flast_size == text_len) {
                char *last_text = malloc(flast_size + 1);
                if (last_text) {
                    size_t read_bytes = fread(last_text, 1, flast_size, flast);
                    last_text[read_bytes] = '\0';
                    bool is_identical = (strcmp(current_text, last_text) == 0);
                    free(last_text);
                    fclose(flast);
                    if (is_identical) {
                        free(current_text);
                        return; // Skip duplicate snapshot!
                    }
                } else {
                    fclose(flast);
                }
            } else {
                fclose(flast);
            }
        }
    }

    // Create snapshot file
    char snap_filename[128];
    snprintf(snap_filename, sizeof(snap_filename), "snap_%ld.txt", (long)now);

    char snap_filepath[PATH_MAX];
    snprintf(snap_filepath, sizeof(snap_filepath), "%s/%s", history_dir, snap_filename);

    FILE *fsnap = fopen(snap_filepath, "w");
    if (fsnap) {
        fwrite(current_text, 1, text_len, fsnap);
        fclose(fsnap);
    }
    free(current_text);

    // Format human readable time
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // Append entry to index.txt
    fidx = fopen(index_path, "a");
    if (fidx) {
        fprintf(fidx, "%ld|%s|%s|%s\n", (long)now, time_str, reason ? reason : "Snapshot", snap_filename);
        fclose(fidx);
    }

    g_last_snapshot_time = now;
    strncpy(g_last_snapshot_file, state->buffer.filename, sizeof(g_last_snapshot_file) - 1);
}

void local_history_check_idle_snapshot(EditorState *state) {
    if (!state || !state->buffer.filename[0] || !state->buffer.modified) return;
    
    time_t now = time(NULL);
    if (g_last_snapshot_time == 0) g_last_snapshot_time = now;

    if (now - g_last_snapshot_time >= 30) { // 30 seconds idle snapshot
        local_history_take_snapshot(state, "Auto-Save Idle");
    }
}

void display_local_history(EditorState *state) {
    if (!state || !state->buffer.filename[0]) {
        editor_set_status_msg(state, "No file open for local history.");
        return;
    }

    char history_dir[PATH_MAX];
    get_file_history_dir(state->buffer.filename, history_dir, sizeof(history_dir));

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index.txt", history_dir);

    FILE *fidx = fopen(index_path, "r");
    if (!fidx) {
        editor_set_status_msg(state, "No local history entries found for this file yet.");
        return;
    }

    char *temp_filename = get_cache_filename("history_list.XXXXXX");
    if (!temp_filename) {
        fclose(fidx);
        return;
    }

    FILE *temp_file = fopen(temp_filename, "w");
    if (!temp_file) {
        fclose(fidx);
        free(temp_filename);
        return;
    }

    fprintf(temp_file, "# LOCAL HISTORY: %s\n", state->buffer.filename);
    fprintf(temp_file, "# Use :history-restore <num> to restore a snapshot.\n");
    fprintf(temp_file, "# Use :history-diff <num> to view diff with a snapshot.\n\n");

    char line[512];
    int count = 0;
    char entries[100][512];
    int total_entries = 0;

    while (fgets(line, sizeof(line), fidx) && total_entries < 100) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        strncpy(entries[total_entries], line, sizeof(entries[total_entries]) - 1);
        total_entries++;
    }
    fclose(fidx);

    if (total_entries == 0) {
        fprintf(temp_file, "No history entries found.\n");
    } else {
        // Display entries in reverse chronological order (newest first)
        for (int i = total_entries - 1; i >= 0; i--) {
            count++;
            long timestamp = 0;
            char time_str[64] = {0};
            char reason[128] = {0};
            char snap_file[128] = {0};

            if (sscanf(entries[i], "%ld|%63[^|]|%127[^|]|%127s", &timestamp, time_str, reason, snap_file) >= 3) {
                char snap_path[PATH_MAX];
                snprintf(snap_path, sizeof(snap_path), "%s/%s", history_dir, snap_file);
                long size = 0;
                struct stat st;
                if (stat(snap_path, &st) == 0) size = st.st_size;

                fprintf(temp_file, " [%2d]  %s  |  %-16s  (%ld bytes)\n", 
                        total_entries - i, time_str, reason, size);
            }
        }
    }

    fclose(temp_file);
    display_help_viewer(temp_filename);
    free(temp_filename);
}

void restore_local_history_entry(EditorState *state, int entry_idx) {
    if (!state || !state->buffer.filename[0] || entry_idx <= 0) {
        editor_set_status_msg(state, "Usage: :history-restore <number>");
        return;
    }

    char history_dir[PATH_MAX];
    get_file_history_dir(state->buffer.filename, history_dir, sizeof(history_dir));

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index.txt", history_dir);

    FILE *fidx = fopen(index_path, "r");
    if (!fidx) {
        editor_set_status_msg(state, "No history found for this file.");
        return;
    }

    char entries[100][512];
    int total_entries = 0;

    char line[512];
    while (fgets(line, sizeof(line), fidx) && total_entries < 100) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        strncpy(entries[total_entries], line, sizeof(entries[total_entries]) - 1);
        total_entries++;
    }
    fclose(fidx);

    if (entry_idx > total_entries) {
        editor_set_status_msg(state, "Invalid entry number %d (max: %d)", entry_idx, total_entries);
        return;
    }

    int target_i = total_entries - entry_idx;
    long timestamp = 0;
    char time_str[64] = {0};
    char reason[128] = {0};
    char snap_file[128] = {0};

    if (sscanf(entries[target_i], "%ld|%63[^|]|%127[^|]|%127s", &timestamp, time_str, reason, snap_file) < 4) {
        editor_set_status_msg(state, "Error parsing history entry.");
        return;
    }

    char snap_path[PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/%s", history_dir, snap_file);

    if (access(snap_path, F_OK) != 0) {
        editor_set_status_msg(state, "Snapshot file missing.");
        return;
    }

    push_undo(state);
    clear_redo_stack(state);

    // Free existing lines
    for (int i = 0; i < state->buffer.num_lines; i++) {
        if (state->buffer.lines[i]) {
            free(state->buffer.lines[i]);
            state->buffer.lines[i] = NULL;
        }
    }
    state->buffer.num_lines = 0;

    FILE *fsnap = fopen(snap_path, "r");
    if (fsnap) {
        char sline[MAX_LINE_LEN];
        while (fgets(sline, sizeof(sline), fsnap) && state->buffer.num_lines < MAX_LINES) {
            sline[strcspn(sline, "\n\r")] = '\0';
            state->buffer.lines[state->buffer.num_lines] = strdup(sline);
            state->buffer.num_lines++;
        }
        fclose(fsnap);

        if (state->buffer.num_lines == 0) {
            state->buffer.lines[0] = strdup("");
            state->buffer.num_lines = 1;
        }

        state->cursor.line = 0;
        state->cursor.col = 0;
        state->cursor.ideal_col = 0;
        state->buffer.modified = true;
        state->buffer.is_dirty = true;
        editor_set_status_msg(state, "Restored local history snapshot [%d] (%s)", entry_idx, time_str);
    }
}

void diff_local_history_entry(EditorState *state, int entry_idx) {
    if (!state || !state->buffer.filename[0] || entry_idx <= 0) {
        editor_set_status_msg(state, "Usage: :history-diff <number>");
        return;
    }

    char history_dir[PATH_MAX];
    get_file_history_dir(state->buffer.filename, history_dir, sizeof(history_dir));

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index.txt", history_dir);

    FILE *fidx = fopen(index_path, "r");
    if (!fidx) {
        editor_set_status_msg(state, "No history found for this file.");
        return;
    }

    char entries[100][512];
    int total_entries = 0;

    char line[512];
    while (fgets(line, sizeof(line), fidx) && total_entries < 100) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        strncpy(entries[total_entries], line, sizeof(entries[total_entries]) - 1);
        total_entries++;
    }
    fclose(fidx);

    if (entry_idx > total_entries) {
        editor_set_status_msg(state, "Invalid entry number %d (max: %d)", entry_idx, total_entries);
        return;
    }

    int target_i = total_entries - entry_idx;
    long timestamp = 0;
    char time_str[64] = {0};
    char reason[128] = {0};
    char snap_file[128] = {0};

    if (sscanf(entries[target_i], "%ld|%63[^|]|%127[^|]|%127s", &timestamp, time_str, reason, snap_file) < 4) {
        editor_set_status_msg(state, "Error parsing history entry.");
        return;
    }

    char snap_path[PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/%s", history_dir, snap_file);

    if (access(snap_path, F_OK) != 0) {
        editor_set_status_msg(state, "Snapshot file missing.");
        return;
    }

    show_diff_between_files(state->buffer.filename, snap_path);
}
