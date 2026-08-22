/*
 * utf8rastport.h  –  UTF-8 text rendering on AmigaOS RastPorts (via FreeType2).
 *
 * Renders Unicode text (UTF-8 encoded) to any AmigaOS RastPort.
 * Supports TrueType (.ttf/.ttc) and OpenType (.otf/.otc) fonts.
 * Manages a fallback font chain: the first font in the list that contains
 * a glyph for a given codepoint is used.
 *
 * Glyph cache modes:
 *   URP_CACHE_MONO  – 1-bit/pixel, MSB-first, word-aligned rows.
 *                     Drawn with BltTemplate().  Works on any screen depth.
 *   URP_CACHE_GRAY  – 1-byte/pixel alpha mask.  Custom alpha-blend renderer
 *                     (future).
 *   URP_CACHE_RGBA  – 4-bytes/pixel R,G,B,A.  CyberGfx WritePixelArray /
 *                     alpha-blend path (future).
 *
 * The cache mode is chosen automatically from the DrawContext preferences
 * and the pixel format returned by FreeType (color emoji always → RGBA).
 * Scaling to the requested point size is applied at cache-fill time, so
 * embedded-bitmap glyphs (CBDT color emoji) are always stored at the right
 * size regardless of the strike that FreeType selected.
 */

#ifndef DEFS_UTF8RASTPORT_H
#define DEFS_UTF8RASTPORT_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <utility/tagitem.h>

struct Screen;

/* -------------------------------------------------------------------------
 * Glyph-cache pixel formats
 * ------------------------------------------------------------------------- */

#define URP_CACHE_MONO  0   /* 1 bit/pixel,  BltTemplate-compatible          */
#define URP_CACHE_GRAY  1   /* 1 byte/pixel alpha; future alpha-blend render  */
#define URP_CACHE_RGBA  2   /* 4 bytes/pixel R,G,B,A; future CyberGfx render */


/* -------------------------------------------------------------------------
 * DrawContext preference flags  (URPDC_SetPreferences)
 * ------------------------------------------------------------------------- */

/* Request anti-aliased (grey) glyph cache for scalable fonts.
 * Color emoji are always cached as RGBA regardless of this flag. */
#define URP_PREF_ANTIALIAS      (1UL << 0)

/* Request high-quality downscaling for color emoji (BGRA → RGBA).
 * When set, emoji glyphs that need to be shrunk by more than 2× use an
 * iterative 2×2 box-averaging pyramid before the final nearest-neighbour
 * pass.  Produces noticeably better results at large scale-down ratios
 * (e.g. NotoColorEmoji at small point sizes) at the cost of a one-time
 * per-glyph extra pass when the glyph is first cached.
 * Has no effect on MONO or GRAY glyphs. */
#define URP_PREF_HIGHFILTERING  (1UL << 2)

/* When in indexed color screens (AGA, or 8Bit cgx)
 *  and antialias enabled or color glyphs,
 *  glyphs whole rectangle are blitted with background color
 *  which is faster and use less memory.
 *  If not present, antialias blendded pixel are cutted through,
 *  but not blended to the actual background if there is a BG pattern or else.
 */
#define URP_PREF_CLUTMODE_NOMASK (1UL << 1)

/* Force all char have same width, even if font is not monopspace */
#define URP_PREF_FORCE_MONOSPACE (1UL << 3)


/* -------------------------------------------------------------------------
 * Style flags  (URPDC_SetStyle)
 * ------------------------------------------------------------------------- */
#define URP_STYLE_NORMAL  0
#define URP_STYLE_BOLD    (1UL << 0)
#define URP_STYLE_ITALIC  (1UL << 1)


/* -------------------------------------------------------------------------
 * Metric structs
 * ------------------------------------------------------------------------- */

struct URPTextMetric {
    WORD width;   /* total advance width of the string. If multiple lines, width of the longest line. */
    WORD height;  /* ascender + descender of the primary font. If multiple line, get full height of text. */
    WORD baseX;   /* baseline X offset from the origin (usually 0) */
    WORD baseY;   /* ascender: distance from origin to top of tallest glyph */
};

struct URPTextPos {
    WORD x;
    WORD y;
};


/* -------------------------------------------------------------------------
 * DrawContext  (opaque)
 * ------------------------------------------------------------------------- */

struct URPDrawContext; /* opaque */


/* -------------------------------------------------------------------------
 * DrawContext attribute tags  (URPDC_SetAttribsA / URPDC_SetAttribs)
 * ------------------------------------------------------------------------- */

#define URPDCA_Dummy        (TAG_USER | 0x7200)

/* [S] (ULONG) Number of space-glyph advances rendered per TAB character.
 *             Valid range: [1..12].  Default: 4. */
#define URPDCA_TabSpaces    (URPDCA_Dummy + 1)

/* [S] (LONG) Not persistent -- applies to the very next draw/measure call
 *            only, then must be re-set (default reverts to 0).
 *            Correction added to a draw call's local pen x before computing
 *            TAB stops, so that mid-line slices (e.g. a scrolled tile, or a
 *            selection-overlay redraw starting mid-line) still align TAB
 *            stops to the true left edge of the logical line rather than to
 *            the slice's own local x=0.  Pass
 *            (trueLineXOfFirstChar - pos.x) of the upcoming call.  Leave
 *            unset (0) when drawing/measuring a whole line from its own
 *            start. */
#define URPDCA_TabOriginX   (URPDCA_Dummy + 2)


/* Varargs convenience wrapper – builds the array on the stack. */
#define URPDC_SetAttribs(dc, ...) \
    do { ULONG _urpta_[] = {__VA_ARGS__, TAG_DONE}; \
         URPDC_SetAttribsA((dc), _urpta_); } while(0)


#endif /* UTF8RASTPORT_H */
