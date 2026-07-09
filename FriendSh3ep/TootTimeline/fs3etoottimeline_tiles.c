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
#include <stdio.h>

#include "fs3etoottimeline_private.h"
#include "../avatarimages.h"
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
/* Action-bar button labels/types — the single shared copy.             */
/*                                                                      */
/* Both drawing (below) and hot-spot rect computation                  */
/* (ttl_post_build_hotspots in fs3etoottimeline_posts.c) measure/draw   */
/* from this exact array, so a click always lines up with what's on     */
/* screen -- previously each file kept its own hand-typed copy of these */
/* strings and they'd quietly drifted apart (different Boost/Fave       */
/* glyphs), which threw off the hot-spot rects vs the drawn glyphs.     */
/* ------------------------------------------------------------------ */

const char * const ttl_actionLabels[3] = {
    "\xe2\x86\xa9 Reply",       /* ↩ Reply */
    "\xF0\x9F\x94\x81 Boost",   /* 🔁 Boost */
    "\xF0\x9F\x92\xAB Fave"     /* 💫 Fave */
};

const UBYTE ttl_actionTypes[3] = {
    TTL_HOT_REPLY, TTL_HOT_BOOST, TTL_HOT_FAVORITE
};

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

/* Same as tile_draw_text, but for a byte-bounded, non-NUL-terminated run
 * (TTLHotSpot.data is a borrowed pointer/length into another string's
 * buffer -- see the TTLHotSpot comment in fs3etoottimeline_private.h). */
static void tile_draw_text_n(struct RastPort *rp, WORD x, WORD y,
                              const char *utf8, ULONG byteLen,
                              struct URPDrawContext *dc)
{
    struct URPTextPos pos;
    if (!dc || !utf8 || byteLen == 0) return;
    pos.x = x;
    pos.y = y;
    URPDrawTextUTF8(rp, dc, &pos, utf8,
                     (ULONG)utf8_codepoints_range(utf8, utf8 + byteLen));
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
    WORD             textX;
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

        /* Pool-backed hot-spots (avatar/profile/@mention/#hashtag/URL/
         * media/Reply/Boost/Fave) -- cheap no-op if already fresh. Must
         * run before anything below reads post->hotSpots. */
        ttl_post_ensure_hotspots(inst, post);

        /* ---- Avatar: draw from cache if available, else placeholder ---- */
        {
            WORD ay = (WORD)(drawY + TTL_POST_PAD_TOP);
            WORD ax = padLeft;
            WORD as = avatarW;
            RgbImage *avImg = inst->avatarImages
                            ? AvatarImages_Get(inst->avatarImages, post->acct)
                            : NULL;

            if (avImg) {
                /* The cached image is already box-fit to a fixed size (see
                 * fs3ethumb.h) and may not be square -- aspect-fit it into
                 * the as x as avatar box (same rule bmimage.c's box-fit
                 * uses) and centre it, then let RgbImage_DrawScaled do the
                 * actual scale+draw at whatever the live avatarSize is. */
                ULONG dw, dh;
                WORD  bx, by;

                if (avImg->width >= avImg->height) {
                    dw = as;
                    dh = ((ULONG)avImg->height * (ULONG)as) / avImg->width;
                } else {
                    dh = as;
                    dw = ((ULONG)avImg->width * (ULONG)as) / avImg->height;
                }
                if (dw < 1) dw = 1;
                if (dh < 1) dh = 1;

                bx = (WORD)(ax + (as - (WORD)dw) / 2);
                by = (WORD)(ay + (as - (WORD)dh) / 2);

                RgbImage_DrawScaled(avImg, rp, inst->screen, inst->style->dcNormal,
                                     bx, by, (UWORD)dw, (UWORD)dh);
            } else {
                /* Placeholder: filled rectangle with cross */
                SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                RectFill(rp, ax, ay, ax + as - 1, ay + as - 1);
                SetAPen(rp, bgpen);
                Move(rp, ax,          ay + as/2);
                Draw(rp, ax + as - 1, ay + as/2);
                Move(rp, ax + as/2,   ay);
                Draw(rp, ax + as/2,   ay + as - 1);
            }
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
            curY = (WORD)(drawY + TTL_POST_PAD_TOP);

            /* "↺ Name boosted" header line (dcMini, dim pen) — reblogs only */
            if (post->boostBy && post->boostBy[0]) {
                char boostLine[128];

                snprintf(boostLine, sizeof(boostLine),
                         "\xE2\x99\xBB %s boosted", post->boostBy);
                baselineY = (WORD)(curY + inst->miniLineAscent);
                URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                tile_draw_text(inst, rp, textX, baselineY, boostLine, dcMini);
                curY += inst->miniLineHeight;
            }

            /* Username (dcUsername) */
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

            /* Body text: draw the pre-wrapped TTL_SPAN_BODY spans built by
             * ttl_post_layout (via fs3etextwrap) -- this is what makes
             * drawn pixels match the height layout reserved; there is no
             * re-wrapping at draw time anymore. */
            URPDC_SetDrawColorFromPen(dcBody, inst->screen, txtPen, bgPen);
            {
                TTLTextSpan *sp;
                UBYTE hi;

                for (sp = (TTLTextSpan *)post->textSpans.mlh_Head;
                     sp->node.mln_Succ;
                     sp = (TTLTextSpan *)sp->node.mln_Succ)
                {
                    WORD lineY, baseline;
                    if (sp->spanType != TTL_SPAN_BODY) continue;
                    lineY    = (WORD)(drawY + sp->postRelY);
                    baseline = (WORD)(lineY + sp->ascent);
                    tile_draw_text(inst, rp, sp->x, baseline, sp->utf8, dcBody);
                }

                /* Recolor @mention / #hashtag / URL tokens with the link
                 * pen: same glyphs, same spot, redrawn on top in a
                 * different draw colour (see ttl_post_ensure_hotspots for
                 * where these rects/strings come from). */
                {
                    LONG linkPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_HASHTAG);
                    URPDC_SetDrawColorFromPen(dcBody, inst->screen, linkPen, bgPen);
                    for (hi = 0; hi < post->hotSpotCount; hi++) {
                        TTLHotSpot *hs = &post->hotSpots[hi];
                        if (hs->type != TTL_HOT_MENTION &&
                            hs->type != TTL_HOT_HASHTAG &&
                            hs->type != TTL_HOT_URL) continue;
                        tile_draw_text_n(rp, hs->x,
                                         (WORD)(drawY + hs->y + inst->lineAscent),
                                         hs->data, hs->dataLen, dcBody);
                    }
                }

                /* Media preview: the actual thumbnail once its RgbImage is
                 * loaded (AvatarImages_GetMedia -- same fetch/thumbnail/
                 * cache pipeline as avatars, a separate pool keyed by
                 * URL), else the placeholder rectangle as before. */
                for (hi = 0; hi < post->hotSpotCount; hi++) {
                    TTLHotSpot *hs = &post->hotSpots[hi];
                    WORD rx, ry;
                    RgbImage *thumb;
                    if (hs->type != TTL_HOT_IMAGE) continue;
                    rx = hs->x;
                    ry = (WORD)(drawY + hs->y);

                    thumb = (inst->avatarImages && hs->data)
                          ? AvatarImages_GetMedia(inst->avatarImages, hs->data)
                          : NULL;

                    if (thumb && RgbImage_IsLoaded(thumb)) {
                        /* Box-fit thumb into hs->w x hs->h (not
                         * necessarily square, unlike the avatar box) --
                         * whichever axis is the tighter constraint wins;
                         * see rgbimage.h's box-fit rule. */
                        ULONG dw, dh;
                        WORD  bx, by;

                        if ((ULONG)hs->w * thumb->height <= (ULONG)hs->h * thumb->width) {
                            dw = hs->w;
                            dh = ((ULONG)thumb->height * (ULONG)hs->w) / thumb->width;
                        } else {
                            dh = hs->h;
                            dw = ((ULONG)thumb->width * (ULONG)hs->h) / thumb->height;
                        }
                        if (dw < 1) dw = 1;
                        if (dh < 1) dh = 1;

                        bx = (WORD)(rx + (hs->w - (WORD)dw) / 2);
                        by = (WORD)(ry + (hs->h - (WORD)dh) / 2);

                        RgbImage_DrawScaled(thumb, rp, inst->screen, inst->style->dcNormal,
                                             bx, by, (UWORD)dw, (UWORD)dh);
                    } else {
                        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                        RectFill(rp, rx, ry, (WORD)(rx + hs->w - 1), (WORD)(ry + hs->h - 1));
                    }
                }

                /* Prev/next arrows, overlaid on the image, at the exact
                 * zones ttl_post_build_hotspots computed -- only present
                 * (TTL_HOT_MEDIA_PREV/NEXT hot-spots exist) when the post
                 * has more than one image. */
                {
                    LONG arrowPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT);
                    URPDC_SetDrawColorFromPen(dcBody, inst->screen, arrowPen, bgPen);
                    for (hi = 0; hi < post->hotSpotCount; hi++) {
                        TTLHotSpot *hs = &post->hotSpots[hi];
                        const char *glyph;
                        struct URPTextMetric m;
                        struct URPTextPos pos;
                        LONG nc;

                        if      (hs->type == TTL_HOT_MEDIA_PREV) glyph = "\xE2\x97\x80"; /* ◀ */
                        else if (hs->type == TTL_HOT_MEDIA_NEXT) glyph = "\xE2\x96\xB6"; /* ▶ */
                        else continue;

                        nc = utf8_codepoints_range(glyph, glyph + strlen(glyph));
                        URPDC_TextSizeUTF8(dcBody, glyph, nc, &m);
                        pos.x = (WORD)(hs->x + (hs->w - m.width) / 2);
                        pos.y = (WORD)(drawY + hs->y + (hs->h - inst->lineHeight) / 2 + inst->lineAscent);
                        URPDrawTextUTF8(rp, dcBody, &pos, glyph, (ULONG)nc);
                    }
                }

                /* Action bar: ↩ Reply  🔁 Boost  💫 Fave — right-aligned, normal
                 * font. Same row Y formula, and the same ttl_actionLabels[]/
                 * ttl_actionTypes[], that the hot-spot rects in
                 * ttl_post_build_hotspots are measured from -- rather than
                 * re-deriving "where does the content end" from the body
                 * spans here (which used to ignore the media preview rect
                 * entirely when it was taller than the text, or stacked
                 * below it), this reads post->height, which
                 * ttl_post_layout already computed correctly accounting
                 * for whichever of the two is taller. */
                {
                    LONG actionPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT);
                    WORD barTopY = (WORD)(drawY + post->height - 1
                                           - TTL_POST_PAD_BOT - inst->lineHeight);
                    WORD barBaselineY = (WORD)(barTopY + inst->lineAscent);
                    WORD xRight = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT);
                    int  a;
                    URPDC_SetDrawColorFromPen(dcBody, inst->screen, actionPen, bgPen);
                    for (a = 2; a >= 0; a--) {
                        struct URPTextMetric m;
                        struct URPTextPos pos;
                        WORD itemX;
                        LONG nc = utf8_codepoints_range(ttl_actionLabels[a],
                                     ttl_actionLabels[a] + strlen(ttl_actionLabels[a]));
                        URPDC_TextSizeUTF8(dcBody, ttl_actionLabels[a], nc, &m);
                        itemX = (WORD)(xRight - m.width);
                        pos.x = itemX;
                        pos.y = barBaselineY;
                        URPDrawTextUTF8(rp, dcBody, &pos, ttl_actionLabels[a], (ULONG)nc);
                        xRight = (WORD)(itemX - TTL_ACTION_GAP);
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

void ttl_notify_hotspot(Class *cl, Object *o, struct GadgetInfo *gi,
                         UBYTE type, const char *data, ULONG dataLen,
                         const char *postId)
{
    TTLData         *inst = TTL_DATA(cl, o);
    struct TagItem  tags[5];
    struct opUpdate nmsg;

    /* Copy into the gadget-owned buffers first -- see the TTLData comment
     * on lastHotSpotStr/lastHotSpotPostId for why these can't just be
     * pointed at directly. */
    if (data && dataLen > 0) {
        ULONG n = dataLen;
        if (n >= sizeof(inst->lastHotSpotStr)) n = sizeof(inst->lastHotSpotStr) - 1;
        CopyMem((APTR)data, inst->lastHotSpotStr, n);
        inst->lastHotSpotStr[n] = '\0';
    } else {
        inst->lastHotSpotStr[0] = '\0';
    }

    if (postId && postId[0]) {
        ULONG n = (ULONG)strlen(postId);
        if (n >= sizeof(inst->lastHotSpotPostId)) n = sizeof(inst->lastHotSpotPostId) - 1;
        CopyMem((APTR)postId, inst->lastHotSpotPostId, n);
        inst->lastHotSpotPostId[n] = '\0';
    } else {
        inst->lastHotSpotPostId[0] = '\0';
    }

    if (!inst->target) return;

    tags[0].ti_Tag  = GA_ID;
    tags[0].ti_Data = inst->ga_id;
    tags[1].ti_Tag  = TTIMELINE_HotSpotNotify;
    tags[1].ti_Data = (ULONG)type;
    tags[2].ti_Tag  = TTIMELINE_LastHotSpotString;
    tags[2].ti_Data = inst->lastHotSpotStr[0]     ? (ULONG)inst->lastHotSpotStr     : 0;
    tags[3].ti_Tag  = TTIMELINE_LastHotSpotPostId;
    tags[3].ti_Data = inst->lastHotSpotPostId[0]  ? (ULONG)inst->lastHotSpotPostId  : 0;
    tags[4].ti_Tag  = TAG_DONE;

    nmsg.MethodID     = OM_UPDATE;
    nmsg.opu_AttrList = (struct TagItem *)tags;
    nmsg.opu_GInfo    = gi;
    nmsg.opu_Flags    = 0;
    DoMethodA(inst->target, (Msg)&nmsg);
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
