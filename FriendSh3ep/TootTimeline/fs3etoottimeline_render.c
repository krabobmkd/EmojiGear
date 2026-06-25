/*
 * TootTimeline – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * GM_LAYOUT / size-change detection
 * ----------------------------------
 * GM_LAYOUT (called by the parent layout gadget when the window is
 * resized) updates gadWidth/gadHeight.  If gadWidth differs from
 * lastTileWidth the tile pool is freed and rebuilt, and all posts are
 * re-laid-out so their heights reflect the new column width.
 *
 * GM_RENDER
 * ---------
 * 1. Detect size change and call ttl_do_layout if needed.
 * 2. Apply any pending scroll delta (set by GM_HANDLEINPUT).
 * 3. Evict tiles outside the warm window:
 *    [scrollY - TTL_TILE_BUF*TTL_TILE_HEIGHT,
 *     scrollY + gadHeight + TTL_TILE_BUF*TTL_TILE_HEIGHT)
 * 4. For each tile row in the warm window:
 *    a. Acquire a tile (takes a free one or evicts the farthest).
 *    b. If dirty: call ttl_render_tile().
 *    c. BltBitMapRastPort to the gadget RastPort.
 * 5. Fill empty space above or below all posts with bgPen.
 * 6. Draw text-selection overlay (COMPLEMENT mode) — placeholder.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>

#include "fs3etoottimeline_private.h"

#include "../bdbprintf.h"

/* ------------------------------------------------------------------ */
/* Internal layout                                                      */
/* ------------------------------------------------------------------ */

static void ttl_do_layout(Class *cl, Object *o, WORD newW, WORD newH, struct RastPort *rp)
{
    TTLData *inst = TTL_DATA(cl, o);
    BOOL widthChanged = (newW != inst->lastTileWidth);

    inst->gadWidth  = newW;
    inst->gadHeight = newH;

    if (widthChanged || inst->tileCount == 0) {
        ttl_tiles_free(inst);
        ttl_tiles_alloc(inst,rp);
        if (widthChanged) {
            ttl_layout_all_posts(inst);
            ttl_rebuild_ypositions(inst);
        }
    }

    /* Clamp scrollY to valid range */
    {
        LONG maxScroll = inst->contentBottomY - inst->gadHeight;
        if (maxScroll < inst->contentTopY) maxScroll = inst->contentTopY;
        if (inst->scrollY < inst->contentTopY) inst->scrollY = inst->contentTopY;
        if (inst->scrollY > maxScroll)         inst->scrollY = maxScroll;
    }
}

/* ------------------------------------------------------------------ */
/* GM_LAYOUT                                                            */
/* ------------------------------------------------------------------ */

ULONG TTL_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    TTLData      *inst   = TTL_DATA(cl, o);
    // struct Gadget    *g   = G(o);
    // struct GadgetInfo *gi = msg->gpl_GInfo;
    // struct Screen    *sc  = gi && gi->gi_Window ? gi->gi_Window->WScreen : NULL;

    inst->layoutToDo = TRUE;

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* GM_DOMAIN                                                            */
/* ------------------------------------------------------------------ */

ULONG TTL_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    TTLData      *inst   = TTL_DATA(cl, o);
    struct IBox  *domain = &msg->gpd_Domain;

    switch (msg->gpd_Which) {
        case GDOMAIN_MINIMUM:
            domain->Width  = (WORD)(inst->dpiHeight * 4);
            domain->Height = (WORD)(inst->dpiHeight * 4);
            break;
        case GDOMAIN_MAXIMUM:
            domain->Width  = 32767;
            domain->Height = 32767;
            break;
        case GDOMAIN_NOMINAL:
        default:
            domain->Width  = 200;
            domain->Height = (WORD)(inst->dpiHeight * 20);
            break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* GM_RENDER                                                            */
/* ------------------------------------------------------------------ */

ULONG TTL_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    TTLData         *inst = TTL_DATA(cl, o);
    struct RastPort *rp   = msg->gpr_RPort;
    struct Gadget   *g    = G(o);

    WORD  gadLeft, gadTop, gadW, gadH;
    LONG  warmTop, warmBot;
    LONG  tileIdx, firstIdx, lastIdx;

    gadLeft = g->LeftEdge;
    gadTop  = g->TopEdge;
    gadW    = g->Width;
    gadH    = g->Height;

    if (gadW <= 0 || gadH <= 0 || msg->gpr_GInfo == NULL ||
        msg->gpr_GInfo->gi_Screen == NULL
        || inst->style == NULL) return TRUE;

    /* note should check if color mode and depth changed */
    inst->screen =  msg->gpr_GInfo->gi_Screen;
    {
        struct Task *t=FindTask(NULL);
//bdbprintf("dr1 inst->callerTask %08x here:%08x\n",(int)inst->callerTask,(int)t);
        if(t != inst->callerTask ||
            rp == NULL)
        {
            /* sorry, but on the right process will you ? */
            ttl_notify(cl, o, msg->gpr_GInfo, TTIMELINE_ProcessRefresh, TRUE);
            return TRUE;
        }
    }
//    bdbprintf("dr2\n");
    /* do layout from correct proces if not done*/
    if(inst->LastLayoutedWidth != g->Width ||
        inst->LastLayoutedHeight != g->Height)
    {
        inst->layoutToDo = TRUE;
    }

    if(inst->layoutToDo)
    {
//    bdbprintf("ttl_do_layout\n");
        ttl_do_layout(cl, o, g->Width, g->Height,rp );

        inst->LastLayoutedWidth = g->Width;
        inst->LastLayoutedHeight = g->Height;
        inst->layoutToDo = FALSE;
    }

    /* Apply pending scroll */
    if (inst->pendingScroll) {
        LONG maxScroll = inst->contentBottomY - gadH;
        if (maxScroll < inst->contentTopY) maxScroll = inst->contentTopY;
        inst->scrollY = inst->pendingScrollY;
        if (inst->scrollY < inst->contentTopY) inst->scrollY = inst->contentTopY;
        if (inst->scrollY > maxScroll)         inst->scrollY = maxScroll;
        inst->pendingScroll = FALSE;
    }

    if (inst->tileCount == 0) {
        /* Pool not ready – paint solid background */       
        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
        RectFill(rp, gadLeft, gadTop, gadLeft+gadW-1, gadTop+gadH-1);
        return 0;
    }

    /* Warm window: a buffer of TTL_TILE_BUF tile rows above and below
     * the visible area ensures tiles are ready when the user scrolls. */
    warmTop = inst->scrollY - (LONG)TTL_TILE_BUF * TTL_TILE_HEIGHT;
    warmBot = inst->scrollY + gadH + (LONG)TTL_TILE_BUF * TTL_TILE_HEIGHT;

    /* Evict cold tiles */
    ttl_tile_evict_out_of_range(inst, warmTop, warmBot);

    /* Acquire and render tiles in the warm window */
    firstIdx = ttl_tile_index(warmTop);
    lastIdx  = ttl_tile_index(warmBot - 1);

    for (tileIdx = firstIdx; tileIdx <= lastIdx; tileIdx++) {
        LONG      tileBaseY = ttl_tile_base(tileIdx);
        TTLTile  *tile      = ttl_tile_acquire(inst, tileBaseY);

        if (!tile) continue;

        if (tile->dirty)
            ttl_render_tile(inst, tile);

        /* Only blit the portion that intersects the visible gadget area */
        {
            LONG visTop = inst->scrollY;
            LONG visBot = inst->scrollY + gadH;

            /* Intersection of tile and visible window in timeline coords */
            LONG clipTop = tileBaseY > visTop ? tileBaseY : visTop;
            LONG clipBot = (tileBaseY + TTL_TILE_HEIGHT) < visBot
                           ? (tileBaseY + TTL_TILE_HEIGHT) : visBot;

            if (clipBot <= clipTop) continue;

            /* Source Y within the tile bitmap */
            WORD srcY = (WORD)(clipTop - tileBaseY);
            /* Destination Y within the gadget */
            WORD dstY = (WORD)(gadTop + (clipTop - visTop));
            WORD rows = (WORD)(clipBot - clipTop);

            BltBitMapRastPort(tile->bm, 0, srcY,
                              rp, gadLeft, dstY,
                              gadW, rows, 0xC0);
        }
    }

    /* Fill the area above the first post if visible */
    if (inst->contentTopY > inst->scrollY) {
        WORD fillBot = (WORD)(gadTop + (inst->contentTopY - inst->scrollY));
        if (fillBot > gadTop) {
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
            RectFill(rp, gadLeft, gadTop, gadLeft+gadW-1, fillBot-1);
        }
    }

    /* Fill the area below the last post if visible */
    if (inst->contentBottomY < inst->scrollY + gadH) {
        WORD fillTop = (WORD)(gadTop + (inst->contentBottomY - inst->scrollY));
        if (fillTop < gadTop + gadH) {
            if (fillTop < gadTop) fillTop = gadTop;
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
            RectFill(rp, gadLeft, fillTop, gadLeft+gadW-1, gadTop+gadH-1);
        }
    }

    /* TODO: text-selection COMPLEMENT overlay (future: walk selSpanA..selSpanB) */

    /* Resize grip: three short diagonal lines in the bottom-right corner,
     * drawn directly onto the window RastPort so they survive tile blitting. */
    {
        WORD cx = (WORD)(gadLeft + gadW);   /* right edge (exclusive) */
        WORD cy = (WORD)(gadTop  + gadH);   /* bottom edge (exclusive) */
        int  i;
        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
        for (i = 3; i <= TTL_RESIZE_HANDLE - 1; i += 4) {
            Move(rp, cx - 1 - i, cy - 1);
            Draw(rp, cx - 1,     cy - 1 - i);
        }
    }

    return 0;
}
