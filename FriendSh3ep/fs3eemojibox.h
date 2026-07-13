#ifndef FS3EEMOJIBOX_H
#define FS3EEMOJIBOX_H

/*
 * fs3eemojibox.h - Emoji-table popup window for FriendSh3ep.
 *
 * Forked from EmojiGear/emojibox.c/.h (same private BOOPSI grid-gadget
 * approach, rendered with utf8rastport like every other text surface in
 * this app), NOT shared with it -- adapted to FriendSh3ep's own
 * sub-window conventions (see fs3eloginview.h/fs3etootview.h: BOOL
 * _Create(view, dc) called once at startup, not lazily on first open) and
 * trimmed to what a Mastodon compose box actually needs:
 *   - dropped the ANSI escape-modifier buttons (Bold/Italic/colour...) --
 *     those insert raw terminal escape sequences that EmojiGear's own
 *     utf8rastport-based renderer interprets locally; a Mastodon toot is
 *     posted as plain UTF-8 text, so they'd just show up as literal
 *     garbage characters in a real post.
 *   - dropped the F1-F10 quick-insert shortcuts -- EmojiGear wires those
 *     through its main window's raw-key handling and an app-wide
 *     "activeEditorObj" concept neither of which FriendSh3ep has; the
 *     button-triggered popup (GID_TOOT_EMOJI_BUTTON) is the requested
 *     entry point.
 *   - insertion always targets FS3ETootView's body editor (FriendSh3ep
 *     has exactly one relevant compose field, unlike EmojiGear's
 *     multi-tab editor tracking) -- see FS3EEmojiBoxWindow_GetClickedUTF8,
 *     called from friendsh3ep.c's GID_EMOJIBOX_GRID case.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>

struct URPDrawContext;

/* Number of emoji sets (see the tables in fs3eemojibox.c). */
#define FS3EEMOJIBOX_NUM_SETS 11

typedef struct FS3EEmojiBoxWindow {
    Object         *windowObj;     /* BOOPSI window object (persistent) */
    struct Window  *window;        /* Intuition window, valid while open */

    LONG            left, top, width, height; /* remembered window geometry */

    Object         *chooser;       /* set-selector */
    Object         *gridGadget;    /* private grid gadget (see fs3eemojibox.c) */
    Object         *mainLayout;

    struct List     chooserList;
    struct Node    *chooserNodes[FS3EEMOJIBOX_NUM_SETS];

    int             currentSetIdx; /* 0..FS3EEMOJIBOX_NUM_SETS-1 */
    Class          *gridClass;     /* private gadget class, MakeClass'd */

    /* Borrowed, not owned -- same convention as FS3ELoginView/FS3ETootView's
     * textDC (see FS3EEmojiBoxWindow_Create): the caller (app->style)
     * keeps this alive for the whole app lifetime, so no retain/release
     * here. */
    struct URPDrawContext *dc;
} FS3EEmojiBoxWindow;

/* Build the BOOPSI window+layout. dc is forwarded to the grid gadget for
 * emoji rendering. Returns TRUE on success. */
BOOL FS3EEmojiBoxWindow_Create(FS3EEmojiBoxWindow *ebw, struct URPDrawContext *dc);

/* Dispose the window object and everything below it. */
void FS3EEmojiBoxWindow_Dispose(FS3EEmojiBoxWindow *ebw);

/* Open (or bring to front) the window on CurrentMainScreen. */
void FS3EEmojiBoxWindow_Open(FS3EEmojiBoxWindow *ebw);

/* Close (hide) the window. No-op if already closed. */
void FS3EEmojiBoxWindow_Close(FS3EEmojiBoxWindow *ebw);

/* Handle input messages from this window. Call when its signal fires.
 * Handles the emoji-set chooser locally; a grid click (GID_EMOJIBOX_GRID)
 * is left for the caller to react to via the BoopsiDelay queue, same as
 * every other cross-cutting gadget in this app -- see
 * FS3EEmojiBoxWindow_GetClickedUTF8. */
BOOL FS3EEmojiBoxWindow_HandleInput(FS3EEmojiBoxWindow *ebw);

/* Signal bit mask to OR into Wait(). Returns 0 when the window is closed. */
ULONG FS3EEmojiBoxWindow_GetSignalMask(FS3EEmojiBoxWindow *ebw);

/* Refreshes left/top/width/height from the live window if currently open
 * (no-op if closed -- those fields already hold the last-known position,
 * set by FS3EEmojiBoxWindow_Close). Call before persisting geometry (see
 * fs3esettings.c's TT_EMOJIBOXWINDOW), same as FS3EMain_GetWindowPos does
 * for the main window. */
void FS3EEmojiBoxWindow_GetWindowPos(FS3EEmojiBoxWindow *ebw);

/* Call from the main task when SIGBREAKF_CTRL_F fires (same signal the
 * BoopsiDelay queue uses). Redraws the emoji grid if GM_RENDER was
 * deferred earlier because Intuition called it from the wrong process --
 * see the file header comment in fs3eemojibox.c. */
void FS3EEmojiBoxWindow_FlushPendingRender(FS3EEmojiBoxWindow *ebw);

/* Resolves the grid's most recently clicked cell (GID_EMOJIBOX_GRID) to
 * its UTF-8 emoji string. Returns NULL if nothing valid was clicked
 * (grid not created, or a header/corner cell). The returned pointer is
 * into a static table -- always valid, never needs freeing. */
const char *FS3EEmojiBoxWindow_GetClickedUTF8(FS3EEmojiBoxWindow *ebw);

BOOL FS3EEmojiBox_HandleFKey(FS3EEmojiBoxWindow *ebw, Object *editor, ULONG code, ULONG qualifiers,
                         struct Window *win);

#endif /* FS3EEMOJIBOX_H */
