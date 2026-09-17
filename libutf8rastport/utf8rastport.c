
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

/* Variation selectors (Mongolian FVS, standard VS, VS supplement) carry no
 * glyph of their own -- they modify how the preceding base character is
 * drawn.  We don't yet act on that, but they must never fall through to the
 * "missing glyph" tofu-box path: no width, no glyph lookup, no draw. */
INLINE int urp_is_variation_selector(unsigned long cp)
{
    return (cp >= 0x180B && cp <= 0x180D) ||   /* Mongolian free VS */
           (cp >= 0xFE00 && cp <= 0xFE0F) ||   /* Variation Selectors */
           (cp >= 0xE0100 && cp <= 0xE01EF);   /* Variation Selectors Supplement */
}


/* =========================================================================
 * Shared font pool
 *
 * FT_Face/FT_Size objects are opened at most once per (resolved path,
 * point size) for the whole system and refcounted -- see the design
 * comment in urp_internal.h next to struct URPSharedFace/URPSharedSize.
 * Guarded by urpFontSem, a library-global semaphore distinct from any
 * per-DC dc->sem.
 *
 * Lock order when both are needed: dc->sem (outer) then urpFontSem
 * (inner) -- every place in this file that nests them follows this
 * order, so there is no lock-order inversion / deadlock risk. Several
 * functions (URPDC_AddFont, URPDC_RemoveFont) instead take them
 * sequentially, never nested, which is equally safe.
 * ========================================================================= */

static struct SignalSemaphore urpFontSem;
static FT_Library             urpSharedFTLib;
static struct URPSharedFace   urpSharedFaces[URP_SHARED_FACE_MAX];
static struct URPSharedSize   urpSharedSizes[URP_SHARED_SIZE_MAX];

/* Called once from CLibInit(), before any process can OpenLibrary() this
 * library -- runs single-threaded, no locking needed yet. Returns TRUE
 * on success; on failure urpSharedFTLib is left NULL so cleanup can
 * detect init never completed. */
int urp_shared_fonts_init(void)
{
    InitSemaphore(&urpFontSem);
    return FT_Init_FreeType(&urpSharedFTLib) == 0;
}

/* Called once from CLibExpunge(), when the last opener has closed the
 * library and the system reclaims it -- also reached if CLibInit()
 * itself failed partway through, possibly before urp_shared_fonts_init()
 * ever ran, so guard on urpSharedFTLib rather than assume init happened.
 * Any DC that leaked a reference at this point is a caller bug (didn't
 * URPDC_Release everything it created) -- close what's left rather than
 * leak it further. */
void urp_shared_fonts_cleanup(void)
{
    int i;
    if (!urpSharedFTLib) return;
    for (i = 0; i < URP_SHARED_SIZE_MAX; i++)
        if (urpSharedSizes[i].owner) FT_Done_Size(urpSharedSizes[i].ftSize);
    for (i = 0; i < URP_SHARED_FACE_MAX; i++)
        if (urpSharedFaces[i].face) FT_Done_Face(urpSharedFaces[i].face);
    memset(urpSharedSizes, 0, sizeof(urpSharedSizes));
    memset(urpSharedFaces, 0, sizeof(urpSharedFaces));
    FT_Done_FreeType(urpSharedFTLib);
    urpSharedFTLib = NULL;
}

/* =========================================================================
 * Shared screen CLUT remap pool -- see struct URPSharedScreenClut's design
 * comment in urp_internal.h. Independent of the font pool above (separate
 * semaphore, never nested with urpFontSem); only ever nested inside a
 * per-DC dc->sem (outer), same lock-order convention as the font pool.
 * ========================================================================= */

static struct SignalSemaphore     urpClutSem;
static struct URPSharedScreenClut urpSharedCluts[URP_SHARED_CLUT_MAX];

/* Called once from CLibInit(), same one-shot timing as urp_shared_fonts_init(). */
void urp_shared_cluts_init(void)
{
    /* note global inits will not be done with this tricked c runtime,
    so cleaning defaults can be done here */
    memset(&urpClutSem,0,sizeof(struct SignalSemaphore));
    memset(&urpSharedCluts[0],0,sizeof(urpSharedCluts));

    InitSemaphore(&urpClutSem);
}

/*
 * Recompute sc->clutRemap from screen's current palette, but only if
 * sc has never been computed (sc->refCount == 0, i.e. a just-allocated
 * slot) or the palette actually changed since the last computation.
 * Change detection is a cheap folded checksum over the same per-pen
 * RGB values GetRGB32 has to read anyway just to know; the O(4096*256)
 * nearest-pen search below that is what this whole cache exists to
 * skip when nothing actually changed. Caller must hold urpClutSem.
 * Returns TRUE if the expensive rebuild actually ran, FALSE if the
 * existing table was reused as-is -- purely informational, for tracing.
 */
static BOOL urp_clut_ensure_fresh(struct URPSharedScreenClut *sc, struct Screen *screen)
{
    UBYTE clut_r[256], clut_g[256], clut_b[256];
    ULONG rgb[3];
    ULONG hash;
    int   numColors, i, j;
    int   bestDist, bestIdx, dist, dr, dg, db;

    if (!screen->ViewPort.ColorMap) return FALSE;

    numColors = (int)screen->ViewPort.ColorMap->Count; /*can have sprite colors that bitmaps can't display */
    if ((int)sc->depth <= 8 && numColors > (1L << sc->depth))
        numColors = (int)(1L << sc->depth);
    if (numColors > 256) numColors = 256;
    /* vf manage less calls to GetRGB32() */


    /* Seed with numColors so a palette that shrinks or grows always
     * changes the hash even if every surviving entry is identical. */
     hash = (ULONG)numColors; // * 2654435761UL;
    for(i =0 ; i<numColors ; i+=64 )
    {
        ULONG rgb[3*64];
        int nextcolor = i+64;
        int nbc;
        if(numColors<nextcolor) nextcolor = numColors;
        nbc = nextcolor-i;
        if(nbc>0)
        {
            ULONG *prgb = rgb;
            GetRGB32(screen->ViewPort.ColorMap, (ULONG)i, nbc, rgb);
            for( j=i ; j<nextcolor ; j++ )
            {
                ULONG c;
                clut_r[j] = (UBYTE)((*prgb++) >> 24);
                clut_g[j] = (UBYTE)((*prgb++) >> 24);
                clut_b[j] = (UBYTE)((*prgb++) >> 24);

                c =  (((ULONG)clut_r[j])<<16) |
                     (((ULONG)clut_g[j])<<8) |
                     ((ULONG)clut_b[j]);
                hash ^= c;
            }
        }
    }


    if (sc->refCount > 0 && sc->paletteHash == hash)
    {
        return FALSE; /* already current palette */
    }

 //  bdbprintf(" - EFRESH do one color remap nbc:%d hash:%08x\n",numColors,hash);


    {
     UBYTE *pclut = &sc->clutRemap[0];
     for(int r=0 ; r<256; r+=17) // 0->255
      for(int g=0 ; g<256; g+=17)
        for(int b=0 ; b<256; b+=17)
    {

        bestDist = 0x7FFFFFFF;
        bestIdx  = 0;
        for (i = 0; i < numColors; i++) {
            dr = r - (int)clut_r[i];
            dg = g - (int)clut_g[i];
            db = b - (int)clut_b[i];
            dist = dr*dr + dg*dg + db*db;
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx  = i;
                if (dist == 0) break;
            }
        }
         *pclut++ = (UBYTE)bestIdx;
    }
    }
   sc->paletteHash = hash;

    return TRUE;
}

static struct URPSharedScreenClut *urp_shared_clut_find(struct Screen *screen, ULONG depth)
{
    int i;
    for (i = 0; i < URP_SHARED_CLUT_MAX; i++)
        if (urpSharedCluts[i].screen == screen && urpSharedCluts[i].depth == depth)
            return &urpSharedCluts[i];
    return NULL;
}

static struct URPSharedScreenClut *urp_shared_clut_find_free(void)
{
    int i;
    for (i = 0; i < URP_SHARED_CLUT_MAX; i++)
        if (!urpSharedCluts[i].screen) return &urpSharedCluts[i];
    return NULL;
}

/*
 * Acquire a reference to the shared CLUT table for (screen,depth),
 * creating and computing it on first use, or refreshing it in place
 * (shared by every other reference too) if the palette changed since
 * the last acquire/refresh by anyone. Caller must hold urpClutSem.
 */
static struct URPSharedScreenClut *urp_shared_clut_acquire(struct Screen *screen, ULONG depth)
{
    struct URPSharedScreenClut *sc = urp_shared_clut_find(screen, depth);
    if (!sc) {
        sc = urp_shared_clut_find_free();
        if (!sc) return NULL;
        sc->screen   = screen;
        sc->depth    = depth;
        sc->refCount = 0;
    }

    sc->refCount++;
    return sc;
}

/* Caller must hold urpClutSem. */
static void urp_shared_clut_release(struct URPSharedScreenClut *sc)
{
    if (!sc) return;
    if (sc->refCount > 0) sc->refCount--;
    if (sc->refCount == 0) sc->screen = NULL; /* slot free for reuse */
}

/* urp_dc_bind_screen_clut(), which ties the pool above into a
 * URPDrawContext, is defined further down next to urp_rebuild_aa_remap
 * and urp_flush_clut_bitmaps, which it calls. */

/* ---- pool lookup/allocation; caller must hold urpFontSem ---- */

static struct URPSharedFace *urp_shared_face_find(const char *path)
{
    int i;
    for (i = 0; i < URP_SHARED_FACE_MAX; i++)
        if (urpSharedFaces[i].face && strcmp(urpSharedFaces[i].path, path) == 0)
            return &urpSharedFaces[i];
    return NULL;
}

static struct URPSharedFace *urp_shared_face_find_free(void)
{
    int i;
    for (i = 0; i < URP_SHARED_FACE_MAX; i++)
        if (!urpSharedFaces[i].face) return &urpSharedFaces[i];
    return NULL;
}

static struct URPSharedSize *urp_shared_size_find(struct URPSharedFace *owner, int pointSize)
{
    int i;
    for (i = 0; i < URP_SHARED_SIZE_MAX; i++)
        if (urpSharedSizes[i].owner == owner && urpSharedSizes[i].pointSize == pointSize)
            return &urpSharedSizes[i];
    return NULL;
}

static struct URPSharedSize *urp_shared_size_find_free(void)
{
    int i;
    for (i = 0; i < URP_SHARED_SIZE_MAX; i++)
        if (!urpSharedSizes[i].owner) return &urpSharedSizes[i];
    return NULL;
}

/*
 * Configure a freshly created (and activated) FT_Size for pointSize on
 * face: FT_Set_Char_Size for scalable fonts, or FT_Select_Size (nearest
 * strike) for bitmap-only fonts (e.g. CBDT color emoji). Done once per
 * shared (face,pointSize) pair, at creation time -- everyday use is just
 * FT_Activate_Size, see urp_activate_face_size below.
 */
static int urp_configure_size(FT_Face face, int pointSize)
{
    FT_Error err;
    int i, best, diff, bestDiff, target;

    if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
        target   = pointSize;
        best     = 0;
        bestDiff = abs(face->available_sizes[0].height - target);
        for (i = 1; i < face->num_fixed_sizes; i++) {
            diff = abs(face->available_sizes[i].height - target);
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        err = FT_Select_Size(face, best);
        return (err == 0);
    }

    err = FT_Set_Char_Size(face, 0,
                           (FT_F26Dot6)(pointSize * 64),
                           URP_DPI_X, URP_DPI_Y);
    return (err == 0);
}

/*
 * Acquire a reference to the shared (face,pointSize) pair for a resolved
 * path, opening the face and/or creating the FT_Size the first time
 * either is needed. Bumps ss->refCount (and, on first use of the face,
 * sf->refCount too). Returns NULL if the file can't be opened/parsed or
 * a pool is full. Caller must hold urpFontSem.
 */
static struct URPSharedSize *urp_shared_font_acquire(const char *path, int pointSize)
{
    struct URPSharedFace *sf;
    struct URPSharedSize *ss;
    int sfIsNew = FALSE;

    sf = urp_shared_face_find(path);
    if (!sf) {
        sf = urp_shared_face_find_free();
        if (!sf) return NULL;
        if (FT_New_Face(urpSharedFTLib, path, 0, &sf->face) != 0) {
            sf->face = NULL;
            return NULL;
        }
        /* Explicitly request the Unicode charmap. FreeType usually
         * auto-selects it, but some CJK fonts carry Shift-JIS/Big5
         * charmaps alongside Unicode and auto-selection can land on the
         * wrong one. Ignore the error: if no Unicode cmap exists,
         * FreeType keeps its default choice. */
        FT_Select_Charmap(sf->face, FT_ENCODING_UNICODE);
        strncpy(sf->path, path, URP_PATH_MAX - 1);
        sf->path[URP_PATH_MAX - 1] = '\0';
        sf->refCount = 0;
        sfIsNew = TRUE;
    }

    ss = urp_shared_size_find(sf, pointSize);
    if (ss) {
        ss->refCount++;
        return ss;
    }

    ss = urp_shared_size_find_free();
    if (ss && FT_New_Size(sf->face, &ss->ftSize) == 0) {
        FT_Activate_Size(ss->ftSize);
        if (urp_configure_size(sf->face, pointSize)) {
            ss->owner     = sf;
            ss->pointSize = pointSize;
            ss->refCount  = 1;
            sf->refCount++;
            return ss;
        }
        FT_Done_Size(ss->ftSize);
    }

    /* Size pool full, or size creation/config failed: if we just opened
     * this face for a size that never materialised, nobody else
     * references it yet -- close it now instead of leaking an orphan. */
    if (sfIsNew) { FT_Done_Face(sf->face); sf->face = NULL; }
    return NULL;
}

/*
 * Drop one reference to a shared (face,pointSize) pair, freeing the
 * FT_Size (and, if it was the face's last user, the FT_Face) at zero.
 * Caller must hold urpFontSem.
 */
static void urp_shared_font_release(struct URPSharedSize *ss)
{
    struct URPSharedFace *sf;
    if (!ss) return;
    sf = ss->owner;
    if (ss->refCount > 0) ss->refCount--;
    if (ss->refCount > 0) return;
    FT_Done_Size(ss->ftSize);
    ss->owner  = NULL;
    ss->ftSize = NULL;
    if (sf && sf->refCount > 0) {
        sf->refCount--;
        if (sf->refCount == 0) { FT_Done_Face(sf->face); sf->face = NULL; }
    }
}

/*
 * Make fe's (face,pointSize) the one FT_Load_Glyph/metrics reads see on
 * its face. Cheap -- FT_Activate_Size only flips face->size to point at
 * fe->shared->ftSize, already fully configured -- no rescale, unlike the
 * old per-DC-private urp_set_face_size(), which called FT_Set_Char_Size
 * unconditionally. Must be called immediately before any size-dependent
 * FreeType call on this face, even if fe was also the last thing to use
 * it: another DC (same process or another one entirely, since faces are
 * shared system-wide) may have activated a different size on it since.
 * Caller must hold urpFontSem for the activate-then-use sequence.
 */
static void urp_activate_face_size(struct URPFontEntry *fe)
{
    FT_Activate_Size(fe->shared->ftSize);
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
    FT_Face               face;
    FT_GlyphSlot          slot;
    FT_Bitmap            *bm;
    struct URPGlyphEntry *entry;
    int   cachePixFmt;
    int   load_flags;
    int   srcW, srcH, dstW, dstH, pitch;
    int   cellH, scaleNum, scaleDen;
    UBYTE *pixels;
    WORD  bearingX, bearingY, advanceX;

    /* fe->shared->ftSize/face are shared system-wide: another DC (this
     * process or another one) may activate a different size on the same
     * face at any time via preemption. Everything that reads face->size
     * or face->glyph must therefore stay inside one urpFontSem-locked
     * section, from the activate through the last read of slot/bm below
     * -- releasing early would let a concurrent FT_Load_Glyph on the same
     * face clobber the single shared glyph slot before we've copied out
     * of it. */
    ObtainSemaphore(&urpFontSem);
    urp_activate_face_size(fe);
    face = fe->shared->owner->face;
/*re
    bdbprintf("urp_fill_cache_entry dc:%08lx task:%08lx cp:%04lx font:%s size:%ld style:%ld face:%08lx\n",
              (ULONG)dc, (ULONG)FindTask(NULL), (ULONG)cp, fe->path, (LONG)fe->pointSize,
              (LONG)dc->currentStyle, (ULONG)face);
*/
    /* Choose FreeType load flags.
     * FT_LOAD_TARGET_MONO asks the outline rasteriser to produce a 1-bit bitmap.
     * It must NOT be set for bitmap-only fonts (e.g. CBDT/CBLC color emoji):
     * doing so causes FreeType to convert the decoded BGRA bitmap to MONO,
     * hiding the color data and preventing URP_CACHE_RGBA from being chosen.
     * For bitmap-only fonts FT_LOAD_COLOR alone is sufficient. */
    load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
    if (!(dc->prefFlags & URP_PREF_ANTIALIAS) && FT_IS_SCALABLE(face))
        load_flags |= FT_LOAD_TARGET_MONO;

    /* For scalable fonts with active style, load outline only then transform+render. */
    if (dc->currentStyle && FT_IS_SCALABLE(face)) {
        int norender_flags = FT_LOAD_COLOR;
        if (!(dc->prefFlags & URP_PREF_ANTIALIAS))
            norender_flags |= FT_LOAD_TARGET_MONO;
        if (FT_Load_Glyph(face, gi, norender_flags)) {
            ReleaseSemaphore(&urpFontSem);
            return NULL;
        }
        slot = face->glyph;
        if (dc->currentStyle & URP_STYLE_BOLD)   FT_GlyphSlot_Embolden(slot);
        if (dc->currentStyle & URP_STYLE_ITALIC)  FT_GlyphSlot_Oblique(slot);
        {
            FT_Render_Mode rmode = (norender_flags & FT_LOAD_TARGET_MONO)
                                   ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL;
            if (FT_Render_Glyph(slot, rmode)) {
                ReleaseSemaphore(&urpFontSem);
                return NULL;
            }
        }
    } else {
        if (FT_Load_Glyph(face, gi, load_flags)) {
            ReleaseSemaphore(&urpFontSem);
            return NULL;
        }
        slot = face->glyph;
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
    if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
        cellH = (int)(face->size->metrics.height >> 6);
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
    if (!entry) { ReleaseSemaphore(&urpFontSem); return NULL; }

    pixels = NULL;
    if (dstW > 0 && dstH > 0) {
        pixels = (UBYTE *)AllocVec((ULONG)(pitch * dstH), MEMF_CLEAR);
        if (!pixels) { FreeVec(entry); ReleaseSemaphore(&urpFontSem); return NULL; }

        /* Reads bm (== &slot->bitmap), still the shared face's glyph
         * slot -- must happen before we release urpFontSem. */
        urp_blit_to_cache(dc, bm, pixels, dstW, dstH, pitch, cachePixFmt);
    }

    /* Capture the rest of what we need from the shared glyph slot now,
     * while still locked; nothing after ReleaseSemaphore() below may
     * touch slot/bm/face again. */
    bearingX = (WORD)((slot->bitmap_left  * scaleNum + scaleDen / 2) / scaleDen);
    bearingY = (WORD)((slot->bitmap_top   * scaleNum + scaleDen / 2) / scaleDen);
    advanceX = (WORD)(((slot->advance.x >> 6) * scaleNum + scaleDen / 2) / scaleDen);
    ReleaseSemaphore(&urpFontSem);

    entry->codepoint = cp;
    entry->style     = dc->currentStyle;
    entry->pixelFmt  = cachePixFmt;
    entry->width     = (WORD)dstW;
    entry->rows      = (WORD)dstH;
    entry->bearingX  = bearingX;
    entry->bearingY  = bearingY;
    entry->advanceX  = advanceX;
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

    if (!dc->screenClut) return;
    /* i=0 (alpha=0): use the exact background pen, bypassing clutRemap[] so
     * we never accidentally remap it to a neighbouring colour. */

    for (i = 0; i < 16; i++) {
        UBYTE r = (UBYTE)(((int)dc->background.argb.R * (15 - i) + (int)dc->draw.argb.R * i) / 15);
        UBYTE g = (UBYTE)(((int)dc->background.argb.G * (15 - i) + (int)dc->draw.argb.G * i) / 15);
        UBYTE b = (UBYTE)(((int)dc->background.argb.B * (15 - i) + (int)dc->draw.argb.B * i) / 15);
        dc->aaRemap[i] = dc->screenClut->clutRemap[
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
    dc->screenSharedClutSync = dc->screenClut->paletteHash;

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

    if (!dc->currentFriendBitmap || !dc->screenClut) return 0;
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
 * Maps each RGB triple through dc->screenClut->clutRemap[] (nearest-pen lookup).
 * The aaRemap shade ramp is not used here; colour comes from the glyph data.
 */
static int urp_build_clut_bitmaps_rgba(struct URPDrawContext *dc,
                                        struct URPGlyphEntry  *ge)
{
    UBYTE *chunky, *maskBuf;
    ULONG  size, x, y;
    UBYTE bgpen;
    if (!dc->currentFriendBitmap || !dc->screenClut) return 0;
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
                    dstRow[x] = dc->screenClut->clutRemap[
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
                    dstRow[x] = dc->screenClut->clutRemap[
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
    dc = (struct URPDrawContext *)AllocVec(sizeof(*dc), MEMF_PUBLIC|MEMF_CLEAR);
    if (!dc) return NULL;

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
/*re
    bdbprintf("URPDC_Create dc:%08lx name:%s useCount->1\n",
              (ULONG)dc, name ? name : "(null)");
*/
    return dc;
}


void URPDC_Retain(REG(a0, struct URPDrawContext *dc))
{
    if (!dc) return;

    dc->useCount++;
//re    bdbprintf("URPDC_Retain  dc:%08lx useCount->%ld\n", (ULONG)dc, (LONG)dc->useCount);
}
void URPDC_Release(REG(a0, struct URPDrawContext *dc))
{
    int i;
    if (!dc) return;

    if(dc->useCount>0 ) dc->useCount--;
//re    bdbprintf("URPDC_Release dc:%08lx useCount->%ld\n", (ULONG)dc, (LONG)dc->useCount);

    /* if some other object still use it, do not delete */
    if(dc->useCount>0 ) return;

//re    bdbprintf("URPDC_Release dc:%08lx task:%08lx useCount==0, DESTROYING now (numFonts:%ld)\n",
//re              (ULONG)dc, (ULONG)FindTask(NULL), (LONG)dc->numFonts);

    ObtainSemaphore(&dc->sem);

    urp_cache_free_all(&dc->cache);
    if(dc->tempChipRamAlloc)
    {
        FreeVec(dc->tempChipRamAlloc);
        dc->tempChipRamAlloc = NULL;
    }

    ObtainSemaphore(&urpFontSem);
    for (i = 0; i < dc->numFonts; i++) {
        if (dc->fonts[i].shared) {
            urp_shared_font_release(dc->fonts[i].shared);
            dc->fonts[i].shared = NULL;
        }
    }
    ReleaseSemaphore(&urpFontSem);

    if (dc->screenClut) {
        ObtainSemaphore(&urpClutSem);
        urp_shared_clut_release(dc->screenClut);
        ReleaseSemaphore(&urpClutSem);
        dc->screenClut = NULL;
    }

    if (dc->hqScratch) { FreeVec(dc->hqScratch); dc->hqScratch = NULL; }

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

/*
 * Try FONTS:<name>, then PROGDIR:fonts/<name>, then <name> verbatim,
 * accepting the first whose header passes urp_font_magic_ok(). Writes
 * the winning path into resolved (URP_PATH_MAX bytes) and returns TRUE,
 * or FALSE if none of the three exist/look like a font file. Split out
 * of URPDC_AddFont so path resolution (no pool state touched) happens
 * before the shared-pool lookup keyed on the resolved path.
 */
static BOOL urp_resolve_font_path(const char *fontPath, char *resolved)
{
    resolved[0] = 0;
    strcat(resolved, "FONTS:");
    strcat(resolved, fontPath);
    if (urp_font_magic_ok(resolved)) return TRUE;

    resolved[0] = 0;
    strcat(resolved, "PROGDIR:fonts/");
    strcat(resolved, fontPath);
    if (urp_font_magic_ok(resolved)) return TRUE;

    if (urp_font_magic_ok(fontPath)) {
        strncpy(resolved, fontPath, URP_PATH_MAX - 1);
        resolved[URP_PATH_MAX - 1] = '\0';
        return TRUE;
    }
    return FALSE;
}

int URPDC_AddFont(REG(a0, struct URPDrawContext *dc),
                  REG(a1, const char           *fontPath),
                  REG(d0, int                   pointSize),
                  REG(d1, ULONG                 flags))
{
    struct URPFontEntry  *fe;
    struct URPSharedSize *ss;
    char resolved[URP_PATH_MAX];

    if (!dc || !fontPath) return 0;
    if (dc->numFonts >= URP_MAX_FONTS) return 0;

    /* first FONTS:fontname.ttf, then PROGDIR:fonts/, then raw path --
     * whichever passes the magic-byte check first. No pool state touched
     * yet: the shared pool is keyed on this resolved path. */
    if (!urp_resolve_font_path(fontPath, resolved))
        return 0;

    ObtainSemaphore(&urpFontSem);
    ss = urp_shared_font_acquire(resolved, pointSize);
    ReleaseSemaphore(&urpFontSem);
    if (!ss) {
        // bdbprintf("URPDC_AddFont dc:%08lx path:%s size:%ld FAILED (acquire)\n",
        //           (ULONG)dc, resolved, (LONG)pointSize);
        return 0;
    }

    ObtainSemaphore(&dc->sem);
    fe = &dc->fonts[dc->numFonts];
    fe->shared = ss;
    strncpy(fe->path, fontPath, URP_PATH_MAX - 1);
    fe->path[URP_PATH_MAX - 1] = '\0';
    fe->pointSize = pointSize;
    fe->flags     = flags;
    dc->numFonts++;
    dc->monoAdvanceX = 0; /* invalidate: primary font may have changed */
    // bdbprintf("URPDC_AddFont dc:%08lx path:%s size:%ld slot:%ld numFonts->%ld shared:%08lx(refs %ld) face:%08lx(refs %ld)\n",
    //           (ULONG)dc, resolved, (LONG)pointSize, (LONG)(dc->numFonts - 1),
    //           (LONG)dc->numFonts, (ULONG)ss, (LONG)ss->refCount,
    //           (ULONG)ss->owner, (LONG)ss->owner->refCount);
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
            ObtainSemaphore(&urpFontSem);
            urp_shared_font_release(dc->fonts[i].shared);
            ReleaseSemaphore(&urpFontSem);
            for (j = i; j < dc->numFonts - 1; j++)
                dc->fonts[j] = dc->fonts[j + 1];
            dc->numFonts--;
            memset(&dc->fonts[dc->numFonts], 0, sizeof(dc->fonts[0]));
            dc->monoAdvanceX = 0;
    //         bdbprintf("URPDC_RemoveFont dc:%08lx path:%s size:%ld numFonts->%ld\n",
    //                   (ULONG)dc, fontPath, (LONG)pointSize, (LONG)dc->numFonts);
    ReleaseSemaphore(&dc->sem);
            return;
        }
    }
    // bdbprintf("URPDC_RemoveFont dc:%08lx path:%s size:%ld NOT FOUND\n",
    //           (ULONG)dc, fontPath, (LONG)pointSize);
    ReleaseSemaphore(&dc->sem);
}

void URPDC_FlushFonts(REG(a0, struct URPDrawContext *dc))
{
    int i;

    if (!dc) return;
//    bdbprintf("URPDC_FlushFonts dc:%08lx numFonts:%ld -> 0\n", (ULONG)dc, (LONG)dc->numFonts);
    ObtainSemaphore(&dc->sem);
    ObtainSemaphore(&urpFontSem);
    for (i = 0; i < dc->numFonts; i++) {
        if (dc->fonts[i].shared)
            urp_shared_font_release(dc->fonts[i].shared);
    }
    ReleaseSemaphore(&urpFontSem);
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
    /* Sanity floor only -- FT_Set_Char_Size(face, 0, 0, ...) is degenerate.
     * No upper/lower policy clamp here: URPDC_AddFont applies none, and a
     * mismatched clamp between the two entry points made a font's actual
     * size depend on which API last touched it (e.g. FriendSh3ep's
     * dcUsername legitimately reaches 81pt; a hardcoded 64pt cap here
     * silently overrode that on the second resize while AddFont's first
     * call had honored it). Callers own their valid range. */
    if(nPointSize<1) nPointSize=1;

    // bdbprintf("URPDC_ChangeFontsSize dc:%08lx size:%ld mask:%08lx (numFonts:%ld)\n",
    //           (ULONG)dc, (LONG)nPointSize, (ULONG)fontMask, (LONG)dc->numFonts);

    ObtainSemaphore(&dc->sem);
    ObtainSemaphore(&urpFontSem);
     for (i = 0; i < dc->numFonts; i++) {
        if ((fontMaskBit &fontMask) &&
            dc->fonts[i].shared &&
             dc->fonts[i].pointSize != (int)nPointSize )
        {
            /* Swap to the shared (face,pointSize) pair for the new size --
             * reusing one that another DC/font-entry already has open
             * (e.g. dcUsername already at this exact size) costs nothing
             * but a refcount bump. Keep the old one on failure. */
            struct URPSharedSize *newSs = urp_shared_font_acquire(
                dc->fonts[i].shared->owner->path, (int)nPointSize);
            if (newSs) {
                urp_shared_font_release(dc->fonts[i].shared);
                dc->fonts[i].shared    = newSs;
                dc->fonts[i].pointSize = (int)nPointSize;
                anyChange = TRUE;
            }
        }
        fontMaskBit<<=1;
    }
    ReleaseSemaphore(&urpFontSem);
    if(anyChange) {
        dc->monoAdvanceX = 0;
        urp_cache_free_all(&dc->cache);
    }
    ReleaseSemaphore(&dc->sem);
}




void URPDC_SetPreferenceFlags(REG(a0, struct URPDrawContext *dc), REG(d0, ULONG flags))
{
    if (!dc) return;
    ObtainSemaphore(&dc->sem);
    if (dc->prefFlags != flags) {
        dc->prefFlags    = flags;
        dc->monoAdvanceX = 0; /* invalidate: FORCE_MONOSPACE may have changed */
        urp_cache_free_all(&dc->cache); /* flush: pixel format may have changed */
    }
    ReleaseSemaphore(&dc->sem);
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

    ObtainSemaphore(&dc->sem);
        dc->draw.ARGB = textRGB;
        dc->background.ARGB = backgroundRGB;

        // bdbprintf("URPDC_SetDrawColor dc:%08lx txt:%08lx bg:%08lx CHANGED -> flush+rebuild\n",
        //           (ULONG)dc, (ULONG)textRGB, (ULONG)backgroundRGB);

        /* GRAY CLUT bitmaps are baked with the draw colour; flush them so they
         * are rebuilt with the new colour on the next render call. */
        urp_flush_clut_bitmaps(&dc->cache);

        if(dc->screenClut)
        {
            urp_rebuild_aa_remap(dc);
        }

    ReleaseSemaphore(&dc->sem);
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

    /* was: "if (!dc || !screen) { dc->currentFriendBitmap = NULL; return; }"
     * -- dereferenced dc even in the !dc branch. Pre-existing bug, fixed
     * here while instrumenting this function; unrelated to the tracing. */
    if (!dc) return;

    ObtainSemaphore(&dc->sem);
    if (!screen)
    {
        dc->currentFriendBitmap = NULL;
        ReleaseSemaphore(&dc->sem);
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
    // bdbprintf("URPDC_SetDrawColorFromPen dc:%08lx scr:%08lx txtPen:%ld bgPen:%ld differs:%ld\n",
    //           (ULONG)dc, (ULONG)screen, (LONG)txtPen, (LONG)backgroundpen, (LONG)colorsDiffers);
    if(colorsDiffers)
    {
        urp_flush_clut_bitmaps(&dc->cache);
        urp_rebuild_aa_remap(dc);
    }
    ReleaseSemaphore(&dc->sem);
}
/*
 * Point dc at the shared CLUT table (struct URPSharedScreenClut) for
 * (screen,depth): reuse the one already bound if it matches (just
 * re-checking freshness), otherwise release the old reference (if any)
 * and acquire the matching/new one. Shared by URPDC_SetDrawScreen (which
 * only calls this after its own cheap screen-pointer/depth pre-check
 * finds a real change) and URPDC_UpdateColorMap (which has no such
 * pre-check -- its whole job is to notice a palette change on a screen
 * the DC was already bound to). Must be called with dc->sem already held.
 */
static ULONG urp_dc_bind_screen_clut(struct URPDrawContext *dc, struct Screen *screen, ULONG depth)
{
    BOOL rebuilt;
    struct URPSharedScreenClut *oldSc;

    if (!screen || !screen->ViewPort.ColorMap) return 0;

    oldSc = dc->screenClut;
    ObtainSemaphore(&urpClutSem);
    if (dc->screenClut && dc->screenClut->screen == screen && dc->screenClut->depth == depth) {

        /* dc->clutValid must keep state */
    } else {
        struct URPSharedScreenClut *sc = urp_shared_clut_acquire(screen, depth);
        if (dc->screenClut) urp_shared_clut_release(dc->screenClut);
        dc->screenClut = sc;

    }

    ReleaseSemaphore(&urpClutSem);

    if (!dc->screenClut) { return 0; }

    return TRUE;
}

/* like URPDC_UpdateColorMap(), but only act if screen changed */
ULONG URPDC_SetDrawScreen(REG(a0, struct URPDrawContext *dc), REG(a1, struct Screen *screen))
{
    ULONG ndepth;
    ULONG result = 0;

    if (!dc || !screen || !screen->RastPort.BitMap) return 0;


    ndepth = GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);
    if (dc->lastScreen != screen || ndepth != dc->lastScreenDepth) {

        ObtainSemaphore(&dc->sem);

            dc->lastScreen      = screen;
            dc->lastScreenDepth = ndepth;

            result = urp_dc_bind_screen_clut(dc, screen, ndepth);

        ReleaseSemaphore(&dc->sem);

    }
    /* if color have to be updated when screen new
     - note does not check palette change, URPC_UpdateColor does -
     */
    if(dc->screenClut)
    {
        LONG rebuilt = urp_clut_ensure_fresh(dc->screenClut, screen);
     // bdbprintf("URPDC_SetDrawScreen color update clutValid:%d rebuilt:%d\n",
     //    dc->clutValid,rebuilt);

        if(rebuilt || dc->screenSharedClutSync != dc->screenClut->paletteHash)
        {
            /* as clut is shared, could have not been refresh, but the dc map must */
            urp_rebuild_aa_remap(dc);
            /* Old CLUT bitmaps may have been built under a stale palette; */
            urp_flush_clut_bitmaps(&dc->cache);

        }
    }
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
 *
 * Unlike URPDC_SetDrawScreen, this always at least checks whether the
 * palette changed (no screen-pointer/depth shortcut) -- that's the point
 * of calling it: the screen may be unchanged but its palette may not be
 * (e.g. an image load allocated new pens). The actual 4096-entry rebuild
 * only happens if urp_clut_ensure_fresh's cheap hash says something
 * really did change; if this DC's screen+depth already match another
 * DC's shared table that's already current, this call costs one palette
 * read-and-hash and nothing else.
 */
ULONG URPDC_UpdateColorMap(REG(a0, struct URPDrawContext *dc), REG(a1, struct Screen *screen))
{
    ULONG depth, result;
    if (!dc || !screen || !screen->RastPort.BitMap) return 0;

    ObtainSemaphore(&dc->sem);
        depth = GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);
        dc->lastScreen      = screen;
        dc->lastScreenDepth = depth;
        result = urp_dc_bind_screen_clut(dc, screen, depth);

        /* note: even if clut valid, verify if need update */
        {
            LONG rebuilt = urp_clut_ensure_fresh(dc->screenClut, screen);
            if(rebuilt || dc->screenSharedClutSync != dc->screenClut->paletteHash)
            {
                urp_rebuild_aa_remap(dc);
                /* Old CLUT bitmaps may have been built under a stale palette; */
                urp_flush_clut_bitmaps(&dc->cache);

            }
        }


    ReleaseSemaphore(&dc->sem);
    return result;
}

/*
 * URPDC_RemapRGB24ToPen8() – bulk RGB24→CLUT pen remap using the table
 * built by URPDC_UpdateColorMap()/URPDC_SetDrawScreen(). Same nearest-pen
 * lookup formula as the internal glyph remapper
 * (urp_build_clut_bitmaps_rgba() above): index = (R>>4)<<8 | (G>>4)<<4 |
 * (B>>4) into dc->screenClut->clutRemap[].
 *
 * srcRGB is pixelCount tightly-packed 3-byte (R,G,B) pixels (e.g. a decoded
 * thumbnail); dstPen receives one pen byte per pixel, suitable for
 * graphics.library/WritePixelArray8(). Intended for depth<=8 screens where
 * cybergraphics.library/ScalePixelArray()'s own RECTFMT_LUT8 path isn't
 * available/desired (see FriendSh3ep/PlanToReworkThumbnails.txt step 2-3).
 *
 * Returns 0 (nothing written) if dc/srcRGB/dstPen is NULL or the colour map
 * hasn't been built yet (dc->clutValid == 0) -- call
 * URPDC_UpdateColorMap()/URPDC_SetDrawSURPDC_RemapRGB24ToPen8creen() first. Returns pixelCount on
 * success.
 */
ULONG URPDC_RemapRGB24ToPen8(REG(a0, struct URPDrawContext *dc),
                              REG(a1, CONST UBYTE *srcRGB),
                              REG(a2, UBYTE *dstPen),
                              REG(d0, ULONG pixelCount))
{
    ULONG i;
    UBYTE  *clutRemap; /*[4096];*/
    if (!dc || !srcRGB || !dstPen) return 0;
    if (!dc->screenClut) return 0;

    clutRemap = &dc->screenClut->clutRemap[0];
    for (i = 0; i < pixelCount; i++) {
        const UBYTE *sp = srcRGB + i * 3UL;
        dstPen[i] = clutRemap[
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
            case URPDCA_TabOriginX:
                dc->tabOriginX = (LONG)v;
                break;
        }
    }
}

/* =========================================================================
 * urp_tab_advance – pixel advance width for one tab character
 *
 * Aligns to the next multiple of (tabSpaces * unit-advance) measured from
 * the true start of the logical line, not a fixed jump: curX is the pen's
 * current local x (whatever the caller already tracks -- totalAdvance,
 * curX, pos->x); dc->tabOriginX (see URPDCA_TabOriginX) corrects it to the
 * true line-relative x when curX's zero point isn't the line's own start
 * (e.g. a mid-line tile/selection redraw).  Guarantees at least one
 * unit-advance of width so a tab never collapses to a sliver.
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

INLINE WORD urp_tab_advance(struct URPDrawContext *dc, LONG curX)
{
    LONG unitAdv, tabWidth, absX, advance;

    if (dc->prefFlags & URP_PREF_FORCE_MONOSPACE) {
        if (dc->monoAdvanceX <= 0)
            urp_compute_mono_advance(dc);
        unitAdv = (dc->monoAdvanceX > 0) ? dc->monoAdvanceX : 8;
    } else {
        struct URPGlyphEntry *ge = urp_get_glyph(dc, 0x20UL, NULL, NULL);
        unitAdv = (ge && ge->advanceX > 0) ? ge->advanceX : 8;
    }

    tabWidth = (LONG)dc->tabSpaces * unitAdv;
    if (tabWidth <= 0) tabWidth = (unitAdv > 0) ? unitAdv : 8;

    /* Align to the next tab stop measured from the true line start. */
    absX    = curX + dc->tabOriginX;
    advance = (((absX / tabWidth) + 1) * tabWidth) - absX;

    /* Never let a tab collapse to less than one full unit-advance wide. */
    if (advance < unitAdv) advance += tabWidth;

    return (WORD)advance;
}

static WORD urp_font_ascender(struct URPDrawContext *dc)
{
    int i, asc, maxAscend = 0;
    for (i = 0; i < dc->numFonts; i++) {
        struct URPFontEntry *fe = &dc->fonts[i];
        FT_Face face;
        int scaleNum = 1, scaleDen = 1;
        if (!fe->shared) continue;
        ObtainSemaphore(&urpFontSem);
        urp_activate_face_size(fe);
        face = fe->shared->owner->face;
        asc = (int)(face->size->metrics.ascender >> 6);
        if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
            int cellH = (int)(face->size->metrics.height >> 6);
            if (cellH > 0 && cellH != fe->pointSize) {
                scaleNum = fe->pointSize;
                scaleDen = cellH;
            }
            if (asc <= 0) asc = cellH;
        }
        ReleaseSemaphore(&urpFontSem);
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
        FT_Face face;
        FT_Pos raw_dsc;
        int scaleNum = 1, scaleDen = 1;
        if (!fe->shared) continue;
        ObtainSemaphore(&urpFontSem);
        urp_activate_face_size(fe);
        face = fe->shared->owner->face;
        asc = (int)(face->size->metrics.ascender >> 6);
        raw_dsc = face->size->metrics.descender;
        dsc = (int)((-raw_dsc) >> 6);
        if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
            int cellH = (int)(face->size->metrics.height >> 6);
            if (cellH > 0 && cellH != fe->pointSize) {
                scaleNum = fe->pointSize;
                scaleDen = cellH;
            }
            if (asc <= 0) { asc = cellH; dsc = 0; }
        }
        ReleaseSemaphore(&urpFontSem);
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
    /* This measures the whole string from its own start, so tab stops are
     * always relative to totalAdvance's own zero -- ignore any origin
     * correction a mid-line draw call may have left set. */
    dc->tabOriginX = 0;
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

        /* Tab: advance to the next tab stop (min one unit-advance wide) */
        if (cp == 0x09) {
            totalAdvance += (WORD)urp_tab_advance(dc, totalAdvance);
            continue;
        }

        /* Variation selector: no glyph, no width. */
        if (urp_is_variation_selector(cp)) continue;

        fe = NULL; gi = 0;
        ObtainSemaphore(&urpFontSem);
        for (i = 0; i < dc->numFonts; i++) {
            gi = FT_Get_Char_Index(dc->fonts[i].shared->owner->face, (FT_ULong)cp);
            if (gi != 0) { fe = &dc->fonts[i]; break; }
        }
        ReleaseSemaphore(&urpFontSem);
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
        FT_Face face;
        FT_Pos raw_dsc;
        int scaleNum = 1;
        int scaleDen = 1;

        if (!fe->shared) continue;

        ObtainSemaphore(&urpFontSem);
        urp_activate_face_size(fe);
        face = fe->shared->owner->face;

        asc = (int)(face->size->metrics.ascender >> 6);

        /* descender is a negative FT_Pos; negate before shifting to avoid
         * implementation-defined behaviour on negative right-shifts. */
        raw_dsc = face->size->metrics.descender;
        dsc = (int)((-raw_dsc) >> 6);

        /* Bitmap-only fonts (PNG/CBDT/CBLC emoji) report metrics at the native
         * strike size, not at pointSize.  Mirror the same scale used in
         * urp_fill_cache_entry so the metrics reflect actual rendered pixels. */
        if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
            int cellH = (int)(face->size->metrics.height >> 6);
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
        ReleaseSemaphore(&urpFontSem);

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

    /* This always measures the whole line from char 0 (see caller
     * uted_line_build_metrics), so tab stops are relative to totalAdvance's
     * own zero -- ignore any origin correction a draw call may have left. */
    dc->tabOriginX = 0;
    p         = (const unsigned char *)utf8;
    remaining = (maxChars < 0) ? INT_MAX : maxChars;

    while (1) {
        cp = urp_utf8_next(&p, &remaining);

        *arrayout++ = totalAdvance;
        if (cp == 0) break;

        /* Tab: advance to the next tab stop (min one unit-advance wide) */
        if (cp == 0x09) {
            totalAdvance += (WORD)urp_tab_advance(dc, totalAdvance);
            continue;
        }

        /* Variation selector: no glyph, no width. */
        if (urp_is_variation_selector(cp)) continue;

        fe = NULL; gi = 0;
        ObtainSemaphore(&urpFontSem);
        for (i = 0; i < dc->numFonts; i++) {
            gi = FT_Get_Char_Index(dc->fonts[i].shared->owner->face, (FT_ULong)cp);
            if (gi != 0) { fe = &dc->fonts[i]; break; }
        }
        ReleaseSemaphore(&urpFontSem);
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
    ObtainSemaphore(&urpFontSem);
    for (i = 0; i < dc->numFonts; i++) {
        gi = FT_Get_Char_Index(dc->fonts[i].shared->owner->face, (FT_ULong)cp);
        if (gi != 0) { fe = &dc->fonts[i]; break; }
    }
    ReleaseSemaphore(&urpFontSem);
    if (fe_out) *fe_out = fe;
    if (gi_out) *gi_out = gi;

    if (!fe) return NULL;

    ge = urp_cache_lookup(&dc->cache, (ULONG)cp, dc->currentStyle);
    if (!ge) {
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
                curX += urp_tab_advance(hd->dc, curX);
                continue;
            }
            if (urp_is_variation_selector(cp)) continue;
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
                curX += urp_tab_advance(hd->dc, curX);
                continue;
            }
            if (urp_is_variation_selector(cp)) continue;
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
            pos->x += urp_tab_advance(dc, pos->x);
            continue;
        }

        if (urp_is_variation_selector(cp)) continue;

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
            pos->x += urp_tab_advance(dc, pos->x);
            continue;
        }

        if (urp_is_variation_selector(cp)) continue;

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
            pos->x += urp_tab_advance(dc, pos->x);
            continue;
        }

        if (urp_is_variation_selector(cp)) continue;

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
                if (dc->screenClut && dc->currentFriendBitmap) {
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
                if (dc->screenClut && dc->currentFriendBitmap) {
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
            pos->x += urp_tab_advance(dc, pos->x);
            continue;
        }

        if (urp_is_variation_selector(cp)) continue;

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
                if (dc->screenClut && dc->currentFriendBitmap) {
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
                if (dc->screenClut && dc->currentFriendBitmap) {
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
    /* URPDCA_TabOriginX (if set) applies to this call only -- consume it
     * unconditionally so a shared/external dc never leaks it into an
     * unrelated draw. */
    if(*utf8 == 0) { dc->tabOriginX = 0; return; } /* empty string, happens a lot. */
    dc->numberOfGlyphsNotFound = 0;
    p         = (const unsigned char *)utf8;
    remaining = ((int)maxChars < 0) ? 32767 : maxChars;

    /* task:%08lx identifies which task actually called this -- compare
     * this value across a hexButton draw and a TootTimeline draw sharing
     * the same dc to confirm/refute whether they really run on the same
     * process, instead of assuming it. */
    // bdbprintf("URPDrawTextUTF8 dc:%08lx task:%08lx bm:%08lx text:\"%.24s\"\n",
    //           (ULONG)dc, (ULONG)FindTask(NULL), (ULONG)rp->BitMap, utf8);

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
    dc->tabOriginX = 0;

}
