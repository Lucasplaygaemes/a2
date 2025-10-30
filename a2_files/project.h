#ifndef PROJECT_H
#define PROJECT_H

#include "defs.h"

// Salva a sessão atual em um arquivo .json nomeado dentro da pasta .a2
void project_save_session(const char *project_name);

// Carrega uma sessão nomeada, substituindo o estado atual do editor
bool project_load_session(const char *project_name);

// Verifica na inicialização se uma sessão padrão existe e pergunta ao usuário se quer carregá-la
void project_startup_check();

// Lists all saved project session files in the .a2 directory
void display_project_list();

char *find_project_root(const char *file_path);

#endif // PROJECT_H
