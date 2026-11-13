#include <fcntl.h>
#include <pty.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "window_management.h"
#include "io_utils.h"
#include "logger.h"

#define ACTIVE_WS (workspace_manager.workspaces[workspace_manager.active_workspace_idx])

void update_size_pty(EditorWindow *jw) {
    if (jw->type != WINDOW_TYPE_TERMINAL || jw->term.pty_fd == -1) return;

    int rows, cols;
    if (jw->content_win) {
        getmaxyx(jw->content_win, rows, cols);
    } else {
        int border_offset = ACTIVE_WS->num_windows > 1 ? 1 : 0;
        rows = jw->height - (2 * border_offset);
        cols = jw->width - (2 * border_offset);
    }

    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    ioctl(jw->term.pty_fd, TIOCSWINSZ, &ws);
}

void create_generic_terminal_window(char *const argv[]) {
    if (!argv || !argv[0]) return;

    Workspace *ws = ACTIVE_WS;
    int old_active_idx = ws->active_window_idx;
    EditorWindow **grown_windows = realloc(ws->windows, sizeof(EditorWindow*) * (ws->num_windows + 1));
    if (!grown_windows) { perror("realloc failed"); return; }
    ws->windows = grown_windows;
    ws->num_windows++;

    EditorWindow *jw = calloc(1, sizeof(EditorWindow));
    if (!jw) { perror("calloc failed"); ws->num_windows--; return; }
    ws->windows[ws->num_windows - 1] = jw;
    ws->active_window_idx = ws->num_windows - 1;

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);

    if (pid < 0) {
        perror("forkpty failed");
        // Restore the previous active window when terminal creation fails.
        ws->windows[--ws->num_windows] = NULL;
        ws->active_window_idx = old_active_idx;
        free(jw);
        return;
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execvp(argv[0], argv);
        exit(127);
    }

    recalculate_window_layout();
    if (!jw->win || !jw->content_win) {
        // Stop the child and roll back if the current layout cannot fit a terminal.
        close(master_fd);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        ws->windows[--ws->num_windows] = NULL;
        ws->active_window_idx = old_active_idx;
        free(jw);
        recalculate_window_layout();
        return;
    }

    jw->type = WINDOW_TYPE_TERMINAL;
    jw->term.pid = pid;
    jw->term.pty_fd = master_fd;
    fcntl(master_fd, F_SETFL, O_NONBLOCK);

    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    int border_offset = ws->num_windows > 1 ? 1 : 0;

    // make sure the content_win exist
    if (!jw->content_win) jw->content_win = jw->win;

    jw->term.vterm = vterm_create(cols - 2 * border_offset, rows - 2 * border_offset, VTERM_FLAG_XTERM_256);
    if (!jw->term.vterm) {
        // Release the child and window if the terminal emulator cannot initialize.
        A2_LOG(LOG_ERROR, TAG_UI, "Could not initialize embedded terminal");
        ws->windows[--ws->num_windows] = NULL;
        ws->active_window_idx = old_active_idx;
        free_editor_window(jw);
        recalculate_window_layout();
        return;
    }
    vterm_wnd_set(jw->term.vterm, jw->content_win);
    vterm_set_userptr(jw->term.vterm, jw);

    vterm_resize(jw->term.vterm, cols - 2 * border_offset, rows - 2 * border_offset);
    update_size_pty(jw);
    redraw_all_windows();
}

void toggle_floating_terminal() {
    Workspace *ws = ACTIVE_WS;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // Re-create terminal if it was closed or finished
    if (ws->floating_term != NULL && ws->floating_term->term.pty_fd == -1) {
        free_editor_window(ws->floating_term);
        ws->floating_term = NULL;
    }

    if (ws->floating_term == NULL) {
        // Criar o terminal se não existir
        ws->floating_term = calloc(1, sizeof(EditorWindow));
        if (!ws->floating_term) return;
        ws->floating_term->type = WINDOW_TYPE_TERMINAL;

        // Define posição flutuante (centralizado, 80% da tela)
        if (rows < 3 || cols < 3) {
            free(ws->floating_term);
            ws->floating_term = NULL;
            return;
        }
        ws->floating_term->height = min(rows, max(5, (int)(rows * 0.8)));
        ws->floating_term->width = min(cols, max(20, (int)(cols * 0.8)));
        ws->floating_term->y = (rows - ws->floating_term->height) / 2;
        ws->floating_term->x = (cols - ws->floating_term->width) / 2;

        int master_fd;
        pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);

        if (pid < 0) {
            free(ws->floating_term);
            ws->floating_term = NULL;
            return;
        }
        if (pid == 0) {
            setenv("TERM", "xterm-256color", 1);
            execlp("/bin/bash", "/bin/bash", NULL);
            exit(127);
        }

        ws->floating_term->term.pid = pid;
        ws->floating_term->term.pty_fd = master_fd;
        fcntl(master_fd, F_SETFL, O_NONBLOCK);

        ws->floating_term->win = newwin(ws->floating_term->height, ws->floating_term->width,
                                        ws->floating_term->y, ws->floating_term->x);
        if (ws->floating_term->win) {
            ws->floating_term->content_win = derwin(ws->floating_term->win, ws->floating_term->height - 2, ws->floating_term->width - 2, 1, 1);
        }
        if (!ws->floating_term->win || !ws->floating_term->content_win) {
            // Release the child and partial curses windows if allocation fails.
            if (ws->floating_term->content_win) delwin(ws->floating_term->content_win);
            if (ws->floating_term->win) delwin(ws->floating_term->win);
            close(master_fd);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            free(ws->floating_term);
            ws->floating_term = NULL;
            return;
        }
        keypad(ws->floating_term->win, TRUE);
        keypad(ws->floating_term->content_win, TRUE);

        ws->floating_term->term.vterm = vterm_create(ws->floating_term->width - 2, ws->floating_term->height - 2, VTERM_FLAG_XTERM_256);
        if (!ws->floating_term->term.vterm) {
            // Release terminal resources if libvterm cannot allocate its state.
            A2_LOG(LOG_ERROR, TAG_UI, "Could not initialize floating terminal");
            free_editor_window(ws->floating_term);
            ws->floating_term = NULL;
            return;
        }
        vterm_wnd_set(ws->floating_term->term.vterm, ws->floating_term->content_win);
        vterm_set_userptr(ws->floating_term->term.vterm, ws->floating_term);

        ws->floating_terminal_visible = true;
        update_size_pty(ws->floating_term);
    } else {
        ws->floating_terminal_visible = !ws->floating_terminal_visible;
    }

    if (ws->floating_terminal_visible) {
        touchwin(stdscr);
    }
    redraw_all_windows();
}

void execute_command_in_terminal(const char *comando_str) {
    // If no command is specified, open a default shell
    if (strlen(comando_str) == 0) {
        char *const cmd[] = {"/bin/bash", NULL};
        create_generic_terminal_window(cmd);
        return;
    }

    // Create a copy of the string, as strtok modifies it
    char *str_copia = strdup(comando_str);
    if (!str_copia) return;

    // Array to store the arguments (e.g., "btop", "--utf-force")
    char **argv = NULL;
    int argc = 0;

    // Use strtok to split the string into words (tokens) separated by spaces
    char *token = strtok(str_copia, " ");
    while (token != NULL) {
        argc++;
        argv = realloc(argv, sizeof(char*) * argc);
        argv[argc - 1] = token;
        token = strtok(NULL, " ");
    }

    // Add NULL at the end, which is required for the execvp function
    argc++;
    argv = realloc(argv, sizeof(char*) * argc);
    argv[argc - 1] = NULL;

    // Call our magic function that is already prepared!
    if (argv) {
        create_generic_terminal_window(argv);
    }

    // Free the memory we allocated
    free(str_copia);
    free(argv);
}

void execute_command_in_new_workspace(const char *comando_str) {
    create_new_empty_workspace();

    if (strlen(comando_str) == 0) {
        char *const cmd[] = {"/bin/bash", NULL};
        create_generic_terminal_window(cmd);
        return;
    }

    char *str_copia = strdup(comando_str);
    if (!str_copia) return;

    char **argv = NULL;
    int argc = 0;

    char *token = strtok(str_copia, " ");
    while (token != NULL) {
        argc++;
        argv = realloc(argv, sizeof(char*) * argc);
        argv[argc - 1] = token;
        token = strtok(NULL, " ");
    }

    argc++;
    argv = realloc(argv, sizeof(char*) * argc);
    argv[argc - 1] = NULL;

    if (argv) {
        create_generic_terminal_window(argv);
    }

    free(str_copia);
    free(argv);
}

void execute_command_in_split(const char *comando_str) {
    Workspace *ws = ACTIVE_WS;

    // Split the window if there is only 1
    if (ws->num_windows == 1) {
        ws->current_layout = LAYOUT_VERTICAL_SPLIT;
    }

    if (strlen(comando_str) == 0) {
        char *const cmd[] = {"/bin/bash", NULL};
        create_generic_terminal_window(cmd);
        recalculate_window_layout();
        return;
    }

    char *str_copia = strdup(comando_str);
    if (!str_copia) return;

    char **argv = NULL;
    int argc = 0;

    char *token = strtok(str_copia, " ");
    while (token != NULL) {
        argc++;
        argv = realloc(argv, sizeof(char*) * argc);
        argv[argc - 1] = token;
        token = strtok(NULL, " ");
    }

    argc++;
    argv = realloc(argv, sizeof(char*) * argc);
    argv[argc - 1] = NULL;

    if (argv) {
        create_generic_terminal_window(argv);
    }

    free(str_copia);
    free(argv);

    recalculate_window_layout();
}

bool send_cmd_to_terminal_window(const char *cmd_str) {
    Workspace *ws = ACTIVE_WS;
    if (!ws) return false;
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (jw && jw->type == WINDOW_TYPE_TERMINAL && jw->term.pty_fd > 0) {
            return a2_write_all(jw->term.pty_fd, cmd_str, strlen(cmd_str)) &&
                   a2_write_all(jw->term.pty_fd, "\n", 1);
        }
    }
    if (ws->floating_term && ws->floating_term->term.pty_fd > 0) {
        return a2_write_all(ws->floating_term->term.pty_fd, cmd_str, strlen(cmd_str)) &&
               a2_write_all(ws->floating_term->term.pty_fd, "\n", 1);
    }
    return false;
}