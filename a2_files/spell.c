#include "spell.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

static void *g_hunspell_lib = NULL;
static void* (*fn_Hunspell_create)(const char*, const char*) = NULL;
static void (*fn_Hunspell_destroy)(void*) = NULL;
static int (*fn_Hunspell_spell)(void*, const char*) = NULL;
static int (*fn_Hunspell_suggest)(void*, char***, const char*) = NULL;
static void (*fn_Hunspell_free_list)(void*, char***, int) = NULL;

static bool load_hunspell_symbols(void) {
    if (g_hunspell_lib) return true;

    g_hunspell_lib = dlopen("libhunspell-1.7.so", RTLD_NOW | RTLD_GLOBAL);
    if (!g_hunspell_lib) g_hunspell_lib = dlopen("libhunspell-1.6.so", RTLD_NOW | RTLD_GLOBAL);
    if (!g_hunspell_lib) g_hunspell_lib = dlopen("libhunspell.so", RTLD_NOW | RTLD_GLOBAL);

    if (!g_hunspell_lib) {
        A2_LOG(LOG_WARN, TAG_SPELL, "Hunspell dynamic library not found");
        return false;
    }

    fn_Hunspell_create = dlsym(g_hunspell_lib, "Hunspell_create");
    fn_Hunspell_destroy = dlsym(g_hunspell_lib, "Hunspell_destroy");
    fn_Hunspell_spell = dlsym(g_hunspell_lib, "Hunspell_spell");
    fn_Hunspell_suggest = dlsym(g_hunspell_lib, "Hunspell_suggest");
    fn_Hunspell_free_list = dlsym(g_hunspell_lib, "Hunspell_free_list");

    return (fn_Hunspell_create && fn_Hunspell_destroy && fn_Hunspell_spell);
}

void spell_log(const char *message) {
    A2_LOG(LOG_DEBUG, TAG_SPELL, "%s", message);
}

const char *dict_paths[] = {
    "/usr/share/hunspell/",
    "/usr/share/myspell/",
    "/usr/local/share/hunspell/",
    "./dictionaries/",
    NULL
};

void spell_checker_init(SpellChecker *sc) {
    if (!sc) return;
    sc->hunspell_handle = NULL;
    sc->current_lang[0] = '\0';
    sc->enabled = false;
}

void spell_checker_destroy(SpellChecker *sc) {
    if (!sc) return;
    if (sc->hunspell_handle && fn_Hunspell_destroy) {
        fn_Hunspell_destroy(sc->hunspell_handle);
        sc->hunspell_handle = NULL;
    }
}

void spell_checker_unload_dict(SpellChecker *sc) {
    if (!sc) return;
    spell_checker_destroy(sc);
    sc->current_lang[0] = '\0';
    sc->enabled = false;
}

bool spell_checker_load_dict(SpellChecker *sc, const char *lang) {
    if (!sc || !lang) return false;
    if (!load_hunspell_symbols()) return false;

    if (sc->hunspell_handle) {
        spell_checker_unload_dict(sc);
    }

    char aff_path[1024];
    char dic_path[1024];
    bool found = false;

    const char *home_dir = getenv("HOME");
    if (home_dir) {
        snprintf(aff_path, sizeof(aff_path), "%s/.config/a2/hunspell/%s.aff", home_dir, lang);
        snprintf(dic_path, sizeof(dic_path), "%s/.config/a2/hunspell/%s.dic", home_dir, lang);
        if (access(aff_path, F_OK) == 0 && access(dic_path, F_OK) == 0) {
            found = true;
        }
    }

    if (!found) {
        for (int i = 0; dict_paths[i] != NULL; i++) {
            snprintf(aff_path, sizeof(aff_path), "%s%s.aff", dict_paths[i], lang);
            snprintf(dic_path, sizeof(dic_path), "%s%s.dic", dict_paths[i], lang);
            if (access(aff_path, F_OK) == 0 && access(dic_path, F_OK) == 0) {
                found = true;
                break;
            }
        }
    }

    if (!found) return false;

    sc->hunspell_handle = fn_Hunspell_create(aff_path, dic_path);
    if (sc->hunspell_handle) {
        strncpy(sc->current_lang, lang, sizeof(sc->current_lang) - 1);
        sc->enabled = true;
        return true;
    }
    return false;
}

bool spell_checker_check_word(SpellChecker *sc, const char *word) {
    if (!sc || !sc->enabled || !sc->hunspell_handle || !word || word[0] == '\0') {
        return true;
    }
    if (!fn_Hunspell_spell) return true;
    return fn_Hunspell_spell(sc->hunspell_handle, word) != 0;
}

char **spell_checker_suggest(SpellChecker *sc, const char *word, int *n_suggestions) {
    if (!sc || !sc->enabled || !sc->hunspell_handle || !fn_Hunspell_suggest) {
        *n_suggestions = 0;
        return NULL;
    }
    char **suggestions = NULL;
    *n_suggestions = fn_Hunspell_suggest(sc->hunspell_handle, &suggestions, word);
    return suggestions;
}

void spell_checker_free_suggestions(SpellChecker *sc, char **suggestions, int n_suggestions) {
    if (!sc || !sc->hunspell_handle || !suggestions || !fn_Hunspell_free_list) {
        return;
    }
    fn_Hunspell_free_list(sc->hunspell_handle, &suggestions, n_suggestions);
}

bool spell_checker_is_downloaded(const char *lang) {
    if (!lang) return false;
    const char *home_dir = getenv("HOME");
    if (!home_dir) return false;

    char aff_path[1024];
    char dic_path[1024];
    snprintf(aff_path, sizeof(aff_path), "%s/.config/a2/hunspell/%s.aff", home_dir, lang);
    snprintf(dic_path, sizeof(dic_path), "%s/.config/a2/hunspell/%s.dic", home_dir, lang);

    return (access(aff_path, F_OK) == 0 && access(dic_path, F_OK) == 0);
}
