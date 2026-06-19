#ifndef FS3ENET_H
#define FS3ENET_H

/*
 * FriendSh3ep network process - public API.
 *
 * This static library is linked into the GUI executable, but (besides
 * FS3ENet_Start/FS3ENet_Stop, which run on the caller's task) all of its work
 * happens in a separate AmigaDOS process created with CreateNewProc().
 * The GUI and the network process talk only through FS3ENetMessage exchanged
 * over Exec MsgPorts - no shared GUI state, and this library never includes
 * Intuition/BOOPSI/utf8rastport headers.
 *
 * See ../ARCHITECTURE.md for the full design and roadmap.
 */

#include <exec/ports.h>
#include <exec/types.h>

#include "fs3enet_mastodon.h"

/* Request types, sent by the GUI to the network process' request port. */
enum FS3ENetRequestType
{
    FS3ENETQ_SHUTDOWN = 0,   /* ask the network process to exit */
    FS3ENETQ_LOGIN_START,    /* register app, return authorize URL  (Phase 1) */
    FS3ENETQ_LOGIN_FINISH,   /* exchange oauth code for access token (Phase 1) */
    FS3ENETQ_TIMELINE,       /* fetch a timeline page                (Phase 2) */
    FS3ENETQ_POST_STATUS,    /* publish a new status (toot)          (Phase 2) */
    FS3ENETQ_FETCH_IMAGE     /* fetch/return cached avatar or media   (Phase 2) */
};

/* Result codes returned in FS3ENetMessage.fs3em_Result on reply. */
enum FS3ENetResult
{
    FS3ENETR_OK = 0,
    FS3ENETR_NETWORK_ERROR,
    FS3ENETR_HTTP_ERROR,
    FS3ENETR_AUTH_ERROR,
    FS3ENETR_PARSE_ERROR
};

/*
 * Generic request/reply envelope.
 *
 * fs3em_Msg.mn_ReplyPort is set by the caller before PutMsg(); the network
 * process PutMsg()s the same structure back to that port once done.
 *
 * fs3em_Data/fs3em_DataLen describe a request- or reply-specific payload
 * allocated with AllocVec() by whichever side produces it. Ownership passes
 * to the receiver, which must FreeVec() it.
 */
typedef struct FS3ENetMessage
{
    struct Message fs3em_Msg;
    ULONG           fs3em_Type;    /* enum FS3ENetRequestType */
    ULONG           fs3em_Result;  /* enum FS3ENetResult, set on reply */
    APTR            fs3em_Data;
    ULONG           fs3em_DataLen;
} FS3ENetMessage;

/*
 * FS3ENETQ_LOGIN_START - fs3em_Data points at an FS3ENetLoginStartReq
 * (fs3em_DataLen = sizeof(FS3ENetLoginStartReq)). Registers FriendSh3ep as an
 * OAuth app on fs3enl_ApiBaseUrl (see brutaldon's Mastodon.create_app and
 * ARCHITECTURE.md section 4.4).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetLoginStartReply; the GUI
 * must show fs3enl_AuthorizeUrl to the user (e.g. via unitexteditor, so it
 * can be copied to a browser) and keep fs3enl_ClientId/fs3enl_ClientSecret to
 * pass back in FS3ENETQ_LOGIN_FINISH.
 */
typedef struct FS3ENetLoginStartReq
{
    char fs3enl_ApiBaseUrl[256];
} FS3ENetLoginStartReq;

typedef struct FS3ENetLoginStartReply
{
    char fs3enl_ClientId[64];
    char fs3enl_ClientSecret[64];
    char fs3enl_AuthorizeUrl[512];
} FS3ENetLoginStartReply;

/*
 * FS3ENETQ_LOGIN_FINISH - fs3em_Data points at an FS3ENetLoginFinishReq
 * (fs3em_DataLen = sizeof(FS3ENetLoginFinishReq)), carrying the code the user
 * pasted back from the authorize URL plus the client_id/secret returned by
 * FS3ENETQ_LOGIN_START. Exchanges the code for an access token and verifies
 * it (brutaldon's mastodon.log_in + verify_credentials).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetLoginFinishReply; the GUI
 * must persist fs3enl_AccessToken (and fs3enl_ApiBaseUrl) to use with later
 * FS3ENETQ_TIMELINE/FS3ENETQ_POST_STATUS requests.
 */
typedef struct FS3ENetLoginFinishReq
{
    char fs3enl_ApiBaseUrl[256];
    char fs3enl_ClientId[64];
    char fs3enl_ClientSecret[64];
    char fs3enl_Code[256];
} FS3ENetLoginFinishReq;

typedef struct FS3ENetLoginFinishReply
{
    char              fs3enl_AccessToken[128];
    FS3EMastodonAccount fs3enl_Account;
} FS3ENetLoginFinishReply;

/*
 * Start the network process. Returns its request MsgPort, or NULL on
 * failure. Safe to call once at GUI startup.
 */
struct MsgPort *FS3ENet_Start(void);

/*
 * Ask the network process to shut down and wait for it to exit.
 * requestPort is the port returned by FS3ENet_Start(); replyPort is a
 * temporary port created by the caller to receive the shutdown reply.
 */
void FS3ENet_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort);

#endif /* FS3ENET_H */
