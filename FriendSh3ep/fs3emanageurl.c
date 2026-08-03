/*
 * fs3emanageurl.c - handles clicks on plain-website-URL toot hot zones.
 *
 * See fs3emanageurl.h.
 */

#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/intuition.h>

#include <libraries/asl.h>
#include <proto/asl.h>

#include <libraries/openurl.h>
#include <proto/openurl.h>

#include "friendsh3ep.h"
#include "fs3esettings.h"
#include "fs3eboopsimainwindow.h"
#include "clipboard.h"
#include "fs3emanageurl.h"
#include "fs3erequests.h"
#include "network_fs3e/fs3enet.h"

extern struct Library *OpenURLBase;
extern struct Library *AslBase;

/* Straight to the browser, falling back to a clipboard copy if
 * openurl.library isn't installed -- see this file's header comment. */
static void manageurl_open(const char *url)
{
    if (OpenURLBase)
        URL_Open((STRPTR)url,
            URL_Launch,TRUE,
            URL_BringToFront,TRUE,
            URL_Show,TRUE,
            TAG_DONE);
    else
        Clipboard_WriteText(url);
}

/* FS3E_URLLINK_ASK: "Open with ?" -- Copy to Clipboard / Open with
 * OpenURL, the latter only offered when OpenURLBase is available (asking
 * a question with only one real answer isn't a question). Either way
 * exactly 2 gadgets, so EasyRequestArgs' real numbering is rightmost=0,
 * leftmost=1 (see TTL_HOT_BOOST's comment in friendsh3ep.c for the
 * general N-gadget rule this is the 2-gadget special case of). */
static void manageurl_ask(const char *url)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0,
        (UBYTE *)"FriendSh3ep - Open Link",
        (UBYTE *)"Open with ?",
        NULL
    };
    LONG choice;

    if (OpenURLBase) {
        es.es_GadgetFormat = (UBYTE *)"Copy to Clipboard|Open with OpenURL";
        choice = EasyRequestArgs(CurrentMainWindow, &es, NULL, NULL);
        if (choice == 1)
            Clipboard_WriteText(url);
        else if (choice == 0)
            URL_Open((STRPTR)url, TAG_DONE);
    } else {
        es.es_GadgetFormat = (UBYTE *)"Copy to Clipboard|Cancel";
        choice = EasyRequestArgs(CurrentMainWindow, &es, NULL, NULL);
        if (choice == 1)
            Clipboard_WriteText(url);
    }
}

/* Case-insensitive "does url end in ext" (ext includes the leading '.') --
 * same plain manual-compare approach as fs3etootview.c's ExtEquals() and
 * network_fs3e/fs3enet.c's FS3ENet_UrlHasExt(): neither of those lives in
 * a header shareable with this (main-task) side, so a third small copy
 * here is simpler than manufacturing one. */
static BOOL manageurl_url_has_ext(const char *url, const char *ext)
{
    ULONG urlLen, extLen, i;
    if (!url || !ext) return FALSE;
    urlLen = (ULONG)strlen(url);
    extLen = (ULONG)strlen(ext);
    if (extLen > urlLen) return FALSE;
    for (i = 0; i < extLen; i++) {
        char a = url[urlLen - extLen + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return FALSE;
    }
    return TRUE;
}

static BOOL manageurl_is_archive(const char *url)
{
    return manageurl_url_has_ext(url, ".lha") ||
           manageurl_url_has_ext(url, ".zip") ||
           manageurl_url_has_ext(url, ".lzh") ||
           manageurl_url_has_ext(url, ".lzx");
}

/* "Direct download .zip,.lha" -- file-save requester defaulting to
 * app->settings.downloadPath / the URL's own final path segment (the part
 * after the last '/'), then a background FS3ENETQ_FETCH_IMAGE download
 * straight to wherever the user confirmed -- see
 * FS3ENetFetchImageReq_AllocDownload's doc comment in fs3enet.h for the
 * "exact path, not a cache entry" mode this relies on. The requester
 * itself does the drawer+file path aggregation (fr_Drawer/fr_File), same
 * ":" vs "/" join fs3emediaview.c's mediaview_save_media() already
 * handles the same way. Cancelling the requester does nothing further --
 * no fall back to the Ask/OpenURL/Clipboard flow below, same as picking
 * Cancel on any other save-as dialog. */
static void manageurl_download_archive(const char *url)
{
    struct FileRequester *req;
    const char *slash;
    const char *defaultName;

    if (!AslBase || !app->netRequestPort) return;

    slash = strrchr(url, '/');
    defaultName = slash ? slash + 1 : url;
    if (!defaultName[0]) return; /* URL ends in '/' -- nothing to name the file */

    req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window,        (ULONG)CurrentMainWindow,
        ASLFR_TitleText,     (ULONG)"Download file to",
        ASLFR_DoSaveMode,    TRUE,
        ASLFR_RejectIcons,   TRUE,
        ASLFR_InitialDrawer, (ULONG)(app->settings.downloadPath ? app->settings.downloadPath : "ram:"),
        ASLFR_InitialFile,   (ULONG)defaultName,
        TAG_DONE);
    if (!req) return;

    if (AslRequest(req, NULL)) {
        char destPath[512];
        ULONG dirLen = (ULONG)strlen((char *)req->fr_Drawer);
        BOOL  needSlash = dirLen > 0 &&
                          req->fr_Drawer[dirLen - 1] != ':' &&
                          req->fr_Drawer[dirLen - 1] != '/';
        FS3ENetFetchImageReq *dlreq;

        snprintf(destPath, sizeof(destPath), "%s%s%s",
                 req->fr_Drawer, needSlash ? "/" : "", req->fr_File);

        dlreq = FS3ENetFetchImageReq_AllocDownload(url, destPath);
        if (dlreq) {
            FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, dlreq, sizeof(*dlreq));
            /* Let the user watch it happen instead of having to know the
             * Network menu entry exists -- see fs3enetworkview.h. */
            FS3ENetworkView_Open(&app->networkView);
        }
    }

    FreeAslRequest(req);
}

void ManageUrl(const char *url)
{
    if (!url || !url[0]) return;

    if (app->settings.directDownloadArchives && manageurl_is_archive(url)) {
        manageurl_download_archive(url);
        return;
    }

    switch (app->settings.urlLinkAction) {
        case FS3E_URLLINK_OPENURL:
            manageurl_open(url);
            break;

        case FS3E_URLLINK_CLIPBOARD:
            Clipboard_WriteText(url);
            break;

        case FS3E_URLLINK_ASK:
        default:
            manageurl_ask(url);
            break;
    }
}
