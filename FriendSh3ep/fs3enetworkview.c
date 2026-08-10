/*
 * fs3enetworkview.c - "Network" downloads window for FriendSh3ep.
 *
 * See fs3enetworkview.h. Window lifecycle pattern adapted from
 * fs3esettingsview.c; listbrowser pattern (columns, rebuild-detach-
 * reattach) adapted from fs3eloginview.c's accounts list.
 */

#include <stdio.h>
#include <string.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>

#include "friendsh3ep.h"
#include "fs3enetworkview.h"
#include "fs3elocale.h"
#include "fs3egadgetid.h"
#include "fs3eboopsimainwindow.h"

/* Max characters copied into a node's column text -- generous enough for
 * any realistic file name or status word (same idea as fs3eloginview.c's
 * ACCLIST_MAXCHARS). */
#define NETWORKLIST_MAXCHARS 255

/* ------------------------------------------------------------------ */
/* Row data model -- maintained regardless of window-open state        */
/* ------------------------------------------------------------------ */

static FS3ENetworkRow *networkview_find_row(FS3ENetworkView *nv, const char *key)
{
    ULONG i;
    if (!nv || !key) return NULL;
    for (i = 0; i < nv->rowCount; i++) {
        if (strcmp(nv->rows[i].key, key) == 0)
            return &nv->rows[i];
    }
    return NULL;
}

/* Derives a display name from a URL the same way fs3emanageurl.c's
 * manageurl_download_archive() picks a default save-as file name: the part
 * after the URL's last '/'. Falls back to the whole key if there's no '/'
 * or it's the last character. */
static void networkview_name_from_key(const char *key, char *out, ULONG outSize)
{
    const char *slash = strrchr(key, '/');
    const char *name = (slash && slash[1]) ? slash + 1 : key;
    strncpy(out, name, outSize - 1);
    out[outSize - 1] = '\0';
}

/* Appends a new ACTIVE row for key, evicting the oldest finished (OK/Error)
 * row first if the table is full -- an ACTIVE row is never evicted (see
 * this window's header comment). Returns NULL if full of ACTIVE rows
 * (every one of the up to FS3ENET_MAX_ACTIVE_DOWNLOADS=8 concurrent
 * downloads mid-flight at once -- well under FS3ENETV_MAX_ROWS=16, so this
 * is not expected to happen in practice). */
static FS3ENetworkRow *networkview_new_row(FS3ENetworkView *nv, const char *key)
{
    FS3ENetworkRow *row;

    if (nv->rowCount >= FS3ENETV_MAX_ROWS) {
        ULONG i;
        BOOL evicted = FALSE;
        for (i = 0; i < nv->rowCount; i++) {
            if (nv->rows[i].status != FS3ENETV_ROWSTATUS_ACTIVE) {
                memmove(&nv->rows[i], &nv->rows[i + 1],
                        (nv->rowCount - i - 1) * sizeof(FS3ENetworkRow));
                nv->rowCount--;
                evicted = TRUE;
                break;
            }
        }
        if (!evicted) return NULL;
    }

    row = &nv->rows[nv->rowCount++];
    memset(row, 0, sizeof(*row));
    strncpy(row->key, key, sizeof(row->key) - 1);
    networkview_name_from_key(key, row->name, sizeof(row->name));
    row->status = FS3ENETV_ROWSTATUS_ACTIVE;
    return row;
}

/* "NN%" if totalBytes is known, else "NNN KB" downloaded so far -- exactly
 * the two display modes asked for. Integer-only percent calculation (no
 * float, no 64-bit: this NDK's exec/types.h has no UQUAD -- see
 * fs3eaudio.c's own comment on that) that avoids overflowing bytesSoFar*100
 * for any file over ~41MB. */
static void networkview_format_progress(const FS3ENetworkRow *row, char *out, ULONG outSize)
{
    if (row->totalBytes > 0) {
        ULONG percent;
        if (row->totalBytes >= 100) {
            ULONG denom = row->totalBytes / 100;
            percent = denom ? (row->bytesSoFar / denom) : 100;
        } else {
            percent = (row->bytesSoFar >= row->totalBytes) ? 100 : 0;
        }
        if (percent > 100) percent = 100;
        snprintf(out, outSize, "%lu%%", (unsigned long)percent);
    } else {
        snprintf(out, outSize, "%lu KB", (unsigned long)(row->bytesSoFar / 1024));
    }
}

static const char *networkview_status_text(UBYTE status)
{
    switch (status) {
        case FS3ENETV_ROWSTATUS_OK:    return LOC(MSG_NETWORKV_STATUS_OK);
        case FS3ENETV_ROWSTATUS_ERROR: return LOC(MSG_NETWORKV_STATUS_ERROR);
        default:                       return LOC(MSG_NETWORKV_STATUS_ON);
    }
}

/* Detach -> free old nodes -> build new ones -> reattach, same rule (and
 * same reason) as FS3ELoginView_SetAccountsList's own doc comment in
 * fs3eloginview.c. No-op if the window is closed -- see this window's
 * header comment on not needing to stay live while hidden. */
static void networkview_rebuild_list(FS3ENetworkView *nv)
{
    ULONG i;

    if (!nv || !nv->listBrowser || !nv->window) return;

    SetGadgetAttrs((struct Gadget *)nv->listBrowser, nv->window, NULL,
        LISTBROWSER_Labels, (ULONG)~0, TAG_DONE);

    FreeListBrowserList(&nv->rowNodeList);
    NewList(&nv->rowNodeList);

    for (i = 0; i < nv->rowCount; i++) {
        char progressText[32];
        struct Node *node;

        networkview_format_progress(&nv->rows[i], progressText, sizeof(progressText));

        node = AllocListBrowserNode(3,
            LBNA_Column,        0,
                LBNCA_CopyText,   TRUE,
                LBNCA_MaxChars,   NETWORKLIST_MAXCHARS,
                LBNCA_Text,      (ULONG)nv->rows[i].name,
            LBNA_Column,        1,
                LBNCA_CopyText,   TRUE,
                LBNCA_MaxChars,   NETWORKLIST_MAXCHARS,
                LBNCA_Text,      (ULONG)progressText,
            LBNA_Column,        2,
                LBNCA_CopyText,   TRUE,
                LBNCA_MaxChars,   NETWORKLIST_MAXCHARS,
                LBNCA_Text,      (ULONG)networkview_status_text(nv->rows[i].status),
            TAG_DONE);
        if (!node) continue;
        AddTail(&nv->rowNodeList, node);
    }

    SetGadgetAttrs((struct Gadget *)nv->listBrowser, nv->window, NULL,
        LISTBROWSER_Labels, (ULONG)&nv->rowNodeList,
        TAG_DONE);
    RefreshGList((struct Gadget *)nv->listBrowser, nv->window, NULL, 1);
}

void FS3ENetworkView_OnProgress(FS3ENetworkView *nv, const char *key,
                                 ULONG bytesSoFar, ULONG totalBytes)
{
    FS3ENetworkRow *row;

    if (!nv || !key || !key[0]) return;

    row = networkview_find_row(nv, key);
    if (!row) row = networkview_new_row(nv, key);
    if (!row) return;

    row->bytesSoFar = bytesSoFar;
    row->totalBytes = totalBytes;

    networkview_rebuild_list(nv);
}

void FS3ENetworkView_OnFinished(FS3ENetworkView *nv, const char *key, BOOL ok)
{
    FS3ENetworkRow *row;

    if (!nv || !key || !key[0]) return;

    row = networkview_find_row(nv, key);
    if (!row) return; /* never sent a progress ping -- see header comment */

    row->status = ok ? FS3ENETV_ROWSTATUS_OK : FS3ENETV_ROWSTATUS_ERROR;
    /* last progress ping can round down (e.g. 99%) due to integer
     * truncation in networkview_format_progress -- snap to complete so a
     * finished row never displays anything but 100%. */
    if (ok && row->totalBytes > 0) row->bytesSoFar = row->totalBytes;

    networkview_rebuild_list(nv);
}

void FS3ENetworkView_SetName(FS3ENetworkView *nv, const char *key, const char *name)
{
    FS3ENetworkRow *row;

    if (!nv || !key || !key[0] || !name) return;

    row = networkview_find_row(nv, key);
    if (!row) row = networkview_new_row(nv, key);
    if (!row) return;

    strncpy(row->name, name, sizeof(row->name) - 1);
    row->name[sizeof(row->name) - 1] = '\0';

    networkview_rebuild_list(nv);
}

void FS3ENetworkView_SetStatus(FS3ENetworkView *nv, const char *message, int level)
{
    (void)level;

    if (!nv || !nv->statusBar || !nv->window || !message) return;

    SetGadgetAttrs((struct Gadget *)nv->statusBar, nv->window, NULL,
        GA_Text, (ULONG)message,
        TAG_DONE);
    RefreshGList((struct Gadget *)nv->statusBar, nv->window, NULL, 1);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

BOOL FS3ENetworkView_Create(FS3ENetworkView *nv, const char *title)
{
    {
        LONG sl = nv->left, st = nv->top, sw = nv->width, sh = nv->height;
        memset(nv, 0, sizeof(*nv));
        nv->left = sl; nv->top = st; nv->width = sw; nv->height = sh;
    }
    NewList(&nv->rowNodeList);

    nv->columnInfo[0].ci_Width = 200;
    nv->columnInfo[0].ci_Title = (STRPTR)LOC(MSG_NETWORKV_COL_NAME);
    nv->columnInfo[0].ci_Flags = CIF_WEIGHTED;
    nv->columnInfo[1].ci_Width = 60;
    nv->columnInfo[1].ci_Title = (STRPTR)LOC(MSG_NETWORKV_COL_PROGRESS);
    nv->columnInfo[1].ci_Flags = CIF_WEIGHTED;
    nv->columnInfo[2].ci_Width = 60;
    nv->columnInfo[2].ci_Title = (STRPTR)LOC(MSG_NETWORKV_COL_STATUS);
    nv->columnInfo[2].ci_Flags = CIF_WEIGHTED;
    nv->columnInfo[3].ci_Width = -1;
    nv->columnInfo[3].ci_Title = (STRPTR)~0;
    nv->columnInfo[3].ci_Flags = (ULONG)-1;

    nv->listBrowser = (Object *)NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID,                   GID_NETWORKV_LIST,
        GA_RelVerify,             TRUE,
        LISTBROWSER_ColumnInfo,  (ULONG)nv->columnInfo,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_Labels,      (ULONG)&nv->rowNodeList,
        LISTBROWSER_Selected,    (ULONG)-1,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_MinVisible,   8,
        TAG_END);
    if (!nv->listBrowser) return FALSE;

    /* Read-only, no-bevel button used purely as a status-bar label -- same
     * "inert label" trick as fs3eloginview.c's Spacer(), but with real
     * text (BUTTON_Transparent is NOT set here, unlike Spacer(), so it
     * still paints a flat background behind the text like a status bar
     * should). */
    nv->statusBar = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,       TRUE,
        GA_Text,           (ULONG)LOC(MSG_NETWORKV_IDLE),
        BUTTON_BevelStyle, BVS_NONE,
        TAG_END);
    if (!nv->statusBar) return FALSE;

    nv->mainLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_AddChild,      (ULONG)nv->listBrowser,
        CHILD_WeightedHeight, 1,
        LAYOUT_AddChild,      (ULONG)nv->statusBar,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!nv->mainLayout) return FALSE;

    nv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,  160,
        WA_Top,   100,
        WA_Width, 360,
        WA_Height, 260,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY | IDCMP_NEWSIZE,
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIZEGADGET |
                  WFLG_SMART_REFRESH,
        WA_Title, (ULONG)title,
        WINDOW_ParentGroup, (ULONG)nv->mainLayout,
        TAG_END);
    if (!nv->windowObj) {
        DisposeObject(nv->mainLayout);
        nv->mainLayout = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3ENetworkView_Dispose(FS3ENetworkView *nv)
{
    if (!nv) return;

    if (nv->windowObj) {
        FS3ENetworkView_Close(nv);
        DisposeObject(nv->windowObj);
        nv->windowObj = NULL;
    }

    /* nv->listBrowser was disposed along with windowObj above, but the
     * nodes we AllocListBrowserNode'd into nv->rowNodeList are not owned
     * by the gadget -- free them separately, same as
     * FS3ELoginView_Dispose(). */
    FreeListBrowserList(&nv->rowNodeList);
    NewList(&nv->rowNodeList);
}

void FS3ENetworkView_Open(FS3ENetworkView *nv)
{
    if (!nv || !nv->windowObj) return;

    if (nv->window) {
        WindowToFront(nv->window);
        ActivateWindow(nv->window);
        return;
    }

    if (CurrentMainScreen) {
        SetAttrs(nv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (nv->width > 0) {
        SetAttrs(nv->windowObj,
                 WA_Left,   (ULONG)nv->left,
                 WA_Top,    (ULONG)nv->top,
                 WA_Width,  (ULONG)nv->width,
                 WA_Height, (ULONG)nv->height,
                 TAG_END);
    }

    nv->window = (struct Window *)DoMethod(nv->windowObj, WM_OPEN, NULL);

    /* Catch up on whatever happened while the window was closed -- see
     * this window's header comment ("window will not have to be updated
     * if closed", so the reopen is where it finally syncs). */
    networkview_rebuild_list(nv);
}

void FS3ENetworkView_Close(FS3ENetworkView *nv)
{
    if (!nv || !nv->windowObj || !nv->window) return;

    GetAttr(WA_Left,   nv->windowObj, (ULONG *)&nv->left);
    GetAttr(WA_Top,    nv->windowObj, (ULONG *)&nv->top);
    GetAttr(WA_Width,  nv->windowObj, (ULONG *)&nv->width);
    GetAttr(WA_Height, nv->windowObj, (ULONG *)&nv->height);

    DoMethod(nv->windowObj, WM_CLOSE, NULL);
    nv->window = NULL;
}

BOOL FS3ENetworkView_HandleInput(FS3ENetworkView *nv)
{
    ULONG result;

    if (!nv || !nv->windowObj) return FALSE;
    if (!nv->window) return TRUE;

    while ((result = DoMethod(nv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3ENetworkView_Close(nv);
                return TRUE;

            case WMHI_RAWKEY:
            {
                ULONG key = result & 0x07f;
                ULONG isUp = (result & 0x080);

                if (key == 0x45 && isUp) { /* Esc */
                    FS3ENetworkView_Close(nv);
                    return TRUE;
                }
            }
                break;

            case WMHI_NEWSIZE:
                if (nv->listBrowser)
                    RefreshGList((struct Gadget *)nv->listBrowser, nv->window, NULL, 1);
                if (nv->statusBar)
                    RefreshGList((struct Gadget *)nv->statusBar, nv->window, NULL, 1);
                break;

            default:
                break;
        }
    }

    return TRUE;
}

ULONG FS3ENetworkView_GetSignalMask(FS3ENetworkView *nv)
{
    if (!nv || !nv->window) return 0;
    return (1L << nv->window->UserPort->mp_SigBit);
}
