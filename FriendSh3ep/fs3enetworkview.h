#ifndef FS3ENETWORKVIEW_H
#define FS3ENETWORKVIEW_H

/*
 * fs3enetworkview.h - "Network" downloads window for FriendSh3ep.
 *
 * A classic BOOPSI window.class window, same shape as fs3esettingsview.c/
 * fs3eloginview.c: own dragbar/close/depth gadgets, opened on demand from
 * the Settings menu. Holds a listbrowser.gadget (most of the window) with
 * one row per tracked download, plus a read-only button.gadget acting as a
 * bottom status bar (BVS_NONE, GA_ReadOnly -- same "inert label" trick as
 * fs3eloginview.c's Spacer(), just with real, changing text).
 *
 * Scope: a row is created for a download the moment its first
 * FS3ENETQ_FETCH_PROGRESS ping arrives (see FS3ENetworkView_OnProgress).
 * Since progress pings are only ever sent for requests with
 * fs3enf_WantProgress=TRUE, and today those are exactly "click to view
 * full image" media fetches (fs3emediaview.c) and archive downloads
 * (fs3emanageurl.c's FS3ENetFetchImageReq_AllocDownload), this naturally
 * covers "images (can be big) and archive downloads" with no further
 * network_fs3e/ changes. Known accepted limitation: a download that
 * finishes within a single small chunk (under FS3ENET_PROGRESS_MIN_DELTA)
 * never sends a progress ping and so never appears here at all -- it still
 * gets its success/failure notifyMessage() status-bar line, just no row.
 *
 * Per the caller's explicit instruction, the window does NOT need to be
 * kept up to date while closed: FS3ENetworkView_OnProgress/OnFinished
 * always update the row data model, but only push it into the listbrowser
 * gadget (detach/rebuild/reattach, same pattern as
 * FS3ELoginView_SetAccountsList) when nv->window is non-NULL. Reopening the
 * window later calls FS3ENetworkView_RebuildList() once to catch it up.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <gadgets/listbrowser.h>

/* Row cap -- oldest FINISHED (OK/Error) row is evicted to make room for a
 * new ACTIVE one once full; an ACTIVE row is never evicted. */
#define FS3ENETV_MAX_ROWS 16

enum {
    FS3ENETV_ROWSTATUS_ACTIVE = 0,
    FS3ENETV_ROWSTATUS_OK,
    FS3ENETV_ROWSTATUS_ERROR
};

/* key is the download's URL (matches FS3ENetFetchProgress's fs3efp_Key /
 * FS3ENetFetchImageReq's fs3enf_Key -- both are the URL itself for the
 * wantProgress requests this window tracks). name is the URL's final path
 * segment, computed once when the row is created. */
typedef struct FS3ENetworkRow {
    char  key[384];
    char  name[64];
    ULONG bytesSoFar;
    ULONG totalBytes;  /* 0 = unknown */
    UBYTE status;      /* FS3ENETV_ROWSTATUS_* */
} FS3ENetworkRow;

typedef struct FS3ENetworkView {
    Object        *windowObj;
    struct Window *window;

    LONG left, top, width, height;

    Object *mainLayout;
    Object *listBrowser;
    struct List rowNodeList; /* AllocListBrowserNode'd, rebuilt wholesale */

    /* Filled with LOC()'d column titles at Create() time -- can't be a
     * file-static compile-time initializer like fs3eloginview.c's
     * accListColumns since LOC() is a runtime call, not a constant. The
     * LOC()'d pointers themselves stay valid for the app's whole lifetime
     * (see fs3elocale.c), so filling this once is enough. */
    struct ColumnInfo columnInfo[4];

    Object *statusBar;

    FS3ENetworkRow rows[FS3ENETV_MAX_ROWS];
    ULONG          rowCount;
} FS3ENetworkView;

BOOL  FS3ENetworkView_Create(FS3ENetworkView *nv, const char *title);
void  FS3ENetworkView_Dispose(FS3ENetworkView *nv);
void  FS3ENetworkView_Open(FS3ENetworkView *nv);
void  FS3ENetworkView_Close(FS3ENetworkView *nv);
BOOL  FS3ENetworkView_HandleInput(FS3ENetworkView *nv);
ULONG FS3ENetworkView_GetSignalMask(FS3ENetworkView *nv);

/* Creates the row for key on first call, updates bytesSoFar/totalBytes on
 * every call. Pushes the change into the listbrowser only if the window is
 * open (see this header's top comment). */
void FS3ENetworkView_OnProgress(FS3ENetworkView *nv, const char *key,
                                 ULONG bytesSoFar, ULONG totalBytes);

/* Marks the row for key (if any -- a no-op if that download never sent a
 * progress ping, see this header's top comment) OK or ERROR. */
void FS3ENetworkView_OnFinished(FS3ENetworkView *nv, const char *key, BOOL ok);

/* Drives the status-bar button's text -- see notifyMessage() in
 * friendsh3ep.h/.c, the intended sole caller. level is one of the
 * FS3ENOTIFY_* values from friendsh3ep.h; unused today (no color/icon per
 * level), kept for a future refinement. A no-op while the window is
 * closed, same "don't bother updating a hidden GUI" rule as the row list. */
void FS3ENetworkView_SetStatus(FS3ENetworkView *nv, const char *message, int level);

#endif /* FS3ENETWORKVIEW_H */
