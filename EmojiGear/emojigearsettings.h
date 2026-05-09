#ifndef APPSETTINGS_H
#define APPSETTINGS_H

/*
 * AppSettings - Application-level settings persisted via icon tooltypes
 *
 * Manages temp directory path and a stack of recent project files (max 8).
 * Uses AllocVec/FreeVec for all path allocation (no fixed-size buffers).
 * Loaded/saved through tooltypepref API.
 */

#define APPSETTINGS_MAX_RECENT 8

typedef struct AppSettings {
    char *recentFiles[APPSETTINGS_MAX_RECENT];     /* [0] = most recent */
    int   recentEncodings[APPSETTINGS_MAX_RECENT]; /* 0=UTF-8, 1=Latin-1, 2=Latin-2 */
    int   recentCount;

    char *lastDir; /* last directory visited in a file requester */

    /* values that can be changed in SettingsView window: */

    ULONG editorBgColor;   /* editor background as 0x00RRGGBB */
    ULONG editorPenColor;  /* editor text pen as 0x00RRGGBB */
    int   tabSpaces;       /* number of spaces per tab, 2..12 */
    int   visualizeTabs;   /* TRUE = draw tab markers in editor; default FALSE */
    int   tabsAreSpaces;   /* TRUE = Tab key inserts spaces instead of \t; default FALSE */

    int currentFontSizeIndex; /* index into font size table {8,12,16,20,24,28} */

    /* Font rendering settings */
    short   antialias;          /* TRUE = antialias when possible */
    short   monospace;
    short   wordWrap;
    short   applyAnsi;          /* TRUE = interpret ANSI escape sequences */
    short   ansiUnixColors;     /* TRUE = resolve colors via xterm palette + FindColor() */
    int   emojiQuality;       /* TRUE = emoji quality scaling */
    char *primaryFontPath;    /* AllocVec'd path to primary .ttf/.odt, NULL = none */
    char *fallback1FontPath;  /* AllocVec'd path to fallback font 1 */
    char *fallback2FontPath;  /* AllocVec'd path to fallback font 2 */
    char *emojiFontPath;      /* AllocVec'd path to emoji font */
} AppSettings;

/* Load settings from icon tooltypes. Calls ToolTypePrefs_Init internally. */
void AppSettings_Load(AppSettings *as);

/* Save settings to icon tooltypes, then ToolTypePrefs_Save. */
void AppSettings_Save(AppSettings *as);

/* Free all allocated strings and call ToolTypePrefs_Close. */
void AppSettings_Close(AppSettings *as);

/* Set last-used directory for ASL requesters (duplicates string; NULL clears it). */
void AppSettings_SetLastDir(AppSettings *as, const char *dir);

/* Add a file path to top of recent list, with its encoding (0=UTF-8,1=Latin-1,2=Latin-2).
 * Moves to top if already present, updating encoding. */
void AppSettings_AddRecentFile(AppSettings *as, const char *path, int encoding);

/* Get number of recent files currently stored. */
int AppSettings_GetRecentCount(AppSettings *as);

/* Get recent file path by index (0 = most recent). Returns NULL if out of range. */
const char *AppSettings_GetRecentFile(AppSettings *as, int index);

/* Get encoding for recent file at index (0=UTF-8, 1=Latin-1, 2=Latin-2). */
int AppSettings_GetRecentEncoding(AppSettings *as, int index);

#endif /* APPSETTINGS_H */
