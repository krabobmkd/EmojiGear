/*
 * TootTimeline — profile header row (TTLProfileHeader_Class).
 *
 * Shown pinned above a channel's post list when a user profile is open
 * (see TTIMELINE_ShowProfile in fs3etoottimeline.h). Deliberately NOT a
 * member of that post list -- see TTLChannel.headerPost in
 * fs3etoottimeline_private.h for why, and fs3etoottimeline_posts.c /
 * fs3etoottimeline_input.c / fs3etoottimeline_render.c for the small
 * guarded additions that let it participate in tile rendering, hit-
 * testing, and scroll clamping despite that.
 *
 * Layout mirrors ttl_toot_layout's top-to-bottom structure closely, and
 * reuses the exact same span/hot-spot building blocks (ttl_span_alloc,
 * ttl_span_from_row, ttl_hs_add, ttl_scan_span_tokens) a toot uses for its
 * own username/acct/body -- the bio gets identical @mention/#hashtag/URL
 * hot-spot treatment for free from ttl_scan_span_tokens, which is already
 * generic over any TTL_SPAN_BODY span.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <string.h>
#include <stdio.h>

#include "fs3etoottimeline_private.h"
#include "../fs3etextwrap.h"
#include "../avatarimages.h"
#include "../bmimage.h"
#include "../bdbprintf.h"

/* Button zone (Follow/Unfollow today, room for another button later --
 * see the layout comment below) reserved at the very BOTTOM of the
 * header, right above the separator -- same convention as a toot's own
 * Reply/Boost/Fave action bar. .layout reserves this height once;
 * .render/.buildHotspots both re-derive the same row's Y by counting
 * back from post->height, so all three MUST agree on its height, which
 * is why this one shared helper computes it instead of three separate
 * copies (a previous version had layout and render disagree on where
 * this row was, which drew the button on top of the bio). */
/* This is an important, primary action (unlike the small text-only
 * Reply/Boost/Fave labels it otherwise takes visual cues from) -- drawn
 * with dcNormal (body-size font, not dcMini) and roughly double the
 * padding of a typical small button, so the reserved row/rect end up
 * roughly twice the size of a plain text-label row. */
#define TTL_PROFILE_BUTTON_PADX 10
#define TTL_PROFILE_BUTTON_PADY 6
#define TTL_PROFILE_BUTTON_GAP  4

static WORD ttl_profile_button_row_height(TTLData *inst)
{
    return (WORD)(inst->lineHeight + TTL_PROFILE_BUTTON_PADY * 2);
}

/* Followers/Following buttons -- same "reserved row above/below other
 * content" convention as the Follow/Unfollow button above, but dcMini
 * sized (these are secondary/informational, not the one primary action)
 * so they don't compete visually with Follow/Unfollow. */
static WORD ttl_profile_counts_row_height(TTLData *inst)
{
    return (WORD)(inst->miniLineHeight + TTL_PROFILE_BUTTON_PADY * 2);
}

/* Computes both counts-buttons' geometry from one shared place, called
 * identically by .layout (height only), .render (to draw), and
 * .buildHotspots (to size the hit rects) -- unlike
 * ttl_profile_button_row_height above, sharing just a height isn't
 * enough here: box WIDTHS must also match exactly between .render and
 * .buildHotspots, or a click lands on the wrong button (or nothing), the
 * same class of bug this file's header comment already warns about for
 * button-row Y. followersLabel/followingLabel are filled in here so
 * .render can reuse them for drawing without a second snprintf. */
static void ttl_profile_counts_box_layout(TTLData *inst, TTLPost *post, WORD textX,
    char *followersLabel, ULONG followersLabelSz,
    char *followingLabel, ULONG followingLabelSz,
    WORD *box1X, WORD *box1W, WORD *box2X, WORD *box2W, WORD *boxH)
{
    snprintf(followersLabel, followersLabelSz, "%lu Followers",
             (unsigned long)post->followersCount);
    snprintf(followingLabel, followingLabelSz, "%lu Following",
             (unsigned long)post->followingCount);

    *boxH = ttl_profile_counts_row_height(inst);

    if (inst->style && inst->style->dcMini) {
        struct URPTextMetric m;
        LONG nc;

        nc = utf8_codepoints_range(followersLabel, followersLabel + strlen(followersLabel));
        URPDC_TextSizeUTF8(inst->style->dcMini, followersLabel, nc, &m);
        *box1W = (WORD)(m.width + TTL_PROFILE_BUTTON_PADX * 2);

        nc = utf8_codepoints_range(followingLabel, followingLabel + strlen(followingLabel));
        URPDC_TextSizeUTF8(inst->style->dcMini, followingLabel, nc, &m);
        *box2W = (WORD)(m.width + TTL_PROFILE_BUTTON_PADX * 2);
    } else {
        *box1W = *box2W = 0;
    }

    *box1X = textX;
    *box2X = (WORD)(textX + *box1W + TTL_PROFILE_BUTTON_GAP);
}

static char *dup_str(const char *s)
{
    ULONG len;
    char *copy;
    if (!s) return NULL;
    len  = (ULONG)strlen(s);
    copy = (char *)AllocVec(len + 1, MEMF_ANY);
    if (copy) CopyMem((APTR)s, copy, len + 1);
    return copy;
}

/* ------------------------------------------------------------------ */
/* ttl_profile_header_alloc                                             */
/* ------------------------------------------------------------------ */

TTLPost *ttl_profile_header_alloc(const TTLProfileHeaderSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls           = &TTLProfileHeader_Class;
    post->dirty         = TRUE;
    post->hotSpotBucket = -1;
    post->hotSpotsDirty = TRUE;

    if (setup) {
        /* postId doubles as this row's Mastodon *account* id -- see the
         * TTLPost field comment in fs3etoottimeline_private.h. */
        post->postId    = (setup->accountId && setup->accountId[0]) ? dup_str(setup->accountId) : NULL;
        post->username  = dup_str(setup->username);
        post->acct      = dup_str(setup->acct);
        post->avatarURL = (setup->avatarURL && setup->avatarURL[0]) ? dup_str(setup->avatarURL) : NULL;
        post->body      = dup_str(setup->bio); /* already HTML-stripped by the caller */

        post->followersCount = setup->followersCount;
        post->followingCount = setup->followingCount;
        post->following       = setup->following;
        /* setup->showFollow isn't stored -- .buildHotspots and .render
         * both need it, so it's cheaper to just derive "should we show
         * a Follow control" from postId being set (self-profiles pass
         * showFollow=FALSE together with accountId==our own id, but
         * there's no separate flag field on TTLPost for it). Simplest:
         * reuse mediaCount, unused by this class, as a 0/1 flag. */
        post->mediaCount = setup->showFollow ? 1 : 0;
    }

    return post;
}

static void ttl_profile_header_dispose(TTLPost *post)
{
    if (post->postId)    FreeVec(post->postId);
    if (post->username)  FreeVec(post->username);
    if (post->acct)      FreeVec(post->acct);
    if (post->avatarURL) FreeVec(post->avatarURL);
    if (post->body)      FreeVec(post->body);
}

/* ------------------------------------------------------------------ */
/* ttl_profile_header_layout                                            */
/* ------------------------------------------------------------------ */

static void ttl_profile_header_layout(TTLData *inst, TTLPost *post)
{
    WORD avatarW, padLeft, avatarGap, textX, textW;
    LONG curRelY;

    ttl_clear_textspans(post);

    if (inst->style && inst->style->avatarSize > 0) {
        avatarW   = inst->style->avatarSize;
        padLeft   = inst->style->postPadLeft;
        avatarGap = inst->style->avatarGap;
    } else {
        avatarW   = 35;
        padLeft   = 6;
        avatarGap = 6;
    }
    textX = (WORD)(padLeft + avatarW + avatarGap);
    textW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
    if (textW < 32) textW = 32;

    curRelY = TTL_POST_PAD_TOP;

    /* Display name */
    if (post->username && post->username[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->username, TTL_SPAN_USERNAME, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->nameLineHeight;

    /* @acct */
    if (post->acct && post->acct[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->acct, TTL_SPAN_ACCT, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->miniLineHeight;

    /* Followers/Following buttons -- drawn boxes (see .render), not a
     * text span, same style as the Follow/Unfollow button below.
     * countsRowY stored here as "top of this button row" and reused
     * as-is by .render/.buildHotspots, never re-derived, same "store
     * once" rule as pollBlockY/threadRowY elsewhere in this file. */
    {
        char followersLabel[40], followingLabel[40];
        WORD box1X, box1W, box2X, box2W, boxH;
        post->countsRowY = (WORD)curRelY;
        ttl_profile_counts_box_layout(inst, post, textX,
            followersLabel, sizeof(followersLabel),
            followingLabel, sizeof(followingLabel),
            &box1X, &box1W, &box2X, &box2W, &boxH);
        curRelY += TTL_PROFILE_BUTTON_GAP + boxH;
    }

    /* Make sure curRelY is at least below the avatar */
    {
        LONG minY = (LONG)TTL_POST_PAD_TOP + avatarW;
        if (curRelY < minY) curRelY = minY;
    }

    curRelY += 4; /* gap above the bio */

    /* Bio, word-wrapped exactly like a toot body -- one TTL_SPAN_BODY
     * span per visual row, scanned for hot-spots in .buildHotspots via
     * the same ttl_scan_span_tokens() toot bodies use. */
    if (post->body && post->body[0]) {
        struct URPDrawContext *dcBody = inst->style ? inst->style->dcNormal : NULL;
        if (dcBody) {
            FS3ETextWrap tw;
            if (FS3ETextWrap_Build(&tw, dcBody, inst->lineHeight, post->body, textW)) {
                ULONG i;
                for (i = 0; i < tw.rowCount; i++) {
                    TTLTextSpan *sp = ttl_span_from_row(&tw.rows[i], textX, curRelY, inst);
                    if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
                    curRelY += inst->lineHeight;
                }
                FS3ETextWrap_Free(&tw);
            }
        } else {
            curRelY += ttl_count_wrapped_lines(inst, post->body, textW) * inst->lineHeight;
        }
    }

    /* Follow/Unfollow button zone -- reserved as the LAST row before the
     * separator (see ttl_profile_button_row_height's comment for why this
     * must stay last: .render/.buildHotspots measure this row's Y by
     * counting back from post->height, not forward from here). Deliberately
     * a whole row, not just enough for one label, so a later second button
     * (e.g. a "This Toot"-style menu action) has somewhere to go without
     * another layout change. Skipped entirely for a self-profile
     * (post->mediaCount holds the showFollow flag -- see
     * ttl_profile_header_alloc). */
    if (post->mediaCount != 0)
        curRelY += TTL_PROFILE_BUTTON_GAP + ttl_profile_button_row_height(inst);

    curRelY += TTL_POST_PAD_BOT;
    curRelY += 1; /* separator pixel, drawn generically by the tile-render caller */
    post->height = curRelY;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* ttl_profile_header_render                                            */
/* ------------------------------------------------------------------ */

static void ttl_profile_header_render(TTLData *inst, struct RastPort *rp,
                                       TTLPost *post, LONG tileBaseY)
{
    WORD  avatarW, padLeft;
    LONG  bgpen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_PROFILE_HEADER_BG);
    WORD  drawY = (WORD)(post->timelineY - tileBaseY);
    TTLTextSpan *sp;

    if (inst->style && inst->style->avatarSize > 0) {
        avatarW = inst->style->avatarSize;
        padLeft = inst->style->postPadLeft;
    } else {
        avatarW = 35;
        padLeft = 6;
    }

    /* ---- Background: a distinct color from the ordinary timeline
     * background (see FS3E_COLOR_PROFILE_HEADER_BG's comment) so this
     * reads as "not a toot" at a glance. Overrides the shared per-tile
     * fill ttl_render_tile already did for the WHOLE tile in
     * FS3E_COLOR_TIMELINE_BG, clipped to however much of this row is
     * actually visible within this tile's [0, TTL_TILE_HEIGHT) band --
     * the header can span more than one tile. ---- */
    {
        WORD fillTop = drawY > 0 ? drawY : 0;
        WORD fillBot = (WORD)(drawY + post->height);
        if (fillBot > TTL_TILE_HEIGHT) fillBot = TTL_TILE_HEIGHT;
        if (fillBot > fillTop) {
            SetAPen(rp, bgpen);
            RectFill(rp, 0, fillTop, (WORD)(inst->gadWidth - 1), (WORD)(fillBot - 1));
        }
    }

    /* ---- Avatar: same cache/draw pipeline as a toot's, keyed by acct ---- */
    {
        WORD ax = padLeft;
        WORD ay = (WORD)(drawY + TTL_POST_PAD_TOP);
        WORD as = avatarW;
        RgbImage *avImg = (inst->avatarImages && post->acct)
                        ? AvatarImages_Get(inst->avatarImages, post->acct) : NULL;

        if (avImg) {
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
            ttl_draw_avatar_placeholder(rp, ax, ay, as,
                (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT), bgpen);
        }
    }

    /* ---- Name/acct/counts spans ---- */
    for (sp = (TTLTextSpan *)post->textSpans.mlh_Head;
         sp->node.mln_Succ;
         sp = (TTLTextSpan *)sp->node.mln_Succ)
    {
        struct URPDrawContext *dc;
        LONG pen;
        struct URPTextPos pos;

        if (!sp->utf8 || !sp->utf8[0]) continue;

        switch (sp->spanType) {
            case TTL_SPAN_USERNAME:
                dc  = inst->style->dcUsername;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_USERNAME);
                break;
            case TTL_SPAN_ACCT:
            case TTL_SPAN_TIMESTAMP:
                dc  = inst->style->dcMini;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT_DIM);
                break;
            default: /* TTL_SPAN_BODY -- the bio */
                dc  = inst->style->dcNormal;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT);
                break;
        }
        if (!dc) continue;

        URPDC_SetDrawColorFromPen(dc, inst->screen, pen, bgpen);
        pos.x = sp->x;
        pos.y = (WORD)(drawY + sp->postRelY + sp->ascent);
        URPDrawTextUTF8(rp, dc, &pos, sp->utf8, (ULONG)sp->charCount);
    }

    /* ---- Followers/Following buttons -- see ttl_profile_counts_box_layout
     * for why box geometry is computed by one shared helper instead of
     * duplicating measurements between here and .buildHotspots. Unlike
     * the Follow/Unfollow button below, boxY is counted FORWARD from
     * countsRowY (this row isn't last), not backward from post->height. ---- */
    if (inst->style && inst->style->dcMini) {
        char followersLabel[40], followingLabel[40];
        WORD box1X, box1W, box2X, box2W, boxH, boxY;
        WORD textX = (WORD)(padLeft + avatarW +
                             (inst->style ? inst->style->avatarGap : 6));
        struct URPDrawContext *dc = inst->style->dcMini;
        struct URPTextPos pos;
        LONG nc;

        ttl_profile_counts_box_layout(inst, post, textX,
            followersLabel, sizeof(followersLabel),
            followingLabel, sizeof(followingLabel),
            &box1X, &box1W, &box2X, &box2W, &boxH);
        boxY = (WORD)(drawY + post->countsRowY);

        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_BUTTON_BG));
        RectFill(rp, box1X, boxY, (WORD)(box1X + box1W - 1), (WORD)(boxY + boxH - 1));
        RectFill(rp, box2X, boxY, (WORD)(box2X + box2W - 1), (WORD)(boxY + boxH - 1));

        URPDC_SetDrawColorFromPen(dc, inst->screen,
            (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT),
            (LONG)FS3E_PEN(inst->style, FS3E_COLOR_BUTTON_BG));

        nc = utf8_codepoints_range(followersLabel, followersLabel + strlen(followersLabel));
        pos.x = (WORD)(box1X + TTL_PROFILE_BUTTON_PADX);
        pos.y = (WORD)(boxY + TTL_PROFILE_BUTTON_PADY + inst->miniLineAscent);
        URPDrawTextUTF8(rp, dc, &pos, followersLabel, (ULONG)nc);

        nc = utf8_codepoints_range(followingLabel, followingLabel + strlen(followingLabel));
        pos.x = (WORD)(box2X + TTL_PROFILE_BUTTON_PADX);
        pos.y = (WORD)(boxY + TTL_PROFILE_BUTTON_PADY + inst->miniLineAscent);
        URPDrawTextUTF8(rp, dc, &pos, followingLabel, (ULONG)nc);
    }

    /* ---- Follow/Unfollow button (left) + Message button (right) -- same
     * row, reserved as the LAST row before the separator (see .layout and
     * ttl_profile_button_row_height's comment); boxH/boxY MUST match that
     * reservation exactly, or the buttons end up drawn over whatever
     * .layout actually left space for. Message shares Follow/Unfollow's
     * visibility condition (post->mediaCount != 0, i.e. hidden on a
     * self-profile -- see ttl_profile_header_alloc) since messaging
     * yourself isn't a useful action either. ---- */
    if (post->mediaCount != 0 && inst->style && inst->style->dcNormal) {
        struct URPDrawContext *dc = inst->style->dcNormal;
        WORD  boxH = ttl_profile_button_row_height(inst);
        WORD  boxY = (WORD)(drawY + post->height - 1 - TTL_POST_PAD_BOT - boxH);
        struct URPTextPos    pos;

        /* Follow/Unfollow */
        {
            const char *label = post->following ? "Following" : "Follow";
            struct URPTextMetric m;
            LONG  nc = utf8_codepoints_range(label, label + strlen(label));
            WORD  boxW, boxX;
            WORD  textX = (WORD)(padLeft + avatarW +
                                  (inst->style ? inst->style->avatarGap : 6));

            URPDC_TextSizeUTF8(dc, label, nc, &m);
            boxW = (WORD)(m.width + TTL_PROFILE_BUTTON_PADX * 2);
            boxX = textX;

            SetAPen(rp, (LONG)FS3E_PEN(inst->style,
                post->following ? FS3E_COLOR_BUTTON_SELECTED_BG : FS3E_COLOR_BUTTON_BG));
            RectFill(rp, boxX, boxY, (WORD)(boxX + boxW - 1), (WORD)(boxY + boxH - 1));

            URPDC_SetDrawColorFromPen(dc, inst->screen,
                (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT),
                (LONG)FS3E_PEN(inst->style,
                    post->following ? FS3E_COLOR_BUTTON_SELECTED_BG : FS3E_COLOR_BUTTON_BG));
            pos.x = (WORD)(boxX + TTL_PROFILE_BUTTON_PADX);
            pos.y = (WORD)(boxY + TTL_PROFILE_BUTTON_PADY + inst->lineAscent);
            URPDrawTextUTF8(rp, dc, &pos, label, (ULONG)nc);
        }

        /* Message -- right-aligned, same row */
        {
            const char *label = "Message";
            struct URPTextMetric m;
            LONG  nc = utf8_codepoints_range(label, label + strlen(label));
            WORD  boxW, boxX;

            URPDC_TextSizeUTF8(dc, label, nc, &m);
            boxW = (WORD)(m.width + TTL_PROFILE_BUTTON_PADX * 2);
            boxX = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT - boxW);

            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_BUTTON_BG));
            RectFill(rp, boxX, boxY, (WORD)(boxX + boxW - 1), (WORD)(boxY + boxH - 1));

            URPDC_SetDrawColorFromPen(dc, inst->screen,
                (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACTION_TEXT),
                (LONG)FS3E_PEN(inst->style, FS3E_COLOR_BUTTON_BG));
            pos.x = (WORD)(boxX + TTL_PROFILE_BUTTON_PADX);
            pos.y = (WORD)(boxY + TTL_PROFILE_BUTTON_PADY + inst->lineAscent);
            URPDrawTextUTF8(rp, dc, &pos, label, (ULONG)nc);
        }
    }
}

/* ------------------------------------------------------------------ */
/* ttl_profile_header_build_hotspots                                    */
/* ------------------------------------------------------------------ */

static void ttl_profile_header_build_hotspots(TTLData *inst, TTLPost *post)
{
    WORD textX;
    TTLTextSpan *sp;

    post->hotSpotCount = 0;

    if (inst->style && inst->style->avatarSize > 0) {
        textX = (WORD)(inst->style->postPadLeft + inst->style->avatarSize + inst->style->avatarGap);
    } else {
        textX = (WORD)(6 + 35 + 6);
    }

    /* No avatar/@acct hot-spot here, unlike a toot's -- both would just
     * re-open the exact profile already being shown, which isn't a
     * useful click (unlike a toot's avatar, which always points at a
     * DIFFERENT profile than whatever's currently on screen). @mentions
     * inside the bio still open THEIR OWN (different) profile via
     * ttl_scan_span_tokens below, same as always. */
    for (sp = (TTLTextSpan *)post->textSpans.mlh_Head;
         sp->node.mln_Succ && post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT;
         sp = (TTLTextSpan *)sp->node.mln_Succ)
    {
        if (sp->spanType == TTL_SPAN_BODY)
            ttl_scan_span_tokens(post, sp);
    }

    /* Followers/Following button hit rects -- geometry computed by the
     * exact same shared helper .render calls, so a hotspot can never
     * drift from the box actually drawn (see ttl_profile_counts_box_
     * layout's own comment). */
    if (inst->style && inst->style->dcMini) {
        char followersLabel[40], followingLabel[40];
        WORD box1X, box1W, box2X, box2W, boxH;

        ttl_profile_counts_box_layout(inst, post, textX,
            followersLabel, sizeof(followersLabel),
            followingLabel, sizeof(followingLabel),
            &box1X, &box1W, &box2X, &box2W, &boxH);

        ttl_hs_add(post, TTL_HOT_FOLLOWERS_LIST, box1X, post->countsRowY, box1W, boxH, NULL, 0);
        ttl_hs_add(post, TTL_HOT_FOLLOWING_LIST, box2X, post->countsRowY, box2W, boxH, NULL, 0);
    }

    /* Same row as .render draws into -- see ttl_profile_button_row_height's
     * comment for why boxH/boxY must match that exactly. Message shares
     * Follow/Unfollow's visibility condition -- see .render's comment. */
    if (post->mediaCount != 0 && inst->style && inst->style->dcNormal) {
        WORD boxH = ttl_profile_button_row_height(inst);
        WORD boxY = (WORD)(post->height - 1 - TTL_POST_PAD_BOT - boxH);

        {
            const char *label = post->following ? "Following" : "Follow";
            struct URPTextMetric m;
            LONG  nc = utf8_codepoints_range(label, label + strlen(label));
            WORD  boxW = 0, boxX;

            URPDC_TextSizeUTF8(inst->style->dcNormal, label, nc, &m);
            boxW = (WORD)(m.width + TTL_PROFILE_BUTTON_PADX * 2);
            boxX = textX;

            ttl_hs_add(post, TTL_HOT_FOLLOW, boxX, boxY, boxW, boxH, NULL, 0);
        }

        {
            const char *label = "Message";
            struct URPTextMetric m;
            LONG  nc = utf8_codepoints_range(label, label + strlen(label));
            WORD  boxW, boxX;

            URPDC_TextSizeUTF8(inst->style->dcNormal, label, nc, &m);
            boxW = (WORD)(m.width + TTL_PROFILE_BUTTON_PADX * 2);
            boxX = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT - boxW);

            ttl_hs_add(post, TTL_HOT_MESSAGE, boxX, boxY, boxW, boxH, NULL, 0);
        }
    }
}

/* ------------------------------------------------------------------ */

const TTLItemClass TTLProfileHeader_Class = {
    ttl_profile_header_layout,
    ttl_profile_header_render,
    ttl_profile_header_build_hotspots,
    NULL, /* .activate: the generic ttl_notify_hotspot() already fires for
           * every hot-spot type (TTL_HOT_FOLLOW included), same reasoning
           * as TTLLoadNewer_Class -- nothing kind-specific to do locally. */
    ttl_profile_header_dispose
};
