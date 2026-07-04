/*
 * stylefile.c - flat "key = value" theme config parser for FriendSh3ep.
 *
 * $VER: stylefile.c 1.0 (03.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See stylefile.h for the format and public interface.
 */

#include "stylefile.h"

#include <string.h>
#include <stdlib.h>
#include <proto/dos.h>

static char *stylefile_skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Trim trailing whitespace/newline in place. */
static void stylefile_rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

BOOL StyleFile_Load(StyleFile *sf, const char *path)
{
    BPTR fh;
    char line[256];

    if (!sf) return FALSE;
    memset(sf, 0, sizeof(*sf));

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return FALSE;

    while (sf->count < STYLEFILE_MAX_ENTRIES &&
           FGets(fh, (STRPTR)line, sizeof(line))) {
        char *p = stylefile_skip_ws(line);
        char *eq, *key, *val, *keyEnd;

        stylefile_rtrim(p);
        if (!*p || *p == ';') continue;

        eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        key = p;
        val = stylefile_skip_ws(eq + 1);

        keyEnd = key + strlen(key);
        while (keyEnd > key && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t'))
            keyEnd--;
        *keyEnd = '\0';
        if (!*key) continue;

        {
            StyleFileEntry *e = &sf->entries[sf->count];
            strncpy(e->key,   key, STYLEFILE_MAX_KEY - 1);
            strncpy(e->value, val, STYLEFILE_MAX_VALUE - 1);
            e->key[STYLEFILE_MAX_KEY - 1]     = '\0';
            e->value[STYLEFILE_MAX_VALUE - 1] = '\0';
            sf->count++;
        }
    }

    Close(fh);
    return TRUE;
}

const char *StyleFile_GetString(const StyleFile *sf, const char *key,
                                 const char *defaultValue)
{
    int i;
    if (!sf) return defaultValue;
    for (i = 0; i < sf->count; i++)
        if (strcmp(sf->entries[i].key, key) == 0)
            return sf->entries[i].value;
    return defaultValue;
}

LONG StyleFile_GetInt(const StyleFile *sf, const char *key, LONG defaultValue)
{
    const char *v = StyleFile_GetString(sf, key, NULL);
    if (!v || !v[0]) return defaultValue;
    return atol(v);
}

ULONG StyleFile_GetColor(const StyleFile *sf, const char *key, ULONG defaultValue)
{
    const char *v = StyleFile_GetString(sf, key, NULL);
    if (!v || !v[0]) return defaultValue;
    if (v[0] == '#') v++;
    return (ULONG)strtoul(v, NULL, 16);
}
