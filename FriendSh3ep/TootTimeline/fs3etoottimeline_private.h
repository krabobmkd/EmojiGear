#ifndef FS3ETOOTTIMELINE_PRIVATE_H
#define FS3ETOOTTIMELINE_PRIVATE_H

/*
 * TootTimeline private data structures and prototypes.
 * Do not include outside TootTimeline/.
 *
 * Tile coordinate system
 * ----------------------
 * Timeline Y grows downward (same as screen Y).  The newest post sits at the
 * lowest (most negative) Y value; older posts extend toward positive Y.
 * scrollY = timeline Y currently at the gadget's top pixel row.
 *
 * Tiles
 * -----
 * A tile is a pre-rendered bitmap strip of (gadWidth × TTL_TILE_HEIGHT) pixels.
 * It covers one TTL_TILE_HEIGHT-pixel band of the timeline.
 * tileBaseY = ttl_tile_base(ttl_tile_index(timelineY)) is always a multiple of
 * TTL_TILE_HEIGHT (possibly negative).
 *
 * A finite pool of TTL_TILE_POOL_MAX tiles is allocated when the gadget is first
 * laid out.  If gadWidth changes the pool is rebuilt.  Pool capacity is chosen
 * to cover twice the gadget height plus a buffer so fast scrolling is smooth.
 *
 * Tiles whose tileBaseY == TTL_TILE_UNUSED are "free" (available in the pool).
 * Active tiles have a valid tileBaseY and may be dirty (needs re-render) or
 * clean (ready to blit).
 *
 * Text selection
 * --------------
 * TTLTextSpan records the pixel layout of each rendered text line within a post.
 * charXOffsets[i] is the x pixel where character i starts (relative to span.x).
 * Selection is stored as (selPost, selSpanA+charA, selSpanB+charB); drawn with
 * COMPLEMENT mode overlay over the tile blit.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>

#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include "fs3etoottimeline.h"
#include "../compilers.h"

/* ------------------------------------------------------------------ */
/* Tile pool constants                                                   */
/* ------------------------------------------------------------------ */

#define TTL_TILE_HEIGHT     128   /* pixel height of every tile */
#define TTL_TILE_POOL_MAX    48   /* absolute cap on number of tiles */
#define TTL_TILE_BUF         2    /* extra tile rows kept warm above/below viewport */

#define TTL_TILE_UNUSED  (0x80000000L)  /* sentinel: tile not assigned */

/* ------------------------------------------------------------------ */
/* Resize handle                                                         */
/* ------------------------------------------------------------------ */

/* Size in pixels of the bottom-right corner resize grip */
#define TTL_RESIZE_HANDLE  12

/* Globals owned by friendsh3ep.c — set in GM_HITTEST, read in WMHI_MOUSEMOVE */
extern BOOL windowResizeActive;
extern WORD windowResizeStartSX;
extern WORD windowResizeStartSY;
extern WORD windowResizeStartW;
extern WORD windowResizeStartH;
extern WORD windowResizeLastTargetW;  /* last width  sent to SizeWindow */
extern WORD windowResizeLastTargetH;  /* last height sent to SizeWindow */

/* ------------------------------------------------------------------ */
/* Post layout constants                                                 */
/* ------------------------------------------------------------------ */

#define TTL_POST_PAD_TOP   3   /* pixels above content in a post */
#define TTL_POST_PAD_BOT   4   /* pixels below content (before separator) */
#define TTL_POST_PAD_LEFT  4
#define TTL_POST_PAD_RIGHT 4
#define TTL_AVATAR_GAP     4   /* gap between avatar column and text */

/* ------------------------------------------------------------------ */
/* TTLTile — one pre-rendered bitmap strip                              */
/* ------------------------------------------------------------------ */

typedef struct {
    struct BitMap *bm;        /* bitmap of (gadWidth × TTL_TILE_HEIGHT) */
    LONG  tileBaseY;          /* timeline Y of tile top; TTL_TILE_UNUSED if free */
    BOOL  dirty;              /* TRUE = must re-render before blitting */
} TTLTile;

/* ------------------------------------------------------------------ */
/* TTLHotSpot — clickable region within a post                          */
/* ------------------------------------------------------------------ */

struct TTLHotSpot {
    struct MinNode node;
    WORD   x, y, w, h;   /* pixel rect relative to post->timelineY */
    UBYTE  type;          /* TTL_HOT_* */
    char  *data;          /* AllocVec'd string (URL / hashtag / handle) */
};

/* ------------------------------------------------------------------ */
/* TTLTextSpan — one rendered text line for hit-testing / selection     */
/* ------------------------------------------------------------------ */

#define TTL_SPAN_USERNAME  0
#define TTL_SPAN_ACCT      1
#define TTL_SPAN_BODY      2
#define TTL_SPAN_TIMESTAMP 3

/* Hot-spot types */
#define TTL_HOT_AVATAR     0
#define TTL_HOT_REPLY      1
#define TTL_HOT_BOOST      2
#define TTL_HOT_FAVORITE   3

typedef struct {
    struct MinNode  node;
    LONG   postRelY;      /* Y of span top, relative to post->timelineY */
    WORD   x;             /* left pixel X within the gadget */
    WORD   width;         /* rendered advance width in pixels */
    WORD   height;        /* line height */
    WORD   ascent;        /* pixels from span top to text baseline */
    UBYTE  spanType;      /* TTL_SPAN_* */
    char  *utf8;          /* AllocVec'd text */
    ULONG  byteLen;
    LONG  *charXOffsets;  /* AllocVec'd array of (charCount+1) LONG values */
    ULONG  charCount;
} TTLTextSpan;

/* ------------------------------------------------------------------ */
/* TTLPost — one post entry in the timeline                             */
/* ------------------------------------------------------------------ */

typedef struct TTLPost {
    struct MinNode  node;         /* doubly-linked in TTLData.posts, newest=head */

    LONG   timelineY;             /* Y of post top in timeline coordinates */
    LONG   height;                /* computed pixel height of this post */
    BOOL   dirty;                 /* TRUE = overlapping tiles need re-render */

    /* Content strings (all AllocVec'd copies) */
    char  *username;
    char  *acct;
    char  *body;
    char  *timestamp;

    /* Per-post spatial index lists */
    struct MinList  hotSpots;     /* list of TTLHotSpot   (for click events) */
    struct MinList  textSpans;    /* list of TTLTextSpan  (for text selection) */
} TTLPost;

/* ------------------------------------------------------------------ */
/* TTLData — per-instance INST_DATA block                               */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Screen for AllocBitMap and colour mapping */
    struct Screen *screen;
    /* layout and draw are only allowed on the process that own the gadget */
    struct Task         *callerTask;

    /* Font metrics derived from style->dc* (updated when TTIMELINE_Style is set).
     * Cached here so post-layout code doesn't call GetFontLineMetrics every time. */
    WORD    lineHeight;      /* dcNormal: ascender + descender */
    WORD    lineAscent;      /* dcNormal: distance from line top to baseline */
    WORD    nameLineHeight;  /* dcUsername line height */
    WORD    nameLineAscent;  /* dcUsername ascent */
    WORD    miniLineHeight;  /* dcMini line height */
    WORD    miniLineAscent;  /* dcMini ascent */

    /* Gadget dimensions from last layout */
    WORD    gadWidth;
    WORD    gadHeight;
    WORD    lastTileWidth;  /* gadWidth when pool was built; rebuild if differs */

    /* dpiHeight factor (row size in pixels) */
    UWORD   dpiHeight;

    /* ---- Tile pool ---- */
    TTLTile  tiles[TTL_TILE_POOL_MAX];
    ULONG    tileCount;          /* entries actually allocated (<=TTL_TILE_POOL_MAX) */

    /* Shared Layer+RastPort for rendering into tile bitmaps.
     * rp->BitMap is swapped to tiles[i].bm before each tile render. */
    struct Layer_Info *tileLayerInfo;
    struct Layer      *tileLayer;
    /* tileLayer->rp is the rendering RastPort */

    /* ---- Timeline scroll / content extents ---- */
    LONG    scrollY;         /* timeline Y at gadget top */
    LONG    contentTopY;     /* Y of topmost post's top edge */
    LONG    contentBottomY;  /* Y one pixel past the last post's bottom edge */

    /* ---- Post list (newest = head) ---- */
    struct MinList posts;
    ULONG          postCount;

    /* ---- Text selection state ---- */
    TTLPost     *selPost;
    TTLTextSpan *selSpanA;
    TTLTextSpan *selSpanB;
    ULONG        selCharA;    /* char index within selSpanA */
    ULONG        selCharB;    /* char index within selSpanB */
    BOOL         hasSelection;

    /* ---- Drag / scroll input (written in HandleInput, consumed in Render) ---- */
    BOOL   dragActive;
    WORD   dragStartGadY;    /* gadget-relative Y at button-down */
    LONG   dragStartScrollY;

    BOOL   pendingScroll;
    LONG   pendingScrollY;   /* new scrollY to apply in GM_RENDER */

    /* ---- Color theme (not owned; pointer set via TTIMELINE_Style) ---- */
    FS3EStyle *style;

    /* ---- Notification target (ICA workaround) ---- */
    Object *target;
    ULONG   ga_id;

    /* Do layout from right process and render function */
    ULONG layoutToDo;
    WORD  LastLayoutedWidth,LastLayoutedHeight;

} TTLData;

#define TTL_DATA(cl, o)  ((TTLData *)INST_DATA((cl), (o)))
#define G(o)             ((struct Gadget *)(o))

/* ------------------------------------------------------------------ */
/* Inline tile-index helpers                                            */
/* ------------------------------------------------------------------ */

/* Count UTF-8 codepoints in [start, end). */
INLINE LONG utf8_codepoints_range(const char *start, const char *end)
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

/* Floor division of y by TTL_TILE_HEIGHT (works for negative y too) */
INLINE LONG ttl_tile_index(LONG y)
{
    if (y >= 0)
        return y / TTL_TILE_HEIGHT;
    /* For negative y: -1→-1, -128→-1, -129→-2 */
    return -(((-y) + TTL_TILE_HEIGHT - 1) / TTL_TILE_HEIGHT);
}

/* Timeline Y of the start of a tile given its index */
INLINE LONG ttl_tile_base(LONG tileIdx)
{
    return tileIdx * (LONG)TTL_TILE_HEIGHT;
}

/* ------------------------------------------------------------------ */
/* Prototypes                                                           */
/* ------------------------------------------------------------------ */

/* fs3etoottimeline.c */
ULONG ASM SAVEDS TootTimeline_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* fs3etoottimeline_attribs.c */
ULONG TTL_OnNew    (Class *cl, Object *o, struct opSet *msg);
ULONG TTL_OnDispose(Class *cl, Object *o, Msg msg);
ULONG TTL_OnSet    (Class *cl, Object *o, struct opSet *msg);
ULONG TTL_OnGet    (Class *cl, Object *o, struct opGet *msg);

/* fs3etoottimeline_render.c */
ULONG TTL_OnLayout (Class *cl, Object *o, struct gpLayout *msg);
ULONG TTL_OnRender (Class *cl, Object *o, struct gpRender *msg);
ULONG TTL_OnDomain (Class *cl, Object *o, struct gpDomain *msg);

/* fs3etoottimeline_input.c */
ULONG TTL_OnHitTest    (Class *cl, Object *o, struct gpHitTest *msg);
ULONG TTL_OnGoActive   (Class *cl, Object *o, struct gpInput *msg);
ULONG TTL_OnHandleInput(Class *cl, Object *o, struct gpInput *msg);
ULONG TTL_OnGoInactive (Class *cl, Object *o, struct gpGoInactive *msg);

/* fs3etoottimeline_posts.c */
TTLPost *ttl_post_alloc        (const TTLPostSetup *setup);
void     ttl_post_free         (TTLPost *post);
void     ttl_post_layout       (TTLData *inst, TTLPost *post);
void     ttl_layout_all_posts  (TTLData *inst);
void     ttl_clear_posts       (TTLData *inst);
void     ttl_rebuild_ypositions(TTLData *inst);

/* fs3etoottimeline_tiles.c */
BOOL     ttl_tiles_alloc  (TTLData *inst, struct RastPort *rp);
void     ttl_tiles_free   (TTLData *inst);
void     ttl_tiles_invalidate_all(TTLData *inst);
void     ttl_tiles_invalidate_range(TTLData *inst, LONG fromY, LONG toY);
TTLTile *ttl_tile_find    (TTLData *inst, LONG tileBaseY);
TTLTile *ttl_tile_acquire (TTLData *inst, LONG tileBaseY);
void     ttl_tile_evict   (TTLData *inst, TTLTile *tile);
void     ttl_tile_evict_out_of_range(TTLData *inst, LONG keepTopY, LONG keepBotY);
void     ttl_render_tile  (TTLData *inst, TTLTile *tile);
void     ttl_notify       (Class *cl, Object *o, struct GadgetInfo *gi,
                           ULONG tag, ULONG value);
/* only process have right to send render, ask with notify ProcessREfresh
 * void     ttl_render_self  (Class *cl, Object *o, struct GadgetInfo *gi);
*/
#endif /* FS3ETOOTTIMELINE_PRIVATE_H */
