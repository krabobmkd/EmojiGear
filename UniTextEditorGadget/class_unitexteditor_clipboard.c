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
 *
 * Write protocol (matches RKM cbio.c):
 *   - incremental CMD_WRITE calls, one per IFF field
 *   - CMD_UPDATE to commit (NOT CBD_POST, which is the deferred-post mechanism
 *     expecting io_Data to be a SatisfyMsg MsgPort – using it here crashes OS 3.9)
 *
 * Read protocol (matches RKM cbio.c):
 *   - sequential CMD_READ calls, no manual io_Offset repositioning
 *   - clip_read_done() drains remaining data so the device is left clean
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <string.h>

#include "unitexteditor_private.h"

/* =========================================================================
 * Low-level clipboard I/O primitives
 * =========================================================================
 */

/* Write exactly 4 bytes (chunk ID literal or big-endian ULONG) to the stream. */
static BOOL clip_write4(struct IOClipReq *ior, const void *data)
{
    ior->io_Data    = (STRPTR)data;
    ior->io_Length  = 4;
    ior->io_Command = CMD_WRITE;
    DoIO((struct IORequest *)ior);
    return (ior->io_Actual == 4 && !ior->io_Error);
}

/* Read exactly 4 bytes from the stream into *out. Device advances io_Offset. */
static BOOL clip_read4(struct IOClipReq *ior, ULONG *out)
{
    ior->io_Data    = (STRPTR)out;
    ior->io_Length  = 4;
    ior->io_Command = CMD_READ;
    DoIO((struct IORequest *)ior);
    return (ior->io_Actual == 4 && !ior->io_Error);
}

/* Drain remaining clipboard stream data to signal end-of-read to the device.
 * Must be called after every read session, even on early exit (see cbio.c CBReadDone). */
static void clip_read_done(struct IOClipReq *ior)
{
    char buf[256];
    ior->io_Command = CMD_READ;
    ior->io_Data    = (STRPTR)buf;
    ior->io_Length  = sizeof(buf);
    /* falls through immediately if io_Actual is already 0 */
    while (ior->io_Actual)
        if (DoIO((struct IORequest *)ior)) break;
}

/* =========================================================================
 * Internal clipboard device helpers
 * =========================================================================
 */

/* Write a byte buffer to the clipboard as IFF FTXT/CHRS.
 * Uses incremental CMD_WRITE calls (one per IFF field) then CMD_UPDATE to
 * commit, matching the cbio.c reference exactly. */
static BOOL clipboard_write(const char *text, ULONG len)
{
    struct MsgPort   *port;
    struct IOClipReq  ior;
    ULONG             formBodyLen;
    BOOL              odd;
    const UBYTE       zero = 0;

    if (!text || len == 0) return FALSE;

    port = CreateMsgPort();
    if (!port) return FALSE;

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0) != 0) {
        DeleteMsgPort(port);
        return FALSE;
    }

    odd = (len & 1);
    /* FORM body = "FTXT"(4) + "CHRS"(4) + chunk-size(4) + data (+ optional pad) */
    formBodyLen = 12 + (odd ? len + 1 : len);

    ior.io_Offset = 0;
    ior.io_Error  = 0;
    ior.io_ClipID = 0;

    /* IFF FORM header */
    clip_write4(&ior, "FORM");
    clip_write4(&ior, &formBodyLen);   /* big-endian on 68k – correct for IFF */
    clip_write4(&ior, "FTXT");
    /* CHRS chunk */
    clip_write4(&ior, "CHRS");
    clip_write4(&ior, &len);

    /* text payload */
    ior.io_Data    = (STRPTR)text;
    ior.io_Length  = len;
    ior.io_Command = CMD_WRITE;
    DoIO((struct IORequest *)&ior);

    /* IFF pad byte for odd-length chunks */
    if (odd) {
        ior.io_Data   = (STRPTR)&zero;
        ior.io_Length = 1;
        DoIO((struct IORequest *)&ior);
    }

    /* Commit: CMD_UPDATE tells the clipboard the write is complete.
     * CBD_POST must NOT be used here – it is the deferred-post mechanism
     * and expects io_Data to point to a SatisfyMsg MsgPort, not our buffer. */
    ior.io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)&ior);

    CloseDevice((struct IORequest *)&ior);
    DeleteMsgPort(port);
    return (ior.io_Error == 0);
}

/* Read raw bytes from the clipboard CHRS chunk.
 * Uses sequential CMD_READ calls (device advances io_Offset automatically)
 * and calls clip_read_done() before closing so the device is left clean.
 * Returns an AllocVec'd NUL-terminated buffer, or NULL on empty/error.
 * Caller must FreeVec the result. */
static char *clipboard_read(void)
{
    struct MsgPort   *port;
    struct IOClipReq  ior;
    ULONG             id, size;
    char             *result = NULL;

    port = CreateMsgPort();
    if (!port) return NULL;

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0) != 0) {
        DeleteMsgPort(port);
        return NULL;
    }

    ior.io_Offset = 0;
    ior.io_Error  = 0;
    ior.io_ClipID = 0;

    /* Expect "FORM" <formSize> "FTXT" */
    if (!clip_read4(&ior, &id) || id != 0x464F524DUL) goto done; /* "FORM" */
    if (!clip_read4(&ior, &size))                      goto done; /* form body size */
    if (!clip_read4(&ior, &id) || id != 0x46545854UL) goto done; /* "FTXT" */

    /* Walk chunks, looking for the first non-empty CHRS */
    for (;;) {
        ULONG chunkId, chunkSize;
        if (!clip_read4(&ior, &chunkId))   break;
        if (!clip_read4(&ior, &chunkSize)) break;

        if (chunkId == 0x43485253UL && chunkSize > 0) { /* "CHRS" */
            /* read chunkSize bytes plus optional IFF pad byte */
            ULONG readLen = (chunkSize & 1) ? chunkSize + 1 : chunkSize;
            result = (char *)AllocVec(chunkSize + 1, MEMF_ANY);
            if (result) {
                ior.io_Data    = (STRPTR)result;
                ior.io_Length  = readLen;
                ior.io_Command = CMD_READ;
                DoIO((struct IORequest *)&ior);
                if (ior.io_Actual >= chunkSize) {
                    result[chunkSize] = '\0';
                } else {
                    FreeVec(result);
                    result = NULL;
                }
            }
            break;
        } else {
            /* skip unknown / zero-size chunk: advance past padded data */
            ULONG skip = (chunkSize & 1) ? chunkSize + 1 : chunkSize;
            ior.io_Offset += skip;
        }
    }

done:
    clip_read_done(&ior); /* drain stream – device requires this even on early exit */
    CloseDevice((struct IORequest *)&ior);
    DeleteMsgPort(port);
    return result;
}

/* =========================================================================
 * Encoding helpers for paste
 * =========================================================================
 */

/* Returns TRUE when every byte sequence in the NUL-terminated string is
 * well-formed UTF-8.  Rejects overlong encodings, surrogates, and any
 * byte that cannot appear as a lead byte (0x80-0xBF, 0xC0, 0xC1, 0xF5-0xFF). */
static BOOL is_valid_utf8(const char *s)
{
    const UBYTE *p = (const UBYTE *)s;
    while (*p) {
        UBYTE b = *p++;
        ULONG extra;
        if (b < 0x80) continue;
        if (b < 0xC2 || b > 0xF4) return FALSE;
        if      (b < 0xE0) extra = 1;
        else if (b < 0xF0) extra = 2;
        else               extra = 3;
        while (extra--) {
            if ((*p & 0xC0) != 0x80) return FALSE;
            p++;
        }
    }
    return TRUE;
}

/* ISO 8859-2 (Latin-2) codepoint table for bytes 0xA0-0xFF.
 * Bytes 0x80-0x9F are C1 controls and are silently dropped. */
static const ULONG cb_latin2_uni[96] = {
    /* A0 */ 0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
    /* A8 */ 0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
    /* B0 */ 0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
    /* B8 */ 0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
    /* C0 */ 0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    /* C8 */ 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    /* D0 */ 0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    /* D8 */ 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    /* E0 */ 0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    /* E8 */ 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    /* F0 */ 0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    /* F8 */ 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

/* Convert a NUL-terminated Latin-1 or Latin-2 string to a freshly
 * AllocVec'd UTF-8 string.  Bytes 0x80-0x9F (C1 controls) are dropped.
 * Returns NULL on allocation failure; caller must FreeVec the result. */
static char *latin_to_utf8(const char *src, ULONG ansiCode)
{
    const UBYTE *s = (const UBYTE *)src;
    ULONG srcLen = (ULONG)strlen(src);
    /* Each input byte produces at most 3 UTF-8 bytes; guard against ULONG
     * overflow on pathologically large inputs by capping at 16 MB. */
    if (srcLen > 0x555554UL) srcLen = 0x555554UL;
    ULONG maxOut = srcLen * 3 + 1;
    char *dst = (char *)AllocVec(maxOut, MEMF_ANY);
    if (!dst) return NULL;

    char *d = dst;
    while (*s && (ULONG)(d - dst) < maxOut - 3) {
        UBYTE b = *s++;
        if (b < 0x80) {
            *d++ = (char)b;
        } else if (b >= 0xA0) {
            ULONG cp;
            if (ansiCode == UTED_VANILLAKEY_LATIN2)
                cp = cb_latin2_uni[b - 0xA0];
            else
                cp = (ULONG)b; /* Latin-1: codepoint == byte value */
            if (cp < 0x80) {
                *d++ = (char)cp;
            } else if (cp < 0x800) {
                *d++ = (char)(0xC0 | (cp >> 6));
                *d++ = (char)(0x80 | (cp & 0x3F));
            } else {
                *d++ = (char)(0xE0 | (cp >> 12));
                *d++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                *d++ = (char)(0x80 | (cp & 0x3F));
            }
        }
        /* 0x80-0x9F: C1 controls – silently dropped */
    }
    *d = '\0';
    return dst;
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
 * If the clipboard content is not valid UTF-8 it is treated as encoded in
 * the vanillaAnsiCode encoding (Latin-1 or Latin-2) and converted on the fly.
 * Returns TRUE if text was inserted (caller should trigger redraw). */
BOOL uted_do_clipboard_paste(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    char *text = clipboard_read();
    if (!text) return FALSE;

    if (!is_valid_utf8(text)) {
        char *converted = latin_to_utf8(text, inst->vanillaAnsiCode);
        FreeVec(text);
        if (!converted) return FALSE;
        UniTextEditor_DoInsertText(cl, o, converted, -1);
        FreeVec(converted);
    } else {
        UniTextEditor_DoInsertText(cl, o, text, -1);
        FreeVec(text);
    }
    return TRUE;
}
