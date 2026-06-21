#ifndef FS3ELOGINVIEW_H
#define FS3ELOGINVIEW_H

/*
 * fs3eloginview.h - Login sub-window for FriendSh3ep.
 *
 * A classic BOOPSI window.class window (own dragbar/close/depth gadgets,
 * unlike the borderless main window), holding a centered named layout
 * group with a small login form: server, user and code fields, then a
 * "Login" button. Modeled after EmojiGear's EgSearchBox_* sub-window
 * pattern (egsearchbox.c/.h).
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

typedef struct FS3ELoginView {
    Object *windowObj;     /* BOOPSI window object (persistent) */
    struct Window *window; /* Intuition window, valid while open */

    LONG left, top, width, height; /* remembered window geometry */

    Object *layout;        /* outer layout - add to a parent or as ParentGroup */

    Object *serverEditor;  /* single-line UniTextEditor: instance/server */
    Object *userEditor;    /* single-line UniTextEditor: user/account */
    Object *codeEditor;    /* single-line UniTextEditor: OAuth code */
    Object *loginBtn;
} FS3ELoginView;

/* Build the BOOPSI window+layout. pointSize is forwarded to the
 * UniTextEditor fields. Returns TRUE on success. */
BOOL FS3ELoginView_Create(FS3ELoginView *lv, ULONG pointSize);

/* Dispose the window object and everything below it. */
void FS3ELoginView_Dispose(FS3ELoginView *lv);

/* Open (or bring to front) the login window on CurrentMainScreen. */
void FS3ELoginView_Open(FS3ELoginView *lv);

/* Close (hide) the login window. No-op if already closed. */
void FS3ELoginView_Close(FS3ELoginView *lv);

/* Handle input messages from this window. Call when its signal fires. */
BOOL FS3ELoginView_HandleInput(FS3ELoginView *lv);

/* Signal bit mask to OR into Wait(). Returns 0 when the window is closed. */
ULONG FS3ELoginView_GetSignalMask(FS3ELoginView *lv);

/* Field accessors. DO NOT KEEP the returned pointer; can return NULL. */
const char *FS3ELoginView_GetUTF8Server(FS3ELoginView *lv);
const char *FS3ELoginView_GetUTF8User(FS3ELoginView *lv);
const char *FS3ELoginView_GetUTF8Code(FS3ELoginView *lv);

#endif /* FS3ELOGINVIEW_H */
