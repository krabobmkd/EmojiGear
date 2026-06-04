/*
 * TextEditor gadget – line bitmap chunk cache + pool management.
 *
 * Bitmap pool
 * -----------
 * All chunk bitmaps are identical in size (LINE_CHUNK_WIDTH × lineHeight) and
 * are allocated once at layout time into UTEDBitMapPool.  Lines "borrow" pool
 * entries when a chunk comes into view and return them when evicted.  No
 * AllocBitMap/FreeBitMap happens during normal editing or scrolling; chip RAM
 * usage is bounded.
 *
 * Pool is rebuilt when lineHeight changes (font resize).  A simple eviction
 * sweep in GM_RENDER returns the bitmaps of lines that have scrolled beyond
 * [scrollTopLine - POOL_LINE_BUFFER, scrollTopLine + visibleLines + POOL_LINE_BUFFER].
 *
 * Chunk rendering
 * ---------------
 * Chunk i covers pixels [i*LINE_CHUNK_WIDTH .. (i+1)*LINE_CHUNK_WIDTH).
 * Rendering starts from the last character whose left edge is strictly before
 * i*LINE_CHUNK_WIDTH so that glyphs straddling the left boundary are included.
 * Selection and cursor overlays are NOT baked in – drawn by GM_RENDER via
 * COMPLEMENT mode.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include "unitexteditor_private.h"



/* =========================================================================
 * Pool helpers (file-scope)
 * =========================================================================
 */

INLINE struct BitMap *pool_take(UTEDBitMapPool *pool)
{
    if (pool->freeCount == 0) return NULL;
    return pool->freeStack[--pool->freeCount];
}

INLINE void pool_return(UTEDBitMapPool *pool, struct BitMap *bm)
{
    if (!bm) return;
    if (pool->freeCount >= pool->size) return; /* safety: never exceed capacity */
    pool->freeStack[pool->freeCount++] = bm;
}

/* =========================================================================
 * uted_pool_alloc
 *
 * Allocate `size` BitMap tiles of LINE_CHUNK_WIDTH × lineHeight plus one
 * shared Layer+RastPort for tile rendering.
 * Stops early if chip RAM runs out; returns FALSE if zero tiles allocated.
 * =========================================================================
 */
BOOL uted_pool_alloc(UTEDBitMapPool *pool, ULONG size,
                     UWORD lineHeight, struct Screen *screen)
{
    ULONG i;
    struct BitMap *friendBM = screen ? screen->RastPort.BitMap : NULL;
    ULONG         depth = 4;
    if(friendBM) depth = GetBitMapAttr(friendBM,BMA_DEPTH);

    pool->bitmaps   = (struct BitMap **)AllocVec(size * sizeof(struct BitMap *),
                                                   MEMF_ANY | MEMF_CLEAR);
    pool->freeStack = (struct BitMap **)AllocVec(size * sizeof(struct BitMap *),
                                                    MEMF_ANY);
    if (!pool->bitmaps || !pool->freeStack) {
        if (pool->bitmaps)   { FreeVec(pool->bitmaps);   pool->bitmaps   = NULL; }
        if (pool->freeStack) { FreeVec(pool->freeStack); pool->freeStack = NULL; }
        return FALSE;
    }

    pool->size      = 0;
    pool->freeCount = 0;
    pool->height    = lineHeight;
    pool->layerInfo = NULL;
    pool->layer     = NULL;
    pool->rp        = NULL;

    // if(GfxBase->LibNode.lib_Version>=47)
    // {
    //     ULONG abmtags[]= {
    //        // BMATags_Friend,(ULONG)friendBM,
    //         BMATags_Depth, depth,
    //         BMATags_BitmapInvisible,TRUE,
    //         TAG_END
    //     };
    //     /* New AllocBitMap, may alloc in fast */
    //     for (i = 0; i < size; i++) {
    //         pool->bitmaps[i] = AllocBitMap(LINE_CHUNK_WIDTH, (ULONG)lineHeight,
    //                                 depth, BMF_CLEAR
    //                                 /* want tags*/
    //                                 | BMF_RTGTAGS|BMF_RTGCHECK|BMF_FRIENDISTAG
    //                                 , (struct BitMap *)&abmtags[0]
    //                                 );
    //         if (!pool->bitmaps[i]) break;   /* chip RAM exhausted */
    //         pool->freeStack[pool->freeCount++] = pool->bitmaps[i];
    //         pool->size++;
    //     }
    // } else
    {
        /* antique AllocBitMap, surely in chipram if native mode */
        for (i = 0; i < size; i++) {
            pool->bitmaps[i] = AllocBitMap(LINE_CHUNK_WIDTH, (ULONG)lineHeight,
                                            depth, BMF_CLEAR, friendBM);
            if (!pool->bitmaps[i]) break;   /* chip RAM exhausted */
            pool->freeStack[pool->freeCount++] = pool->bitmaps[i];
            pool->size++;
        }
    }

    if (pool->size == 0) {
        FreeVec(pool->bitmaps);   pool->bitmaps   = NULL;
        FreeVec(pool->freeStack); pool->freeStack = NULL;
        return FALSE;
    }

    /* Create the single shared Layer+RastPort IF NOT DONE used when rendering tiles.
     * The layer is initialised with bitmaps[0] but rp->BitMap is swapped
     * to each tile's bitmap before every uted_line_render_chunk() call. */

    if(pool->layerInfo == NULL)
    {
        pool->layerInfo = NewLayerInfo();
        if(pool->layerInfo)
        {
           //test
           LockLayerInfo(pool->layerInfo);
            pool->layer = CreateUpfrontLayer(pool->layerInfo, pool->bitmaps[0],
                                 0, 0,
                                 LINE_CHUNK_WIDTH - 1, (LONG)lineHeight - 1,
                                 LAYERSIMPLE, NULL);
            if (!pool->layer) {uted_pool_free_layer(pool);  uted_pool_free_bitmapcache(pool);  return FALSE; }

            pool->rp = pool->layer->rp;
        }

    } else
    {
        /* clipping layer already created, but may have to be resized (if font size change) */
        if((lineHeight - 1) != pool->layer->bounds.MaxY)
        {
            LONG dx = 0;
            LONG dy = (lineHeight - 1) - pool->layer->bounds.MaxY;
            SizeLayer(0,pool->layer,0,dy);
        }
    }


    return TRUE;
}

/* =========================================================================
 * uted_pool_growalloc
 *
 * Grow an existing pool to `newSize` entries WITHOUT freeing and
 * reallocating the bitmaps that are already in it.
 *
 * Precondition: all lines must have returned their borrowed pointers
 * (uted_line_evict_chunks called for every line) so freeCount == size
 * and no line->chunks[] pointer references the old bitmaps array.
 *
 * The chip-RAM bitmaps never move; only the host-RAM pointer array is
 * replaced with a larger one.  The shared Layer and RastPort are reused.
 * =========================================================================
 */
BOOL uted_pool_growalloc(UTEDBitMapPool *pool, ULONG newSize, struct Screen *screen)
{
    ULONG         i;
    ULONG         oldSize   = pool->size;
    ULONG         allocCount;
    struct BitMap **newBitmaps;
    struct BitMap **newFreeStack;
    struct BitMap *friendBM = screen ? screen->RastPort.BitMap : NULL;
    ULONG         depth = 4;

    if(friendBM) depth = GetBitMapAttr(friendBM,BMA_DEPTH);
    //  friendBM ? (ULONG)friendBM->Depth : 4;

    if (newSize <= oldSize) return (pool->freeCount > 0);

    newBitmaps = (struct BitMap **)AllocVec(newSize * sizeof(struct BitMap *),
                                             MEMF_ANY | MEMF_CLEAR);
    newFreeStack = (struct BitMap **)AllocVec(newSize * sizeof(struct BitMap *),
                                               MEMF_ANY| MEMF_CLEAR);
    if (!newBitmaps || !newFreeStack) {
        if (newBitmaps)   FreeVec(newBitmaps);
        if (newFreeStack) FreeVec(newFreeStack);
        return (pool->freeCount > 0);
    }

    /* Copy existing bitmap pointers; chip-RAM bitmaps stay at their addresses. */
    if (oldSize > 0)
        CopyMem(pool->bitmaps, newBitmaps, oldSize * sizeof(struct BitMap *));

    /* Allocate bitmaps for the newly added slots */
    allocCount = oldSize;


    // if(GfxBase->LibNode.lib_Version>=47)
    // {
    //     ULONG abmtags[]= {
    //        // BMATags_Friend,(ULONG)friendBM,
    //         BMATags_Depth, depth,
    //         BMATags_BitmapInvisible,TRUE,
    //         TAG_END
    //     };
    //     /* New AllocBitMap, may alloc in fast */

    //     for (i = oldSize; i < newSize; i++) {
    //         newBitmaps[i] = AllocBitMap(LINE_CHUNK_WIDTH, (ULONG)pool->height,
    //                                      depth, BMF_CLEAR
    //                                 /* want tags*/
    //                                 | BMF_RTGTAGS|BMF_RTGCHECK|BMF_FRIENDISTAG
    //                                 , (struct BitMap *)&abmtags[0]
    //                                 );
    //         if (!newBitmaps[i]) break;
    //         allocCount++;
    //     }
    // } else
    {

        for (i = oldSize; i < newSize; i++) {
            newBitmaps[i] = AllocBitMap(LINE_CHUNK_WIDTH, (ULONG)pool->height,
                                         depth, BMF_CLEAR, friendBM);
            if (!newBitmaps[i]) break;
            allocCount++;
        }
    }

    /* All entries are free (precondition: caller evicted everything first) */
    pool->freeCount = 0;
    for (i = 0; i < allocCount; i++)
        newFreeStack[pool->freeCount++] = newBitmaps[i];

    FreeVec(pool->bitmaps);
    FreeVec(pool->freeStack);

    pool->bitmaps   = newBitmaps;
    pool->freeStack = newFreeStack;
    pool->size      = allocCount;

    /* The shared Layer and RastPort are unchanged (same tile dimensions). */
    return (pool->freeCount > 0);
}

/* =========================================================================
 * uted_pool_free_bitmapcache
 *
 * Close all pool bitmaps and free the two AllocVec'd arrays.
 * All lines MUST have returned their borrowed pointers before calling this.
 * =========================================================================
 */
void uted_pool_free_bitmapcache(UTEDBitMapPool *pool)
{
    ULONG i;
    if (pool->bitmaps) {
        /* in case there is some rendering on bitmap */
        WaitBlit();

        for (i = 0; i < pool->size; i++)
            if (pool->bitmaps[i]) FreeBitMap(pool->bitmaps[i]);
        FreeVec(pool->bitmaps);
        pool->bitmaps = NULL;
    }
    if (pool->freeStack) { FreeVec(pool->freeStack); pool->freeStack = NULL; }
    pool->size      = 0;
    pool->freeCount = 0;
    pool->height    = 0;
}
void uted_pool_free_layer(UTEDBitMapPool *pool)
{
    if (pool->layer)     { DeleteLayer(0, pool->layer);       pool->layer     = NULL; }
    if (pool->layerInfo) {
      //was test
      UnlockLayerInfo(pool->layerInfo);
        DisposeLayerInfo(pool->layerInfo); pool->layerInfo = NULL;
     }
    pool->rp = NULL;
}

/* =========================================================================
 * uted_line_evict_chunks
 *
 * Return all chunk bitmap pointers borrowed by `line` to the pool.
 * The chunks[] pointer array is kept allocated for future reuse.
 * =========================================================================
 */
void uted_line_evict_chunks(UniTextEditorData *inst, UniTextEditorLine *line)
{
    ULONG i;
    for (i = 0; i < line->chunkAlloc; i++) {
        if (line->chunks[i]) {
            pool_return(&inst->bmPool, line->chunks[i]);
            line->chunks[i] = NULL;
        }
    }
}

/* =========================================================================
 * uted_line_evict_distant_chunks
 *
 * Return chunk bitmaps whose index is outside [keepFirst, keepLast] to the
 * pool.  Used by the horizontal eviction sweep: lines in the vertical keep
 * zone shed only their off-screen horizontal chunks, not all of them.
 * =========================================================================
 */
void uted_line_evict_distant_chunks(UniTextEditorData *inst,
                                     UniTextEditorLine *line,
                                     ULONG keepFirst, ULONG keepLast)
{
    ULONG i;
    for (i = 0; i < line->chunkAlloc; i++) {
        if (line->chunks[i] && (i < keepFirst || i > keepLast)) {
            pool_return(&inst->bmPool, line->chunks[i]);
            line->chunks[i] = NULL;
        }
    }
}

/* =========================================================================
 * uted_line_free_cache
 *
 * Return all borrowed chunk bitmaps to pool and free the pointer array.
 * =========================================================================
 */
void uted_line_free_cache(UniTextEditorData *inst, UniTextEditorLine *line)
{
    ObtainSemaphore(&inst->cacheSem);
    if (line->chunks) {
        uted_line_evict_chunks(inst, line);
        FreeVec(line->chunks);
        line->chunks = NULL;
    }
    line->chunkAlloc  = 0;
    line->chunkHeight = 0;
    if (line->ansiRuns) { FreeVec(line->ansiRuns); line->ansiRuns = NULL; }
    line->ansiRunCount = 0;
    line->ansiRunAlloc = 0;
    ReleaseSemaphore(&inst->cacheSem);
}

/* =========================================================================
 * ANSI escape run builder
 * =========================================================================
 */

/* Return UTF-8 byte length of sequence starting with byte c. */
INLINE ULONG ansi_seqlen(unsigned char c)
{
    if (c < 0x80) return 1;
    if (c < 0xC0) return 1;
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

/* xterm default 16-colour palette: indices 0-7 standard, 8-15 bright.
 * Standard channels boosted: 170*1.5=255; bright off-channels: 85*1.5≈128.
 * Index 7 kept at 192 (VGA light-gray) to stay distinct from Bright White. */
static const UBYTE ansi_unix_rgb[16][3] = {
    {   0,   0,   0 }, /*  0 Black         */
    { 255,   0,   0 }, /*  1 Red           */
    {   0, 255,   0 }, /*  2 Green         */
    { 255, 255,   0 }, /*  3 Yellow        */
    {   0,   0, 255 }, /*  4 Blue          */
    { 255,   0, 255 }, /*  5 Magenta       */
    {   0, 255, 255 }, /*  6 Cyan          */
    { 192, 192, 192 }, /*  7 White/Gray    */
    { 128, 128, 128 }, /*  8 Bright Black  */
    { 255, 128, 128 }, /*  9 Bright Red    */
    { 128, 255, 128 }, /* 10 Bright Green  */
    { 255, 255, 128 }, /* 11 Bright Yellow */
    { 128, 128, 255 }, /* 12 Bright Blue   */
    { 255, 128, 255 }, /* 13 Bright Magenta*/
    { 128, 255, 255 }, /* 14 Bright Cyan   */
    { 255, 255, 255 }, /* 15 Bright White  */
};

/* Convert xterm 256-colour index to R,G,B (each 0-255). */
static void xterm256_to_rgb(int idx, int *r, int *g, int *b)
{
    if (idx < 16) {
        if (idx < 0) idx = 0;
        *r = ansi_unix_rgb[idx][0];
        *g = ansi_unix_rgb[idx][1];
        *b = ansi_unix_rgb[idx][2];
    } else if (idx < 232) {
        int n = idx - 16;
        int ri = n / 36, gi = (n % 36) / 6, bi = n % 6;
        *r = ri ? 55 + 40 * ri : 0;
        *g = gi ? 55 + 40 * gi : 0;
        *b = bi ? 55 + 40 * bi : 0;
    } else if (idx < 256) {
        int v = 8 + 10 * (idx - 232);
        *r = *g = *b = v;
    } else {
        *r = *g = *b = 255;
    }
}

/* Resolve xterm 256-colour index to Intuition pen via FindColor(). */
static LONG ansi_resolve_color(int idx, struct Screen *screen, BOOL unixMode)
{
    int r, g, b;
    if (!unixMode)
        return (LONG)(idx & 7);   /* Amiga: direct pen */
    if (!screen)
        return (LONG)(idx & 7);   /* no screen yet: fallback */
    xterm256_to_rgb(idx, &r, &g, &b);
    return (LONG)FindColor(screen->ViewPort.ColorMap,
                           (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24, -1);
}

/* Resolve 24-bit RGB to Intuition pen via FindColor(). */
static LONG ansi_resolve_truecolor(int r8, int g8, int b8, struct Screen *screen)
{
    if (!screen) return -1;
    if (r8 < 0) r8 = 0; if (r8 > 255) r8 = 255;
    if (g8 < 0) g8 = 0; if (g8 > 255) g8 = 255;
    if (b8 < 0) b8 = 0; if (b8 > 255) b8 = 255;
    return (LONG)FindColor(screen->ViewPort.ColorMap,
                           (ULONG)r8 << 24, (ULONG)g8 << 24, (ULONG)b8 << 24, -1);
}

static void ansi_apply_sgr(int *params, int nparams,
                            UBYTE *bold, UBYTE *italic,
                            UBYTE *underline, UBYTE *reverseVideo,
                            LONG *fgPen, LONG *bgPen,
                            struct Screen *screen, BOOL unixColors)
{
    int i, p;
    if (nparams == 0) {
        *bold = *italic = *underline = *reverseVideo = 0;
        *fgPen = *bgPen = -1;
        return;
    }
    for (i = 0; i < nparams; i++) {
        p = params[i];
        switch (p) {
        case  0: *bold=*italic=*underline=*reverseVideo=0; *fgPen=*bgPen=-1; break;
        case  1: *bold        = 1; break;
        case  3: *italic      = 1; break;
        case  4: *underline   = 1; break;
        case  7: *reverseVideo = 1; break;
        case  8: if (*bgPen >= 0) *fgPen = *bgPen; break; /* invisible */
        case 22: *bold        = 0; break;
        case 23: *italic      = 0; break;
        case 24: *underline   = 0; break;
        case 27: *reverseVideo = 0; break;
        case 28: *fgPen = -1; break;
        case 39: *fgPen = -1; break;
        case 49: *bgPen = -1; break;
        case 38: /* extended foreground */
            if (i + 2 < nparams && params[i + 1] == 5) {
                /* 38;5;N  xterm 256-colour */
                *fgPen = ansi_resolve_color(params[i + 2], screen, unixColors);
                i += 2;
            } else if (i + 4 < nparams && params[i + 1] == 2) {
                /* 38;2;r;g;b  true colour */
                *fgPen = ansi_resolve_truecolor(params[i+2], params[i+3], params[i+4], screen);
                i += 4;
            }
            break;
        case 48: /* extended background */
            if (i + 2 < nparams && params[i + 1] == 5) {
                *bgPen = ansi_resolve_color(params[i + 2], screen, unixColors);
                i += 2;
            } else if (i + 4 < nparams && params[i + 1] == 2) {
                *bgPen = ansi_resolve_truecolor(params[i+2], params[i+3], params[i+4], screen);
                i += 4;
            }
            break;
        default:
            if      (p >= 30 && p <= 37)   /* standard fg 0-7 */
                *fgPen = ansi_resolve_color(p - 30, screen, unixColors);
            else if (p >= 40 && p <= 47)   /* standard bg 0-7 */
                *bgPen = ansi_resolve_color(p - 40, screen, unixColors);
            else if (p >= 90 && p <= 97)   /* bright fg 8-15 */
                *fgPen = ansi_resolve_color(p - 90 + 8, screen, unixColors);
            else if (p >= 100 && p <= 107) /* bright bg 8-15 */
                *bgPen = ansi_resolve_color(p - 100 + 8, screen, unixColors);
            break;
        }
    }
}

static void ansi_push_run(UniTextEditorLine *line,
                           ULONG startByte, ULONG endByte,
                           ULONG startChar, ULONG endChar,
                           LONG fgPen, LONG bgPen,
                           UBYTE bold, UBYTE italic,
                           UBYTE underline, UBYTE reverseVideo)
{
    UTEDAnsiRun *run;
    if (line->ansiRunCount >= line->ansiRunAlloc) {
        ULONG newAlloc = line->ansiRunAlloc ? line->ansiRunAlloc * 2 : 4;
        UTEDAnsiRun *newRuns = (UTEDAnsiRun *)AllocVec(
            newAlloc * sizeof(UTEDAnsiRun), MEMF_ANY | MEMF_CLEAR);
        if (!newRuns) return;
        if (line->ansiRuns && line->ansiRunCount)
            CopyMem(line->ansiRuns, newRuns,
                    line->ansiRunCount * sizeof(UTEDAnsiRun));
        if (line->ansiRuns) FreeVec(line->ansiRuns);
        line->ansiRuns     = newRuns;
        line->ansiRunAlloc = newAlloc;
    }
    run = &line->ansiRuns[line->ansiRunCount++];
    run->startByte   = startByte;
    run->endByte     = endByte;
    run->startChar   = startChar;
    run->endChar     = endChar;
    run->fgPen       = fgPen;
    run->bgPen       = bgPen;
    run->style       = (bold   ? (UBYTE)URP_STYLE_BOLD   : 0)
                     | (italic ? (UBYTE)URP_STYLE_ITALIC : 0);
    run->underline   = underline;
    run->reverseVideo = reverseVideo;
    run->_pad        = 0;
}

void uted_line_build_ansi_runs(UniTextEditorLine *line, UniTextEditorData *inst)
{
    const unsigned char *p = (const unsigned char *)line->utf8;
    ULONG byteOff = 0, charOff = 0;
    ULONG runStartByte = 0, runStartChar = 0;
    UBYTE bold = 0, italic = 0, underline = 0, reverseVideo = 0;
    LONG  fgPen = -1, bgPen = -1;
    struct Screen *screen   = inst ? inst->screen      : NULL;
    BOOL          unixColors = inst ? inst->ansiUnixColors : FALSE;

    /* Free any stale run list */
    if (line->ansiRuns) { FreeVec(line->ansiRuns); line->ansiRuns = NULL; }
    line->ansiRunCount = 0;
    line->ansiRunAlloc = 0;

    while (byteOff < line->byteUsed) {
        unsigned char c = p[byteOff];
        BOOL isAmiga, isUnix;

        isAmiga = (c == '*' && byteOff + 1 < line->byteUsed &&
                   (p[byteOff + 1] == 'E' || p[byteOff + 1] == 'e'));
        isUnix  = (c == 0x1B);  /* ESC byte */

        if (isAmiga || isUnix) {
            /* Close the current visible run */
            if (byteOff > runStartByte)
                ansi_push_run(line, runStartByte, byteOff,
                              runStartChar, charOff,
                              fgPen, bgPen, bold, italic, underline, reverseVideo);

            /* Consume prefix: '*E' = 2 chars, ESC = 1 char */
            if (isAmiga) { byteOff += 2; charOff += 2; }
            else         { byteOff += 1; charOff += 1; }

            if (byteOff < line->byteUsed && p[byteOff] == '[') {
                /* CSI sequence */
                int params[16], nparams = 0, cur = 0, hasDigit = 0;
                byteOff++; charOff++;   /* skip '[' */

                while (byteOff < line->byteUsed) {
                    c = p[byteOff];
                    byteOff++; charOff++;
                    if (c >= '0' && c <= '9') {
                        cur = cur * 10 + (c - '0');
                        hasDigit = 1;
                    } else if (c == ';') {
                        if (nparams < 16) params[nparams++] = hasDigit ? cur : 0;
                        cur = 0; hasDigit = 0;
                    } else if (c == '?' || c == '>' || c == '<' || c == '!') {
                        /* private/intermediate marker: skip, keep accumulating */
                    } else {
                        /* Terminating letter */
                        if (hasDigit && nparams < 16) params[nparams++] = cur;
                        if (c == 'm')
                            ansi_apply_sgr(params, nparams, &bold, &italic,
                                           &underline, &reverseVideo, &fgPen, &bgPen,
                                           screen, unixColors);
                        /* All other terminators consumed silently */
                        break;
                    }
                }
            } else if (byteOff < line->byteUsed && p[byteOff] == ']') {
                /* OSC sequence: consume until BEL (0x07) or ESC+'\' */
                byteOff++; charOff++;
                while (byteOff < line->byteUsed) {
                    unsigned char oc = p[byteOff];
                    if (oc == 0x07) { byteOff++; charOff++; break; }
                    if (oc == 0x1B && byteOff + 1 < line->byteUsed
                                   && p[byteOff + 1] == '\\') {
                        byteOff += 2; charOff += 2; break;
                    }
                    byteOff++; charOff++;
                }
            } else if (byteOff < line->byteUsed) {
                /* Other escape sequences: consume one more byte */
                /* Covers: ESCc (RIS), ESC= etc., Amiga *Ec */
                byteOff++; charOff++;
            }

            /* Start a new visible run after the escape */
            runStartByte = byteOff;
            runStartChar = charOff;
        } else {
            byteOff += ansi_seqlen(c);
            charOff++;
        }
    }

    /* Close the final run */
    if (byteOff > runStartByte)
        ansi_push_run(line, runStartByte, byteOff,
                      runStartChar, charOff,
                      fgPen, bgPen, bold, italic, underline, reverseVideo);
}

void uted_line_ensure_ansi_runs(UniTextEditorLine *line, UniTextEditorData *inst)
{
    if (!line->ansiRuns)
        uted_line_build_ansi_runs(line, inst);
}

/* Returns TRUE if charIdx is inside an escape sequence.
 * Sets *seqStart / *seqEnd to the inclusive char range [seqStart..seqEnd). */
BOOL uted_ansi_find_seq(UniTextEditorLine *line, ULONG charIdx,
                         ULONG *seqStart, ULONG *seqEnd)
{
    ULONG i;
    if (!line->ansiRuns) return FALSE;

    /* All chars are sequence chars when run list is empty but line has content */
    if (line->ansiRunCount == 0) {
        if (line->charCount == 0) return FALSE;
        *seqStart = 0; *seqEnd = line->charCount;
        return TRUE;
    }

    /* Before the first run */
    if (charIdx < line->ansiRuns[0].startChar) {
        *seqStart = 0;
        *seqEnd   = line->ansiRuns[0].startChar;
        return TRUE;
    }

    for (i = 0; i < line->ansiRunCount; i++) {
        /* Inside a visible run → not a sequence char */
        if (charIdx >= line->ansiRuns[i].startChar &&
            charIdx <  line->ansiRuns[i].endChar)
            return FALSE;

        /* In the gap between run i and run i+1 */
        if (i + 1 < line->ansiRunCount &&
            charIdx >= line->ansiRuns[i].endChar &&
            charIdx <  line->ansiRuns[i + 1].startChar)
        {
            *seqStart = line->ansiRuns[i].endChar;
            *seqEnd   = line->ansiRuns[i + 1].startChar;
            return TRUE;
        }
    }

    /* After the last run */
    if (charIdx >= line->ansiRuns[line->ansiRunCount - 1].endChar &&
        charIdx <  line->charCount)
    {
        *seqStart = line->ansiRuns[line->ansiRunCount - 1].endChar;
        *seqEnd   = line->charCount;
        return TRUE;
    }

    return FALSE;
}

/* =========================================================================
 * uted_line_build_metrics
 *
 * Populate line->charXOffsets[0..charCount].
 * charXOffsets[i] = x-pixel where char i starts.
 * charXOffsets[charCount] = total advance width of the line.
 *
 * In ANSI mode: escape-sequence chars share the X offset of the char before
 * the sequence (zero visual advance).  Metrics for each visible run are
 * computed via a temporary NUL-terminated copy so the HorizontalOffset
 * function never scans into adjacent escape-sequence bytes.
 * =========================================================================
 */
void uted_line_build_metrics(UniTextEditorLine *line, UniTextEditorData *inst)
{
    struct URPDrawContext *dc = inst ? inst->dc : NULL;

    if (line->charXOffsets) return;
    if (!dc) return;

    line->charXOffsets = (LONG *)AllocVec(
        (line->charCount + 1) * sizeof(LONG), MEMF_ANY | MEMF_CLEAR);
    if (!line->charXOffsets) return;

    if (inst->applyAnsiEscapes) {
        /* ANSI-aware metrics: measure run by run, assign zero advance to
         * escape-sequence characters (chars not in any run). */
        ULONG ri, i;
        LONG  baseX = 0;
        ULONG charFill = 0;

        uted_line_ensure_ansi_runs(line, inst);

        for (ri = 0; ri < line->ansiRunCount; ri++) {
            UTEDAnsiRun *run = &line->ansiRuns[ri];
            ULONG runCharCount = run->endChar  - run->startChar;
            ULONG runByteCount = run->endByte  - run->startByte;

            /* Escape-sequence chars before this run → same X as run start */
            for (i = charFill; i < run->startChar; i++)
                line->charXOffsets[i] = baseX;
            charFill = run->startChar;

            if (runCharCount > 0 && runByteCount > 0) {
                char *tmp = (char *)AllocVec(runByteCount + 1, MEMF_ANY);
                if (tmp) {
                    CopyMem(line->utf8 + run->startByte, tmp, runByteCount);
                    tmp[runByteCount] = '\0';
                    URPDC_SetStyle(dc, (ULONG)run->style);
                    URPDC_HorizontalOffsetArrayUTF8(dc, tmp,
                        (int)(runCharCount + 1),
                        &line->charXOffsets[run->startChar]);
                    URPDC_SetStyle(dc, URP_STYLE_NORMAL);
                    FreeVec(tmp);
                    for (i = 0; i <= runCharCount; i++)
                        line->charXOffsets[run->startChar + i] += baseX;
                    baseX = line->charXOffsets[run->endChar];
                }
                charFill = run->endChar;
            }
        }
        /* Trailing escape-sequence chars (or empty line) */
        for (i = charFill; i <= line->charCount; i++)
            line->charXOffsets[i] = baseX;
    } else {
        URPDC_HorizontalOffsetArrayUTF8(dc, line->utf8,
            line->charCount + 1, line->charXOffsets);
    }
}

/* =========================================================================
 * uted_line_render_chunk
 *
 * Borrow a bitmap from the pool (if the chunk has none), then render the
 * text slice covering [chunkIdx*LINE_CHUNK_WIDTH .. (chunkIdx+1)*LINE_CHUNK_WIDTH).
 *
 * If the pool is exhausted (all borrowed by visible lines) this is a no-op;
 * the render loop will blit background colour for that chunk instead.
 * =========================================================================
 */
void uted_line_render_chunk(UniTextEditorData *inst,
                             UniTextEditorLine *line,
                             ULONG              chunkIdx)
{
    struct RastPort  *rp;
    struct URPTextPos pos;
    ULONG             startChar, startByte;
    LONG              chunkStartPx;
    UWORD             lineHeight = (UWORD)inst->lineHeightBase;
    UWORD             lineAscent = (UWORD)inst->lineAscent;

    if (!inst->dc || lineHeight == 0 || !inst->bmPool.rp) return;

    /* Grow pointer array if chunkIdx is beyond current allocation */
    if (chunkIdx >= line->chunkAlloc) {
        ULONG          newAlloc  = chunkIdx + 8;
        struct BitMap **newChunks = (struct BitMap **)
            AllocVec(newAlloc * sizeof(struct BitMap *), MEMF_ANY | MEMF_CLEAR);
        if (!newChunks) return;

        if (line->chunks && line->chunkAlloc > 0)
            CopyMem(line->chunks, newChunks,
                    line->chunkAlloc * sizeof(struct BitMap *));
        if (line->chunks) FreeVec(line->chunks);

        line->chunks     = newChunks;
        line->chunkAlloc = newAlloc;
    }

    /* Borrow from pool if not currently held */
    if (!line->chunks[chunkIdx]) {
        line->chunks[chunkIdx] = pool_take(&inst->bmPool);
        if (!line->chunks[chunkIdx]) return;  /* pool exhausted */
        line->chunkHeight = lineHeight;
    }

    /* Point the shared RastPort at this tile's bitmap before drawing. */
    rp = inst->bmPool.rp;
    rp->BitMap = line->chunks[chunkIdx];

    /* Fill background */
    SetAPen(rp, (LONG)inst->bgPen);
    SetBPen(rp, (LONG)inst->bgPen);
    SetDrMd(rp, JAM2);
    RectFill(rp, 0, 0, LINE_CHUNK_WIDTH - 1, (LONG)(lineHeight - 1));

    /* Draw text */
    if (line->byteUsed > 0 && inst->screen) {

        /* Ensure metrics so we can skip chars before this chunk */
        if (!line->charXOffsets)
            uted_line_build_metrics(line, inst);

        chunkStartPx = (LONG)chunkIdx * LINE_CHUNK_WIDTH;

        if (inst->applyAnsiEscapes) {
            /* ---- ANSI render: run by run -------------------------------- */
            ULONG ri;
            uted_line_ensure_ansi_runs(line, inst);

            for (ri = 0; ri < line->ansiRunCount; ri++) {
                UTEDAnsiRun *run = &line->ansiRuns[ri];
                LONG fgPen, bgPenRun;
                ULONG sc;
                LONG runEndPx, runStartPx;

                if (run->startChar >= run->endChar) continue;

                runStartPx = line->charXOffsets
                             ? line->charXOffsets[run->startChar] : 0;
                runEndPx   = line->charXOffsets
                             ? line->charXOffsets[run->endChar]   : 0;

                /* Skip run entirely outside this chunk */
                if (runEndPx   <= chunkStartPx)                    continue;
                if (runStartPx >= chunkStartPx + LINE_CHUNK_WIDTH) break;

                /* Find first char in the run to start rendering from */
                sc = run->startChar;
                if (chunkIdx > 0 && line->charXOffsets) {
                    ULONG lo = run->startChar, hi = run->endChar;
                    while (lo < hi) {
                        ULONG mid = (lo + hi) / 2;
                        if (line->charXOffsets[mid] < chunkStartPx) lo = mid + 1;
                        else hi = mid;
                    }
                    sc = (lo > run->startChar) ? lo - 1 : run->startChar;
                }

                startByte = uted_char_to_byte(line->utf8, line->byteUsed, sc);
                startChar = sc;

                pos.x = line->charXOffsets
                        ? (WORD)(line->charXOffsets[startChar] - chunkStartPx)
                        : (WORD)(-chunkStartPx);
                pos.y = (WORD)lineAscent;

                fgPen    = (run->fgPen >= 0) ? run->fgPen : (LONG)inst->txtPen;
                bgPenRun = (run->bgPen >= 0) ? run->bgPen : (LONG)inst->bgPen;
                if (run->reverseVideo) { LONG tmp = fgPen; fgPen = bgPenRun; bgPenRun = tmp; }

                /* Background fill when different from default */
                if (bgPenRun != (LONG)inst->bgPen) {
                    LONG x1 = runStartPx - chunkStartPx;
                    LONG x2 = runEndPx   - chunkStartPx - 1;
                    if (x1 < 0) x1 = 0;
                    if (x2 >= LINE_CHUNK_WIDTH) x2 = LINE_CHUNK_WIDTH - 1;
                    if (x1 <= x2) {
                        SetAPen(rp, bgPenRun);
                        SetDrMd(rp, JAM1);
                        RectFill(rp, x1, 0, x2, (LONG)(lineHeight - 1));
                    }
                }

                URPDC_SetStyle(inst->dc, (ULONG)run->style);
                URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                          fgPen, bgPenRun);
                SetAPen(rp, fgPen);
                SetBPen(rp, bgPenRun);
                SetDrMd(rp, JAM2);

                URPDrawTextUTF8(rp, inst->dc, &pos,
                                line->utf8 + startByte,
                                run->endChar - startChar);

                /* Underline */
                if (run->underline) {
                    LONG x1 = runStartPx - chunkStartPx;
                    LONG x2 = runEndPx   - chunkStartPx - 1;
                    WORD ulY = (WORD)(lineAscent + 1);
                    if (x1 < 0) x1 = 0;
                    if (x2 >= LINE_CHUNK_WIDTH) x2 = LINE_CHUNK_WIDTH - 1;
                    if (x1 <= x2) {
                        SetAPen(rp, fgPen);
                        SetDrMd(rp, JAM1);
                        RectFill(rp, x1, (LONG)ulY, x2, (LONG)ulY);
                    }
                }
            }
            URPDC_SetStyle(inst->dc, URP_STYLE_NORMAL);
            URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                      (LONG)inst->txtPen, (LONG)inst->bgPen);
            SetDrMd(rp, JAM2);
        } else {
            /* ---- Normal render ----------------------------------------- */
            startChar = 0;
            if (chunkIdx > 0 && line->charXOffsets && line->charCount > 0) {
                ULONG lo = 0, hi = line->charCount;
                while (lo < hi) {
                    ULONG mid = (lo + hi) / 2;
                    if ((LONG)line->charXOffsets[mid] < chunkStartPx) lo = mid + 1;
                    else                                               hi = mid;
                }
                startChar = (lo > 0) ? lo - 1 : 0;
            }

            startByte = uted_char_to_byte(line->utf8, line->byteUsed, startChar);

            pos.x = line->charXOffsets
                    ? (WORD)((LONG)line->charXOffsets[startChar] - chunkStartPx)
                    : (WORD)(-chunkStartPx);
            pos.y = (WORD)lineAscent;

            URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                      (LONG)inst->txtPen, (LONG)inst->bgPen);
            SetAPen(rp, (LONG)inst->txtPen);
            SetBPen(rp, (LONG)inst->bgPen);
            SetDrMd(rp, JAM2);

            URPDrawTextUTF8(rp, inst->dc, &pos,
                            line->utf8 + startByte, (ULONG)(-1));
        }
    }
}

/* =========================================================================
 * uted_x_to_char
 *
 * Binary search in charXOffsets → character index nearest to pixel x.
 * =========================================================================
 */
ULONG uted_x_to_char(const UniTextEditorLine *line, WORD pixelX)
{
    ULONG lo, hi, mid;

    if (!line->charXOffsets || line->charCount == 0) return 0;
    if (pixelX <= 0) return 0;
    if ((LONG)pixelX >= line->charXOffsets[line->charCount])
        return line->charCount;

    lo = 0;
    hi = line->charCount;

    while (lo + 1 < hi) {
        mid = (lo + hi) / 2;
        if ((LONG)pixelX < line->charXOffsets[mid]) hi = mid;
        else                                          lo = mid;
    }

    {
        LONG leftEdge  = line->charXOffsets[lo];
        LONG rightEdge = line->charXOffsets[lo + 1];
        LONG midX      = (LONG)((leftEdge + rightEdge) / 2);
        return ((UWORD)pixelX >= midX) ? lo + 1 : lo;
    }
}

/* =========================================================================
 * uted_x_to_char_floor
 *
 * Like uted_x_to_char but always returns lo — the index of the character
 * whose cell contains pixelX — without the midpoint snap.
 * Used to identify which character the user clicked for directional
 * drag-selection anchoring.
 * =========================================================================
 */
ULONG uted_x_to_char_floor(const UniTextEditorLine *line, WORD pixelX)
{
    ULONG lo, hi, mid;

    if (!line->charXOffsets || line->charCount == 0) return 0;
    if (pixelX <= 0) return 0;
    if ((UWORD)pixelX >= line->charXOffsets[line->charCount])
        return line->charCount; /* past end: treat same as charCount */

    lo = 0;
    hi = line->charCount;

    while (lo + 1 < hi) {
        mid = (lo + hi) / 2;
        if ((UWORD)pixelX < line->charXOffsets[mid]) hi = mid;
        else                                          lo = mid;
    }
    return lo;
}

/* =========================================================================
 * uted_free_wrap_map
 * =========================================================================
 */
void uted_free_wrap_map(UniTextEditorData *inst)
{
    if (inst->wrapMap) { FreeVec(inst->wrapMap); inst->wrapMap = NULL; }
    inst->wrapRowCount = 0;
    inst->wrapMapAlloc = 0;
    inst->wrapMapDirty = inst->wordWrap; /* needs rebuild when re-enabled */
}

/* =========================================================================
 * uted_rebuild_wrap_map
 *
 * Walks every logical line, builds its charXOffsets (metrics) if absent,
 * then segments it into visual rows of at most gadWidth pixels wide.
 * Wrap prefers the last space/tab; falls back to last fitting character.
 * Called lazily at the start of GM_RENDER when wrapMapDirty is TRUE.
 * =========================================================================
 */

/* Grow wrapMap to hold at least (inst->wrapRowCount + 1) entries. */
static BOOL wrap_map_grow(UniTextEditorData *inst)
{
    ULONG     newAlloc = inst->wrapMapAlloc ? inst->wrapMapAlloc * 2 : 32;
    UTEDWrapRow *newMap = (UTEDWrapRow *)AllocVec(
        newAlloc * sizeof(UTEDWrapRow), MEMF_ANY | MEMF_CLEAR);
    if (!newMap) return FALSE;
    if (inst->wrapMap && inst->wrapRowCount)
        CopyMem(inst->wrapMap, newMap,
                inst->wrapRowCount * sizeof(UTEDWrapRow));
    if (inst->wrapMap) FreeVec(inst->wrapMap);
    inst->wrapMap      = newMap;
    inst->wrapMapAlloc = newAlloc;
    return TRUE;
}

void uted_rebuild_wrap_map(UniTextEditorData *inst)
{
    UniTextEditorLine *line;
    ULONG logIdx;
    ULONG gadW;

    uted_free_wrap_map(inst);

    if (!inst->wordWrap || inst->gadWidth <= 0 || !inst->dc) {
        inst->wrapMapDirty = FALSE;
        return;
    }

    gadW   = (ULONG)inst->gadWidth;
    logIdx = 0;
    line   = (UniTextEditorLine *)inst->lines.mlh_Head;

    while (line->node.mln_Succ) {
        ULONG rowStartChar;

        /* Metrics must exist for wrap-point computation */
        if (!line->charXOffsets)
            uted_line_build_metrics(line, inst);

        /* Empty line → single visual row with zero width */
        if (line->charCount == 0 || !line->charXOffsets) {
            if (inst->wrapRowCount >= inst->wrapMapAlloc)
                if (!wrap_map_grow(inst)) goto done;
            inst->wrapMap[inst->wrapRowCount].logicalLine = logIdx;
            inst->wrapMap[inst->wrapRowCount].startChar   = 0;
            inst->wrapMap[inst->wrapRowCount].endChar     = 0;
            inst->wrapMap[inst->wrapRowCount].startPixel  = 0;
            inst->wrapRowCount++;
            goto next_line;
        }

        rowStartChar = 0;
        while (rowStartChar <= line->charCount) {
            ULONG rowStartPx   = line->charXOffsets[rowStartChar];
            ULONG rowTargetPx  = rowStartPx + gadW;
            ULONG rowEndChar;   /* first char NOT on this visual row */
            ULONG wrapAt;

            /* Find last charXOffsets[i] <= rowTargetPx (binary search) */
            {
                ULONG lo = rowStartChar, hi = line->charCount;
                while (lo < hi) {
                    ULONG mid = (lo + hi + 1) / 2;
                    if ((ULONG)line->charXOffsets[mid] <= rowTargetPx) lo = mid;
                    else                                                hi = mid - 1;
                }
                rowEndChar = lo; /* last char whose left edge fits */
            }

            /* Force at least one char per visual row to avoid infinite loops */
            if (rowEndChar <= rowStartChar)
                rowEndChar = rowStartChar + 1;

            if (rowEndChar >= line->charCount) {
                /* Remainder fits: last visual row for this logical line */
                if (inst->wrapRowCount >= inst->wrapMapAlloc)
                    if (!wrap_map_grow(inst)) goto done;
                inst->wrapMap[inst->wrapRowCount].logicalLine = logIdx;
                inst->wrapMap[inst->wrapRowCount].startChar   = rowStartChar;
                inst->wrapMap[inst->wrapRowCount].endChar     = line->charCount;
                inst->wrapMap[inst->wrapRowCount].startPixel  = rowStartPx;
                inst->wrapRowCount++;
                break;
            }

            /* Search forward for last space/tab in [rowStartChar, rowEndChar).
             * We track the char index *after* each space/tab so the space
             * is included on the current row and the next row begins after it. */
            wrapAt = rowEndChar; /* hard wrap if no space found */
            {
                ULONG byteI = uted_char_to_byte(
                    line->utf8, line->byteUsed, rowStartChar);
                ULONG si;
                for (si = rowStartChar; si < rowEndChar; si++) {
                    unsigned char c = (unsigned char)line->utf8[byteI];
                    if (c == ' ' || c == '\t')
                        wrapAt = si + 1;   /* wrap after this space/tab */
                    if      (c < 0x80) byteI += 1;
                    else if (c < 0xE0) byteI += 2;
                    else if (c < 0xF0) byteI += 3;
                    else               byteI += 4;
                }
            }

            if (inst->wrapRowCount >= inst->wrapMapAlloc)
                if (!wrap_map_grow(inst)) goto done;
            inst->wrapMap[inst->wrapRowCount].logicalLine = logIdx;
            inst->wrapMap[inst->wrapRowCount].startChar   = rowStartChar;
            inst->wrapMap[inst->wrapRowCount].endChar     = wrapAt;
            inst->wrapMap[inst->wrapRowCount].startPixel  = rowStartPx;
            inst->wrapRowCount++;

            rowStartChar = wrapAt;
        }

next_line:
        logIdx++;
        line = (UniTextEditorLine *)line->node.mln_Succ;
    }

done:
    inst->wrapMapDirty = FALSE;
}

/* =========================================================================
 * uted_cursor_visual_row
 *
 * Returns the visual row index for the current cursor position.
 * Falls back to cursor.line (logical) when the wrap map is absent/dirty.
 * =========================================================================
 */
ULONG uted_cursor_visual_row(UniTextEditorData *inst)
{
    ULONG i, best;

    if (!inst->wordWrap || !inst->wrapMap || inst->wrapRowCount == 0)
        return inst->cursor.line;

    best = 0;
    for (i = 0; i < inst->wrapRowCount; i++) {
        if (inst->wrapMap[i].logicalLine == inst->cursor.line) {
            best = i;
            if (inst->cursor.ch < inst->wrapMap[i].endChar ||
                inst->wrapMap[i].endChar == inst->wrapMap[i].startChar)
                return i;
        } else if (inst->wrapMap[i].logicalLine > inst->cursor.line) {
            break;
        }
    }
    return best;
}
