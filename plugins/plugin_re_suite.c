#include "a2_plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static const A2PluginAPI *g_api = NULL;

#define PLUGIN_NAME "Reverse Engineering Suite"

static bool g_use_radare2 = false;
static void run_re_tool_and_display(EditorState *state, const char *title, const char *cmd) {
    static int output_counter = 0;
    output_counter++;
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/a2_re_%d_%d.txt", getpid(), output_counter);
    FILE *f = fopen(tmp_path, "w");
    if (f) {
        fprintf(f, "======================================================================\n");
        fprintf(f, " %s\n", title);
        fprintf(f, " Command: %s\n", cmd);
        fprintf(f, "======================================================================\n\n");
        fclose(f);
    }
    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "%s >> \"%s\" 2>&1", cmd, tmp_path);
    int res = system(full_cmd);
    (void)res;
    g_api->create_new_window(tmp_path);
    g_api->set_status_msg(state, "RE Suite: Result opened in new split (%s)", title);
}

static void cmd_disasm(EditorState *state, const char *args) {
    char target[256] = {0};
    char func[256] = {0};
    if (args && strlen(args) > 0) {
        sscanf(args, "%255s %255s", target, func);
    }
    if (target[0] == '\0' && state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :disasm <file> [func]");
        return;
    }
    char bin_target[512];
    bool is_source = false;
    char *ext = strrchr(target, '.');
    if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0)) {
        is_source = true;
        snprintf(bin_target, sizeof(bin_target), "/tmp/a2_disasm_target.o");
        char gcc_cmd[1024];
        snprintf(gcc_cmd, sizeof(gcc_cmd), "gcc -g -c \"%s\" -o \"%s\"", target, bin_target);
        if (system(gcc_cmd) != 0) {
            g_api->set_status_msg(state, "RE Suite: Compilation failed for %s", target);
            return;
        }
    } else {
        strncpy(bin_target, target, sizeof(bin_target) - 1);
    }
    const char *engine_name = g_use_radare2 ? "Radare2/Cutter" : "Objdump";
    if (g_use_radare2) {
        if (func[0] != '\0') {
            char cmd_str[256];
            snprintf(cmd_str, sizeof(cmd_str), "-AAA; pdf @ %s", func);
            char *const r2_cmd[] = {"r2", "-c", cmd_str, bin_target, NULL};
            if (g_api->create_terminal_window) {
                g_api->create_terminal_window(r2_cmd);
            }
        } else {
            char *const r2_cmd[] = {"r2", bin_target, NULL};
            if (g_api->create_terminal_window) {
                g_api->create_terminal_window(r2_cmd);
            }
        }
        g_api->set_status_msg(state, "Opening Radare2 for %s...", target);
        return;
    } else {
        // Formato objdump
        char disasm_cmd[1024];
        if (func[0] != '\0') {
            snprintf(disasm_cmd, sizeof(disasm_cmd), "objdump -d -M intel --disassemble=\"%s\" \"%s\"", func, bin_target);
        } else if (is_source) {
            snprintf(disasm_cmd, sizeof(disasm_cmd), "objdump -d -S -M intel --no-show-raw-insn \"%s\"", bin_target);
        } else {
            snprintf(disasm_cmd, sizeof(disasm_cmd), "objdump -d -M intel --no-show-raw-insn \"%s\"", bin_target);
        }
        char title[256];
        if (func[0] != '\0') {
            snprintf(title, sizeof(title), "Disassembly [%s] of '%s' in %s", engine_name, func, target);
        } else {
            snprintf(title, sizeof(title), "Disassembly [%s] of %s", engine_name, target);
        }
        g_api->set_status_msg(state, "Disassembling %s (%s)...", target, engine_name);
        run_re_tool_and_display(state, title, disasm_cmd);
    }
}

static void cmd_set_backend(EditorState *state, const char *args) {
    if (!args || strlen(args) == 0) {
        g_api->set_status_msg(state, "RE Engine atual: %s. Use :re-backend [objdump|r2]", 
            g_use_radare2 ? "Radare2" : "Objdump");
        return;
    }
    if (strcasecmp(args, "r2") == 0 || strcasecmp(args, "radare2") == 0 || strcasecmp(args, "cutter") == 0) {
        g_use_radare2 = true;
        if (g_api->save_plugin_settings) {
            g_api->save_plugin_settings();
        }
        g_api->set_status_msg(state, "RE Suite: Backend alterado para Radare2 / Cutter");
    } else if (strcasecmp(args, "objdump") == 0) {
        g_use_radare2 = false;
        if (g_api->save_plugin_settings) {
            g_api->save_plugin_settings();
        }
        g_api->set_status_msg(state, "RE Suite: Backend alterado para Objdump");
    } else {
        g_api->set_status_msg(state, "RE Suite: Opção inválida. Use 'objdump' ou 'r2'");
    }
}
static void cmd_elf(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :elf <file>");
        return;
    }
    char readelf_cmd[1024];
    snprintf(readelf_cmd, sizeof(readelf_cmd), "readelf -h -l \"%s\"", target);
    
    char title[256];
    snprintf(title, sizeof(title), "ELF Header & Program Headers of %s", target);
    run_re_tool_and_display(state, title, readelf_cmd);
}
static void cmd_sections(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :sections <file>");
        return;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "readelf -S \"%s\"", target);
    char title[256];
    snprintf(title, sizeof(title), "ELF Section Headers of %s", target);
    run_re_tool_and_display(state, title, cmd);
}
static void cmd_symbols(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :symbols <file>");
        return;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "nm -C --demangle \"%s\" || readelf -s --demangle \"%s\"", target, target);
    char title[256];
    snprintf(title, sizeof(title), "Symbol Table of %s", target);
    run_re_tool_and_display(state, title, cmd);
}
static void cmd_imports(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :imports <file>");
        return;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ldd \"%s\" 2>/dev/null; echo '\n--- Dynamic Section ---'; readelf -d \"%s\"", target, target);
    char title[256];
    snprintf(title, sizeof(title), "Shared Libraries & Dependencies of %s", target);
    run_re_tool_and_display(state, title, cmd);
}
static void cmd_relocs(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :relocs <file>");
        return;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "readelf -r \"%s\"", target);
    char title[256];
    snprintf(title, sizeof(title), "Relocations (GOT/PLT) of %s", target);
    run_re_tool_and_display(state, title, cmd);
}
static void cmd_checksec(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :checksec <file>");
        return;
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "echo '[+] Analyzing binary security mitigations for %s:' && "
        "echo -n 'RELRO:    ' && (readelf -l \"%s\" | grep -q GNU_RELRO && echo 'Full/Partial RELRO' || echo 'No RELRO') && "
        "echo -n 'Stack:    ' && (readelf -s \"%s\" | grep -q __stack_chk_fail && echo 'Canary found' || echo 'No Canary') && "
        "echo -n 'NX Bit:   ' && (readelf -l \"%s\" | grep -A1 GNU_STACK | grep -q RWE && echo 'NX Disabled (Executable Stack!)' || echo 'NX Enabled') && "
        "echo -n 'PIE:      ' && (readelf -h \"%s\" | grep -q 'DYN (Position-Independent Executable file)' && echo 'PIE Enabled' || echo 'No PIE')",
        target, target, target, target, target);
    char title[256];
    snprintf(title, sizeof(title), "Security Mitigations Check (checksec) of %s", target);
    run_re_tool_and_display(state, title, cmd);
}
static void cmd_strings(EditorState *state, const char *args) {
    char target[256] = {0};
    int min_len = 4;
    if (args && strlen(args) > 0) {
        sscanf(args, "%255s %d", target, &min_len);
    }
    if (target[0] == '\0' && state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :strings <file> [min_len]");
        return;
    }
    if (min_len <= 0) min_len = 4;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "strings -a -n %d \"%s\"", min_len, target);
    char title[256];
    snprintf(title, sizeof(title), "Printable Strings (len >= %d) of %s", min_len, target);
    run_re_tool_and_display(state, title, cmd);
}

static bool g_dec_use_ghidra = true;

static void cmd_decompile(EditorState *state, const char *args) {
    char target[256] = {0};
    char func[256] = {0};
    if (args && strlen(args) > 0) {
        sscanf(args, "%255s %255s", target, func);
    }
    if (target[0] == '\0' && state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :decompile <file> [func]");
        return;
    }
    char bin_target[512];
    strncpy(bin_target, target, sizeof(bin_target) - 1);
    
    char cmd_str[256];
    const char *dec_cmd = g_dec_use_ghidra ? "pdg" : "pdc";
    if (func[0] != '\0') {
        snprintf(cmd_str, sizeof(cmd_str), "-AAA; s %s; %s", func, dec_cmd);
    } else {
        snprintf(cmd_str, sizeof(cmd_str), "-AAA; s main; %s", dec_cmd);
    }
    
    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "\"%s\" \"%s\"", cmd_str, bin_target);
    
    char *const decomp_cmd[] = {"r2", "-c", cmd_str, bin_target, NULL};
    
    char tmp_path[512];
    static int dec_counter = 0;
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/a2_dec_%d_%d.c", getpid(), ++dec_counter);
    
    // char sys_cmd[1024];
    // snprintf(sys_cmd, sizeof(sys_cmd), "%s > \"%s\" 2>/dev/null", full_cmd, tmp_path);
    // int res = system(sys_cmd);
    // (void)res;
    
    if (g_api->create_terminal_window) {
        g_api->create_terminal_window(decomp_cmd);
    }

    g_api->set_status_msg(state, "RE Suite: Decompilation [%s] opened in new split", g_dec_use_ghidra ? "Ghidra" : "Radare2");
    // g_api->set_status_msg(state, decomp_cmd[3]);


}

static void cmd_dec_backend(EditorState *state, const char *args) {
    if (!args || strlen(args) == 0) {
        g_api->set_status_msg(state, "Dec Engine atual: %s. Use :dec-backend [ghidra|r2]", 
            g_dec_use_ghidra ? "Ghidra" : "Radare2");
        return;
    }
    if (strcasecmp(args, "ghidra") == 0) {
        g_dec_use_ghidra = true;
        if (g_api->save_plugin_settings) g_api->save_plugin_settings();
        g_api->set_status_msg(state, "RE Suite: Decompiler alterado para Ghidra (pdg)");
    } else if (strcasecmp(args, "r2") == 0 || strcasecmp(args, "radare2") == 0 || strcasecmp(args, "pdc") == 0) {
        g_dec_use_ghidra = false;
        if (g_api->save_plugin_settings) g_api->save_plugin_settings();
        g_api->set_status_msg(state, "RE Suite: Decompiler alterado para Radare2 (pdc)");
    } else {
        g_api->set_status_msg(state, "RE Suite: Opção inválida. Use 'ghidra' ou 'r2'");
    }
}

static void cmd_repl(EditorState *state, const char *args) {
    (void)args;
    char *const cmd[] = {"python3", NULL};
    if (g_api->create_terminal_window) {
        g_api->create_terminal_window(cmd);
    }
    g_api->set_status_msg(state, "RE Suite: Opened Python REPL");
}
static void cmd_hex(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) strncpy(target, args, sizeof(target) - 1);
    else if (state && state->buffer.filename[0]) strncpy(target, state->buffer.filename, sizeof(target) - 1);
    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :hex <file>");
        return;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "hexdump -C \"%s\"", target);
    char title[256];
    snprintf(title, sizeof(title), "Hexadecimal Dump of %s", target);
    run_re_tool_and_display(state, title, cmd);
}
A2_PLUGIN_DEFINE_INFO(
    PLUGIN_NAME,
    "lucasplaygaemes",
    "1.1.0",
    "Reverse Engineering commands: :disasm, :dec, :repl, :elf, :symbols, :checksec... (run :re-suite for full manual)"
);

static void cmd_re_suite_info(EditorState *state, const char *args) {
    (void)args;
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/a2_re_suite_man_%d.txt", getpid());
    FILE *f = fopen(tmp_path, "w");
    if (f) {
        fprintf(f, "=========================================================\n");
        fprintf(f, "               REVERSE ENGINEERING SUITE                 \n");
        fprintf(f, "=========================================================\n\n");
        fprintf(f, "This plugin provides reverse engineering capabilities directly\n");
        fprintf(f, "within the a2 editor.\n\n");
        fprintf(f, "--- MAIN COMMANDS ---\n");
        fprintf(f, ":disasm <file> [func]  - Disassemble binary/function\n");
        fprintf(f, ":dec <file> [func]     - Decompile binary/function to C\n");
        fprintf(f, ":repl                  - Open an interactive Python3 REPL\n");
        fprintf(f, "\n--- ANALYSIS COMMANDS ---\n");
        fprintf(f, ":elf <file>            - View ELF Header\n");
        fprintf(f, ":sections <file>       - View ELF Sections\n");
        fprintf(f, ":symbols <file>        - View Symbol Table\n");
        fprintf(f, ":imports <file>        - View Dependencies/Imports\n");
        fprintf(f, ":relocs <file>         - View Relocations (GOT/PLT)\n");
        fprintf(f, ":checksec <file>       - View Security Mitigations\n");
        fprintf(f, ":strings <file> [len]  - Extract printable strings\n");
        fprintf(f, ":hex <file>            - Hexadecimal Dump\n");
        fprintf(f, "\n--- SETTINGS ---\n");
        fprintf(f, ":re-backend [r2|objdump]   - Set disassembler engine\n");
        fprintf(f, ":dec-backend [ghidra|r2]   - Set decompiler engine\n");
        fprintf(f, "\n* Settings can also be toggled via the Plugins menu.\n");
        fclose(f);
    }
    if (g_api->display_output_screen) {
        g_api->display_output_screen("RE Suite Manual", tmp_path);
    } else {
        g_api->set_status_msg(state, "RE Suite Active! Commands: :disasm, :dec, :repl, :checksec, :symbols...");
    }
}
bool a2_plugin_init(const A2PluginAPI *api) {
    if (!api || api->api_version != A2_PLUGIN_API_VERSION) {
        return false;
    }
    g_api = api;
    // Registra comandos do editor
    g_api->register_command("disasm", cmd_disasm);
    g_api->register_command("re-backend", cmd_set_backend);
    g_api->register_command("elf", cmd_elf);
    g_api->register_command("sections", cmd_sections);
    g_api->register_command("symbols", cmd_symbols);
    g_api->register_command("nm", cmd_symbols);
    g_api->register_command("imports", cmd_imports);
    g_api->register_command("deps", cmd_imports);
    g_api->register_command("relocs", cmd_relocs);
    g_api->register_command("checksec", cmd_checksec);
    g_api->register_command("strings", cmd_strings);
    g_api->register_command("hex", cmd_hex);
    g_api->register_command("re-suite", cmd_re_suite_info);
    
    g_api->register_command("decompile", cmd_decompile);
    g_api->register_command("dec", cmd_decompile);
    g_api->register_command("dec-backend", cmd_dec_backend);
    g_api->register_command("repl", cmd_repl);

    // Registra no submenu do menu de plugins do a2
    g_api->register_plugin_setting_bool(
        PLUGIN_NAME,
        "Use Radare2 Engine",
        &g_use_radare2,
        "Use Radare2/Cutter instead of Objdump for disassembly"
    );
    g_api->register_plugin_setting_bool(
        PLUGIN_NAME,
        "Use Ghidra Decompiler",
        &g_dec_use_ghidra,
        "Use Ghidra (pdg) instead of Radare2 (pdc) for decompilation"
    );
    return true;
}
void a2_plugin_cleanup(void) {
}

