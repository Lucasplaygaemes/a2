#ifndef UNDO_REDO_H
#define UNDO_REDO_H

#include "defs.h"

EditorSnapshot* create_snapshot(EditorState *state);
void free_snapshot(EditorSnapshot *snapshot);
void restore_from_snapshot(EditorState *state, EditorSnapshot *snapshot);
void push_undo(EditorState *state);
void clear_redo_stack(EditorState *state);
void do_undo(EditorState *state);
void do_redo(EditorState *state);

#endif // UNDO_REDO_H
