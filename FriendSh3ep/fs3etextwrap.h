#ifndef FS3ETEXTWRAP_H
#define FS3ETEXTWRAP_H

/*
 * fs3etextwrap — word-wrap a UTF-8 string into rows that fit a given pixel
 * width, using utf8rastport metrics, without drawing anything.
 *
 * Any caller that needs "wrap this text into a box of known width, find out
 * how tall it needs to be, then draw it" (toot bodies, image alt-text
 * tooltips, ...) should go through this instead of re-deriving its own
 * wrap loop: it is the single place that decides where line breaks land,
 * so layout (height / hit-testing) and drawing always agree pixel-for-pixel.
 *
 * Explicit '\n' bytes are always forced breaks. Otherwise a row grows
 * until the next word would exceed maxW, preferring to break at the last
 * space/tab; a single word wider than maxW is hard-broken mid-word so
 * progress is always made.
 *
 * Usage:
 *   FS3ETextWrap tw;
 *   if (FS3ETextWrap_Build(&tw, dc, lineHeight, utf8, maxW)) {
 *       // tw.totalHeight now known -- reserve that much space, THEN:
 *       ULONG i;
 *       for (i = 0; i < tw.rowCount; i++) {
 *           FS3ETextRow *row = &tw.rows[i];
 *           // row->start/byteLen -> substring (not NUL-terminated);
 *           // row->width -> pixel width; row->charXOffsets[c] -> pixel x
 *           // of char c within the row.
 *       }
 *       FS3ETextWrap_Free(&tw);
 *   }
 */

#include <exec/types.h>

struct URPDrawContext;

/* One wrapped visual row. `start` points into the original text passed to
 * FS3ETextWrap_Build (not owned, not NUL-terminated at byteLen); that text
 * must stay alive and unchanged until the caller is done reading rows.
 * charXOffsets is AllocVec'd here and owned by the FS3ETextWrap. */
typedef struct {
    const char *start;
    ULONG       byteLen;
    ULONG       charCount;
    LONG       *charXOffsets;  /* charCount+1 entries; [0]=0, [charCount]=width */
    WORD        width;         /* == charXOffsets[charCount], or 0 if charCount==0 */
} FS3ETextRow;

typedef struct {
    FS3ETextRow *rows;         /* AllocVec'd array of rowCount entries */
    ULONG        rowCount;
    WORD         lineHeight;   /* as passed to Build */
    LONG         totalHeight;  /* rowCount * lineHeight */
} FS3ETextWrap;

/* Word-wrap utf8 (a plain NUL-terminated string) into rows of at most maxW
 * pixels using dc's metrics. lineHeight should come from the caller's
 * cached URPDC_GetFontLineMetrics(dc, ...).height and is used only to
 * compute totalHeight -- one row is emitted per visual line regardless.
 *
 * Returns FALSE (tw left zeroed) if dc is NULL, utf8 is NULL/empty, or
 * maxW <= 0 -- callers can treat that as "zero rows, zero height".
 * On success tw is filled in and must be released with FS3ETextWrap_Free. */
BOOL FS3ETextWrap_Build(FS3ETextWrap *tw, struct URPDrawContext *dc,
                         WORD lineHeight, const char *utf8, WORD maxW);

void FS3ETextWrap_Free(FS3ETextWrap *tw);

#endif /* FS3ETEXTWRAP_H */
