/*
 * fs3elocale.h - localization support for FriendSh3ep.
 *
 * Uses locale.library catalog if available, falls back to built-in
 * English strings. LocaleBase must be declared in friendsh3ep.c.
 *
 * Pattern adapted from EmojiGear/eglocale.h.
 */

#ifndef FS3ELOCALE_H
#define FS3ELOCALE_H

#include <exec/types.h>

/* String IDs - must stay in the same order as defaultStrings[] in fs3elocale.c */
enum {
    /* Main window */
    MSG_WINDOW_TITLE = 0,
    MSG_LOGIN_BUTTON,
    MSG_NEWTOOT_BUTTON,

    /* Login window (fs3eloginview.c) */
    MSG_LOGIN_TITLE,    /* window title and group label */
    MSG_LOGIN_SERVER,
    MSG_LOGIN_USER,
    MSG_LOGIN_USERORMAIL,
    MSG_LOGIN_CODE,
    MSG_LOGIN_LOGIN,
    MSG_LOGIN_DELETE_SERVER,   /* "Delete Server" button, below the accounts list --
                                 * see GID_LOGIN_DELETE_SERVER_BUTTON */

    /* New toot window (fs3etootview.c) */
    MSG_TOOT_TITLE,
    MSG_TOOT_CONTEXT_NEW,          /* FS3ETOOT_KIND_NEW contextMessage text */
    MSG_TOOT_CONTEXT_MODIFY,       /* FS3ETOOT_KIND_MODIFY contextMessage text */
    MSG_TOOT_CONTEXT_POLL,         /* FS3ETOOT_KIND_POLL contextMessage text */
    MSG_TOOT_CONTEXT_REPLY_FORMAT, /* FS3ETOOT_KIND_REPLY contextMessage format: "Reply to %s's
                                     * toot" + a second line reminding to stay civil -- UniButton
                                     * renders the embedded \n\n as real line breaks */
    MSG_TOOT_CONTEXT_QUOTE_FORMAT, /* FS3ETOOT_KIND_QUOTE contextMessage format: "Quoting %s's toot" */
    MSG_TOOT_CONTEXT_MESSAGE_FORMAT, /* FS3ETOOT_KIND_MESSAGE contextMessage format: "Messaging %s" */
    MSG_TOOT_ATTACH_MEDIA, /* "Attach Media" -- label above the attachMediaGF/attachMediaClearBtn row */
    MSG_TOOT_VISIBILITY_PUBLIC,
    MSG_TOOT_VISIBILITY_UNLISTED,
    MSG_TOOT_VISIBILITY_PRIVATE,
    MSG_TOOT_VISIBILITY_DIRECT,
    /* tv->visibilityMeaning's text, explaining what the currently selected
     * visibility choice means -- see FS3ETootView_UpdateVisibilityMeaning.
     * Order must match visibilityMsgIds. */
    MSG_TOOT_VISIBILITY_MEANING_PUBLIC,
    MSG_TOOT_VISIBILITY_MEANING_UNLISTED,
    MSG_TOOT_VISIBILITY_MEANING_PRIVATE,
    MSG_TOOT_VISIBILITY_MEANING_DIRECT,
    /* "Who can quote you" chooser -- POST .../statuses' quote_approval_policy
     * ("public"/"followers"/"nobody"), see FS3ETootView_GetQuotePolicy. */
    MSG_TOOT_QUOTEPOLICY_PUBLIC,
    MSG_TOOT_QUOTEPOLICY_FOLLOWERS,
    MSG_TOOT_QUOTEPOLICY_NOBODY,
    /* tv->visibilityMeaning's second line, explaining what the currently
     * selected quote-policy choice means -- see
     * FS3ETootView_UpdateVisibilityMeaning. Order must match
     * quotePolicyMsgIds. */
    MSG_TOOT_QUOTEPOLICY_MEANING_PUBLIC,
    MSG_TOOT_QUOTEPOLICY_MEANING_FOLLOWERS,
    MSG_TOOT_QUOTEPOLICY_MEANING_NOBODY,
    MSG_TOOT_SEND,        /* tootBtn label for FS3ETOOT_KIND_NEW/POLL */
    MSG_TOOT_SEND_MODIFY, /* tootBtn label for FS3ETOOT_KIND_MODIFY */
    MSG_TOOT_SEND_REPLY,  /* tootBtn label for FS3ETOOT_KIND_REPLY */
    MSG_TOOT_SEND_QUOTE,  /* tootBtn label for FS3ETOOT_KIND_QUOTE */
    MSG_TOOT_CHARS_FORMAT, /* format: "%lu / Max: %s" -- typed chars, then
                             * either the active account's server-confirmed
                             * per-toot limit as a decimal string, or "-" if
                             * not confirmed yet (see App.accountMaxChars in
                             * friendsh3ep.h and FS3ETootView_UpdateCharCount) */

    /* Toot window's own menu (fs3etootview.c) */
    MSG_TOOTMENU_TOOT,     /* title */
    MSG_TOOTMENU_CLEAR,
    MSG_TOOTMENU_UNDO,
    MSG_TOOTMENU_REDO,
    MSG_TOOTMENU_CUT,
    MSG_TOOTMENU_COPY,
    MSG_TOOTMENU_PASTE,
    MSG_TOOTMENU_EMOJIBOX,

    /* Menu: FriendSh3ep */
    MSG_MENU_FRIENDSH3EP,
    MSG_MENU_ACCOUNTS,
    MSG_MENU_NEW_TOOT,
    MSG_MENU_ABOUT,
    MSG_MENU_QUIT,

    /* Menu: View */
    MSG_MENU_VIEW,

    MSG_VIEW_USER,
    MSG_VIEW_HOME,
    MSG_VIEW_LOCAL,
    MSG_VIEW_FEDERATED,
    MSG_VIEW_SEARCH,

    MSG_VIEW_NOTIFICATIONS,
    MSG_VIEW_BOOKMARK,
    MSG_VIEW_NEWS,

    MSG_VIEW_REFRESH,

    /* Menu: Timeline -- navigation/action shortcuts for whichever toot
     * list is currently on screen (see FS3EACTION_TIMELINE_* in
     * fs3eaction.h). Space is intentionally reused for two different
     * items: NEXT_TOOT and AUTOSCROLL_STOP are mutually exclusive by
     * runtime state (autoscroll off vs. on), same "one physical key,
     * meaning depends on mode" convention a media player's Space/
     * Play-Pause key uses -- not a real key-equivalent collision, see
     * the WMHI_RAWKEY handling in friendsh3ep.c. */
    MSG_MENU_TIMELINE,
    MSG_TIMELINE_NEXT_TOOT,
    MSG_TIMELINE_TOP,
    MSG_TIMELINE_AUTOSCROLL_PLAY,
    MSG_TIMELINE_AUTOSCROLL_STOP,
    MSG_TIMELINE_COPY_TEXT,

    /* Menu: User -- actions on whichever profile is currently open in the
     * Search view's FS3ESEARCH_USER_PROFILE sub-mode (see
     * FS3EACTION_USER_* in fs3eaction.h). Follow/Unfollow menu entries
     * call into the same toggle Action_ToggleFollow already backs the
     * profile header's own Follow/Unfollow button -- not reimplemented
     * here. */
    MSG_MENU_USER,
    MSG_USER_COPY_URL,
    MSG_USER_FOLLOW,
    MSG_USER_UNFOLLOW,
    MSG_USER_MASK,
    MSG_USER_UNMASK,
    MSG_USER_BLOCK,
    MSG_USER_UNBLOCK,
    MSG_USER_BLOCK_SERVER,
    MSG_USER_UNBLOCK_SERVER,

    /* Menu: Settings */
    MSG_MENU_SETTINGS,
    MSG_SETTINGS_THEME,
    MSG_SETTINGS_GENERAL,
    MSG_SETTINGS_FONTSIZEM,  /* "Font size -" */
    MSG_SETTINGS_FONTSIZEP,  /* "Font size +" */

    /* Font & Theme view (fs3ethemeview.c) */
    MSG_THEMEV_TITLE,
    MSG_THEMEV_OPTIONS_GROUP,
    MSG_THEMEV_ANTIALIAS,
    MSG_THEMEV_EMOJIQUALITY,
    MSG_THEMEV_FONTS_GROUP,
    MSG_THEMEV_PRIMARY,
    MSG_THEMEV_FALLBACK1,
    MSG_THEMEV_FALLBACK2,
    MSG_THEMEV_EMOJIFONT,       /* UI emoji font label (buttons/navbar) */
    MSG_THEMEV_COLOREMOJIFONT,  /* color emoji font label (timeline)    */
    MSG_THEMEV_PRESETS_GROUP,
    MSG_THEMEV_PRESET_LOW,
    MSG_THEMEV_PRESET_HQ,
    MSG_THEMEV_PRESET_MONO,
    MSG_THEMEV_THEME_GROUP,
    MSG_THEMEV_THEME_NAME,
    MSG_THEMEV_THEME_NONE,      /* "-" -- first chooser entry, no theme */
    MSG_THEMEV_SCAN_THEMES,     /* "Scan Themes" button */

    /* General Settings view (fs3esettingsview.c) */
    MSG_SETTINGSV_TITLE,
    MSG_SETTINGSV_PATHS_GROUP,
    MSG_SETTINGSV_CACHE_PATH,
    MSG_SETTINGSV_CACHE_PATH_APPLY,     /* "Apply" button next to the cache path gadget --
                                          * applies a typed-in-place path without opening
                                          * the requester (see GID_SETTINGSV_CACHE_PATH_APPLY) */
    MSG_SETTINGSV_USERDATA_PATH,
    MSG_SETTINGSV_CACHE_GROUP,
    MSG_SETTINGSV_MAX_CACHE_SIZE,
    MSG_SETTINGSV_FLUSH_CACHE,
    MSG_SETTINGSV_SERVERCHECK_GROUP, /* was "Server", now "Server Check" */
    MSG_SETTINGSV_CHECK_INTERVAL,
    MSG_SETTINGSV_KEEP_BIG_USERICONS,
    MSG_SETTINGSV_KEEP_BIG_THUMBNAILS,
    MSG_SETTINGSV_THUMBNAILS_GROUP,
    MSG_SETTINGSV_BIGGER_THUMBNAILS,
    MSG_SETTINGSV_MINIFY_THUMBNAILS,
    MSG_SETTINGSV_SCALING_QUALITY,
    MSG_SETTINGSV_SCALEQ_FAST,
    MSG_SETTINGSV_SCALEQ_BILINEAR,
    MSG_SETTINGSV_SCALEQ_TRILINEAR,
    MSG_SETTINGSV_RGB_DRAW_FUNCTION,
    MSG_SETTINGSV_RGBDRAW_SCALEPIXELARRAY,
    MSG_SETTINGSV_RGBDRAW_INTERNAL_BILINEAR,
    MSG_SETTINGSV_TOOTPLAYBACK_GROUP, /* was "Playback", now "Toot Timeline Playback" */
    MSG_SETTINGSV_PLAY_TOOT_TIME,
    MSG_SETTINGSV_SMOOTH_AUTO_SCROLL,
    MSG_SETTINGSV_URLLINK_GROUP, /* "URL Link" group label */
    MSG_SETTINGSV_URLLINK_ACTION,       /* "URL Link Action" chooser label */
    MSG_SETTINGSV_URLLINK_ASK,          /* chooser entry: "Ask" */
    MSG_SETTINGSV_URLLINK_OPENURL,      /* chooser entry: "Use OpenURL" */
    MSG_SETTINGSV_URLLINK_CLIPBOARD,    /* chooser entry: "Copy to Clipboard" */
    MSG_SETTINGSV_DIRECT_DL_ARCHIVES,   /* "Direct download .zip,.lha" checkbox label */
    MSG_SETTINGSV_DOWNLOAD_PATH,        /* "Download directory" getfile label */
    MSG_SETTINGSV_TOOT_ACTIONS_DBLCLICK, /* "Toot actions need double click" checkbox label */

    /* clicktab labels for the settings window's 3 tabs -- the 3rd tab
     * reuses MSG_SETTINGSV_THUMBNAILS_GROUP's text (that tab's sole page
     * is the "Thumbnails & icons" group itself, so both read the same). */
    MSG_SETTINGSV_TAB_USEREXP,
    MSG_SETTINGSV_TAB_PATHSCACHE,

    /* Search channel (VIEWMODE_Search) TTIMELINE_WaitText, shown while a
     * word/hashtag search request is in flight (see
     * FS3EApp_CheckConnectionState, FS3EApp_SearchWord) instead of the
     * generic "Updating..." every other channel gets. One is picked per
     * search fired, round-robin. Must stay exactly 4 -- see
     * FS3EApp_CheckConnectionState's modulo. */
    MSG_SEARCHV_WAIT1,
    MSG_SEARCHV_WAIT2,
    MSG_SEARCHV_WAIT3,
    MSG_SEARCHV_WAIT4,

    /* searchWordTypeChooser entries (friendsh3ep.c) -- must stay in the
     * same order as FS3ESearchTypeChoice. */
    MSG_SEARCH_TYPE_WORD,
    MSG_SEARCH_TYPE_PEOPLE,

    /* Network downloads window (fs3enetworkview.c) */
    MSG_NETWORKV_TITLE,          /* window title, and Settings menu entry */
    MSG_NETWORKV_COL_NAME,       /* listbrowser column: file name */
    MSG_NETWORKV_COL_PROGRESS,   /* listbrowser column: "NN%" or "NNN KB" */
    MSG_NETWORKV_COL_STATUS,     /* listbrowser column: status word */
    MSG_NETWORKV_STATUS_ON,      /* status word: still downloading */
    MSG_NETWORKV_STATUS_OK,      /* status word: finished */
    MSG_NETWORKV_STATUS_ERROR,   /* status word: failed */
    MSG_NETWORKV_IDLE,           /* default status-bar text, no activity yet */

    /* First-use disk-cache-usage warning (friendsh3ep.c, main()) -- shown
     * once via EasyRequestArgs before the network process starts and before
     * the main window opens (NULL window, screen-wide requester), gated on
     * app->settings.warningDone (fs3esettings.h). The 3 gadgets double as
     * the cache directory picker: the two non-Quit choices are directory
     * paths, written straight into app->settings.cachePath before
     * FS3ENet_Start() ever runs. */
    MSG_FIRSTUSE_TITLE,          /* window title */
    MSG_FIRSTUSE_TEXT,           /* body text, \n-separated lines */
    MSG_FIRSTUSE_GADGETS,        /* es_GadgetFormat: "PROGDIR:.cache|Ram:T/FriendSh3ep|Quit" --
                                   * left-to-right numbering is 1,...,N-1,0: PROGDIR:.cache=1,
                                   * Ram:T/FriendSh3ep=2, Quit(rightmost)=0 */

    /* Must be last */
    MSG_COUNT
};

/* Initialize locale system. catalogName may be NULL (English only). */
BOOL FS3ELocale_Init(const char *catalogName, ULONG version);

/* Close catalog and free resources. */
void FS3ELocale_Close(void);

/* Return localized string by ID. Falls back to built-in English. */
const char *FS3ELocale_GetString(ULONG stringID);

/* Convenience macro */
#define LOC(id) FS3ELocale_GetString(id)

#endif /* FS3ELOCALE_H */
