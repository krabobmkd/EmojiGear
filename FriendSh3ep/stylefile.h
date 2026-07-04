/*
 * stylefile.h - flat "key = value" theme config parser for FriendSh3ep.
 *
 * $VER: stylefile.h 1.0 (03.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Format (see themes/mouton/style.txt): one "key = value" pair per line,
 * ';' starts a line comment, blank lines are ignored. No [sections] --
 * keys use dot-notation (e.g. "bt1Patch9.cornersize") for grouping.
 * Not a general INI/CSS parser: just what this project's theme files need.
 */

#ifndef STYLEFILE_H
#define STYLEFILE_H

#include <exec/types.h>

#define STYLEFILE_MAX_ENTRIES 64
#define STYLEFILE_MAX_KEY     64
#define STYLEFILE_MAX_VALUE   128

typedef struct StyleFileEntry {
    char key[STYLEFILE_MAX_KEY];
    char value[STYLEFILE_MAX_VALUE];
} StyleFileEntry;

typedef struct StyleFile {
    StyleFileEntry entries[STYLEFILE_MAX_ENTRIES];
    int            count;
} StyleFile;

/*
 * Load a style file. path is a full AmigaDOS path (e.g.
 * "PROGDIR:themes/mouton/style.txt"). *sf is zeroed either way; returns
 * FALSE if the file couldn't be opened (sf->count stays 0, so every
 * StyleFile_Get* call below just returns its caller-supplied default --
 * safe to use unconditionally after a failed load).
 */
BOOL StyleFile_Load(StyleFile *sf, const char *path);

/* Raw string value for key, or defaultValue if not present. The returned
 * pointer is owned by *sf -- valid until the next StyleFile_Load(). */
const char *StyleFile_GetString(const StyleFile *sf, const char *key,
                                 const char *defaultValue);

/* Parses the value as a decimal integer. */
LONG StyleFile_GetInt(const StyleFile *sf, const char *key, LONG defaultValue);

/* Parses a "#RRGGBB" value into a 0x00RRGGBB ULONG (matches
 * FS3EManagedColor.rgbcolor's convention in fs3estyle.h). */
ULONG StyleFile_GetColor(const StyleFile *sf, const char *key, ULONG defaultValue);

#endif /* STYLEFILE_H */
