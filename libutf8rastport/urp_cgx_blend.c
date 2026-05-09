/*
 * urp_cgx_blend.c  –  CyberGraphX alpha-blend routines for libutf8rastport.
 *
 * Generates 26 static blend functions (2 source formats × 13 dest formats)
 * via a C preprocessor macro, then dispatches through the exported vtable
 * urp_blend_table[], indexed by the CyberGfx PIXFMT_* constant obtained from
 * LockBitMapTags().
 *
 * The caller (utf8rastport.c) is responsible for calling LockBitMapTags() /
 * UnLockBitMap() once per string, outside the glyph loop.  Per-glyph work is
 * limited to updating urp_blend_ctx.ge / .dx / .dy and invoking the function.
 *
 * Pixel format index  (cybergraphx/cybergraphics.h)
 *   0  PIXFMT_LUT8     – not handled (no RGB locked memory)
 *   1  PIXFMT_RGB15    – 16-bit BE  xRRRRRGGGGGBBBBB
 *   2  PIXFMT_BGR15    – 16-bit BE  xBBBBBGGGGGRRRRR
 *   3  PIXFMT_RGB15PC  – 16-bit LE  (bytes of RGB15 swapped)
 *   4  PIXFMT_BGR15PC  – 16-bit LE  (bytes of BGR15 swapped)
 *   5  PIXFMT_RGB16    – 16-bit BE  RRRRRGGGGGGBBBBB
 *   6  PIXFMT_BGR16    – 16-bit BE  BBBBBGGGGGGRRRRR
 *   7  PIXFMT_RGB16PC  – 16-bit LE  (bytes of RGB16 swapped)
 *   8  PIXFMT_BGR16PC  – 16-bit LE  (bytes of BGR16 swapped)
 *   9  PIXFMT_RGB24    – 24-bit     R G B
 *  10  PIXFMT_BGR24    – 24-bit     B G R
 *  11  PIXFMT_ARGB32   – 32-bit     A R G B
 *  12  PIXFMT_BGRA32   – 32-bit     B G R A
 *  13  PIXFMT_RGBA32   – 32-bit     R G B A
 *
 * Alpha blend formula (integer, >>8 approximation):
 *   out = (src * (a+1) + dst * (255-a)) >> 8
 *   Error vs exact /255: at most 1 LSB, invisible at any bit depth.
 */

#include "urp_cgx_blend.h"

#include <exec/types.h>
#include <graphics/layers.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>


/* =========================================================================
 * Alpha blend helper
 * ========================================================================= */

/* Blend two 8-bit values; result accurate to ±1 LSB vs true /255. */
#define BLEND8(s, d, a) \
    ((UBYTE)(((ULONG)(s) * ((ULONG)(a) + 1UL) + \
              (ULONG)(d) * (ULONG)(255 - (a))) >> 8))


/* =========================================================================
 * Per-format pixel GET / PUT macros
 *
 * GET_<FMT>(ptr, r, g, b)  – read R,G,B from pixel at ptr; scale to 0-255
 * PUT_<FMT>(ptr, r, g, b)  – pack R,G,B (0-255) back into pixel at ptr
 *
 * All use byte-by-byte access so they work on any alignment.
 * ========================================================================= */

/* --- RGB15 big-endian: xRRRRRGGGGGBBBBB (5-5-5) --- */
#define GET_RGB15(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[0]<<8)|(UWORD)(p)[1]; \
    UBYTE _rv = (_px>>10)&0x1F; (r) = (_rv<<3)|(_rv>>2); \
    UBYTE _gv = (_px>> 5)&0x1F; (g) = (_gv<<3)|(_gv>>2); \
    UBYTE _bv =  _px     &0x1F; (b) = (_bv<<3)|(_bv>>2); } while(0)
#define PUT_RGB15(p,r,g,b) do { \
    UWORD _px = (((UWORD)(r)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(b)>>3); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

/* --- BGR15 big-endian: xBBBBBGGGGGRRRRR (5-5-5) --- */
#define GET_BGR15(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[0]<<8)|(UWORD)(p)[1]; \
    UBYTE _bv = (_px>>10)&0x1F; (b) = (_bv<<3)|(_bv>>2); \
    UBYTE _gv = (_px>> 5)&0x1F; (g) = (_gv<<3)|(_gv>>2); \
    UBYTE _rv =  _px     &0x1F; (r) = (_rv<<3)|(_rv>>2); } while(0)
#define PUT_BGR15(p,r,g,b) do { \
    UWORD _px = (((UWORD)(b)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(r)>>3); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

/* --- RGB15PC little-endian: same bit layout as RGB15, bytes swapped --- */
#define GET_RGB15PC(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[1]<<8)|(UWORD)(p)[0]; \
    UBYTE _rv = (_px>>10)&0x1F; (r) = (_rv<<3)|(_rv>>2); \
    UBYTE _gv = (_px>> 5)&0x1F; (g) = (_gv<<3)|(_gv>>2); \
    UBYTE _bv =  _px     &0x1F; (b) = (_bv<<3)|(_bv>>2); } while(0)
#define PUT_RGB15PC(p,r,g,b) do { \
    UWORD _px = (((UWORD)(r)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(b)>>3); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

/* --- BGR15PC little-endian: same bit layout as BGR15, bytes swapped --- */
#define GET_BGR15PC(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[1]<<8)|(UWORD)(p)[0]; \
    UBYTE _bv = (_px>>10)&0x1F; (b) = (_bv<<3)|(_bv>>2); \
    UBYTE _gv = (_px>> 5)&0x1F; (g) = (_gv<<3)|(_gv>>2); \
    UBYTE _rv =  _px     &0x1F; (r) = (_rv<<3)|(_rv>>2); } while(0)
#define PUT_BGR15PC(p,r,g,b) do { \
    UWORD _px = (((UWORD)(b)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(r)>>3); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

/* --- RGB16 big-endian: RRRRRGGGGGGBBBBB (5-6-5) --- */
#define GET_RGB16(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[0]<<8)|(UWORD)(p)[1]; \
    UBYTE _rv = (_px>>11)&0x1F; (r) = (_rv<<3)|(_rv>>2); \
    UBYTE _gv = (_px>> 5)&0x3F; (g) = (_gv<<2)|(_gv>>4); \
    UBYTE _bv =  _px     &0x1F; (b) = (_bv<<3)|(_bv>>2); } while(0)
#define PUT_RGB16(p,r,g,b) do { \
    UWORD _px = (((UWORD)(r)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(b)>>3); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

/* --- BGR16 big-endian: BBBBBGGGGGGRRRRR (5-6-5) --- */
#define GET_BGR16(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[0]<<8)|(UWORD)(p)[1]; \
    UBYTE _bv = (_px>>11)&0x1F; (b) = (_bv<<3)|(_bv>>2); \
    UBYTE _gv = (_px>> 5)&0x3F; (g) = (_gv<<2)|(_gv>>4); \
    UBYTE _rv =  _px     &0x1F; (r) = (_rv<<3)|(_rv>>2); } while(0)
#define PUT_BGR16(p,r,g,b) do { \
    UWORD _px = (((UWORD)(b)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(r)>>3); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

/* --- RGB16PC little-endian: same bit layout as RGB16, bytes swapped --- */
#define GET_RGB16PC(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[1]<<8)|(UWORD)(p)[0]; \
    UBYTE _rv = (_px>>11)&0x1F; (r) = (_rv<<3)|(_rv>>2); \
    UBYTE _gv = (_px>> 5)&0x3F; (g) = (_gv<<2)|(_gv>>4); \
    UBYTE _bv =  _px     &0x1F; (b) = (_bv<<3)|(_bv>>2); } while(0)
#define PUT_RGB16PC(p,r,g,b) do { \
    UWORD _px = (((UWORD)(r)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(b)>>3); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

/* --- BGR16PC little-endian: same bit layout as BGR16, bytes swapped --- */
#define GET_BGR16PC(p,r,g,b) do { \
    UWORD _px = ((UWORD)(p)[1]<<8)|(UWORD)(p)[0]; \
    UBYTE _bv = (_px>>11)&0x1F; (b) = (_bv<<3)|(_bv>>2); \
    UBYTE _gv = (_px>> 5)&0x3F; (g) = (_gv<<2)|(_gv>>4); \
    UBYTE _rv =  _px     &0x1F; (r) = (_rv<<3)|(_rv>>2); } while(0)
#define PUT_BGR16PC(p,r,g,b) do { \
    UWORD _px = (((UWORD)(b)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(r)>>3); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

/* --- RGB24: byte order R G B --- */
#define GET_RGB24(p,r,g,b) do { (r)=(p)[0]; (g)=(p)[1]; (b)=(p)[2]; } while(0)
#define PUT_RGB24(p,r,g,b) do { (p)[0]=(r); (p)[1]=(g); (p)[2]=(b); } while(0)

/* --- BGR24: byte order B G R --- */
#define GET_BGR24(p,r,g,b) do { (b)=(p)[0]; (g)=(p)[1]; (r)=(p)[2]; } while(0)
#define PUT_BGR24(p,r,g,b) do { (p)[0]=(b); (p)[1]=(g); (p)[2]=(r); } while(0)

/* --- ARGB32: byte order A R G B --- */
#define GET_ARGB32(p,r,g,b) do { (r)=(p)[1]; (g)=(p)[2]; (b)=(p)[3]; } while(0)
#define PUT_ARGB32(p,r,g,b) do { (p)[1]=(r); (p)[2]=(g); (p)[3]=(b); } while(0)

/* --- BGRA32: byte order B G R A --- */
#define GET_BGRA32(p,r,g,b) do { (b)=(p)[0]; (g)=(p)[1]; (r)=(p)[2]; } while(0)
#define PUT_BGRA32(p,r,g,b) do { (p)[0]=(b); (p)[1]=(g); (p)[2]=(r); } while(0)

/* --- RGBA32: byte order R G B A --- */
#define GET_RGBA32(p,r,g,b) do { (r)=(p)[0]; (g)=(p)[1]; (b)=(p)[2]; } while(0)
#define PUT_RGBA32(p,r,g,b) do { (p)[0]=(r); (p)[1]=(g); (p)[2]=(b); } while(0)


/* =========================================================================
 * Blend function generator macro
 *
 * Expands to two static functions per pixel format:
 *   urp_blend_gray_<NAME>  –  URP_CACHE_GRAY source (1-byte alpha per pixel)
 *                             foreground colour taken from dc->draw.argb
 *   urp_blend_rgba_<NAME>  –  URP_CACHE_RGBA source (R,G,B,A per pixel)
 *                             dc unused (colour comes from glyph data)
 *
 * Both receive (urp_blend_ctx *, URPDrawContext *); glyph geometry comes
 * from c->ge so nothing needs to be copied before each call.
 * ========================================================================= */

#define DEF_BLEND_FUNCS(NAME, BPP, GETRGB, PUTRGB) \
\
static void urp_blend_gray_##NAME(REG(a0, const struct urp_blend_ctx *c), \
                                  REG(a1, const struct URPDrawContext *dc)) \
{ \
    WORD gw = c->ge->width, gh = c->ge->rows; \
    WORD x0 = (c->dx < c->cx1) ? (c->cx1 - c->dx) : 0; \
    WORD y0 = (c->dy < c->cy1) ? (c->cy1 - c->dy) : 0; \
    WORD x1 = (c->dx + gw > c->cx2) ? (c->cx2 - c->dx) : gw; \
    WORD y1 = (c->dy + gh > c->cy2) ? (c->cy2 - c->dy) : gh; \
    if (x1 <= x0 || y1 <= y0) return; \
    for (WORD y = y0; y < y1; y++) { \
        UBYTE *drow = c->base + (ULONG)((WORD)(c->dy + y)) * c->bpr; \
        const UBYTE *srow = c->ge->pixels + (ULONG)y * (UWORD)c->ge->pitch; \
        for (WORD x = x0; x < x1; x++) { \
            UBYTE a = srow[x]; \
            if (!a) continue; \
            UBYTE *dp = drow + (ULONG)((WORD)(c->dx + x)) * (BPP); \
            UBYTE dr, dg, db; \
            GETRGB(dp, dr, dg, db); \
            dr = BLEND8(dc->draw.argb.R, dr, a); \
            dg = BLEND8(dc->draw.argb.G, dg, a); \
            db = BLEND8(dc->draw.argb.B, db, a); \
            PUTRGB(dp, dr, dg, db); \
        } \
    } \
} \
\
static void urp_blend_rgba_##NAME(REG(a0, const struct urp_blend_ctx *c), \
                                  REG(a1, const struct URPDrawContext *dc)) \
{ \
    (void)dc; \
    WORD gw = c->ge->width, gh = c->ge->rows; \
    WORD x0 = (c->dx < c->cx1) ? (c->cx1 - c->dx) : 0; \
    WORD y0 = (c->dy < c->cy1) ? (c->cy1 - c->dy) : 0; \
    WORD x1 = (c->dx + gw > c->cx2) ? (c->cx2 - c->dx) : gw; \
    WORD y1 = (c->dy + gh > c->cy2) ? (c->cy2 - c->dy) : gh; \
    if (x1 <= x0 || y1 <= y0) return; \
    for (WORD y = y0; y < y1; y++) { \
        UBYTE *drow = c->base + (ULONG)((WORD)(c->dy + y)) * c->bpr; \
        const UBYTE *srow = c->ge->pixels + (ULONG)y * (UWORD)c->ge->pitch; \
        for (WORD x = x0; x < x1; x++) { \
            const UBYTE *sp = srow + (ULONG)x * 4UL; \
            UBYTE a = sp[3]; \
            if (!a) continue; \
            UBYTE *dp = drow + (ULONG)((WORD)(c->dx + x)) * (BPP); \
            UBYTE dr, dg, db; \
            GETRGB(dp, dr, dg, db); \
            dr = BLEND8(sp[0], dr, a); \
            dg = BLEND8(sp[1], dg, a); \
            db = BLEND8(sp[2], db, a); \
            PUTRGB(dp, dr, dg, db); \
        } \
    } \
}


/* =========================================================================
 * Generate the 26 blend functions (2 × 13 formats)
 * ========================================================================= */

DEF_BLEND_FUNCS(RGB15,   2, GET_RGB15,   PUT_RGB15)
DEF_BLEND_FUNCS(BGR15,   2, GET_BGR15,   PUT_BGR15)
DEF_BLEND_FUNCS(RGB15PC, 2, GET_RGB15PC, PUT_RGB15PC)
DEF_BLEND_FUNCS(BGR15PC, 2, GET_BGR15PC, PUT_BGR15PC)
DEF_BLEND_FUNCS(RGB16,   2, GET_RGB16,   PUT_RGB16)
DEF_BLEND_FUNCS(BGR16,   2, GET_BGR16,   PUT_BGR16)
DEF_BLEND_FUNCS(RGB16PC, 2, GET_RGB16PC, PUT_RGB16PC)
DEF_BLEND_FUNCS(BGR16PC, 2, GET_BGR16PC, PUT_BGR16PC)
DEF_BLEND_FUNCS(RGB24,   3, GET_RGB24,   PUT_RGB24)
DEF_BLEND_FUNCS(BGR24,   3, GET_BGR24,   PUT_BGR24)
DEF_BLEND_FUNCS(ARGB32,  4, GET_ARGB32,  PUT_ARGB32)
DEF_BLEND_FUNCS(BGRA32,  4, GET_BGRA32,  PUT_BGRA32)
DEF_BLEND_FUNCS(RGBA32,  4, GET_RGBA32,  PUT_RGBA32)


/* =========================================================================
 * Dispatch vtable
 *
 * Exported (extern in urp_cgx_blend.h).
 * Indexed by PIXFMT_* value (0-13).  Entry 0 (LUT8) is NULL: not handled.
 * ========================================================================= */

const struct urp_blend_vtable urp_blend_table[14] = {
    /* 0  PIXFMT_LUT8    */ { NULL,                    NULL                   },
    /* 1  PIXFMT_RGB15   */ { urp_blend_gray_RGB15,    urp_blend_rgba_RGB15   },
    /* 2  PIXFMT_BGR15   */ { urp_blend_gray_BGR15,    urp_blend_rgba_BGR15   },
    /* 3  PIXFMT_RGB15PC */ { urp_blend_gray_RGB15PC,  urp_blend_rgba_RGB15PC },
    /* 4  PIXFMT_BGR15PC */ { urp_blend_gray_BGR15PC,  urp_blend_rgba_BGR15PC },
    /* 5  PIXFMT_RGB16   */ { urp_blend_gray_RGB16,    urp_blend_rgba_RGB16   },
    /* 6  PIXFMT_BGR16   */ { urp_blend_gray_BGR16,    urp_blend_rgba_BGR16   },
    /* 7  PIXFMT_RGB16PC */ { urp_blend_gray_RGB16PC,  urp_blend_rgba_RGB16PC },
    /* 8  PIXFMT_BGR16PC */ { urp_blend_gray_BGR16PC,  urp_blend_rgba_BGR16PC },
    /* 9  PIXFMT_RGB24   */ { urp_blend_gray_RGB24,    urp_blend_rgba_RGB24   },
    /* 10 PIXFMT_BGR24   */ { urp_blend_gray_BGR24,    urp_blend_rgba_BGR24   },
    /* 11 PIXFMT_ARGB32  */ { urp_blend_gray_ARGB32,   urp_blend_rgba_ARGB32  },
    /* 12 PIXFMT_BGRA32  */ { urp_blend_gray_BGRA32,   urp_blend_rgba_BGRA32  },
    /* 13 PIXFMT_RGBA32  */ { urp_blend_gray_RGBA32,   urp_blend_rgba_RGBA32  }
};
