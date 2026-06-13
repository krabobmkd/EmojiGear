#ifndef FSNET_H
#define FSNET_H

/*
 * FriendSheeep network process - public API.
 *
 * This static library is linked into the GUI executable, but (besides
 * FSNet_Start/FSNet_Stop, which run on the caller's task) all of its work
 * happens in a separate AmigaDOS process created with CreateNewProc().
 * The GUI and the network process talk only through FSNetMessage exchanged
 * over Exec MsgPorts - no shared GUI state, and this library never includes
 * Intuition/BOOPSI/utf8rastport headers.
 *
 * See ../ARCHITECTURE.md for the full design and roadmap.
 */

#include <exec/ports.h>
#include <exec/types.h>

/* Request types, sent by the GUI to the network process' request port. */
enum FSNetRequestType
{
    FSNETQ_SHUTDOWN = 0,   /* ask the network process to exit */
    FSNETQ_LOGIN_START,    /* register app, return authorize URL  (Phase 1) */
    FSNETQ_LOGIN_FINISH,   /* exchange oauth code for access token (Phase 1) */
    FSNETQ_TIMELINE,       /* fetch a timeline page                (Phase 2) */
    FSNETQ_POST_STATUS,    /* publish a new status (toot)          (Phase 2) */
    FSNETQ_FETCH_IMAGE     /* fetch/return cached avatar or media   (Phase 2) */
};

/* Result codes returned in FSNetMessage.fsm_Result on reply. */
enum FSNetResult
{
    FSNETR_OK = 0,
    FSNETR_NETWORK_ERROR,
    FSNETR_HTTP_ERROR,
    FSNETR_AUTH_ERROR,
    FSNETR_PARSE_ERROR
};

/*
 * Generic request/reply envelope.
 *
 * fsm_Msg.mn_ReplyPort is set by the caller before PutMsg(); the network
 * process PutMsg()s the same structure back to that port once done.
 *
 * fsm_Data/fsm_DataLen describe a request- or reply-specific payload
 * allocated with AllocVec() by whichever side produces it. Ownership passes
 * to the receiver, which must FreeVec() it.
 */
typedef struct FSNetMessage
{
    struct Message fsm_Msg;
    ULONG           fsm_Type;    /* enum FSNetRequestType */
    ULONG           fsm_Result;  /* enum FSNetResult, set on reply */
    APTR            fsm_Data;
    ULONG           fsm_DataLen;
} FSNetMessage;

/*
 * Start the network process. Returns its request MsgPort, or NULL on
 * failure. Safe to call once at GUI startup.
 */
struct MsgPort *FSNet_Start(void);

/*
 * Ask the network process to shut down and wait for it to exit.
 * requestPort is the port returned by FSNet_Start(); replyPort is a
 * temporary port created by the caller to receive the shutdown reply.
 */
void FSNet_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort);

#endif /* FSNET_H */
