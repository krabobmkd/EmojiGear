#ifndef MMGSETTINGS_H
#define MMGSETTINGS_H

/*
 * mmgsettings.h – Application settings for MUImojiGear.
 *
 * Adapted from EmojiGear/emojigearsettings.h:
 *   - Removed EmojiGear-specific window / search / emoji-box position fields.
 *   - MUI handles window position automatically via MUIA_Window_ID.
 *   - Added AppSettings_ApplyToEditor() to push all settings to the editor.
 *
 * Persistence: icon tooltypes via tooltypepref API.
 * Caller must call ToolTypePrefs_Init("MUImojiGear") before AppSettings_Load.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

#define APPSETTINGS_MAX_RECENT 8

/* Font size steps – also used by AppSettings_ApplyToEditor */
extern const int mmgFontSizeTable[];
extern const int mmgFontSizeTableCount;

typedef struct AppSettings {
    /* Recent files (0 = most recent) */
    char *recentFiles[APPSETTINGS_MAX_RECENT];
    int   recentEncodings[APPSETTINGS_MAX_RECENT]; /* 0=UTF-8 1=Latin-1 2=Latin-2 */
    int   recentCount;

    char *lastDir;             /* last directory used in a file requester */

    /* Editor display */
    ULONG editorBgColor;       /* 0x00RRGGBB; default = screen colour 0  */
    ULONG editorPenColor;      /* 0x00RRGGBB; default = black            */
    int     colorsWereLoaded;
    int   tabSpaces;           /* spaces per tab, 2..12; default 4       */
    int   visualizeTabs;       /* draw tab markers; default FALSE        */
    int   tabsAreSpaces;       /* Tab key → spaces; default FALSE        */

    int   currentFontSizeIndex; /* index into mmgFontSizeTable            */

    /* Font rendering */
    short antialias;           /* URP_PREF_ANTIALIAS                     */
    short monospace;           /* URP_PREF_FORCE_MONOSPACE               */
    short wordWrap;
    short applyAnsi;
    int   emojiQuality;        /* URP_PREF_HIGHFILTERING                 */

    char *primaryFontPath;     /* AllocVec'd .ttf/.otf path, or NULL     */
    char *fallback1FontPath;
    char *fallback2FontPath;
    char *emojiFontPath;
} AppSettings;

/* Load all settings from tooltypes (call ToolTypePrefs_Init first). */
void AppSettings_Load(AppSettings *as);

/* Save all settings to tooltypes, then flush to disk via ToolTypePrefs_Save. */
void AppSettings_Save(AppSettings *as);

/* Free all AllocVec'd strings and call ToolTypePrefs_Close. */
void AppSettings_Close(AppSettings *as);

/* Apply all font/rendering settings to the MUI Boopsi editor object.
 * rawG and rawW may be NULL if the window is not yet open; in that case
 * SetAttrs() is used (MUI proxies to the BOOPSI gadget via Boopsi_Smart)
 * and the gadget will pick up the settings at first render. Pass the raw
 * Intuition gadget + window when the window is already open to trigger
 * an immediate redraw via RefreshGList. */
void AppSettings_ApplyToEditor(AppSettings *as, Object *editorMuiObj,
                               struct Gadget *rawG, struct Window *rawW);

/* Recent-file management */
void        AppSettings_AddRecentFile    (AppSettings *as, const char *path, int enc);
int         AppSettings_GetRecentCount   (AppSettings *as);
const char *AppSettings_GetRecentFile    (AppSettings *as, int index);
int         AppSettings_GetRecentEncoding(AppSettings *as, int index);

/* Update last-visited directory (duplicates string; NULL clears). */
void AppSettings_SetLastDir(AppSettings *as, const char *dir);

#endif /* MMGSETTINGS_H */
