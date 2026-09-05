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
#include "cache.h"
#include "themes.h"
#include "dictionary.h"
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

bool is_global_action(EditorAction action) {
    return (action >= ACT_NEW_WINDOW && action <= ACT_ROTATE_WINDOWS) || 
           (action >= ACT_SWITCH_TO_WS_1 && action <= ACT_MOVE_WIN_TO_POS_9) ||
           (action == ACT_SETTINGS || action == ACT_HELP || action == ACT_KSC || action == ACT_TIMER_REPORT || action == ACT_TOGGLE_FLOATING_TERMINAL || action == ACT_OPEN_TERMSIDE || action == ACT_TOGGLE_POPUP_MOVE);
}

void execute_action(EditorAction action, EditorState *state, bool *should_exit) {
    if (action == ACT_NONE) return;
    
    if (action >= ACT_CUSTOM_TASK_START && action <= ACT_CUSTOM_TASK_END) {
        if (!state) return; // Need state for commands
        int task_idx = action - ACT_CUSTOM_TASK_START;
        if (task_idx < global_task_manager.num_tasks) {
            strncpy(state->input.command_buffer, global_task_manager.tasks[task_idx].command, sizeof(state->input.command_buffer) - 1);
            state->input.command_buffer[sizeof(state->input.command_buffer) - 1] = '\0';
            void process_command(EditorState *state, bool *should_exit);
            process_command(state, should_exit);
        }
        return;
    }

    bool is_global = is_global_action(action);
    if (!state && !is_global) return;
    switch (action) {
        case ACT_TOGGLE_FLOATING_TERMINAL: toggle_floating_terminal(); break;
        case ACT_TOGGLE_POPUP_MOVE:
            if (state->lsp.is_popup_visible) {
                state->lsp.is_popup_movable = !state->lsp.is_popup_movable;
                state->buffer.is_dirty = true;
                if (state->lsp.is_popup_movable) {
                    ui_show_message("Hover Mode", "Use arrows/mouse to move, ENTER to pin, ESC to close.");
                }
            }
            break;
        case ACT_OPEN_TERMSIDE: execute_command_in_split(""); break;
        case ACT_INSERT_MODE: 
            if (!state->buffer.is_image) {
                if (state->input.prefix_count > 1) {
                    state->input.insert_repeat_count = state->input.prefix_count;
                    state->input.prefix_count = 0;
                } else {
                    state->input.insert_repeat_count = 0;
                }
                state->input.insert_repeat_buf[0] = '\0';
                state->input.mode = INSERT; 
                state->buffer.is_dirty = true; 
            }
            break;
        case ACT_MULTI_CURSOR_UP:
            if (state->num_extra_cursors < MAX_EXTRA_CURSORS && state->cursor.line > 0) {
                state->extra_cursors[state->num_extra_cursors].line = state->cursor.line;
                state->extra_cursors[state->num_extra_cursors].col = state->cursor.col;
                state->num_extra_cursors++;
                state->cursor.line--;
                state->buffer.is_dirty = true;
            }
            break;
        case ACT_MULTI_CURSOR_DOWN:
            if (state->num_extra_cursors < MAX_EXTRA_CURSORS && state->cursor.line < state->buffer.num_lines - 1) {
                state->extra_cursors[state->num_extra_cursors].line = state->cursor.line;
                state->extra_cursors[state->num_extra_cursors].col = state->cursor.col;
                state->num_extra_cursors++;
                state->cursor.line++;
                state->buffer.is_dirty = true;
            }
            break;
        case ACT_MULTI_CURSOR_CLEAR:
            state->num_extra_cursors = 0;
            state->buffer.is_dirty = true;
            break;
        case ACT_NORMAL_MODE: 
            if (state->input.mode == NORMAL) {
                state->num_extra_cursors = 0;
            }
            state->input.mode = NORMAL; 
            state->cursor.visual_selection_mode = VISUAL_MODE_NONE; 
            state->buffer.is_dirty = true; 
            break;
        case ACT_VISUAL_MODE: 
            if (!state->buffer.is_image) {
                state->cursor.selection_start_line = state->cursor.line;
                state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_SELECT;
                state->input.mode = VISUAL;
                editor_set_status_msg(state, "-- VISUAL --");
                state->buffer.is_dirty = true; 
            }
            break;
        case ACT_VISUAL_LINE_MODE:
            if (!state->buffer.is_image) {
                state->cursor.selection_start_line = state->cursor.line;
                state->cursor.selection_start_col = 0;
                state->cursor.visual_selection_mode = VISUAL_MODE_LINE;
                state->input.mode = VISUAL;
                editor_set_status_msg(state, "-- VISUAL LINE --");
                state->buffer.is_dirty = true;
            }
            break;
        case ACT_VISUAL_BLOCK_MODE:
            if (!state->buffer.is_image) {
                state->cursor.selection_start_line = state->cursor.line; 
                state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_BLOCK; 
                state->input.mode = VISUAL; 
                editor_set_status_msg(state, "-- VISUAL BLOCK --");
                state->buffer.is_dirty = true; 
            }
            break;
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
        case ACT_HOVER_IMAGE:
            state->image_hover.hover_pending = true;
            state->image_hover.hover_last_move.tv_sec = 0; // Trigger instantly
            break;
        case ACT_OPEN_IMAGE_SPLIT: {
            char *line = state->buffer.lines[state->cursor.line];
            char parsed_path[PATH_MAX] = {0};
            if (line) {
                char *bang = strstr(line, "![");
                if (bang) {
                    char *bracket = strchr(bang, ']');
                    if (bracket && *(bracket+1) == '(') {
                        char *paren = strchr(bracket + 2, ')');
                        if (paren) {
                            int len = paren - (bracket + 2);
                            if (len > 0 && len < PATH_MAX) {
                                strncpy(parsed_path, bracket + 2, len);
                                parsed_path[len] = '\0';
                            }
                        }
                    }
                } else {
                    char *at = strstr(line, "@[");
                    if (at) {
                        char *bracket = strchr(at + 2, ']');
                        if (bracket) {
                            int len = bracket - (at + 2);
                            if (len > 0 && len < PATH_MAX) {
                                strncpy(parsed_path, at + 2, len);
                                parsed_path[len] = '\0';
                            }
                        }
                    }
                }
            }
            if (parsed_path[0] != '\0') {
                char final_path[PATH_MAX];
                if (parsed_path[0] != '/' && parsed_path[0] != '~') {
                    char dir_path[PATH_MAX];
                    strncpy(dir_path, state->buffer.filename, PATH_MAX-1);
                    char *last_slash = strrchr(dir_path, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        snprintf(final_path, PATH_MAX, "%s/%s", dir_path, parsed_path);
                    } else {
                        strncpy(final_path, parsed_path, PATH_MAX-1);
                    }
                } else {
                    strncpy(final_path, parsed_path, PATH_MAX-1);
                }
                
                if (final_path[0] == '~') {
                    char expanded[PATH_MAX];
                    const char *home = getenv("HOME");
                    if (home) {
                        snprintf(expanded, PATH_MAX, "%s%s", home, final_path + 1);
                        strncpy(final_path, expanded, PATH_MAX-1);
                    }
                }
                create_new_window(final_path);
            } else {
                editor_set_status_msg(state, "No image link found on this line.");
            }
            break;
        }
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
            state->input.mode = INSERT;
        } break;
        case ACT_INDENT_LINE: editor_ident_line(state, state->cursor.line); break;
        case ACT_UNINDENT_LINE: editor_unindent_line(state, state->cursor.line); break;
        case ACT_JOIN_LINES: editor_join_line(state); break;
        case ACT_NEXT_WORD: editor_move_to_next_word(state); break;
        case ACT_PREV_WORD: editor_move_to_previous_word(state); break;
        case ACT_FIND_LOCAL: 
            state->input.mode = COMMAND; 
            state->input.command_buffer[0] = '/'; 
            state->input.command_buffer[1] = '\0'; 
            state->input.command_pos = 1; 
            state->search.history_pos = state->search.history_count;
            state->buffer.is_dirty = true; 
            break;
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
        case ACT_GDB_DEBUG: {
            bool exit_flag = false;
            char orig_buf[256];
            strncpy(orig_buf, state->input.command_buffer, sizeof(orig_buf) - 1);
            strncpy(state->input.command_buffer, "gdb", sizeof(state->input.command_buffer) - 1);
            process_command(state, &exit_flag);
            strncpy(state->input.command_buffer, orig_buf, sizeof(state->input.command_buffer) - 1);
            break;
        }
        case ACT_GDB_BREAK: {
            if (state->buffer.filename[0] != '\0') {
                char bk_cmd[512];
                // Extract filename basename for gdb breakpoint
                const char *fn = strrchr(state->buffer.filename, '/');
                fn = fn ? fn + 1 : state->buffer.filename;
                snprintf(bk_cmd, sizeof(bk_cmd), "break %s:%d", fn, state->cursor.line + 1);
                if (send_cmd_to_terminal_window(bk_cmd)) {
                    editor_set_status_msg(state, "GDB: %s", bk_cmd);
                } else {
                    editor_set_status_msg(state, "GDB not active. Run :gdb first.");
                }
            }
            break;
        }
        case ACT_GDB_RUN: {
            if (send_cmd_to_terminal_window("run")) {
                editor_set_status_msg(state, "GDB: sent 'run'");
            } else {
                editor_set_status_msg(state, "GDB terminal split not found. Run :gdb first.");
            }
            break;
        }
        case ACT_GDB_NEXT: {
            if (send_cmd_to_terminal_window("next")) {
                editor_set_status_msg(state, "GDB: sent 'next'");
            } else {
                editor_set_status_msg(state, "GDB terminal split not found.");
            }
            break;
        }
        case ACT_GDB_STEP: {
            if (send_cmd_to_terminal_window("step")) {
                editor_set_status_msg(state, "GDB: sent 'step'");
            } else {
                editor_set_status_msg(state, "GDB terminal split not found.");
            }
            break;
        }
        case ACT_GDB_CONTINUE: {
            if (send_cmd_to_terminal_window("continue")) {
                editor_set_status_msg(state, "GDB: sent 'continue'");
            } else {
                editor_set_status_msg(state, "GDB terminal split not found.");
            }
            break;
        }
        case ACT_GDB_PRINT: {
            char word[100] = {0};
            get_word_at_cursor(state, word, sizeof(word));
            if (word[0]) {
                char p_cmd[256];
                snprintf(p_cmd, sizeof(p_cmd), "print %s", word);
                if (send_cmd_to_terminal_window(p_cmd)) editor_set_status_msg(state, "GDB: %s", p_cmd);
                else editor_set_status_msg(state, "GDB terminal split not found.");
            } else {
                editor_set_status_msg(state, "No variable under cursor to print.");
            }
            break;
        }
        case ACT_GDB_WATCH: {
            char word[100] = {0};
            get_word_at_cursor(state, word, sizeof(word));
            if (word[0]) {
                char w_cmd[256];
                snprintf(w_cmd, sizeof(w_cmd), "watch %s", word);
                if (send_cmd_to_terminal_window(w_cmd)) editor_set_status_msg(state, "GDB: %s", w_cmd);
                else editor_set_status_msg(state, "GDB terminal split not found.");
            } else {
                editor_set_status_msg(state, "No variable under cursor to watch.");
            }
            break;
        }
        case ACT_GDB_TUI: {
            bool exit_flag = false;
            char orig_buf[256];
            strncpy(orig_buf, state->input.command_buffer, sizeof(orig_buf) - 1);
            strncpy(state->input.command_buffer, "gdb-tui", sizeof(state->input.command_buffer) - 1);
            process_command(state, &exit_flag);
            strncpy(state->input.command_buffer, orig_buf, sizeof(state->input.command_buffer) - 1);
            break;
        }
        case ACT_GDB_QUIT: {
            if (send_cmd_to_terminal_window("quit")) {
                editor_set_status_msg(state, "GDB: sent 'quit'");
            } else {
                editor_set_status_msg(state, "GDB terminal split not found.");
            }
            break;
        }
        case ACT_ASM_CONVERT: asm_convert_file(state, state->buffer.filename); break;
        case ACT_GIT_ADD_U: { char *const cmd[] = {"git", "add", "-u", NULL}; create_generic_terminal_window(cmd); } break;
        case ACT_DIR_NAVIGATOR: display_directory_navigator(state); break;
        case ACT_PASTE_CLIPBOARD: paste_from_clipboard(state); break;
        case ACT_PASTE_ABOVE: { state->cursor.col = 0; state->cursor.ideal_col = 0; editor_handle_enter(state); state->cursor.line--; editor_paste(state); } break;
        case ACT_PASTE_GLOBAL_ABOVE: { state->cursor.col = 0; state->cursor.ideal_col = 0; editor_handle_enter(state); state->cursor.line--; editor_global_paste(state); } break;
        case ACT_PASTE_BELOW: { state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); editor_handle_enter(state); editor_paste(state); } break;
        case ACT_PASTE_GLOBAL_BELOW: { state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); editor_handle_enter(state); editor_global_paste(state); } break;
        case ACT_GENERIC_INPUT: { char mb[256] = ""; ui_ask_input("Generic Input:", mb, 256); } break;
        case ACT_REPEAT_TEXT: {
            char input_buf[256] = "";
            if (ui_ask_input("Repeat (count text, e.g. 10 hello):", input_buf, sizeof(input_buf))) {
                int count = 0;
                char text[256] = {0};
                if (sscanf(input_buf, "%d %[^\n]", &count, text) == 2 && count > 0) {
                    push_undo(state);
                    clear_redo_stack(state);
                    for (int r = 0; r < count; r++) {
                        for (int i = 0; text[i] != '\0'; i++) {
                            if (text[i] == '\\' && text[i+1] == 'n') {
                                editor_handle_enter(state);
                                i++;
                            } else {
                                editor_insert_char(state, (wint_t)(unsigned char)text[i]);
                            }
                        }
                    }
                    editor_set_status_msg(state, "Repeated %d times", count);
                } else {
                    editor_set_status_msg(state, "Invalid input. Format: <count> <text>");
                }
            }
        } break;
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
        case ACT_PASTE_LOCAL:
            editor_paste(state);
            state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
            state->input.mode = NORMAL;
            break;
        case ACT_PASTE_GLOBAL:
            editor_global_paste(state);
            state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
            state->input.mode = NORMAL;
            break;
        case ACT_MOVE_LOCAL:
            if (state->input.mode == VISUAL) {
                /* Visual mode: existing move-register behaviour */
                if (state->cursor.visual_selection_mode != VISUAL_MODE_NONE) { 
                    editor_yank_to_move_register(state); 
                    editor_delete_selection(state); 
                    state->cursor.is_moving = true; 
                    editor_set_status_msg(state, "Text cut. Press 'm' again to paste."); 
                }
            } else if (state->cursor.is_moving) {
                /* Normal mode, mid-move: paste moved text */
                editor_paste_from_move_register(state); 
                state->cursor.is_moving = false; 
                free(state->cursor.move_register); 
                state->cursor.move_register = NULL; 
                editor_set_status_msg(state, "Text moved.");
            } else {
                /* Normal mode, not moving: set a mark (a–z) */
                editor_set_status_msg(state, "m");
                redraw_all_windows();
                wint_t mc;
                wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &mc);
                editor_set_status_msg(state, "");
                if (mc >= 'a' && mc <= 'z') {
                    int idx = mc - 'a';
                    state->buffer.marks[idx].active = true;
                    state->buffer.marks[idx].line   = state->cursor.line;
                    state->buffer.marks[idx].col    = state->cursor.col;
                    editor_set_status_msg(state, "Mark '%c' set at line %d",
                                          (char)mc, state->cursor.line + 1);
                } else {
                    editor_set_status_msg(state, "Invalid mark.");
                }
            }
            state->buffer.is_dirty = true;
            break;

        case ACT_MOVE_GLOBAL:
            if (state->input.mode == VISUAL) {
                if (state->cursor.visual_selection_mode != VISUAL_MODE_NONE) { 
                    editor_global_yank_to_move_register(state); 
                    editor_delete_selection(state); 
                    is_global_moving = true; 
                    editor_set_status_msg(state, "Global text cut. Press 'M' again to paste."); 
                }
            } else {
                if (is_global_moving) { 
                    editor_paste_from_global_move_register(state); 
                    is_global_moving = false; 
                    if (global_move_register) { free(global_move_register); global_move_register = NULL; } 
                    editor_set_status_msg(state, "Global text moved."); 
                }
            }
            state->buffer.is_dirty = true;
            break;
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
        case ACT_LSP_CODE_ACTION: lsp_request_code_actions(state); break;
        case ACT_TIMER_REPORT: display_work_summary(); break;
        case ACT_SETTINGS: create_settings_panel_window(); break;
        case ACT_HELP: display_help_viewer("a2_help.txt"); break;
        case ACT_KSC: display_dynamic_ksc(); break;
        case ACT_DICTIONARY_HOVER: dictionary_trigger_hover(state); break;
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
        case KEY_BTAB: push_undo(state); editor_unindent_line(state, state->cursor.line); break;
        case '>': state->input.mode = OPERATOR_PENDING; state->input.pending_operator = '>'; break;
        case '<': state->input.mode = OPERATOR_PENDING; state->input.pending_operator = '<'; break;
        case 'w': { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; state->input.prefix_count = 0; for (int i = 0; i < r; i++) editor_move_to_next_word(state); state->buffer.is_dirty = true; break; }
        case 'b': { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; state->input.prefix_count = 0; for (int i = 0; i < r; i++) editor_move_to_previous_word(state); state->buffer.is_dirty = true; break; }
        case 'e': { int r = state->input.prefix_count > 0 ? state->input.prefix_count : 1; state->input.prefix_count = 0; for (int i = 0; i < r; i++) editor_move_to_end_of_word(state); state->buffer.is_dirty = true; break; }
        case 'f': {
            editor_set_status_msg(state, "f"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, true, false);
            else { state->input.prefix_count = 0; editor_set_status_msg(state, ""); }
            break; }
        case 'F': {
            editor_set_status_msg(state, "F"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, false, false);
            else { state->input.prefix_count = 0; editor_set_status_msg(state, ""); }
            break; }
        case 't': {
            editor_set_status_msg(state, "t"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, true, true);
            else { state->input.prefix_count = 0; editor_set_status_msg(state, ""); }
            break; }
        case 'T': {
            editor_set_status_msg(state, "T"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, false, true);
            else { state->input.prefix_count = 0; editor_set_status_msg(state, ""); }
            break; }
        case ';': editor_repeat_find_char(state, false); break;
        case ',': editor_repeat_find_char(state, true); break;
        // --- Single-char operators ---
        case 'r': { // Replace char(s) under cursor, stay in Normal
            editor_set_status_msg(state, "r"); redraw_all_windows();
            wint_t rc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &rc);
            editor_set_status_msg(state, "");
            if (rc == 27) { state->input.prefix_count = 0; break; } // ESC cancels
            char *line = state->buffer.lines[state->cursor.line];
            if (line && rc > 0 && rc < 128) {
                int count = state->input.prefix_count > 0 ? state->input.prefix_count : 1;
                state->input.prefix_count = 0;
                int len = strlen(line);
                if (state->cursor.col < len) {
                    push_undo(state); clear_redo_stack(state);
                    for (int i = 0; i < count && state->cursor.col + i < len; i++)
                        line[state->cursor.col + i] = (char)rc;
                    if (count > 1 && state->cursor.col + count - 1 < len)
                        state->cursor.col += count - 1;
                    state->cursor.ideal_col = state->cursor.col;
                    state->buffer.modified = true; state->buffer.is_dirty = true;
                    mark_line_as_dirty(state, state->cursor.line);
                }
            } else { state->input.prefix_count = 0; }
            break; }
        case 'x': { // Delete char(s) under cursor (like Ndl)
            char *line = state->buffer.lines[state->cursor.line];
            if (!line) break;
            int len = strlen(line);
            if (state->cursor.col >= len) break;
            int count = state->input.prefix_count > 0 ? state->input.prefix_count : 1;
            state->input.prefix_count = 0;
            push_undo(state); clear_redo_stack(state);
            int start = utf8_align_col(line, state->cursor.col);
            int end = start;
            for (int i = 0; i < count && end < len; i++) {
                end = utf8_next_char(line, end, len);
            }
            memmove(line + start, line + end, len - end + 1);
            len = strlen(line);
            if (start >= len) start = len > 0 ? utf8_prev_char(line, len) : 0;
            state->cursor.col = start;
            state->cursor.ideal_col = state->cursor.col;
            state->buffer.modified = true; state->buffer.is_dirty = true;
            mark_line_as_dirty(state, state->cursor.line);
            break; }
        case 'X': { // Delete char(s) before cursor (like Ndh)
            char *line = state->buffer.lines[state->cursor.line];
            if (!line || state->cursor.col == 0) break;
            int count = state->input.prefix_count > 0 ? state->input.prefix_count : 1;
            state->input.prefix_count = 0;
            push_undo(state); clear_redo_stack(state);
            int len = strlen(line);
            int end = utf8_align_col(line, state->cursor.col);
            int start = end;
            for (int i = 0; i < count && start > 0; i++) {
                start = utf8_prev_char(line, start);
            }
            memmove(line + start, line + end, len - end + 1);
            state->cursor.col = start;
            state->cursor.ideal_col = state->cursor.col;
            state->buffer.modified = true; state->buffer.is_dirty = true;
            mark_line_as_dirty(state, state->cursor.line);
            break; }
        case 's': { // Substitute char(s): delete N chars, enter Insert
            char *line = state->buffer.lines[state->cursor.line];
            int count = state->input.prefix_count > 0 ? state->input.prefix_count : 1;
            state->input.prefix_count = 0;
            if (line) {
                int len = strlen(line);
                if (state->cursor.col < len) {
                    push_undo(state); clear_redo_stack(state);
                    int end = state->cursor.col + count;
                    if (end > len) end = len;
                    memmove(line + state->cursor.col, line + end, len - end + 1);
                    state->buffer.modified = true; state->buffer.is_dirty = true;
                    mark_line_as_dirty(state, state->cursor.line);
                }
            }
            state->input.mode = INSERT;
            break; }
        case 'S': { // Substitute line: clear content, enter Insert (like cc)
            push_undo(state); clear_redo_stack(state);
            char *l = state->buffer.lines[state->cursor.line];
            if (l) { l[0] = '\0'; }
            state->cursor.col = 0; state->cursor.ideal_col = 0;
            state->input.prefix_count = 0;
            state->buffer.modified = true; state->buffer.is_dirty = true;
            mark_line_as_dirty(state, state->cursor.line);
            state->input.mode = INSERT;
            break; }
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
        case 'G': state->buffer.is_dirty = true; state->cursor.line = state->buffer.num_lines - 1; state->cursor.col = 0; state->cursor.ideal_col = 0; break;
        case 'g': state->buffer.is_dirty = true; state->cursor.line = 0; state->cursor.col = 0; state->cursor.ideal_col = 0; break;
        case 'v': state->input.mode = VISUAL; state->buffer.is_dirty = true; break;
        case 'i': 
            if (state->input.prefix_count > 1) {
                state->input.insert_repeat_count = state->input.prefix_count;
                state->input.prefix_count = 0;
            } else {
                state->input.insert_repeat_count = 0;
            }
            state->input.insert_repeat_buf[0] = '\0';
            state->input.mode = INSERT; 
            state->buffer.is_dirty = true; 
            break;
        case ':': state->input.mode = COMMAND; state->input.history_pos = state->input.history_count; state->input.command_buffer[0] = '\0'; state->input.command_pos = 0; state->buffer.is_dirty = true; break;
        case KEY_CTRL_RIGHT_BRACKET: next_window(); state->buffer.is_dirty = true; break;
        case KEY_CTRL_LEFT_BRACKET: previous_window(); state->buffer.is_dirty = true; break;
        case '/': 
            state->input.mode = COMMAND; 
            state->input.command_buffer[0] = '/'; 
            state->input.command_buffer[1] = '\0'; 
            state->input.command_pos = 1; 
            state->search.history_pos = state->search.history_count;
            state->buffer.is_dirty = true; 
            break;
        case 6: // Ctrl+F
            state->input.mode = COMMAND; 
            state->input.command_buffer[0] = '/'; 
            state->input.command_buffer[1] = '\0'; 
            state->input.command_pos = 1; 
            state->search.history_pos = state->search.history_count;
            state->buffer.is_dirty = true; 
            break;
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
        /* ── MARKS (jump only — set is handled via ACT_MOVE_LOCAL) ─ */
        case '\'':  /* ' -> jump to mark line (first non-blank col) */
        case '`': { /* ` -> jump to exact mark position (line + col) */
            bool exact_col = (ch == '`');
            editor_set_status_msg(state, exact_col ? "`" : "'");
            redraw_all_windows();
            wint_t mc;
            wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &mc);
            editor_set_status_msg(state, "");

            /* '' or `` -> jump back to position before the last mark jump */
            if (mc == '\'' || mc == '`') {
                int prev_l = state->buffer.mark_prev_line;
                int prev_c = state->buffer.mark_prev_col;
                state->buffer.mark_prev_line = state->cursor.line;
                state->buffer.mark_prev_col  = state->cursor.col;
                state->cursor.line      = prev_l;
                state->cursor.col       = exact_col ? prev_c : 0;
                state->cursor.ideal_col = state->cursor.col;
                state->buffer.is_dirty  = true;
                break;
            }

            if (mc >= 'a' && mc <= 'z') {
                int idx = mc - 'a';
                if (state->buffer.marks[idx].active) {
                    /* Save current position as "before jump" */
                    state->buffer.mark_prev_line = state->cursor.line;
                    state->buffer.mark_prev_col  = state->cursor.col;

                    state->cursor.line = state->buffer.marks[idx].line;
                    if (state->cursor.line >= state->buffer.num_lines)
                        state->cursor.line = state->buffer.num_lines - 1;

                    if (exact_col) {
                        state->cursor.col = state->buffer.marks[idx].col;
                    } else {
                        /* Jump to the first non-blank character on the line */
                        state->cursor.col = 0;
                        char *ml = state->buffer.lines[state->cursor.line];
                        if (ml) {
                            while (ml[state->cursor.col] && isspace((unsigned char)ml[state->cursor.col]))
                                state->cursor.col++;
                        }
                    }
                    state->cursor.ideal_col = state->cursor.col;
                    state->buffer.is_dirty  = true;
                } else {
                    editor_set_status_msg(state, "Mark '%c' not set.", (char)mc);
                }
            } else {
                editor_set_status_msg(state, "Invalid mark.");
            }
            break;
        }
        /* ── END MARKS ──────────────────────────────────────────── */
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
            } else { editor_global_yank(state); state->cursor.visual_selection_mode = VISUAL_MODE_NONE; state->input.mode = NORMAL; }
            break;
        case 's':
            if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
                state->cursor.selection_start_line = state->cursor.line; state->cursor.selection_start_col = state->cursor.col;
                state->cursor.visual_selection_mode = VISUAL_MODE_SELECT; editor_set_status_msg(state, "Visual selection started");
            } else state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
            break;
        case 'y':
            state->input.mode = NORMAL; state->buffer.is_dirty = true; break;
        case 'v': 
            state->input.mode = NORMAL; 
            state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
            state->buffer.is_dirty = true; 
            break;
        case 'w': editor_move_to_next_word(state); state->buffer.is_dirty = true; break;
        case 'b': editor_move_to_previous_word(state); state->buffer.is_dirty = true; break;
        case 'e': editor_move_to_end_of_word(state); state->buffer.is_dirty = true; break;
        case 'f': {
            editor_set_status_msg(state, "f"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, true, false);
            else editor_set_status_msg(state, "");
            break; }
        case 'F': {
            editor_set_status_msg(state, "F"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, false, false);
            else editor_set_status_msg(state, "");
            break; }
        case 't': {
            editor_set_status_msg(state, "t"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, true, true);
            else editor_set_status_msg(state, "");
            break; }
        case 'T': {
            editor_set_status_msg(state, "T"); redraw_all_windows();
            wint_t tc; wget_wch(ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win, &tc);
            if (tc > 0 && tc < 128) editor_find_char(state, (char)tc, false, true);
            else editor_set_status_msg(state, "");
            break; }
        case ';': editor_repeat_find_char(state, false); break;
        case ',': editor_repeat_find_char(state, true); break;
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
