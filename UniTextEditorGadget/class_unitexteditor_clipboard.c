/*
 * class_unitexteditor_clipboard.c – Amiga clipboard.device I/O for UniTextEditor
 *
 * Implements UTED_ApplyCopy and UTED_ApplyPaste:
 *   uted_do_clipboard_copy()  – gets selected text and writes it to the
 *                               system clipboard as IFF FTXT/CHRS (UTF-8).
 *   uted_do_clipboard_paste() – reads the CHRS chunk from the clipboard and
 *                               inserts it at the cursor via DoInsertText.
 *
 * Both functions are called from UniTextEditor_OnSet in attribs.c when the
 * corresponding attribute tag is processed.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <string.h>

#include "unitexteditor_private.h"

/* =========================================================================
 * Internal clipboard device helpers
 * =========================================================================
 */

/* Write a byte buffer to the clipboard as IFF FTXT/CHRS.
 * The bytes are stored verbatim — callers choose the encoding (UTF-8 here).
 * Mirrors the implementation in egaction.c; lives here so the gadget is
 * self-contained for standard UTF-8 copy/paste. */
static BOOL clipboard_write(const char *text, ULONG len)
{
    struct MsgPort   *port;
    struct IOClipReq  ior;
    UBYTE            *buf;
    ULONG             padLen, formSize, bufSize;
    BOOL              ok = FALSE;

    if (!text || len == 0) return FALSE;

    padLen   = (len + 1) & ~1UL;
    formSize = 4 + 8 + padLen;          /* "FTXT" + "CHRS"+size + data */
    bufSize  = 8 + formSize;            /* "FORM"+size + body           */

    buf = (UBYTE *)AllocVec(bufSize, MEMF_ANY | MEMF_CLEAR);
    if (!buf) return FALSE;

    buf[0]='F'; buf[1]='O'; buf[2]='R'; buf[3]='M';
    buf[4]=(UBYTE)(formSize>>24); buf[5]=(UBYTE)(formSize>>16);
    buf[6]=(UBYTE)(formSize>>8);  buf[7]=(UBYTE)(formSize);
    buf[8]='F'; buf[9]='T'; buf[10]='X'; buf[11]='T';
    buf[12]='C'; buf[13]='H'; buf[14]='R'; buf[15]='S';
    buf[16]=(UBYTE)(len>>24); buf[17]=(UBYTE)(len>>16);
    buf[18]=(UBYTE)(len>>8);  buf[19]=(UBYTE)(len);
    CopyMem((APTR)(ULONG)text, &buf[20], len);

    port = CreateMsgPort();
    if (!port) { FreeVec(buf); return FALSE; }

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0) == 0) {
        ior.io_Command = CMD_WRITE;
        ior.io_ClipID  = 0;
        ior.io_Offset  = 0;
        ior.io_Data    = (STRPTR)buf;
        ior.io_Length  = bufSize;
        DoIO((struct IORequest *)&ior);

        ior.io_Command = CBD_POST;
        DoIO((struct IORequest *)&ior);

        ok = (ior.io_Error == 0);
        CloseDevice((struct IORequest *)&ior);
    }

    DeleteMsgPort(port);
    FreeVec(buf);
    return ok;
}

/* Read raw bytes from the clipboard CHRS chunk.
 * Returns an AllocVec'd NUL-terminated buffer, or NULL on empty/error.
 * Caller must FreeVec the result. */
static char *clipboard_read(void)
{
    struct MsgPort   *port;
    struct IOClipReq  ior;
    UBYTE             hdr[8];
    UBYTE            *body   = NULL;
    char             *result = NULL;
    ULONG             formSize;

    port = CreateMsgPort();
    if (!port) return NULL;

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0)) {
        DeleteMsgPort(port);
        return NULL;
    }

    ior.io_Command = CMD_READ;
    ior.io_Offset  = 0;
    ior.io_Data    = (STRPTR)hdr;
    ior.io_Length  = 8;
    DoIO((struct IORequest *)&ior);

    if (ior.io_Actual < 8)                                     goto done;
    if (hdr[0]!='F'||hdr[1]!='O'||hdr[2]!='R'||hdr[3]!='M') goto done;

    formSize = ((ULONG)hdr[4]<<24)|((ULONG)hdr[5]<<16)|
               ((ULONG)hdr[6]<<8) |(ULONG)hdr[7];

    if (formSize < 4 || formSize > 4UL*1024UL*1024UL) goto done;

    body = (UBYTE *)AllocVec(formSize, MEMF_ANY);
    if (!body) goto done;

    ior.io_Command = CMD_READ;
    ior.io_Offset  = 8;
    ior.io_Data    = (STRPTR)body;
    ior.io_Length  = formSize;
    DoIO((struct IORequest *)&ior);

    if (ior.io_Actual < 4)                                       goto done;
    if (body[0]!='F'||body[1]!='T'||body[2]!='X'||body[3]!='T') goto done;

    {
        ULONG pos    = 4;
        ULONG actual = ior.io_Actual;

        while (pos + 8 <= actual) {
            ULONG cid = ((ULONG)body[pos  ]<<24)|((ULONG)body[pos+1]<<16)|
                        ((ULONG)body[pos+2]<< 8)|(ULONG)body[pos+3];
            ULONG csz = ((ULONG)body[pos+4]<<24)|((ULONG)body[pos+5]<<16)|
                        ((ULONG)body[pos+6]<< 8)|(ULONG)body[pos+7];
            pos += 8;

            if (cid == 0x43485253UL) { /* 'CHRS' */
                ULONG avail = (actual > pos) ? (actual - pos) : 0;
                ULONG len   = (csz < avail) ? csz : avail;
                if (len > 0) {
                    result = (char *)AllocVec(len + 1, MEMF_ANY);
                    if (result) { CopyMem(&body[pos], result, len); result[len]='\0'; }
                }
                break;
            }
            pos += (csz + 1) & ~1UL;
        }
    }

done:
    if (body) FreeVec(body);
    CloseDevice((struct IORequest *)&ior);
    DeleteMsgPort(port);
    return result;
}

/* =========================================================================
 * Public action functions – called from UniTextEditor_OnSet
 * =========================================================================
 */

/* UTED_ApplyCopy: copy the current selection to the system clipboard (UTF-8).
 * No-op if nothing is selected. */
void uted_do_clipboard_copy(Class *cl, Object *o)
{
    char *sel = NULL;
    UniTextEditor_DoGetSelectedText(cl, o, (STRPTR *)&sel);
    if (sel) {
        clipboard_write(sel, (ULONG)strlen(sel));
        FreeVec(sel);
    }
}

/* UTED_ApplyCut: copy selection to clipboard then delete it.
 * No-op if nothing is selected.
 * Returns TRUE if text was cut (caller should trigger redraw). */
BOOL uted_do_clipboard_cut(Class *cl, Object *o)
{
    char *sel = NULL;
    UniTextEditor_DoGetSelectedText(cl, o, (STRPTR *)&sel);
    if (!sel) return FALSE;
    clipboard_write(sel, (ULONG)strlen(sel));
    FreeVec(sel);
    UniTextEditor_DoDeleteSelection(cl, o);
    return TRUE;
}

/* UTED_ApplyPaste: read the system clipboard and insert at the cursor.
 * Returns TRUE if text was inserted (caller should trigger redraw). */
BOOL uted_do_clipboard_paste(Class *cl, Object *o)
{
    char *text = clipboard_read();
    if (!text) return FALSE;
    UniTextEditor_DoInsertText(cl, o, text, -1);
    FreeVec(text);
    return TRUE;
}
