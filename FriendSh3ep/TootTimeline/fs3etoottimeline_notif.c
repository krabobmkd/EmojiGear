/*
 * TootTimeline — account-only notification row (TTLNotifFollow_Class).
 *
 * Shown for FOLLOW/FOLLOW_REQUEST notifications (see
 * network_fs3e/fs3enet.h's FS3ENetNotifType) -- these carry no status at
 * all, unlike every other notification type, which reuses TTLToot_Class
 * with a generalized actor/verb prefix line instead (see
 * TTLPostSetup.notifType in fs3etoottimeline.h). A normal scrolling list
 * item, added via TTIMELINE_AddPost/AppendPost exactly like a toot --
 * unlike TTLProfileHeader_Class (whose layout this borrows heavily from),
 * this is NOT a single pinned header, just a much shorter row: avatar +
 * name + acct + "followed you", no bio/counts/Follow-button.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <string.h>

#include "fs3etoottimeline_private.h"
#include "../avatarimages.h"
#include "../bmimage.h"

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
/* ttl_notif_follow_alloc                                               */
/* ------------------------------------------------------------------ */

/* Takes the same TTLPostSetup a toot does -- only username/acct/avatarURL/
 * postId/notifType are used. postId is the *notification's* own id (see
 * TTLPostSetup.notifStatusId's comment on why that distinction matters
 * for pagination); username/acct/avatarURL describe the account that
 * followed you, reusing the plain fields rather than notifActorName/Acct
 * since there's no separate "status author" here to distinguish them
 * from -- the actor IS the whole row. */
TTLPost *ttl_notif_follow_alloc(const TTLPostSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls           = &TTLNotifFollow_Class;
    post->dirty         = TRUE;
    post->hotSpotBucket = -1;
    post->hotSpotsDirty = TRUE;

    if (setup) {
        post->postId    = (setup->postId && setup->postId[0]) ? dup_str(setup->postId) : NULL;
        post->username  = dup_str(setup->username);
        post->acct      = dup_str(setup->acct);
        post->avatarURL = (setup->avatarURL && setup->avatarURL[0]) ? dup_str(setup->avatarURL) : NULL;
        post->notifType = setup->notifType;
    }

    return post;
}

static void ttl_notif_follow_dispose(TTLPost *post)
{
    if (post->postId)    FreeVec(post->postId);
    if (post->username)  FreeVec(post->username);
    if (post->acct)      FreeVec(post->acct);
    if (post->avatarURL) FreeVec(post->avatarURL);
}

/* ------------------------------------------------------------------ */
/* ttl_notif_follow_layout                                              */
/* ------------------------------------------------------------------ */

static void ttl_notif_follow_layout(TTLData *inst, TTLPost *post)
{
    WORD avatarW, padLeft, avatarGap, textX;
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

    /* "followed you" / "requested to follow you" -- plain mini text,
     * same TTL_SPAN_TIMESTAMP-as-generic-dim-text reuse the profile
     * header's counts row already does. */
    {
        const char *verb = (post->notifType == TTL_NOTIF_FOLLOW_REQUEST)
                          ? "requested to follow you" : "followed you";
        TTLTextSpan *sp = ttl_span_alloc(verb, TTL_SPAN_TIMESTAMP, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->miniLineHeight;

    /* Make sure curRelY is at least below the avatar */
    {
        LONG minY = (LONG)TTL_POST_PAD_TOP + avatarW;
        if (curRelY < minY) curRelY = minY;
    }

    curRelY += TTL_POST_PAD_BOT;
    curRelY += 1; /* separator pixel, drawn generically by the tile-render caller */
    post->height = curRelY;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* ttl_notif_follow_render                                              */
/* ------------------------------------------------------------------ */

static void ttl_notif_follow_render(TTLData *inst, struct RastPort *rp,
                                     TTLPost *post, LONG tileBaseY)
{
    WORD  avatarW, padLeft;
    LONG  bgpen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);
    WORD  drawY = (WORD)(post->timelineY - tileBaseY);
    TTLTextSpan *sp;

    if (inst->style && inst->style->avatarSize > 0) {
        avatarW = inst->style->avatarSize;
        padLeft = inst->style->postPadLeft;
    } else {
        avatarW = 35;
        padLeft = 6;
    }

    /* ---- Avatar: same cache/draw pipeline as a toot's/profile header's,
     * keyed by acct ---- */
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

    /* ---- Name/acct/verb spans ---- */
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
            default: /* TTL_SPAN_ACCT / TTL_SPAN_TIMESTAMP -- @acct and the verb line */
                dc  = inst->style->dcMini;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT_DIM);
                break;
        }
        if (!dc) continue;

        URPDC_SetDrawColorFromPen(dc, inst->screen, pen, bgpen);
        pos.x = sp->x;
        pos.y = (WORD)(drawY + sp->postRelY + sp->ascent);
        URPDrawTextUTF8(rp, dc, &pos, sp->utf8, (ULONG)sp->charCount);
    }
}

/* ------------------------------------------------------------------ */
/* ttl_notif_follow_build_hotspots                                      */
/* ------------------------------------------------------------------ */

/* One hotspot covering the whole row -- reuses TTL_HOT_AVATAR verbatim
 * (data = post->acct), same as clicking any toot's avatar; opening this
 * account's profile is already exactly the right action, no new hot-spot
 * type needed (see TTL_HOT_NOTIF_STATUS's doc comment for why the
 * status-bearing notification types DO need one of their own -- they
 * have two different ids in play, this row only ever has one). */
static void ttl_notif_follow_build_hotspots(TTLData *inst, TTLPost *post)
{
    post->hotSpotCount = 0;

    if (post->acct && post->acct[0])
        ttl_hs_add(post, TTL_HOT_AVATAR, 0, 0,
                   inst->gadWidth, (WORD)post->height,
                   post->acct, (ULONG)strlen(post->acct));
}

/* ------------------------------------------------------------------ */

const TTLItemClass TTLNotifFollow_Class = {
    ttl_notif_follow_layout,
    ttl_notif_follow_render,
    ttl_notif_follow_build_hotspots,
    NULL, /* .activate: the generic ttl_notify_hotspot() already fires for
           * TTL_HOT_AVATAR, same reasoning as every other class here --
           * nothing kind-specific to do locally. */
    ttl_notif_follow_dispose
};
