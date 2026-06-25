/*
 * TootTimeline – post list management.
 *
 * Post heights are computed from the body text length, the current font
 * metrics (lineHeight), and the gadget width.  When dc is available the
 * height is derived from a proper advance-width measurement; otherwise a
 * conservative character-count estimate is used.
 *
 * Y positions are rebuilt whenever a post is added or the gadget width
 * changes: posts are stored newest-first (head) and their timelineY
 * values decrease toward more negative values.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <string.h>
#include "fs3etoottimeline_private.h"

/* ------------------------------------------------------------------ */
/* Static helpers                                                       */
/* ------------------------------------------------------------------ */

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

/* Number of visual lines needed to display utf8 in a column of maxW pixels.
 * When dc is available a real advance-width measurement is used; otherwise
 * falls back to a character-count heuristic. */
static LONG ttl_count_wrapped_lines(TTLData *inst, const char *utf8, WORD maxW)
{
    LONG lines;

    if (!utf8 || !utf8[0] || maxW <= 0) return 0;

    if (inst->dc) {
        struct URPTextMetric m;
        URPDC_TextSizeUTF8(inst->dc, utf8, -1, &m);
        if (m.width <= 0) return 0;
        lines = (m.width + maxW - 1) / maxW;
    } else {
        /* Heuristic: assume avg glyph width ≈ lineHeight/2 */
        LONG avgGlyphW = inst->lineHeight > 0 ? inst->lineHeight / 2 : 7;
        LONG textW     = (LONG)strlen(utf8) * avgGlyphW;
        lines = (textW + maxW - 1) / maxW;
    }

    return lines < 1 ? 1 : lines;
}

/* ------------------------------------------------------------------ */
/* TTLTextSpan helpers                                                  */
/* ------------------------------------------------------------------ */

static TTLTextSpan *ttl_span_alloc(const char *utf8, UBYTE spanType,
                                   LONG postRelY, WORD x,
                                   TTLData *inst)
{
    TTLTextSpan *sp = (TTLTextSpan *)AllocVec(sizeof(TTLTextSpan),
                                               MEMF_ANY | MEMF_CLEAR);
    if (!sp) return NULL;

    sp->spanType = spanType;
    sp->postRelY = postRelY;
    sp->x        = x;
    sp->height   = inst->lineHeight;
    sp->ascent   = inst->lineAscent;
    sp->utf8     = dup_str(utf8);
    sp->byteLen  = utf8 ? (ULONG)strlen(utf8) : 0;

    if (inst->dc && sp->utf8 && sp->byteLen > 0) {
        /* Count codepoints (simplified: assume each char ≤ 4 bytes) */
        ULONG cc = 0;
        const unsigned char *p = (const unsigned char *)sp->utf8;
        while (*p) {
            unsigned char c = *p;
            if      (c < 0x80) p += 1;
            else if (c < 0xE0) p += 2;
            else if (c < 0xF0) p += 3;
            else               p += 4;
            cc++;
        }
        sp->charCount = cc;
        if (cc > 0) {
            sp->charXOffsets = (LONG *)AllocVec((cc + 1) * sizeof(LONG),
                                                 MEMF_ANY | MEMF_CLEAR);
            if (sp->charXOffsets)
                URPDC_HorizontalOffsetArrayUTF8(inst->dc, sp->utf8,
                                                (LONG)cc, sp->charXOffsets);
            if (sp->charXOffsets && cc > 0)
                sp->width = (WORD)sp->charXOffsets[cc];
        }
    }

    return sp;
}

static void ttl_span_free(TTLTextSpan *sp)
{
    if (!sp) return;
    if (sp->utf8)         FreeVec(sp->utf8);
    if (sp->charXOffsets) FreeVec(sp->charXOffsets);
    FreeVec(sp);
}

static void ttl_clear_textspans(TTLPost *post)
{
    struct Node *node;
    while ((node = RemHead((struct List *)&post->textSpans)) != NULL)
        ttl_span_free((TTLTextSpan *)node);
}

/* ------------------------------------------------------------------ */
/* TTLHotSpot helpers                                                   */
/* ------------------------------------------------------------------ */

static void ttl_clear_hotspots(TTLPost *post)
{
    struct Node *node;
    while ((node = RemHead((struct List *)&post->hotSpots)) != NULL) {
        TTLHotSpot *hs = (TTLHotSpot *)node;
        if (hs->data) FreeVec(hs->data);
        FreeVec(hs);
    }
}

/* ------------------------------------------------------------------ */
/* ttl_post_alloc                                                       */
/* ------------------------------------------------------------------ */

TTLPost *ttl_post_alloc(const TTLPostSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->hotSpots);
    NewList((struct List *)&post->textSpans);
    post->dirty = TRUE;

    if (setup) {
        post->username  = dup_str(setup->username);
        post->acct      = dup_str(setup->acct);
        post->body      = dup_str(setup->body);
        post->timestamp = dup_str(setup->timestamp);
    }

    return post;
}

/* ------------------------------------------------------------------ */
/* ttl_post_free                                                        */
/* ------------------------------------------------------------------ */

void ttl_post_free(TTLPost *post)
{
    if (!post) return;
    ttl_clear_textspans(post);
    ttl_clear_hotspots(post);
    if (post->username)  FreeVec(post->username);
    if (post->acct)      FreeVec(post->acct);
    if (post->body)      FreeVec(post->body);
    if (post->timestamp) FreeVec(post->timestamp);
    FreeVec(post);
}

/* ------------------------------------------------------------------ */
/* ttl_post_layout                                                      */
/*                                                                      */
/* Compute post->height and rebuild the textSpans list.                 */
/* Called whenever the gadget width or font changes.                    */
/* ------------------------------------------------------------------ */

void ttl_post_layout(TTLData *inst, TTLPost *post)
{
    WORD  avatarW, textX, textW;
    LONG  curRelY;
    LONG  bodyLines;
    LONG  avatarH;

    ttl_clear_textspans(post);

    avatarW = (WORD)(inst->dpiHeight * 2);  /* avatar column width */
    textX   = (WORD)(TTL_POST_PAD_LEFT + avatarW + TTL_AVATAR_GAP);
    textW   = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
    if (textW < 32) textW = 32;

    avatarH = avatarW;  /* square avatar */

    curRelY = TTL_POST_PAD_TOP;

    /* Username span */
    if (post->username && post->username[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->username, TTL_SPAN_USERNAME,
                                          curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->lineHeight;

    /* Acct span */
    if (post->acct && post->acct[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->acct, TTL_SPAN_ACCT,
                                          curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->lineHeight;

    /* Make sure curRelY is at least below the avatar */
    {
        LONG minY = TTL_POST_PAD_TOP + avatarH;
        if (curRelY < minY) curRelY = minY;
    }

    /* Body text spans (one per wrapped visual line) */
    bodyLines = ttl_count_wrapped_lines(inst, post->body, textW);
    if (post->body && post->body[0]) {
        /* For now record the whole body as one span (word-wrap hit-testing
         * will use charXOffsets + per-line character ranges in the future). */
        TTLTextSpan *sp = ttl_span_alloc(post->body, TTL_SPAN_BODY,
                                          curRelY, textX, inst);
        if (sp) {
            sp->height = (WORD)(bodyLines * inst->lineHeight);
            AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
        }
    }
    curRelY += bodyLines * inst->lineHeight;

    curRelY += TTL_POST_PAD_BOT;
    curRelY += 1;  /* separator pixel */

    post->height = curRelY;
    post->dirty  = TRUE;

    /* Avatar hot-spot */
    ttl_clear_hotspots(post);
    {
        TTLHotSpot *hs = (TTLHotSpot *)AllocVec(sizeof(TTLHotSpot),
                                                  MEMF_ANY | MEMF_CLEAR);
        if (hs) {
            hs->type = TTL_HOT_AVATAR;
            hs->x    = TTL_POST_PAD_LEFT;
            hs->y    = TTL_POST_PAD_TOP;
            hs->w    = avatarW;
            hs->h    = (WORD)avatarH;
            hs->data = dup_str(post->acct);
            AddTail((struct List *)&post->hotSpots, (struct Node *)&hs->node);
        }
    }
}

/* ------------------------------------------------------------------ */
/* ttl_layout_all_posts                                                 */
/*                                                                      */
/* Recompute heights for all posts and rebuild their Y positions.       */
/* Called after font or width change.                                   */
/* ------------------------------------------------------------------ */

void ttl_layout_all_posts(TTLData *inst)
{
    TTLPost *post;

    /* Re-layout all posts (may change heights) */
    for (post = (TTLPost *)inst->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        ttl_post_layout(inst, post);
    }

    /* Recompute Y positions: head is newest, sits at contentTopY */
    ttl_rebuild_ypositions(inst);
}

/* ------------------------------------------------------------------ */
/* ttl_rebuild_ypositions                                               */
/*                                                                      */
/* Walk the post list from head (newest) to tail (oldest), assigning   */
/* consecutive timelineY values starting at inst->contentTopY.         */
/* Updates contentBottomY.                                              */
/* ------------------------------------------------------------------ */

void ttl_rebuild_ypositions(TTLData *inst)
{
    TTLPost *post;
    LONG     y = inst->contentTopY;

    for (post = (TTLPost *)inst->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        post->timelineY = y;
        y += post->height;
    }
    inst->contentBottomY = y;
}

/* ------------------------------------------------------------------ */
/* ttl_clear_posts                                                      */
/* ------------------------------------------------------------------ */

void ttl_clear_posts(TTLData *inst)
{
    struct Node *node;
    while ((node = RemHead((struct List *)&inst->posts)) != NULL)
        ttl_post_free((TTLPost *)node);
    inst->postCount      = 0;
    inst->contentTopY    = 0;
    inst->contentBottomY = 0;
    inst->scrollY        = 0;
    ttl_tiles_invalidate_all(inst);
}
