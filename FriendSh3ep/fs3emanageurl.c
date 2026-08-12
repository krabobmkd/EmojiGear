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
#include "fs3erequester.h"
#include "network_fs3e/fs3enet.h"
#include "bdbprintf.h"

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
 * a question with only one real answer isn't a question) -- plus an
 * explicit Cancel, always placed last/rightmost so it lands on gadget ID
 * 0. EasyRequestArgs' real numbering for N gadgets is left-to-right
 * 1, 2, ..., N, 0 -- confirmed against the intuition.library/
 * EasyRequestArgs autodoc's RESULT section, which explains 0 is
 * deliberately the rightmost gadget "for compatibility with
 * AutoRequest(), which has FALSE for the rightmost gadget" (see
 * TTL_HOT_BOOST's comment in friendsh3ep.c for the general N-gadget rule
 * this follows). Putting Cancel there -- rather than leaving the
 * rightmost slot to whichever real action happened to be listed last --
 * means gadget ID 0 unambiguously means "did nothing" everywhere this
 * requester can terminate, including via a window-close gadget if the
 * running environment provides one on system requesters (classic
 * EasyRequestArgs itself has none, but this makes the return value safe
 * either way instead of depending on it). */
static void manageurl_ask(const char *url)
{
    LONG choice;

    if (OpenURLBase) {
        choice = FS3ERequester_Show(CurrentMainWindow, "FriendSh3ep - Open Link",
            "Open with ?", "Copy to Clipboard|Open with OpenURL|Cancel", FS3EREQ_QUESTION);
        ExpungeMessages();
        if (choice == 1)
            Clipboard_WriteText(url);
        else if (choice == 2)
            URL_Open((STRPTR)url, TAG_DONE);
        /* choice == 0: Cancel, or the requester otherwise closed without
         * picking a real action -- do nothing. */
    } else {
        choice = FS3ERequester_Show(CurrentMainWindow, "FriendSh3ep - Open Link",
            "Open with ?", "Copy to Clipboard|Cancel", FS3EREQ_QUESTION);
        ExpungeMessages();
        if (choice == 1)
            Clipboard_WriteText(url);
        /* choice == 0: Cancel -- do nothing (already the rightmost/0
         * gadget in this 2-gadget case, so no change needed here). */
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
            /* bdbprintf("ManageUrl: archive download url=%s dest=%s\n", url, destPath); */
            /* Row is keyed by url (see FS3ENetFetchImageReq_AllocDownload's
             * doc comment), but the Network window should show where the
             * file is actually landing on disk, not the server's own file
             * name -- see FS3ENetworkView_SetName's doc comment. Called
             * before the request is even sent so the row (and its real
             * name) appears immediately, not just once the first progress
             * ping arrives. */
            FS3ENetworkView_SetName(&app->networkView, url, destPath);
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
