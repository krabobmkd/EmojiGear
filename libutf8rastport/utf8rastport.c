
/*
 * utf8rastport.c  –  implementation of the UTF-8 RastPort rendering library.
 *
 * Rendering path:
 *   FreeType2 → URPGlyphCache (MONO / GRAY / RGBA, scaled to point size)
 *             → RastPort via BltTemplate() [MONO]
 *                         or future GRAY / RGBA alpha-blend renderers.
 *
 * Glyph cache
 * -----------
 * Each codepoint is rendered once and stored in a hash table keyed by
 * codepoint value.  The cache is flushed when preferences change or the
 * caller explicitly calls URPDC_FlushGlyphCache().
 *
 * Pixel-format selection (per glyph, at cache-fill time):
 *   FT returns BGRA (color emoji)  → URP_CACHE_RGBA  (always)
 *   URP_PREF_ANTIALIAS set         → URP_CACHE_GRAY
 *   otherwise                      → URP_CACHE_MONO
 *
 * Scaling
 * -------
 * For scalable fonts FreeType renders at exactly the requested point size,
 * so no post-processing is needed.
 * For bitmap-only fonts (e.g. NotoColorEmoji CBDT/CBLC) FreeType returns
 * the nearest available strike, which may differ from the requested size.
 * The cache-fill step scales with nearest-neighbour to bring it to the
 * correct pixel dimensions before storing.
 */
#include "urp_internal.h"   /* URPDrawContext, URPGlyphEntry, URPGlyphCache, URPFontEntry */

#include <proto/graphics.h>
#include <proto/exec.h>
#include <proto/cybergraphics.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <cybergraphx/cybergraphics.h>
#include <graphics/layers.h>
#include <utility/hooks.h>

#include "urp_cgx_blend.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

void URPDC_FlushGlyphCache(REG(a0, struct URPDrawContext *dc));


#include "bdbprintf.h"

/* define collision, an infinite source of problems... */
#ifdef textPen
#undef textPen
#endif
/* =========================================================================
 * UTF-8 decoder
 * ========================================================================= */

INLINE unsigned long urp_utf8_next(
        REG(a0,const unsigned char **pp),
        REG(a1,int *remaining) )
{
    const unsigned char *p = *pp;
    unsigned char c;
    unsigned long cp;
    int extra;

    if (*remaining == 0) return 0;

    c = *p++;
    if (c == 0) return 0;

    if (c < 0x80) {
        *pp = p; (*remaining)--; return (unsigned long)c;
    }

    if      ((c & 0xE0) == 0xC0) { cp = (unsigned long)(c & 0x1F); extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = (unsigned long)(c & 0x0F); extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = (unsigned long)(c & 0x07); extra = 3; }
    else {
        *pp = p; (*remaining)--; return URP_REPLACEMENT_CHAR;
    }

    while (extra-- > 0) {
        c = *p;
        if ((c & 0xC0) != 0x80) {
            *pp = p; (*remaining)--; return URP_REPLACEMENT_CHAR;
        }
        cp = (cp << 6) | (unsigned long)(c & 0x3F);
        p++;
    }

    *pp = p; (*remaining)--;
    return cp;
}


/* =========================================================================
 * Face size helper
 * ========================================================================= */

/*
 * For scalable fonts: FT_Set_Char_Size at the requested point size.
 * For bitmap-only fonts (no outlines, e.g. CBDT emoji): FT_Select_Size
 * choosing the strike whose height is closest to the requested point size.
 */
static int urp_set_face_size(struct URPFontEntry *fe)
{
    FT_Error err;
    int i, best, diff, bestDiff, target;

    if (!FT_IS_SCALABLE(fe->face) && fe->face->num_fixed_sizes > 0) {
        target   = fe->pointSize;
        best     = 0;
        bestDiff = abs(fe->face->available_sizes[0].height - target);
        for (i = 1; i < fe->face->num_fixed_sizes; i++) {
            diff = abs(fe->face->available_sizes[i].height - target);
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        err = FT_Select_Size(fe->face, best);
        return (err == 0);
    }

    err = FT_Set_Char_Size(fe->face, 0,
                           (FT_F26Dot6)(fe->pointSize * 64),
                           URP_DPI_X, URP_DPI_Y);
    return (err == 0);
}


/* =========================================================================
 * Glyph cache – hash helpers
 * ========================================================================= */

INLINE ULONG urp_hash_cp(ULONG cp)
{
    cp ^= (cp >> 13);
    return cp & 0x000000ff; /* cp % URP_GLYPH_HASH; */
}

/* Fold style bits into hash so styled entries land in different buckets. */
INLINE ULONG urp_hash_cp_style(ULONG cp, UBYTE style)
{
    return urp_hash_cp(cp ^ ((ULONG)style << 21));
}

INLINE struct URPGlyphEntry *urp_cache_lookup(struct URPGlyphCache *cache,
                                              ULONG cp, UBYTE style)
{
    struct URPGlyphEntry *e = cache->buckets[urp_hash_cp_style(cp, style)];
    while (e) {
        if (e->codepoint == cp && e->style == style) return e;
        e = e->next;
    }
    return NULL;
}

static void urp_cache_insert(struct URPGlyphCache *cache,
                             struct URPGlyphEntry *e)
{
    ULONG h = urp_hash_cp_style(e->codepoint, e->style);
    e->next = cache->buckets[h];
    cache->buckets[h] = e;
}

static void urp_cache_free_all(struct URPGlyphCache *cache)
{
    int i; // nbcltbmfreed=0,nbmskfreed=0;
    struct URPGlyphEntry *e, *next;


    /* in case there is some rendering on bitmap */
    WaitBlit();

    for (i = 0; i < URP_GLYPH_HASH; i++) {
        e = cache->buckets[i];
        if(e)
        {
        while (e) {
            next = e->next;
            if (e->pixels)      FreeVec(e->pixels);
            if (e->clutBitmap)
            {
          //  nbcltbmfreed++;
              FreeBitMap(e->clutBitmap);
            }
            if (e->maskBitmap)
            {
         //   nbmskfreed++;
              FreeBitMap(e->maskBitmap);
            }
            if (e->chk_clutBitmap)
            {
                FreeVec(e->chk_clutBitmap);
            }
            FreeVec(e);
            e = next;
        }
        }
        cache->buckets[i] = NULL;
    }

}

/* Free only the CLUT/mask bitmap mirrors from all cache entries, leaving the
 * main pixel data intact.  Called when the palette or draw colour changes. */
static void urp_flush_clut_bitmaps(struct URPGlyphCache *cache)
{
    int i;
    struct URPGlyphEntry *e;

    /* wait pending draw to end - must be done before any FreeBitMap(); */
    WaitBlit();

    for (i = 0; i < URP_GLYPH_HASH; i++) {
        for (e = cache->buckets[i]; e; e = e->next) {
            if (e->clutBitmap) { FreeBitMap(e->clutBitmap); e->clutBitmap = NULL; }
            if (e->maskBitmap) { FreeBitMap(e->maskBitmap); e->maskBitmap = NULL; }
            if (e->chk_clutBitmap) { FreeVec(e->chk_clutBitmap); e->chk_clutBitmap = NULL; }
        }
    }

}


/* =========================================================================
 * Glyph cache – fill (FreeType -> cache entry)
 * ========================================================================= */

/*
 * urp_blit_to_cache()
 *
 * Convert a FreeType bitmap (any pixel mode) into the requested cache pixel
 * format, with nearest-neighbour scaling from (src->width x src->rows) to
 * (dstW x dstH).
 *
 * dst must be pre-zeroed (AllocVec MEMF_CLEAR is sufficient).
 *
 * Implementation notes
 * --------------------
 * Only two integer divisions are performed, up-front, to derive the 16:16
 * fixed-point step values dx / dy.  Inside the loops the source coordinate
 * is simply `accumX >> 16` with `accumX += dx` per column (same for Y).
 * This is safe on 68020 which has no FPU and where division is expensive.
 *
 * The outer switch selects a dedicated inner-loop function for each
 * (dst-format, src-pixel-mode) pair so the per-pixel dispatch is gone.
 */

/* =========================================================================
 * dst = MONO (1 bit/pixel, MSB-first, word-aligned rows)
 * ========================================================================= */

static void blit_mono_to_mono(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dstRow = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            int srcX = (int)(accumX >> 16);
            if (((srcRow[srcX >> 3] >> (7 - (srcX & 7))) & 1)) {
                dstRow[x >> 3] |= (UBYTE)(1 << (7 - (x & 7)));
            }
            accumX += dx;
        }
        accumY += dy;
    }
}

static void blit_gray_to_mono(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dstRow = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            int srcX = (int)(accumX >> 16);
            if (srcRow[srcX] >= 128)
                dstRow[x >> 3] |= (UBYTE)(1 << (7 - (x & 7)));
            accumX += dx;
        }
        accumY += dy;
    }
}

static void blit_bgra_to_mono(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dstRow = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            int srcX = (int)(accumX >> 16);
            if (srcRow[srcX * 4 + 3] >= 128)
                dstRow[x >> 3] |= (UBYTE)(1 << (7 - (x & 7)));
            accumX += dx;
        }
        accumY += dy;
    }
}

/* =========================================================================
 * dst = GRAY (1 byte/pixel alpha)
 * ========================================================================= */

static void blit_mono_to_gray(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dstRow = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            int srcX = (int)(accumX >> 16);
            dstRow[x] = ((srcRow[srcX >> 3] >> (7 - (srcX & 7))) & 1) ? 0xFF : 0;
            accumX += dx;
        }
        accumY += dy;
    }
}

static void blit_gray_to_gray(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dp     = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            *dp++ = srcRow[(int)(accumX >> 16)];
            accumX += dx;
        }
        accumY += dy;
    }
}

static void blit_bgra_to_gray(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dp     = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            *dp++ = srcRow[(int)(accumX >> 16) * 4 + 3]; /* alpha channel */
            accumX += dx;
        }
        accumY += dy;
    }
}

/* =========================================================================
 * dst = RGBA (4 bytes/pixel, stored R,G,B,A)
 * ========================================================================= */

static void blit_mono_to_rgba(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dp     = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            int   srcX = (int)(accumX >> 16);
            UBYTE a    = ((srcRow[srcX >> 3] >> (7 - (srcX & 7))) & 1) ? 0xFF : 0;
            dp[0] = dp[1] = dp[2] = dp[3] = a;
            dp += 4;
            accumX += dx;
        }
        accumY += dy;
    }
}

static void blit_gray_to_rgba(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = src->buffer + (int)(accumY >> 16) * src->pitch;
        UBYTE       *dp     = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            dp[0] = dp[1] = dp[2] = 0xFF;
            dp[3] = srcRow[(int)(accumX >> 16)];
            dp += 4;
            accumX += dx;
        }
        accumY += dy;
    }
}

/* Raw BGRA→RGBA blit from an arbitrary buffer (used by both regular and HQ paths). */
static void blit_bgra_to_rgba_raw(const UBYTE *srcBuf, int srcPitch,
                                   UBYTE *dst,
                                   int dstW, int dstH, int dstPitch,
                                   ULONG dx, ULONG dy)
{
    ULONG accumY = 0;
    int y;
    for (y = 0; y < dstH; y++) {
        const UBYTE *srcRow = srcBuf + (int)(accumY >> 16) * srcPitch;
        UBYTE       *dp     = dst + y * dstPitch;
        ULONG accumX = 0;
        int x;
        for (x = 0; x < dstW; x++) {
            const UBYTE *sp = srcRow + (int)(accumX >> 16) * 4;
            dp[0] = sp[2]; /* R ← B */
            dp[1] = sp[1]; /* G */
            dp[2] = sp[0]; /* B ← R */
            dp[3] = sp[3]; /* A (pre-multiplied by FreeType) */
            dp += 4;
            accumX += dx;
        }
        accumY += dy;
    }
}

static void blit_bgra_to_rgba(FT_Bitmap *src, UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               ULONG dx, ULONG dy)
{
    blit_bgra_to_rgba_raw(src->buffer, src->pitch,
                           dst, dstW, dstH, dstPitch, dx, dy);
}

/* =========================================================================
 * halve_bgra
 *
 * 2× box-filter downscale of a BGRA image: each output pixel is the
 * average of the 2×2 source block.  Output dimensions = (srcW/2)×(srcH/2).
 *
 * Writing to the same buffer as reading is safe when dst == src: output row N
 * uses source rows 2N and 2N+1, and the output occupies 1/4 the source
 * footprint, so the write head never catches the read head.
 * ========================================================================= */
static void halve_bgra(const UBYTE *src, int srcW, int srcH, int srcPitch,
                        UBYTE *dst, int dstPitch)
{
    const UBYTE *srcY = src;
    UBYTE       *dstY = dst;
    int y, x;
    for (y = 0; y < srcH / 2; y++) {
        const UBYTE *row0 = srcY;
        const UBYTE *row1 = srcY + srcPitch;
        UBYTE       *dp   = dstY;
        for (x = 0; x < srcW / 2; x++) {

            *dp++ = (UBYTE)((row0[0] + row0[4 + 0]
                        + row1[0] + row1[4 + 0]) >> 2);
            *dp++ = (UBYTE)((row0[1] + row0[4 + 1]
                        + row1[1] + row1[4 + 1]) >> 2);
            *dp++ = (UBYTE)((row0[2] + row0[4 + 2]
                        + row1[2] + row1[4 + 2]) >> 2);
            *dp++ = (UBYTE)((row0[3] + row0[4 + 3]
                        + row1[3] + row1[4 + 3]) >> 2);


            row0 += 8; /* advance 2 source pixels */
            row1 += 8;
        }
        srcY += srcPitch * 2;
        dstY += dstPitch;
    }
}

/* =========================================================================
 * blit_bgra_to_rgba_HQ
 *
 * High-quality BGRA→RGBA with pyramid downscaling (URP_PREF_HIGHFILTERING).
 * Called only when scale-down exceeds 2× in at least one dimension.
 *
 * Algorithm:
 *   1. Iteratively halve with halve_bgra (2×2 box average) until both
 *      dimensions are within 2× of the target.  The first halving reads
 *      from src->buffer and writes to dc->hqScratch.  All further halvings
 *      are in-place into dc->hqScratch (safe: output is 1/4 the input size).
 *   2. Finish with blit_bgra_to_rgba_raw for the final ≤2× NN step.
 *
 * Falls back to regular NN on allocation failure.
 * ========================================================================= */
static void blit_bgra_to_rgba_HQ(struct URPDrawContext *dc,
                                   FT_Bitmap *src,
                                   UBYTE *dst,
                                   int dstW, int dstH, int dstPitch,
                                   ULONG dx, ULONG dy)
{
    const UBYTE *curBuf   = src->buffer;
    int          curPitch = (int)src->pitch;
    int          curW     = (int)src->width;
    int          curH     = (int)src->rows;

//bdbprintf("blit_bgra_to_rgba_HQ w:%d h:%d > %d %d\n",curW,curH,dstW,dstH);

    while (curW > dstW * 2 || curH > dstH * 2) {
        int   halfW  = curW / 2;
        int   halfH  = curH / 2;
        ULONG needed = (ULONG)halfW * (ULONG)halfH * 4;

        if (halfW < 1 || halfH < 1) break;

        /* Grow scratch only on first iteration (curBuf == src->buffer).
         * Subsequent halvings are in-place and never exceed initial size. */
        if (needed > dc->hqScratchBytes) {
            if (dc->hqScratch) { FreeVec(dc->hqScratch); dc->hqScratch = NULL; }
            dc->hqScratch = (UBYTE *)AllocVec(needed, MEMF_ANY);
            dc->hqScratchBytes = dc->hqScratch ? needed : 0;
            if (!dc->hqScratch) goto fallback;
        }

        halve_bgra(curBuf, curW, curH, curPitch,
                   dc->hqScratch, halfW * 4);

        curBuf   = dc->hqScratch;
        curPitch = halfW * 4;
        curW     = halfW;
        curH     = halfH;

        /* Recompute fixed-point steps for the reduced source */
        dx = (dstW > 0) ? ((ULONG)curW << 16) / (ULONG)dstW : 0;
        dy = (dstH > 0) ? ((ULONG)curH << 16) / (ULONG)dstH : 0;
    }

    blit_bgra_to_rgba_raw(curBuf, curPitch, dst, dstW, dstH, dstPitch, dx, dy);
    return;

fallback:
    blit_bgra_to_rgba_raw(src->buffer, (int)src->pitch,
                           dst, dstW, dstH, dstPitch,
                           (dstW > 0) ? ((ULONG)src->width << 16) / (ULONG)dstW : 0,
                           (dstH > 0) ? ((ULONG)src->rows  << 16) / (ULONG)dstH : 0);
}

/* =========================================================================
 * urp_blit_to_cache – dispatch wrapper
 * ========================================================================= */
static void urp_blit_to_cache(struct URPDrawContext *dc,
                               FT_Bitmap *src,
                               UBYTE *dst,
                               int dstW, int dstH, int dstPitch,
                               int cachePixFmt)
{
    int   srcW = (int)src->width;
    int   srcH = (int)src->rows;
    /* Two divisions, done once.  Result fits in ULONG: srcW/H <= ~4096,
     * so srcW<<16 <= 268 435 456 which is well below 2^32. */
    ULONG dx = (dstW > 0) ? ((ULONG)srcW << 16) / (ULONG)dstW : 0;
    ULONG dy = (dstH > 0) ? ((ULONG)srcH << 16) / (ULONG)dstH : 0;

    switch (cachePixFmt) {
        case URP_CACHE_MONO:
            switch (src->pixel_mode) {
                case FT_PIXEL_MODE_MONO: blit_mono_to_mono(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                case FT_PIXEL_MODE_GRAY: blit_gray_to_mono(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                case FT_PIXEL_MODE_BGRA: blit_bgra_to_mono(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                default: break;
            }
            break;
        case URP_CACHE_GRAY:
            switch (src->pixel_mode) {
                case FT_PIXEL_MODE_MONO: blit_mono_to_gray(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                case FT_PIXEL_MODE_GRAY: blit_gray_to_gray(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                case FT_PIXEL_MODE_BGRA: blit_bgra_to_gray(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                default: break;
            }
            break;
        case URP_CACHE_RGBA:
            switch (src->pixel_mode) {
                case FT_PIXEL_MODE_MONO: blit_mono_to_rgba(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                case FT_PIXEL_MODE_GRAY: blit_gray_to_rgba(src,dst,dstW,dstH,dstPitch,dx,dy); break;
                case FT_PIXEL_MODE_BGRA:
                    /* Use pyramid downscaling when HQ flag is set and either
                     * dimension is being shrunk by more than 2× (dx or dy > 2.0
                     * in 16:16 fixed point = 2<<16 = 131072). */
                    if ((dc->prefFlags & URP_PREF_HIGHFILTERING)
                        && (dx > (2UL << 16) || dy > (2UL << 16)))
                        blit_bgra_to_rgba_HQ(dc,src,dst,dstW,dstH,dstPitch,dx,dy);
                    else
                        blit_bgra_to_rgba(src,dst,dstW,dstH,dstPitch,dx,dy);
                    break;
                default: break;
            }
            break;
        default: break;
    }
}


/*
 * urp_fill_cache_entry()
 *
 * Load glyph gi from font fe, convert it to the appropriate cache pixel
 * format (determined by dc->prefFlags and the FT pixel mode), scale to the
 * requested point size if the font is bitmap-only, and return a heap-
 * allocated URPGlyphEntry ready to be inserted into the cache.
 *
 * Returns NULL if the glyph cannot be loaded or memory allocation fails.
 */
static struct URPGlyphEntry *urp_fill_cache_entry(struct URPDrawContext *dc,
                                                   struct URPFontEntry   *fe,
                                                   FT_UInt               gi,
                                                   ULONG                 cp)
{
    FT_GlyphSlot          slot;
    FT_Bitmap            *bm;
    struct URPGlyphEntry *entry;
    int   cachePixFmt;
    int   load_flags;
    int   srcW, srcH, dstW, dstH, pitch;
    int   cellH, scaleNum, scaleDen;
    UBYTE *pixels;

    /* Choose FreeType load flags.
     * FT_LOAD_TARGET_MONO asks the outline rasteriser to produce a 1-bit bitmap.
     * It must NOT be set for bitmap-only fonts (e.g. CBDT/CBLC color emoji):
     * doing so causes FreeType to convert the decoded BGRA bitmap to MONO,
     * hiding the color data and preventing URP_CACHE_RGBA from being chosen.
     * For bitmap-only fonts FT_LOAD_COLOR alone is sufficient. */
    load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
    if (!(dc->prefFlags & URP_PREF_ANTIALIAS) && FT_IS_SCALABLE(fe->face))
        load_flags |= FT_LOAD_TARGET_MONO;

    /* For scalable fonts with active style, load outline only then transform+render. */
    if (dc->currentStyle && FT_IS_SCALABLE(fe->face)) {
        int norender_flags = FT_LOAD_COLOR;
        if (!(dc->prefFlags & URP_PREF_ANTIALIAS))
            norender_flags |= FT_LOAD_TARGET_MONO;
        if (FT_Load_Glyph(fe->face, gi, norender_flags))
            return NULL;
        slot = fe->face->glyph;
        if (dc->currentStyle & URP_STYLE_BOLD)   FT_GlyphSlot_Embolden(slot);
        if (dc->currentStyle & URP_STYLE_ITALIC)  FT_GlyphSlot_Oblique(slot);
        {
            FT_Render_Mode rmode = (norender_flags & FT_LOAD_TARGET_MONO)
                                   ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL;
            if (FT_Render_Glyph(slot, rmode)) return NULL;
        }
    } else {
        if (FT_Load_Glyph(fe->face, gi, load_flags))
            return NULL;
        slot = fe->face->glyph;
    }
    bm   = &slot->bitmap;

    /* Choose cache pixel format */
    if (bm->pixel_mode == FT_PIXEL_MODE_BGRA) {
        cachePixFmt = URP_CACHE_RGBA;
    } else if (dc->prefFlags & URP_PREF_ANTIALIAS) {
        cachePixFmt = URP_CACHE_GRAY;
    } else {
        cachePixFmt = URP_CACHE_MONO;
    }

    srcW = (int)bm->width;
    srcH = (int)bm->rows;

    /* Compute scale for bitmap-only fonts.
     * Scalable fonts render at exactly pointSize; no scaling needed.
     * Bitmap-only fonts (e.g. NotoColorEmoji CBDT) return the nearest strike's
     * native size, which we scale proportionally using the face cell height. */
    scaleNum = 1;
    scaleDen = 1;
    if (!FT_IS_SCALABLE(fe->face) && fe->face->num_fixed_sizes > 0) {
        cellH = (int)(fe->face->size->metrics.height >> 6);
        if (cellH > 0 && cellH != fe->pointSize) {
            scaleNum = fe->pointSize;
            scaleDen = cellH;
        }
    }

    /* Compute destination dimensions */
    if (srcW == 0 || srcH == 0) {
        dstW = 0;
        dstH = 0;
    } else {
        dstW = (srcW * scaleNum + scaleDen / 2) / scaleDen;
        dstH = (srcH * scaleNum + scaleDen / 2) / scaleDen;
        if (dstW < 1) dstW = 1;
        if (dstH < 1) dstH = 1;
    }

    /* Compute row pitch */
    switch (cachePixFmt) {
        case URP_CACHE_MONO: pitch = ((dstW + 15) >>4) * 2; break; /* word-aligned */
        case URP_CACHE_GRAY: pitch = dstW;                    break;
        case URP_CACHE_RGBA: pitch = dstW * 4;                break;
        default:             pitch = dstW;
    }

    entry = (struct URPGlyphEntry *)AllocVec(sizeof(*entry), MEMF_CLEAR);
    if (!entry) return NULL;

    pixels = NULL;
    if (dstW > 0 && dstH > 0) {
        pixels = (UBYTE *)AllocVec((ULONG)(pitch * dstH), MEMF_CLEAR);
        if (!pixels) { FreeVec(entry); return NULL; }

        urp_blit_to_cache(dc, bm, pixels, dstW, dstH, pitch, cachePixFmt);
    }

    entry->codepoint = cp;
    entry->style     = dc->currentStyle;
    entry->pixelFmt  = cachePixFmt;
    entry->width     = (WORD)dstW;
    entry->rows      = (WORD)dstH;
    entry->bearingX  = (WORD)((slot->bitmap_left  * scaleNum + scaleDen / 2) / scaleDen);
    entry->bearingY  = (WORD)((slot->bitmap_top   * scaleNum + scaleDen / 2) / scaleDen);
    entry->advanceX  = (WORD)(((slot->advance.x >> 6) * scaleNum + scaleDen / 2) / scaleDen);
    entry->pitch     = (WORD)pitch;
    entry->pixels    = pixels;
    entry->next      = NULL;
 //   entry->pixelsIsInChip = FALSE; /* may realloc in chip*/



    return entry;
}


/* =========================================================================
 * "Missing glyph" tofu box
 * ========================================================================= */

/* Set one pixel in a MONO (MSB-first) bitmap. */
static void urp_mono_set_pixel(UBYTE *buf, int pitch, int x, int y)
{
    buf[y * pitch + (x >> 3)] |= (UBYTE)(0x80 >> (x & 7));
}

/* Set a horizontal run of pixels in a MONO bitmap. */
static void urp_mono_hline(UBYTE *buf, int pitch, int y, int x0, int x1)
{
    int x;
    for (x = x0; x <= x1; x++)
        urp_mono_set_pixel(buf, pitch, x, y);
}

/*
 * urp_make_notfound_entry()
 *
 * Synthesise a hollow-rectangle "tofu" box sized to the primary font's
 * em-height and return it as a MONO cache entry keyed to URP_CP_NOTFOUND.
 * Called once; subsequent missing codepoints reuse the same cached entry.
 */
static struct URPGlyphEntry *urp_make_notfound_entry(struct URPDrawContext *dc)
{
    struct URPGlyphEntry *entry;
    UBYTE *pixels;
    int primarySize, boxW, boxH, pitch, y;

    primarySize = (dc->numFonts > 0) ? dc->fonts[0].pointSize : 16;

    /* ~70% of line height tall, slightly narrower than tall */
    boxH = (primarySize * 7 + 5) / 10;
    boxW = (boxH * 6 + 5) / 10;
    if (boxW < 3) boxW = 3;
    if (boxH < 3) boxH = 3;

    pitch = ((boxW + 15) / 16) * 2; /* word-aligned MONO */

    entry = (struct URPGlyphEntry *)AllocVec(sizeof(*entry), MEMF_CLEAR);
    if (!entry) return NULL;

    pixels = (UBYTE *)AllocVec((ULONG)(pitch * boxH), MEMF_CLEAR);
    if (!pixels) { FreeVec(entry); return NULL; }

    /* Top and bottom rows */
    urp_mono_hline(pixels, pitch, 0,        0, boxW - 1);
    urp_mono_hline(pixels, pitch, boxH - 1, 0, boxW - 1);
    /* Left and right columns */
    for (y = 1; y < boxH - 1; y++) {
        urp_mono_set_pixel(pixels, pitch, 0,        y);
        urp_mono_set_pixel(pixels, pitch, boxW - 1, y);
    }

    entry->codepoint = URP_CP_NOTFOUND;
    entry->pixelFmt  = URP_CACHE_MONO;
    entry->width     = (WORD)boxW;
    entry->rows      = (WORD)boxH;
    entry->bearingX  = 1;
    entry->bearingY  = (WORD)boxH; /* box sits entirely above the baseline */
    entry->advanceX  = (WORD)(boxW + 2);
    entry->pitch     = (WORD)pitch;
    entry->pixels    = pixels;
    entry->next      = NULL;

    return entry;
}


/* =========================================================================
 * CLUT / planar bitmap helpers
 * ========================================================================= */

/* Ensure PIXFMT_LUT8 is defined even if the CGX header omits it. */
#ifndef PIXFMT_LUT8
#define PIXFMT_LUT8  0
#endif

/*
 * urp_rebuild_aa_remap()
 *
 * Precompute the 16-entry AA shade table for GRAY glyph rendering on CLUT
 * screens.  Entry i interpolates linearly in RGB from bgColor (i=0) to
 * drawColor (i=15), then maps through clutRemap[].
 * Must be called whenever a colour or the screen palette changes.
 * No-op if clutValid is not yet set.
 */
static void urp_rebuild_aa_remap(struct URPDrawContext *dc)
{
    int i;

    if (!dc->clutValid) return;
    /* i=0 (alpha=0): use the exact background pen, bypassing clutRemap[] so
     * we never accidentally remap it to a neighbouring colour. */

    for (i = 0; i < 16; i++) {
        UBYTE r = (UBYTE)(((int)dc->background.argb.R * (15 - i) + (int)dc->draw.argb.R * i) / 15);
        UBYTE g = (UBYTE)(((int)dc->background.argb.G * (15 - i) + (int)dc->draw.argb.G * i) / 15);
        UBYTE b = (UBYTE)(((int)dc->background.argb.B * (15 - i) + (int)dc->draw.argb.B * i) / 15);
        dc->aaRemap[i] = dc->clutRemap[
            ((ULONG)(r >> 4) << 8) |
            ((ULONG)(g >> 4) << 4) |
             (ULONG)(b >> 4)];
    }
    if(dc->bgPen>=0)
    {       
        dc->aaRemap[0] = dc->bgPen;
    }

    if(dc->txtPen>=0)
    {
        dc->aaRemap[15] = dc->txtPen;
    }

}

/*
 * urp_commit_clut_bitmaps()
 *
 * Common finalisation step for urp_build_clut_bitmaps_gray/_rgba.
 * Allocates clutBitmap + maskBitmap, fills them from the caller-supplied
 * chunky (CLUT indices) and maskBuf (1-byte-per-pixel opacity flags) arrays,
 * then frees both temporary buffers.
 * Returns 1 on success, 0 on failure.
 */
static int urp_commit_clut_bitmaps(struct URPDrawContext *dc,
                                    struct URPGlyphEntry  *ge,
                                    UBYTE *chunky, UBYTE *maskBuf)
{
    ULONG  maskBpr;
    UBYTE *maskPlane;
    struct RastPort tmpRp;
    ULONG  x, y;

    ge->clutBitmap = AllocBitMap((ULONG)ge->width, (ULONG)ge->rows,
                                 dc->currentFriendBitmap->Depth, BMF_CLEAR,
                                 dc->currentFriendBitmap);
    if (!ge->clutBitmap) {
        FreeVec(chunky);
        FreeVec(maskBuf);
        return 0;
    }

    if((dc->prefFlags & URP_PREF_CLUTMODE_NOMASK) == 0)
    {
        ge->maskBitmap = AllocBitMap((ULONG)ge->width, (ULONG)ge->rows,
                                 1, BMF_CLEAR, NULL);
    } else  ge->maskBitmap =  NULL;

    /* Write CLUT indices into clutBitmap via WriteChunkyPixels().
     * Works for both planar (OCS/ECS/AGA) and chunky (CGX/P96) bitmaps. */
    InitRastPort(&tmpRp);
    tmpRp.BitMap = ge->clutBitmap;
    WriteChunkyPixels(&tmpRp, 0, 0,
                      (LONG)ge->width - 1, (LONG)ge->rows - 1,
                      chunky, (LONG)ge->width);

    /* Write transparency mask bits. */
    if(ge->maskBitmap)
    {
        maskBpr   = ge->maskBitmap->BytesPerRow;
        maskPlane = (UBYTE *)ge->maskBitmap->Planes[0];
        if (maskPlane) {
            for (y = 0; y < (ULONG)ge->rows; y++) {
                const UBYTE *maskBufRow = maskBuf   + y * (ULONG)ge->width; /* Y-constant */
                UBYTE       *maskPlRow  = maskPlane + y * maskBpr;           /* Y-constant */
                for (x = 0; x < (ULONG)ge->width; x++) {
                    if (maskBufRow[x])
                        maskPlRow[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
                }
            }
        }
    }

    FreeVec(chunky);
    FreeVec(maskBuf);
    return 1;
}

/*
 * urp_build_clut_bitmaps_gray()
 *
 * Build CLUT-remapped bitmap + mask for a URP_CACHE_GRAY glyph entry.
 * Maps each alpha byte through the precomputed 16-entry AA shade ramp
 * (dc->aaRemap[alpha>>4]) so the glyph is drawn in the current draw colour
 * anti-aliased against pen 0 (background).
 */
static int urp_build_clut_bitmaps_gray(struct URPDrawContext *dc,
                                        struct URPGlyphEntry  *ge)
{
    UBYTE *chunky, *maskBuf;
    ULONG  size, x, y;
    UBYTE bgpen;

    if (!dc->currentFriendBitmap || !dc->clutValid) return 0;
    if (ge->width <= 0 || ge->rows <= 0 || !ge->pixels) return 0;

    /* wait pending draw to end - must be done before any FreeBitMap(); */
    if(ge->clutBitmap || ge->maskBitmap)
    {
        WaitBlit();
    }

    if (ge->clutBitmap) { FreeBitMap(ge->clutBitmap); ge->clutBitmap = NULL; }
    if (ge->maskBitmap) { FreeBitMap(ge->maskBitmap); ge->maskBitmap = NULL; }
    if (ge->chk_clutBitmap) { FreeVec(ge->chk_clutBitmap); ge->chk_clutBitmap = NULL; }

    size    = (ULONG)ge->width * (ULONG)ge->rows;
    chunky  = (UBYTE *)AllocVec(size, MEMF_CLEAR);
    if (!chunky) return 0;

    bgpen = dc->aaRemap[0];

    if(dc->saveChipMode)
    {
        /* GRAY antialias: 1 byte per pixel = alpha.
         * Use the precomputed 16-entry AA shade ramp: aaRemap[alpha>>4]. */
        for (y = 0; y < (ULONG)ge->rows; y++) {
            const UBYTE *srcRow  = ge->pixels + y * (ULONG)ge->pitch; /* Y-constant */
            UBYTE       *dstRow  = chunky     + y * (ULONG)ge->width;  /* Y-constant */
            for (x = 0; x < (ULONG)ge->width; x++) {
                UBYTE alpha = srcRow[x];
                dstRow[x]  = dc->aaRemap[alpha >> 4];
            }
        }

        /* only keep remaped table pixel array, do not create Amiga Bitmap
            only use WritePixel at drawings (experimental)
         */
        ge->chk_clutBitmap = chunky;

        return TRUE;
    } else
    {

        maskBuf = (UBYTE *)AllocVec(size, MEMF_CLEAR);
        if (!maskBuf) { FreeVec(chunky); return 0; }

        /* GRAY antialias: 1 byte per pixel = alpha.
         * Use the precomputed 16-entry AA shade ramp: aaRemap[alpha>>4]. */
        for (y = 0; y < (ULONG)ge->rows; y++) {
            const UBYTE *srcRow  = ge->pixels + y * (ULONG)ge->pitch; /* Y-constant */
            UBYTE       *dstRow  = chunky     + y * (ULONG)ge->width;  /* Y-constant */
            UBYTE       *maskRow = maskBuf    + y * (ULONG)ge->width;  /* Y-constant */
            for (x = 0; x < (ULONG)ge->width; x++) {
                UBYTE alpha = srcRow[x];
                if (alpha > 16) {
                    dstRow[x]  = dc->aaRemap[alpha >> 4];
                    maskRow[x] = 1;
                } else {
                    dstRow[x]  = bgpen;
                }
            }
        }

        return urp_commit_clut_bitmaps(dc, ge, chunky, maskBuf);
    }
}

/*
 * urp_build_clut_bitmaps_rgba()
 *
 * Build CLUT-remapped bitmap + mask for a URP_CACHE_RGBA glyph entry.
 * Maps each RGB triple through dc->clutRemap[] (nearest-pen lookup).
 * The aaRemap shade ramp is not used here; colour comes from the glyph data.
 */
static int urp_build_clut_bitmaps_rgba(struct URPDrawContext *dc,
                                        struct URPGlyphEntry  *ge)
{
    UBYTE *chunky, *maskBuf;
    ULONG  size, x, y;
    UBYTE bgpen;
    if (!dc->currentFriendBitmap || !dc->clutValid) return 0;
    if (ge->width <= 0 || ge->rows <= 0 || !ge->pixels) return 0;

    /* wait pending draw to end - must be done before any FreeBitMap(); */
    if(ge->clutBitmap || ge->maskBitmap)
    {
        WaitBlit();
    }

    if (ge->clutBitmap) { FreeBitMap(ge->clutBitmap); ge->clutBitmap = NULL; }
    if (ge->maskBitmap) { FreeBitMap(ge->maskBitmap); ge->maskBitmap = NULL; }
    if (ge->chk_clutBitmap) { FreeVec(ge->chk_clutBitmap); ge->chk_clutBitmap = NULL; }

    size    = (ULONG)ge->width * (ULONG)ge->rows;
    chunky  = (UBYTE *)AllocVec(size, MEMF_CLEAR);
    if (!chunky) return 0;

    bgpen = dc->aaRemap[0];

    if(dc->saveChipMode)
    {
        /* full AllocBitMap() mode */
        /* RGBA: 4 bytes per pixel = R,G,B,A (FreeType pre-multiplied). */
        for (y = 0; y < (ULONG)ge->rows; y++) {
            const UBYTE *srcRow  = ge->pixels + y * (ULONG)ge->pitch; /* Y-constant */
            UBYTE       *dstRow  = chunky     + y * (ULONG)ge->width;  /* Y-constant */
            for (x = 0; x < (ULONG)ge->width; x++) {
                const UBYTE *sp = srcRow + x * 4UL;
                if (sp[3] > 192) {
                    dstRow[x] = dc->clutRemap[
                        ((ULONG)(sp[0] >> 4) << 8) |
                        ((ULONG)(sp[1] >> 4) << 4) |
                         (ULONG)(sp[2] >> 4)];
                } else dstRow[x] = bgpen;
            }
        }
        /* only keep remaped table pixel array, do not create Amiga Bitmap
            only use WritePixel at drawings (experimental)
         */
        ge->chk_clutBitmap = chunky;

        return 1;
    } else
    {
        maskBuf = (UBYTE *)AllocVec(size, MEMF_CLEAR);
        if (!maskBuf) { FreeVec(chunky); return 0; }

        /* full AllocBitMap() mode */
        /* RGBA: 4 bytes per pixel = R,G,B,A (FreeType pre-multiplied). */
        for (y = 0; y < (ULONG)ge->rows; y++) {
            const UBYTE *srcRow  = ge->pixels + y * (ULONG)ge->pitch; /* Y-constant */
            UBYTE       *dstRow  = chunky     + y * (ULONG)ge->width;  /* Y-constant */
            UBYTE       *maskRow = maskBuf    + y * (ULONG)ge->width;  /* Y-constant */
            for (x = 0; x < (ULONG)ge->width; x++) {
                const UBYTE *sp = srcRow + x * 4UL;
                if (sp[3] > 192) {
                    dstRow[x] = dc->clutRemap[
                        ((ULONG)(sp[0] >> 4) << 8) |
                        ((ULONG)(sp[1] >> 4) << 4) |
                         (ULONG)(sp[2] >> 4)];
                    maskRow[x] = 1;
                } else dstRow[x] = bgpen;
            }
        }

        return urp_commit_clut_bitmaps(dc, ge, chunky, maskBuf);
    }

}


/* =========================================================================
 * API implementation
 * ========================================================================= */

struct URPDrawContext *URPDC_Create(REG(a0, const char *name))
{
    struct URPDrawContext *dc;
    FT_Error err;
    dc = (struct URPDrawContext *)AllocVec(sizeof(*dc), MEMF_PUBLIC|MEMF_CLEAR);
    if (!dc) return NULL;

    err = FT_Init_FreeType(&dc->library);
    if (err) { FreeVec(dc); return NULL; }

    InitSemaphore(&dc->sem);

    dc->numFonts     = 0;
    dc->currentFriendBitmap = NULL;
    dc->prefFlags    = 0;
    dc->draw.ARGB    = 0x00FFFFFF;
    dc->bgPen = dc->txtPen = -1;
    dc->tabSpaces    = 4;

    dc->saveChipMode = TRUE; /* experimental */

    memset(dc->fonts,  0, sizeof(dc->fonts));
    memset(&dc->cache, 0, sizeof(dc->cache));

    dc->useCount = 1;

    return dc;
}


void URPDC_Retain(REG(a0, struct URPDrawContext *dc))
{
    if (!dc) return;

    dc->useCount++;
}
void URPDC_Release(REG(a0, struct URPDrawContext *dc))
{
    int i;
    if (!dc) return;

    if(dc->useCount>0 ) dc->useCount--;

    /* if some other object still use it, do not delete */
    if(dc->useCount>0 ) return;

    ObtainSemaphore(&dc->sem);

    urp_cache_free_all(&dc->cache);
    if(dc->tempChipRamAlloc)
    {
        FreeVec(dc->tempChipRamAlloc);
        dc->tempChipRamAlloc = NULL;
    }

    for (i = 0; i < dc->numFonts; i++) {
        if (dc->fonts[i].face) {
            FT_Done_Face(dc->fonts[i].face);
            dc->fonts[i].face = NULL;
        }
    }

    if (dc->hqScratch) { FreeVec(dc->hqScratch); dc->hqScratch = NULL; }

    FT_Done_FreeType(dc->library);

    ReleaseSemaphore(&dc->sem);
    FreeVec(dc);
}


/*
 * urp_font_magic_ok()
 *
 * Read the first 4 bytes of a file and confirm it starts with a recognised
 * TrueType / OpenType magic.  Returns FALSE for anything else (HTML pages,
 * corrupt downloads, wrong file) so FT_New_Face is never called on garbage
 * data — which would cause out-of-bounds memory access and a hard crash on
 * Amiga (no memory protection).
 *
 * Recognised tags:
 *   \x00\x01\x00\x00  TrueType 1.0 outline
 *   OTTO              OpenType / CFF
 *   true              Apple TrueType
 *   typ1              Obsolete Type 1 wrapper
 *   ttcf              TrueType Collection
 *   wOFF              WOFF
 */
static BOOL urp_font_magic_ok(const char *path)
{
    UBYTE magic[4] = {0, 0, 0, 0};
    BPTR  fh       = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return FALSE;
    Read(fh, magic, 4);
    Close(fh);
    if (magic[0]==0x00 && magic[1]==0x01 && magic[2]==0x00 && magic[3]==0x00) return TRUE;
    if (magic[0]=='O' && magic[1]=='T' && magic[2]=='T' && magic[3]=='O')     return TRUE;
    if (magic[0]=='t' && magic[1]=='r' && magic[2]=='u' && magic[3]=='e')     return TRUE;
    if (magic[0]=='t' && magic[1]=='y' && magic[2]=='p' && magic[3]=='1')     return TRUE;
    if (magic[0]=='t' && magic[1]=='t' && magic[2]=='c' && magic[3]=='f')     return TRUE;
    if (magic[0]=='w' && magic[1]=='O' && magic[2]=='F' && magic[3]=='F')     return TRUE;
    return FALSE;
}

int URPDC_AddFont(REG(a0, struct URPDrawContext *dc),
                  REG(a1, const char           *fontPath),
                  REG(d0, int                   pointSize),
                  REG(d1, ULONG                 flags))
{
    struct URPFontEntry *fe;
    FT_Error err;
    char tfontpath[256];

    if (!dc || !fontPath) return 0;
    if (dc->numFonts >= URP_MAX_FONTS) return 0;

    ObtainSemaphore(&dc->sem);

    fe = &dc->fonts[dc->numFonts];

    /* first try FONTS:fontname.ttf */
    tfontpath[0]=0;
    strcat(tfontpath,"FONTS:");
    strcat(tfontpath,fontPath);

    err = 1;
    if (urp_font_magic_ok(tfontpath))
        err = FT_New_Face(dc->library, tfontpath, 0, &fe->face);
    if(err!=0)
    {
        /* then try progdir */
        tfontpath[0]=0;
        strcat(tfontpath,"PROGDIR:fonts/");
        strcat(tfontpath,fontPath);
        err = 1;
        if (urp_font_magic_ok(tfontpath))
            err = FT_New_Face(dc->library, tfontpath, 0, &fe->face);
    }
    if(err!=0)
    {
        /* then only try raw current path */
        err = 1;
        if (urp_font_magic_ok(fontPath))
            err = FT_New_Face(dc->library, fontPath, 0, &fe->face);
    }

    if (err) {
    ReleaseSemaphore(&dc->sem);
        return 0;
    }

    /* Explicitly request the Unicode charmap.  FreeType usually auto-selects
     * it, but some CJK fonts carry Shift-JIS / Big5 charmaps alongside
     * Unicode and the auto-selection can land on the wrong one.  Ignore the
     * error: if no Unicode cmap exists FreeType keeps its default choice. */
    FT_Select_Charmap(fe->face, FT_ENCODING_UNICODE);

    strncpy(fe->path, fontPath, URP_PATH_MAX - 1);
    fe->path[URP_PATH_MAX - 1] = '\0';
    fe->pointSize = pointSize;
    fe->flags     = flags;

    urp_set_face_size(fe);

    dc->numFonts++;
    dc->monoAdvanceX = 0; /* invalidate: primary font may have changed */
    ReleaseSemaphore(&dc->sem);
    return 1;
}


void URPDC_RemoveFont(REG(a0, struct URPDrawContext *dc),
                      REG(a1, const char           *fontPath),
                      REG(d0, int                   pointSize))
{
    int i, j;
    if (!dc || !fontPath) return;

    ObtainSemaphore(&dc->sem);
    for (i = 0; i < dc->numFonts; i++) {
        if (dc->fonts[i].pointSize == pointSize &&
            strcmp(dc->fonts[i].path, fontPath) == 0)
        {
            FT_Done_Face(dc->fonts[i].face);
            for (j = i; j < dc->numFonts - 1; j++)
                dc->fonts[j] = dc->fonts[j + 1];
            dc->numFonts--;
            memset(&dc->fonts[dc->numFonts], 0, sizeof(dc->fonts[0]));
            dc->monoAdvanceX = 0;
    ReleaseSemaphore(&dc->sem);
            return;
        }
    }
    ReleaseSemaphore(&dc->sem);
}

void URPDC_FlushFonts(REG(a0, struct URPDrawContext *dc))
{
    int i;

    if (!dc) return;
    ObtainSemaphore(&dc->sem);
    for (i = 0; i < dc->numFonts; i++) {
        if (dc->fonts[i].face)
            FT_Done_Face(dc->fonts[i].face);
    }
    memset(dc->fonts, 0, sizeof(dc->fonts));
    dc->numFonts     = 0;
    dc->monoAdvanceX = 0;
    urp_cache_free_all(&dc->cache);
    ReleaseSemaphore(&dc->sem);
}

void URPDC_ChangeFontsSize(REG(a0, struct URPDrawContext *dc),
                                REG(d0,ULONG nPointSize),
                                REG(d1,ULONG fontMask)
                                )
{
    int i;
    ULONG fontMaskBit=1;
    ULONG anyChange=FALSE;
    if(!dc) return;
    if(nPointSize<8) nPointSize=8;
    if(nPointSize>64) nPointSize=64;

    ObtainSemaphore(&dc->sem);
     for (i = 0; i < dc->numFonts; i++) {
        if ((fontMaskBit &fontMask) &&
            dc->fonts[i].face &&
             dc->fonts[i].pointSize != nPointSize )
        {
            dc->fonts[i].pointSize = nPointSize;
            urp_set_face_size(&dc->fonts[i]);
            anyChange = TRUE;
        }
        fontMaskBit<<=1;
    }
    if(anyChange) {
        dc->monoAdvanceX = 0;
        urp_cache_free_all(&dc->cache);
    }
    ReleaseSemaphore(&dc->sem);
}




void URPDC_SetPreferenceFlags(REG(a0, struct URPDrawContext *dc), REG(d0, ULONG flags))
{
    if (!dc) return;

    if (dc->prefFlags != flags) {
        dc->prefFlags    = flags;
        dc->monoAdvanceX = 0; /* invalidate: FORCE_MONOSPACE may have changed */
        urp_cache_free_all(&dc->cache); /* flush: pixel format may have changed */
    }
}


ULONG URPDC_GetPreferenceFlags(REG(a0, const struct URPDrawContext *dc))
{
    if (!dc) return 0;
    return dc->prefFlags;
}


void URPDC_FlushGlyphCache(REG(a0, struct URPDrawContext *dc))
{
    if (!dc)
    {
        // trick
        flushbdbprint();
        return;
    }
    ObtainSemaphore(&dc->sem);
    urp_cache_free_all(&dc->cache);
    ReleaseSemaphore(&dc->sem);
}

void URPDC_SetStyle(REG(a0, struct URPDrawContext *dc), REG(d0, ULONG styleBits))
{
    if (!dc) return;
    ObtainSemaphore(&dc->sem);
    dc->currentStyle = (UBYTE)(styleBits & (URP_STYLE_BOLD | URP_STYLE_ITALIC));
    ReleaseSemaphore(&dc->sem);
}

void  URPDC_SetDrawColor(REG(a0, struct URPDrawContext *dc),
                         REG(d0, ULONG textRGB), REG(d1, ULONG backgroundRGB))
{
    if (!dc) return;
    /*important or flush clut cache constantly */
    if(textRGB == dc->draw.ARGB && dc->background.ARGB == backgroundRGB) return;
    dc->draw.ARGB = textRGB;
    dc->background.ARGB = backgroundRGB;

    /* GRAY CLUT bitmaps are baked with the draw colour; flush them so they
     * are rebuilt with the new colour on the next render call. */
    urp_flush_clut_bitmaps(&dc->cache);

    urp_rebuild_aa_remap(dc);
}

/* same but use your screen color index setting */
void  URPDC_SetDrawColorFromPen(
                    REG(a0, struct URPDrawContext *dc),
                    REG(a1, struct Screen *screen),
                    REG(d0, LONG txtPen),
                    REG(d1, LONG backgroundpen)
                    )
{
    ULONG RGB32Colors[3];
    int colorsDiffers = 0;

    if (!dc || !screen )
    {
        dc->currentFriendBitmap = NULL;
        return;
    }
    if(txtPen>=0 && screen->ViewPort.ColorMap)
    {
        GetRGB32(screen->ViewPort.ColorMap,txtPen,1,&RGB32Colors[0]);
        dc->draw.argb.R = RGB32Colors[0]>>24;
        dc->draw.argb.G = RGB32Colors[1]>>24;
        dc->draw.argb.B = RGB32Colors[2]>>24;
    }
    if( dc->txtPen != txtPen || (dc->pensSet==0)) colorsDiffers=1;
    dc->txtPen = txtPen;


    if(backgroundpen>=0 &&  screen->ViewPort.ColorMap )
    {
        GetRGB32(screen->ViewPort.ColorMap,backgroundpen,1,&RGB32Colors[0]);

        dc->background.argb.R = RGB32Colors[0]>>24;
        dc->background.argb.G = RGB32Colors[1]>>24;
        dc->background.argb.B = RGB32Colors[2]>>24;
    }

    if( dc->bgPen != backgroundpen || (dc->pensSet==0)) colorsDiffers=1;
    dc->bgPen = backgroundpen;

    dc->pensSet = TRUE;
    if(colorsDiffers)
    {
        urp_flush_clut_bitmaps(&dc->cache);
        urp_rebuild_aa_remap(dc);
    }

}
/* Internal implementation shared by URPDC_SetDrawScreen and URPDC_UpdateColorMap.
 * Must be called with dc->sem already held. */
static ULONG urp_update_color_map(struct URPDrawContext *dc, struct Screen *screen)
{
    UBYTE clut_r[256], clut_g[256], clut_b[256];
    ULONG rgb[3];
    int   numColors, i, j;
    int   bestDist, bestIdx, dist, dr, dg, db, depth;

    if (!dc || !screen || !screen->ViewPort.ColorMap) return 0;

    depth = GetBitMapAttr(screen->RastPort.BitMap,BMA_DEPTH);

    numColors = (int)screen->ViewPort.ColorMap->Count;
    if(depth<=8 && numColors> (1L<<depth)) numColors = 1L<<depth;

    if (numColors > 256) numColors = 256;

    /* Read all palette entries once */
    for (i = 0; i < numColors; i++) {
        GetRGB32(screen->ViewPort.ColorMap, (ULONG)i, 1, rgb);
        clut_r[i] = (UBYTE)(rgb[0] >> 24);
        clut_g[i] = (UBYTE)(rgb[1] >> 24);
        clut_b[i] = (UBYTE)(rgb[2] >> 24);
    }

    /* For each of the 4096 possible RGB444 values find the nearest pen */
    for (j = 0; j < 4096; j++) {
        /* Expand 4-bit channels to 8-bit by nibble-replication */
        int r4 = (j >> 8) & 0xF;
        int g4 = (j >> 4) & 0xF;
        int b4 =  j       & 0xF;
        int r8 = (r4 << 4) | r4;
        int g8 = (g4 << 4) | g4;
        int b8 = (b4 << 4) | b4;

        bestDist = 0x7FFFFFFF;
        bestIdx  = 0;
        for (i = 0; i < numColors; i++) {
            dr = r8 - (int)clut_r[i];
            dg = g8 - (int)clut_g[i];
            db = b8 - (int)clut_b[i];
            dist = dr*dr + dg*dg + db*db;
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx  = i;
                if (dist == 0) break; /* exact match, no need to continue */
            }
        }
        dc->clutRemap[j] = (UBYTE)bestIdx;
    }

    dc->clutValid = 1;
    urp_rebuild_aa_remap(dc);

    /* Old CLUT bitmaps were built with the previous palette; discard them */
    urp_flush_clut_bitmaps(&dc->cache);

    return TRUE;
}

/* like URPDC_UpdateColorMap(), but only act if screen changed */
ULONG URPDC_SetDrawScreen(REG(a0, struct URPDrawContext *dc), REG(a1, struct Screen *screen))
{
    ULONG ndepth;
    ULONG result = 0;

    if (!dc || !screen || !screen->RastPort.BitMap) return 0;

    ObtainSemaphore(&dc->sem);
    ndepth = GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);
    if (dc->lastScreen != screen || ndepth != dc->lastScreenDepth) {
        dc->lastScreen      = screen;
        dc->lastScreenDepth = ndepth;
        result = urp_update_color_map(dc, screen);
    }
    ReleaseSemaphore(&dc->sem);
    return result;
}

/*
 * URPDC_UpdateColorMap() – rebuild the RGB444→CLUT pen remap table from the
 * current screen palette and discard all cached CLUT glyph bitmaps so they
 * are regenerated on the next draw call.
 *
 * The remap table has 4096 entries, one for each possible combination of
 * 4-bit red, green and blue values (index = (R>>4)<<8 | (G>>4)<<4 | (B>>4)).
 * For each entry the nearest screen pen is found by minimising the squared
 * Euclidean distance in RGB space after expanding the 4-bit channels back to
 * 8 bits via replication (e.g. R4=0xA → R8=0xAA).
 */
ULONG URPDC_UpdateColorMap(REG(a0, struct URPDrawContext *dc), REG(a1, struct Screen *screen))
{
    ULONG result;
    if (!dc) return 0;
    ObtainSemaphore(&dc->sem);
    result = urp_update_color_map(dc, screen);
    ReleaseSemaphore(&dc->sem);
    return result;
}

/*
 * URPDC_RemapRGB24ToPen8() – bulk RGB24→CLUT pen remap using the table
 * built by URPDC_UpdateColorMap()/URPDC_SetDrawScreen(). Same nearest-pen
 * lookup formula as the internal glyph remapper
 * (urp_build_clut_bitmaps_rgba() above): index = (R>>4)<<8 | (G>>4)<<4 |
 * (B>>4) into dc->clutRemap[].
 *
 * srcRGB is pixelCount tightly-packed 3-byte (R,G,B) pixels (e.g. a decoded
 * thumbnail); dstPen receives one pen byte per pixel, suitable for
 * graphics.library/WritePixelArray8(). Intended for depth<=8 screens where
 * cybergraphics.library/ScalePixelArray()'s own RECTFMT_LUT8 path isn't
 * available/desired (see FriendSh3ep/PlanToReworkThumbnails.txt step 2-3).
 *
 * Returns 0 (nothing written) if dc/srcRGB/dstPen is NULL or the colour map
 * hasn't been built yet (dc->clutValid == 0) -- call
 * URPDC_UpdateColorMap()/URPDC_SetDrawScreen() first. Returns pixelCount on
 * success.
 */
ULONG URPDC_RemapRGB24ToPen8(REG(a0, struct URPDrawContext *dc),
                              REG(a1, CONST UBYTE *srcRGB),
                              REG(a2, UBYTE *dstPen),
                              REG(d0, ULONG pixelCount))
{
    ULONG i;

    if (!dc || !srcRGB || !dstPen) return 0;
    if (!dc->clutValid) return 0;

    for (i = 0; i < pixelCount; i++) {
        const UBYTE *sp = srcRGB + i * 3UL;
        dstPen[i] = dc->clutRemap[
            ((ULONG)(sp[0] >> 4) << 8) |
            ((ULONG)(sp[1] >> 4) << 4) |
             (ULONG)(sp[2] >> 4)];
    }

    return pixelCount;
}


/* Forward declaration – defined later in this file */
static struct URPGlyphEntry *urp_get_glyph(struct URPDrawContext *dc,
                                            unsigned long cp,
                                            struct URPFontEntry **fe_out,
                                            FT_UInt             *gi_out);

/* =========================================================================
 * URPDC_SetAttribsA – tag/value list attribute setter
 * ========================================================================= */

void URPDC_SetAttribsA(REG(a0, struct URPDrawContext *dc),
                       REG(a1, ULONG               *taglist))
{
    ULONG *p = taglist;
    if (!dc || !taglist) return;
    while (*p != TAG_DONE) {
        ULONG t = *p++;
        ULONG v = *p++;
        switch (t) {
            case URPDCA_TabSpaces:
                if (v < 1)  v = 1;
                if (v > 12) v = 12;
                dc->tabSpaces = v;
                break;
        }
    }
}

/* =========================================================================
 * urp_tab_advance – pixel advance width for one tab character
 *
 * Returns tabSpaces * advanceX-of-space.
 * Falls back to tabSpaces * 8 if the space glyph isn't available.
 * ========================================================================= */

/* Compute the monospace cell width from the advance of 'M' in the primary font.
 * Falls back to half the point size if 'M' is not available. */
static void urp_compute_mono_advance(struct URPDrawContext *dc)
{
    struct URPGlyphEntry *ge = urp_get_glyph(dc, (unsigned long)'A', NULL, NULL);
    if (ge && ge->advanceX > 0)
        dc->monoAdvanceX = ge->advanceX;
    else
        dc->monoAdvanceX = (WORD)(dc->numFonts > 0 ? dc->fonts[0].pointSize / 2 : 8);
}

/* Return the advance to use for a glyph: fixed cell in FORCE_MONOSPACE mode,
 * natural glyph advance otherwise. */
INLINE WORD urp_glyph_advance(struct URPDrawContext *dc,
                                     const struct URPGlyphEntry *ge)
{

    if (dc->prefFlags & URP_PREF_FORCE_MONOSPACE) {
        if (dc->monoAdvanceX <= 0)
            urp_compute_mono_advance(dc);
        if (dc->monoAdvanceX > 0)
            return dc->monoAdvanceX;
    }
    return ge->advanceX;
}

INLINE WORD urp_tab_advance(struct URPDrawContext *dc)
{
    if (dc->prefFlags & URP_PREF_FORCE_MONOSPACE) {
        if (dc->monoAdvanceX <= 0)
            urp_compute_mono_advance(dc);
        if (dc->monoAdvanceX > 0)
            return (WORD)((ULONG)dc->tabSpaces * (ULONG)dc->monoAdvanceX);
    }
    {
        struct URPGlyphEntry *ge = urp_get_glyph(dc, 0x20UL, NULL, NULL);
        if (ge && ge->advanceX > 0)
            return (WORD)((ULONG)dc->tabSpaces * (ULONG)ge->advanceX);
    }
    return (WORD)((ULONG)dc->tabSpaces * 8UL);
}

static WORD urp_font_ascender(struct URPDrawContext *dc)
{
    int i, asc, maxAscend = 0;
    for (i = 0; i < dc->numFonts; i++) {
        struct URPFontEntry *fe = &dc->fonts[i];
        int scaleNum = 1, scaleDen = 1;
        if (!fe->face) continue;
        urp_set_face_size(fe);
        asc = (int)(fe->face->size->metrics.ascender >> 6);
        if (!FT_IS_SCALABLE(fe->face) && fe->face->num_fixed_sizes > 0) {
            int cellH = (int)(fe->face->size->metrics.height >> 6);
            if (cellH > 0 && cellH != fe->pointSize) {
                scaleNum = fe->pointSize;
                scaleDen = cellH;
            }
            if (asc <= 0) asc = cellH;
        }
        asc = (asc * scaleNum + scaleDen / 2) / scaleDen;
        if (asc > maxAscend) maxAscend = asc;
    }
    return (WORD)(maxAscend > 0 ? maxAscend : 8);
}

static WORD urp_line_height(struct URPDrawContext *dc)
{
    int i, asc, dsc, maxAscend = 0, maxDescend = 0;
    for (i = 0; i < dc->numFonts; i++) {
        struct URPFontEntry *fe = &dc->fonts[i];
        FT_Pos raw_dsc;
        int scaleNum = 1, scaleDen = 1;
        if (!fe->face) continue;
        urp_set_face_size(fe);
        asc = (int)(fe->face->size->metrics.ascender >> 6);
        raw_dsc = fe->face->size->metrics.descender;
        dsc = (int)((-raw_dsc) >> 6);
        if (!FT_IS_SCALABLE(fe->face) && fe->face->num_fixed_sizes > 0) {
            int cellH = (int)(fe->face->size->metrics.height >> 6);
            if (cellH > 0 && cellH != fe->pointSize) {
                scaleNum = fe->pointSize;
                scaleDen = cellH;
            }
            if (asc <= 0) { asc = cellH; dsc = 0; }
        }
        if (dsc < 0) dsc = 0;
        asc = (asc * scaleNum + scaleDen / 2) / scaleDen;
        dsc = (dsc * scaleNum + scaleDen / 2) / scaleDen;
        if (asc > maxAscend)  maxAscend  = asc;
        if (dsc > maxDescend) maxDescend = dsc;
    }
    return (WORD)((maxAscend + maxDescend > 0) ? maxAscend + maxDescend : 8);
}


void URPDC_TextSizeUTF8(REG(a0, struct URPDrawContext *dc),
                        REG(a1, const char           *utf8),
                        REG(d0, int                   maxChars),
                        REG(a2, struct URPTextMetric *out))
{
    const unsigned char  *p;
    int                   remaining;
    WORD                  totalAdvance = 0;
    WORD                  maxAscender  = 0;
    WORD                  maxDescender = 0;
    unsigned long         cp;
    int                   i;
    struct URPFontEntry  *fe;
    FT_UInt               gi;
    struct URPGlyphEntry *ge;

    if (!out) return;
    out->width = out->height = out->baseX = out->baseY = 0;
    if (!dc || !utf8) return;

    ObtainSemaphore(&dc->sem);
    p         = (const unsigned char *)utf8;
    remaining = (maxChars < 0) ? INT_MAX : maxChars;

    {
    WORD maxWidth  = 0;
    int  lineCount = 1;

    while (1) {
        cp = urp_utf8_next(&p, &remaining);
        if (cp == 0) break;

        /* Newline: end current line, start a new one */
        if (cp == 0x0a) {
            if (totalAdvance > maxWidth) maxWidth = totalAdvance;
            totalAdvance = 0;
            lineCount++;
            continue;
        }

        /* Tab: fixed advance = tabSpaces * space advance */
        if (cp == 0x09) {
            totalAdvance += (WORD)urp_tab_advance(dc);
            continue;
        }

        fe = NULL; gi = 0;
        for (i = 0; i < dc->numFonts; i++) {
            gi = FT_Get_Char_Index(dc->fonts[i].face, (FT_ULong)cp);
            if (gi != 0) { fe = &dc->fonts[i]; break; }
        }
        if (!fe) {
            /* Count the tofu box advance in the measured width */
            ge = urp_cache_lookup(&dc->cache, URP_CP_NOTFOUND, 0);
            if (!ge) {
                ge = urp_make_notfound_entry(dc);
                if (ge) urp_cache_insert(&dc->cache, ge);
            }
            if (ge) totalAdvance += urp_glyph_advance(dc, ge);
            continue;
        }

        /* Use cache for advance (also warms it up before the draw call) */
        ge = urp_cache_lookup(&dc->cache, (ULONG)cp, dc->currentStyle);
        if (!ge) {
            urp_set_face_size(fe);
            ge = urp_fill_cache_entry(dc, fe, gi, (ULONG)cp);
            if (ge) urp_cache_insert(&dc->cache, ge);
        }
        if (!ge) continue;

        totalAdvance += urp_glyph_advance(dc, ge);

        /* Line-height metrics from the cached glyph entry.
         * bearingY is the ascender (baseline → top); already correctly
         * scaled for bitmap-only fonts (NotoColorEmoji etc.). */
        {
            WORD asc = ge->bearingY;
            WORD dsc = (ge->rows > ge->bearingY)
                       ? (WORD)(ge->rows - ge->bearingY) : 0;
            if (asc > maxAscender)  maxAscender  = asc;
            if (dsc > maxDescender) maxDescender = dsc;
        }
    }

    if (totalAdvance > maxWidth) maxWidth = totalAdvance;

    {
    /* lineH is the exact Y step used by URPDrawTextUTF8 on every \n.
     * Height must use the same value so sizing and drawing agree. */
    WORD lineH = urp_line_height(dc);

    /* Clamp ascender and descender to at least the font-designed values.
     * This guarantees:
     *   baseY  == font ascender (what callers pass as pos.y)
     *   height == N * lineH     (consistent across all strings at the same size)
     * so sizing and drawing always agree regardless of which glyphs appear. */
    {
        WORD fontAsc  = urp_font_ascender(dc);
        WORD fontDesc = (lineH > fontAsc) ? (WORD)(lineH - fontAsc) : 0;
        if (maxAscender  < fontAsc)  maxAscender  = fontAsc;
        if (maxDescender < fontDesc) maxDescender = fontDesc;
    }

    out->width  = maxWidth;
    /* Total drawn pixels: first-line ascender, then (N-1) full line steps,
     * then the last-line descender.  For N=1 this reduces to asc+desc. */
    out->height = (WORD)(maxAscender + (lineCount - 1) * (int)lineH + maxDescender);
    out->baseX  = 0;
    out->baseY  = maxAscender;
    }
    }
    ReleaseSemaphore(&dc->sem);
}

void URPDC_GetFontLineMetrics(REG(a0, struct URPDrawContext *dc),
                              REG(a1, struct URPTextMetric *out))
{
    int i, asc, dsc;
    int maxAscend  = 0;
    int maxDescend = 0;

    if (!out) return;
    out->width = out->height = out->baseX = out->baseY = 0;
    if (!dc || dc->numFonts == 0) return;

    ObtainSemaphore(&dc->sem);
    for (i = 0; i < dc->numFonts; i++) {
        struct URPFontEntry *fe = &dc->fonts[i];
        FT_Pos raw_dsc;
        int scaleNum = 1;
        int scaleDen = 1;

        if (!fe->face) continue;

        urp_set_face_size(fe);

        asc = (int)(fe->face->size->metrics.ascender >> 6);

        /* descender is a negative FT_Pos; negate before shifting to avoid
         * implementation-defined behaviour on negative right-shifts. */
        raw_dsc = fe->face->size->metrics.descender;
        dsc = (int)((-raw_dsc) >> 6);

        /* Bitmap-only fonts (PNG/CBDT/CBLC emoji) report metrics at the native
         * strike size, not at pointSize.  Mirror the same scale used in
         * urp_fill_cache_entry so the metrics reflect actual rendered pixels. */
        if (!FT_IS_SCALABLE(fe->face) && fe->face->num_fixed_sizes > 0) {
            int cellH = (int)(fe->face->size->metrics.height >> 6);
            if (cellH > 0 && cellH != fe->pointSize) {
                scaleNum = fe->pointSize;
                scaleDen = cellH;
            }
            /* Bitmap fonts often report ascender = 0; treat full cell as
             * ascender since the whole glyph bitmap sits above the baseline. */
            if (asc <= 0) {
                asc = cellH;
                dsc = 0;
            }
        }

        if (dsc < 0) dsc = 0;

        /* Apply the same proportional scale as glyph pixel dimensions. */
        asc = (asc * scaleNum + scaleDen / 2) / scaleDen;
        dsc = (dsc * scaleNum + scaleDen / 2) / scaleDen;

        if (asc > maxAscend)  maxAscend  = asc;
        if (dsc > maxDescend) maxDescend = dsc;
    }

    out->height = (WORD)(maxAscend + maxDescend);
    out->baseY  = (WORD)maxAscend;
    ReleaseSemaphore(&dc->sem);
}

/*
 * URPDC_TextSizeUTF8() – compute horizontal offsets for UTF8 characters,
 *          to a preallocated array.
 *          Take care of tab according to URPDCA_TabSpaces
 *
 * utf8:     NUL-terminated UTF-8 string (or limited by maxChars).
 * maxChars: maximum Unicode codepoints to measure; pass -1 for whole string.
 * out:      for each character the horizontal offset, first being always 0.
 */
void URPDC_HorizontalOffsetArrayUTF8(REG(a0, struct URPDrawContext *dc),
                        REG(a1, const char           *utf8),
                        REG(d0, int                   maxChars),
                        REG(a2, LONG *arrayout))
{
    const unsigned char  *p;
    int                   remaining;
    LONG                 totalAdvance = 0;
    unsigned long         cp;
    int                   i;
    struct URPFontEntry  *fe;
    FT_UInt               gi;
    struct URPGlyphEntry *ge;

    if (!arrayout || !dc || !utf8) return;

    p         = (const unsigned char *)utf8;
    remaining = (maxChars < 0) ? INT_MAX : maxChars;

    while (1) {
        cp = urp_utf8_next(&p, &remaining);

        *arrayout++ = totalAdvance;
        if (cp == 0) break;

        /* Tab: fixed advance = tabSpaces * space advance */
        if (cp == 0x09) {
            totalAdvance += (WORD)urp_tab_advance(dc);
            continue;
        }

        fe = NULL; gi = 0;
        for (i = 0; i < dc->numFonts; i++) {
            gi = FT_Get_Char_Index(dc->fonts[i].face, (FT_ULong)cp);
            if (gi != 0) { fe = &dc->fonts[i]; break; }
        }
        if (!fe) {
            /* Count the tofu box advance in the measured width */
            ge = urp_cache_lookup(&dc->cache, URP_CP_NOTFOUND, 0);
            if (!ge) {
                ge = urp_make_notfound_entry(dc);
                if (ge) urp_cache_insert(&dc->cache, ge);
            }
            if (ge) totalAdvance += urp_glyph_advance(dc, ge);
            continue;
        }

        /* Use cache for advance (also warms it up before the draw call) */
        ge = urp_cache_lookup(&dc->cache, (ULONG)cp, dc->currentStyle);
        if (!ge) {
            urp_set_face_size(fe);
            ge = urp_fill_cache_entry(dc, fe, gi, (ULONG)cp);
            if (ge) urp_cache_insert(&dc->cache, ge);
        }
        if (!ge) continue;

        totalAdvance += urp_glyph_advance(dc, ge);

    }


}

/* =========================================================================
 * Per-glyph cache lookup + fill helper (shared by both draw paths)
 * ========================================================================= */

static struct URPGlyphEntry *urp_get_glyph(struct URPDrawContext *dc,
                                            unsigned long cp,
                                            struct URPFontEntry **fe_out,
                                            FT_UInt             *gi_out)
{
    struct URPGlyphEntry *ge;
    struct URPFontEntry  *fe;
    FT_UInt               gi;
    int i;

    fe = NULL; gi = 0;
    for (i = 0; i < dc->numFonts; i++) {
        gi = FT_Get_Char_Index(dc->fonts[i].face, (FT_ULong)cp);
        if (gi != 0) { fe = &dc->fonts[i]; break; }
    }
    if (fe_out) *fe_out = fe;
    if (gi_out) *gi_out = gi;

    if (!fe) return NULL;

    ge = urp_cache_lookup(&dc->cache, (ULONG)cp, dc->currentStyle);
    if (!ge) {
        urp_set_face_size(fe);
        ge = urp_fill_cache_entry(dc, fe, gi, (ULONG)cp);
        if (ge) urp_cache_insert(&dc->cache, ge);
    }
    return ge;
}

static struct URPGlyphEntry *urp_get_notfound(struct URPDrawContext *dc)
{
    struct URPGlyphEntry *ge = urp_cache_lookup(&dc->cache, URP_CP_NOTFOUND, 0);
    if (!ge) {
        ge = urp_make_notfound_entry(dc);
        if (ge) urp_cache_insert(&dc->cache, ge);
    }
    return ge;
}


/* =========================================================================
 * CGX true-colour draw path
 *
 * When the RastPort has a Layer, DoHookClipRects() is used so that only
 * the actually-visible ClipRects are painted — preventing writes into
 * areas covered by other windows.  The hook is called once per visible
 * rectangle; inside it we LockBitMapTags, clip to bf_Bounds, draw the
 * full string (restarting from startX each call), then UnLockBitMap.
 *
 * Without a Layer the original single-pass path is used unchanged.
 * ========================================================================= */

/* BackFillMsg is not defined in AmigaOS 3.x system headers; mirror it here. */
struct urp_bf_msg {
    struct Layer     *bf_Layer;
    struct Rectangle  bf_Bounds;
    LONG              bf_OffsetX;
    LONG              bf_OffsetY;
};

/* Hook data: everything needed to (re)draw the string per ClipRect. */
struct urp_cgx_clip_hook {
    struct Hook           hook;       /* must be first – a0 on entry */
    struct RastPort      *rp;
    struct URPDrawContext *dc;
    const unsigned char  *p;
    int                   remaining;
    WORD                  startX;
    WORD                  posY;
    WORD                  lineH;      /* line height for newline advance */
    WORD                  finalX;     /* updated each call; all calls agree */
    WORD                  finalY;     /* updated each call; all calls agree */
    int                   forcedmono; /* 0 = proportional, 1 = forced-mono */
    int                   firstCall;  /* limit glyph-not-found tracking */
};

/* Hook entry: called by DoHookClipRects once per visible ClipRect.
 * a0 = struct Hook *, a2 = struct RastPort *, a1 = struct urp_bf_msg * */
static void urp_cgx_clip_hook_func(
        REG(a0, struct Hook          *h),
        REG(a2, struct RastPort      *rp),
        REG(a1, struct urp_bf_msg    *msg))
{
    struct urp_cgx_clip_hook     *hd = (struct urp_cgx_clip_hook *)h;
    struct urp_blend_ctx          ctx;
    APTR                          handle;
    ULONG                         bmwidth, bmheight, pixfmt;
    const struct urp_blend_vtable *vt;
    unsigned long                 cp;
    struct URPGlyphEntry         *ge;
    WORD                          gx, gy, layer_x, layer_y;
    const unsigned char          *p;
    int                           remaining;
    struct RastPort               crp; /* Layer=NULL copy for raw BltTemplate */

    handle = LockBitMapTags(rp->BitMap,
                            LBMI_PIXFMT,      (ULONG)&pixfmt,
                            LBMI_BASEADDRESS, (ULONG)&ctx.base,
                            LBMI_BYTESPERROW, (ULONG)&ctx.bpr,
                            LBMI_WIDTH,       (ULONG)&bmwidth,
                            LBMI_HEIGHT,      (ULONG)&bmheight,
                            TAG_DONE);
    if (!handle) return;

    /* Clip strictly to this visible rectangle (screen coords). */
    ctx.cx1 = msg->bf_Bounds.MinX;
    ctx.cy1 = msg->bf_Bounds.MinY;
    ctx.cx2 = (msg->bf_Bounds.MaxX + 1 < (WORD)bmwidth)  ? msg->bf_Bounds.MaxX + 1 : (WORD)bmwidth;
    ctx.cy2 = (msg->bf_Bounds.MaxY + 1 < (WORD)bmheight) ? msg->bf_Bounds.MaxY + 1 : (WORD)bmheight;

    /* Layer origin: converts window-local glyph coords to screen coords. */
    layer_x = msg->bf_Layer ? msg->bf_Layer->bounds.MinX : 0;
    layer_y = msg->bf_Layer ? msg->bf_Layer->bounds.MinY : 0;

    /* Layer=NULL copy: required for raw bitmap access inside the hook.
     * Docs: "make sure you use a copy of the RastPort and NULL the Layer". */
    crp       = *rp;
    crp.Layer = NULL;

    vt = (pixfmt < 14) ? &urp_blend_table[pixfmt] : NULL;

    p         = hd->p;
    remaining = hd->remaining;

    if (hd->forcedmono) {
        WORD cellW = (hd->dc->monoAdvanceX > 0) ? hd->dc->monoAdvanceX : 8;
        WORD curX  = hd->startX;
        WORD curY  = hd->posY;
        WORD centerOff;

        while (1) {
            cp = urp_utf8_next(&p, &remaining);
            if (cp == 0) break;

            if (cp == 0x0a) {
                curY += hd->lineH;
                curX  = hd->startX;
                continue;
            }
            if (cp == 0x09) {
                curX += (WORD)((ULONG)hd->dc->tabSpaces * (ULONG)cellW);
                continue;
            }
            ge = urp_get_glyph(hd->dc, cp, NULL, NULL);
            if (!ge) {
                if (hd->firstCall && hd->dc->numberOfGlyphsNotFound < MAX_CODE_NOT_FOUND)
                    hd->dc->codeNotFound[hd->dc->numberOfGlyphsNotFound++] = (ULONG)cp;
                ge = urp_get_notfound(hd->dc);
            }
            if (!ge || ge->width <= 0 || ge->rows <= 0) { curX += cellW; continue; }

            centerOff = (cellW - ge->advanceX) / 2;
            if (centerOff < 0) centerOff = 0;
            gx = curX + centerOff + ge->bearingX;
            gy = curY - ge->bearingY;

            switch (ge->pixelFmt) {
                case URP_CACHE_MONO: {
                    /* manually intersect glyph rect with clip rect */
                    WORD sx  = (WORD)(gx + layer_x), sy = (WORD)(gy + layer_y);
                    WORD bx1 = sx  > ctx.cx1 ? sx  : (WORD)ctx.cx1;
                    WORD by1 = sy  > ctx.cy1 ? sy  : (WORD)ctx.cy1;
                    WORD bx2 = sx + (WORD)ge->width < ctx.cx2 ? sx + (WORD)ge->width : (WORD)ctx.cx2;
                    WORD by2 = sy + (WORD)ge->rows  < ctx.cy2 ? sy + (WORD)ge->rows  : (WORD)ctx.cy2;
                    if (bx1 < bx2 && by1 < by2) {
                        WORD srcx = bx1 - sx, srcy = by1 - sy;
                        BltTemplate(
                            (PLANEPTR)(ge->pixels + (ULONG)srcy * ge->pitch),
                            (LONG)srcx, (LONG)ge->pitch,
                            &crp, (LONG)bx1, (LONG)by1,
                            (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                    }
                    break;
                }
                case URP_CACHE_GRAY:
                    if (vt && vt->gray) {
                        ctx.ge = ge;
                        ctx.dx = gx + layer_x;
                        ctx.dy = gy + layer_y;
                        vt->gray(&ctx, hd->dc);
                    }
                    break;
                case URP_CACHE_RGBA:
                    if (vt && vt->rgba) {
                        ctx.ge = ge;
                        ctx.dx = gx + layer_x;
                        ctx.dy = gy + layer_y;
                        vt->rgba(&ctx, hd->dc);
                    }
                    break;
            }
            curX += cellW;
        }
        hd->finalX = curX;
        hd->finalY = curY;

    } else {
        WORD curX = hd->startX;
        WORD curY = hd->posY;

        while (1) {
            cp = urp_utf8_next(&p, &remaining);
            if (cp == 0) break;

            if (cp == 0x0a) {
                curY += hd->lineH;
                curX  = hd->startX;
                continue;
            }
            if (cp == 0x09) {
                curX += urp_tab_advance(hd->dc);
                continue;
            }
            ge = urp_get_glyph(hd->dc, cp, NULL, NULL);
            if (!ge) {
                if (hd->firstCall && hd->dc->numberOfGlyphsNotFound < MAX_CODE_NOT_FOUND)
                    hd->dc->codeNotFound[hd->dc->numberOfGlyphsNotFound++] = (ULONG)cp;
                ge = urp_get_notfound(hd->dc);
            }
            if (!ge || ge->width <= 0 || ge->rows <= 0) {
                if (ge) curX += ge->advanceX;
                continue;
            }
            gx = curX + ge->bearingX;
            gy = curY - ge->bearingY;

            switch (ge->pixelFmt) {
                case URP_CACHE_MONO: {
                    WORD sx  = (WORD)(gx + layer_x), sy = (WORD)(gy + layer_y);
                    WORD bx1 = sx  > ctx.cx1 ? sx  : (WORD)ctx.cx1;
                    WORD by1 = sy  > ctx.cy1 ? sy  : (WORD)ctx.cy1;
                    WORD bx2 = sx + (WORD)ge->width < ctx.cx2 ? sx + (WORD)ge->width : (WORD)ctx.cx2;
                    WORD by2 = sy + (WORD)ge->rows  < ctx.cy2 ? sy + (WORD)ge->rows  : (WORD)ctx.cy2;
                    if (bx1 < bx2 && by1 < by2) {
                        WORD srcx = bx1 - sx, srcy = by1 - sy;
                        BltTemplate(
                            (PLANEPTR)(ge->pixels + (ULONG)srcy * ge->pitch),
                            (LONG)srcx, (LONG)ge->pitch,
                            &crp, (LONG)bx1, (LONG)by1,
                            (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                    }
                    break;
                }
                case URP_CACHE_GRAY:
                    if (vt && vt->gray) {
                        ctx.ge = ge;
                        ctx.dx = gx + layer_x;
                        ctx.dy = gy + layer_y;
                        vt->gray(&ctx, hd->dc);
                    }
                    break;
                case URP_CACHE_RGBA:
                    if (vt && vt->rgba) {
                        ctx.ge = ge;
                        ctx.dx = gx + layer_x;
                        ctx.dy = gy + layer_y;
                        vt->rgba(&ctx, hd->dc);
                    }
                    break;
            }
            curX += ge->advanceX;
        }
        hd->finalX = curX;
        hd->finalY = curY;
    }

    hd->firstCall = 0;
    UnLockBitMap(handle);
}

static void urp_draw_text_cgx(struct RastPort      *rp,
                               struct URPDrawContext *dc,
                               struct URPTextPos    *pos,
                               const unsigned char  *p,
                               int                   remaining)
{
    struct urp_blend_ctx            ctx;
    APTR                            handle;
    ULONG                           bmwidth, bmheight, pixfmt;
    const struct urp_blend_vtable  *vt;
    unsigned long                   cp;
    struct URPGlyphEntry           *ge;
    WORD                            gx, gy;

    if (rp->Layer) {
        struct urp_cgx_clip_hook hd;
        hd.hook.h_MinNode.mln_Succ = NULL;
        hd.hook.h_MinNode.mln_Pred = NULL;
        hd.hook.h_Entry    = (ULONG(*)())urp_cgx_clip_hook_func;
        hd.hook.h_SubEntry = NULL;
        hd.hook.h_Data     = NULL;
        hd.rp         = rp;
        hd.dc         = dc;
        hd.p          = p;
        hd.remaining  = remaining;
        hd.startX     = pos->x;
        hd.posY       = pos->y;
        hd.lineH      = urp_line_height(dc);
        hd.finalX     = pos->x;
        hd.finalY     = pos->y;
        hd.forcedmono = 0;
        hd.firstCall  = 1;
        DoHookClipRects(&hd.hook, rp, NULL);
        pos->x = hd.finalX;
        pos->y = hd.finalY;
        return;
    }

    handle = LockBitMapTags(rp->BitMap,
                            LBMI_PIXFMT,      (ULONG)&pixfmt,
                            LBMI_BASEADDRESS, (ULONG)&ctx.base,
                            LBMI_BYTESPERROW, (ULONG)&ctx.bpr,
                            LBMI_WIDTH,       (ULONG)&bmwidth,
                            LBMI_HEIGHT,      (ULONG)&bmheight,
                            TAG_DONE);
    if (!handle) return;

    ctx.cx1 = ctx.cy1 = 0;
    ctx.cx2 = bmwidth;
    ctx.cy2 = bmheight;

    vt = (pixfmt < 14) ? &urp_blend_table[pixfmt] : NULL;

    {
    WORD startX = pos->x;
    WORD lineH  = urp_line_height(dc);

    while (1) {
        cp = urp_utf8_next(&p, &remaining);
        if (cp == 0) break;

        /* Newline: advance to next line */
        if (cp == 0x0a) {
            pos->y += lineH;
            pos->x  = startX;
            continue;
        }

        /* Tab: advance without drawing */
        if (cp == 0x09) {
            pos->x += urp_tab_advance(dc);
            continue;
        }

        ge = urp_get_glyph(dc, cp, NULL, NULL);
        if (!ge) {
            if (dc->numberOfGlyphsNotFound < MAX_CODE_NOT_FOUND)
                dc->codeNotFound[dc->numberOfGlyphsNotFound++] = (ULONG)cp;
            ge = urp_get_notfound(dc);
        }
        if (!ge || ge->width <= 0 || ge->rows <= 0) {
            if (ge) pos->x += ge->advanceX;
            continue;
        }

        gx = pos->x + ge->bearingX;
        gy = pos->y - ge->bearingY;

        switch (ge->pixelFmt) {
            case URP_CACHE_MONO: {
                WORD bx1, by1, bx2, by2, srcx, srcy;
                WORD cx1 = (WORD)ctx.cx1, cy1 = (WORD)ctx.cy1;
                WORD cx2 = (WORD)ctx.cx2, cy2 = (WORD)ctx.cy2;
                bx1 = gx > cx1 ? gx : cx1;
                by1 = gy > cy1 ? gy : cy1;
                bx2 = (gx + (WORD)ge->width) < cx2 ? (gx + (WORD)ge->width) : cx2;
                by2 = (gy + (WORD)ge->rows)  < cy2 ? (gy + (WORD)ge->rows)  : cy2;
                if (bx1 < bx2 && by1 < by2) {
                    srcx = bx1 - gx; srcy = by1 - gy;
                    BltTemplate(
                        (PLANEPTR)(ge->pixels + (ULONG)srcy * ge->pitch),
                        (LONG)srcx, (LONG)ge->pitch,
                        rp, (LONG)bx1, (LONG)by1,
                        (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                }
                break;
            }
            case URP_CACHE_GRAY:
                if (vt && vt->gray) {
                    ctx.ge = ge;
                    ctx.dx = gx;
                    ctx.dy = gy;
                    vt->gray(&ctx, dc);
                }
                break;
            case URP_CACHE_RGBA:
                if (vt && vt->rgba) {
                    ctx.ge = ge;
                    ctx.dx = gx;
                    ctx.dy = gy;
                    vt->rgba(&ctx, dc);
                }
                break;
        }
        pos->x += ge->advanceX;
    }
    } /* startX / lineH block */

    UnLockBitMap(handle);
}

static void urp_draw_text_cgx_forcedmono(struct RastPort      *rp,
                                          struct URPDrawContext *dc,
                                          struct URPTextPos    *pos,
                                          const unsigned char  *p,
                                          int                   remaining)
{
    struct urp_blend_ctx            ctx;
    APTR                            handle;
    ULONG                           bmwidth, bmheight, pixfmt;
    const struct urp_blend_vtable  *vt;
    unsigned long                   cp;
    struct URPGlyphEntry           *ge;
    WORD                            gx, gy, cellW, centerOff;

    if (dc->monoAdvanceX <= 0) urp_compute_mono_advance(dc);
    cellW = (dc->monoAdvanceX > 0) ? dc->monoAdvanceX : 8;

    if (rp->Layer) {
        struct urp_cgx_clip_hook hd;
        hd.hook.h_MinNode.mln_Succ = NULL;
        hd.hook.h_MinNode.mln_Pred = NULL;
        hd.hook.h_Entry    = (ULONG(*)())urp_cgx_clip_hook_func;
        hd.hook.h_SubEntry = NULL;
        hd.hook.h_Data     = NULL;
        hd.rp         = rp;
        hd.dc         = dc;
        hd.p          = p;
        hd.remaining  = remaining;
        hd.startX     = pos->x;
        hd.posY       = pos->y;
        hd.lineH      = urp_line_height(dc);
        hd.finalX     = pos->x;
        hd.finalY     = pos->y;
        hd.forcedmono = 1;
        hd.firstCall  = 1;
        DoHookClipRects(&hd.hook, rp, NULL);
        pos->x = hd.finalX;
        pos->y = hd.finalY;
        return;
    }

    handle = LockBitMapTags(rp->BitMap,
                            LBMI_PIXFMT,      (ULONG)&pixfmt,
                            LBMI_BASEADDRESS, (ULONG)&ctx.base,
                            LBMI_BYTESPERROW, (ULONG)&ctx.bpr,
                            LBMI_WIDTH,       (ULONG)&bmwidth,
                            LBMI_HEIGHT,      (ULONG)&bmheight,
                            TAG_DONE);
    if (!handle) return;

    ctx.cx1 = ctx.cy1 = 0;
    ctx.cx2 = bmwidth;
    ctx.cy2 = bmheight;

    vt = (pixfmt < 14) ? &urp_blend_table[pixfmt] : NULL;

    {
    WORD startX = pos->x;
    WORD lineH  = urp_line_height(dc);

    while (1) {
        cp = urp_utf8_next(&p, &remaining);
        if (cp == 0) break;

        if (cp == 0x0a) {
            pos->y += lineH;
            pos->x  = startX;
            continue;
        }
        if (cp == 0x09) {
            pos->x += (WORD)((ULONG)dc->tabSpaces * (ULONG)cellW);
            continue;
        }

        ge = urp_get_glyph(dc, cp, NULL, NULL);
        if (!ge) {
            if (dc->numberOfGlyphsNotFound < MAX_CODE_NOT_FOUND)
                dc->codeNotFound[dc->numberOfGlyphsNotFound++] = (ULONG)cp;
            ge = urp_get_notfound(dc);
        }
        if (!ge || ge->width <= 0 || ge->rows <= 0) {
            pos->x += cellW;
            continue;
        }

        centerOff = (cellW - ge->advanceX) / 2;
        if (centerOff < 0) centerOff = 0;
        gx = pos->x + centerOff + ge->bearingX;
        gy = pos->y - ge->bearingY;

        switch (ge->pixelFmt) {
            case URP_CACHE_MONO: {
                WORD bx1, by1, bx2, by2, srcx, srcy;
                WORD cx1 = (WORD)ctx.cx1, cy1 = (WORD)ctx.cy1;
                WORD cx2 = (WORD)ctx.cx2, cy2 = (WORD)ctx.cy2;
                bx1 = gx > cx1 ? gx : cx1;
                by1 = gy > cy1 ? gy : cy1;
                bx2 = (gx + (WORD)ge->width) < cx2 ? (gx + (WORD)ge->width) : cx2;
                by2 = (gy + (WORD)ge->rows)  < cy2 ? (gy + (WORD)ge->rows)  : cy2;
                if (bx1 < bx2 && by1 < by2) {
                    srcx = bx1 - gx; srcy = by1 - gy;
                    BltTemplate(
                        (PLANEPTR)(ge->pixels + (ULONG)srcy * ge->pitch),
                        (LONG)srcx, (LONG)ge->pitch,
                        rp, (LONG)bx1, (LONG)by1,
                        (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                }
                break;
            }
            case URP_CACHE_GRAY:
                if (vt && vt->gray) {
                    ctx.ge = ge;
                    ctx.dx = gx;
                    ctx.dy = gy;
                    vt->gray(&ctx, dc);
                }
                break;
            case URP_CACHE_RGBA:
                if (vt && vt->rgba) {
                    ctx.ge = ge;
                    ctx.dx = gx;
                    ctx.dy = gy;
                    vt->rgba(&ctx, dc);
                }
                break;
        }
        pos->x += cellW;
    }
    } /* startX / lineH block */

    UnLockBitMap(handle);
}


/* =========================================================================
 * CLUT / planar draw path
 *
 * Uses BltTemplate for MONO, BltMaskBitMapRastPort for GRAY / RGBA.
 * ========================================================================= */

static void urp_draw_text_clut(struct RastPort      *rp,
                                struct URPDrawContext *dc,
                                struct URPTextPos    *pos,
                                const unsigned char  *p,
                                int                   remaining)
{
    unsigned long         cp;
    struct URPGlyphEntry *ge;
    WORD                  gx, gy;
    WORD                  startX = pos->x;
    WORD                  lineH  = urp_line_height(dc);
    BOOL targetIsChipRam = (GetBitMapAttr(rp->BitMap,BMA_FLAGS) & BMF_STANDARD);

 // bdbprintf("*** drcl bounds %d %d %d %d, rp:%08x dc:%08x pos:%d %d\n",
 //            (int)rp->Layer->bounds.MinX,
 //            (int)rp->Layer->bounds.MinY,
 //            (int)rp->Layer->bounds.MaxX,
 //            (int)rp->Layer->bounds.MaxY,
 //            (int)rp,
 //            (int)dc,
 //            (int)pos->x,
 //            (int)pos->y

 //            );

    while (1) {
        cp = urp_utf8_next(&p, &remaining);
        if (cp == 0) break;

        /* Newline: advance to next line */
        if (cp == 0x0a) {
            pos->y += lineH;
            pos->x  = startX;
            continue;
        }

        /* Tab: advance without drawing */
        if (cp == 0x09) {
            pos->x += urp_tab_advance(dc);
            continue;
        }

        ge = urp_get_glyph(dc, cp, NULL, NULL);
        if (!ge) {
            if (dc->numberOfGlyphsNotFound < MAX_CODE_NOT_FOUND)
                dc->codeNotFound[dc->numberOfGlyphsNotFound++] = (ULONG)cp;
            ge = urp_get_notfound(dc);
        }
        if (!ge || ge->width <= 0 || ge->rows <= 0) {
            if (ge) pos->x += ge->advanceX;
            continue;
        }

        gx = pos->x + ge->bearingX;
        gy = pos->y - ge->bearingY;

        switch (ge->pixelFmt) {
            case URP_CACHE_MONO:
            {
                /* if target rastport bitmap is in chip ram, source needs to be also, use temp buffer
                    is better because chipram may be low.
                 */
                if(targetIsChipRam)
                {
                    // if(!ge->pixelsIsInChip && ge->pixels)
                    // {
                    //     ULONG maskSize = ge->rows * ge->pitch;
                    //     UBYTE *f = ge->pixels;
                    //     ge->pixels = AllocVec(maskSize,MEMF_CHIP);
                    //     if(ge->pixels) CopyMem(f,ge->pixels,maskSize);
                    //     FreeVec(f);
                    //     ge->pixelsIsInChip = TRUE;
                    // }
                    if(ge->pixels)
                    {
                        WORD bx1, by1, bx2, by2, srcx, srcy;
                        WORD cx1 = 0, cy1 = 0;
                        WORD cx2 = rp->Layer ? (WORD)((rp->Layer->bounds.MaxX - rp->Layer->bounds.MinX) + 1) : (WORD) GetBitMapAttr( rp->BitMap,BMA_WIDTH);
                        WORD cy2 = rp->Layer ? (WORD)((rp->Layer->bounds.MaxY - rp->Layer->bounds.MinY) + 1) : (WORD) GetBitMapAttr( rp->BitMap,BMA_HEIGHT);
                        bx1 = gx > cx1 ? gx : cx1;
                        by1 = gy > cy1 ? gy : cy1;
                        bx2 = (gx + (WORD)ge->width) < cx2 ? (gx + (WORD)ge->width) : cx2;
                        by2 = (gy + (WORD)ge->rows)  < cy2 ? (gy + (WORD)ge->rows)  : cy2;
                        if (bx1 < bx2 && by1 < by2 && dc->tempChipRamAlloc) {
                            /* swap because previous buffer may be still in use ,
                             *  and we're not going to WaitBlit(). */
                            UWORD *swp = dc->tempChipRamB;
                            ULONG byteOfsx=0;
                            dc->tempChipRamB = dc->tempChipRamA;
                            dc->tempChipRamA = swp;

                            srcx = bx1 - gx; srcy = by1 - gy;
                            if(srcx>15)
                            {
                                byteOfsx = (srcx>>4)*2;
                                srcx &= 15;
                            }

                            CopyMem(ge->pixels + ((ULONG)srcy * ge->pitch),
                                dc->tempChipRamA,(by2 - by1) * ge->pitch);

                            /* was ok if pixels in chip
                            BltTemplate(
                                (PLANEPTR)((UBYTE *)ge->pixels + (ULONG)srcy * ge->pitch),
                                (LONG)srcx, (LONG)ge->pitch,
                                rp, (LONG)bx1, (LONG)by1,
                                (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                            */
                    /*note we keep external code draw mode */
                            BltTemplate(
                                (PLANEPTR)(dc->tempChipRamA+byteOfsx),
                                (LONG)srcx, (LONG)ge->pitch,
                                rp, (LONG)bx1, (LONG)by1,
                                (LONG)(bx2 - bx1), (LONG)(by2 - by1));

                        }
                    } // end if ->pixels ok


                } else
                {
                    if(ge->pixels)
                    {
                        WORD bx1, by1, bx2, by2, srcx, srcy;
                        WORD cx1 = 0, cy1 = 0;
                        WORD cx2 = rp->Layer ? (WORD)(rp->Layer->bounds.MaxX - rp->Layer->bounds.MinX + 1) : 0x7FFF;
                        WORD cy2 = rp->Layer ? (WORD)(rp->Layer->bounds.MaxY - rp->Layer->bounds.MinY + 1) : 0x7FFF;
                        bx1 = gx > cx1 ? gx : cx1;
                        by1 = gy > cy1 ? gy : cy1;
                        bx2 = (gx + (WORD)ge->width) < cx2 ? (gx + (WORD)ge->width) : cx2;
                        by2 = (gy + (WORD)ge->rows)  < cy2 ? (gy + (WORD)ge->rows)  : cy2;
                        if (bx1 < bx2 && by1 < by2) {
                            srcx = bx1 - gx; srcy = by1 - gy;
                            BltTemplate(
                                (PLANEPTR)(ge->pixels + (ULONG)srcy * ge->pitch),
                                (LONG)srcx, (LONG)ge->pitch,
                                rp, (LONG)bx1, (LONG)by1,
                                (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                        }
                    }

                }
            }
                break;
            case URP_CACHE_GRAY:
                if (dc->clutValid && dc->currentFriendBitmap) {
                    if (!ge->clutBitmap && !ge->chk_clutBitmap)
                        urp_build_clut_bitmaps_gray(dc, ge);
                    if(ge->chk_clutBitmap)
                    {
                        /* chip saving mode, */
                        WriteChunkyPixels(rp,(ULONG)gx, (ULONG)gy,
                                        (ULONG)(gx+ge->width-1),
                                        (ULONG)(gy+ge->rows-1),
                                        ge->chk_clutBitmap,
                                        ge->width
                                        );
                    } else
                    if (ge->clutBitmap) {
                        if (ge->maskBitmap) {
                            BltMaskBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows,
                                0xe0, (PLANEPTR)ge->maskBitmap->Planes[0]);
                        } else {
                            BltBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows, 0xc0);
                        }
                    }
                }
                break;
            case URP_CACHE_RGBA:
                if (dc->clutValid && dc->currentFriendBitmap) {
                    if (!ge->clutBitmap && !ge->chk_clutBitmap)
                        urp_build_clut_bitmaps_rgba(dc, ge);

                    if(ge->chk_clutBitmap)
                    {
                        /* chip saving mode, */
                        WriteChunkyPixels(rp,(ULONG)gx, (ULONG)gy,
                                        (ULONG)(gx+ge->width-1),
                                        (ULONG)(gy+ge->rows-1),
                                        ge->chk_clutBitmap,
                                        ge->width
                                        );
                    } else
                    if (ge->clutBitmap) {
                        if (ge->maskBitmap) {
                            BltMaskBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows,
                                0xe0, (PLANEPTR)ge->maskBitmap->Planes[0]);
                        } else {
                            BltBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows, 0xc0);
                        }
                    }
                }
                break;
        }
        pos->x += ge->advanceX;
    }
}

static void urp_draw_text_clut_forcedmono(struct RastPort      *rp,
                                           struct URPDrawContext *dc,
                                           struct URPTextPos    *pos,
                                           const unsigned char  *p,
                                           int                   remaining)
{
    unsigned long         cp;
    struct URPGlyphEntry *ge;
    WORD                  gx, gy, cellW, centerOff;
    WORD                  startX, lineH;
    BOOL targetIsChipRam = (GetBitMapAttr(rp->BitMap,BMA_FLAGS) & BMF_STANDARD);

    if (dc->monoAdvanceX <= 0) urp_compute_mono_advance(dc);
    cellW  = (dc->monoAdvanceX > 0) ? dc->monoAdvanceX : 8;
    startX = pos->x;
    lineH  = urp_line_height(dc);

    while (1) {
        cp = urp_utf8_next(&p, &remaining);
        if (cp == 0) break;

        if (cp == 0x0a) {
            pos->y += lineH;
            pos->x  = startX;
            continue;
        }
        if (cp == 0x09) {
            pos->x += (WORD)((ULONG)dc->tabSpaces * (ULONG)cellW);
            continue;
        }

        ge = urp_get_glyph(dc, cp, NULL, NULL);
        if (!ge) {
            if (dc->numberOfGlyphsNotFound < MAX_CODE_NOT_FOUND)
                dc->codeNotFound[dc->numberOfGlyphsNotFound++] = (ULONG)cp;
            ge = urp_get_notfound(dc);
        }
        if (!ge || ge->width <= 0 || ge->rows <= 0) {
            pos->x += cellW;
            continue;
        }

        centerOff = (cellW - ge->advanceX) / 2;
        if (centerOff < 0) centerOff = 0;
        gx = pos->x + centerOff + ge->bearingX;
        gy = pos->y - ge->bearingY;

        switch (ge->pixelFmt) {
            case URP_CACHE_MONO:

                if(targetIsChipRam)
                {
                    if(ge->pixels)
                    {
                        WORD bx1, by1, bx2, by2, srcx, srcy;
                        WORD cx1 = 0, cy1 = 0;
                        WORD cx2 = rp->Layer ? (WORD)((rp->Layer->bounds.MaxX - rp->Layer->bounds.MinX) + 1) : (WORD) GetBitMapAttr( rp->BitMap,BMA_WIDTH);
                        WORD cy2 = rp->Layer ? (WORD)((rp->Layer->bounds.MaxY - rp->Layer->bounds.MinY) + 1) : (WORD) GetBitMapAttr( rp->BitMap,BMA_HEIGHT);
                        bx1 = gx > cx1 ? gx : cx1;
                        by1 = gy > cy1 ? gy : cy1;
                        bx2 = (gx + (WORD)ge->width) < cx2 ? (gx + (WORD)ge->width) : cx2;
                        by2 = (gy + (WORD)ge->rows)  < cy2 ? (gy + (WORD)ge->rows)  : cy2;
                        if (bx1 < bx2 && by1 < by2 && dc->tempChipRamAlloc) {
                            /* swap because previous buffer may be still in use ,
                             *  and we're not going to WaitBlit(). */
                            UWORD *swp = dc->tempChipRamB;
                            ULONG byteOfsx=0;
                            dc->tempChipRamB = dc->tempChipRamA;
                            dc->tempChipRamA = swp;

                            srcx = bx1 - gx; srcy = by1 - gy;
                            if(srcx>15)
                            {
                                byteOfsx = (srcx>>4)*2;
                                srcx &= 15;
                            }

                            CopyMem(ge->pixels + ((ULONG)srcy * ge->pitch),
                                dc->tempChipRamA,(by2 - by1) * ge->pitch);

                            /*note we keep external code draw mode */
                            BltTemplate(
                                (PLANEPTR)(dc->tempChipRamA+byteOfsx),
                                (LONG)srcx, (LONG)ge->pitch,
                                rp, (LONG)bx1, (LONG)by1,
                                (LONG)(bx2 - bx1), (LONG)(by2 - by1));

                        }
                    } // end if ->pixels ok

                } else
                {
                    WORD bx1, by1, bx2, by2, srcx, srcy;
                    WORD cx1 = 0, cy1 = 0;
                    WORD cx2 = rp->Layer ? (WORD)(rp->Layer->bounds.MaxX - rp->Layer->bounds.MinX + 1) : 0x7FFF;
                    WORD cy2 = rp->Layer ? (WORD)(rp->Layer->bounds.MaxY - rp->Layer->bounds.MinY + 1) : 0x7FFF;
                    bx1 = gx > cx1 ? gx : cx1;
                    by1 = gy > cy1 ? gy : cy1;
                    bx2 = (gx + (WORD)ge->width) < cx2 ? (gx + (WORD)ge->width) : cx2;
                    by2 = (gy + (WORD)ge->rows)  < cy2 ? (gy + (WORD)ge->rows)  : cy2;
                    if (bx1 < bx2 && by1 < by2) {
                        srcx = bx1 - gx; srcy = by1 - gy;
                        BltTemplate(
                            (PLANEPTR)(ge->pixels + (ULONG)srcy * ge->pitch),
                            (LONG)srcx, (LONG)ge->pitch,
                            rp, (LONG)bx1, (LONG)by1,
                            (LONG)(bx2 - bx1), (LONG)(by2 - by1));
                    }
                }


                break;
            case URP_CACHE_GRAY:
                if (dc->clutValid && dc->currentFriendBitmap) {
                    if (!ge->clutBitmap && !ge->chk_clutBitmap)
                        urp_build_clut_bitmaps_gray(dc, ge);

                    if(ge->chk_clutBitmap)
                    {
                        /* chip saving mode, */
                        WriteChunkyPixels(rp,(ULONG)gx, (ULONG)gy,
                                        (ULONG)(gx+ge->width-1),
                                        (ULONG)(gy+ge->rows-1),
                                        ge->chk_clutBitmap,
                                        ge->width
                                        );
                    } else
                    if (ge->clutBitmap) {
                        if (ge->maskBitmap) {
                            BltMaskBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows,
                                0xe0, (PLANEPTR)ge->maskBitmap->Planes[0]);
                        } else {
                            BltBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows, 0xc0);
                        }
                    }
                }
                break;
            case URP_CACHE_RGBA:
                if (dc->clutValid && dc->currentFriendBitmap) {
                    if (!ge->clutBitmap && !ge->chk_clutBitmap)
                        urp_build_clut_bitmaps_rgba(dc, ge);

                    if(ge->chk_clutBitmap)
                    {
                        /* chip saving mode, */
                        WriteChunkyPixels(rp,(ULONG)gx, (ULONG)gy,
                                        (ULONG)(gx+ge->width-1),
                                        (ULONG)(gy+ge->rows-1),
                                        ge->chk_clutBitmap,
                                        ge->width
                                        );
                    } else
                    if (ge->clutBitmap) {
                        if (ge->maskBitmap) {
                            BltMaskBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows,
                                0xe0, (PLANEPTR)ge->maskBitmap->Planes[0]);
                        } else {
                            BltBitMapRastPort(ge->clutBitmap, 0, 0,
                                rp, (LONG)gx, (LONG)gy,
                                (LONG)ge->width, (LONG)ge->rows, 0xc0);
                        }
                    }
                }
                break;
        }
        pos->x += cellW;
    }
}


/* =========================================================================
 * URPDrawTextUTF8  –  public entry point
 * ========================================================================= */

void URPDrawTextUTF8(REG(a0, struct RastPort      *rp),
                     REG(a1, struct URPDrawContext *dc),
                     REG(a2, struct URPTextPos    *pos),
                     REG(a3, const char           *utf8),
                     REG(d0, ULONG                maxChars))
{
    const unsigned char *p;
    int                  remaining;
    if (!rp || !rp->BitMap || !dc || !utf8) return;
    if(*utf8 == 0) return; /* empty string, happens a lot. */
    dc->numberOfGlyphsNotFound = 0;
    p         = (const unsigned char *)utf8;
    remaining = ((int)maxChars < 0) ? 32767 : maxChars;

// bdbprintf("\n ** URPDrawTextUTF8 ** \n");

    dc->currentFriendBitmap = rp->BitMap;

    /* disambiguation from previous version: if you used SetColorFromPen(),
    no need to set SetAPen/BPen yourself, which is only used in monocolor+8bit target
    note rastport's SetDrMd() is used in that case.
    */
    if(dc->pensSet)
    {
        SetAPen(rp,dc->txtPen);
        SetBPen(rp,dc->bgPen);
    }

    if (CyberGfxBase != NULL &&
        GetCyberMapAttr(rp->BitMap, CYBRMATTR_ISCYBERGFX) != 0 &&
        GetCyberMapAttr(rp->BitMap, CYBRMATTR_PIXFMT) != PIXFMT_LUT8)
    {
        if (dc->prefFlags & URP_PREF_FORCE_MONOSPACE)
            urp_draw_text_cgx_forcedmono(rp, dc, pos, p, remaining);
        else
            urp_draw_text_cgx(rp, dc, pos, p, remaining);
    } else {

        /* if no antialias and target is chipram,
            We'll blit 2 color characters with BltTemplate,
            which needs source in chipram. Yet to save chipram
            we'll keep glyph source in fast as planar,
            and do copy to a short temp chip buffer. Then due
            to the Blitter delaying, we need double buffer for this.
         */
         BOOL targetIsChipRam = (GetBitMapAttr(rp->BitMap,BMA_FLAGS) & BMF_STANDARD);
         /* here we manage both native 8bit modes and CGX 8 bit modes !
            We have to differentiate true native modes like this.
            "no antialias" would means vector glyphs would be "mono, 2 colors"
         */
         if(targetIsChipRam && !(dc->prefFlags & URP_PREF_ANTIALIAS) &&
            dc->tempChipRamAlloc == NULL
            )
         {
            /* max for maximum char size: 48*40 than *2 for double buff 576b */
            int maxbm = 6*48;
            dc->tempChipRamSize = maxbm;
            dc->tempChipRamAlloc = (UBYTE *) AllocVec(maxbm*2,MEMF_CHIP);
            if(dc->tempChipRamAlloc)
            {
                dc->tempChipRamA = (UWORD*) (dc->tempChipRamAlloc );
                dc->tempChipRamB = (UWORD*) (dc->tempChipRamAlloc +maxbm);
            }
         }

        /* then only go */
        if (dc->prefFlags & URP_PREF_FORCE_MONOSPACE)
            urp_draw_text_clut_forcedmono(rp, dc, pos, p, remaining);
        else
            urp_draw_text_clut(rp, dc, pos, p, remaining);
    }
    /* can't retain that */
    dc->currentFriendBitmap = NULL;

}
