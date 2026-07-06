/*
 * fs3esettings.h - Application settings for FriendSh3ep.
 *
 * Persisted via Amiga icon tooltypes (tooltypepref API).
 * Pattern adapted from EmojiGear/emojigearsettings.h.
 *
 * What is stored:
 *   - Font paths (primary, two fallbacks, emoji)
 *   - Font rendering flags (antialias, emoji quality)
 *   - Font point size
 *   - Theme name
 *   - Main window position (written to app->mainwindow on Load,
 *                           read from app->mainwindow on Save)
 *
 * What is NOT stored here: network / account / server credentials.
 */

#ifndef FS3ESETTINGS_H
#define FS3ESETTINGS_H

#include <exec/types.h>

typedef struct FS3ESettings {
    /* Font paths (AllocVec'd, NULL = use default) */
    char *primaryFontPath;       /* primary Latin/Unicode font .ttf/.otf */
    char *fallback1FontPath;     /* fallback font 1 */
    char *fallback2FontPath;     /* fallback font 2 */
    char *emojiFontPath;         /* UI emoji font for buttons/navbar (2-color glyph);
                                  * default: OpenMoji-black-glyf.ttf */
    char *colorEmojiFontPath;    /* color emoji font for TootTimeline post rendering;
                                  * default: NotoColorEmoji32.ttf */

    /* Font rendering */
    int   fontPointSize;  /* point size loaded into URPDrawContext; default 12 */
    short antialias;      /* TRUE = antialias when possible */
    short emojiQuality;   /* TRUE = high-quality emoji scaling */

    /* UI theme */
    char *themeName;      /* AllocVec'd theme name, NULL = built-in default */

    /* Media cache directory passed to the network process at startup.
     * NULL or "" → network process uses FS3ECACHE_DEFAULT_DIR. */
    char *cachePath;

    /* User data directory (accounts, drafts, etc.) -- not consumed by any
     * subsystem yet. AllocVec'd, always non-NULL; default "PROGDIR:.user". */
    char *userDataPath;

    /* Maximum on-disk media cache size, in megabytes. Not yet enforced by
     * the network process (see FS3ECache_Flush for the manual alternative);
     * default 4. */
    int   maxCacheSizeMB;

    /* Seconds between polling the server for new toots; default 60. */
    int   tootCheckIntervalSec;

} FS3ESettings;

/*
 * Load settings from PROGDIR:FriendSh3ep.info tooltypes.
 * Window position is written directly into app->mainwindow.
 * Safe to call even when the .info file does not exist (defaults apply).
 */
void FS3ESettings_Load(FS3ESettings *s);

/*
 * Save all settings to PROGDIR:FriendSh3ep.info.
 * Reads current window position from app->mainwindow / app->window_obj
 * before flushing, so call while the window is still open.
 */
void FS3ESettings_Save(FS3ESettings *s);

/* Free all AllocVec'd strings and close ToolTypePrefs. */
void FS3ESettings_Close(FS3ESettings *s);

#endif /* FS3ESETTINGS_H */
