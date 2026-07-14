/*
 * fs3erequests.c - network/thumbnail request construction and async reply
 * dispatch for FriendSh3ep, split out of friendsh3ep.c. See fs3erequests.h
 * for the public entry points and friendsh3ep.h for struct App.
 */

#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <gadgets/unitexteditor.h>

#include "compilers.h"
#include "bdbprintf.h"

#include "friendsh3ep.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "fs3emediaview.h"
#include "avatarimages.h"

#include "TootTimeline/fs3etoottimeline.h"

#include "network_fs3e/fs3enet.h"
#include "fs3ethumb.h"

#include "fs3erequests.h"
#include "fs3eaccounts.h"
#include "fs3enetworkhelper.h"

/* Functions defined in friendsh3ep.c, reused here -- not static there, see
 * the "Not static" comment on each definition. */
extern void  FS3EApp_CheckConnectionState(void);
extern void  FS3EApp_UpdateUserIcon(void);

/* Send a pre-allocated request block to the network process asynchronously.
 * On failure, frees data and returns FALSE.
 *
 * PutMsg() enqueues via Exec's Enqueue(), which is priority-ordered (FIFO
 * within equal priority) -- so a bulk FETCH_IMAGE backlog (one avatar plus
 * up to TTL_POST_MAX_MEDIA thumbnails per status, times a whole timeline
 * page) doesn't make an interactive request like switching timelines wait
 * behind dozens of queued downloads: FETCH_IMAGE goes in at a lower
 * priority than everything else, so a fresh TIMELINE/LOGIN/POST_STATUS
 * request cuts to the front of that backlog instead of queuing after it.
 *
 * Not static: fs3eaction.c's toot actions (e.g. Action_ToggleFavorite)
 * call this directly too -- see the extern declaration there. */
BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen)
{
    FS3ENetMessage *msg;
    if (!app->netRequestPort || !app->netReplyPort || !data) {
        if (data) FreeVec(data);
        return FALSE;
    }
    msg = (FS3ENetMessage *)AllocVec(sizeof(FS3ENetMessage), MEMF_CLEAR | MEMF_PUBLIC );
    if (!msg) { FreeVec(data); return FALSE; }
    msg->fs3em_Msg.mn_Length   = sizeof(*msg);
    msg->fs3em_Msg.mn_ReplyPort = app->netReplyPort;
    msg->fs3em_Msg.mn_Node.ln_Pri = (type == FS3ENETQ_FETCH_IMAGE) ? -5 : 0;

    msg->fs3em_Type    = type;
    msg->fs3em_Data    = data;
    msg->fs3em_DataLen = dataLen;
    PutMsg(app->netRequestPort, &msg->fs3em_Msg);
    return TRUE;
}

/* Builds the /api/v1/-relative path to fetch for a given VIEWMODE_* value
 * into buf (see FS3EMastodon_GetTimeline, which just appends this onto
 * "apiBaseUrl/api/v1/"). Returns FALSE if there's nothing to fetch yet
 * (unknown view mode, or VIEWMODE_User before accountId is known). */
static BOOL ViewModeTimeline(ULONG viewMode, char *buf, ULONG bufSize)
{
    /* Initial page is kept small on purpose: a big first page (Home used to
     * ask for 35) enqueues one low-priority FETCH_IMAGE request per avatar/
     * thumbnail, and since the network process is single-threaded/serialized
     * (see FS3ENet_ProcEntry), that backlog is what actually stalls switching
     * to another view right after startup -- not the timeline fetch itself.
     * Scrolling triggers incremental older/newer pages on top of this. */
    switch (viewMode) {
        case VIEWMODE_Home:
            snprintf(buf, bufSize, "timelines/home?limit=4");
            return TRUE;
        case VIEWMODE_Local:
            snprintf(buf, bufSize, "timelines/public?local=true&limit=4");
            return TRUE;
        case VIEWMODE_Fed:
            snprintf(buf, bufSize, "timelines/public?limit=4");
            return TRUE;
        case VIEWMODE_User:
            if (!app->accountId || !app->accountId[0]) return FALSE;
            /* exclude_reblogs=false is Mastodon's own documented default,
             * but made explicit rather than left implicit -- some servers/
             * versions could differ, and a profile that only ever boosts
             * (never posts originally) would otherwise show as empty. */
            snprintf(buf, bufSize, "accounts/%s/statuses?limit=4&exclude_reblogs=false", app->accountId);
            return TRUE;
        case VIEWMODE_Search:
            /* Only the FS3ESEARCH_USER_PROFILE sub-mode fetches anything
             * today (word/user search are future sub-modes of this same
             * channel -- see FS3ESearchMode). Mirrors VIEWMODE_User
             * above exactly, just for whichever account is currently
             * open instead of always our own. */
            if (app->searchMode != FS3ESEARCH_USER_PROFILE ||
                !app->searchProfileAccountId || !app->searchProfileAccountId[0])
                return FALSE;
            /* exclude_reblogs=false -- see the matching comment on
             * VIEWMODE_User above; a profile that only ever boosts is
             * exactly the case that prompted making this explicit. */
            snprintf(buf, bufSize, "accounts/%s/statuses?limit=4&exclude_reblogs=false", app->searchProfileAccountId);
            return TRUE;
        default:
            return FALSE;
    }
}

/* Send an async TIMELINE request for viewMode if credentials are available
 * and a fetch hasn't already been started for that channel. */
void FS3EApp_FetchTimeline(ULONG viewMode)
{
    char tl[128];
    FS3ENetTimelineReq *req;
    ULONG bit = (1UL << viewMode);

    /* Search is never driven by this "fetch once per session the first
     * time a channel is viewed" mechanism -- FS3EApp_OpenProfile() owns
     * its fetch entirely (fired once per profile opened, not once per
     * view switch), always as an FS3ENETPAGE_OLDER page so it correctly
     * lands via TIMELINE_AppendPost below the profile header (see
     * TTIMELINE_ShowProfile) instead of through the AddPost/prepend path
     * this function's FS3ENETPAGE_INITIAL request below would take.
     * Without this guard, merely switching back to an already-open
     * profile (fs3e_setViewMode calls this unconditionally) would fire a
     * second, wrongly-directed fetch. */
    if (viewMode == VIEWMODE_Search) return;

    /* Notifications aren't Status objects and don't have a /api/v1/-
     * relative timeline path the way every other channel does (see
     * ViewModeTimeline, which has no case for VIEWMODE_Notifs at all) --
     * FS3ENETQ_NOTIFICATIONS instead, reusing the exact same
     * timelineFetchedMask/timelineErrorMask bookkeeping so
     * FS3EApp_CheckConnectionState's waiting/idle text keeps working here
     * with no special-casing there. */
    if (viewMode == VIEWMODE_Notifs) {
        FS3ENetNotificationsReq *nreq;

        if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
        if (app->channelPopulatedMask & bit) return; /* already has its first page -- see the field comment */
        if (app->timelineFetchedMask & bit) return;


        nreq = FS3ENetNotificationsReq_Alloc(FS3ENETPAGE_INITIAL,
                   app->accountGeneration, app->accountApiBaseUrl,
                   app->accountAccessToken, NULL, NULL);
        if (!nreq) return;

        if (FS3EApp_NetSend(FS3ENETQ_NOTIFICATIONS, nreq, sizeof(*nreq))) {
            app->timelineFetchedMask |= bit;
            app->timelineErrorMask   &= ~bit;
            FS3EApp_CheckConnectionState();
        }
        return;
    }

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (app->channelPopulatedMask & bit) return; /* already has its first page -- see the field comment */
    if (app->timelineFetchedMask & bit) return;
    if (!ViewModeTimeline(viewMode, tl, sizeof(tl))) return;


    req = FS3ENetTimelineReq_Alloc(viewMode, FS3ENETPAGE_INITIAL,
              app->accountGeneration, FS3ENET_TLSHAPE_ARRAY,
              app->accountApiBaseUrl, app->accountAccessToken,
              tl, NULL, NULL, NULL);
    if (!req) return;

    if (FS3EApp_NetSend(FS3ENETQ_TIMELINE, req,
            sizeof(FS3ENetTimelineReq) /* net process only reads char* fields */)) {
        app->timelineFetchedMask |= bit;
        app->timelineErrorMask   &= ~bit; /* clear any previous error for this channel */
        FS3EApp_CheckConnectionState();
    }
}
/* Send an async TIMELINE request for viewMode paginating in `direction`
 * (FS3ENETPAGE_OLDER/NEWER) from whatever status id the gadget currently
 * has at that end of its list (TTIMELINE_OldestPostId/NewestPostId) --
 * no-op if a page in that direction is already in flight for this
 * channel, or there's no known id to paginate from yet. Separate from
 * FS3EApp_FetchTimeline's one-shot-per-session initial fetch: this fires
 * repeatedly, driven by scroll position/clicks on the pinned boundary
 * rows -- see the TTL_HOT_LOAD_OLDER/NEWER handling in
 * FS3EApp_HandleNetReply's GID_TTIMELINE case. */
void FS3EApp_FetchTimelinePage(ULONG viewMode, ULONG direction)
{
    char tl[128];
    FS3ENetTimelineReq *req;
    ULONG  bit = (1UL << viewMode);
    ULONG *inFlightMask = (direction == FS3ENETPAGE_OLDER)
                         ? &app->olderPageInFlightMask
                         : &app->newerPageInFlightMask;
    ULONG  attrTag = (direction == FS3ENETPAGE_OLDER)
                    ? TTIMELINE_OldestPostId : TTIMELINE_NewestPostId;
    ULONG  fromIdVal = 0;
    const char *fromId;

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (*inFlightMask & bit) return;
    if (!app->tootTimeline) return;

    /* Notifications: same FS3ENETQ_NOTIFICATIONS/postId-as-notification-id
     * reasoning as FS3EApp_FetchTimeline's VIEWMODE_Notifs branch --
     * TTIMELINE_OldestPostId/NewestPostId already read post->postId
     * generically, which for a notification row holds the notification's
     * own id (not the embedded status' id -- see
     * TTLPostSetup.notifStatusId), exactly what /api/v1/notifications'
     * max_id/min_id pagination needs. */
    if (viewMode == VIEWMODE_Notifs) {
        FS3ENetNotificationsReq *nreq;

        GetAttr(attrTag, app->tootTimeline, &fromIdVal);
        fromId = (const char *)fromIdVal;
        if (!fromId || !fromId[0]) return;


        nreq = FS3ENetNotificationsReq_Alloc(direction,
                   app->accountGeneration, app->accountApiBaseUrl, app->accountAccessToken,
                   (direction == FS3ENETPAGE_OLDER) ? fromId : NULL,
                   (direction == FS3ENETPAGE_NEWER) ? fromId : NULL);
        if (!nreq) return;

        if (FS3EApp_NetSend(FS3ENETQ_NOTIFICATIONS, nreq, sizeof(*nreq)))
            *inFlightMask |= bit;
        return;
    }

    if (!ViewModeTimeline(viewMode, tl, sizeof(tl))) return;

    GetAttr(attrTag, app->tootTimeline, &fromIdVal);
    fromId = (const char *)fromIdVal;
    if (!fromId || !fromId[0]) return; /* nothing loaded yet to paginate from */


    req = FS3ENetTimelineReq_Alloc(viewMode, direction,
              app->accountGeneration, FS3ENET_TLSHAPE_ARRAY,
              app->accountApiBaseUrl, app->accountAccessToken, tl,
              (direction == FS3ENETPAGE_OLDER) ? fromId : NULL,
              (direction == FS3ENETPAGE_NEWER) ? fromId : NULL,
              NULL);
    if (!req) return;

    if (FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(FS3ENetTimelineReq)))
        *inFlightMask |= bit;
}

/* Opens (or re-opens) a user's profile in the Search channel -- the
 * shared entry point for both TTL_HOT_AVATAR and TTL_HOT_MENTION clicks
 * (a toot author's avatar, or an @mention inside any toot/bio body).
 * acctOrHandle may or may not have a leading '@' (avatar-click data
 * never does, mention-click data always does -- see TTL_HOT_MENTION's
 * comment in fs3etoottimeline.h) -- stripped here so both paths converge
 * on the same acct string /api/v1/accounts/lookup expects.
 *
 * Only starts the lookup; the rest of the flow (header population,
 * avatar fetch, relationship + first toot page) continues in the
 * FS3ENETQ_ACCOUNT_LOOKUP reply handler once the account id is known. */
void FS3EApp_OpenProfile(const char *acctOrHandle)
{
    const char *acct = acctOrHandle;
    FS3ENetAccountLookupReq *req;

    if (!acct || !acct[0]) return;
    if (acct[0] == '@') acct++;
    if (!acct[0]) return;
    if (!app->accountApiBaseUrl) return;

    if (app->searchProfileAcct)      { FreeVec(app->searchProfileAcct);      app->searchProfileAcct      = NULL; }
    if (app->searchProfileAccountId) { FreeVec(app->searchProfileAccountId); app->searchProfileAccountId = NULL; }
    app->searchProfileAcct = NetStrDup(acct);
    app->searchMode        = FS3ESEARCH_USER_PROFILE;

    fs3e_setViewMode(VIEWMODE_Search);

    /* Mirror the opened profile into the search editor as "user@server" --
     * Mastodon's own "acct" field already has "@server" for remote users,
     * but is bare "user" for local ones (same instance as us), so fill in
     * our own instance's domain in that case. */
    if (app->searchWordEditor) {
        char handle[256];
        if (strchr(acct, '@')) {
            snprintf(handle, sizeof(handle), "%s", acct);
        } else {
            const char *domain = app->accountApiBaseUrl ? app->accountApiBaseUrl : "";
            if (strncmp(domain, "https://", 8) == 0) domain += 8;
            else if (strncmp(domain, "http://", 7) == 0) domain += 7;
            snprintf(handle, sizeof(handle), "%s@%s", acct, domain);
        }
        SetGdAttrs(app->searchWordEditor, UTED_Text, (ULONG)handle, TAG_END);
    }

    if (!app->searchProfileAcct) return;

    req = FS3ENetAccountLookupReq_Alloc(app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              app->searchProfileAcct);
    if (!req) return;

    FS3EApp_NetSend(FS3ENETQ_ACCOUNT_LOOKUP, req, sizeof(*req));
}

/* "Discussion mode" -- shows statusId's toot as the first item in the
 * Search channel, followed by its replies (Mastodon's "descendants"),
 * each flagged isThreadReply (see TTLPostSetup.isThreadReply). Deliberately
 * simple, mirroring the user's own description: no ancestors (the chain
 * this toot itself replied to), no nested/threaded indentation, no
 * pagination for long discussions -- just "main toot, then its answers
 * below," flat, in server order. Unlike FS3EApp_OpenProfile there's no
 * single atomic "clear + seed" tag, so the channel is cleared proactively
 * here, before either fetch's reply can land.
 *
 * Two FS3ENETQ_TIMELINE requests, fired back to back: the main toot
 * (FS3ENET_TLSHAPE_SINGLE, lands via AddPost as item 1 since the channel
 * is already empty) then its replies (FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS,
 * FS3ENETPAGE_OLDER so it lands via AppendPost below -- same trick
 * FS3EApp_OpenProfile's second request uses for its header). Relies on
 * the network process being a single serialized task (see the
 * accountGeneration comment in FS3EApp_HandleNetReply's FS3ENETQ_TIMELINE
 * case) so reply #1 always lands before reply #2 is even attempted. */
void FS3EApp_OpenDiscussion(const char *statusId)
{
    char tl[128];
    FS3ENetTimelineReq *req;

    if (!statusId || !statusId[0]) return;
    if (!app->accountApiBaseUrl) return;

    if (app->searchDiscussionStatusId) { FreeVec(app->searchDiscussionStatusId); app->searchDiscussionStatusId = NULL; }
    app->searchDiscussionStatusId = NetStrDup(statusId);
    app->searchMode = FS3ESEARCH_DISCUSSION;

    fs3e_setViewMode(VIEWMODE_Search);

    if (app->tootTimeline)
        SetAttrs(app->tootTimeline, TTIMELINE_ClearPosts, TRUE, TAG_DONE);

    if (!app->searchDiscussionStatusId) return;

    snprintf(tl, sizeof(tl), "statuses/%s", app->searchDiscussionStatusId);
    req = FS3ENetTimelineReq_Alloc(VIEWMODE_Search, FS3ENETPAGE_INITIAL,
              app->accountGeneration, FS3ENET_TLSHAPE_SINGLE,
              app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              tl, "", "", NULL);
    if (req) FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(*req));

    snprintf(tl, sizeof(tl), "statuses/%s/context", app->searchDiscussionStatusId);
    req = FS3ENetTimelineReq_Alloc(VIEWMODE_Search, FS3ENETPAGE_OLDER,
              app->accountGeneration, FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS,
              app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              tl, "", "", NULL);
    if (req) FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(*req));
}

/* Index into the MSG_SEARCHV_WAIT1..4 rotation -- bumped once per search
 * fired below, NOT on every FS3EApp_CheckConnectionState call (that runs on
 * all sorts of unrelated state changes too, e.g. login phase or account
 * changes; incrementing here would rotate the message mid-search for no
 * reason instead of picking one per search).
 *
 * Not static: FS3EApp_CheckConnectionState (friendsh3ep.c) reads this to
 * pick which wait message to show -- see the extern declaration there. */
ULONG s_searchWaitMsgIdx = 0;

/* Word/hashtag search -- the Enter-pressed handler on the search line (see
 * GID_SEARCH_WORD_EDITOR in the main event loop) calls this for any query
 * that doesn't look like a "name@server" user handle. Mirrors
 * FS3EApp_OpenDiscussion() exactly: clears the Search channel synchronously
 * (no profile header, just a flat status list, same as discussion mode),
 * then fires one FS3ENETQ_TIMELINE request through the
 * FS3ENET_TLSHAPE_SEARCH_STATUSES shape (GET /api/v2/search, see
 * FS3EMastodon_GetTimeline). query is raw UTF-8 text, NOT URL-encoded --
 * FS3ENet_HandleTimeline (network process) does that itself from
 * fs3et_SearchQuery. No pagination yet (single page, FS3ENETPAGE_INITIAL
 * only): TTL_HOT_LOAD_OLDER/NEWER route through ViewModeTimeline(), which
 * has no FS3ESEARCH_WORD case, so they're a harmless no-op for now. */
void FS3EApp_SearchWord(const char *query)
{
    char tl[64];
    FS3ENetTimelineReq *req;

    if (!query || !query[0]) return;
    if (!app->accountApiBaseUrl) return;

    if (app->searchProfileAcct)        { FreeVec(app->searchProfileAcct);        app->searchProfileAcct        = NULL; }
    if (app->searchProfileAccountId)   { FreeVec(app->searchProfileAccountId);   app->searchProfileAccountId   = NULL; }
    if (app->searchDiscussionStatusId) { FreeVec(app->searchDiscussionStatusId); app->searchDiscussionStatusId = NULL; }
    app->searchMode = FS3ESEARCH_WORD;

    fs3e_setViewMode(VIEWMODE_Search);

    if (app->tootTimeline)
        SetAttrs(app->tootTimeline, TTIMELINE_ClearPosts, TRUE, TAG_DONE);

    snprintf(tl, sizeof(tl), "search?type=statuses&limit=20");
    req = FS3ENetTimelineReq_Alloc(VIEWMODE_Search, FS3ENETPAGE_INITIAL,
              app->accountGeneration, FS3ENET_TLSHAPE_SEARCH_STATUSES,
              app->accountApiBaseUrl,
              app->accountAccessToken ? app->accountAccessToken : "",
              tl, "", "", query);
    if (req && FS3EApp_NetSend(FS3ENETQ_TIMELINE, req, sizeof(*req))) {
        /* Marks the Search channel "fetching" so
         * FS3EApp_CheckConnectionState shows one of the search wait
         * messages instead of "Updating..." -- cleared the same way every
         * other channel's INITIAL fetch already is, in the generic
         * FS3ENETQ_TIMELINE reply handler. */
        app->timelineFetchedMask |= (1UL << VIEWMODE_Search);
        s_searchWaitMsgIdx++;
        FS3EApp_CheckConnectionState();
    }
}

/* GID_LOGIN_LOGIN_BUTTON -- start a fresh OAuth flow for whatever server is
 * typed into the login window. With multi-account support this means "add
 * another account", not "log out of the current one", so only the interim
 * login state (any half-finished previous flow) is discarded here; the
 * currently active account (app->accountXXX) is left untouched. Switching
 * to an already-known account is the acclistGroup listbrowser's job
 * (FS3EApp_SwitchAccount), not this button. */
void FS3EApp_LoginStart(void)
{
    char serverBuf[256];
    const char *server = NormalizeServerUrl(
        FS3ELoginView_GetANSIServer(&app->loginView),
        serverBuf, sizeof(serverBuf));

    FS3EApp_FreeLoginState(); /* also sets loginPhase = IDLE */
    if (server && server[0]) {
        FS3ENetLoginStartReq *req = FS3ENetLoginStartReq_Alloc(server);
        if (req) {
            if (app->loginApiBaseUrl) FreeVec(app->loginApiBaseUrl);
            app->loginApiBaseUrl = NetStrDup(server);
            if (FS3EApp_NetSend(FS3ENETQ_LOGIN_START, req, sizeof(*req))) {
                app->loginPhase = FS3ELOGIN_WAITING_START;
                FS3EApp_CheckConnectionState();
            }
        }
    }
}

/* GID_LOGIN_SUBMIT_CODE_BUTTON -- exchange the pasted OOB code for an access
 * token, completing the OAuth flow FS3EApp_LoginStart() began. */
void FS3EApp_LoginSubmitCode(void)
{
    const char *code = FS3ELoginView_GetANSICode(&app->loginView);
    if (app->loginPhase == FS3ELOGIN_WAITING_CODE &&
        code && code[0] &&
        app->loginApiBaseUrl &&
        app->loginClientId && app->loginClientSecret)
    {
        FS3ENetLoginFinishReq *req =
            FS3ENetLoginFinishReq_Alloc(
                app->loginApiBaseUrl,
                app->loginClientId,
                app->loginClientSecret,
                code);
        if (req) {
            if (FS3EApp_NetSend(FS3ENETQ_LOGIN_FINISH, req, sizeof(*req))) {
                app->loginPhase = FS3ELOGIN_WAITING_FINISH;
                FS3EApp_CheckConnectionState();
            }
        }
    }
}

/* Handle one reply message from the network process. */
void FS3EApp_HandleNetReply(FS3ENetMessage *msg)
{
    switch (msg->fs3em_Type)
    {
    case FS3ENETQ_LOGIN_START:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetLoginStartReply *reply = (FS3ENetLoginStartReply *)msg->fs3em_Data;
            /* Save client credentials for phase 2 */
            if (app->loginClientId)     FreeVec(app->loginClientId);
            if (app->loginClientSecret) FreeVec(app->loginClientSecret);
            app->loginClientId     = NetStrDup(reply->fs3enl_ClientId);
            app->loginClientSecret = NetStrDup(reply->fs3enl_ClientSecret);
            app->loginPhase = FS3ELOGIN_WAITING_CODE;


            /* Show the URL in the login window and enable phase-2 widgets. */
            FS3ELoginView_SetAuthorizeUrl(&app->loginView, reply->fs3enl_AuthorizeUrl);
        } else {
            FS3EApp_FreeLoginState();
        }
        break;

    case FS3ENETQ_LOGIN_FINISH:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetLoginFinishReply *reply = (FS3ENetLoginFinishReply *)msg->fs3em_Data;
            FS3EApp_SetAccount(app->loginApiBaseUrl,
                               reply->fs3enl_AccessToken,
                               reply->fs3enl_Account.fma_DisplayName,
                               reply->fs3enl_Account.fma_Acct,
                               reply->fs3enl_Account.fma_AvatarURL,
                               reply->fs3enl_Account.fma_Id);
            FS3EApp_SaveAccount();
            FS3EApp_FreeLoginState();
            app->loginPhase = FS3ELOGIN_DONE;
            /* Fetch the current view mode timeline */
            app->timelineFetchedMask  = 0; /* reset so new account fetches fresh */
            app->channelPopulatedMask = 0;
            FS3EApp_FetchTimeline(app->viewMode);
            /* Credentials just confirmed -- empty the fields so there's
             * nothing to accidentally resubmit later (see the matching
             * clear at startup, right after FS3ELoginView_Create). */
            FS3ELoginView_ClearFields(&app->loginView);
            FS3EApp_RefreshLoginAccountsList(); /* new/refreshed row, mark current */
            FS3ELoginView_Close(&app->loginView);
        } else {
            struct EasyStruct es = {
                sizeof(struct EasyStruct), 0,
                (UBYTE *)"FriendSh3ep - Login Error",
                (UBYTE *)"Could not exchange the authorization code.\nCheck the code and try again.",
                (UBYTE *)"OK"
            };
            EasyRequestArgs(CurrentMainWindow, &es, NULL, NULL);
            app->loginPhase = FS3ELOGIN_WAITING_CODE; /* let user retry code */
        }
        break;

    case FS3ENETQ_VERIFY_ACCOUNT:
        /* Backfill for an account.dat saved before accountId existed (see
         * FS3EApp_BackfillAccountId()). Re-runs the same "resync from
         * server" FS3EApp_SetAccount() does at login, just without a fresh
         * access token -- apiBaseUrl/accessToken are unchanged, only the
         * account fields (chiefly fma_Id) are new. */
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetVerifyAccountReply *reply = (FS3ENetVerifyAccountReply *)msg->fs3em_Data;
            FS3EApp_SetAccount(app->accountApiBaseUrl,
                               app->accountAccessToken,
                               reply->fs3eva_Account.fma_DisplayName,
                               reply->fs3eva_Account.fma_Acct,
                               reply->fs3eva_Account.fma_AvatarURL,
                               reply->fs3eva_Account.fma_Id);
            FS3EApp_SaveAccount();
            FS3EApp_RefreshLoginAccountsList();
            /* In case the user is already sitting on a channel that
             * couldn't fetch without an id (VIEWMODE_User). */
            FS3EApp_FetchTimeline(app->viewMode);
        } else
	{
		/*TODO: tell account failed to wait screen message */
	}
        break;

    case FS3ENETQ_INSTANCE_INFO:
        /* Always OK (see FS3ENet_HandleInstanceInfo -- a fetch failure on
         * the network side is reported via fs3eii_Known, not fs3em_Result),
         * but guard anyway rather than assume. Only a server-confirmed
         * value (fs3eii_Known) is shown -- a failed fetch leaves
         * accountMaxChars at 0 ("Max: -"), never presenting
         * FS3EMastodon_GetInstanceInfo's internal fallback guess as fact. */
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ENetInstanceInfoReply *reply = (FS3ENetInstanceInfoReply *)msg->fs3em_Data;
            if (reply->fs3eii_Known) {
                app->accountMaxChars = reply->fs3eii_MaxChars;
                FS3ETootView_UpdateCharCount(&app->tootView);
            }
        } else {
		/* tell instance info failed to wait state message */
	}
        break;

    case FS3ENETQ_TIMELINE:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetTimelineReply *reply = (FS3ENetTimelineReply *)msg->fs3em_Data;
            FS3ENetStatus *statuses = (FS3ENetStatus *)(reply + 1);
            BOOL  older = (reply->fs3et_PageDirection == FS3ENETPAGE_OLDER);
            ULONG addAttr = older ? TTIMELINE_AppendPost : TTIMELINE_AddPost;
            ULONG i;

            /* This request was sent for whichever account was active back
             * when it went out -- the network process is one serialized
             * task, so it can still reply after the user has since
             * switched accounts (see accountGeneration's comment in
             * friendsh3ep.h). Applying it now would splice a stale
             * account's posts into the *new* account's channel and/or
             * re-clear timelineFetchedMask, restarting a fetch that
             * belongs to no one currently active -- discard outright
             * without touching any mask, exactly as if it was never sent;
             * the request that actually matches the current account
             * (fired by FS3EApp_SwitchAccount/SetAccount) owns this
             * channel's bookkeeping instead. */
            if (reply->fs3et_AccountGeneration != app->accountGeneration) {
                break;
            }
/*
            printf("timeline reply: viewMode=%u dir=%u count=%u\n",
                   (unsigned)reply->fs3et_ViewModeBit,
                   (unsigned)reply->fs3et_PageDirection, (unsigned)reply->fs3et_Count);
*/
            switch (reply->fs3et_PageDirection) {
                case FS3ENETPAGE_OLDER:
                    app->olderPageInFlightMask &= ~(1UL << reply->fs3et_ViewModeBit);
                    break;
                case FS3ENETPAGE_NEWER:
                    app->newerPageInFlightMask &= ~(1UL << reply->fs3et_ViewModeBit);
                    break;
                default:
                    /* Fetch is complete — clear the in-flight bit so
                     * CheckConnectionState can show the "connected" idle
                     * message instead of "Updating…", AND mark the channel
                     * as populated so FS3EApp_FetchTimeline won't fire
                     * another INITIAL fetch (and re-insert a duplicate
                     * first page) next time this channel is entered --
                     * see channelPopulatedMask's doc comment for the bug
                     * this fixes. */
                    app->timelineFetchedMask   &= ~(1UL << reply->fs3et_ViewModeBit);
                    app->channelPopulatedMask  |=  (1UL << reply->fs3et_ViewModeBit);
                    break;
            }

            /* A profile can be replaced (see FS3EApp_OpenProfile) while an
             * older/newer page fetched for the PREVIOUS profile is still
             * in flight -- this reply has no field saying "which profile"
             * it was for beyond the statuses themselves, so for Search
             * specifically, cross-check the first status's own OWNER
             * against whichever profile is CURRENTLY loaded and discard
             * the whole page if it doesn't match. accounts/{id}/statuses
             * only ever returns that one account's own statuses, so this
             * is a reliable check, not a heuristic -- but "own statuses"
             * includes boosts, and fmas_Acct is always the CONTENT's
             * author (src), not who posted it into this timeline -- for a
             * boost those differ (fmas_Acct is the ORIGINAL author,
             * fmas_BoostByAcct is the booster). Using fmas_Acct alone here
             * made every reply for a boost-only profile look "stale" and
             * get silently discarded, even on the very first, entirely
             * legitimate page -- see fmas_BoostByAcct's own comment in
             * fs3enet.h. This is exactly the kind of stale transient data
             * (a pointer into a reply block for a profile the user already
             * navigated away from) that must never be applied to whatever
             * now-different content actually occupies the Search channel,
             * so the check itself still needs to stay -- it just needs
             * the right field. The in-flight bookkeeping above still runs
             * unconditionally either way, so a stale reply doesn't leave
             * olderPageInFlightMask/etc. permanently stuck for the new
             * profile. Gated on FS3ESEARCH_USER_PROFILE specifically --
             * discussion mode (see FS3EApp_OpenDiscussion) also uses the
             * Search channel but never sets searchProfileAcct, so without
             * this guard every discussion-mode reply would look "stale"
             * (searchProfileAcct NULL) and get discarded outright. */
            if (reply->fs3et_ViewModeBit == VIEWMODE_Search &&
                app->searchMode == FS3ESEARCH_USER_PROFILE) {
                const char *statusOwnerAcct = NULL;
                if (reply->fs3et_Count > 0) {
                    statusOwnerAcct = (statuses[0].fmas_BoostByAcct && statuses[0].fmas_BoostByAcct[0])
                                    ? statuses[0].fmas_BoostByAcct : statuses[0].fmas_Acct;
                }
                if (!app->searchProfileAcct ||
                    (reply->fs3et_Count > 0 &&
                     (!statusOwnerAcct || strcmp(statusOwnerAcct, app->searchProfileAcct) != 0)))
                {
                    break;
                }
            }

            /* Mastodon always returns each page newest-first. An older
             * page (max_id) is appended below existing content, so it
             * must walk forward (newest-of-page lands right below what's
             * already there, oldest-of-page ends up at the very bottom).
             * An initial/newer page is prepended above existing content,
             * so it walks in reverse (oldest-of-page first, so the
             * overall newest -- index 0 -- ends up prepended last, at the
             * very top) -- see TTIMELINE_AddPost/AppendPost. */
            for (i = 0; i < reply->fs3et_Count; i++) {
                ULONG idx = older ? i : (reply->fs3et_Count - 1 - i);
                TTLPostSetup post;
                memset(&post, 0, sizeof(post));
                post.username    = statuses[idx].fmas_DisplayName[0]
                                   ? statuses[idx].fmas_DisplayName
                                   : statuses[idx].fmas_Acct;
                post.acct        = statuses[idx].fmas_Acct;
                /* Compares the ORIGINAL author's acct, not the booster's
                 * (fmas_BoostByAcct) -- boosting someone else's toot must
                 * not grant Modify/Delete on it. See TTLPostSetup.isOwn. */
                post.isOwn       = (statuses[idx].fmas_Acct && app->accountAcct &&
                                     !strcmp(statuses[idx].fmas_Acct, app->accountAcct));
                /* Every status in a CONTEXT_DESCENDANTS reply is, by
                 * definition, one of the discussion's replies -- see
                 * TTLPostSetup.isThreadReply / FS3EApp_OpenDiscussion. */
                post.isThreadReply = (reply->fs3et_ResponseShape == FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS);
                post.body        = statuses[idx].fmas_Content;
                post.timestamp   = statuses[idx].fmas_CreatedAt;
                post.boostBy     = statuses[idx].fmas_BoostBy;
                post.boostByAcct = statuses[idx].fmas_BoostByAcct;
                post.avatarURL   = statuses[idx].fmas_AvatarURL;
                post.postId      = statuses[idx].fmas_Id;
                post.repliesCount    = statuses[idx].fmas_RepliesCount;
                post.reblogsCount    = statuses[idx].fmas_ReblogsCount;
                post.favouritesCount = statuses[idx].fmas_FavouritesCount;
                post.favourited      = statuses[idx].fmas_Favourited;
                post.reblogged       = statuses[idx].fmas_Reblogged;
                {
                    ULONG mi;
                    for (mi = 0; mi < statuses[idx].fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                        post.mediaUrls[mi] = statuses[idx].fmas_MediaUrls[mi];
                        post.mediaIds[mi]  = statuses[idx].fmas_MediaIds[mi];
                        switch (statuses[idx].fmas_MediaKind[mi]) {
                            case FS3ENET_MEDIAKIND_IMAGE: post.mediaKinds[mi] = TTL_MEDIA_KIND_IMAGE; break;
                            case FS3ENET_MEDIAKIND_VIDEO: post.mediaKinds[mi] = TTL_MEDIA_KIND_VIDEO; break;
                            case FS3ENET_MEDIAKIND_GIFV:  post.mediaKinds[mi] = TTL_MEDIA_KIND_GIFV;  break;
                            case FS3ENET_MEDIAKIND_AUDIO: post.mediaKinds[mi] = TTL_MEDIA_KIND_AUDIO; break;
                            default:                       post.mediaKinds[mi] = TTL_MEDIA_KIND_UNKNOWN; break;
                        }
                    }
                    post.mediaCount = statuses[idx].fmas_MediaCount;
                }
                {
                    ULONG oi;
                    for (oi = 0; oi < statuses[idx].fmas_PollOptionCount && oi < TTL_POST_MAX_POLL_OPTIONS; oi++) {
                        post.pollOptionTitles[oi] = statuses[idx].fmas_PollOptionTitles[oi];
                        post.pollOptionVotes[oi]  = statuses[idx].fmas_PollOptionVotes[oi];
                    }
                    post.pollOptionCount = statuses[idx].fmas_PollOptionCount;
                    post.pollVotesCount  = statuses[idx].fmas_PollVotesCount;
                    post.pollExpired     = statuses[idx].fmas_PollExpired;
                    post.pollMultiple    = statuses[idx].fmas_PollMultiple;
                }

                /* Trigger avatar download for this user if not already requested. */
                if (app->avatarImages &&
                    statuses[idx].fmas_AvatarURL &&
                    statuses[idx].fmas_AvatarURL[0] &&
                    statuses[idx].fmas_Acct &&
                    !AvatarImages_IsRequested(app->avatarImages,
                                              statuses[idx].fmas_Acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(statuses[idx].fmas_AvatarURL) + 1
                                  + strlen(statuses[idx].fmas_Acct) + 1
                                  + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
                    FS3ENetFetchImageReq *req =
                        FS3ENetFetchImageReq_Alloc(statuses[idx].fmas_AvatarURL,
                                                   statuses[idx].fmas_Acct,
                                                   FS3E_CACHE_SUBDIR_USERICONS,
                                                   (BOOL)app->settings.keepBigUserIcons);
                    if (req) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages,
                                                       statuses[idx].fmas_Acct);
                        else
                            FreeVec(req);
                    }
                }

                /* Trigger a thumbnail download for each attachment not
                 * already requested -- same pipeline as avatars above,
                 * just a different cache subdir/pool (see
                 * AvatarImages_IsMediaRequested and the file header
                 * comment in avatarimages.h). */
                if (app->avatarImages) {
                    ULONG mi;
                    for (mi = 0; mi < statuses[idx].fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                        const char *url = statuses[idx].fmas_MediaUrls[mi];
                        if (!url || !url[0]) continue;
                        /* Audio has no thumbnail to fetch -- TootTimeline
                         * draws a play button for it instead (see
                         * TTL_HOT_PLAY_AUDIO); its (fallback, no-preview)
                         * URL here is the actual media file, not a
                         * picture. */
                        if (statuses[idx].fmas_MediaKind[mi] == FS3ENET_MEDIAKIND_AUDIO) continue;
                        if (AvatarImages_IsMediaRequested(app->avatarImages, url)) continue;
                        {
                            ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                          + strlen(url) + 1
                                          + strlen(url) + 1
                                          + strlen(FS3E_CACHE_SUBDIR_THUMBNAILS) + 1;
                            FS3ENetFetchImageReq *req =
                                FS3ENetFetchImageReq_Alloc(url, url,
                                                           FS3E_CACHE_SUBDIR_THUMBNAILS,
                                                           (BOOL)app->settings.keepBigThumbnails);
                            if (req) {
                                if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                                    AvatarImages_MarkMediaRequested(app->avatarImages, url);
                                else
                                    FreeVec(req);
                            }
                        }
                    }
                }
                post.viewModeBits = (1UL << reply->fs3et_ViewModeBit);
                SetAttrs(app->tootTimeline, addAttr, (ULONG)&post, TAG_DONE);
            }

            /* Open a channel scrolled to its newest post, not wherever it
             * happened to be after AddHead's "scrollY stays fixed"
             * behavior -- only for the very first page, pagination must
             * never move the user's scroll position. */
            if (reply->fs3et_PageDirection == FS3ENETPAGE_INITIAL)
                SetAttrs(app->tootTimeline, TTIMELINE_ScrollToNewest, TRUE, TAG_DONE);

            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            FS3ENetTimelineReq *req = (FS3ENetTimelineReq *)msg->fs3em_Data;
            ULONG bit = req ? (1UL << req->fs3et_ViewModeBit) : 0;

            /* Same stale-generation guard as the success branch above --
             * a failure for an account we've since switched away from
             * must not touch the current account's in-flight/error masks
             * (see accountGeneration's comment in friendsh3ep.h). */
            if (req && req->fs3et_AccountGeneration != app->accountGeneration) {
                break;
            }

            if (req && req->fs3et_PageDirection == FS3ENETPAGE_OLDER) {
                app->olderPageInFlightMask &= ~bit; /* allow retry next time the user hits bottom */
            } else if (req && req->fs3et_PageDirection == FS3ENETPAGE_NEWER) {
                app->newerPageInFlightMask &= ~bit; /* allow retry on next click */
            } else {
                app->timelineErrorMask   |= bit;
                app->timelineFetchedMask &= ~bit; /* allow retry on next view switch */
                app->lastTimelineResult   = msg->fs3em_Result;
            }
        }
        break;

    case FS3ENETQ_NOTIFICATIONS:
        /* Mirrors FS3ENETQ_TIMELINE's reply handling closely (same
         * accountGeneration staleness guard, same older/AddPost-vs-
         * AppendPost dispatch, same avatar/thumbnail prefetch pipeline) --
         * see that case's comments for the reasoning, not repeated here.
         * Genuinely different: no fs3et_ViewModeBit to read (this queue
         * only ever targets VIEWMODE_Notifs, see FS3ENetNotificationsReq's
         * doc comment), and each entry is a Notification, not a bare
         * Status -- FOLLOW/FOLLOW_REQUEST carry no status at all
         * (fen_HasStatus FALSE), routed to TTLNotifFollow_Class purely by
         * TTLPostSetup.notifType (see fs3etoottimeline_attribs.c's
         * TTIMELINE_AddPost/AppendPost). */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetNotificationsReply *reply = (FS3ENetNotificationsReply *)msg->fs3em_Data;
            FS3ENetNotification *notifs = (FS3ENetNotification *)(reply + 1);
            BOOL  older = (reply->fs3en_PageDirection == FS3ENETPAGE_OLDER);
            ULONG addAttr = older ? TTIMELINE_AppendPost : TTIMELINE_AddPost;
            ULONG bit = (1UL << VIEWMODE_Notifs);
            ULONG i;

            if (reply->fs3en_AccountGeneration != app->accountGeneration) {
                break;
            }


            switch (reply->fs3en_PageDirection) {
                case FS3ENETPAGE_OLDER:
                    app->olderPageInFlightMask &= ~bit;
                    break;
                case FS3ENETPAGE_NEWER:
                    app->newerPageInFlightMask &= ~bit;
                    break;
                default:
                    /* See channelPopulatedMask's doc comment -- same fix
                     * as FS3ENETQ_TIMELINE's equivalent switch. */
                    app->timelineFetchedMask  &= ~bit;
                    app->channelPopulatedMask |=  bit;
                    break;
            }

            /* Same walk-order reasoning as FS3ENETQ_TIMELINE -- see its
             * comment just above its own equivalent loop. */
            for (i = 0; i < reply->fs3en_Count; i++) {
                ULONG idx = older ? i : (reply->fs3en_Count - 1 - i);
                FS3ENetNotification *n = &notifs[idx];
                TTLPostSetup post;
                memset(&post, 0, sizeof(post));

                switch (n->fen_Type) {
                    case FS3ENOTIF_MENTION:        post.notifType = TTL_NOTIF_MENTION;        break;
                    case FS3ENOTIF_REBLOG:         post.notifType = TTL_NOTIF_REBLOG;         break;
                    case FS3ENOTIF_FAVOURITE:      post.notifType = TTL_NOTIF_FAVOURITE;      break;
                    case FS3ENOTIF_FOLLOW:         post.notifType = TTL_NOTIF_FOLLOW;         break;
                    case FS3ENOTIF_FOLLOW_REQUEST: post.notifType = TTL_NOTIF_FOLLOW_REQUEST; break;
                    case FS3ENOTIF_POLL:           post.notifType = TTL_NOTIF_POLL;           break;
                    case FS3ENOTIF_UPDATE:         post.notifType = TTL_NOTIF_UPDATE;         break;
                    default:
                        /* Admin-only types (sign-up, report, severed
                         * relationships, moderation warning) and anything
                         * else this app doesn't specifically handle --
                         * skip rather than render a broken row (see
                         * FS3ENetNotifType's comment in fs3enet.h). */
                        continue;
                }
                /* The NOTIFICATION's own id, NOT the embedded status' --
                 * see TTLPostSetup.notifStatusId's comment on why this
                 * distinction matters for pagination. */
                post.postId = n->fen_Id;

                if (!n->fen_HasStatus) {
                    post.username  = n->fen_ActorDisplayName[0] ? n->fen_ActorDisplayName : n->fen_ActorAcct;
                    post.acct      = n->fen_ActorAcct;
                    post.avatarURL = n->fen_ActorAvatarURL;
                } else {
                    FS3ENetStatus *st = &n->fen_Status;
                    post.username = st->fmas_DisplayName[0] ? st->fmas_DisplayName : st->fmas_Acct;
                    post.acct      = st->fmas_Acct;
                    post.isOwn     = (st->fmas_Acct && app->accountAcct &&
                                       !strcmp(st->fmas_Acct, app->accountAcct));
                    post.body      = st->fmas_Content;
                    post.timestamp = st->fmas_CreatedAt;
                    post.avatarURL = st->fmas_AvatarURL;
                    post.repliesCount    = st->fmas_RepliesCount;
                    post.reblogsCount    = st->fmas_ReblogsCount;
                    post.favouritesCount = st->fmas_FavouritesCount;
                    post.favourited      = st->fmas_Favourited;
                    post.reblogged       = st->fmas_Reblogged;
                    post.notifActorName  = n->fen_ActorDisplayName;
                    post.notifActorAcct  = n->fen_ActorAcct;
                    post.notifStatusId   = st->fmas_Id;
                    {
                        ULONG mi;
                        for (mi = 0; mi < st->fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                            post.mediaUrls[mi] = st->fmas_MediaUrls[mi];
                            post.mediaIds[mi]  = st->fmas_MediaIds[mi];
                            switch (st->fmas_MediaKind[mi]) {
                                case FS3ENET_MEDIAKIND_IMAGE: post.mediaKinds[mi] = TTL_MEDIA_KIND_IMAGE; break;
                                case FS3ENET_MEDIAKIND_VIDEO: post.mediaKinds[mi] = TTL_MEDIA_KIND_VIDEO; break;
                                case FS3ENET_MEDIAKIND_GIFV:  post.mediaKinds[mi] = TTL_MEDIA_KIND_GIFV;  break;
                                case FS3ENET_MEDIAKIND_AUDIO: post.mediaKinds[mi] = TTL_MEDIA_KIND_AUDIO; break;
                                default:                       post.mediaKinds[mi] = TTL_MEDIA_KIND_UNKNOWN; break;
                            }
                        }
                        post.mediaCount = st->fmas_MediaCount;
                    }
                    {
                        ULONG oi;
                        for (oi = 0; oi < st->fmas_PollOptionCount && oi < TTL_POST_MAX_POLL_OPTIONS; oi++) {
                            post.pollOptionTitles[oi] = st->fmas_PollOptionTitles[oi];
                            post.pollOptionVotes[oi]  = st->fmas_PollOptionVotes[oi];
                        }
                        post.pollOptionCount = st->fmas_PollOptionCount;
                        post.pollVotesCount  = st->fmas_PollVotesCount;
                        post.pollExpired     = st->fmas_PollExpired;
                        post.pollMultiple    = st->fmas_PollMultiple;
                    }

                    /* Media thumbnail prefetch -- same pipeline as
                     * FS3ENETQ_TIMELINE's. */
                    if (app->avatarImages) {
                        ULONG mi;
                        for (mi = 0; mi < st->fmas_MediaCount && mi < TTL_POST_MAX_MEDIA; mi++) {
                            const char *url = st->fmas_MediaUrls[mi];
                            if (!url || !url[0]) continue;
                            if (st->fmas_MediaKind[mi] == FS3ENET_MEDIAKIND_AUDIO) continue;
                            if (AvatarImages_IsMediaRequested(app->avatarImages, url)) continue;
                            {
                                ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                              + strlen(url) + 1 + strlen(url) + 1
                                              + strlen(FS3E_CACHE_SUBDIR_THUMBNAILS) + 1;
                                FS3ENetFetchImageReq *req =
                                    FS3ENetFetchImageReq_Alloc(url, url,
                                                               FS3E_CACHE_SUBDIR_THUMBNAILS,
                                                               (BOOL)app->settings.keepBigThumbnails);
                                if (req) {
                                    if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                                        AvatarImages_MarkMediaRequested(app->avatarImages, url);
                                    else
                                        FreeVec(req);
                                }
                            }
                        }
                    }
                }

                /* Avatar download prefetch, keyed by post.acct regardless
                 * of which branch set it (toot author for status-bearing
                 * rows, the actor themselves for follow rows) -- same
                 * pipeline as FS3ENETQ_TIMELINE's. */
                if (app->avatarImages && post.avatarURL && post.avatarURL[0] &&
                    post.acct && post.acct[0] &&
                    !AvatarImages_IsRequested(app->avatarImages, post.acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(post.avatarURL) + 1
                                  + strlen(post.acct) + 1
                                  + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
                    FS3ENetFetchImageReq *req =
                        FS3ENetFetchImageReq_Alloc(post.avatarURL, post.acct,
                                                   FS3E_CACHE_SUBDIR_USERICONS,
                                                   (BOOL)app->settings.keepBigUserIcons);
                    if (req) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages, post.acct);
                        else
                            FreeVec(req);
                    }
                }

                post.viewModeBits = bit;
                SetAttrs(app->tootTimeline, addAttr, (ULONG)&post, TAG_DONE);
            }

            if (reply->fs3en_PageDirection == FS3ENETPAGE_INITIAL)
                SetAttrs(app->tootTimeline, TTIMELINE_ScrollToNewest, TRUE, TAG_DONE);

            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
        } else if (msg->fs3em_Result != FS3ENETR_OK) {
            FS3ENetNotificationsReq *req = (FS3ENetNotificationsReq *)msg->fs3em_Data;
            ULONG bit = (1UL << VIEWMODE_Notifs);

            if (req && req->fs3en_AccountGeneration != app->accountGeneration) {
                break;
            }

            if (req && req->fs3en_PageDirection == FS3ENETPAGE_OLDER) {
                app->olderPageInFlightMask &= ~bit;
            } else if (req && req->fs3en_PageDirection == FS3ENETPAGE_NEWER) {
                app->newerPageInFlightMask &= ~bit;
            } else {
                app->timelineErrorMask   |= bit;
                app->timelineFetchedMask &= ~bit;
                app->lastTimelineResult   = msg->fs3em_Result;
            }
        }
        break;

    case FS3ENETQ_FETCH_IMAGE:
        if (msg->fs3em_Result == FS3ENETR_OK && app->avatarImages) {
            FS3ENetFetchImageReply *reply = (FS3ENetFetchImageReply *)msg->fs3em_Data;
            BOOL isMedia = reply && reply->fs3enf_Subdir &&
                           strcmp(reply->fs3enf_Subdir, FS3E_CACHE_SUBDIR_THUMBNAILS) == 0;

            if (reply && reply->fs3enf_Key && reply->fs3enf_LocalPath &&
                app->thumbRequestPort && app->thumbReplyPort)
            {
                /* Hand the (possibly large, original-size) downloaded file
                 * to the thumbnail process instead of decoding/scaling it
                 * here -- see fs3ethumb.h. Its reply lands in
                 * FS3EApp_HandleThumbReply(), which uses fs3etm_Kind to
                 * tell the two apart again. fs3enf_CachePath/IsTemp (see
                 * their doc comments in fs3enet.h) make sure the resized
                 * thumbnail always lands under a name stable across runs
                 * and that a RAM:T download gets cleaned up afterwards,
                 * regardless of whether the original itself was kept. */
                if (isMedia) {
                    if (!AvatarImages_IsMediaThumbRequested(app->avatarImages, reply->fs3enf_Key) &&
                        FS3EThumb_Request(app->thumbRequestPort, app->thumbReplyPort,
                            reply->fs3enf_LocalPath, reply->fs3enf_Key, FS3ETHUMB_KIND_MEDIA,
                            reply->fs3enf_CachePath, reply->fs3enf_IsTemp,
                            FS3ETHUMB_MEDIA_WIDTH, FS3ETHUMB_MEDIA_HEIGHT_CAP))
                        AvatarImages_MarkMediaThumbRequested(app->avatarImages, reply->fs3enf_Key);
                } else {
                    if (!AvatarImages_IsThumbRequested(app->avatarImages, reply->fs3enf_Key) &&
                        FS3EThumb_Request(app->thumbRequestPort, app->thumbReplyPort,
                            reply->fs3enf_LocalPath, reply->fs3enf_Key, FS3ETHUMB_KIND_AVATAR,
                            reply->fs3enf_CachePath, reply->fs3enf_IsTemp,
                            FS3ETHUMB_AVATAR_SIZE, FS3ETHUMB_AVATAR_SIZE))
                        AvatarImages_MarkThumbRequested(app->avatarImages, reply->fs3enf_Key);
                }
            }
        }

        /* Same reply, independent consumer: FS3EMediaView_ShowUrl() may be
         * waiting on this exact URL (see fs3emediaview.h) -- ignores it if
         * not. No-op on failure beyond clearing its own pending/loading
         * state (msg->fs3em_Data on failure is the original request block,
         * not a reply -- fs3enf_Key sits at the same offset in both, see
         * FS3ENetFetchImageReq/Reply in fs3enet.h, so this is still safe). */
        FS3EMediaView_OnFetchReply(&app->mediaView, msg->fs3em_Result,
                                    (const FS3ENetFetchImageReply *)msg->fs3em_Data);
        break;

    case FS3ENETQ_POST_STATUS:
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ETootView_Close(&app->tootView);
        }
        break;

    case FS3ENETQ_EDIT_STATUS:
        /* Mirrors POST_STATUS's reply handling exactly -- deliberately not
         * patching the edited text into the live timeline in place (see
         * fs3etoottimeline.h's TTLPostUpdate, which only carries
         * favourited/reblogged today, no body/content field); the edited
         * text will just show correctly whenever the timeline next
         * naturally refreshes, same as a freshly-POSTed toot already
         * behaves (no local insert either). */
        if (msg->fs3em_Result == FS3ENETR_OK) {
            FS3ETootView_Close(&app->tootView);
        }
        break;

    case FS3ENETQ_DELETE_STATUS:
        /* Unlike EDIT_STATUS, a delete's effect on the timeline IS worth
         * reflecting immediately -- the toot is simply gone, no relayout-
         * height-cascade risk the way patching edited body text in place
         * would have (see EDIT_STATUS's comment above): TTIMELINE_RemovePost
         * unlinks+frees the post and rebuilds Y positions itself. Same
         * "may have already scrolled out of every channel" race as
         * FS3ENETQ_FAVORITE -- silently a no-op then. */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetDeleteStatusReply *reply = (FS3ENetDeleteStatusReply *)msg->fs3em_Data;
            if (reply && reply->fs3ed_StatusId) {
                SetAttrs(app->tootTimeline, TTIMELINE_RemovePost,
                         (ULONG)reply->fs3ed_StatusId, TAG_DONE);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        }
        break;

    case FS3ENETQ_FAVORITE:
        /* The toot may have scrolled out of every channel (evicted, or the
         * user cleared/switched away) by the time this reply lands --
         * TTIMELINE_UpdatePost is a silent no-op in that case, same as any
         * other async reply racing a list change. */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline) {
            FS3ENetFavouriteReply *reply = (FS3ENetFavouriteReply *)msg->fs3em_Data;
            TTLPostUpdate upd;
            memset(&upd, 0, sizeof(upd));
            upd.postId     = reply->fs3efa_StatusId;
            upd.flags      = TTL_POSTUPD_FAVOURITED;
            upd.favourited = reply->fs3efa_Favourited;
            SetAttrs(app->tootTimeline, TTIMELINE_UpdatePost, (ULONG)&upd, TAG_DONE);
            if (CurrentMainWindow)
                RefreshGList((struct Gadget *)app->tootTimeline,
                             CurrentMainWindow, NULL, 1);
        }
        break;

    case FS3ENETQ_ACCOUNT_LOOKUP:
        /* Stale-reply guard: the user may have clicked a second
         * avatar/mention before this lookup came back for the first
         * one -- only apply it if it's still the profile we last asked
         * for. */
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline &&
            app->searchProfileAcct)
        {
            FS3ENetAccountLookupReply *reply = (FS3ENetAccountLookupReply *)msg->fs3em_Data;
            FS3EMastodonAccount *acc = &reply->fs3eal_Account;

            if (strcmp(app->searchProfileAcct, acc->fma_Acct) == 0) {
                TTLProfileHeaderSetup setup;
                BOOL isSelf = (app->accountId && acc->fma_Id[0] &&
                               strcmp(app->accountId, acc->fma_Id) == 0);

                if (app->searchProfileAccountId) FreeVec(app->searchProfileAccountId);
                app->searchProfileAccountId = NetStrDup(acc->fma_Id);

                memset(&setup, 0, sizeof(setup));
                setup.accountId      = acc->fma_Id;
                setup.username       = acc->fma_DisplayName[0] ? acc->fma_DisplayName : acc->fma_Acct;
                setup.acct           = acc->fma_Acct;
                setup.avatarURL      = acc->fma_AvatarURL;
                setup.bio            = acc->fma_Note;
                setup.followersCount = acc->fma_FollowersCount;
                setup.followingCount = acc->fma_FollowingCount;
                setup.following      = FALSE; /* unknown until the FS3ENETQ_RELATIONSHIP reply */
                setup.showFollow     = !isSelf;

                SetAttrs(app->tootTimeline, TTIMELINE_ShowProfile, (ULONG)&setup, TAG_DONE);

                /* Avatar fetch -- same cache/pipeline as a toot's own
                 * avatar, mirrors FS3EApp_SetAccount's own-avatar fetch
                 * block; a transparent cache hit if this acct's avatar
                 * was already fetched while scrolling a timeline. */
                if (app->avatarImages && acc->fma_Acct[0] &&
                    acc->fma_AvatarURL[0] &&
                    !AvatarImages_IsRequested(app->avatarImages, acc->fma_Acct))
                {
                    ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                                  + strlen(acc->fma_AvatarURL) + 1
                                  + strlen(acc->fma_Acct) + 1
                                  + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
                    FS3ENetFetchImageReq *imgReq =
                        FS3ENetFetchImageReq_Alloc(acc->fma_AvatarURL, acc->fma_Acct,
                                                   FS3E_CACHE_SUBDIR_USERICONS,
                                                   (BOOL)app->settings.keepBigUserIcons);
                    if (imgReq) {
                        if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, imgReq, reqSize))
                            AvatarImages_MarkRequested(app->avatarImages, acc->fma_Acct);
                        else
                            FreeVec(imgReq);
                    }
                }

                /* Relationship (skipped for a self-profile -- you can't
                 * follow yourself) + the profile's first toot page,
                 * always requested as an "older" page (even though it's
                 * the first one) so it lands via TTIMELINE_AppendPost
                 * below the header instead of the AddPost/prepend path
                 * FS3ENETPAGE_INITIAL would take -- see
                 * TTIMELINE_ShowProfile's comment in fs3etoottimeline.h. */
                if (!isSelf && app->accountAccessToken && app->accountAccessToken[0]) {
                    FS3ENetRelationshipReq *relReq = FS3ENetRelationshipReq_Alloc(
                        app->accountApiBaseUrl, app->accountAccessToken,
                        app->searchProfileAccountId);
                    if (relReq)
                        FS3EApp_NetSend(FS3ENETQ_RELATIONSHIP, relReq, sizeof(*relReq));
                }
                {
                    char tl[128];
                    if (ViewModeTimeline(VIEWMODE_Search, tl, sizeof(tl))) {
                        FS3ENetTimelineReq *tlReq = FS3ENetTimelineReq_Alloc(
                            VIEWMODE_Search, FS3ENETPAGE_OLDER,
                            app->accountGeneration, FS3ENET_TLSHAPE_ARRAY,
                            app->accountApiBaseUrl,
                            app->accountAccessToken ? app->accountAccessToken : "",
                            tl, "", "", NULL);
                        if (tlReq)
                            FS3EApp_NetSend(FS3ENETQ_TIMELINE, tlReq, sizeof(FS3ENetTimelineReq));
                    }
                }

                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        }
        break;

    case FS3ENETQ_RELATIONSHIP:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline &&
            app->searchProfileAccountId)
        {
            FS3ENetRelationshipReply *reply = (FS3ENetRelationshipReply *)msg->fs3em_Data;
            if (strcmp(app->searchProfileAccountId, reply->fs3erl_AccountId) == 0) {
                TTLProfileFollowUpdate upd;
                upd.accountId = reply->fs3erl_AccountId;
                upd.following = reply->fs3erl_Following;
                SetAttrs(app->tootTimeline, TTIMELINE_UpdateProfileFollow, (ULONG)&upd, TAG_DONE);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        }
        break;

    case FS3ENETQ_FOLLOW:
        if (msg->fs3em_Result == FS3ENETR_OK && app->tootTimeline &&
            app->searchProfileAccountId)
        {
            FS3ENetFollowReply *reply = (FS3ENetFollowReply *)msg->fs3em_Data;
            if (strcmp(app->searchProfileAccountId, reply->fs3efo_AccountId) == 0) {
                TTLProfileFollowUpdate upd;
                upd.accountId = reply->fs3efo_AccountId;
                upd.following = reply->fs3efo_Following;
                SetAttrs(app->tootTimeline, TTIMELINE_UpdateProfileFollow, (ULONG)&upd, TAG_DONE);
                if (CurrentMainWindow)
                    RefreshGList((struct Gadget *)app->tootTimeline,
                                 CurrentMainWindow, NULL, 1);
            }
        }
        break;

    default:
        break;
    }

    FS3EApp_CheckConnectionState();
}

/* FS3ETHUMBQ_MAKE reply -- the thumbnail process has finished decoding and
 * box-fit-scaling one avatar's or media attachment's original download
 * down to a small BMP (see fs3ethumb.h); fs3etm_Kind says which. All that
 * remains on the GUI task is a cheap direct read of that already-small
 * file's pixels; scaling to the live display size happens at draw time
 * (RgbImage_DrawScaled), not here. */
void FS3EApp_HandleThumbReply(FS3EThumbMessage *msg)
{
    if (msg->fs3etm_Result == FS3ETHUMBR_OK && app->avatarImages &&
        msg->fs3etm_Key[0] && msg->fs3etm_ThumbPath[0])
    {
        if (msg->fs3etm_Kind == FS3ETHUMB_KIND_MEDIA) {
            AvatarImages_MediaThumbReady(app->avatarImages, msg->fs3etm_Key,
                                          msg->fs3etm_ThumbPath);
        } else {
            AvatarImages_ThumbReady(app->avatarImages, msg->fs3etm_Key,
                                     msg->fs3etm_ThumbPath);
            if (app->accountAcct && strcmp(msg->fs3etm_Key, app->accountAcct) == 0)
                FS3EApp_UpdateUserIcon();
        }
        if (app->tootTimeline) {
            /* This image just became available in the cache -- one-shot
             * event, not a poll: tell the timeline to invalidate its
             * currently rendered tiles so whichever of them drew a
             * placeholder for this avatar/thumbnail get redrawn with the
             * real image on the next render (see TTIMELINE_InvalidateImages). */
            SetAttrs(app->tootTimeline, TTIMELINE_InvalidateImages, TRUE, TAG_DONE);
        }
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->tootTimeline,
                         CurrentMainWindow, NULL, 1);
    }
    else if (msg->fs3etm_Result == FS3ETHUMBR_ERROR && app->avatarImages &&
             msg->fs3etm_Key[0])
    {
        /* Previously silently dropped: a failed decode left the entry's
         * .requested flag latched forever with no distinction from
         * "still pending", so AvatarImages_Get(Media)() returned NULL
         * forever and the placeholder redrew with nothing to explain why.
         * Latch it explicitly instead, with the sniffed format (see
         * FS3EThumb_HandleMake/BmImage_SniffFormat) so the tile renderer
         * can show e.g. "webp" instead of a bare box. */
        UBYTE fmt = (UBYTE)msg->fs3etm_DetectedFormat;
        if (msg->fs3etm_Kind == FS3ETHUMB_KIND_MEDIA)
            AvatarImages_MarkMediaFailed(app->avatarImages, msg->fs3etm_Key, fmt);
        else
            AvatarImages_MarkFailed(app->avatarImages, msg->fs3etm_Key, fmt);

        if (app->tootTimeline)
            SetAttrs(app->tootTimeline, TTIMELINE_InvalidateImages, TRUE, TAG_DONE);
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->tootTimeline,
                         CurrentMainWindow, NULL, 1);
    }
}

