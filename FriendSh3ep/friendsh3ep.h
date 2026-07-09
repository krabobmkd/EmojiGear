#ifndef FRIENDSH3EP_H
#define FRIENDSH3EP_H

/*
 * FriendSh3ep - main application header: struct App and global state.
 * See ARCHITECTURE.md for the overall design.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/classusr.h>
#include <libraries/utf8rastport.h>

#include "fs3eboopsimainwindow.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "fs3ethemeview.h"
#include "fs3esettingsview.h"
#include "fs3estyle.h"
#include "fs3emenu.h"
#include "fs3esettings.h"
#include "avatarimages.h"

#define FRIENDSH3EP_VERSION "0.2"

/* Login two-phase OAuth state machine */
typedef enum {
    FS3ELOGIN_IDLE = 0,
    FS3ELOGIN_WAITING_START,   /* LOGIN_START sent, awaiting async reply */
    FS3ELOGIN_WAITING_CODE,    /* authorize URL shown; waiting for code paste */
    FS3ELOGIN_WAITING_FINISH,  /* LOGIN_FINISH sent, awaiting async reply */
    FS3ELOGIN_DONE
} FS3ELoginPhase;

typedef enum {
    VIEWMODE_User    = 0,
    VIEWMODE_Home,
    VIEWMODE_Local,
    VIEWMODE_Fed,
    VIEWMODE_Search,
    VIEWMODE_Notifs,
    VIEWMODE_Bookmarks,
    VIEWMODE_News,
    VIEWMODE_NumberOf
} fs3eViewMode;


/* Application struct: holds every persistent BOOPSI object and IPC handle. */
struct App {
    Object *window_obj;        /* window.class object (persistent) */
    struct MsgPort *app_port;  /* needed to keep receiving messages while iconified */

    FS3EMainWindow mainwindow;
    FS3EMenu       menu;
    FS3ESettings   settings;

    /* Root vertical layout (Part A + B + C) */
    Object *mainlayout;

    /* Alternate window position for the altpos button.
     * Swapped with the current position on each button press via ChangeWindowBox.
     * altWinWidth == 0 means not yet set (first press only saves, doesn't move). */
    LONG altWinLeft, altWinTop, altWinWidth, altWinHeight;

    /* Part A: title bar (TitleBarLayoutClass).
     * Children owned by titleBarLayout and disposed with it. */
    Object *titleBarLayout;
    Object *titlebar_closeBtn;       /* GID_TITLEBAR_CLOSE   */
    Object *titlebar_iconifyBtn;     /* GID_TITLEBAR_ICONIFY */
    Object *titlebar_altposBtn;      /* GID_TITLEBAR_ALTPOS  */
    Object *titlebar_depthBtn;       /* GID_TITLEBAR_DEPTH   */
    /* Row-2 user icon is drawn directly by TitleBarLayout_OnRender() from
     * TBLAYOUT_AvatarImages/TBLAYOUT_AccountAcct -- no gadget/image object
     * of its own anymore (see fs3etitlebar.c's file header comment). */
    Object *titlebar_settingsBtn;       /*  */
    Object *titlebar_accountBtn;       /*  */
    Object *titlebar_newtootBtn;       /*  */

    /* Part B: navigation bar (NavBarLayoutClass).
     * nav_btns[0..7] correspond to GID_NAV_HOME..GID_NAV_ACCOUNTS. */
    Object *navBarLayout;
    Object *nav_btns[8];

    /* Part C: toot timeline (TootTimelineClass) */
    Object *tootTimeline;

    /* Shared draw context for all UniButtonP9 buttons */
    struct URPDrawContext *buttonDC;

    /* Classic BOOPSI sub-windows, opened on demand */
    FS3ELoginView  loginView;
    FS3ETootView   tootView;
    FS3EThemeView  themeView;
    FS3ESettingsView settingsView;

    /* fs3enet ports: requestPort send-only; replyPort receives async replies */
    struct MsgPort *netRequestPort;
    struct MsgPort *netReplyPort;

    /* fs3ethumb ports: same shape as the fs3enet ports above, but talking
     * to the thumbnail process (see fs3ethumb.h) instead of the network
     * process. */
    struct MsgPort *thumbRequestPort;
    struct MsgPort *thumbReplyPort;

    /* Login two-phase state machine (FS3ELoginPhase) */
    ULONG  loginPhase;
    char  *loginApiBaseUrl;    /* saved between LOGIN_START reply and LOGIN_FINISH send */
    char  *loginClientId;
    char  *loginClientSecret;

    /* Logged-in account — all NULL when not logged in */
    char  *accountApiBaseUrl;
    char  *accountAccessToken;
    char  *accountDisplayName;
    char  *accountAcct;
    char  *accountAvatarURL;
    char  *accountId;         /* Mastodon numeric account id (fma_Id); used
                                * for VIEWMODE_User's accounts/{id}/statuses
                                * fetch (see ViewModeTimeline). */

    /* Bitmask of VIEWMODE channels that have a fetch in flight or already
     * have data.  Bit i set → do not re-fetch channel i automatically. */
    ULONG  timelineFetchedMask;

    /* Bitmask of channels whose last fetch returned an error.
     * Cleared for a channel when a new fetch starts for it. */
    ULONG  timelineErrorMask;
    /* FS3ENetResult of the last timeline fetch that failed (for message text). */
    ULONG  lastTimelineResult;

    /* Avatar bitmap cache — one scaled BmImage per @user@instance */
    struct AvatarImages *avatarImages;

    /* Color theme — pens obtained from the current screen's ColorMap */
    FS3EStyle style;

    /* enum fs3eViewMode */
    ULONG     viewMode;
};

extern struct App *app;

/* Print pmessage (if non-NULL) and exit(0); runs exitclose() via atexit(). */
void cleanexit(const char *pmessage);

void fs3e_setViewMode(ULONG viewMode);
#endif /* FRIENDSH3EP_H */
