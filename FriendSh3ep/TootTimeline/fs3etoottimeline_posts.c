/*
 * TootTimeline – post list management.
 *
 * Post heights are computed from the body text wrapped through
 * fs3etextwrap.c (the same wrap engine tiles.c draws from -- see
 * ttl_post_layout), the current font metrics, and the gadget width.
 * Three draw contexts from inst->style provide per-role metrics:
 * dcNormal (body), dcUsername (display name), dcMini (acct, timestamp).
 * When no style is set yet a character-count heuristic is used as a
 * transient fallback (see ttl_count_wrapped_lines) -- TTIMELINE_Style
 * always forces a full relayout once it's actually set.
 *
 * Hot-spots (clickable rects: avatar/profile, @mention, #hashtag, URL,
 * media preview, Reply/Boost/Fave) are NOT computed here and are not
 * individually allocated -- see ttl_post_ensure_hotspots() below and the
 * TTLHotSpot comment in fs3etoottimeline_private.h.
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
#include "../fs3etextwrap.h"
#include "../bdbprintf.h"
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
 * Coarse fallback for when no draw context is available yet (style not
 * set) -- ttl_post_layout uses the pixel-exact fs3etextwrap path whenever
 * dcNormal exists, which it always does by the time anything is actually
 * rendered (see TTL_OnRender's style==NULL early return). Not static:
 * fs3etoottimeline_profile.c's layout uses the same fallback for a
 * profile header's bio. */
LONG ttl_count_wrapped_lines(TTLData *inst, const char *utf8, WORD maxW)
{
    const char *seg;
    LONG total = 0;

    if (!utf8 || !utf8[0] || maxW <= 0) return 0;

    seg = utf8;
    for (;;) {
        const char *nl = seg;
        LONG segLines;
        LONG segBytes;
        LONG avgGlyphW = inst->lineHeight > 0 ? inst->lineHeight / 2 : 7;
        LONG textW;

        while (*nl && *nl != '\n') nl++;
        segBytes = (LONG)(nl - seg);
        textW    = segBytes * avgGlyphW;
        segLines = segBytes > 0 ? (textW + maxW - 1) / maxW : 1;

        total += segLines < 1 ? 1 : segLines;

        if (*nl == '\0') break;
        seg = nl + 1;
    }

    return total < 1 ? 1 : total;
}

/* ------------------------------------------------------------------ */
/* TTLTextSpan helpers                                                  */
/* ------------------------------------------------------------------ */

/* Return the draw context appropriate for a given span type */
static struct URPDrawContext *ttl_dc_for_span(TTLData *inst, UBYTE spanType)
{
    if (!inst->style) return NULL;
    switch (spanType) {
        case TTL_SPAN_USERNAME:  return inst->style->dcUsername;
        case TTL_SPAN_ACCT:
        case TTL_SPAN_TIMESTAMP: return inst->style->dcMini;
        default:                 return inst->style->dcNormal;
    }
}

/* Return cached line height for a span type */
static WORD ttl_lineheight_for_span(TTLData *inst, UBYTE spanType)
{
    switch (spanType) {
        case TTL_SPAN_USERNAME:  return inst->nameLineHeight;
        case TTL_SPAN_ACCT:
        case TTL_SPAN_TIMESTAMP: return inst->miniLineHeight;
        default:                 return inst->lineHeight;
    }
}

static WORD ttl_lineascent_for_span(TTLData *inst, UBYTE spanType)
{
    switch (spanType) {
        case TTL_SPAN_USERNAME:  return inst->nameLineAscent;
        case TTL_SPAN_ACCT:
        case TTL_SPAN_TIMESTAMP: return inst->miniLineAscent;
        default:                 return inst->lineAscent;
    }
}

/* Not static: fs3etoottimeline_profile.c's layout builds the profile
 * header's name/acct/counts spans with this too. */
TTLTextSpan *ttl_span_alloc(const char *utf8, UBYTE spanType,
                            LONG postRelY, WORD x,
                            TTLData *inst)
{
    struct URPDrawContext *dc = ttl_dc_for_span(inst, spanType);
    TTLTextSpan *sp = (TTLTextSpan *)AllocVec(sizeof(TTLTextSpan),
                                               MEMF_ANY | MEMF_CLEAR);
    if (!sp) return NULL;

    sp->spanType = spanType;
    sp->postRelY = postRelY;
    sp->x        = x;
    sp->height   = ttl_lineheight_for_span(inst, spanType);
    sp->ascent   = ttl_lineascent_for_span(inst, spanType);
    sp->utf8     = dup_str(utf8);
    sp->byteLen  = utf8 ? (ULONG)strlen(utf8) : 0;

    if (dc && sp->utf8 && sp->byteLen > 0) {
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
                URPDC_HorizontalOffsetArrayUTF8(dc, sp->utf8,
                                                (LONG)cc, sp->charXOffsets);
            if (sp->charXOffsets && cc > 0)
                sp->width = (WORD)sp->charXOffsets[cc];
        }
    }

    return sp;
}

/* Build a TTL_SPAN_BODY span from an already-wrapped FS3ETextRow, reusing
 * its charXOffsets (a CopyMem, not a second FreeType measurement pass --
 * fs3etextwrap.c already paid for that) instead of calling ttl_span_alloc,
 * which would recompute them from scratch. Not static: fs3etoottimeline_profile.c's
 * layout builds a profile header's word-wrapped bio spans with this too. */
TTLTextSpan *ttl_span_from_row(const FS3ETextRow *row, WORD x,
                               LONG postRelY, TTLData *inst)
{
    TTLTextSpan *sp = (TTLTextSpan *)AllocVec(sizeof(TTLTextSpan),
                                               MEMF_ANY | MEMF_CLEAR);
    if (!sp) return NULL;

    sp->spanType  = TTL_SPAN_BODY;
    sp->postRelY  = postRelY;
    sp->x         = x;
    sp->height    = inst->lineHeight;
    sp->ascent    = inst->lineAscent;
    sp->byteLen   = row->byteLen;
    sp->charCount = row->charCount;
    sp->width     = row->width;
    sp->bodySrc   = row->start;  /* still points into post->body -- see the field comment */

    sp->utf8 = (char *)AllocVec(row->byteLen + 1, MEMF_ANY);
    if (sp->utf8) {
        if (row->byteLen) CopyMem((APTR)row->start, sp->utf8, row->byteLen);
        sp->utf8[row->byteLen] = '\0';
    }

    if (row->charCount > 0 && row->charXOffsets) {
        sp->charXOffsets = (LONG *)AllocVec((row->charCount + 1) * sizeof(LONG),
                                             MEMF_ANY);
        if (sp->charXOffsets)
            CopyMem(row->charXOffsets, sp->charXOffsets,
                    (row->charCount + 1) * sizeof(LONG));
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

/* Not static: fs3etoottimeline_profile.c's layout clears a profile
 * header's stale spans before rebuilding them too (its .layout can
 * re-run on a font/width change same as a toot's -- see
 * ttl_layout_all_posts). */
void ttl_clear_textspans(TTLPost *post)
{
    struct Node *node;
    while ((node = RemHead((struct List *)&post->textSpans)) != NULL)
        ttl_span_free((TTLTextSpan *)node);
}

/* ------------------------------------------------------------------ */
/* ttl_post_alloc                                                       */
/* ------------------------------------------------------------------ */

TTLPost *ttl_post_alloc(const TTLPostSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls            = &TTLToot_Class;
    post->dirty          = TRUE;
    post->hotSpotBucket  = -1;
    post->hotSpotsDirty  = TRUE;

    if (setup) {
        ULONG mi;
        post->username  = dup_str(setup->username);
        post->acct      = dup_str(setup->acct);
        post->body      = dup_str(setup->body);
        post->timestamp = dup_str(setup->timestamp);
        post->boostBy   = (setup->boostBy  && setup->boostBy[0])  ? dup_str(setup->boostBy)  : NULL;
        post->boostByAcct = (setup->boostByAcct && setup->boostByAcct[0]) ? dup_str(setup->boostByAcct) : NULL;
        post->avatarURL = (setup->avatarURL && setup->avatarURL[0]) ? dup_str(setup->avatarURL) : NULL;
        post->postId    = (setup->postId && setup->postId[0]) ? dup_str(setup->postId) : NULL;

        post->mediaCount = setup->mediaCount;
        if (post->mediaCount > TTL_POST_MAX_MEDIA) post->mediaCount = TTL_POST_MAX_MEDIA;
        for (mi = 0; mi < post->mediaCount; mi++) {
            post->mediaUrls[mi] = (setup->mediaUrls[mi] && setup->mediaUrls[mi][0])
                                 ? dup_str(setup->mediaUrls[mi]) : NULL;
            post->mediaKinds[mi] = setup->mediaKinds[mi];
        }

        post->repliesCount    = setup->repliesCount;
        post->reblogsCount    = setup->reblogsCount;
        post->favouritesCount = setup->favouritesCount;
        post->favourited      = setup->favourited;
        post->reblogged       = setup->reblogged;

        post->pollOptionCount = setup->pollOptionCount;
        if (post->pollOptionCount > TTL_POST_MAX_POLL_OPTIONS) post->pollOptionCount = TTL_POST_MAX_POLL_OPTIONS;
        for (mi = 0; mi < post->pollOptionCount; mi++) {
            post->pollOptionTitles[mi] = (setup->pollOptionTitles[mi] && setup->pollOptionTitles[mi][0])
                                        ? dup_str(setup->pollOptionTitles[mi]) : NULL;
            post->pollOptionVotes[mi] = setup->pollOptionVotes[mi];
        }
        post->pollVotesCount = setup->pollVotesCount;
        post->pollExpired    = setup->pollExpired;
        post->pollMultiple   = setup->pollMultiple;
    }

    return post;
}

/* ------------------------------------------------------------------ */
/* ttl_pseudo_post_alloc                                                */
/*                                                                      */
/* A TTLPost-shaped node for a non-toot pinned row (see                */
/* TTLLoadNewer_Class/TTLLoadOlder_Class) -- reuses the exact same node */
/* shape, list/tile/hit-testing machinery as a toot, just via a         */
/* different class and none of TTLPostSetup's fields. `label` is a      */
/* borrowed static string literal, not copied: these classes' .dispose  */
/* is NULL (nothing to free), so it must outlive the post, which a      */
/* literal owned by the class definition itself always does.           */
/* ------------------------------------------------------------------ */

TTLPost *ttl_pseudo_post_alloc(const TTLItemClass *cls, const char *label)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls            = cls;
    post->dirty          = TRUE;
    post->hotSpotBucket  = -1;
    post->hotSpotsDirty  = TRUE;
    post->body           = (char *)label;

    return post;
}

/* ------------------------------------------------------------------ */
/* ttl_post_free                                                        */
/* ------------------------------------------------------------------ */

/* Toot-specific teardown: TTLItemClass.dispose for TTLToot_Class. Frees
 * everything ttl_post_alloc's setup-driven copies own beyond the bare
 * node/textSpans, which ttl_post_free (the generic caller) already
 * handles for every item kind alike. */
static void ttl_toot_dispose(TTLPost *post)
{
    if (post->username)  FreeVec(post->username);
    if (post->acct)      FreeVec(post->acct);
    if (post->body)      FreeVec(post->body);
    if (post->timestamp) FreeVec(post->timestamp);
    if (post->boostBy)   FreeVec(post->boostBy);
    if (post->boostByAcct) FreeVec(post->boostByAcct);
    if (post->avatarURL) FreeVec(post->avatarURL);
    if (post->postId)    FreeVec(post->postId);
    {
        ULONG mi;
        for (mi = 0; mi < post->mediaCount; mi++)
            if (post->mediaUrls[mi]) FreeVec(post->mediaUrls[mi]);
        for (mi = 0; mi < post->pollOptionCount; mi++)
            if (post->pollOptionTitles[mi]) FreeVec(post->pollOptionTitles[mi]);
    }
}

void ttl_post_free(TTLData *inst, TTLPost *post)
{
    if (!post) return;

    /* A pool bucket outlives the post's memory (AllocVec can hand the
     * same address to the next TTLPost), so drop the owner reference now
     * or a future ttl_post_ensure_hotspots on an unrelated post could
     * mistake it for "already owns this bucket, still fresh". */
    if (post->hotSpotBucket >= 0 &&
        inst->hotSpotBucketOwner[post->hotSpotBucket] == post)
        inst->hotSpotBucketOwner[post->hotSpotBucket] = NULL;

    ttl_clear_textspans(post);
    if (post->cls && post->cls->dispose)
        post->cls->dispose(post);
    FreeVec(post);
}

/* ------------------------------------------------------------------ */
/* ttl_toot_layout -- TTLItemClass.layout for TTLToot_Class             */
/*                                                                      */
/* Compute post->height and rebuild the textSpans list.                 */
/* Called whenever the gadget width or font changes.                    */
/* ------------------------------------------------------------------ */

static void ttl_toot_layout(TTLData *inst, TTLPost *post)
{
    WORD  avatarW, padLeft, avatarGap, textX, textW;
    LONG  curRelY;
    LONG  avatarH;
    WORD  previewW = 0, previewH = 0;
    BOOL  sideBySide = FALSE;

    ttl_clear_textspans(post);

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
    if (textW < 32) textW = 32;

    avatarH = avatarW;

    /* Media preview rectangle sizing: same scale factor as the avatar
     * (both derive from lineH via FS3EStyle's compute_layout -- see
     * TTL_AVATAR_BASE_SIZE). Side-by-side needs the gadget wide enough
     * both by the caller's own >400px rule and to leave a usable text
     * column; otherwise it stacks below the text. */
    post->previewX = post->previewY = post->previewW = post->previewH = 0;
    if (post->mediaCount > 0 && post->pollOptionCount == 0) {
        previewW = (WORD)(((LONG)TTL_PREVIEW_BASE_W * avatarW) / TTL_AVATAR_BASE_SIZE);
        previewH = (WORD)(((LONG)TTL_PREVIEW_BASE_H * avatarW) / TTL_AVATAR_BASE_SIZE);
        if (inst->gadWidth > 400 && (textW - previewW - avatarGap) >= 32)
            sideBySide = TRUE;
        if (!sideBySide && previewW > textW)
            previewW = textW;
    }

    curRelY = TTL_POST_PAD_TOP;

    /* "↺ Name boosted" line (dcMini) — only for reblogs */
    if (post->boostBy && post->boostBy[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->boostBy, TTL_SPAN_BOOSTBY,
                                          curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
        curRelY += inst->miniLineHeight;
    }

    /* Username span (dcUsername metrics) */
    if (post->username && post->username[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->username, TTL_SPAN_USERNAME,
                                          curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->nameLineHeight;

    /* Acct span (dcMini metrics) -- also the "@handle" profile link target;
     * see ttl_post_build_hotspots. */
    if (post->acct && post->acct[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->acct, TTL_SPAN_ACCT,
                                          curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->miniLineHeight;

    /* Make sure curRelY is at least below the avatar */
    {
        LONG minY = (LONG)TTL_POST_PAD_TOP + avatarH;
        if (curRelY < minY) curRelY = minY;
    }

    /* ---- Body text: word-wrap via fs3etextwrap so layout (this
     * function) and drawing (ttl_render_tile) always agree pixel-for-
     * pixel -- one TTL_SPAN_BODY span per visual row. ---- */
    {
        LONG bodyTopY  = curRelY;
        WORD bodyTextW = sideBySide ? (WORD)(textW - previewW - avatarGap) : textW;
        struct URPDrawContext *dcBody = inst->style ? inst->style->dcNormal : NULL;

        if (post->body && post->body[0] && dcBody) {
            FS3ETextWrap tw;
            if (FS3ETextWrap_Build(&tw, dcBody, inst->lineHeight, post->body, bodyTextW)) {
                ULONG i;
                for (i = 0; i < tw.rowCount; i++) {
                    TTLTextSpan *sp = ttl_span_from_row(&tw.rows[i], textX, curRelY, inst);
                    if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
                    curRelY += inst->lineHeight;
                }
                FS3ETextWrap_Free(&tw);
            }
        } else if (post->body && post->body[0]) {
            /* No draw context yet -- transient, see file header comment. */
            curRelY += ttl_count_wrapped_lines(inst, post->body, bodyTextW) * inst->lineHeight;
        }

        if (post->mediaCount > 0 && post->pollOptionCount == 0 && previewW > 0 && previewH > 0) {
            if (sideBySide) {
                post->previewX = (WORD)(textX + bodyTextW + avatarGap);
                post->previewY = (WORD)bodyTopY;
                post->previewW = previewW;
                post->previewH = previewH;
                if (bodyTopY + previewH > curRelY) curRelY = bodyTopY + previewH;
            } else {
                curRelY += avatarGap;
                post->previewX = textX;
                post->previewY = (WORD)curRelY;
                post->previewW = previewW;
                post->previewH = previewH;
                curRelY += previewH;
            }
        }
    }

    /* ---- Poll ("survey") results block -- closed/result rendering only.
     * Skipped entirely when there's no poll; when there is, the media
     * preview block above was already suppressed (Mastodon disallows
     * both on one status anyway). Each option reserves one text row
     * (title + percentage) plus a thin proportional bar underneath;
     * pollBlockY is stored here and reused as-is by render/
     * build_hotspots -- never re-derived, same fix as the profile
     * Follow-button-row bug. ---- */
    post->pollBlockY = 0;
    if (post->pollOptionCount > 0) {
        ULONG oi;
        curRelY += avatarGap;
        post->pollBlockY = (WORD)curRelY;
        for (oi = 0; oi < post->pollOptionCount; oi++) {
            curRelY += inst->miniLineHeight;   /* title + percentage text */
            curRelY += TTL_POLL_BAR_H;         /* proportional result bar */
            curRelY += TTL_POLL_ROW_GAP;       /* gap before next option */
        }
        curRelY += inst->miniLineHeight;       /* "N votes - Poll closed" summary line */
    }

    /* ---- Action bar: ↩ Reply N  🔁 Boost N  ⭐/💫 N ----
     * Only the row's height is needed for layout; exact button rects are
     * (re)computed lazily in ttl_post_build_hotspots from
     * ttl_build_action_labels(), the same builder tiles.c draws from, so
     * the two can never drift apart the way two separately-hand-maintained
     * copies would. */
    curRelY += 2;                              /* gap above the action bar */
    curRelY += inst->lineHeight + TTL_POST_PAD_BOT;

    curRelY += 1;  /* separator pixel */
    post->height = curRelY;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* Hot-spots: pool-based, built lazily for posts actually being drawn.  */
/* See the TTLHotSpot comment in fs3etoottimeline_private.h.            */
/* ------------------------------------------------------------------ */

extern const UBYTE        ttl_actionTypes[3];

/* data/dataLen are stored as given -- a borrowed pointer into text the
 * caller already owns (post->acct/boostBy, or a TTLTextSpan's utf8), not
 * copied. See the TTLHotSpot comment in fs3etoottimeline_private.h for
 * why that's safe. Not static: fs3etoottimeline_profile.c's buildHotspots
 * uses this for a profile header's avatar/Follow hot-spots too. */
void ttl_hs_add(TTLPost *post, UBYTE type, WORD x, WORD y, WORD w, WORD h,
                const char *data, ULONG dataLen)
{
    TTLHotSpot *hs;
    if (post->hotSpotCount >= TTL_HOTSPOT_MAX_PER_TOOT) return;
    hs = &post->hotSpots[post->hotSpotCount++];
    hs->type    = type;
    hs->x = x; hs->y = y; hs->w = w; hs->h = h;
    hs->data    = (data && dataLen > 0) ? data : NULL;
    hs->dataLen = hs->data ? dataLen : 0;
}

static BOOL ttl_bytes_match(const unsigned char *p, const unsigned char *end,
                             const char *lit)
{
    ULONG n = (ULONG)strlen(lit);
    if ((ULONG)(end - p) < n) return FALSE;
    return (BOOL)(memcmp(p, lit, n) == 0);
}

/* Scan one already-wrapped body row for @mention / #hashtag / http(s)://
 * URL tokens, adding a hot-spot for each. Token pixel rects come straight
 * from sp->charXOffsets, so they stay exact with what tiles.c draws --
 * that rect can only ever cover what's visible on this one row, even if
 * word-wrap cut a long URL mid-word into this row and the next. The click
 * *text* shouldn't be cut short by that, though: hs->data/dataLen are
 * re-measured from sp->bodySrc (the persistent, unwrapped post->body) by
 * just counting up to the next real separator, ignoring wherever this row
 * happened to end. Not static: fs3etoottimeline_profile.c's buildHotspots
 * calls this on a profile header's wrapped bio spans too -- fully generic
 * over any TTL_SPAN_BODY span, no toot-specific assumptions. */
void ttl_scan_span_tokens(TTLPost *post, TTLTextSpan *sp)
{
    const unsigned char *p    = (const unsigned char *)sp->utf8;
    const unsigned char *end  = p + sp->byteLen;
    ULONG charIdx = 0;

    if (!sp->charXOffsets || !sp->bodySrc) return;

    while (p < end && post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT) {
        UBYTE hotType = 0xFF;
        ULONG startChar = charIdx;
        const unsigned char *tokStart = p;

        if (*p == '@') hotType = TTL_HOT_MENTION;
        else if (*p == '#') hotType = TTL_HOT_HASHTAG;
        else if (ttl_bytes_match(p, end, "https://") || ttl_bytes_match(p, end, "http://"))
            hotType = TTL_HOT_URL;

        if (hotType != 0xFF) {
            while (p < end && (unsigned char)*p > 0x20) {
                unsigned char c = *p;
                p += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                charIdx++;
            }
            /* Require something beyond the bare symbol/scheme */
            if (charIdx > startChar + 1) {
                const char *dataStart = sp->bodySrc + (tokStart - (const unsigned char *)sp->utf8);
                const unsigned char *q = (const unsigned char *)dataStart;

                while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;

                ttl_hs_add(post, hotType,
                           (WORD)(sp->x + sp->charXOffsets[startChar]),
                           (WORD)sp->postRelY,
                           (WORD)(sp->charXOffsets[charIdx] - sp->charXOffsets[startChar]),
                           sp->height,
                           dataStart, (ULONG)(q - (const unsigned char *)dataStart));
            }
            continue;
        }

        {
            unsigned char c = *p;
            p += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            charIdx++;
        }
    }
}

/* (Re)build post->hotSpots[0..hotSpotCount) in place -- TTLItemClass.
 * buildHotspots for TTLToot_Class. Caller (ttl_post_ensure_hotspots) has
 * already pointed post->hotSpots at a pool bucket. */
static void ttl_toot_build_hotspots(TTLData *inst, TTLPost *post)
{
    TTLTextSpan *sp;
    WORD avatarW, padLeft;

    post->hotSpotCount = 0;

    if (inst->style && inst->style->avatarSize > 0) {
        avatarW = inst->style->avatarSize;
        padLeft = inst->style->postPadLeft;
    } else {
        avatarW = 35;
        padLeft = 6;
    }

    /* Avatar icon -> profile */
    ttl_hs_add(post, TTL_HOT_AVATAR, padLeft, TTL_POST_PAD_TOP, avatarW, avatarW,
               post->acct, post->acct ? (ULONG)strlen(post->acct) : 0);

    for (sp = (TTLTextSpan *)post->textSpans.mlh_Head;
         sp->node.mln_Succ && post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT;
         sp = (TTLTextSpan *)sp->node.mln_Succ)
    {
        if (sp->spanType == TTL_SPAN_ACCT || sp->spanType == TTL_SPAN_USERNAME) {
            /* "@handle" line and the display-name line both click ==
             * avatar click: same profile target. */
            ttl_hs_add(post, TTL_HOT_AVATAR, sp->x, (WORD)sp->postRelY,
                       sp->width, sp->height,
                       post->acct, post->acct ? (ULONG)strlen(post->acct) : 0);
        } else if (sp->spanType == TTL_SPAN_BOOSTBY) {
            /* "↺ Name boosted" line click -> that booster's OWN profile,
             * not the original author's -- important because some
             * accounts only ever boost, never post themselves, and this
             * line is otherwise the only way to reach (and e.g. unfollow)
             * them. Needs their acct, not just their display name (which
             * isn't a valid /api/v1/accounts/lookup query) -- see
             * TTLPost.boostByAcct. No hot-spot at all if the network
             * layer didn't have it (older cached data, or a server that
             * omits it), rather than a click that silently resolves to
             * the wrong account. */
            if (post->boostByAcct && post->boostByAcct[0])
                ttl_hs_add(post, TTL_HOT_AVATAR, sp->x, (WORD)sp->postRelY,
                           sp->width, sp->height,
                           post->boostByAcct, (ULONG)strlen(post->boostByAcct));
        } else if (sp->spanType == TTL_SPAN_BODY) {
            ttl_scan_span_tokens(post, sp);
        }
    }

    if (post->mediaCount > 0 && post->previewW > 0) {
        const char *curUrl = (post->mediaCurrentIndex < post->mediaCount)
                            ? post->mediaUrls[post->mediaCurrentIndex] : NULL;
        ULONG curKind = (post->mediaCurrentIndex < post->mediaCount)
                       ? post->mediaKinds[post->mediaCurrentIndex] : TTL_MEDIA_KIND_UNKNOWN;
        UBYTE hotType = (curKind == TTL_MEDIA_KIND_AUDIO) ? TTL_HOT_PLAY_AUDIO : TTL_HOT_IMAGE;

        ttl_hs_add(post, hotType, post->previewX, post->previewY,
                   post->previewW, post->previewH,
                   curUrl, curUrl ? (ULONG)strlen(curUrl) : 0);

        if (post->mediaCount > 1) {
            WORD arrowW = ttl_media_arrow_width(post->previewW);
            ttl_hs_add(post, TTL_HOT_MEDIA_PREV, post->previewX, post->previewY,
                       arrowW, post->previewH, NULL, 0);
            ttl_hs_add(post, TTL_HOT_MEDIA_NEXT,
                       (WORD)(post->previewX + post->previewW - arrowW), post->previewY,
                       arrowW, post->previewH, NULL, 0);
        }
    }

    /* Action bar buttons: same geometry rule ttl_post_layout used to
     * reserve the row (post->height - separator - PAD_BOT - barH), same
     * labels tiles.c draws -- both build from ttl_build_action_labels(),
     * see that function's comment. */
    if (post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT) {
        struct URPDrawContext *dcA = inst->style ? inst->style->dcNormal : NULL;
        WORD barH   = inst->lineHeight;
        WORD y      = (WORD)(post->height - 1 - TTL_POST_PAD_BOT - barH);
        WORD xRight = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT);
        char labels[3][TTL_ACTION_LABEL_MAX];
        int  a;

        ttl_build_action_labels(post, labels);

        for (a = 2; a >= 0; a--) {
            WORD w = 40;
            if (dcA) {
                struct URPTextMetric m;
                LONG nc = 0;
                const char *s = labels[a];
                const unsigned char *q = (const unsigned char *)s;
                while (*q) {
                    unsigned char c = *q;
                    q += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                    nc++;
                }
                URPDC_TextSizeUTF8(dcA, s, nc, &m);
                w = (WORD)(m.width > 0 ? m.width : 40);
            }
            ttl_hs_add(post, ttl_actionTypes[a], (WORD)(xRight - w), y, w, barH, NULL, 0);
            xRight = (WORD)(xRight - w - TTL_ACTION_GAP);
        }
    }
}

void ttl_post_ensure_hotspots(TTLData *inst, TTLPost *post)
{
    BOOL haveBucket = (post->hotSpotBucket >= 0 &&
                        inst->hotSpotBucketOwner[post->hotSpotBucket] == post);

    if (haveBucket && !post->hotSpotsDirty) return;

    if (!haveBucket) {
        WORD bucket = (WORD)inst->hotSpotNextBucket;
        TTLPost *prevOwner;

        inst->hotSpotNextBucket = (inst->hotSpotNextBucket + 1) % TTL_HOTSPOT_POOL_TOOTS;

        prevOwner = inst->hotSpotBucketOwner[bucket];
        if (prevOwner) {
            prevOwner->hotSpots      = NULL;
            prevOwner->hotSpotCount  = 0;
            prevOwner->hotSpotBucket = -1;
            prevOwner->hotSpotsDirty = TRUE;
        }

        inst->hotSpotBucketOwner[bucket] = post;
        post->hotSpotBucket = bucket;
        post->hotSpots       = inst->hotSpotPool[bucket];
    }

    if (post->cls && post->cls->buildHotspots)
        post->cls->buildHotspots(inst, post);
    post->hotSpotsDirty = FALSE;
}

/* ------------------------------------------------------------------ */
/* ttl_layout_all_posts                                                 */
/*                                                                      */
/* Recompute heights for all posts and rebuild their Y positions, in    */
/* every channel -- not just the active one, so any channel is ready   */
/* to display correctly the moment it becomes active via                */
/* TTIMELINE_ViewMode, without needing per-channel dirty tracking.      */
/* Called after font or width change.                                   */
/* ------------------------------------------------------------------ */

void ttl_layout_all_posts(TTLData *inst)
{
    ULONG ch;
    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++) {
        TTLPost    *post;
        TTLChannel *channel = &inst->channels[ch];
        struct MinList *posts = &channel->posts;

        /* Profile header (see TTLChannel.headerPost) is outside `posts`,
         * so the loop below never reaches it -- relayout it first (its
         * height can change with the new font/width same as any post's
         * can) and resync contentTopY to match, since that's what
         * ttl_rebuild_ypositions below uses as the list's own start Y.
         * Guarded/no-op for every channel without a header. */
        if (channel->headerPost) {
            if (channel->headerPost->cls && channel->headerPost->cls->layout)
                channel->headerPost->cls->layout(inst, channel->headerPost);
            channel->contentTopY = channel->headerPost->height;
        }

        /* Re-layout all posts (may change heights) */
        for (post = (TTLPost *)posts->mlh_Head;
             post->node.mln_Succ;
             post = (TTLPost *)post->node.mln_Succ)
        {
            if (post->cls && post->cls->layout)
                post->cls->layout(inst, post);
        }

        /* Recompute Y positions: head is newest, sits at contentTopY */
        ttl_rebuild_ypositions(inst, ch);
    }
}

/* ------------------------------------------------------------------ */
/* ttl_rebuild_ypositions                                               */
/*                                                                      */
/* Walk channel ch's post list from head (newest) to tail (oldest),    */
/* assigning consecutive timelineY values starting at its contentTopY. */
/* Updates that channel's contentBottomY.                               */
/* ------------------------------------------------------------------ */

void ttl_rebuild_ypositions(TTLData *inst, ULONG ch)
{
    TTLChannel *channel = &inst->channels[ch];
    TTLPost    *post;
    LONG        y = channel->contentTopY;

    for (post = (TTLPost *)channel->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        post->timelineY = y;
        y += post->height;
    }
    channel->contentBottomY = y;
}

/* ------------------------------------------------------------------ */
/* ttl_clear_channel / ttl_clear_posts                                  */
/* ------------------------------------------------------------------ */

void ttl_clear_channel(TTLData *inst, ULONG ch)
{
    TTLChannel  *channel = &inst->channels[ch];
    struct Node *node;
    ULONG freedCount = 0;
    while ((node = RemHead((struct List *)&channel->posts)) != NULL) {
        ttl_post_free(inst, (TTLPost *)node);
        freedCount++;
    }
    bdbprintf_now("ttl_clear_channel: ch=%lu freed %lu list posts\n", ch, freedCount);
    /* Profile header (see TTLChannel.headerPost) is outside `posts`, so
     * the loop above never frees it -- guarded/no-op for every channel
     * without one. */
    if (channel->headerPost) {
        bdbprintf_now("ttl_clear_channel: ch=%lu freeing headerPost=%08lx bucket=%d\n",
                  ch, (unsigned long)channel->headerPost, (int)channel->headerPost->hotSpotBucket);
        ttl_post_free(inst, channel->headerPost);
        channel->headerPost = NULL;
    }
    channel->postCount         = 0;
    channel->contentTopY       = 0;
    channel->contentBottomY    = 0;
    channel->scrollY           = 0;
    channel->olderLoadTriggered = FALSE;
    inst->layoutToDo = TRUE;
    ttl_tiles_invalidate_all(inst);
}

/* Clear only the currently displayed channel (see TTIMELINE_ClearPosts). */
void ttl_clear_posts(TTLData *inst)
{
    ttl_clear_channel(inst, inst->viewMode);
}

/* ------------------------------------------------------------------ */
/* TTLToot_Class -- the original (and still only) TTLItemClass, kept    */
/* here since this file already owns toot construction/teardown         */
/* (ttl_post_alloc/ttl_post_free). .render and .activate are defined    */
/* in fs3etoottimeline_tiles.c / fs3etoottimeline_input.c respectively  */
/* -- see the extern prototypes in fs3etoottimeline_private.h.          */
/* ------------------------------------------------------------------ */

const TTLItemClass TTLToot_Class = {
    ttl_toot_layout,
    ttl_toot_render,
    ttl_toot_build_hotspots,
    ttl_toot_activate,
    ttl_toot_dispose
};

/* ------------------------------------------------------------------ */
/* Boundary-aware insertion (TTIMELINE_AddPost/AppendPost)              */
/*                                                                      */
/* Both assume the channel's post list is already non-empty -- the      */
/* very-first-post bootstrap (contentTopY/BottomY = 0, plain AddHead)   */
/* is handled by the caller before the list has anything in it to check */
/* the head/tail class of; see ttl_channel_add_boundaries below, called */
/* right after that bootstrap so the two pinned rows are only ever      */
/* inserted once real content already exists.                          */
/* ------------------------------------------------------------------ */

void ttl_channel_insert_top(TTLData *inst, TTLChannel *channel, TTLPost *post)
{
    TTLPost *head = (TTLPost *)channel->posts.mlh_Head;

    if (head->cls == &TTLLoadNewer_Class) {
        /* Real content currently starts right where the pinned "load
         * newer" row ends; the new (newest) post takes that spot, and the
         * boundary row shifts up by the same amount to stay glued to the
         * top of real content -- everything else's timelineY is
         * untouched, same as the no-boundary case below. */
        LONG oldHeadY = head->timelineY;
        post->timelineY = head->timelineY + head->height - post->height;
        head->timelineY -= post->height;
        channel->contentTopY -= post->height;
        Insert((struct List *)&channel->posts,
               (struct Node *)&post->node, (struct Node *)&head->node);
        /* The boundary's own tile(s) must be redrawn at both its old and
         * new position -- ttl_tiles_invalidate_range for the new post
         * itself is still the caller's job (see TTIMELINE_AddPost), same
         * as before this row existed. */
        if (channel == ttl_active(inst))
            ttl_tiles_invalidate_range(inst, head->timelineY, oldHeadY + head->height);
    } else {
        channel->contentTopY -= post->height;
        post->timelineY        = channel->contentTopY;
        AddHead((struct List *)&channel->posts, (struct Node *)&post->node);
    }
}

void ttl_channel_insert_bottom(TTLData *inst, TTLChannel *channel, TTLPost *post)
{
    TTLPost *tail = (TTLPost *)channel->posts.mlh_TailPred;

    if (tail->cls == &TTLLoadOlder_Class) {
        /* Mirror of ttl_channel_insert_top: real content currently ends
         * right where the pinned "load older" row begins; the new
         * (oldest) post takes that spot, and the boundary row shifts down
         * by the same amount to stay glued below real content. */
        TTLPost *head = (TTLPost *)channel->posts.mlh_Head;
        LONG oldTailY = tail->timelineY;
        post->timelineY           = tail->timelineY;
        tail->timelineY          += post->height;
        channel->contentBottomY  += post->height;
        if (tail == head)
            AddHead((struct List *)&channel->posts, (struct Node *)&post->node);
        else
            Insert((struct List *)&channel->posts, (struct Node *)&post->node,
                   (struct Node *)tail->node.mln_Pred);
        if (channel == ttl_active(inst))
            ttl_tiles_invalidate_range(inst, oldTailY, tail->timelineY + tail->height);
    } else {
        post->timelineY          = channel->contentBottomY;
        channel->contentBottomY += post->height;
        AddTail((struct List *)&channel->posts, (struct Node *)&post->node);
    }

    /* New content actually landed at the bottom -- let the proximity
     * trigger in TTL_OnRender fire again once the user scrolls back down
     * to (the new) bottom. */
    channel->olderLoadTriggered = FALSE;
}

/* ------------------------------------------------------------------ */
/* TTLLoadNewer_Class / TTLLoadOlder_Class -- pinned boundary rows      */
/*                                                                      */
/* Both share ttl_boundary_render (fs3etoottimeline_tiles.c) and a      */
/* fixed one-line height; only TTLLoadNewer_Class has a hot-spot (the   */
/* "load older" row is proximity-triggered from TTL_OnRender instead -- */
/* see the TTLItemClass comment in fs3etoottimeline_private.h).         */
/* ------------------------------------------------------------------ */

static void ttl_boundary_layout(TTLData *inst, TTLPost *post)
{
    WORD lineH = inst->lineHeight > 0 ? inst->lineHeight : 14;
    post->height = lineH + 2 * TTL_POST_PAD_TOP;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

static void ttl_load_newer_build_hotspots(TTLData *inst, TTLPost *post)
{
    post->hotSpotCount = 0;
    if (post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT) {
        TTLHotSpot *hs = &post->hotSpots[post->hotSpotCount++];
        hs->type    = TTL_HOT_LOAD_NEWER;
        hs->x = 0; hs->y = 0;
        hs->w = (WORD)inst->gadWidth;
        hs->h = (WORD)post->height;
        hs->data = NULL; hs->dataLen = 0;
    }
}

const TTLItemClass TTLLoadNewer_Class = {
    ttl_boundary_layout,
    ttl_boundary_render,
    ttl_load_newer_build_hotspots,
    NULL,  /* activate: the generic ttl_notify_hotspot(TTL_HOT_LOAD_NEWER) already fired */
    NULL   /* dispose: post->body is a borrowed static literal, nothing to free */
};

const TTLItemClass TTLLoadOlder_Class = {
    ttl_boundary_layout,
    ttl_boundary_render,
    NULL,  /* buildHotspots: no click target -- see TTL_OnRender's proximity check */
    NULL,
    NULL
};

/* ------------------------------------------------------------------ */
/* ttl_channel_add_boundaries                                           */
/*                                                                      */
/* Pin a "look for something new" row above and a "load more…" row      */
/* below a channel's real content, right after its first-ever real post */
/* has already been added (so both land via the same non-empty-list     */
/* insertion path every later post/append uses -- see                  */
/* ttl_channel_insert_top/bottom above). Safe to call only once per     */
/* channel lifetime (see the TTIMELINE_AddPost/AppendPost postCount==0  */
/* bootstrap branch); allocation failure just leaves that side unpinned */
/* rather than failing the whole post insertion.                        */
/* ------------------------------------------------------------------ */

void ttl_channel_add_boundaries(TTLData *inst, TTLChannel *channel)
{
    TTLPost *top = ttl_pseudo_post_alloc(&TTLLoadNewer_Class, "Look for something new");
    TTLPost *bot = ttl_pseudo_post_alloc(&TTLLoadOlder_Class, "Load more\xE2\x80\xA6" /* "Load more…" */);

    if (top) {
        top->cls->layout(inst, top);
        ttl_channel_insert_top(inst, channel, top);
    }
    if (bot) {
        bot->cls->layout(inst, bot);
        ttl_channel_insert_bottom(inst, channel, bot);
    }
}
