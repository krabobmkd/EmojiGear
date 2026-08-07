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
 *   - New toot / emoji box sub-window positions (written to
 *     app->tootView/app->emojiBoxWindow on Load, read back on Save --
 *     see FS3ETootView_GetWindowPos/FS3EEmojiBoxWindow_GetWindowPos).
 *     Only these two sub-windows remember their position; login/theme/
 *     settings sub-windows don't.
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

    /* Maximum on-disk media cache size, in megabytes. Enforced by the
     * network process (see FS3ECache_Init/FS3ECache_Store in
     * network_fs3e/fs3enet_cache.c -- oldest-first eviction, checked once
     * at startup and after every new download); FS3ECache_Flush (the
     * SettingsView "Flush cache" button) is a separate manual full wipe.
     * Takes effect on the next launch, same as cachePath above -- both are
     * only read once at FS3ENet_Start() time. Default 40. */
    int   maxCacheSizeMB;

    /* Seconds between polling the server for new toots; default 60. */
    int   tootCheckIntervalSec;

    /* Seconds to stay on a toot before Autoscroll Play (see
     * Action_TimelineAutoscrollPlay, fs3eaction.c) moves to the next one.
     * Minimum 3, default 10, maximum 60. */
    int   playTootTimeSec;

    /* TRUE (default) = "Next toot" (Space key/menu, and each interval of
     * Autoscroll Play, which just calls the same move -- see
     * FS3ETimelineNextToot_Internal, fs3eaction.c) eases into the next
     * toot over the ~1s, 25Hz animated scroll. FALSE = skip that timer
     * and jump the scroll position straight to the next toot in a
     * single frame. */
    short smoothAutoScroll;

    /* FALSE (default) = discard the full-size original after generating
     * its small thumbnail (downloaded to RAM:T and deleted right after
     * use instead of the persistent cache dir) -- saves a lot of disk
     * space, at the cost of needing a fresh network fetch + RAM round
     * trip if that thumbnail is ever regenerated (e.g. cache flushed).
     * TRUE = keep the original on disk too, like before this option
     * existed. See FS3ENetFetchImageReq.fs3enf_KeepOriginal. */
    short keepBigUserIcons;
    short keepBigThumbnails;

    /* Thumbnails & icons rendering (see fs3esettingsview.c "Thumbnails &
     * icons" group). Not yet consumed by the thumbnail process. */
    short biggerThumbnails;   /* FALSE (default) = current size */

    /* FALSE (default) = skip the fs3ethumb minify-to-disk-cache pass for
     * timeline media thumbnails -- fetch to RAM:T and display directly
     * instead. Minifying makes sense for user icons (small, reused across
     * many toots, worth a persistent disk-cached copy) but is wasted work
     * for thumbnails (already server-sized, rarely redisplayed). TRUE =
     * run thumbnails through the same minify pipeline as icons, like
     * before this option existed. */
    short minifyThumbnails;
    int   scalingQuality;     /* FS3E_SCALEQ_*, default FS3E_SCALEQ_FAST */
    int   rgbDrawFunction;    /* FS3E_RGBDRAW_*, default FS3E_RGBDRAW_SCALEPIXELARRAY */

    /* "URL Link" group (fs3esettingsview.c) -- what happens when a link
     * hotspot in a toot is clicked. Not yet consumed by anything else;
     * these three fields are the whole feature for now. */
    int   urlLinkAction;           /* FS3E_URLLINK_*, default FS3E_URLLINK_ASK */
    short directDownloadArchives;  /* TRUE (default) = .zip/.lha links download
                                     * straight to downloadPath instead of going
                                     * through urlLinkAction's Ask/OpenURL/Clipboard
                                     * choice */
    char *downloadPath;            /* AllocVec'd, always non-NULL; default "ram:" */

    /* FALSE (default) = a single click on a toot action (favorite, boost,
     * reply, ...) fires immediately. TRUE = require a double click, to
     * guard against misclicks. Lives in the "URL Link" group in
     * fs3esettingsview.c alongside the other click-behavior settings. */
    short tootActionsNeedDoubleClick;

    /* FALSE (default) until the user has acknowledged the disk-cache-usage
     * warning EasyRequestArgs shown once, right after the main window
     * first opens (see friendsh3ep.c, main()) -- set TRUE and saved
     * immediately when "Go" is clicked, so the warning never nags again.
     * Deliberately NOT exposed in fs3esettingsview.c -- nothing to toggle
     * back, there's no reason to see this warning a second time. */
    short warningDone;

} FS3ESettings;

/* scalingQuality enum -- index matches the combo's CHOOSER_Active order */
enum {
    FS3E_SCALEQ_FAST = 0,     /* Fast linear (68020) */
    FS3E_SCALEQ_BILINEAR,     /* Quick Bilinear (68030) */
    FS3E_SCALEQ_TRILINEAR,    /* Full Trilinear (>=68060) */
    FS3E_SCALEQ_COUNT
};

/* rgbDrawFunction enum -- index matches the combo's CHOOSER_Active order */
enum {
    FS3E_RGBDRAW_SCALEPIXELARRAY = 0, /* ScalePixelArray() */
    FS3E_RGBDRAW_INTERNAL_BILINEAR,   /* Internal Bilinear (>=68060) */
    FS3E_RGBDRAW_COUNT
};

/* urlLinkAction enum -- index matches the "URL Link Action" combo's
 * CHOOSER_Active order */
enum {
    FS3E_URLLINK_ASK = 0,      /* Ask */
    FS3E_URLLINK_OPENURL,      /* Use OpenURL */
    FS3E_URLLINK_CLIPBOARD,    /* Copy to Clipboard */
    FS3E_URLLINK_COUNT
};

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
