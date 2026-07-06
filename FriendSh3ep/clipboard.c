/*
 * clipboard.c - Amiga IFF FTXT clipboard write for FriendSh3ep.
 *
 * Writes text as an IFF FTXT / CHRS chunk to clipboard unit 0.
 * Adapted from EmojiGear/egaction.c (clipboard_write / clip_write4).
 */

#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <proto/exec.h>

#include <devices/clipboard.h>

#include "clipboard.h"

static BOOL clip_write4(struct IOClipReq *ior, const void *data)
{
    ior->io_Data    = (STRPTR)data;
    ior->io_Length  = 4;
    ior->io_Command = CMD_WRITE;
    DoIO((struct IORequest *)ior);
    return (ior->io_Actual == 4 && !ior->io_Error);
}

BOOL Clipboard_WriteText(const char *text)
{
    struct MsgPort  *port;
    struct IOClipReq ior;
    ULONG            len, formBodyLen;
    BOOL             odd;
    const UBYTE      zero = 0;

    if (!text || !text[0]) return FALSE;

    len  = (ULONG)strlen(text);
    port = CreateMsgPort();
    if (!port) return FALSE;

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0) != 0) {
        DeleteMsgPort(port);
        return FALSE;
    }

    odd         = (len & 1);
    /* FORM body = "FTXT"(4) + "CHRS"(4) + chunk-size(4) + data (+ optional pad) */
    formBodyLen = 12 + (odd ? len + 1 : len);

    ior.io_Offset = 0;
    ior.io_Error  = 0;
    ior.io_ClipID = 0;

    clip_write4(&ior, "FORM");
    clip_write4(&ior, &formBodyLen);
    clip_write4(&ior, "FTXT");
    clip_write4(&ior, "CHRS");
    clip_write4(&ior, &len);

    ior.io_Data    = (STRPTR)text;
    ior.io_Length  = len;
    ior.io_Command = CMD_WRITE;
    DoIO((struct IORequest *)&ior);

    if (odd) {
        ior.io_Data    = (STRPTR)&zero;
        ior.io_Length  = 1;
        ior.io_Command = CMD_WRITE;
        DoIO((struct IORequest *)&ior);
    }

    ior.io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)&ior);

    CloseDevice((struct IORequest *)&ior);
    DeleteMsgPort(port);
    return (ior.io_Error == 0);
}
