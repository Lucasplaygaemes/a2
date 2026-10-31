#include "headless.h"
#include "command_execution.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>

void headless_dump(EditorState *state) {
    if (!state) return;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        if (state->buffer.lines[i]) {
            puts(state->buffer.lines[i]);
        } else {
            putchar('\n');
        }
    }
}

void headless_print_buffer(EditorState *state, int start_line, int end_line) {
    if (!state) return;
    if (state->buffer.num_lines <= 0) {
        printf("(buffer is empty)\n");
        return;
    }

    if (start_line < 1) start_line = 1;
    if (end_line > state->buffer.num_lines || end_line < start_line) {
        end_line = state->buffer.num_lines;
    }

    for (int i = start_line - 1; i < end_line; i++) {
        const char *line = state->buffer.lines[i] ? state->buffer.lines[i] : "";
        printf("%5d | %s\n", i + 1, line);
    }
}

bool headless_exec_command(EditorState *state, const char *cmd, bool *should_exit) {
    if (!state || !cmd) return false;

    while (isspace((unsigned char)*cmd)) cmd++;
    if (*cmd == '\0') return false;

    // Remove leading ':' if present
    const char *actual_cmd = (*cmd == ':') ? (cmd + 1) : cmd;

    strncpy(state->input.command_buffer, actual_cmd, sizeof(state->input.command_buffer) - 1);
    state->input.command_buffer[sizeof(state->input.command_buffer) - 1] = '\0';
    state->view.status_msg[0] = '\0';

    process_command(state, should_exit);

    if (state->view.status_msg[0] != '\0') {
        printf("[a2] %s\n", state->view.status_msg);
    }
    return true;
}

static void print_headless_help(void) {
    printf("Headless mode commands:\n");
    printf("  :p [start] [end]   Print buffer lines (e.g. ':p 1 20' or ':p')\n");
    printf("  :dump              Dump the entire buffer to stdout\n");
    printf("  :info              Show buffer and cursor metadata\n");
    printf("  :w [filename]      Save file to disk\n");
    printf("  :s/find/repl/      Substitute in buffer\n");
    printf("  :help              Show this help message\n");
    printf("  :q / :q!           Quit headless mode\n");
    printf("  :<any a2 command>  Execute any standard a2 command\n");
}

// ─── Mini line editor for the REPL ───────────────────────────────────────────

#define REPL_HISTORY_MAX 64
#define REPL_LINE_MAX    1024

static char  repl_history[REPL_HISTORY_MAX][REPL_LINE_MAX];
static int   repl_history_count = 0;

static void repl_history_add(const char *line) {
    if (!line || !line[0]) return;
    // don't duplicate the last entry
    if (repl_history_count > 0 &&
        strcmp(repl_history[repl_history_count - 1], line) == 0) return;
    if (repl_history_count < REPL_HISTORY_MAX) {
        snprintf(repl_history[repl_history_count], REPL_LINE_MAX, "%s", line);
        repl_history_count++;
    } else {
        // shift everything up
        memmove(repl_history[0], repl_history[1],
                (REPL_HISTORY_MAX - 1) * REPL_LINE_MAX);
        snprintf(repl_history[REPL_HISTORY_MAX - 1], REPL_LINE_MAX, "%s", line);
    }
}

// Returns length of line read, or -1 on EOF.
// When stdin is not a tty (pipe), falls back to plain fgets.
static int repl_readline(const char *prompt, char *buf, int buf_size) {
    int is_tty = isatty(STDIN_FILENO);

    if (!is_tty) {
        // pipe / redirect — use simple fgets
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(buf, buf_size, stdin)) return -1;
        // strip trailing newlines
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        return len;
    }

    // Raw mode for interactive editing
    struct termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("%s", prompt);
    fflush(stdout);

    int len = 0;       // current length
    int pos = 0;       // cursor position within buf
    int hist_idx = repl_history_count; // browsing index
    char saved_line[REPL_LINE_MAX] = "";  // save current input when browsing history

    buf[0] = '\0';

    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) { // EOF
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
            return -1;
        }

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        } else if (c == 4) { // Ctrl+D
            if (len == 0) {
                write(STDOUT_FILENO, "\n", 1);
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
                return -1;
            }
        } else if (c == 3) { // Ctrl+C — clear line
            // erase displayed line
            printf("\r%s%*s\r%s", prompt, len, "", prompt);
            fflush(stdout);
            len = 0; pos = 0; buf[0] = '\0';
        } else if (c == 127 || c == 8) { // Backspace
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, (size_t)(len - pos + 1));
                pos--; len--;
                // Redraw from cursor position
                printf("\r%s%s", prompt, buf);
                // Clear any trailing chars
                printf(" ");
                // Reposition cursor
                int back = len - pos + 1;
                for (int i = 0; i < back; i++) write(STDOUT_FILENO, "\b", 1);
                fflush(stdout);
            }
        } else if (c == 1) { // Ctrl+A — home
            while (pos > 0) { write(STDOUT_FILENO, "\b", 1); pos--; }
        } else if (c == 5) { // Ctrl+E — end
            while (pos < len) { write(STDOUT_FILENO, buf + pos, 1); pos++; }
        } else if (c == 21) { // Ctrl+U — clear to start
            printf("\r%s%*s\r%s%s", prompt, len, "", prompt, buf + pos);
            memmove(buf, buf + pos, (size_t)(len - pos + 1));
            len -= pos;
            pos = 0;
            int back = len;
            for (int i = 0; i < back; i++) write(STDOUT_FILENO, "\b", 1);
            fflush(stdout);
        } else if (c == 27) { // Escape sequence
            unsigned char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
                switch (seq[1]) {
                    case 'A': // Up arrow — history previous
                        if (hist_idx > 0) {
                            if (hist_idx == repl_history_count) {
                                snprintf(saved_line, sizeof(saved_line), "%s", buf);
                            }
                            hist_idx--;
                            snprintf(buf, buf_size, "%s", repl_history[hist_idx]);
                            len = (int)strlen(buf);
                            pos = len;
                            printf("\r%s%s\033[K", prompt, buf);
                            fflush(stdout);
                        }
                        break;
                    case 'B': // Down arrow — history next
                        if (hist_idx < repl_history_count) {
                            hist_idx++;
                            if (hist_idx == repl_history_count) {
                                snprintf(buf, buf_size, "%s", saved_line);
                            } else {
                                snprintf(buf, buf_size, "%s", repl_history[hist_idx]);
                            }
                            len = (int)strlen(buf);
                            pos = len;
                            printf("\r%s%s\033[K", prompt, buf);
                            fflush(stdout);
                        }
                        break;
                    case 'C': // Right arrow
                        if (pos < len) {
                            write(STDOUT_FILENO, buf + pos, 1);
                            pos++;
                        }
                        break;
                    case 'D': // Left arrow
                        if (pos > 0) {
                            write(STDOUT_FILENO, "\b", 1);
                            pos--;
                        }
                        break;
                    case 'H': // Home
                        while (pos > 0) { write(STDOUT_FILENO, "\b", 1); pos--; }
                        break;
                    case 'F': // End
                        while (pos < len) { write(STDOUT_FILENO, buf + pos, 1); pos++; }
                        break;
                    case '3': // Delete key (ESC [ 3 ~)
                        read(STDIN_FILENO, &seq[2], 1); // consume '~'
                        if (pos < len) {
                            memmove(buf + pos, buf + pos + 1, (size_t)(len - pos));
                            len--;
                            printf("%s \b", buf + pos);
                            int back = len - pos;
                            for (int i = 0; i < back; i++) write(STDOUT_FILENO, "\b", 1);
                            fflush(stdout);
                        }
                        break;
                    default:
                        break;
                }
            }
        } else if (c >= 32 && c < 127) { // Printable ASCII
            if (len < buf_size - 1) {
                memmove(buf + pos + 1, buf + pos, (size_t)(len - pos + 1));
                buf[pos] = (char)c;
                len++; pos++;
                // Redraw from cursor
                write(STDOUT_FILENO, buf + pos - 1, (size_t)(len - pos + 1));
                int back = len - pos;
                for (int i = 0; i < back; i++) write(STDOUT_FILENO, "\b", 1);
                fflush(stdout);
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    buf[len] = '\0';
    return len;
}

void headless_repl(EditorState *state) {
    if (!state) return;

    const char *fn = (state->buffer.filename[0] != '\0') ? state->buffer.filename : "[No Name]";
    printf("[a2 HEADLESS] Loaded '%s' (%d lines).\n", fn, state->buffer.num_lines);
    printf("Type :help for headless commands, :p to view buffer, :q to quit.\n");

    char line_buf[REPL_LINE_MAX];
    bool should_exit = false;

    while (!should_exit) {
        int n = repl_readline("a2> ", line_buf, REPL_LINE_MAX);
        if (n < 0) {
            printf("Exiting headless mode.\n");
            break;
        }

        // Trim whitespace
        char *cmd = line_buf;
        while (isspace((unsigned char)*cmd)) cmd++;
        if (*cmd == '\0') continue;

        repl_history_add(cmd);

        // Custom REPL commands
        if (strcmp(cmd, ":q") == 0 || strcmp(cmd, ":q!") == 0 || strcmp(cmd, "q") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, ":help") == 0 || strcmp(cmd, "help") == 0) {
            print_headless_help();
        } else if (strcmp(cmd, ":dump") == 0) {
            headless_dump(state);
        } else if (strcmp(cmd, ":info") == 0) {
            printf("File: %s\n", state->buffer.filename[0] ? state->buffer.filename : "[No Name]");
            printf("Lines: %d\n", state->buffer.num_lines);
            printf("Modified: %s\n", state->buffer.modified ? "yes" : "no");
            printf("Cursor: line %d, col %d\n", state->cursor.line + 1, state->cursor.col + 1);
        } else if (strncmp(cmd, ":p", 2) == 0 && (cmd[2] == '\0' || isspace((unsigned char)cmd[2]))) {
            int start = 1, end_line = state->buffer.num_lines;
            char *args = cmd + 2;
            while (isspace((unsigned char)*args)) args++;
            if (*args != '\0') {
                if (sscanf(args, "%d %d", &start, &end_line) == 1) {
                    end_line = start + 20; // default 20 lines if only start given
                }
            } else if (state->buffer.num_lines > 50) {
                end_line = 50; // default 50 lines for huge buffers without args
            }
            headless_print_buffer(state, start, end_line);
        } else {
            headless_exec_command(state, cmd, &should_exit);
        }
    }
}

int headless_run(EditorState *state, int cmd_count, char **cmds, bool dump_mode) {
    if (!state) return 1;

    bool should_exit = false;
    for (int i = 0; i < cmd_count; i++) {
        if (!headless_exec_command(state, cmds[i], &should_exit)) {
            fprintf(stderr, "Failed to execute headless command: %s\n", cmds[i]);
        }
        if (should_exit) break;
    }

    if (dump_mode) {
        headless_dump(state);
        return 0;
    }

    if (cmd_count == 0) {
        headless_repl(state);
    }

    return 0;
}
