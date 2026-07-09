/*
 * fs3etextwrap — see fs3etextwrap.h.
 */

#include <proto/exec.h>
#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include "fs3etextwrap.h"

/* Count UTF-8 codepoints in [start, end). Duplicated here (rather than
 * shared with TootTimeline's private helper) so this module has no
 * dependency on any gadget's private headers. */
static LONG count_codepoints(const char *start, const char *end)
{
    LONG n = 0;
    const unsigned char *p = (const unsigned char *)start;
    const unsigned char *e = (const unsigned char *)end;
    while (p < e) {
        unsigned char c = *p;
        if      (c < 0x80) p += 1;
        else if (c < 0xE0) p += 2;
        else if (c < 0xF0) p += 3;
        else               p += 4;
        n++;
    }
    return n;
}

/* Byte offset of the charIdx'th codepoint after `start` (charIdx <= the
 * value passed as codepoint count to whatever built the offsets). */
static ULONG char_to_byte(const char *start, ULONG charIdx)
{
    const unsigned char *p = (const unsigned char *)start;
    ULONG i;
    for (i = 0; i < charIdx; i++) {
        unsigned char c = *p;
        if (c == 0) break;
        if      (c < 0x80) p += 1;
        else if (c < 0xE0) p += 2;
        else if (c < 0xF0) p += 3;
        else               p += 4;
    }
    return (ULONG)(p - (const unsigned char *)start);
}

static BOOL row_array_grow(FS3ETextWrap *tw, ULONG *alloc)
{
    ULONG newAlloc = *alloc ? *alloc * 2 : 16;
    FS3ETextRow *newRows = (FS3ETextRow *)AllocVec(
        newAlloc * sizeof(FS3ETextRow), MEMF_ANY | MEMF_CLEAR);
    if (!newRows) return FALSE;
    if (tw->rows && tw->rowCount)
        CopyMem(tw->rows, newRows, tw->rowCount * sizeof(FS3ETextRow));
    if (tw->rows) FreeVec(tw->rows);
    tw->rows = newRows;
    *alloc = newAlloc;
    return TRUE;
}

/* Build one wrapped row's FS3ETextRow entry: its own charXOffsets array,
 * scoped to just that row (offsets are 0-based within the row, not within
 * the whole logical line). */
static BOOL emit_row(FS3ETextWrap *tw, ULONG *alloc,
                      struct URPDrawContext *dc,
                      const char *rowStart, ULONG rowByteLen, ULONG rowCharCount)
{
    FS3ETextRow *row;

    if (tw->rowCount >= *alloc && !row_array_grow(tw, alloc)) return FALSE;

    row = &tw->rows[tw->rowCount++];
    row->start     = rowStart;
    row->byteLen   = rowByteLen;
    row->charCount = rowCharCount;
    row->width     = 0;
    row->charXOffsets = NULL;

    if (rowCharCount > 0) {
        row->charXOffsets = (LONG *)AllocVec(
            (rowCharCount + 1) * sizeof(LONG), MEMF_ANY);
        if (row->charXOffsets) {
            URPDC_HorizontalOffsetArrayUTF8(dc, rowStart, (LONG)rowCharCount,
                                             row->charXOffsets);
            row->width = (WORD)row->charXOffsets[rowCharCount];
        }
    }
    return TRUE;
}

BOOL FS3ETextWrap_Build(FS3ETextWrap *tw, struct URPDrawContext *dc,
                         WORD lineHeight, const char *utf8, WORD maxW)
{
    const char *logStart;
    ULONG       alloc = 0;

    tw->rows        = NULL;
    tw->rowCount    = 0;
    tw->lineHeight  = lineHeight;
    tw->totalHeight = 0;

    if (!dc || !utf8 || !utf8[0] || maxW <= 0) return FALSE;

    logStart = utf8;
    for (;;) {
        const char *logEnd = logStart;
        LONG n;

        while (*logEnd && *logEnd != '\n') logEnd++;
        n = count_codepoints(logStart, logEnd);

        if (n == 0) {
            /* Blank logical line: one zero-width row so callers that
             * iterate rows still see the vertical gap. */
            if (!emit_row(tw, &alloc, dc, logStart, 0, 0)) goto fail;
        } else {
            LONG *lineOffs = (LONG *)AllocVec((n + 1) * sizeof(LONG), MEMF_ANY);
            LONG  rowStartChar = 0;

            if (!lineOffs) goto fail;
            URPDC_HorizontalOffsetArrayUTF8(dc, logStart, n, lineOffs);

            while (rowStartChar < n) {
                LONG rowStartPx = lineOffs[rowStartChar];
                LONG rowEndChar, wrapAt;
                LONG lo = rowStartChar, hi = n;

                /* Last char whose left edge still fits within maxW */
                while (lo < hi) {
                    LONG mid = (lo + hi + 1) / 2;
                    if (lineOffs[mid] - rowStartPx <= (LONG)maxW) lo = mid;
                    else hi = mid - 1;
                }
                rowEndChar = lo;
                if (rowEndChar <= rowStartChar) rowEndChar = rowStartChar + 1;
                if (rowEndChar > n) rowEndChar = n;

                if (rowEndChar >= n) {
                    wrapAt = n;
                } else {
                    /* Prefer breaking at the last space/tab in range */
                    ULONG bi = char_to_byte(logStart, (ULONG)rowStartChar);
                    LONG  si;
                    wrapAt = rowEndChar;
                    for (si = rowStartChar; si < rowEndChar; si++) {
                        unsigned char c = (unsigned char)logStart[bi];
                        if (c == ' ' || c == '\t') wrapAt = si + 1;
                        bi += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                    }
                }

                {
                    ULONG byteStart = char_to_byte(logStart, (ULONG)rowStartChar);
                    ULONG byteEnd   = char_to_byte(logStart, (ULONG)wrapAt);
                    if (!emit_row(tw, &alloc, dc, logStart + byteStart,
                                  byteEnd - byteStart, (ULONG)(wrapAt - rowStartChar))) {
                        FreeVec(lineOffs);
                        goto fail;
                    }
                }

                rowStartChar = wrapAt;
            }
            FreeVec(lineOffs);
        }

        if (!*logEnd) break;
        logStart = logEnd + 1;
    }

    tw->totalHeight = (LONG)tw->rowCount * (LONG)lineHeight;
    return TRUE;

fail:
    /* Keep the "FALSE means tw is left zeroed" contract even when the
     * failure happened mid-wrap (an AllocVec failure partway through),
     * not just on the early up-front validation -- callers only free tw
     * on the success path (see ttl_post_layout), so a partially-built
     * tw here would otherwise leak every row/charXOffsets already built. */
    FS3ETextWrap_Free(tw);
    return FALSE;
}

void FS3ETextWrap_Free(FS3ETextWrap *tw)
{
    ULONG i;
    if (!tw) return;
    if (tw->rows) {
        for (i = 0; i < tw->rowCount; i++)
            if (tw->rows[i].charXOffsets) FreeVec(tw->rows[i].charXOffsets);
        FreeVec(tw->rows);
    }
    tw->rows        = NULL;
    tw->rowCount    = 0;
    tw->totalHeight = 0;
}
