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
#include "../bmimage.h"
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
/* from ttl_build_action_labels()'s output, so a click always lines up  */
/* with what's on screen -- previously each file kept its own hand-     */
/* typed copy of these strings and they'd quietly drifted apart         */
/* (different Boost/Fave glyphs), which threw off the hot-spot rects    */
/* vs the drawn glyphs. Labels are now per-post (they carry that post's */
/* Reply/Boost/Fave counts, and the Fave glyph toggles empty/full star  */
/* by post->favourited), so they can no longer be one static array.     */
/* ------------------------------------------------------------------ */

const UBYTE ttl_actionTypes[3] = {
    TTL_HOT_REPLY, TTL_HOT_BOOST, TTL_HOT_FAVORITE
};

/* Left-aligned, own-toot-only action buttons (post->isOwn) -- plain text,
 * no per-post counts unlike Reply/Boost/Fave above, so there's no
 * "build labels" step: render and hot-spot code both just measure/draw
 * this same array directly, same "single shared copy" reasoning. */
const UBYTE ttl_ownActionTypes[2] = {
    TTL_HOT_MODIFY, TTL_HOT_DELETE
};
const char *const ttl_ownActionLabels[2] = {
    "Modify", "Delete"
};

/* Notifications view's generalized actor/verb prefix -- parameterizes the
 * exact mechanism the "boosted" line below already uses (reserve one
 * mini-line, dcMini/dim pen, one %s format string) instead of hardcoding
 * a single verb, indexed by TTL_NOTIF_* (see fs3etoottimeline.h). NULL
 * entries draw nothing: TTL_NOTIF_NONE because there's nothing to say
 * (ordinary toot), TTL_NOTIF_MENTION because the byline already shows the
 * mentioning author -- a prefix there would just repeat it. */
static const char *const notifVerbFormat[] = {
    NULL,                                            /* TTL_NOTIF_NONE */
    NULL,                                            /* TTL_NOTIF_MENTION */
    "\xE2\x99\xBB %s boosted your toot",              /* TTL_NOTIF_REBLOG */
    "\xE2\x98\x85 %s favourited your toot",           /* TTL_NOTIF_FAVOURITE */
    "%s's poll has ended",                            /* TTL_NOTIF_POLL */
    "A toot by %s you interacted with was edited",    /* TTL_NOTIF_UPDATE */
};
#define TTL_NOTIF_VERBFORMAT_COUNT (sizeof(notifVerbFormat) / sizeof(notifVerbFormat[0]))

/* A zero count draws as a trailing space instead of "0" -- keeps the
 * button from shouting a meaningless zero at every fresh/unboosted toot
 * while still reserving roughly the same slot the digit(s) would take. */
static void ttl_append_count(char *buf, ULONG bufsz, const char *prefix, ULONG count)
{
    if (count == 0)
        snprintf(buf, bufsz, "%s ", prefix);
    else
        snprintf(buf, bufsz, "%s %lu", prefix, (unsigned long)count);
}

/* \xF0\x9F\x92\xAB = 💫 (dizzy symbol, stand-in for an empty/outline
 * star -- none of the bundled fonts have one); \xE2\xAD\x90 = ⭐ (filled
 * star), shown once the connected user has favourited the post. */
void ttl_build_action_labels(const TTLPost *post,
                              char labels[3][TTL_ACTION_LABEL_MAX])
{
    ttl_append_count(labels[0], TTL_ACTION_LABEL_MAX,
                      "\xe2\x86\xa9 Reply", post->repliesCount);
    ttl_append_count(labels[1], TTL_ACTION_LABEL_MAX,
                      "\xF0\x9F\x94\x81 Boost", post->reblogsCount);
    ttl_append_count(labels[2], TTL_ACTION_LABEL_MAX,
                      post->favourited ? "\xE2\xAD\x90" : "\xF0\x9F\x92\xAB",
                      post->favouritesCount);
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
/* ttl_toot_render -- TTLItemClass.render for TTLToot_Class             */
/*                                                                      */
/* Draw one toot into rp at (post->timelineY - tileBaseY). Caller       */
/* (ttl_render_tile) has already ensured post->hotSpots is fresh, filled */
/* the tile background, and draws the post-bottom separator itself      */
/* after this returns -- both are generic across every item kind.       */
/* ------------------------------------------------------------------ */

void ttl_toot_render(TTLData *inst, struct RastPort *rp, TTLPost *post, LONG tileBaseY)
{
    WORD  avatarW, padLeft, avatarGap;
    WORD  textX;
    LONG  bgpen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);
    /* drawY = Y within tile of the post top (may be negative) */
    WORD  drawY = (WORD)(post->timelineY - tileBaseY);

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

    /* Thread-reply marker: full-height vertical accent line down the left
     * edge, for posts shown as replies in "discussion mode" (see
     * TTL_HOT_THREAD / TTLPost.isThreadReply) -- drawn unconditionally (no
     * style-DC dependency), same as the post-bottom separator line in
     * ttl_render_item_in_tile, so it survives even a transient
     * no-style-yet render. Purely decorative, no layout/height impact --
     * otherwise identical to a normal toot, per the "same layouting as
     * classic toots" requirement. */
    if (post->isThreadReply) {
        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
        Move(rp, 2, drawY);
        Draw(rp, 2, (WORD)(drawY + post->height - 1));
    }

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
                UBYTE fmt = BMFMT_UNKNOWN;
                BOOL  failed = (inst->avatarImages && post->acct)
                             && AvatarImages_Failed(inst->avatarImages, post->acct, &fmt);

                /* Placeholder: filled rectangle with cross */
                SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                RectFill(rp, ax, ay, ax + as - 1, ay + as - 1);
                SetAPen(rp, bgpen);
                Move(rp, ax,          ay + as/2);
                Draw(rp, ax + as - 1, ay + as/2);
                Move(rp, ax + as/2,   ay);
                Draw(rp, ax + as/2,   ay + as - 1);

                /* Decode failed and the source sniffed as WebP -- same
                 * "say so instead of a bare box" treatment as a failed
                 * media thumbnail below (see the TTL_HOT_IMAGE case in
                 * this same function). */
                if (failed && fmt == BMFMT_WEBP && inst->style && inst->style->dcNormal) {
                    struct URPDrawContext *dcBody = inst->style->dcNormal;
                    const char *label = "webp";
                    struct URPTextMetric m;
                    struct URPTextPos    pos;
                    LONG nc = utf8_codepoints_range(label, label + strlen(label));

                    URPDC_SetDrawColorFromPen(dcBody, inst->screen,
                        (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT), bgpen);
                    URPDC_TextSizeUTF8(dcBody, label, nc, &m);
                    pos.x = (WORD)(ax + (as - m.width) / 2);
                    pos.y = (WORD)(ay + (as - inst->lineHeight) / 2 + inst->lineAscent);
                    URPDrawTextUTF8(rp, dcBody, &pos, label, (ULONG)nc);
                }
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

            /* "↺ Name boosted" header line (dcMini, dim pen) — reblogs only.
             * Mutually exclusive with the notifications-view prefix line
             * below -- see TTLPost.notifType's comment. */
            if (post->boostBy && post->boostBy[0]) {
                char boostLine[128];

                snprintf(boostLine, sizeof(boostLine),
                         "\xE2\x99\xBB %s boosted", post->boostBy);
                baselineY = (WORD)(curY + inst->miniLineAscent);
                URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                tile_draw_text(inst, rp, textX, baselineY, boostLine, dcMini);
                curY += inst->miniLineHeight;
            } else if (post->notifType < TTL_NOTIF_VERBFORMAT_COUNT &&
                       notifVerbFormat[post->notifType] != NULL) {
                char notifLine[160];

                snprintf(notifLine, sizeof(notifLine), notifVerbFormat[post->notifType],
                         (post->notifActorName && post->notifActorName[0])
                             ? post->notifActorName : "Someone");
                baselineY = (WORD)(curY + inst->miniLineAscent);
                URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                tile_draw_text(inst, rp, textX, baselineY, notifLine, dcMini);
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

                    /* Audio attachment: no thumbnail was ever fetched for
                     * this slot (see friendsh3ep.c's FETCH_IMAGE loop) --
                     * just a filled rect with a centered play glyph,
                     * same pattern as the prev/next arrows below. */
                    if (hs->type == TTL_HOT_PLAY_AUDIO) {
                        const char *glyph = "\xE2\x96\xB6"; /* ▶ */
                        struct URPTextMetric m;
                        struct URPTextPos    pos;
                        LONG nc;

                        rx = hs->x;
                        ry = (WORD)(drawY + hs->y);
                        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                        RectFill(rp, rx, ry, (WORD)(rx + hs->w - 1), (WORD)(ry + hs->h - 1));

                        nc = utf8_codepoints_range(glyph, glyph + strlen(glyph));
                        URPDC_SetDrawColorFromPen(dcBody, inst->screen,
                            (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT), bgPen);
                        URPDC_TextSizeUTF8(dcBody, glyph, nc, &m);
                        pos.x = (WORD)(rx + (hs->w - m.width) / 2);
                        pos.y = (WORD)(ry + (hs->h - inst->lineHeight) / 2 + inst->lineAscent);
                        URPDrawTextUTF8(rp, dcBody, &pos, glyph, (ULONG)nc);
                        continue;
                    }

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
                        UBYTE fmt = BMFMT_UNKNOWN;
                        BOOL  failed = (inst->avatarImages && hs->data)
                                     && AvatarImages_MediaFailed(inst->avatarImages, hs->data, &fmt);

                        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                        RectFill(rp, rx, ry, (WORD)(rx + hs->w - 1), (WORD)(ry + hs->h - 1));

                        /* Decode failed and the source sniffed as WebP
                         * (see BmImage_SniffFormat) -- say so instead of
                         * leaving a bare box with no explanation. */
                        if (failed && fmt == BMFMT_WEBP) {
                            const char *label = "webp";
                            struct URPTextMetric m;
                            struct URPTextPos    pos;
                            LONG nc = utf8_codepoints_range(label, label + strlen(label));

                            URPDC_SetDrawColorFromPen(dcBody, inst->screen,
                                (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT), bgPen);
                            URPDC_TextSizeUTF8(dcBody, label, nc, &m);
                            pos.x = (WORD)(rx + (hs->w - m.width) / 2);
                            pos.y = (WORD)(ry + (hs->h - inst->lineHeight) / 2 + inst->lineAscent);
                            URPDrawTextUTF8(rp, dcBody, &pos, label, (ULONG)nc);
                        }
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

                /* Attachment counter ("1/2"), pinned to the bottom-right
                 * corner of the preview rect whenever there's more than
                 * one to browse between (same condition as the prev/next
                 * arrows above) -- background-filled so it stays legible
                 * over any image content, not just plain-colored ones. */
                if (post->mediaCount > 1) {
                    char  counter[16];
                    struct URPTextMetric m;
                    struct URPTextPos    pos;
                    LONG  nc;
                    WORD  padX = 3, padY = 1;
                    WORD  boxW, boxH, boxX, boxY;

                    snprintf(counter, sizeof(counter), "%lu/%lu",
                             (unsigned long)(post->mediaCurrentIndex + 1),
                             (unsigned long)post->mediaCount);
                    nc = utf8_codepoints_range(counter, counter + strlen(counter));
                    URPDC_TextSizeUTF8(dcBody, counter, nc, &m);

                    boxW = (WORD)(m.width + padX * 2);
                    boxH = (WORD)(inst->lineHeight + padY * 2);
                    boxX = (WORD)(post->previewX + post->previewW - boxW);
                    boxY = (WORD)(drawY + post->previewY + post->previewH - boxH);

                    SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                    RectFill(rp, boxX, boxY, (WORD)(boxX + boxW - 1), (WORD)(boxY + boxH - 1));

                    URPDC_SetDrawColorFromPen(dcBody, inst->screen,
                        (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT),
                        (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                    pos.x = (WORD)(boxX + padX);
                    pos.y = (WORD)(boxY + padY + inst->lineAscent);
                    URPDrawTextUTF8(rp, dcBody, &pos, counter, (ULONG)nc);
                }

                /* Link preview card -- see TTLPost.hasCard/cardX/Y/W/H/
                 * cardImgH/cardTitleLines/cardDescLines, all computed once
                 * by ttl_toot_layout. Image strip (if any) via the
                 * identical AvatarImages_GetCard()/RgbImage_DrawScaled
                 * pipeline the media thumbnail above already uses, just a
                 * separate cache pool; provider/title/description are
                 * plain text rows drawn directly (not via post->textSpans --
                 * card text isn't selectable, unlike the toot body). */
                if (post->hasCard && post->cardW > 0) {
                    WORD cardPad = 4;
                    WORD rx = post->cardX;
                    WORD ry = (WORD)(drawY + post->cardY);
                    WORD rowY;
                    WORD cardRight  = (WORD)(rx + post->cardW - 1);
                    WORD cardBottom = (WORD)(ry + post->cardH - 1);

                    /* Opaque background -- without this, an unbroken long
                     * line in the body text above (typically a URL
                     * fs3etextwrap can't find a break point in) can run
                     * underneath and show through the card's own text,
                     * since nothing was ever painted behind it before. The
                     * border is drawn last (see below), after the image/
                     * text content, so it's never partly covered by an
                     * image that happens to box-fit flush to an edge. */
                    SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_CARD_BG));
                    RectFill(rp, rx, ry, cardRight, cardBottom);

                    if (post->cardImgH > 0) {
                        RgbImage *cimg = (inst->avatarImages && post->cardImageUrl)
                                       ? AvatarImages_GetCard(inst->avatarImages, post->cardImageUrl)
                                       : NULL;

                        if (cimg && RgbImage_IsLoaded(cimg)) {
                            ULONG dw, dh;
                            WORD  bx, by;
                            WORD  boxW = post->cardW, boxH = post->cardImgH;

                            if ((ULONG)boxW * cimg->height <= (ULONG)boxH * cimg->width) {
                                dw = boxW;
                                dh = ((ULONG)cimg->height * (ULONG)boxW) / cimg->width;
                            } else {
                                dh = boxH;
                                dw = ((ULONG)cimg->width * (ULONG)boxH) / cimg->height;
                            }
                            if (dw < 1) dw = 1;
                            if (dh < 1) dh = 1;

                            bx = (WORD)(rx + (boxW - (WORD)dw) / 2);
                            by = (WORD)(ry + (boxH - (WORD)dh) / 2);

                            RgbImage_DrawScaled(cimg, rp, inst->screen, inst->style->dcNormal,
                                                 bx, by, (UWORD)dw, (UWORD)dh);
                        } else {
                            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                            RectFill(rp, rx, ry, (WORD)(rx + post->cardW - 1), (WORD)(ry + post->cardImgH - 1));
                        }
                    }

                    /* Text block below the image strip (or right at the
                     * box top, padded, if there's no image at all). */
                    rowY = (WORD)(ry + post->cardImgH + cardPad);

                    if (post->cardProviderName && post->cardProviderName[0]) {
                        URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                        tile_draw_text(inst, rp, (WORD)(rx + cardPad),
                                       (WORD)(rowY + inst->miniLineAscent),
                                       post->cardProviderName, dcMini);
                        rowY += inst->miniLineHeight;
                    }

                    {
                        ULONG li;
                        URPDC_SetDrawColorFromPen(dcMini, inst->screen, txtPen, bgPen);
                        for (li = 0; li < post->cardTitleLineCount; li++) {
                            tile_draw_text(inst, rp, (WORD)(rx + cardPad),
                                           (WORD)(rowY + inst->miniLineAscent),
                                           post->cardTitleLines[li], dcMini);
                            rowY += inst->miniLineHeight;
                        }
                    }

                    {
                        ULONG li;
                        URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                        for (li = 0; li < post->cardDescLineCount; li++) {
                            tile_draw_text(inst, rp, (WORD)(rx + cardPad),
                                           (WORD)(rowY + inst->miniLineAscent),
                                           post->cardDescLines[li], dcMini);
                            rowY += inst->miniLineHeight;
                        }
                    }

                    /* Thin border, drawn last so it's always crisp on top
                     * of the fill/image/text above. */
                    SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_CARD_BORDER));
                    Move(rp, rx, ry);
                    Draw(rp, cardRight, ry);
                    Draw(rp, cardRight, cardBottom);
                    Draw(rp, rx, cardBottom);
                    Draw(rp, rx, ry);
                }

                /* Poll ("survey") results — closed/result rendering only
                 * (see TTL_POST_MAX_POLL_OPTIONS): one title+percentage
                 * text row per option, followed by a track rect with a
                 * proportional fill, then a "N votes · Poll closed"
                 * summary line. pollBlockY was computed once by
                 * ttl_toot_layout and is reused verbatim here -- never
                 * re-derived (see that function's comment). */
                if (post->pollOptionCount > 0) {
                    LONG accentPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT);
                    LONG trackPen  = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_BUTTON_BG);
                    WORD pollTextW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
                    WORD rowY = (WORD)(drawY + post->pollBlockY);
                    ULONG oi;

                    for (oi = 0; oi < post->pollOptionCount; oi++) {
                        const char *title = post->pollOptionTitles[oi] ? post->pollOptionTitles[oi] : "";
                        ULONG votes = post->pollOptionVotes[oi];
                        ULONG pct = (post->pollVotesCount > 0)
                                  ? (votes * 100) / post->pollVotesCount : 0;
                        char pctLabel[16];
                        struct URPTextMetric m;
                        WORD barY = (WORD)(rowY + inst->miniLineHeight);
                        WORD fillW;

                        snprintf(pctLabel, sizeof(pctLabel), "%lu%%", (unsigned long)pct);

                        URPDC_SetDrawColorFromPen(dcMini, inst->screen, txtPen, bgPen);
                        tile_draw_text(inst, rp, textX, (WORD)(rowY + inst->miniLineAscent), title, dcMini);

                        URPDC_TextSizeUTF8(dcMini, pctLabel, -1, &m);
                        URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                        tile_draw_text(inst, rp,
                                       (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT - m.width),
                                       (WORD)(rowY + inst->miniLineAscent), pctLabel, dcMini);

                        SetAPen(rp, trackPen);
                        RectFill(rp, textX, barY, (WORD)(textX + pollTextW - 1), (WORD)(barY + TTL_POLL_BAR_H - 1));

                        fillW = (WORD)(((LONG)pollTextW * (LONG)pct) / 100);
                        if (fillW > 0) {
                            SetAPen(rp, accentPen);
                            RectFill(rp, textX, barY, (WORD)(textX + fillW - 1), (WORD)(barY + TTL_POLL_BAR_H - 1));
                        }

                        rowY = (WORD)(rowY + inst->miniLineHeight + TTL_POLL_BAR_H + TTL_POLL_ROW_GAP);
                    }

                    {
                        char summary[64];
                        snprintf(summary, sizeof(summary), "%lu votes \xC2\xB7 Poll closed",
                                 (unsigned long)post->pollVotesCount);
                        URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                        tile_draw_text(inst, rp, textX, (WORD)(rowY + inst->miniLineAscent), summary, dcMini);
                    }
                }

                /* Thread indicator: short vertical bar + "..." meaning
                 * "this toot has replies, click to see the discussion" --
                 * threadRowY was computed once by ttl_toot_layout and is
                 * reused verbatim here -- never re-derived, same rule as
                 * pollBlockY above. Not clickable yet (see TTL_HOT_THREAD's
                 * doc comment) -- layout/render only for now. */
                if (post->repliesCount > 0 && post->threadRowY > 0) {
                    LONG accentPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT);
                    WORD rowY    = (WORD)(drawY + post->threadRowY);
                    WORD barBotY = (WORD)(rowY + inst->miniLineHeight - 1);

                    SetAPen(rp, accentPen);
                    Move(rp, textX, rowY);
                    Draw(rp, textX, barBotY);

                    URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
                    tile_draw_text(inst, rp, (WORD)(textX + 6), (WORD)(rowY + inst->miniLineAscent),
                                   "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" /* "•••" */, dcMini);
                }

                /* Action bar: ↩ Reply N  🔁 Boost N  ⭐/💫 N — right-aligned,
                 * normal font. Same row Y formula, and the same labels/
                 * ttl_actionTypes[], that the hot-spot rects in
                 * ttl_post_build_hotspots are measured from (both build
                 * from ttl_build_action_labels(), see that function) --
                 * rather than re-deriving "where does the content end"
                 * from the body spans here (which used to ignore the media
                 * preview rect entirely when it was taller than the text,
                 * or stacked below it), this reads post->height, which
                 * ttl_post_layout already computed correctly accounting
                 * for whichever of the two is taller. */
                {
                    LONG actionPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT);
                    WORD barTopY = (WORD)(drawY + post->height - 1
                                           - TTL_POST_PAD_BOT - inst->lineHeight);
                    WORD barBaselineY = (WORD)(barTopY + inst->lineAscent);
                    WORD xRight = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT);
                    char labels[3][TTL_ACTION_LABEL_MAX];
                    int  a;
                    ttl_build_action_labels(post, labels);
                    URPDC_SetDrawColorFromPen(dcBody, inst->screen, actionPen, bgPen);
                    for (a = 2; a >= 0; a--) {
                        struct URPTextMetric m;
                        struct URPTextPos pos;
                        WORD itemX;
                        LONG nc = utf8_codepoints_range(labels[a],
                                     labels[a] + strlen(labels[a]));
                        URPDC_TextSizeUTF8(dcBody, labels[a], nc, &m);
                        itemX = (WORD)(xRight - m.width);
                        pos.x = itemX;
                        pos.y = barBaselineY;
                        URPDrawTextUTF8(rp, dcBody, &pos, labels[a], (ULONG)nc);
                        xRight = (WORD)(itemX - TTL_ACTION_GAP);
                    }
                }

                /* Modify/Delete -- left-aligned, own toots only. Same bar
                 * row (barBaselineY recomputed identically to the block
                 * above) and same ttl_ownActionTypes[]/ttl_ownActionLabels[]
                 * shared arrays that ttl_toot_build_hotspots measures its
                 * hot-spot rects from -- see the "single shared copy"
                 * comment on those arrays' definition. */
                if (post->isOwn) {
                    LONG actionPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT);
                    WORD barTopY = (WORD)(drawY + post->height - 1
                                           - TTL_POST_PAD_BOT - inst->lineHeight);
                    WORD barBaselineY = (WORD)(barTopY + inst->lineAscent);
                    WORD xLeft = textX;
                    int  a;
                    URPDC_SetDrawColorFromPen(dcBody, inst->screen, actionPen, bgPen);
                    for (a = 0; a < 2; a++) {
                        struct URPTextMetric m;
                        struct URPTextPos pos;
                        LONG nc = utf8_codepoints_range(ttl_ownActionLabels[a],
                                     ttl_ownActionLabels[a] + strlen(ttl_ownActionLabels[a]));
                        URPDC_TextSizeUTF8(dcBody, ttl_ownActionLabels[a], nc, &m);
                        pos.x = xLeft;
                        pos.y = barBaselineY;
                        URPDrawTextUTF8(rp, dcBody, &pos, ttl_ownActionLabels[a], (ULONG)nc);
                        xLeft = (WORD)(xLeft + m.width + TTL_ACTION_GAP);
                    }
                }
            }
        }
}

/* ------------------------------------------------------------------ */
/* ttl_boundary_render -- TTLItemClass.render for TTLLoadNewer_Class /  */
/* TTLLoadOlder_Class: a single centered line drawn from post->body.    */
/* ------------------------------------------------------------------ */

void ttl_boundary_render(TTLData *inst, struct RastPort *rp, TTLPost *post, LONG tileBaseY)
{
    WORD  drawY = (WORD)(post->timelineY - tileBaseY);
    LONG  bgPen  = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);
    LONG  txtPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT);
    struct URPDrawContext *dc = inst->style ? inst->style->dcNormal : NULL;

    if (!dc || !post->body || !post->body[0]) return;

    {
        struct URPTextMetric m;
        struct URPTextPos    pos;
        LONG nc = utf8_codepoints_range(post->body, post->body + strlen(post->body));

        URPDC_SetDrawColorFromPen(dc, inst->screen, txtPen, bgPen);
        URPDC_TextSizeUTF8(dc, post->body, nc, &m);

        pos.x = (WORD)((inst->gadWidth - m.width) / 2);
        pos.y = (WORD)(drawY + (post->height - inst->lineHeight) / 2 + inst->lineAscent);
        URPDrawTextUTF8(rp, dc, &pos, post->body, (ULONG)nc);
    }
}

/* ------------------------------------------------------------------ */
/* ttl_render_tile                                                      */
/*                                                                      */
/* Draw every item that overlaps this tile into its bitmap, dispatching */
/* per-item drawing through item->cls->render -- see the TTLItemClass   */
/* comment in fs3etoottimeline_private.h. The tile background fill and  */
/* the post-bottom separator line are the same for every item kind, so  */
/* they stay here rather than being duplicated per class.               */
/* ------------------------------------------------------------------ */

/* One item's contribution to one tile: ensure-hotspots, class render,
 * bottom separator -- same for every item kind, and (see below) for the
 * profile header too, which isn't a member of the channel's post list so
 * can't just fall out of the loop below. Returns FALSE (nothing to do)
 * if this item doesn't overlap tileBaseY..+TILE_HEIGHT at all. */
static BOOL ttl_render_item_in_tile(TTLData *inst, struct RastPort *rp,
                                     TTLPost *post, LONG tileBaseY)
{
    LONG postTop = post->timelineY;
    LONG postBot = postTop + post->height;

    if (postBot <= tileBaseY)                  return FALSE;
    if (postTop >= tileBaseY + TTL_TILE_HEIGHT) return FALSE;

    /* Pool-backed hot-spots -- cheap no-op if already fresh. Must run
     * before item->cls->render, which may read post->hotSpots. */
    ttl_post_ensure_hotspots(inst, post);

    if (post->cls && post->cls->render)
        post->cls->render(inst, rp, post, tileBaseY);

    /* ---- Separator line at post bottom (every item kind) ---- */
    {
        WORD sepY = (WORD)(postBot - 1 - tileBaseY);
        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
        Move(rp, 0,                sepY);
        Draw(rp, inst->gadWidth-1, sepY);
    }
    return TRUE;
}

void ttl_render_tile(TTLData *inst, TTLTile *tile)
{
    struct RastPort *rp;
    LONG             tileBaseY = tile->tileBaseY;
    TTLPost         *post;
    TTLChannel      *active;
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

    active = ttl_active(inst);

    /* Profile header (see TTLChannel.headerPost) is deliberately not a
     * member of the channel's post list, so the walk below would never
     * see it -- draw it first (it's always timelineY==0, the topmost
     * thing). Guarded/no-op for every channel except Search with a
     * profile loaded. */
    if (active->headerPost)
        ttl_render_item_in_tile(inst, rp, active->headerPost, tileBaseY);

    /* Walk the active channel's items; they are sorted newest-first but
     * their Y values are consecutive, so we can stop as soon as
     * item->timelineY >= tileBaseY+TILE_H */
    for (post = (TTLPost *)active->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        if (post->timelineY >= tileBaseY + TTL_TILE_HEIGHT) break;
        if (post->timelineY + post->height <= tileBaseY) continue;
        ttl_render_item_in_tile(inst, rp, post, tileBaseY);
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
                         const char *postId, BOOL favourited, BOOL following,
                         const char *mediaIds, const char *acct)
{
    TTLData         *inst = TTL_DATA(cl, o);
    struct TagItem  tags[9];
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

    if (mediaIds && mediaIds[0]) {
        ULONG n = (ULONG)strlen(mediaIds);
        if (n >= sizeof(inst->lastHotSpotMediaIds)) n = sizeof(inst->lastHotSpotMediaIds) - 1;
        CopyMem((APTR)mediaIds, inst->lastHotSpotMediaIds, n);
        inst->lastHotSpotMediaIds[n] = '\0';
    } else {
        inst->lastHotSpotMediaIds[0] = '\0';
    }

    if (acct && acct[0]) {
        ULONG n = (ULONG)strlen(acct);
        if (n >= sizeof(inst->lastHotSpotAcct)) n = sizeof(inst->lastHotSpotAcct) - 1;
        CopyMem((APTR)acct, inst->lastHotSpotAcct, n);
        inst->lastHotSpotAcct[n] = '\0';
    } else {
        inst->lastHotSpotAcct[0] = '\0';
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
    tags[4].ti_Tag  = TTIMELINE_LastHotSpotFavourited;
    tags[4].ti_Data = (ULONG)favourited;
    tags[5].ti_Tag  = TTIMELINE_LastHotSpotFollowing;
    tags[5].ti_Data = (ULONG)following;
    tags[6].ti_Tag  = TTIMELINE_LastHotSpotMediaIds;
    tags[6].ti_Data = inst->lastHotSpotMediaIds[0] ? (ULONG)inst->lastHotSpotMediaIds : 0;
    tags[7].ti_Tag  = TTIMELINE_LastHotSpotAcct;
    tags[7].ti_Data = inst->lastHotSpotAcct[0] ? (ULONG)inst->lastHotSpotAcct : 0;
    tags[8].ti_Tag  = TAG_DONE;

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
