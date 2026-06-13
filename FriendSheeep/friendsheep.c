/*
 * FriendSheeep - a Mastodon client for AmigaOS.
 *
 * See ARCHITECTURE.md for the overall design and roadmap. This file
 * currently only proves the GUI <-> network process IPC handshake; the
 * BOOPSI window and gadgets land in later phases.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "network/fsnet.h"

int main(void)
{
    struct MsgPort *netRequestPort;
    struct MsgPort *netReplyPort;

    netRequestPort = FSNet_Start();
    if (!netRequestPort)
    {
        Printf("FriendSheeep: failed to start network process\n");
        return RETURN_FAIL;
    }

    Printf("FriendSheeep: network process started\n");

    /* TODO: open the BOOPSI main window and run the Intuition event loop,
     * dispatching FSNetMessage replies on a GUI-owned reply port together
     * with the window's signal (Phase 3+). */

    netReplyPort = CreateMsgPort();
    if (netReplyPort)
    {
        FSNet_Stop(netRequestPort, netReplyPort);
        DeleteMsgPort(netReplyPort);
    }

    return RETURN_OK;
}
