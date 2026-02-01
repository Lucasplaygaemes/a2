#ifndef WINDOW_MANAGMENT_H
#define WINDOW_MANAGMENT_H

#include "defs.h" // For EditorState, etc.

// Struct for file search results
typedef struct {
    char* path;
} FileResult;

// Helper function for fuzzy matching strings
bool fuzzy_match(const char *str, const char *pattern);

// Helper function to recursively find all files in a project
void find_all_project_files_recursive(const char *base_path, FileResult **results, int *count, int *capacity);


// Function prototypes for window_managment.c
void free_editor_state(EditorState* state);
void free_janela_editor(JanelaEditor* jw);
void free_workspace(GerenciadorJanelas *ws);
void inicializar_workspaces();
void recalcular_layout_janelas();
void criar_nova_janela(const char *filename);
void redesenhar_todas_as_janelas();
void posicionar_cursor_ativo();
void fechar_janela_ativa(bool *should_exit);
void proxima_janela();
void janela_anterior();
void ciclar_layout();
void rotacionar_janelas();
void mover_janela_para_posicao(int target_idx);
void display_recent_files();
void *display_fuzzy_finder(EditorState *state);
void criar_novo_workspace();
void criar_novo_workspace_vazio();
void ciclar_workspaces(int direcao);
void mover_janela_para_workspace(int target_idx);
void fechar_workspace_ativo(bool *should_exit);
void prompt_and_create_gdb_workspace();
void executar_comando_em_novo_workspace(const char *comando_str);
void executar_comando_no_terminal(const char *comando_str);
void criar_janela_terminal_generica(char *const argv[]);


void gf2_starter();
void display_command_palette(EditorState *state);
void criar_janela_explorer();

void display_command_palette(EditorState *state);
void display_content_search(EditorState *state, const char *prefilled_term);

void display_help_viewer(const char* filename);

// Assembly

EditorState *find_source_state_for_assembly(const char *asm_filename);

EditorState *find_assembly_state_for_source(const char *source_filename);

#endif // WINDOW_MANAGMENT_H
