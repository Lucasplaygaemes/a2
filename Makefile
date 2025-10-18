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
A2_SOURCES = a2.c command_execution.c defs.c direct_navigation.c fileio.c lsp_client.c others.c screen_ui.c window_managment.c timer.c cache.c
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

.PHONY: all clean compile_commands
