#include "spell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // for the free function

#define SPELL_LOG_FILE "/tmp/spell_debug.log"

void spell_log(const char *message) {
    FILE *log_file = fopen(SPELL_LOG_FILE, "a");
    if (log_file) {
        fprintf(log_file, "%s\n", message);
        fclose(log_file);
    }
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
    if (sc->hunspell_handle) {
        Hunspell_destroy(sc->hunspell_handle);
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
    
    if (sc->hunspell_handle) {
        spell_checker_unload_dict(sc);
    }
    
    char aff_path[1024];
    char dic_path[1024];
    bool found = false;
    
    // try to find the user paths firts
    const char *home_dir = getenv("HOME");
    if (home_dir) {
        char user_path[1024];
        snprintf(user_path, sizeof(user_path), "%s./config/a2/hunspell/", home_dir);
        snprintf(aff_path, sizeof(aff_path), "%s%s.aff", user_path, lang);
        snprintf(dic_path, sizeof(dic_path), "%s%s.dic", user_path, lang);
        
        FILE *dic_file = fopen(aff_path, "r");
        if (dic_file) {
            fclose(dic_file);
            FILE *aff_file = fopen(aff_path, "r");
            if (aff_file) {
                fclose(aff_file);
                found = true;
            }
        }
    }
    
    // if not found, we try the system paths
    
    if (!found) {
        for (int i = 0; i < dict_paths[i] != NULL; i++) {
            snprintf(aff_path, sizeof(aff_path), "%s%s.aff", dict_paths[i], lang);
            snprintf(dic_path, sizeof(dic_path), "%s%s.dic", dict_paths[i], lang);
            
            FILE *dic_file = fopen(dic_path, "r");
            if (dic_file) {
                fclose(dic_file);
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        return false;
    }
    sc->hunspell_handle = Hunspell_create(aff_path, dic_path);
    if (sc->hunspell_handle) {
        strncpy(sc->current_lang, lang, sizeof(sc->current_lang) - 1);
        sc->enabled = true;
        return true;
    }
    return false;
}

/*
bool spell_checker_load_dict(SpellChecker *sc, const char *lang) {
    if (!sc || !lang) return false;

    if (sc->hunspell_handle) {
        spell_checker_unload_dict(sc);
    }
    char aff_path[1024];
    char dic_path[1024];
    bool found = false;

    for (int i = 0; dict_paths[i] != NULL; i++) {
        snprintf(aff_path, sizeof(aff_path), "%s%s.aff", dict_paths[i], lang);
        snprintf(dic_path, sizeof(dic_path), "%s%s.dic", dict_paths[i], lang);
        
        FILE *aff_file = fopen(aff_path, "r");
        if (aff_file) {
            fclose(aff_file);
            FILE *dic_file = fopen(dic_path, "r");
            if (dic_file) {
                fclose(dic_file);
                found = true;
                break;
            } else {
                break;
                
            }
        }
    }

    if (!found) {
        return false;
    }

    sc->hunspell_handle = Hunspell_create(aff_path, dic_path);
    if (sc->hunspell_handle) {
        strncpy(sc->current_lang, lang, sizeof(sc->current_lang) - 1);
        sc->enabled = true;
        return true;
    }
    return false;
}
*/


bool spell_checker_check_word(SpellChecker *sc, const char *word) {
    if (!sc || !sc->enabled || !sc->hunspell_handle || !word || word[0] == '\0') {
        return true;
    }
    
    int result = Hunspell_spell(sc->hunspell_handle, word);
    
    return result != 0;
}

char **spell_checker_suggest(SpellChecker *sc, const char *word, int *n_suggestions) {
    if (!sc || !sc->enabled || !sc->hunspell_handle) {
        *n_suggestions = 0;
        return NULL;
    }
    
    char **suggestions = NULL;
    *n_suggestions = Hunspell_suggest(sc->hunspell_handle, &suggestions, word);
    
    return suggestions;    
}


void spell_checker_free_suggestions(SpellChecker *sc, char **suggestions, int n_suggestions) {
    if (!sc || !sc->hunspell_handle || !suggestions) {
        return;
    }
    Hunspell_free_list(sc->hunspell_handle, &suggestions, n_suggestions);
}
