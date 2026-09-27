#ifndef SPELL_H
#define SPELL_H

#include <stdbool.h>

void spell_log(const char *message);

typedef struct {
    void *hunspell_handle;
    char current_lang[32];
    bool enabled;
} SpellChecker;

void spell_checker_init(SpellChecker *sc);
bool spell_checker_load_dict(SpellChecker *sc, const char *lang);
void spell_checker_unload_dict(SpellChecker *sc);
bool spell_checker_check_word(SpellChecker *sc, const char *word);
char **spell_checker_suggest(SpellChecker *sc, const char *word, int *n_suggestions);
void spell_checker_destroy(SpellChecker *sc);
void spell_checker_free_suggestions(SpellChecker *sc, char **suggestions, int n_suggestions);
bool spell_checker_is_downloaded(const char *lang);

#endif 
