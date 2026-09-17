/*
 * urp_internal.h  –  Internal struct definitions shared across libutf8rastport.
 *
 * Included by utf8rastport.c and urp_cgx_blend.c.
 * NOT part of the public API; external callers see struct URPDrawContext as opaque.
 */

#ifndef URP_INTERNAL_H
#define URP_INTERNAL_H

#include <exec/types.h>
#include <exec/semaphores.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H
#include FT_SIZES_H

#include <libraries/utf8rastport.h>   /* public constants: URP_CACHE_*, URP_PREF_*, URP_PATH_MAX etc. */


/* -------------------------------------------------------------------------
 * Internal constants
 * ------------------------------------------------------------------------- */

#define URP_MAX_FONTS         8
#define URP_DPI_X             72
#define URP_DPI_Y             72
#define URP_REPLACEMENT_CHAR  0xFFFDUL
#define MAX_CODE_NOT_FOUND    8
#define URP_GLYPH_HASH        256

/* Reserved cache key for the "missing glyph" tofu box. */
#define URP_CP_NOTFOUND  0xFFFFFFFFUL


/* -------------------------------------------------------------------------
 * Glyph cache entry
 * ------------------------------------------------------------------------- */

struct URPGlyphEntry {
    ULONG  codepoint;
    /* kepp 4b alignment as possible */
    UBYTE   style;       /* URP_STYLE_* – part of the cache key */
    UBYTE   pixelFmt;    /* URP_CACHE_MONO / GRAY / RGBA */
    UBYTE   DUMMY; // pixelsIsInChip; /* bool, may realloc pixels */
    UBYTE   pad;

    WORD   width;
    WORD   rows;

    WORD   bearingX;
    WORD   bearingY;

    WORD   advanceX;
    WORD   pitch;

    UBYTE *pixels;  /* the freetype gray or rgb format */
    UBYTE *chk_clutBitmap; /* the remaped chunky image we can use with WritePixelArray() */
    struct BitMap *clutBitmap;
    struct BitMap *maskBitmap;
    struct URPGlyphEntry *next;
};

struct URPGlyphCache {
    struct URPGlyphEntry *buckets[URP_GLYPH_HASH];
};


/* -------------------------------------------------------------------------
 * Shared font pool -- FT_Face/FT_Size objects are opened at most once
 * per (resolved path, point size) system-wide, refcounted, and shared by
 * every URPDrawContext in every process that has this library open (a
 * classic Amiga shared library has exactly one instance of its global
 * data; every OpenLibrary() caller reaches the same one). Guarded by the
 * library-global urpFontSem semaphore (utf8rastport.c), not by any
 * per-DC dc->sem.
 *
 * Two levels, refcounted independently:
 *   URPSharedFace  -- one per distinct font file (path). Owns the parsed
 *                     FT_Face (tables, outlines, cmap). Freed via
 *                     FT_Done_Face when its last URPSharedSize goes away.
 *   URPSharedSize  -- one per (face, pointSize) pair actually in use.
 *                     Owns an FT_Size created via FT_New_Size, pre-
 *                     configured once (FT_Set_Char_Size/FT_Select_Size)
 *                     at creation. Using it afterwards is just
 *                     FT_Activate_Size -- a pointer flip, no rescale --
 *                     which is what makes sharing one face across DCs at
 *                     different sizes (dcNormal/dcUsername/dcMini) cheap
 *                     even when draws interleave sizes glyph-by-glyph;
 *                     without per-size objects, reasserting a size on a
 *                     shared face via FT_Set_Char_Size would force a
 *                     rescale on every switch.
 *
 * All memory FreeType itself allocates for a shared face/size (tables,
 * outlines, glyph slot) already comes from an AllocMem(..., MEMF_PUBLIC)-
 * backed pool -- see builds/amiga/src/base/ftsystem.c's FT_New_Memory --
 * so it's already safe to touch from whichever process's task happens to
 * be executing library code at the time.
 * ------------------------------------------------------------------------- */

#define URP_PATH_MAX  256

#define URP_SHARED_FACE_MAX  16
#define URP_SHARED_SIZE_MAX  64

struct URPSharedFace {
    FT_Face face;                 /* NULL = free slot */
    char    path[URP_PATH_MAX];
    ULONG   refCount;             /* number of URPSharedSize entries owned */
};

struct URPSharedSize {
    struct URPSharedFace *owner;  /* NULL = free slot */
    FT_Size ftSize;
    int     pointSize;
    ULONG   refCount;             /* number of per-DC URPFontEntry's using this pair */
};

/* -------------------------------------------------------------------------
 * Shared screen CLUT remap table -- the RGB444->pen nearest-match table
 * (and the palette snapshot it was built from) for one (Screen*, depth)
 * pair, shared by every URPDrawContext bound to that screen instead of
 * each keeping its own private 4096-entry copy. Building it is the
 * expensive part (up to 4096*256 distance comparisons); rebuilding it
 * redundantly per DC for an unchanged screen+palette is pure waste, and
 * happens a lot in practice: FriendSh3ep alone rebinds 3 draw contexts
 * (dcNormal/dcUsername/dcMini) to the same screen on every font-size
 * change and on every "palette may have changed" refresh (e.g. after an
 * image load allocates new pens).
 *
 * paletteHash is a cheap folded checksum of the palette entries actually
 * read to answer "did anything change" -- reading those entries via
 * GetRGB32 is unavoidable even just to check, but it's far cheaper than
 * the 4096-entry nearest-pen search, which only runs when the hash
 * doesn't match (or the slot is brand new: refCount == 0 forces it).
 *
 * Guarded by urpClutSem (utf8rastport.c), a separate semaphore from
 * urpFontSem -- the two pools are independent and never nested with each
 * other, only each with a per-DC dc->sem (outer).
 * ------------------------------------------------------------------------- */

#define URP_SHARED_CLUT_MAX  8

struct URPSharedScreenClut {
    struct Screen *screen;      /* NULL = free slot */
    ULONG          depth;
    ULONG          paletteHash;
    UBYTE          clutRemap[4096];
    ULONG          refCount;
};

/* -------------------------------------------------------------------------
 * Font entry
 * ------------------------------------------------------------------------- */

struct URPFontEntry {
    struct URPSharedSize *shared;  /* face+size this entry currently uses */
    char     path[URP_PATH_MAX];   /* caller-supplied path, for RemoveFont matching */
    int      pointSize;
    ULONG    flags;
};


/* -------------------------------------------------------------------------
 * Draw context  (full definition – internal use only)
 * ------------------------------------------------------------------------- */

struct sARGB { UBYTE A, R, G, B; };

struct URPDrawContext {
    struct SignalSemaphore sem;
    ULONG               useCount;
    struct URPFontEntry  fonts[URP_MAX_FONTS];
    int                  numFonts;
    /* last screen bitmap we have drawn with,
     *  would flush remaped cache if change. */
    struct BitMap       *currentFriendBitmap;
    struct Screen       *lastScreen; /* just to check screen id difference */
    ULONG               lastScreenDepth; /* in case lastscreen has same pointer */

    UBYTE                currentStyle;  /* URP_STYLE_* set by URPDC_SetStyle() */
    ULONG                prefFlags;
    struct URPGlyphCache cache;

    /* need blitting from chip if native mode...
     * P96 hack something to blit chip from fast
     * but it can be generalized.
     */
    UBYTE *tempChipRamA;
    UBYTE *tempChipRamB;
    UBYTE *tempChipRamAlloc;
    ULONG tempChipRamSize;

    /* Foreground colour for GRAY glyph rendering (default white) */
    union {
        ULONG        ARGB;
        struct sARGB argb;
    } draw;

    /* Background colour for AA shade ramp interpolation on CLUT screens */
    union {
        ULONG        ARGB;
        struct sARGB argb;
    } background;

    LONG bgPen;   /* if < 0, not applied */
    LONG txtPen;  /* if < 0, not applied */
    LONG pensSet; /* 0 if never inited */

    /* Tab spacing: number of space-glyph advances per tab character.
     * Range [1..12], default 4.  Set via URPDC_SetAttribsA(). */
    ULONG tabSpaces;

    /* Correction added to the draw loop's local pen x to recover the true
     * pixel x since the start of the logical line, for tab-stop alignment.
     * A caller that draws a line in one shot from x=0 leaves this at its
     * default of 0.  A caller that draws a mid-line slice (tile cache,
     * selection-overlay redraw, ...) must set it to
     * (trueLineX_of_firstChar - pos.x) immediately before the draw call,
     * via URPDC_SetAttribsA(dc, {URPDCA_TabOriginX, value, TAG_DONE}).
     * Not persistent: callers are expected to set it before every draw
     * that needs a non-zero value. */
    LONG tabOriginX;

    int   numberOfGlyphsNotFound;
    ULONG codeNotFound[MAX_CODE_NOT_FOUND];

    /* RGB444 -> CLUT pen remap table for lastScreen/lastScreenDepth, shared
     * with every other DC bound to the same screen -- see struct
     * URPSharedScreenClut. clutValid mirrors (screenClut != NULL) as a
     * quick flag; set/cleared only in urp_dc_bind_screen_clut(). */
    struct URPSharedScreenClut *screenClut;

    /* hash sync from shared clut state, same when local aa table rebuilt */
    ULONG screenSharedClutSync;

    /* 16-entry AA shade ramp for GRAY glyphs on CLUT screens -- stays
     * per-DC: it also depends on this DC's own draw/background colours,
     * not just the shared screen palette (see urp_rebuild_aa_remap). */
    UBYTE aaRemap[16];
   // UBYTE clut_pad[3];
    /*  UBYTE clutValid was antipattern, we rely only on:
     (screenClut && screenClut->hash == screenSharedClutSync)
    */
    /* Scratch buffer for URP_PREF_HIGHFILTERING pyramid downscaling.
     * Allocated lazily, grown as needed, freed in URPDC_Destroy.
     * Holds one BGRA intermediate level (at most srcW/2 × srcH/2 × 4 bytes). */
    UBYTE *hqScratch;
    ULONG  hqScratchBytes;

    /* Cached advance width for URP_PREF_FORCE_MONOSPACE: advance of 'M' in
     * the primary font.  0 means not yet computed (computed lazily). */
    WORD monoAdvanceX;


    /* experimental */
    int saveChipMode;
};

#endif /* URP_INTERNAL_H */
