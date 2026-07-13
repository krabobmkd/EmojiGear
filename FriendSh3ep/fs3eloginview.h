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
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <libraries/utf8rastport.h>

typedef struct FS3ELoginView {
    Object *windowObj;     /* BOOPSI window object (persistent) */
    struct Window *window; /* Intuition window, valid while open */

    LONG left, top, width, height; /* remembered window geometry */

    Object *layout;        /* outer layout - add to a parent or as ParentGroup */

    /* Phase 1: always enabled */
    Object *serverEditor;      /* string.gadget: instance/server URL */
    Object *userEditor;        /* string.gadget: user/account (informational) */
    Object *loginBtn;          /* "Connect" button — sends LOGIN_START */

    /* Phase 2: disabled until server returns the authorize URL */
    Object *urlInstructLabel;  /* read-only no-bevel button — instruction text */
    Object *urlEditor;         /* read-only string.gadget showing the authorize URL */
    Object *codeEditor;        /* string.gadget: OAuth code pasted by user */
    Object *submitCodeBtn;     /* "Submit Code" button — sends LOGIN_FINISH */

    /* Accounts list: every known/logged account, click a row to switch
     * (see GID_LOGIN_ACCOUNTS_LIST in friendsh3ep.c's FS3EApp_SwitchAccount).
     * accList's nodes are owned by this view (AllocListBrowserNode'd,
     * rebuilt wholesale on every FS3ELoginView_SetAccountsList() call). */
    Object     *acclistBrowser;    /* listbrowser.gadget: server/user columns */
    struct List accList;
} FS3ELoginView;

/* One row of the accounts list -- server/user are copied into the
 * listbrowser node, caller does not need to keep them alive afterwards.
 * current marks the account currently connected/active (highlighted). */
typedef struct FS3ELoginAccountRow {
    const char *server;
    const char *user;
    BOOL        current;
} FS3ELoginAccountRow;

/* Build the BOOPSI window+layout. pointSize is forwarded to the
 * UniTextEditor fields. Returns TRUE on success. */
BOOL FS3ELoginView_Create(FS3ELoginView *lv,struct URPDrawContext *textDC);

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
const char *FS3ELoginView_GetANSIServer(FS3ELoginView *lv);
const char *FS3ELoginView_GetANSIUser(FS3ELoginView *lv);
const char *FS3ELoginView_GetANSICode(FS3ELoginView *lv);

/* Called when LOGIN_START reply arrives with the authorize URL.
 * Populates urlEditor and enables the phase-2 widgets. */
void FS3ELoginView_SetAuthorizeUrl(FS3ELoginView *lv, const char *url);

/* Empties serverEditor/userEditor. Call once credentials are confirmed
 * (saved account loaded, or a fresh login just completed) so there's
 * nothing left pre-filled to accidentally resubmit. */
void FS3ELoginView_ClearFields(FS3ELoginView *lv);

/* Rebuilds the accounts listbrowser from scratch (rows[0..count-1]).
 * Safe to call whether the window is open or closed, and with count==0
 * (empties the list). The row marked current, if any, shows selected. */
void FS3ELoginView_SetAccountsList(FS3ELoginView *lv,
                                    const FS3ELoginAccountRow *rows,
                                    ULONG count);

#endif /* FS3ELOGINVIEW_H */
