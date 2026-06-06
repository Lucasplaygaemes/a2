#include "editor_utils.h"
#include "others.h"
#include "themes.h"
#include "fileio.h"
#include "defs.h"
#include "lsp_client.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "command_execution.h"
#include "cache.h"
#include "settings.h"
#include "diff.h"
#include "project.h"
#include "timer.h"
#include "direct_navigation.h"
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <wctype.h>
#include <errno.h>
#include <regex.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>

const char *editor_commands[] = {
    "q", "q!", "w", "wq", "help", "about", "gcc", "rc", "rc!", "open", "new", "timer", "diff", "set",
    "lsp-restart", "lsp-diag", "lsp-definition", "lsp-references", "lsp-rename",
    "lsp-status", "lsp-hover", "lsp-symbols", "lsp-refresh", "lsp-check", "lsp-debug",
    "lsp-list", "toggle_auto_indent", "llvm", "logs"
};
const int num_editor_commands = sizeof(editor_commands) / sizeof(char*);

typedef struct {
    const char *header_name;
    const char **symbols;
    int count;
} KnownHeader;

typedef struct {
    const char *header;
    const char *flag;
} HeaderFlagMap;

static const HeaderFlagMap header_flags[] = {
    {"aio.h", "-lrt"}, {"alsa/asoundlib.h", "-lasound"}, {"archive.h", "-larchive"},
    {"bluetooth/bluetooth.h", "-lbluetooth"}, {"bzlib.h", "-lbz2"}, {"cairo.h", "-lcairo"},
    {"curses.h", "-lncursesw"}, {"crypt.h", "-lcrypt"}, {"curl/curl.h", "-lcurl"},
    {"dlfcn.h", "-ldl"}, {"execinfo.h", "-lexecinfo"}, {"fontconfig/fontconfig.h", "-lfontconfig"},
    {"freetype2/ft2build.h", "-lfreetype"}, {"gdbm.h", "-lgdbm"}, {"gd.h", "-lgd"},
    {"gmp.h", "-lgmp"}, {"gnutls/gnutls.h", "-lgnutls"}, {"history.h", "-lreadline"},
    {"id3tag.h", "-lid3tag"}, {"jpeglib.h", "-ljpeg"}, {"json-c/json.h", "-ljson-c"},
    {"lcms2.h", "-llcms2"}, {"lzma.h", "-llzma"}, {"lzo/lzo1x.h", "-llzo2"},
    {"mariadb/mysql.h", "-lmariadb"}, {"math.h", "-lm"}, {"mpfr.h", "-lmpfr"},
    {"ncurses.h", "-lncursesw"}, {"netdb.h", "-lnsl"}, {"netinet/in.h", ""},
    {"notify.h", "-lnotify"}, {"odbcss.h", "-lodbc"}, {"openal/al.h", "-lopenal"},
    {"openssl/ssl.h", "-lssl -lcrypto"}, {"panel.h", "-lpanel"}, {"pcap.h", "-lpcap"},
    {"pcre.h", "-lpcre"}, {"png.h", "-lpng"}, {"postgresql/libpq-fe.h", "-lpq"},
    {"pulse/pulseaudio.h", "-lpulse"}, {"pthread.h", "-lpthread"}, {"readline/readline.h", "-lreadline"},
    {"sasl/sasl.h", "-lsasl2"}, {"sdl2/SDL.h", "-lSDL2"}, {"sqlite3.h", "-lsqlite3"},
    {"tiff.h", "-ltiff"}, {"tirpc/rpc/rpc.h", "-ltirpc"}, {"udev.h", "-ludev"},
    {"usb.h", "-lusb"}, {"uuid/uuid.h", "-luuid"}, {"vorbis/codec.h", "-lvorbis"},
    {"X11/Xlib.h", "-lX11"}, {"xml2/libxml2.h", "-lxml2"}, {"xslt/xsltutils.h", "-lxslt"},
    {"yaml.h", "-lyaml"}, {"zlib.h", "-lz"}, {"zstd.h", "-lzstd"}
};
const int num_header_flags = sizeof(header_flags) / sizeof(header_flags[0]);
const KnownHeader known_headers[] = {};
const int num_known_headers = 0;

void editor_update_git_gutter(EditorState *state) {
    if (!state || strcmp(state->buffer.filename, "[No Name]") == 0) return;
    if (!global_config.git_gutter_enabled) {
        if (state->buffer.git_gutter) { free(state->buffer.git_gutter); state->buffer.git_gutter = NULL; }
        return;
    }
    if (state->buffer.git_gutter) free(state->buffer.git_gutter);
    state->buffer.git_gutter = malloc(state->buffer.num_lines);
    memset(state->buffer.git_gutter, ' ', state->buffer.num_lines);
    char cmd[PATH_MAX + 100];
    snprintf(cmd, sizeof(cmd), "git diff --unified=0 \"%s\" 2>/dev/null", state->buffer.filename);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "@@", 2) == 0) {
            int old_start, old_count = 1, new_start, new_count = 1;
            char *minus = strchr(line, '-');
            char *plus = strchr(line, '+');
            if (!minus || !plus) continue;
            if (sscanf(minus, "-%d,%d", &old_start, &old_count) != 2) sscanf(minus, "-%d", &old_start);
            if (sscanf(plus, "+%d,%d", &new_start, &new_count) != 2) sscanf(plus, "+%d", &new_start);
            if (new_count == 0) {
                int idx = new_start;
                if (idx >= 0 && idx < state->buffer.num_lines) state->buffer.git_gutter[idx] = '-';
            } else if (old_count == 0) {
                for (int i = 0; i < new_count; i++) {
                    int idx = new_start + i - 1;
                    if (idx >= 0 && idx < state->buffer.num_lines) state->buffer.git_gutter[idx] = '+';
                }
            } else {
                for (int i = 0; i < new_count; i++) {
                    int idx = new_start + i - 1;
                    if (idx >= 0 && idx < state->buffer.num_lines) state->buffer.git_gutter[idx] = '~';
                }
            }
        }
    }
    pclose(fp);
    state->buffer.is_dirty = true;
}

void editor_set_status_msg(EditorState *state, const char *format, ...) {
    if (!state) return;
    va_list args;
    va_start(args, format);
    vsnprintf(state->view.status_msg, sizeof(state->view.status_msg), format, args);
    va_end(args);
    state->buffer.is_dirty = true;
}

void editor_find_unmatched_brackets(EditorState *state) {
    if (state->buffer.unmatched_brackets) free(state->buffer.unmatched_brackets);
    state->buffer.unmatched_brackets = NULL;
    state->buffer.num_unmatched_brackets = 0;
    typedef struct { int line; int col; char type; } BracketStackItem;
    BracketStackItem *stack = NULL;
    int stack_top = 0, stack_capacity = 0;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        char *line = state->buffer.lines[i];
        if (!line) continue;
        bool in_string = false; char string_char = 0;
        for (int j = 0; line[j] != '\0'; j++) {
            if (in_string) {
                if (line[j] == '\\') { j++; continue; }
                if (line[j] == string_char) in_string = false;
                continue;
            }
            if (line[j] == '"' || line[j] == '\'') { in_string = true; string_char = line[j]; continue; }
            if (line[j] == '/' && line[j+1] != '\0' && line[j+1] == '/') break;
            char c = line[j];
            if (c == '(' || c == '[' || c == '{') {
                if (stack_top >= stack_capacity) {
                    stack_capacity = (stack_capacity == 0) ? 8 : stack_capacity * 2;
                    stack = realloc(stack, stack_capacity * sizeof(BracketStackItem));
                }
                stack[stack_top++] = (BracketStackItem){ .line = i, .col = j, .type = c };
            } else if (c == ')' || c == ']' || c == '}') {
                if (stack_top > 0) {
                    char open_bracket = stack[stack_top - 1].type;
                    if ((c == ')' && open_bracket == '(') || (c == ']' && open_bracket == '[') || (c == '}' && open_bracket == '{')) stack_top--;
                    else {
                        state->buffer.num_unmatched_brackets++;
                        state->buffer.unmatched_brackets = realloc(state->buffer.unmatched_brackets, state->buffer.num_unmatched_brackets * sizeof(BracketInfo));
                        state->buffer.unmatched_brackets[state->buffer.num_unmatched_brackets - 1] = (BracketInfo){ .line = i, .col = j, .type = c };
                    }
                } else {
                    state->buffer.num_unmatched_brackets++;
                    state->buffer.unmatched_brackets = realloc(state->buffer.unmatched_brackets, state->buffer.num_unmatched_brackets * sizeof(BracketInfo));
                    state->buffer.unmatched_brackets[state->buffer.num_unmatched_brackets - 1] = (BracketInfo){ .line = i, .col = j, .type = c };
                }
            }
        }
    }
    if (stack_top > 0) {
        int old_num = state->buffer.num_unmatched_brackets;
        state->buffer.num_unmatched_brackets += stack_top;
        state->buffer.unmatched_brackets = realloc(state->buffer.unmatched_brackets, state->buffer.num_unmatched_brackets * sizeof(BracketInfo));
        for (int k = 0; k < stack_top; k++) state->buffer.unmatched_brackets[old_num + k] = (BracketInfo){ .line = stack[k].line, .col = stack[k].col, .type = stack[k].type };
    }
    if (stack) free(stack);
}

bool is_unmatched_bracket(EditorState *state, int line, int col) {
    for (int i = 0; i < state->buffer.num_unmatched_brackets; i++) {
        if (state->buffer.unmatched_brackets[i].line == line && state->buffer.unmatched_brackets[i].col == col) return true;
    }
    return false;
}

void editor_ensure_dirty_lines_capacity(EditorState *state, int required_capacity) {
    if (required_capacity > state->buffer.dirty_lines_cap) {
        int old_cap = state->buffer.dirty_lines_cap;
        int new_cap = (state->buffer.dirty_lines_cap == 0) ? 128 : state->buffer.dirty_lines_cap;
        while (new_cap < required_capacity) new_cap *= 2;
        state->buffer.dirty_lines_cap = new_cap;
        state->buffer.dirty_lines = realloc(state->buffer.dirty_lines, sizeof(bool) * state->buffer.dirty_lines_cap);
        for (int i = old_cap; i < state->buffer.dirty_lines_cap; i++) state->buffer.dirty_lines[i] = true;
    }
}

void mark_line_as_dirty(EditorState *state, int line_num) {
    state->buffer.is_dirty = true;
    editor_ensure_dirty_lines_capacity(state, line_num + 1);
    if (line_num >= 0 && line_num < state->buffer.num_lines) state->buffer.dirty_lines[line_num] = true;
}

void mark_all_lines_dirty(EditorState *state) {
    editor_ensure_dirty_lines_capacity(state, state->buffer.num_lines);
    for (int i = 0; i < state->buffer.num_lines; i++) state->buffer.dirty_lines[i] = true;
    state->buffer.is_dirty = true;
}

char* trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void ensure_cursor_in_bounds(EditorState *state) {
    if (state->buffer.num_lines == 0) { state->cursor.line = 0; state->cursor.col = 0; return; }
    if (state->cursor.line >= state->buffer.num_lines) state->cursor.line = state->buffer.num_lines - 1;
    if (state->cursor.line < 0) state->cursor.line = 0;
    char *line = state->buffer.lines[state->cursor.line];
    int line_len = line ? strlen(line) : 0;
    if (state->cursor.col > line_len) state->cursor.col = line_len;
    if (state->cursor.col < 0) state->cursor.col = 0;
}

void editor_move_to_next_word(EditorState *state) {
    if (!state) return;
    char *line = state->buffer.lines[state->cursor.line]; if (!line) return;
    int len = strlen(line);
    while (state->cursor.col < len && isspace(line[state->cursor.col])) state->cursor.col++;
    while (state->cursor.col < len && !isspace(line[state->cursor.col])) state->cursor.col++;
    state->cursor.ideal_col = state->cursor.col;
}

void editor_move_to_previous_word(EditorState *state) {
    char *line = state->buffer.lines[state->cursor.line]; if (!line || state->cursor.col == 0) return;
    while (state->cursor.col > 0 && isspace(line[state->cursor.col - 1])) state->cursor.col--;
    while (state->cursor.col > 0 && !isspace(line[state->cursor.col - 1])) state->cursor.col--;
    state->cursor.ideal_col = state->cursor.col;
}

bool is_line_blank(const char *line) {
    if (!line) return true;
    while (*line) { if (!isspace((unsigned char)*line)) return false; line++; }
    return true;
}

void editor_jump_to_matching_bracket(EditorState *state) {
    if (state->cursor.line >= state->buffer.num_lines) return;
    char *line = state->buffer.lines[state->cursor.line];
    if (state->cursor.col >= (int)strlen(line)) return;

    char open_char = 0, close_char = 0;
    int direction = 0;
    char current_char = line[state->cursor.col];

    if (strchr("([{", current_char)) {
        direction = 1; open_char = current_char;
        close_char = (current_char == '(') ? ')' : (current_char == '[') ? ']' : '}';
    } else if (strchr(")]}", current_char)) {
        direction = -1; close_char = current_char;
        open_char = (current_char == ')') ? '(' : (current_char == ']') ? '[' : '{';
    } else return;

    int nest_level = 1;
    int l = state->cursor.line, c = state->cursor.col + direction;
    bool in_string = false, in_multiline_comment = false;
    char string_delimiter = 0;

    while (l >= 0 && l < state->buffer.num_lines) {
        char *scan_line = state->buffer.lines[l];
        int line_len = strlen(scan_line);
        while (c >= 0 && c < line_len) {
            char current = scan_line[c];
            char prev = (c > 0) ? scan_line[c-1] : '\0';
            char next = (c + 1 < line_len) ? scan_line[c+1] : '\0';
            if (prev == '\\') { c += direction; continue; }
            if (in_multiline_comment) { if (current == '/' && prev == '*') in_multiline_comment = false; }
            else if (in_string) { if (current == string_delimiter) in_string = false; }
            else {
                if (current == '"' || current == '\'') { in_string = true; string_delimiter = current; }
                else if (current == '/' && next == '*') { in_multiline_comment = true; c += direction; }
                else if (current == '/' && next == '/') { if (direction > 0) goto next_line_label; else goto prev_line_label; }
                else {
                    if (current == open_char) nest_level += direction;
                    else if (current == close_char) nest_level -= direction;
                }
            }
            if (nest_level == 0) { state->cursor.line = l; state->cursor.col = c; state->cursor.ideal_col = c; return; }
            c += direction;
        }
    next_line_label:
        l += direction;
        if (l >= 0 && l < state->buffer.num_lines) c = (direction == 1) ? 0 : strlen(state->buffer.lines[l]) - 1;
    prev_line_label:;
    }
}

static bool flag_existe(const char* flags_str, const char* flag) {
    char temp_flags[1024]; snprintf(temp_flags, sizeof(temp_flags), " %s ", flags_str);
    char temp_flag[100]; snprintf(temp_flag, sizeof(temp_flag), " %s ", flag);
    return strstr(temp_flags, temp_flag) != NULL;
}

char *analyze_include_and_generate_flags(EditorState *state) {
    size_t buffer_size = 1024; char* flags = malloc(buffer_size); if (!flags) return NULL;
    flags[0] = '\0'; size_t offset = 0;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        char* line = state->buffer.lines[i]; if (!line) continue;
        char* trimmed = line; while(*trimmed && isspace(*trimmed)) trimmed++;
        if (strncmp(trimmed, "#include", 8) == 0) {
            char *inicio = NULL, *fim = NULL;
            char *ob = strchr(trimmed, '<'); if (ob) { char *cb = strchr(ob, '>'); if (cb) { inicio = ob; fim = cb; } }
            if (!inicio) { ob = strchr(trimmed, '"'); if (ob) { char *cb = strchr(ob + 1, '"'); if (cb) { inicio = ob; fim = cb; } } }
            if (inicio && fim && fim > inicio) {
                int tamanho = fim - inicio - 1; char nome_lib[256];
                if (tamanho > 0 && tamanho < 255) {
                    strncpy(nome_lib, inicio + 1, tamanho); nome_lib[tamanho] = '\0';
                    for (int j = 0; j < num_header_flags; j++) {
                        if (strcmp(nome_lib, header_flags[j].header) == 0) {
                            if (!flag_existe(flags, header_flags[j].flag)) {
                                int written = snprintf(flags + offset, buffer_size - offset, "%s ", header_flags[j].flag);
                                if (written > 0 && (size_t)written < buffer_size - offset) offset += written;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    return flags;
}

void make_make_file(EditorState *state, const char *args) {
    save_file(state);
    char *ldflags = analyze_include_and_generate_flags(state);
    FILE *f = fopen("Makefile", "w");
    if (!f) { editor_set_status_msg(state, "Erro ao criar Makefile: %s", strerror(errno)); if (ldflags) free(ldflags); return; }
    char executable_name[256]; strncpy(executable_name, state->buffer.filename, sizeof(executable_name) - 1);
    executable_name[sizeof(executable_name) - 1] = '\0'; char *ponto = strrchr(executable_name, '.'); if (ponto) *ponto = '\0';
    fprintf(f, "CC=gcc\nTARGET=%s\nSOURCES=$(wildcard *.c)\nOBJECTS=$(SOURCES:.c=.o)\nCFLAGS+=-g -Wall %s\nLDFLAGS+=%s\n\n.PHONY: all clean\n\nall: $(TARGET)\n\n$(TARGET): $(OBJECTS)\n\t$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)\n\n%%.o: %%.c\n\t$(CC) $(CFLAGS) -c $< -o $@ -MMD\n\nclean:\n\trm -f $(TARGET) $(OBJECTS) $(OBJECTS:.o=.d)\n\n-include $(OBJECTS:.o=.d)\n", executable_name, args ? args : "", ldflags ? ldflags : "");
    fclose(f); if (ldflags) free(ldflags);
    create_generic_terminal_window((char*[]){"make", NULL});
    editor_set_status_msg(state, "Makefile robusto gerado. Compilando com 'make'...");
}

void build_assembly_mappings(EditorState *asm_state, int num_source_lines) {
    if (!asm_state) return;
    if (asm_state->buffer.mapping) { free(asm_state->buffer.mapping->asm_to_source); free(asm_state->buffer.mapping->source_to_asm); free(asm_state->buffer.mapping); }
    asm_state->buffer.mapping = calloc(1, sizeof(AssemblyMapping));
    asm_state->buffer.mapping->asm_line_count = asm_state->buffer.num_lines;
    asm_state->buffer.mapping->source_line_count = num_source_lines;
    asm_state->buffer.mapping->asm_to_source = malloc(sizeof(int) * asm_state->buffer.num_lines);
    asm_state->buffer.mapping->source_to_asm = malloc(sizeof(AsmRange) * num_source_lines);
    for (int i = 0; i < num_source_lines; i++) { asm_state->buffer.mapping->source_to_asm[i].start_line = -1; asm_state->buffer.mapping->source_to_asm[i].end_line = -1; asm_state->buffer.mapping->source_to_asm[i].active = false; }
    int current_c_line = -1;
    for (int asm_idx = 0; asm_idx < asm_state->buffer.num_lines; asm_idx++) {
        char *line = asm_state->buffer.lines[asm_idx];
        char *loc_ptr = strstr(line, ".loc");
        if (loc_ptr) { int file_id, line_num; if (sscanf(loc_ptr, ".loc %d %d", &file_id, &line_num) == 2) current_c_line = line_num - 1; }
        asm_state->buffer.mapping->asm_to_source[asm_idx] = current_c_line;
        if (current_c_line >= 0 && current_c_line < num_source_lines) {
            AsmRange * range = &asm_state->buffer.mapping->source_to_asm[current_c_line];
            if (range->start_line == -1) { range->start_line = asm_idx; range->active = true; }
            range->end_line = asm_idx;
        }        
    }
}

void build_llvm_mappings(EditorState *llvm_state, int num_source_lines) {
    if (!llvm_state) return;
    if (llvm_state->buffer.mapping) { free(llvm_state->buffer.mapping->asm_to_source); free(llvm_state->buffer.mapping->source_to_asm); free(llvm_state->buffer.mapping); }
    llvm_state->buffer.mapping = calloc(1, sizeof(AssemblyMapping));
    llvm_state->buffer.mapping->asm_line_count = llvm_state->buffer.num_lines;
    llvm_state->buffer.mapping->source_line_count = num_source_lines;
    llvm_state->buffer.mapping->asm_to_source = malloc(sizeof(int) * llvm_state->buffer.num_lines);
    llvm_state->buffer.mapping->source_to_asm = malloc(sizeof(AsmRange) * num_source_lines);
    for (int i = 0; i < num_source_lines; i++) { llvm_state->buffer.mapping->source_to_asm[i].start_line = -1; llvm_state->buffer.mapping->source_to_asm[i].end_line = -1; llvm_state->buffer.mapping->source_to_asm[i].active = false; }
    int max_metadata_id = 5000; int *meta_to_line = malloc(sizeof(int) * max_metadata_id);
    for(int i=0; i<max_metadata_id; i++) meta_to_line[i] = -1;
    for (int i = 0; i < llvm_state->buffer.num_lines; i++) {
        char *line = llvm_state->buffer.lines[i];
        if (line[0] == '!') { int meta_id, line_num; if (sscanf(line, "!%d = !DILocation(line: %d", &meta_id, &line_num) == 2) if (meta_id < max_metadata_id) meta_to_line[meta_id] = line_num - 1; }
    }
    int last_source_line = -1;
    for (int i = 0; i < llvm_state->buffer.num_lines; i++) {
        char *line = llvm_state->buffer.lines[i]; char *dbg_ptr = strstr(line, "!dbg !");
        if (dbg_ptr) { int meta_id; if (sscanf(dbg_ptr, "!dbg !%d", &meta_id) == 1) if (meta_id < max_metadata_id && meta_to_line[meta_id] != -1) last_source_line = meta_to_line[meta_id]; }
        llvm_state->buffer.mapping->asm_to_source[i] = last_source_line;
        if (last_source_line >= 0 && last_source_line < num_source_lines) {
            AsmRange *range = &llvm_state->buffer.mapping->source_to_asm[last_source_line];
            if (range->start_line == -1) { range->start_line = i; range->active = true; }
            range->end_line = i;
        }
    }
    free(meta_to_line);
}

void* background_grep_worker(void* arg) {
    (void)arg;
    ContentSearchResult *local_results = NULL; int count = 0, capacity = 0;
    char cwd[PATH_MAX]; if (getcwd(cwd, sizeof(cwd)) == NULL) { pthread_mutex_lock(&global_grep_state.mutex); global_grep_state.is_running = false; pthread_mutex_unlock(&global_grep_state.mutex); return NULL; }
    recursive_content_search(cwd, global_grep_state.search_term, &local_results, &count, &capacity);
    pthread_mutex_lock(&global_grep_state.mutex);
    if (global_grep_state.results) { for (int i = 0; i < global_grep_state.num_results; i++) { free(global_grep_state.results[i].file_path); free(global_grep_state.results[i].line_content); } free(global_grep_state.results); }
    global_grep_state.results = local_results; global_grep_state.num_results = count; global_grep_state.results_ready = true; global_grep_state.is_running = false;
    pthread_mutex_unlock(&global_grep_state.mutex);
    return NULL;
}

void display_grep_results() {
    EditorState *state = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->state;
    if (global_grep_state.num_results == 0) { editor_set_status_msg(state, "Nenhum resultado para '%s'", global_grep_state.search_term); return; }
    WINDOW *results_win; int rows, cols; getmaxyx(stdscr, rows, cols);
    int win_h = min(25, rows - 4); int win_w = cols - 10;
    int win_y = (rows - win_h) / 2; int win_x = (cols - win_w) / 2;
    results_win = newwin(win_h, win_w, win_y, win_x); keypad(results_win, TRUE); wbkgd(results_win, COLOR_PAIR(9));
    int current_selection = 0, top_of_list = 0;
    while (1) {
        werase(results_win); box(results_win, 0, 0);
        draw_settings_header(results_win, "GREP RESULTS", cols - 10);
        int y_pos = 1;
        for (int i = 0; y_pos < win_h - 1; i++) {
            int idx = top_of_list + i; if (idx >= global_grep_state.num_results) break;
            if (idx == current_selection) wattron(results_win, A_REVERSE);
            char dp[win_w - 4]; snprintf(dp, sizeof(dp), "%s:%d", global_grep_state.results[idx].file_path, global_grep_state.results[idx].line_number);
            mvwprintw(results_win, y_pos++, 2, "%.*s", win_w - 3, dp);
            if (y_pos < win_h - 1) mvwprintw(results_win, y_pos++, 6, "%.*s", win_w - 7, global_grep_state.results[idx].line_content);
            if (idx == current_selection) wattroff(results_win, A_REVERSE);
        }
        wrefresh(results_win); wint_t ch; wget_wch(results_win, &ch);
        if (ch == 'q' || ch == 27) break;
        else if (ch == 'j' || ch == KEY_DOWN) { if (current_selection < global_grep_state.num_results - 1) current_selection++; }
        else if (ch == 'k' || ch == KEY_UP) { if (current_selection > 0) current_selection--; }
        else if (ch == KEY_ENTER || ch == '\n') {
            ContentSearchResult sel = global_grep_state.results[current_selection]; load_file(state, sel.file_path);
            state->cursor.line = sel.line_number - 1; state->cursor.ideal_col = 0;
            adjust_viewport(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, state);
            break;
        }
        int vi = (win_h - 2) / 2; if (vi <= 0) vi = 1;
        if (current_selection < top_of_list) top_of_list = current_selection;
        else if (current_selection >= top_of_list + vi) top_of_list = current_selection - vi + 1;
    }
    for (int i = 0; i < global_grep_state.num_results; i++) { free(global_grep_state.results[i].file_path); free(global_grep_state.results[i].line_content); }
    free(global_grep_state.results);
    pthread_mutex_lock(&global_grep_state.mutex); global_grep_state.results = NULL; global_grep_state.num_results = 0; pthread_mutex_unlock(&global_grep_state.mutex);
    delwin(results_win); touchwin(stdscr); redraw_all_windows();
}
