/*
 * Shared pipe / stdin input handling for EmojiGear and MUImojiGear.
 * See egpipeinput.h for the public API.
 */

#include "egpipeinput.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

/* Returns the length of the longest prefix of buf[0..len-1] that ends on a
 * complete UTF-8 codepoint boundary (no split multi-byte sequences).
 *
 * Algorithm: walk back from the end to find the last leading byte, then
 * check whether all of its continuation bytes are present. */
static LONG utf8_complete_len(const char *buf, LONG len)
{
    LONG     i = len;
    unsigned char c;
    int      seqLen;

    if (len <= 0) return 0;

    while (i > 0) {
        c = (unsigned char)buf[i - 1];
        if ((c & 0xC0) != 0x80) break;
        i--;
    }
    if (i == 0) return 0; /* buffer is all continuation bytes - broken stream */

    c = (unsigned char)buf[i - 1];
    if      (c < 0x80)           seqLen = 1;
    else if ((c & 0xE0) == 0xC0) seqLen = 2;
    else if ((c & 0xF0) == 0xE0) seqLen = 3;
    else if ((c & 0xF8) == 0xF0) seqLen = 4;
    else                          seqLen = 1; /* invalid leading byte */

    return ((i - 1) + seqLen <= len) ? len : i - 1;
}

/* Returns TRUE if buf[0..len-1] is valid UTF-8.  A stray 0x80-0xFF byte that
 * is not part of a legal multi-byte sequence causes an immediate FALSE,
 * which is exactly what plain Latin-1 ("ANSI") text triggers. */
static BOOL is_valid_utf8(const char *buf, LONG len)
{
    LONG i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)buf[i++];
        int extra;
        if (c < 0x80) continue;
        if (c < 0xC2) return FALSE;       /* 0x80-0xC1: stray continuation or overlong */
        else if (c < 0xE0) extra = 1;
        else if (c < 0xF0) extra = 2;
        else if (c < 0xF5) extra = 3;
        else               return FALSE;  /* 0xF5-0xFF: invalid lead byte */
        while (extra--) {
            if (i >= len || ((unsigned char)buf[i] & 0xC0) != 0x80) return FALSE;
            i++;
        }
    }
    return TRUE;
}

/* Re-encode an ISO 8859-1 (Latin-1) buffer to UTF-8.  Code point == byte
 * value, so each source byte expands to at most 2 UTF-8 bytes.  Returns an
 * AllocVec'd, NUL-terminated string the caller must FreeVec(), or NULL on
 * allocation failure. */
static char *latin1_to_utf8(const char *src, LONG srcLen)
{
    char *out, *p;
    LONG  i;

    out = (char *)AllocVec((ULONG)(srcLen * 2 + 1), MEMF_ANY);
    if (!out) return NULL;

    p = out;
    for (i = 0; i < srcLen; i++) {
        unsigned int cp = (unsigned char)src[i];
        if (cp < 0x80) {
            *p++ = (char)cp;
        } else {
            *p++ = (char)(0xC0 | (cp >> 6));
            *p++ = (char)(0x80 | (cp & 0x3F));
        }
    }
    *p = '\0';
    return out;
}

const char *EgPipeInput_Poll(char *buf, ULONG bufSize, LONG *pUsed, BOOL *needsFree)
{
    LONG used = *pUsed;
    LONG safeLen;

    *needsFree = FALSE;

    /* Step 1: read from non-interactive handle (pipe / file redirect).
     * On a real CON: handle Read() would block, so only do this when
     * IsInteractive() says it is safe (PIPE: and file redirects return 0
     * immediately when no data is available). */
    {
        BPTR inpt = Input();
        if (inpt && !IsInteractive(inpt)) {
            LONG canRead = (LONG)bufSize - used - 1; /* reserve 1 for NUL */
            if (canRead > 0) {
                LONG nread = Read(inpt, buf + used, canRead);
                if (nread > 0) used += nread;
            }
        }
    }

    if (used <= 0) {
        *pUsed = used;
        return NULL;
    }

    /* Step 2: flush the largest complete-codepoint prefix of the buffer. */
    safeLen = utf8_complete_len(buf, used);

    /* Safety valve: broken stream, buffer almost full, no boundary found. */
    if (safeLen == 0 && used >= (LONG)bufSize - 4)
        safeLen = used;

    if (safeLen == 0) {
        *pUsed = used;
        return NULL;
    }

    {
        LONG remaining = used - safeLen;
        char *result;

        /* AmigaDOS pipes/redirections may carry plain 8-bit Latin-1 text
         * (e.g. `echo "â" | EmojiGear`) instead of UTF-8.
         * utf8_complete_len() only checks sequence *length*, not validity,
         * so a Latin-1 byte such as 0xE2 followed by ASCII can look
         * "complete" while being illegal UTF-8.  Detect this and re-encode
         * the chunk from Latin-1 to UTF-8 before handing it to the editor. */
        if (!is_valid_utf8(buf, safeLen)) {
            result = latin1_to_utf8(buf, safeLen);
        } else {
            /* Copy out the chunk so buf is free to be rearranged below
             * without corrupting the returned data or its NUL terminator. */
            result = (char *)AllocVec((ULONG)safeLen + 1, MEMF_ANY);
            if (result) {
                CopyMem(buf, result, (ULONG)safeLen);
                result[safeLen] = '\0';
            }
        }
        if (result) *needsFree = TRUE;

        /* Shift any incomplete trailing sequence to the front of buf so it
         * can be completed on a later call. */
        if (remaining > 0)
            memmove(buf, buf + safeLen, (size_t)remaining);
        *pUsed = remaining;

        return result;
    }
}
