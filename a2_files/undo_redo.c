#include "undo_redo.h"
#include "editor_utils.h"
#include "lsp_client.h"
#include <stdlib.h>
#include <string.h>

EditorSnapshot* create_snapshot(EditorState *state) {
    EditorSnapshot *snapshot = malloc(sizeof(EditorSnapshot));
    if (!snapshot) return NULL;
    snapshot->lines = malloc(sizeof(char*) * state->buffer.num_lines);
    if (!snapshot->lines) { free(snapshot); return NULL; }
    for (int i = 0; i < state->buffer.num_lines; i++) snapshot->lines[i] = strdup(state->buffer.lines[i]);
    snapshot->num_lines = state->buffer.num_lines;
    snapshot->current_line = state->cursor.line;
    snapshot->current_col = state->cursor.col;
    snapshot->ideal_col = state->cursor.ideal_col;
    snapshot->top_line = state->view.top_line;
    snapshot->left_col = state->view.left_col;
    return snapshot;
}

void free_snapshot(EditorSnapshot *snapshot) {
    if (!snapshot) return;
    for (int i = 0; i < snapshot->num_lines; i++) free(snapshot->lines[i]);
    free(snapshot->lines);
    free(snapshot);
}

void restore_from_snapshot(EditorState *state, EditorSnapshot *snapshot) {
    for (int i = 0; i < state->buffer.num_lines; i++) free(state->buffer.lines[i]);
    state->buffer.num_lines = snapshot->num_lines;
    for (int i = 0; i < state->buffer.num_lines; i++) state->buffer.lines[i] = snapshot->lines[i];
    state->cursor.line = snapshot->current_line;
    state->cursor.col = snapshot->current_col;
    state->cursor.ideal_col = snapshot->ideal_col;
    state->view.top_line = snapshot->top_line;
    state->view.left_col = snapshot->left_col;
    free(snapshot->lines);
    free(snapshot);
}

void push_undo(EditorState *state) {
    if (state->buffer.undo_count >= MAX_UNDO_LEVELS) {
        free_snapshot(state->buffer.undo_stack[0]);
        for (int i = 1; i < MAX_UNDO_LEVELS; i++) state->buffer.undo_stack[i - 1] = state->buffer.undo_stack[i];
        state->buffer.undo_count--;
    }
    state->buffer.undo_stack[state->buffer.undo_count++] = create_snapshot(state);
}

void clear_redo_stack(EditorState *state) {
    for (int i = 0; i < state->buffer.redo_count; i++) free_snapshot(state->buffer.redo_stack[i]);
    state->buffer.redo_count = 0;
}

void do_undo(EditorState *state) {
    if (state->buffer.undo_count <= 1) return;
    if (state->buffer.redo_count < MAX_UNDO_LEVELS) state->buffer.redo_stack[state->buffer.redo_count++] = create_snapshot(state);
    EditorSnapshot *undo_snap = state->buffer.undo_stack[--state->buffer.undo_count];
    restore_from_snapshot(state, undo_snap);
    state->buffer.modified = true;
    state->buffer.is_dirty = true;
    if (state->lsp.enabled) {
        lsp_did_change(state);
    }
}

void do_redo(EditorState *state) {
    if (state->buffer.redo_count == 0) return;
    EditorSnapshot *redo_snap = state->buffer.redo_stack[--state->buffer.redo_count];
    push_undo(state);
    restore_from_snapshot(state, redo_snap);
    state->buffer.modified = true;
    state->buffer.is_dirty = true;
    if (state->lsp.enabled) {
        lsp_did_change(state);
    }
}
