#ifndef FS3EGADGETID_H
#define FS3EGADGETID_H

/*
 * Gadget ID definitions for FriendSh3ep.
 *
 * These IDs identify specific gadget instances for event routing through
 * the BoopsiDelay message queue (see fs3eboopsimessage.c).
 */

/* Login window (fs3eloginview.c) */
#define GID_LOGIN_SERVER_EDITOR      10
#define GID_LOGIN_USER_EDITOR        11
#define GID_LOGIN_CODE_EDITOR        12
#define GID_LOGIN_LOGIN_BUTTON       13  /* phase 1: connect / register app */
#define GID_LOGIN_URL_EDITOR         14  /* read-only display of authorize URL */
#define GID_LOGIN_SUBMIT_CODE_BUTTON 15  /* phase 2: submit the auth code */
#define GID_LOGIN_ACCOUNTS_LIST      16  /* listbrowser.gadget: known accounts, click to switch */

/* New toot window (fs3etootview.c) */
#define GID_TOOT_BODY_EDITOR     21
#define GID_TOOT_VISIBILITY      22
#define GID_TOOT_EMOJI_BUTTON    23
#define GID_TOOT_SEND_BUTTON     24

/* Title bar (Part A, TitleBarLayout) */
#define GID_TITLEBAR_CLOSE       30
#define GID_TITLEBAR_ICONIFY     31
#define GID_TITLEBAR_ALTPOS      32
#define GID_TITLEBAR_DEPTH       33

/* These ones move to Title Bar layout */
#define GID_TITLEBAR_SETTINGS    34
#define GID_TITLEBAR_ACCOUNTS    35
#define GID_TITLEBAR_NEWTOOT     36

/* Navigation bar (Part B, NavBarLayout) */
#define GID_NAV_USER             40
#define GID_NAV_HOME             41
#define GID_NAV_LOCAL            42
#define GID_NAV_FEDERATED        43

#define GID_NAV_SEARCH           44
#define GID_NAV_NOTIFICATIONS    45
#define GID_NAV_BOOKMARKS        46
#define GID_NAV_NEWS             47

/* Search word editor (Part C container, SearchBarLayout) */
#define GID_SEARCH_WORD_EDITOR       90
#define GID_SEARCH_WORD_TYPE_CHOOSER 91  /* "Word" / "People" mode picker, far right of the search row */
#define GID_SEARCH_BACK_BUTTON       92  /* pops FS3EApp_SearchGoBack's history stack, far right of the row */


/* The whole Toot TimeLine (part C) */
#define GID_TTIMELINE         100

/* Font & Theme settings window (fs3ethemeview.c) */
#define GID_THEMEV_ANTIALIAS        200
#define GID_THEMEV_EMOJIQUALITY     201
#define GID_THEMEV_PRIMARY_FONT     202
#define GID_THEMEV_FALLBACK1_FONT   203
#define GID_THEMEV_FALLBACK2_FONT   204
#define GID_THEMEV_EMOJI_FONT         205
#define GID_THEMEV_COLOR_EMOJI_FONT   206
#define GID_THEMEV_PRIMARY_CLEAR      207
#define GID_THEMEV_FALLBACK1_CLEAR    208
#define GID_THEMEV_FALLBACK2_CLEAR    209
#define GID_THEMEV_EMOJI_CLEAR        210
#define GID_THEMEV_COLOR_EMOJI_CLEAR  211
#define GID_THEMEV_PRESET_LOW         212
#define GID_THEMEV_PRESET_HQ          213
#define GID_THEMEV_PRESET_MONO        214
#define GID_THEMEV_THEME_CHOOSER      215
#define GID_THEMEV_SCAN_THEMES        216

/* General settings window (fs3esettingsview.c) */
#define GID_SETTINGSV_CACHE_PATH      300
#define GID_SETTINGSV_USERDATA_PATH   301
#define GID_SETTINGSV_MAX_CACHE_SIZE  302
#define GID_SETTINGSV_FLUSH_CACHE     303
#define GID_SETTINGSV_CHECK_INTERVAL  304
#define GID_SETTINGSV_KEEP_BIG_USERICONS  305
#define GID_SETTINGSV_KEEP_BIG_THUMBNAILS 306
#define GID_SETTINGSV_BIGGER_THUMBNAILS   307
#define GID_SETTINGSV_SCALING_QUALITY     308
#define GID_SETTINGSV_RGB_DRAW_FUNCTION   309

/* Emoji picker window (fs3eemojibox.c), opened from GID_TOOT_EMOJI_BUTTON */
#define GID_EMOJIBOX_CHOOSER  400  /* emoji-set popup chooser */
#define GID_EMOJIBOX_GRID     401  /* private grid gadget: click a cell to pick */

#endif /* FS3EGADGETID_H */
