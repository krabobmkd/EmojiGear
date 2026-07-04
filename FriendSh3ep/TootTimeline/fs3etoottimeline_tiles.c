/*
 * TootTimeline – tile pool management and tile rendering.
 *
 * Pool lifecycle
 * --------------
 * ttl_tiles_alloc() allocates TTL_TILE_POOL_MAX or fewer bitmaps of
 * (gadWidth × TTL_TILE_HEIGHT) plus one shared clipping Layer+RastPort.
 * All tiles start free (tileBaseY == TTL_TILE_UNUSED).
 *
 * ttl_tile_acquire() finds or borrows a tile for a given tileBaseY:
 *   - If already active with that base Y, returns it.
 *   - Otherwise takes a free tile, assigns the base Y, marks dirty.
 *   - If no free tile is available, evicts the farthest one from the
 *     current viewport.
 *
 * ttl_render_tile() swaps the tile bitmap into the shared Layer's
 * RastPort and draws all posts that overlap the tile's Y band.
 *
 * Post drawing
 * ------------
 * Each post is drawn at its position relative to tileBaseY.  The clipping
 * layer ensures nothing spills outside [0, TTL_TILE_HEIGHT).
 * Drawing uses URPDrawTextUTF8 when a draw context is set; plain pen
 * fills are used for placeholders (avatar box, separators).
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/layers.h>
#include <string.h>

#include "fs3etoottimeline_private.h"
#include "../bdbprintf.h"

/* ------------------------------------------------------------------ */
/* Tile pool allocation / deallocation                                  */
/* ------------------------------------------------------------------ */

BOOL ttl_tiles_alloc(TTLData *inst, struct RastPort *rp)
{
    ULONG  i, depth, needed;
//bdbprintf("ttl_tiles_alloc\n");
    ttl_tiles_free(inst);  /* free old pool first if any */

    if (inst->gadWidth <= 0 || inst->gadHeight <= 0 || !rp || !rp->BitMap) return FALSE;

    /* Pool size: 2× gadget height + buffer rows, capped at TTL_TILE_POOL_MAX */
    needed = (ULONG)((inst->gadHeight * 2) / TTL_TILE_HEIGHT) + TTL_TILE_BUF * 2 + 2;
    if (needed > TTL_TILE_POOL_MAX) needed = TTL_TILE_POOL_MAX;

    depth    = (ULONG)GetBitMapAttr(rp->BitMap, BMA_DEPTH);

    /* Allocate tile bitmaps */
    for (i = 0; i < needed; i++) {
        inst->tiles[i].bm = AllocBitMap((ULONG)inst->gadWidth, TTL_TILE_HEIGHT,
                                         depth, BMF_CLEAR, rp->BitMap);
        if (!inst->tiles[i].bm) break;
        inst->tiles[i].tileBaseY = TTL_TILE_UNUSED;
        inst->tiles[i].dirty     = FALSE;
        inst->tileCount++;
    }

    if (inst->tileCount == 0) return FALSE;

    /* Create shared clipping Layer + RastPort on tiles[0] */
    inst->tileLayerInfo = NewLayerInfo();
    if (!inst->tileLayerInfo) { ttl_tiles_free(inst); return FALSE; }

    InstallLayerInfoHook(inst->tileLayerInfo, LAYERS_NOBACKFILL);

    inst->tileLayer = CreateUpfrontHookLayer(inst->tileLayerInfo,
                                              inst->tiles[0].bm,
                                              0, 0,
                                              inst->gadWidth - 1,
                                              TTL_TILE_HEIGHT - 1,
                                              LAYERSIMPLE,
                                              LAYERS_NOBACKFILL,
                                              NULL);
    if (!inst->tileLayer) { ttl_tiles_free(inst); return FALSE; }

    inst->lastTileWidth = inst->gadWidth;
    return TRUE;
}

void ttl_tiles_free(TTLData *inst)
{
    ULONG i;
//bdbprintf("ttl_tiles_free\n");
    if (inst->tileLayer) {
        if(inst->tileCount>0) inst->tileLayer->rp->BitMap = inst->tiles[0].bm;
        DeleteLayer(0, inst->tileLayer);
        inst->tileLayer = NULL;
    }
    if (inst->tileLayerInfo) {
        DisposeLayerInfo(inst->tileLayerInfo);
        inst->tileLayerInfo = NULL;
    }

    WaitBlit();
    for (i = 0; i < inst->tileCount; i++) {
        if (inst->tiles[i].bm) {
            FreeBitMap(inst->tiles[i].bm);
            inst->tiles[i].bm = NULL;
        }
        inst->tiles[i].tileBaseY = TTL_TILE_UNUSED;
    }
    inst->tileCount     = 0;
    inst->lastTileWidth = -1;
}

/* ------------------------------------------------------------------ */
/* Invalidation                                                         */
/* ------------------------------------------------------------------ */

void ttl_tiles_invalidate_all(TTLData *inst)
{
//bdbprintf("ttl_tiles_invalidate_all\n");
    ULONG i;
    for (i = 0; i < inst->tileCount; i++)
        inst->tiles[i].dirty = TRUE;
}

void ttl_tiles_invalidate_range(TTLData *inst, LONG fromY, LONG toY)
{
    ULONG i;
    for (i = 0; i < inst->tileCount; i++) {
        LONG base = inst->tiles[i].tileBaseY;
        if (base == TTL_TILE_UNUSED) continue;
        if (base + TTL_TILE_HEIGHT > fromY && base < toY)
            inst->tiles[i].dirty = TRUE;
    }
}

/* ------------------------------------------------------------------ */
/* Tile lookup                                                          */
/* ------------------------------------------------------------------ */

TTLTile *ttl_tile_find(TTLData *inst, LONG tileBaseY)
{
    ULONG i;
    for (i = 0; i < inst->tileCount; i++)
        if (inst->tiles[i].tileBaseY == tileBaseY)
            return &inst->tiles[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ttl_tile_acquire                                                     */
/*                                                                      */
/* Return (or assign) a tile for tileBaseY.  If no free tile exists,   */
/* evict the one farthest from the current scroll position.            */
/* ------------------------------------------------------------------ */

TTLTile *ttl_tile_acquire(TTLData *inst, LONG tileBaseY)
{
    ULONG    i;
    TTLTile *found    = NULL;
    TTLTile *freeTile = NULL;
    TTLTile *farthest = NULL;
    LONG     farthestDist = -1;
//bdbprintf("ttl_tile_acquire\n");
    if (inst->tileCount == 0) return NULL;

    for (i = 0; i < inst->tileCount; i++) {
        if (inst->tiles[i].tileBaseY == tileBaseY) return &inst->tiles[i];
        if (inst->tiles[i].tileBaseY == TTL_TILE_UNUSED && !freeTile)
            freeTile = &inst->tiles[i];
    }

    found = freeTile;
    if (!found) {
        /* Evict the tile whose base is farthest from the current scroll */
        for (i = 0; i < inst->tileCount; i++) {
            LONG d = inst->tiles[i].tileBaseY - ttl_active(inst)->scrollY;
            if (d < 0) d = -d;
            if (d > farthestDist) {
                farthestDist = d;
                farthest     = &inst->tiles[i];
            }
        }
        found = farthest;
    }

    if (!found) return NULL;

    found->tileBaseY = tileBaseY;
    found->dirty     = TRUE;
    return found;
}

/* ------------------------------------------------------------------ */
/* ttl_tile_evict                                                       */
/* ------------------------------------------------------------------ */

void ttl_tile_evict(TTLData *inst, TTLTile *tile)
{
//bdbprintf("ttl_tile_evict\n");
    (void)inst;
    if (!tile) return;
    tile->tileBaseY = TTL_TILE_UNUSED;
    tile->dirty     = FALSE;
}

/* ------------------------------------------------------------------ */
/* ttl_tile_evict_out_of_range                                          */
/* ------------------------------------------------------------------ */

void ttl_tile_evict_out_of_range(TTLData *inst, LONG keepTopY, LONG keepBotY)
{
    ULONG i;
    for (i = 0; i < inst->tileCount; i++) {
        LONG base = inst->tiles[i].tileBaseY;
        if (base == TTL_TILE_UNUSED) continue;
        if (base + TTL_TILE_HEIGHT <= keepTopY || base >= keepBotY)
            ttl_tile_evict(inst, &inst->tiles[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Post drawing helpers                                                  */
/* ------------------------------------------------------------------ */


/* Draw UTF-8 text at tile-relative (x, baseline_y) using the shared RP.
 * Falls back to nothing if dc is NULL. */
static void tile_draw_text(TTLData *inst, struct RastPort *rp,
                           WORD x, WORD y, const char *utf8,
                           struct URPDrawContext *dc)
{
//bdbprintf("tile_draw_text\n");
    struct URPTextPos pos;
    if (!dc || !utf8 || !utf8[0]) return;
    /* colour must be set on the draw context via URPDC_SetDrawColorFromPen before calling */
    pos.x = x;
    pos.y = y;
    URPDrawTextUTF8(rp, dc, &pos, utf8, -1);
}

/* Draw body text with naive word-wrap into (x, startY, maxW) column.
 * Returns the Y after the last line. */
static WORD tile_draw_body(TTLData *inst, struct RastPort *rp,
                           const char *body, WORD x, WORD startY, WORD maxW,
                           struct URPDrawContext *dc)
{
//bdbprintf("tile_draw_body\n");
    /* Simple wrap: measure total advance, emit virtual line breaks. */
    const char *p     = body;
    const char *lineStart = body;
    WORD        curY  = startY;
    LONG        lineW = 0;

    if (!dc || !body || !body[0]) return startY;

    /* Walk codepoints; break at spaces when lineW > maxW */
    while (*p) {
        const char *wordStart = p;
        LONG        wordW     = 0;

        /* Advance past a word (non-space bytes) */
        while (*p && (unsigned char)*p > 0x20) {
            unsigned char c = (unsigned char)*p;
            LONG adv = 0;
            /* Skip multi-byte sequence */
            if      (c < 0x80) p += 1;
            else if (c < 0xE0) p += 2;
            else if (c < 0xF0) p += 3;
            else               p += 4;
            (void)adv; /* advance measured below via TextSizeUTF8 */
        }
        {
            LONG wchars = utf8_codepoints_range(wordStart, p);
            if (wchars > 0) {
                struct URPTextMetric m;
                URPDC_TextSizeUTF8(dc, wordStart, wchars, &m);
                wordW = m.width;
            }
        }

        if (lineW + wordW > (LONG)maxW && lineW > 0) {
            /* emit current line up to wordStart */
            {
                struct URPTextPos pos;
                pos.x = x;
                pos.y = (WORD)(curY + inst->lineAscent);
                URPDrawTextUTF8(rp, dc, &pos,
                                lineStart, (ULONG)utf8_codepoints_range(lineStart, wordStart));
            }
            curY     += inst->lineHeight;
            lineStart = wordStart;
            lineW     = 0;
        }
        lineW += wordW;

        /* Skip whitespace between words */
        while (*p == ' ' || *p == '\t') {
            struct URPTextMetric m;
            char sp[2] = { *p, 0 };
            URPDC_TextSizeUTF8(dc, sp, 1, &m);
            lineW += m.width;
            p++;
        }
        /* Handle newlines */
        if (*p == '\n') {
            {
                struct URPTextPos pos;
                pos.x = x;
                pos.y = (WORD)(curY + inst->lineAscent);
                URPDrawTextUTF8(rp, dc, &pos,
                                lineStart, (ULONG)utf8_codepoints_range(lineStart, p));
            }
            curY     += inst->lineHeight;
            p++;
            lineStart = p;
            lineW     = 0;
        }
    }
    /* Trailing line */
    if (p > lineStart) {
        struct URPTextPos pos;
        pos.x = x;
        pos.y = (WORD)(curY + inst->lineAscent);
        URPDrawTextUTF8(rp, dc, &pos,
                        lineStart, (ULONG)utf8_codepoints_range(lineStart, p));
        curY += inst->lineHeight;
    }
    return curY;
}

/* ------------------------------------------------------------------ */
/* ttl_render_tile                                                      */
/*                                                                      */
/* Draw all posts that overlap this tile into its bitmap.               */
/* ------------------------------------------------------------------ */

void ttl_render_tile(TTLData *inst, TTLTile *tile)
{
    struct RastPort *rp;
    LONG             tileBaseY = tile->tileBaseY;
    WORD             avatarW, padLeft, avatarGap;
    WORD             textX, textW;
    TTLPost         *post;
    LONG bgpen;
//bdbprintf("ttl_render_tile\n");
    if (!inst->tileLayer) return;

    bgpen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);
// bdbprintf("ttl bgpen:%d\n",bgpen);
    /* Swap this tile's bitmap into the shared Layer's RastPort */
    inst->tileLayer->rp->BitMap = tile->bm;
    rp = inst->tileLayer->rp;

    /* Background fill */
    SetAPen(rp, bgpen);
    RectFill(rp, 0, 0, inst->gadWidth - 1, TTL_TILE_HEIGHT - 1);

    if (inst->style && inst->style->avatarSize > 0) {
        avatarW  = inst->style->avatarSize;
        padLeft  = inst->style->postPadLeft;
        avatarGap = inst->style->avatarGap;
    } else {
        avatarW  = 35;
        padLeft  = 6;
        avatarGap = 6;
    }
    textX = (WORD)(padLeft + avatarW + avatarGap);
    textW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
    if (textW < 16) textW = 16;

    /* Walk the active channel's posts; they are sorted newest-first but
     * their Y values are consecutive, so we can stop as soon as
     * post->timelineY >= tileBaseY+TILE_H */
    for (post = (TTLPost *)ttl_active(inst)->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        LONG postTop = post->timelineY;
        LONG postBot = postTop + post->height;

        if (postBot  <= tileBaseY)                  continue;
        if (postTop  >= tileBaseY + TTL_TILE_HEIGHT) break;

        /* drawY = Y within tile of the post top (may be negative) */
        WORD drawY = (WORD)(postTop - tileBaseY);

        /* ---- Avatar placeholder rectangle ---- */
        {
            WORD ay = (WORD)(drawY + TTL_POST_PAD_TOP);
            WORD ax = padLeft;
            WORD as = avatarW;

            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
            RectFill(rp, ax, ay, ax + as - 1, ay + as - 1);
            /* A small cross in timeline bg so the placeholder is obvious */
            SetAPen(rp, bgpen);
            Move(rp, ax,          ay + as/2);
            Draw(rp, ax + as - 1, ay + as/2);
            Move(rp, ax + as/2,   ay);
            Draw(rp, ax + as/2,   ay + as - 1);
        }

        /* ---- Text rendering (requires style DCs) ---- */
        if (inst->style && inst->style->dcNormal) {


            struct URPDrawContext *dcName = inst->style->dcUsername;
            struct URPDrawContext *dcMini = inst->style->dcMini;
            struct URPDrawContext *dcBody = inst->style->dcNormal;
            WORD   curY;
            WORD   baselineY;

            LONG bgPen  = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);
            LONG txtPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT);
            LONG dimPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT_DIM);
            LONG namePen= (LONG)FS3E_PEN(inst->style, FS3E_COLOR_USERNAME);

// bdbprintf("ttl_render_tile bgPen:%d\n",bgPen);
// bdbprintf("ttl_render_tile txtPen:%d\n",txtPen);
            /* Username (dcUsername) */
            curY      = (WORD)(drawY + TTL_POST_PAD_TOP);
            baselineY = (WORD)(curY + inst->nameLineAscent);
            URPDC_SetDrawColorFromPen(dcName, inst->screen, namePen, bgPen);
            tile_draw_text(inst, rp, textX, baselineY, post->username, dcName);
            curY += inst->nameLineHeight;

            /* Acct (dcMini, dim pen) */
            URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
            baselineY = (WORD)(curY + inst->miniLineAscent);
            tile_draw_text(inst, rp, textX, baselineY, post->acct, dcMini);
            curY += inst->miniLineHeight;

            /* Timestamp – right-aligned on the username row, dcMini */
            if (post->timestamp && post->timestamp[0]) {
                struct URPTextMetric tsm;
                URPDC_TextSizeUTF8(dcMini, post->timestamp, -1, &tsm);
                WORD tsX = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT - tsm.width);
                WORD tsY = (WORD)(drawY + TTL_POST_PAD_TOP + inst->nameLineAscent);
                URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                tile_draw_text(inst, rp, tsX, tsY, post->timestamp, dcMini);
            }

            /* Ensure text Y is below avatar */
            {
                WORD minY = (WORD)(drawY + TTL_POST_PAD_TOP + avatarW);
                if (curY < minY) curY = minY;
            }

            /* Body text (dcNormal) */
            URPDC_SetDrawColorFromPen(dcBody, inst->screen, txtPen, bgPen);
            {
                WORD bodyEndY = curY;
                if (post->body && post->body[0])
                    bodyEndY = tile_draw_body(inst, rp, post->body,
                                              textX, curY, textW, dcBody);

                /* Action bar: ↩ Reply  ↺ Boost  ★ Fave — right-aligned, normal font */
                {
                    static const char * const aLabels[3] = {
                        "\xe2\x86\xa9 Reply",
                        //"\xe2\x86\xba Boost",
                         "\xF0\x9F\x94\x81 Boost",
                        //"\xe2\x98\x85 Fave",
                        "💫 Fave"
                    };



                    LONG actionPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT);
                    WORD barBaselineY = (WORD)(bodyEndY + 2 + inst->lineAscent);
                    WORD xRight = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT);
                    WORD gap    = 8;
                    int  a;
                    URPDC_SetDrawColorFromPen(dcBody, inst->screen, actionPen, bgPen);
                    for (a = 2; a >= 0; a--) {
                        struct URPTextMetric m;
                        struct URPTextPos pos;
                        WORD itemX;
                        LONG nc = utf8_codepoints_range(aLabels[a],
                                     aLabels[a] + strlen(aLabels[a]));
                        URPDC_TextSizeUTF8(dcBody, aLabels[a], nc, &m);
                        itemX = (WORD)(xRight - m.width);
                        pos.x = itemX;
                        pos.y = barBaselineY;
                        URPDrawTextUTF8(rp, dcBody, &pos, aLabels[a], (ULONG)nc);
                        xRight = (WORD)(itemX - gap);  /* use saved itemX, not pos.x */
                    }
                }
            }
        }

        /* ---- Separator line at post bottom ---- */
        {
            WORD sepY = (WORD)(postBot - 1 - tileBaseY);
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
            Move(rp, 0,                sepY);
            Draw(rp, inst->gadWidth-1, sepY);
        }
    }

    tile->dirty = FALSE;
}

/* ------------------------------------------------------------------ */
/* Notifications and self-render helper                                 */
/* ------------------------------------------------------------------ */

void ttl_notify(Class *cl, Object *o, struct GadgetInfo *gi,
                ULONG tag, ULONG value)
{
    TTLData         *inst = TTL_DATA(cl, o);
    struct TagItem  tags[3];
    struct opUpdate nmsg;

   if (!inst->target) return;

    tags[0].ti_Tag  = GA_ID;
    tags[0].ti_Data = inst->ga_id;
    tags[1].ti_Tag  = tag;
    tags[1].ti_Data = value;
    tags[2].ti_Tag  = TAG_DONE;

    nmsg.MethodID     = OM_UPDATE;
    nmsg.opu_AttrList = (struct TagItem *)tags;
    nmsg.opu_GInfo    = gi;
    nmsg.opu_Flags    = 0;
    DoMethodA(inst->target,(Msg)&nmsg);

}



// void ttl_render_self(Class *cl, Object *o, struct GadgetInfo *gi)
// {
//     struct RastPort *rp;
//     if (!gi) return;
//     rp = ObtainGIRPort(gi);
//     if (rp) {
//         struct gpRender gpr;
//         gpr.MethodID   = GM_RENDER;
//         gpr.gpr_GInfo  = gi;
//         gpr.gpr_RPort  = rp;
//         gpr.gpr_Redraw = GREDRAW_UPDATE;
//         DoMethodA(o, (Msg)&gpr);
//         ReleaseGIRPort(rp);
//     }
// }
