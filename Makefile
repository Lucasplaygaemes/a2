# Makefile para o projeto a2

# --- Configuração do Compilador ---
CC = gcc
# CFLAGS simplificado para o projeto a2
CFLAGS = -g -Wall -Wextra -I. -I./a2_files
LDFLAGS = -lncursesw -ljansson -lcurl -lpthread -ldl -lssl -lcrypto -lvterm -lm

# --- Alvo Principal ---
TARGET = a2

# --- Diretórios ---
A2_DIR = a2_files

# O alvo padrão 'all' compila o executável
all: $(TARGET)


# --- Regras de Compilação para o Editor 'a2' ---

# Arquivos fonte para o a2
A2_SOURCES = a2.c command_execution.c defs.c direct_navigation.c fileio.c lsp_client.c others.c screen_ui.c window_managment.c project.c timer.c cache.c explorer.c themes.c
# Adiciona o prefixo do diretório para os fontes e objetos
A2_SRCS = $(addprefix $(A2_DIR)/, $(A2_SOURCES))
A2_OBJS = $(A2_SRCS:.c=.o)

# Regra para linkar o executável a2 (na raiz do projeto)
$(TARGET): $(A2_OBJS)
	$(CC) $(CFLAGS) -o $@ $(A2_OBJS) $(LDFLAGS)

# Regra de padrão para compilar os arquivos .c do a2 em .o
$(A2_DIR)/%.o: $(A2_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@


# --- Alvos de Limpeza e Utilitários ---

# Limpa todos os arquivos gerados por este Makefile
clean:
	rm -f $(TARGET) $(A2_OBJS)

# Gera o compile_commands.json
compile_commands:
	@echo "Para gerar compile_commands.json para o editor a2, execute: bear -- make"

# Alvo para instalar o executável no sistema
install: all
	@echo "Instalando executável a2 em /usr/local/bin..."
	-sudo rm -f /usr/local/bin/$(TARGET)
	sudo cp $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Instalando página de manual em /usr/local/share/man/man1/..."
	sudo mkdir -p /usr/local/share/man/man1
	sudo cp man/a2.1 /usr/local/share/man/man1/a2.1
	sudo cp man/a2-commands.1 /usr/local/share/man/man1/a2-commands.1
	sudo cp man/a2-shortcuts.1 /usr/local/share/man/man1/a2-shortcuts.1
	sudo cp -r syntaxes/* /usr/local/share/a2/syntaxes/
	sudo gzip -f /usr/local/share/man/man1/a2.1
	sudo gzip -f /usr/local/share/man/man1/a2-commands.1
	sudo gzip -f /usr/local/share/man/man1/a2-shortcuts.1
	@echo "Atualizando banco de dados do man (pode ser necessário)..."
	-sudo mandb
	@echo "Instalação concluída."
    
# Alvo para forçar a limpeza, compilação e instalação em um único comando
rebuild: clean install

.PHONY: all clean compile_commands install rebuild
