#include "editor_actions.h"
#include "editor_utils.h"
#include "others.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "command_execution.h"
#include "undo_redo.h"
#include "lsp_client.h"
#include "text_editing.h"
#include "search_local.h"
#include "autocomplete_logic.h"
#include "project.h"
#include "timer.h"
#include "fileio.h"
#include "diff.h"
#include "direct_navigation.h"
#include "cache.h"
#include "settings.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

EditorAction get_action_from_key(int ch, bool alt, bool ctrl, int leader) {
    for (int i = 1; i < ACT_COUNT; i++) {
        if (global_bindings[i].key == ch &&
            global_bindings[i].alt == alt &&
            global_bindings[i].ctrl == ctrl &&
            global_bindings[i].leader == leader) {
            return global_bindings[i].action;
        }
    } 
    return ACT_NONE;
}

bool is_leader_key(int ch) {
    if (ch == 0) return false;
    for (int i = 1; i < ACT_COUNT; i++) {
        if (global_bindings[i].leader == ch) return true;
    }
    return false;
}

void execute_action(EditorAction action, EditorState *state, bool *should_exit) {
    if (action == ACT_NONE) return;
    bool is_global = (action >= ACT_NEW_WINDOW && action <= ACT_ROTATE_WINDOWS) || 
                      (action >= ACT_SWITCH_TO_WS_1 && action <= ACT_MOVE_WIN_TO_POS_9) ||
                      (action == ACT_SETTINGS || action == ACT_HELP || action == ACT_KSC || action == ACT_TIMER_REPORT || action == ACT_TOGGLE_FLOATING_TERMINAL);
    if (!state && !is_global) return;
    switch (action) {
        case ACT_TOGGLE_FLOATING_TERMINAL: toggle_floating_terminal(); break;
        case ACT_INSERT_MODE: state->input.mode = INSERT; state->buffer.is_dirty = true; break;
        case ACT_NORMAL_MODE: state->input.mode = NORMAL; state->buffer.is_dirty = true; break;
        case ACT_VISUAL_MODE: state->input.mode = VISUAL; state->buffer.is_dirty = true; break;
        case ACT_COMMAND_MODE: state->input.mode = COMMAND; state->input.history_pos = state->input.history_count; state->input.command_buffer[0] = '\0'; state->input.command_pos = 0; state->buffer.is_dirty = true; break;
        case ACT_MOVE_UP: { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; for(int i=0;i<r;i++) if(state->cursor.line>0) state->cursor.line--; state->input.prefix_count=0; state->cursor.col=state->cursor.ideal_col; state->buffer.is_dirty=true; } break;
        case ACT_MOVE_DOWN: { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; for(int i=0;i<r;i++) if(state->cursor.line<state->buffer.num_lines-1) state->cursor.line++; state->input.prefix_count=0; state->cursor.col=state->cursor.ideal_col; state->buffer.is_dirty=true; } break;
        case ACT_MOVE_LEFT: { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; for(int i=0;i<r;i++) if(state->cursor.col>0){state->cursor.col--;while(state->cursor.col>0&&(state->buffer.lines[state->cursor.line][state->cursor.col]&0xC0)==0x80)state->cursor.col--;} state->input.prefix_count=0; state->cursor.ideal_col=state->cursor.col; state->buffer.is_dirty=true; } break;
        case ACT_MOVE_RIGHT: { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; char* l = state->buffer.lines[state->cursor.line]; for(int i=0;i<r;i++) if(l&&state->cursor.col<(int)strlen(l)){state->cursor.col++;while(l[state->cursor.col]!='\0'&&(l[state->cursor.col]&0xC0)==0x80)state->cursor.col++;} state->input.prefix_count=0; state->cursor.ideal_col=state->cursor.col; state->buffer.is_dirty=true; } break;
        case ACT_MOVE_HOME: state->cursor.col = 0; state->cursor.ideal_col = 0; state->buffer.is_dirty = true; break;
        case ACT_MOVE_END: { char* l = state->buffer.lines[state->cursor.line]; if(l) state->cursor.col = strlen(l); state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; } break;
        case ACT_MOVE_PAGE_UP: for(int i=0;i<PAGE_JUMP;i++) if(state->cursor.line>0) state->cursor.line--; state->cursor.col=state->cursor.ideal_col; state->buffer.is_dirty=true; break;
        case ACT_MOVE_PAGE_DOWN: for(int i=0;i<PAGE_JUMP;i++) if(state->cursor.line<state->buffer.num_lines-1) state->cursor.line++; state->cursor.col=state->cursor.ideal_col; state->buffer.is_dirty=true; break;
        case ACT_MOVE_END_ALT: { char* l = state->buffer.lines[state->cursor.line]; if(l) state->cursor.col = strlen(l); state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; } break;
        case ACT_MOVE_HOME_ALT: state->cursor.col = 0; state->cursor.ideal_col = 0; state->buffer.is_dirty = true; break;
        case ACT_MOVE_TOP: state->cursor.line = 0; state->cursor.col = 0; state->cursor.ideal_col = 0; state->buffer.is_dirty = true; break;
        case ACT_MOVE_BOTTOM: state->cursor.line = state->buffer.num_lines - 1; state->cursor.col = 0; state->cursor.ideal_col = 0; state->buffer.is_dirty = true; break;
        case ACT_SCROLL_UP: for(int i=0;i<10;i++) if(state->cursor.line>0) state->cursor.line--; state->cursor.col=state->cursor.ideal_col; state->buffer.is_dirty=true; break;
        case ACT_SCROLL_DOWN: for(int i=0;i<10;i++) if(state->cursor.line<state->buffer.num_lines-1) state->cursor.line++; state->cursor.col=state->cursor.ideal_col; state->buffer.is_dirty=true; break;
        case ACT_DIGIT_0: if(state->input.prefix_count==0){state->cursor.col=0;state->cursor.ideal_col=0;state->buffer.is_dirty=true;}else{state->input.prefix_count=(state->input.prefix_count*10);editor_set_status_msg(state,"%d",state->input.prefix_count);}return;
        case ACT_DIGIT_1:case ACT_DIGIT_2:case ACT_DIGIT_3:case ACT_DIGIT_4:case ACT_DIGIT_5:case ACT_DIGIT_6:case ACT_DIGIT_7:case ACT_DIGIT_8:case ACT_DIGIT_9:
            state->input.prefix_count=(state->input.prefix_count*10)+(action-ACT_DIGIT_0);editor_set_status_msg(state,"%d",state->input.prefix_count);return;
        case ACT_UNDO: do_undo(state); break;
        case ACT_REDO: do_redo(state); break;
        case ACT_DELETE_LINE: { int r=state->input.prefix_count>0?state->input.prefix_count:1; for(int i=0;i<r;i++) editor_delete_line(state); state->input.prefix_count=0; } break;
        case ACT_JUMP_BRACKET: editor_jump_to_matching_bracket(state); break;
        case ACT_MACRO_RECORD:
            state->buffer.is_dirty = true;
            if (state->input.is_recording_macro) { state->input.is_recording_macro = false; editor_set_status_msg(state, "Recording stopped"); }
            else {
                editor_set_status_msg(state, "Recording @"); redraw_all_windows();
                wint_t rc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &rc);
                if (rc >= 'a' && rc <= 'z') {
                    state->input.is_recording_macro = true; state->input.recording_register_idx = rc - 'a';
                    if (state->input.macro_registers[state->input.recording_register_idx]) { free(state->input.macro_registers[state->input.recording_register_idx]); state->input.macro_registers[state->input.recording_register_idx] = NULL; }
                    editor_set_status_msg(state, "recording @%c", (char)rc);
                } else editor_set_status_msg(state, "Macro recording cancelled.");
            } break;
        case ACT_MACRO_PLAY: {
            state->buffer.is_dirty = true; editor_set_status_msg(state, "@"); redraw_all_windows();
            wint_t rc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &rc);
            if (rc == '@') rc = state->input.last_played_macro_register;
            if (rc >= 'a' && rc <= 'z') {
                char* m = state->input.macro_registers[rc - 'a'];
                if (m) {
                    editor_set_status_msg(state, "playing @%c", (char)rc); state->input.last_played_macro_register = rc;
                    bool wr = state->input.is_recording_macro; state->input.is_recording_macro = false;
                    wchar_t wc; int i = 0, len = strlen(m);
                    while (i < len) {
                        int c = mbtowc(&wc, &m[i], len - i);
                        if (c > 0) { void process_editor_input(EditorState *state, wint_t ch, bool *should_exit); process_editor_input(state, wc, should_exit); i += c; } else i++;
                    }
                    state->input.is_recording_macro = wr; editor_set_status_msg(state, "macro finished");
                } else editor_set_status_msg(state, "register @%c is empty", (char)rc);
            } else editor_set_status_msg(state, "Invalid register.");
            break; }
        case ACT_SAVE_FILE: save_file(state); break;
        case ACT_OPENS_RECENT: display_recent_files(); break;
        case ACT_FUZZY_FINDER: display_fuzzy_finder(state); break;
        case ACT_EXPLORER: create_explorer_window(); break;
        case ACT_CMD_PALLETE: display_command_palette(state); break;
        case ACT_NEW_WINDOW: create_new_window(NULL); break;
        case ACT_NEW_TERMINAL_WINDOW: execute_command_in_terminal(""); break;
        case ACT_CLOSE_WINDOW: close_active_window(should_exit); break;
        case ACT_NEW_WORKSPACE: create_new_workspace(); break;
        case ACT_NEXT_WORKSPACE: cycle_workspaces(1); break;
        case ACT_PREV_WORKSPACE: cycle_workspaces(-1); break;
        case ACT_NEXT_WINDOW: next_window(); break;
        case ACT_PREV_WINDOW: previous_window(); break;
        case ACT_CYCLE_LAYOUT: cycle_layout(); break;
        case ACT_ROTATE_WINDOWS: rotate_windows(); break;
        case ACT_TOGGLE_COMMENT: editor_toggle_comment(state); break;
        case ACT_CHANGE_INSIDE_QUOTE: {
            editor_set_status_msg(state, "Change inside (press \", ', (, [, {, <):"); redraw_all_windows();
            wint_t qc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &qc);
            if (qc > 0 && qc < 128) editor_change_inside_quotes(state, (char)qc, true);
            else editor_set_status_msg(state, "Cancelled.");
            state->buffer.is_dirty = true;
        } break;
        case ACT_DELETE_INSIDE_QUOTE: {
            editor_set_status_msg(state, "Delete inside (press \", ', (, [, {, <):"); redraw_all_windows();
            wint_t qc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &qc);
            if (qc > 0 && qc < 128) editor_change_inside_quotes(state, (char)qc, false);
            else editor_set_status_msg(state, "Cancelled.");
            state->buffer.is_dirty = true;
        } break;
        case ACT_DELETE_WORD_BACK: {
            if (state->cursor.col == 0 && state->cursor.line == 0) break;
            push_undo(state); clear_redo_stack(state);
            int el = state->cursor.line, ec = state->cursor.col;
            editor_move_to_previous_word(state);
            state->cursor.selection_start_line = state->cursor.line; state->cursor.selection_start_col = state->cursor.col;
            state->cursor.line = el; state->cursor.col = ec;
            editor_delete_selection(state);
        } break;
        case ACT_INDENT_LINE: editor_ident_line(state, state->cursor.line); break;
        case ACT_UNINDENT_LINE: editor_unindent_line(state, state->cursor.line); break;
        case ACT_JOIN_LINES: editor_join_line(state); break;
        case ACT_NEXT_WORD: editor_move_to_next_word(state); break;
        case ACT_PREV_WORD: editor_move_to_previous_word(state); break;
        case ACT_FIND_LOCAL: editor_find(state); break;
        case ACT_FIND_NEXT: editor_find_next(state); break;
        case ACT_FIND_PREV: editor_find_previous(state); break;
        case ACT_GREP_PROJECT: display_content_search(state, NULL); break;
        case ACT_VIEW_ASSEMBLY: compile_and_view_assembly(state); break;
        case ACT_VIEW_LLVM: compile_and_view_llvm(state); break;
        case ACT_GOTO_DEFINITION: process_lsp_definition(state); break;
        case ACT_SHOW_SYMBOLS: process_lsp_symbols(state); break;
        case ACT_DIFF_INTERACTIVE: start_interactive_diff(state); break;
        case ACT_GIT_STATUS: { char *const cmd[] = {"git", "status", NULL}; create_generic_terminal_window(cmd); } break;
        case ACT_EXPAND_SNIPPET: editor_expand_snippet(state); break;
        case ACT_GDB_DEBUG: prompt_and_create_gdb_workspace(); break;
        case ACT_ASM_CONVERT: asm_convert_file(state, state->buffer.filename); break;
        case ACT_GIT_ADD_U: { char *const cmd[] = {"git", "add", "-u", NULL}; create_generic_terminal_window(cmd); } break;
        case ACT_DIR_NAVIGATOR: display_directory_navigator(state); break;
        case ACT_PASTE_CLIPBOARD: paste_from_clipboard(state); break;
        case ACT_PASTE_ABOVE: { state->cursor.col = 0; state->cursor.ideal_col = 0; editor_handle_enter(state); state->cursor.line--; editor_paste(state); } break;
        case ACT_PASTE_GLOBAL_ABOVE: { state->cursor.col = 0; state->cursor.ideal_col = 0; editor_handle_enter(state); state->cursor.line--; editor_global_paste(state); } break;
        case ACT_PASTE_BELOW: { state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); editor_handle_enter(state); editor_paste(state); } break;
        case ACT_PASTE_GLOBAL_BELOW: { state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); editor_handle_enter(state); editor_global_paste(state); } break;
        case ACT_GENERIC_INPUT: { char mb[256] = ""; ui_ask_input("Generic Input:", mb, 256); } break;
        case ACT_YANK_LOCAL: {
            if (state->input.mode == VISUAL) {
                editor_yank_selection(state);
            } else {
                state->input.mode = OPERATOR_PENDING;
                state->input.pending_operator = 'y';
            }
        } break;
        case ACT_YANK_GLOBAL: {
            if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
                state->cursor.selection_start_line = state->cursor.line;
                state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_YANK;
                editor_set_status_msg(state, "Global visual selection started");
            } else {
                editor_global_yank(state);
                state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
            }
        } break;
        case ACT_YANK_CLIPBOARD: copy_selection_to_clipboard(state); break;
        case ACT_YANK_PARAGRAPH: editor_yank_paragraph(state); break;
        case ACT_NEXT_PARAGRAPH: {
            state->buffer.is_dirty = true; bool fb = false; int i = state->cursor.line + 1;
            while (i < state->buffer.num_lines) { if (is_line_blank(state->buffer.lines[i])) { fb = true; break; } i++; }
            while (i < state->buffer.num_lines) { if (!is_line_blank(state->buffer.lines[i])) { state->cursor.line = i; break; } i++; }
            if (!fb) state->cursor.line = state->buffer.num_lines - 1;
            state->cursor.col = 0; state->cursor.ideal_col = 0;
        } break;
        case ACT_PREV_PARAGRAPH: {
            state->buffer.is_dirty = true; bool fb = false; int i = state->cursor.line - 1;
            while (i > 0) { if (is_line_blank(state->buffer.lines[i])) { fb = true; break; } i--; }
            while (i > 0) { if (!is_line_blank(state->buffer.lines[i])) { state->cursor.line = i; break; } i--; }
            if (!fb) state->cursor.line = 0;
            state->cursor.col = 0; state->cursor.ideal_col = 0;
        } break;
        case ACT_SWITCH_TO_WS_1: move_window_to_workspace(0); break;
        case ACT_SWITCH_TO_WS_2: move_window_to_workspace(1); break;
        case ACT_SWITCH_TO_WS_3: move_window_to_workspace(2); break;
        case ACT_SWITCH_TO_WS_4: move_window_to_workspace(3); break;
        case ACT_SWITCH_TO_WS_5: move_window_to_workspace(4); break;
        case ACT_SWITCH_TO_WS_6: move_window_to_workspace(5); break;
        case ACT_SWITCH_TO_WS_7: move_window_to_workspace(6); break;
        case ACT_SWITCH_TO_WS_8: move_window_to_workspace(7); break;
        case ACT_SWITCH_TO_WS_9: move_window_to_workspace(8); break;
        case ACT_MOVE_WIN_TO_POS_1: move_window_to_position(0); break;
        case ACT_MOVE_WIN_TO_POS_2: move_window_to_position(1); break;
        case ACT_MOVE_WIN_TO_POS_3: move_window_to_position(2); break;
        case ACT_MOVE_WIN_TO_POS_4: move_window_to_position(3); break;
        case ACT_MOVE_WIN_TO_POS_5: move_window_to_position(4); break;
        case ACT_MOVE_WIN_TO_POS_6: move_window_to_position(5); break;
        case ACT_MOVE_WIN_TO_POS_7: move_window_to_position(6); break;
        case ACT_MOVE_WIN_TO_POS_8: move_window_to_position(7); break;
        case ACT_MOVE_WIN_TO_POS_9: move_window_to_position(8); break;
        case ACT_SAVE_PROJECT: project_save_session(NULL); break;
        case ACT_LOAD_PROJECT: project_load_session(NULL); break;
        case ACT_LSP_RENAME: { char nn[100] = ""; ui_ask_input("New Name:", nn, 100); process_lsp_rename(state, nn); } break;
        case ACT_LSP_RESTART: process_lsp_restart(state); break;
        case ACT_TIMER_REPORT: display_work_summary(); break;
        case ACT_SETTINGS: create_settings_panel_window(); break;
        case ACT_HELP: display_help_viewer("a2_help.txt"); break;
        case ACT_KSC: display_dynamic_ksc(); break;
        default: break;
    }
}

void display_dynamic_ksc() {
    char *temp_file = get_cache_filename("current_ksc.md"); if (!temp_file) return;
    FILE *f = fopen(temp_file, "w"); if (!f) { free(temp_file); return; }
    fprintf(f, "# Current Keyboard Shortcuts\n\nThis list reflects your personal customizations. You can change these keys in *Alt+S > Keybindings*.\n\n");
    const char* cc = NULL;
    for (int i = 1; i < ACT_COUNT; i++) {
        const char* c = "Other";
        if (i < ACT_NEW_WINDOW) c = "File Operations";
        else if (i < ACT_TOGGLE_COMMENT) c = "Windows & Workspaces";
        else if (i < ACT_FIND_LOCAL) c = "Editing";
        else if (i < ACT_SETTINGS) c = "Search & Tools";
        else c = "System";
        if (cc == NULL || strcmp(cc, c) != 0) { cc = c; fprintf(f, "\n## %s\n", c); }
        char kt[32]; key_to_string(&global_bindings[i], kt, sizeof(kt));
        const char* n = global_bindings[i].name[0] != '\0' ? global_bindings[i].name : "Unknown";
        const char* d = global_bindings[i].desc[0] != '\0' ? global_bindings[i].desc : "No description";
        fprintf(f, "- *%-15s* : %s (%s)\n", kt, n, d);
    }
    fprintf(f, "\n\n---\n*Use '/' to search for actions or keys.*");
    fclose(f); display_help_viewer(temp_file); free(temp_file);
}

void handle_normal_mode_key(EditorState *state, wint_t ch) {
    char *line = state->buffer.lines[state->cursor.line];
    bool is_conflict_line = (line && (strncmp(line, "<<<<<<<", 7) == 0 || strncmp(line, "=======", 7) == 0 || strncmp(line, ">>>>>>>", 7) == 0));
    
    if (is_conflict_line) {
        if (tolower(ch) == 'm') { editor_resolve_conflict_interactive(state, 'm'); return; }
        if (tolower(ch) == 't') { editor_resolve_conflict_interactive(state, 't'); return; }
    }
    if (ch == '[') { editor_jump_to_conflict(state, false); return; }
    if (ch == ']') { editor_jump_to_conflict(state, true); return; }

    switch (ch) {
        case 22: editor_paste(state); break;
        case KEY_BTAB: push_undo(state); editor_unindent_line(state, state->cursor.line); break;
        case '>': push_undo(state); editor_ident_line(state, state->cursor.line); break;
        case '<': push_undo(state); editor_unindent_line(state, state->cursor.line); break;
        case 'd': state->input.mode = OPERATOR_PENDING; state->input.pending_operator = 'd'; break;
        case KEY_ENTER: case '\n': case 13:
            if (state->cursor.line < state->buffer.num_lines - 1) {
                state->cursor.line++;
                state->cursor.col = 0;
                // Move to first non-blank character (optional but common)
                while (state->buffer.lines[state->cursor.line][state->cursor.col] && 
                       isspace(state->buffer.lines[state->cursor.line][state->cursor.col])) {
                    state->cursor.col++;
                }
                state->cursor.ideal_col = state->cursor.col;
                state->buffer.is_dirty = true;
            }
            break;
        case 25: 
            if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
                state->cursor.selection_start_line = state->cursor.line; state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_YANK; editor_set_status_msg(state, "Global visual selection started");
            } else { editor_global_yank(state); state->cursor.visual_selection_mode = VISUAL_MODE_NONE; }
            break;
        case 'p': editor_paste(state); break;
        case 'y': state->input.mode = OPERATOR_PENDING; state->input.pending_operator = 'y'; break;
        case 'm':
            if (state->cursor.is_moving) { editor_paste_from_move_register(state); state->cursor.is_moving = false; free(state->cursor.move_register); state->cursor.move_register = NULL; editor_set_status_msg(state, "Text moved."); }
            state->buffer.is_dirty = true; break;
        case 'u': do_undo(state); break;
        case 'U': state->buffer.is_dirty = true; state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); editor_handle_enter(state); state->input.mode = INSERT; break;
        case 'G': state->buffer.is_dirty = true; state->cursor.line = state->buffer.num_lines - 1; state->cursor.col = 0; state->cursor.ideal_col = 0; break;
        case 'g': state->buffer.is_dirty = true; state->cursor.line = 0; state->cursor.col = 0; state->cursor.ideal_col = 0; break;
        case 'v': state->input.mode = VISUAL; state->buffer.is_dirty = true; break;
        case 'i': state->input.mode = INSERT; state->buffer.is_dirty = true; break;
        case ':': state->input.mode = COMMAND; state->input.history_pos = state->input.history_count; state->input.command_buffer[0] = '\0'; state->input.command_pos = 0; state->buffer.is_dirty = true; break;
        case KEY_CTRL_RIGHT_BRACKET: next_window(); state->buffer.is_dirty = true; break;
        case KEY_CTRL_LEFT_BRACKET: previous_window(); state->buffer.is_dirty = true; break;
        case '/': case 6: editor_find(state); break;
        case 520: editor_delete_line(state); break;
        case 11: editor_delete_line(state); state->buffer.is_dirty = true; break;
        case 4: editor_find_next(state); break;
        case 1: editor_find_previous(state); break;
        case 7: display_directory_navigator(state); break;
        case 'o': case KEY_UP: {
            int r = (state->input.prefix_count > 0) ? state->input.prefix_count : 1;
            for (int i = 0; i < r; i++) if (state->cursor.line > 0) state->cursor.line--;
            state->input.prefix_count = 0; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true;
            break; }
        case 'l': case KEY_DOWN: {
            int r = (state->input.prefix_count > 0) ? state->input.prefix_count : 1;
            for (int i = 0; i < r; i++) if (state->cursor.line < state->buffer.num_lines - 1) state->cursor.line++;
            state->input.prefix_count = 0; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true;
            break; }
        case 'k': case KEY_LEFT:
            if (state->cursor.col > 0) { state->cursor.col--; while (state->cursor.col > 0 && (state->buffer.lines[state->cursor.line][state->cursor.col] & 0xC0) == 0x80) state->cursor.col--; }
            state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; break;
        case 231: case KEY_RIGHT: {
            char* l = state->buffer.lines[state->cursor.line];
            if (l && state->cursor.col < (int)strlen(l)) { state->cursor.col++; while (l[state->cursor.col] != '\0' && (l[state->cursor.col] & 0xC0) == 0x80) state->cursor.col++; }
            state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; break; }
        case 'O': case KEY_PPAGE: case KEY_SR: for (int i = 0; i < PAGE_JUMP; i++) if (state->cursor.line > 0) state->cursor.line--; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true; break;
        case 'L': case KEY_NPAGE: case KEY_SF: for (int i = 0; i < PAGE_JUMP; i++) if (state->cursor.line < state->buffer.num_lines - 1) state->cursor.line++; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true; break;
        case 'K': case KEY_HOME: state->cursor.col = 0; state->cursor.ideal_col = 0; state->buffer.is_dirty = true; break;
        case 199: case KEY_END: { char* l = state->buffer.lines[state->cursor.line]; if(l) state->cursor.col = strlen(l); state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; } break;
        case KEY_SDC: editor_delete_line(state); break;
    }
}

void handle_visual_mode_key(EditorState *state, wint_t ch) {
    switch (ch) {
        case 22: editor_delete_selection(state); editor_paste(state); break;
        case KEY_BTAB: {
            push_undo(state); int sl, el;
            if (state->cursor.selection_start_line < state->cursor.line) { sl = state->cursor.selection_start_line; el = state->cursor.line; }
            else { sl = state->cursor.line; el = state->cursor.selection_start_line; }
            for (int i = sl; i <= el; i++) editor_unindent_line(state, i);
            break; }
        case '>': {
            push_undo(state); int sl, el;
            if (state->cursor.selection_start_line < state->cursor.line) { sl = state->cursor.selection_start_line; el = state->cursor.line; }
            else { sl = state->cursor.line; el = state->cursor.selection_start_line; }
            for (int i = sl; i <= el; i++) editor_ident_line(state, i);
            break; }
        case '<': {
            push_undo(state); int sl, el;
            if (state->cursor.selection_start_line < state->cursor.line) { sl = state->cursor.selection_start_line; el = state->cursor.line; }
            else { sl = state->cursor.line; el = state->cursor.selection_start_line; }
            for (int i = sl; i <= el; i++) editor_unindent_line(state, i);
            break; }
        case 'd': editor_delete_selection(state); break;
        case 25: 
            if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
                state->cursor.selection_start_line = state->cursor.line; state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_YANK; editor_set_status_msg(state, "Global visual selection started");
            } else { editor_global_yank(state); state->cursor.visual_selection_mode = VISUAL_MODE_NONE; }
            break;
        case 's':
            if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
                state->cursor.selection_start_line = state->cursor.line; state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_SELECT; editor_set_status_msg(state, "Visual selection started");
            } else state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
            break;
        case 'y':
            if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
                state->cursor.selection_start_line = state->cursor.line; state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_YANK; editor_set_status_msg(state, "Visual selection for yank started");
            } else { editor_yank_selection(state); state->cursor.visual_selection_mode = VISUAL_MODE_NONE; }
            state->buffer.is_dirty = true; break;
        case 'm':
            if (state->cursor.visual_selection_mode != VISUAL_MODE_NONE) { editor_yank_to_move_register(state); editor_delete_selection(state); state->cursor.is_moving = true; editor_set_status_msg(state, "Text cut. Press 'm' again to paste."); }
            state->buffer.is_dirty = true; break;
        case 'v': state->input.mode = NORMAL; state->buffer.is_dirty = true; break;
        case 'o': case KEY_UP: if (state->cursor.line > 0) state->cursor.line--; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true; break;
        case 'l': case KEY_DOWN: if (state->cursor.line < state->buffer.num_lines - 1) state->cursor.line++; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true; break;
        case 'k': case KEY_LEFT:
            if (state->cursor.col > 0) { state->cursor.col--; while (state->cursor.col > 0 && (state->buffer.lines[state->cursor.line][state->cursor.col] & 0xC0) == 0x80) state->cursor.col--; }
            state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; break;
        case 231: case KEY_RIGHT: {
            char* l = state->buffer.lines[state->cursor.line];
            if (l && state->cursor.col < (int)strlen(l)) { state->cursor.col++; while (l[state->cursor.col] != '\0' && (l[state->cursor.col] & 0xC0) == 0x80) state->cursor.col++; }
            state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; break; }
    }
}
