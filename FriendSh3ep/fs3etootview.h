#ifndef FS3ETOOTVIEW_H
#define FS3ETOOTVIEW_H

/*
 * fs3etootview.h - "New toot" sub-window for FriendSh3ep.
 *
 * A classic BOOPSI window.class window mimicking brutaldon's web post
 * composer (brutaldon/templates/main/post_partial.html):
 *   - "CW or subject" single-line UniTextEditor
 *   - main body UniTextEditor (multi-line)
 *   - bottom bar: visibility chooser, char-count label, 2 emoji
 *     UniButtons (emojibox access), "Toot" button bottom-right.
 *
 * Pattern adapted from EmojiGear/egsearchbox.c, see fs3eloginview.h for
 * the analogous login sub-window.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

/* Public/Unlisted/Private/Direct, matching brutaldon's PRIVACY_CHOICES. */
#define FS3ETOOT_NUM_VISIBILITIES 4

typedef struct FS3ETootView {
    Object *windowObj;     /* BOOPSI window object (persistent) */
    struct Window *window; /* Intuition window, valid while open */

    LONG left, top, width, height; /* remembered window geometry */

    Object *layout;

    Object *subjectEditor; /* "CW or subject", single line */
    Object *bodyEditor;    /* main toot text, multi-line   */

    struct List   visibilityList;
    struct Node  *visibilityNodes[FS3ETOOT_NUM_VISIBILITIES];
    Object       *visibilityChooser;

    Object *charCountLabel;
    char    charCountText[32];

    Object *emojiBtn1;
    Object *emojiBtn2;

    Object *tootBtn;
} FS3ETootView;

/* Build the BOOPSI window+layout. pointSize is forwarded to the
 * UniTextEditor fields. Returns TRUE on success. */
BOOL FS3ETootView_Create(FS3ETootView *tv, ULONG pointSize);

/* Dispose the window object and everything below it. */
void FS3ETootView_Dispose(FS3ETootView *tv);

/* Open (or bring to front) the New toot window on CurrentMainScreen. */
void FS3ETootView_Open(FS3ETootView *tv);

/* Close (hide) the New toot window. No-op if already closed. */
void FS3ETootView_Close(FS3ETootView *tv);

/* Handle input messages from this window. Call when its signal fires. */
BOOL FS3ETootView_HandleInput(FS3ETootView *tv);

/* Signal bit mask to OR into Wait(). Returns 0 when the window is closed. */
ULONG FS3ETootView_GetSignalMask(FS3ETootView *tv);

/* Recompute the "N chars" label from the current body text length. */
void FS3ETootView_UpdateCharCount(FS3ETootView *tv);

/* Field accessors. DO NOT KEEP the returned pointer; can return NULL. */
const char *FS3ETootView_GetUTF8Subject(FS3ETootView *tv);
const char *FS3ETootView_GetUTF8Body(FS3ETootView *tv);

/* 0=public, 1=unlisted, 2=private, 3=direct */
LONG FS3ETootView_GetVisibility(FS3ETootView *tv);

#endif /* FS3ETOOTVIEW_H */
