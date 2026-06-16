#include "lang.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_TRANS 512

typedef struct {
    char key[64];
    char value[256];
} LangEntry;

static LangEntry entries[MAX_TRANS];
static int entry_count = 0;
static bool loaded = false;

void lang_load(const char *filename)
{
    if (!filename) return;
    FILE *f = fopen(filename, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (line[0] == '#' || line[0] == '\0') continue;
        if (entry_count >= MAX_TRANS) break;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        snprintf(entries[entry_count].key, sizeof(entries[entry_count].key), "%s", line);
        snprintf(entries[entry_count].value, sizeof(entries[entry_count].value), "%s", eq + 1);
        entry_count++;
    }
    fclose(f);
    loaded = true;
}

const char *tr(const char *key, const char *fallback)
{
    if (!loaded) return fallback;
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].key, key) == 0)
            return entries[i].value;
    }
    return fallback;
}