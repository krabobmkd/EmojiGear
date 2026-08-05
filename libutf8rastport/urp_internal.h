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
 * Font entry
 * ------------------------------------------------------------------------- */

#define URP_PATH_MAX  256

struct URPFontEntry {
    FT_Face  face;
    char     path[URP_PATH_MAX];
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
    FT_Library           library;
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

    int   numberOfGlyphsNotFound;
    ULONG codeNotFound[MAX_CODE_NOT_FOUND];

    /* RGB444 → CLUT pen remap table; clutValid set by URPDC_UpdateColorMap() */
    UBYTE clutRemap[4096];

    /* 16-entry AA shade ramp for GRAY glyphs on CLUT screens */
    UBYTE aaRemap[16];
    UBYTE clut_pad[3];
    UBYTE clutValid;/* moved for alignment, goes with clutRemap */

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
