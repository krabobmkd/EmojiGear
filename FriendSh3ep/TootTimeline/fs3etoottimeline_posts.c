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

/* Like dup_str, but for a non-NUL-terminated byte range -- used to copy an
 * FS3ETextRow's substring (row->start/byteLen, which points into the
 * original text FS3ETextWrap_Build was given) into its own small owned
 * buffer before FS3ETextWrap_Free() tears down the row array. */
static char *dup_strn(const char *s, ULONG len)
{
    char *copy;
    if (!s) return NULL;
    copy = (char *)AllocVec(len + 1, MEMF_ANY);
    if (copy) {
        CopyMem((APTR)s, copy, len);
        copy[len] = '\0';
    }
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
        post->targetId  = (setup->targetId && setup->targetId[0]) ? dup_str(setup->targetId) : NULL;

        post->mediaCount = setup->mediaCount;
        if (post->mediaCount > TTL_POST_MAX_MEDIA) post->mediaCount = TTL_POST_MAX_MEDIA;
        for (mi = 0; mi < post->mediaCount; mi++) {
            post->mediaUrls[mi] = (setup->mediaUrls[mi] && setup->mediaUrls[mi][0])
                                 ? dup_str(setup->mediaUrls[mi]) : NULL;
            post->mediaAudioUrls[mi] = (setup->mediaAudioUrls[mi] && setup->mediaAudioUrls[mi][0])
                                      ? dup_str(setup->mediaAudioUrls[mi]) : NULL;
            post->mediaKinds[mi] = setup->mediaKinds[mi];
        }

        /* Comma-join attachment ids for TTL_HOT_MODIFY -- see
         * TTLPost.mediaIdsJoined. Kept separate from post->mediaUrls
         * above since nothing here needs per-item id access, only the
         * whole joined string travelling out through a hot-spot notify. */
        post->mediaIdsJoined = NULL;
        {
            ULONG totalLen = 0, n, joinedCount = 0;
            for (mi = 0; mi < post->mediaCount; mi++) {
                if (setup->mediaIds[mi] && setup->mediaIds[mi][0]) {
                    totalLen += (ULONG)strlen(setup->mediaIds[mi]);
                    joinedCount++;
                }
            }
            if (joinedCount > 0) {
                totalLen += joinedCount - 1; /* one ',' between each pair */
                post->mediaIdsJoined = AllocVec(totalLen + 1, MEMF_ANY);
                if (post->mediaIdsJoined) {
                    char *dst = post->mediaIdsJoined;
                    BOOL  first = TRUE;
                    for (mi = 0; mi < post->mediaCount; mi++) {
                        if (setup->mediaIds[mi] && setup->mediaIds[mi][0]) {
                            if (!first) *dst++ = ',';
                            n = (ULONG)strlen(setup->mediaIds[mi]);
                            CopyMem((APTR)setup->mediaIds[mi], dst, n);
                            dst += n;
                            first = FALSE;
                        }
                    }
                    *dst = '\0';
                }
            }
        }

        post->repliesCount    = setup->repliesCount;
        post->reblogsCount    = setup->reblogsCount;
        post->favouritesCount = setup->favouritesCount;
        post->favourited      = setup->favourited;
        post->reblogged       = setup->reblogged;
        post->quotable        = setup->quotable;
    post->isReply         = setup->isReply;
        post->isOwn           = setup->isOwn;
        post->isThreadReply   = setup->isThreadReply;
        post->sensitive       = setup->sensitive;
        /* contentRevealed left FALSE (MEMF_CLEAR) -- fresh post, not
         * revealed yet even if sensitive. */

        post->notifType       = setup->notifType;
        post->notifActorName  = (setup->notifActorName && setup->notifActorName[0]) ? dup_str(setup->notifActorName) : NULL;
        post->notifActorAcct  = (setup->notifActorAcct && setup->notifActorAcct[0]) ? dup_str(setup->notifActorAcct) : NULL;
        post->notifStatusId   = (setup->notifStatusId && setup->notifStatusId[0]) ? dup_str(setup->notifStatusId) : NULL;

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

        post->hasCard = setup->hasCard;
        post->cardUrl          = (setup->cardUrl          && setup->cardUrl[0])          ? dup_str(setup->cardUrl)          : NULL;
        post->cardTitle        = (setup->cardTitle        && setup->cardTitle[0])        ? dup_str(setup->cardTitle)        : NULL;
        post->cardDescription  = (setup->cardDescription  && setup->cardDescription[0])  ? dup_str(setup->cardDescription)  : NULL;
        post->cardProviderName = (setup->cardProviderName && setup->cardProviderName[0]) ? dup_str(setup->cardProviderName) : NULL;
        post->cardImageUrl     = (setup->cardImageUrl     && setup->cardImageUrl[0])     ? dup_str(setup->cardImageUrl)     : NULL;
        /* cardTitleLines/cardDescLines are computed by ttl_toot_layout
         * (they depend on cardW, a layout-time value), not here. */

        post->hasQuote = setup->hasQuote;
        post->quoteId         = (setup->quoteId         && setup->quoteId[0])         ? dup_str(setup->quoteId)         : NULL;
        post->quoteAuthorName = (setup->quoteAuthorName && setup->quoteAuthorName[0]) ? dup_str(setup->quoteAuthorName) : NULL;
        post->quoteAuthorAcct = (setup->quoteAuthorAcct && setup->quoteAuthorAcct[0]) ? dup_str(setup->quoteAuthorAcct) : NULL;
        post->quoteAvatarURL  = (setup->quoteAvatarURL  && setup->quoteAvatarURL[0])  ? dup_str(setup->quoteAvatarURL)  : NULL;
        post->quoteBody       = (setup->quoteBody       && setup->quoteBody[0])       ? dup_str(setup->quoteBody)       : NULL;
        post->quoteTimestamp  = (setup->quoteTimestamp  && setup->quoteTimestamp[0])  ? dup_str(setup->quoteTimestamp)  : NULL;
        /* quoteBodyLines are computed by ttl_toot_layout (depend on
         * quoteW, a layout-time value), not here. */
    }

    return post;
}

/* ------------------------------------------------------------------ */
/* ttl_post_refresh_fields                                              */
/*                                                                      */
/* F5 "refresh visible toots" (TTIMELINE_RefreshPost): patches an        */
/* EXISTING post's status-derived fields from a freshly re-fetched       */
/* setup, in place -- unlike ttl_post_alloc, this never allocates a new  */
/* TTLPost, and it deliberately does NOT touch:                          */
/*   - postId (it's the match key the caller found this post by)        */
/*   - notifType/notifActorName/notifActorAcct/notifStatusId (a plain    */
/*     GET .../statuses/:id refetch carries none of this -- it's         */
/*     notification metadata, not status content)                       */
/*   - isThreadReply (placement within a discussion view, not content)   */
/*   - timelineY/height (recomputed by the relayout the caller forces    */
/*     after this call, since content/media changes can resize the post) */
/* Every other field ttl_post_alloc copies IS overwritten here, freeing  */
/* the old AllocVec'd value first (same frees ttl_toot_dispose already   */
/* has for each) -- content, author display, avatar, boost-by, media,    */
/* counts, and poll all genuinely can have changed server-side.          */
/* ------------------------------------------------------------------ */

void ttl_post_refresh_fields(TTLPost *post, const TTLPostSetup *setup)
{
    ULONG mi;

    if (!post || !setup) return;

    if (post->username)  FreeVec(post->username);
    if (post->acct)       FreeVec(post->acct);
    if (post->body)       FreeVec(post->body);
    if (post->timestamp)  FreeVec(post->timestamp);
    if (post->boostBy)    FreeVec(post->boostBy);
    if (post->boostByAcct) FreeVec(post->boostByAcct);
    if (post->avatarURL)  FreeVec(post->avatarURL);
    if (post->mediaIdsJoined) FreeVec(post->mediaIdsJoined);
    if (post->cardUrl)          FreeVec(post->cardUrl);
    if (post->cardTitle)        FreeVec(post->cardTitle);
    if (post->cardDescription)  FreeVec(post->cardDescription);
    if (post->cardProviderName) FreeVec(post->cardProviderName);
    if (post->cardImageUrl)     FreeVec(post->cardImageUrl);
    if (post->quoteId)         FreeVec(post->quoteId);
    if (post->quoteAuthorName) FreeVec(post->quoteAuthorName);
    if (post->quoteAuthorAcct) FreeVec(post->quoteAuthorAcct);
    if (post->quoteAvatarURL)  FreeVec(post->quoteAvatarURL);
    if (post->quoteBody)       FreeVec(post->quoteBody);
    if (post->quoteTimestamp)  FreeVec(post->quoteTimestamp);
    for (mi = 0; mi < post->mediaCount; mi++) {
        if (post->mediaUrls[mi]) FreeVec(post->mediaUrls[mi]);
        if (post->mediaAudioUrls[mi]) FreeVec(post->mediaAudioUrls[mi]);
    }
    for (mi = 0; mi < post->pollOptionCount; mi++)
        if (post->pollOptionTitles[mi]) FreeVec(post->pollOptionTitles[mi]);

    post->username  = dup_str(setup->username);
    post->acct      = dup_str(setup->acct);
    post->body      = dup_str(setup->body);
    post->timestamp = dup_str(setup->timestamp);
    post->boostBy   = (setup->boostBy  && setup->boostBy[0])  ? dup_str(setup->boostBy)  : NULL;
    post->boostByAcct = (setup->boostByAcct && setup->boostByAcct[0]) ? dup_str(setup->boostByAcct) : NULL;
    post->avatarURL = (setup->avatarURL && setup->avatarURL[0]) ? dup_str(setup->avatarURL) : NULL;
    post->isOwn     = setup->isOwn;

    post->mediaCount = setup->mediaCount;
    if (post->mediaCount > TTL_POST_MAX_MEDIA) post->mediaCount = TTL_POST_MAX_MEDIA;
    for (mi = 0; mi < post->mediaCount; mi++) {
        post->mediaUrls[mi] = (setup->mediaUrls[mi] && setup->mediaUrls[mi][0])
                             ? dup_str(setup->mediaUrls[mi]) : NULL;
        post->mediaAudioUrls[mi] = (setup->mediaAudioUrls[mi] && setup->mediaAudioUrls[mi][0])
                                  ? dup_str(setup->mediaAudioUrls[mi]) : NULL;
        post->mediaKinds[mi] = setup->mediaKinds[mi];
    }

    post->mediaIdsJoined = NULL;
    {
        ULONG totalLen = 0, n, joinedCount = 0;
        for (mi = 0; mi < post->mediaCount; mi++) {
            if (setup->mediaIds[mi] && setup->mediaIds[mi][0]) {
                totalLen += (ULONG)strlen(setup->mediaIds[mi]);
                joinedCount++;
            }
        }
        if (joinedCount > 0) {
            totalLen += joinedCount - 1;
            post->mediaIdsJoined = AllocVec(totalLen + 1, MEMF_ANY);
            if (post->mediaIdsJoined) {
                char *dst = post->mediaIdsJoined;
                BOOL  first = TRUE;
                for (mi = 0; mi < post->mediaCount; mi++) {
                    if (setup->mediaIds[mi] && setup->mediaIds[mi][0]) {
                        if (!first) *dst++ = ',';
                        n = (ULONG)strlen(setup->mediaIds[mi]);
                        CopyMem((APTR)setup->mediaIds[mi], dst, n);
                        dst += n;
                        first = FALSE;
                    }
                }
                *dst = '\0';
            }
        }
    }

    post->repliesCount    = setup->repliesCount;
    post->reblogsCount    = setup->reblogsCount;
    post->favouritesCount = setup->favouritesCount;
    post->favourited      = setup->favourited;
    post->reblogged       = setup->reblogged;
    post->quotable        = setup->quotable;
    post->isReply         = setup->isReply;
    post->sensitive       = setup->sensitive;
    /* contentRevealed intentionally left untouched -- transient UI state,
     * not content (same reasoning as isThreadReply not being touched
     * above): a routine refresh must not re-hide something the user
     * already chose to reveal. */

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

    post->hasCard = setup->hasCard;
    post->cardUrl          = (setup->cardUrl          && setup->cardUrl[0])          ? dup_str(setup->cardUrl)          : NULL;
    post->cardTitle        = (setup->cardTitle        && setup->cardTitle[0])        ? dup_str(setup->cardTitle)        : NULL;
    post->cardDescription  = (setup->cardDescription  && setup->cardDescription[0])  ? dup_str(setup->cardDescription)  : NULL;
    post->cardProviderName = (setup->cardProviderName && setup->cardProviderName[0]) ? dup_str(setup->cardProviderName) : NULL;
    post->cardImageUrl     = (setup->cardImageUrl     && setup->cardImageUrl[0])     ? dup_str(setup->cardImageUrl)     : NULL;
    /* cardTitleLines/cardDescLines are rebuilt by the forced relayout the
     * caller (TTIMELINE_RefreshPost) triggers right after this call. */

    post->hasQuote = setup->hasQuote;
    post->quoteId         = (setup->quoteId         && setup->quoteId[0])         ? dup_str(setup->quoteId)         : NULL;
    post->quoteAuthorName = (setup->quoteAuthorName && setup->quoteAuthorName[0]) ? dup_str(setup->quoteAuthorName) : NULL;
    post->quoteAuthorAcct = (setup->quoteAuthorAcct && setup->quoteAuthorAcct[0]) ? dup_str(setup->quoteAuthorAcct) : NULL;
    post->quoteAvatarURL  = (setup->quoteAvatarURL  && setup->quoteAvatarURL[0])  ? dup_str(setup->quoteAvatarURL)  : NULL;
    post->quoteBody       = (setup->quoteBody       && setup->quoteBody[0])       ? dup_str(setup->quoteBody)       : NULL;
    post->quoteTimestamp  = (setup->quoteTimestamp  && setup->quoteTimestamp[0])  ? dup_str(setup->quoteTimestamp)  : NULL;
    /* quoteBodyLines are rebuilt by the forced relayout the caller
     * (TTIMELINE_RefreshPost) triggers right after this call. */
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
    if (post->targetId)  FreeVec(post->targetId);
    if (post->mediaIdsJoined) FreeVec(post->mediaIdsJoined);
    if (post->notifActorName) FreeVec(post->notifActorName);
    if (post->notifActorAcct) FreeVec(post->notifActorAcct);
    if (post->notifStatusId)  FreeVec(post->notifStatusId);
    if (post->cardUrl)          FreeVec(post->cardUrl);
    if (post->cardTitle)        FreeVec(post->cardTitle);
    if (post->cardDescription)  FreeVec(post->cardDescription);
    if (post->cardProviderName) FreeVec(post->cardProviderName);
    if (post->cardImageUrl)     FreeVec(post->cardImageUrl);
    if (post->quoteId)         FreeVec(post->quoteId);
    if (post->quoteAuthorName) FreeVec(post->quoteAuthorName);
    if (post->quoteAuthorAcct) FreeVec(post->quoteAuthorAcct);
    if (post->quoteAvatarURL)  FreeVec(post->quoteAvatarURL);
    if (post->quoteBody)       FreeVec(post->quoteBody);
    if (post->quoteTimestamp)  FreeVec(post->quoteTimestamp);
    {
        ULONG mi;
        for (mi = 0; mi < post->mediaCount; mi++) {
            if (post->mediaUrls[mi]) FreeVec(post->mediaUrls[mi]);
            if (post->mediaAudioUrls[mi]) FreeVec(post->mediaAudioUrls[mi]);
        }
        for (mi = 0; mi < post->pollOptionCount; mi++)
            if (post->pollOptionTitles[mi]) FreeVec(post->pollOptionTitles[mi]);
        for (mi = 0; mi < post->cardTitleLineCount; mi++)
            if (post->cardTitleLines[mi]) FreeVec(post->cardTitleLines[mi]);
        for (mi = 0; mi < post->cardDescLineCount; mi++)
            if (post->cardDescLines[mi]) FreeVec(post->cardDescLines[mi]);
        if (post->quoteBodyLines) {
            for (mi = 0; mi < post->quoteBodyLineCount; mi++)
                if (post->quoteBodyLines[mi]) FreeVec(post->quoteBodyLines[mi]);
            FreeVec(post->quoteBodyLines);
        }
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
    WORD  cardW = 0, cardH = 0, cardImgH = 0;
    BOOL  hasThumb, hasCard, hasRight;
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

    post->previewX = post->previewY = post->previewW = post->previewH = 0;
    post->cardX = post->cardY = post->cardW = post->cardH = post->cardImgH = 0;
    post->quoteX = post->quoteY = post->quoteW = post->quoteH = 0;

    /* Free any previously wrapped card title/description/quote body lines
     * before rebuilding below (layout can rerun on width/font change) --
     * same reasoning ttl_clear_textspans handles for textSpans just above. */
    {
        ULONG li;
        for (li = 0; li < post->cardTitleLineCount; li++)
            if (post->cardTitleLines[li]) { FreeVec(post->cardTitleLines[li]); post->cardTitleLines[li] = NULL; }
        post->cardTitleLineCount = 0;
        for (li = 0; li < post->cardDescLineCount; li++)
            if (post->cardDescLines[li]) { FreeVec(post->cardDescLines[li]); post->cardDescLines[li] = NULL; }
        post->cardDescLineCount = 0;
        if (post->quoteBodyLines) {
            for (li = 0; li < post->quoteBodyLineCount; li++)
                if (post->quoteBodyLines[li]) FreeVec(post->quoteBodyLines[li]);
            FreeVec(post->quoteBodyLines);
            post->quoteBodyLines = NULL;
        }
        post->quoteBodyLineCount = 0;
    }

    hasThumb = (post->mediaCount > 0 && post->pollOptionCount == 0);
    hasCard  = post->hasCard;

    /* Media preview rectangle sizing: same scale factor as the avatar
     * (both derive from lineH via FS3EStyle's compute_layout -- see
     * TTL_AVATAR_BASE_SIZE). */
    if (hasThumb) {
        previewW = (WORD)(((LONG)TTL_PREVIEW_BASE_W * avatarW) / TTL_AVATAR_BASE_SIZE);
        previewH = (WORD)(((LONG)TTL_PREVIEW_BASE_H * avatarW) / TTL_AVATAR_BASE_SIZE);
    }

    /* Link preview card box sizing: same width scale as the media preview
     * (TTL_CARD_BASE_W == TTL_PREVIEW_BASE_W, see that constant's comment),
     * height computed dynamically from the (optional) image strip plus
     * however many real wrapped title/description rows are actually
     * needed, capped -- same "reserve exactly what the content needs"
     * approach the poll block below already uses. */
    if (hasCard) {
        struct URPDrawContext *dcMini = inst->style ? inst->style->dcMini : NULL;
        WORD cardPad    = 4;
        WORD cardTextW;

        cardW = (WORD)(((LONG)TTL_CARD_BASE_W * avatarW) / TTL_AVATAR_BASE_SIZE);
        cardTextW = (WORD)(cardW - 2 * cardPad);
        if (cardTextW < 16) cardTextW = 16;

        if (post->cardImageUrl && post->cardImageUrl[0])
            cardImgH = (WORD)(((LONG)TTL_CARD_IMAGE_BASE_H * avatarW) / TTL_AVATAR_BASE_SIZE);

        cardH = cardImgH;
        if (cardImgH > 0) cardH += avatarGap;
        cardH += inst->miniLineHeight; /* provider name line */

        if (dcMini && post->cardTitle && post->cardTitle[0]) {
            FS3ETextWrap tw;
            if (FS3ETextWrap_Build(&tw, dcMini, inst->miniLineHeight, post->cardTitle, cardTextW)) {
                ULONG ri, rows = tw.rowCount;
                if (rows > TTL_CARD_TITLE_MAX_ROWS) rows = TTL_CARD_TITLE_MAX_ROWS;
                for (ri = 0; ri < rows; ri++) {
                    post->cardTitleLines[ri] = dup_strn(tw.rows[ri].start, tw.rows[ri].byteLen);
                    post->cardTitleLineCount++;
                    cardH += inst->miniLineHeight;
                }
                FS3ETextWrap_Free(&tw);
            }
        }

        if (dcMini && post->cardDescription && post->cardDescription[0]) {
            FS3ETextWrap tw;
            if (FS3ETextWrap_Build(&tw, dcMini, inst->miniLineHeight, post->cardDescription, cardTextW)) {
                ULONG ri, rows = tw.rowCount;
                if (rows > TTL_CARD_DESC_MAX_ROWS) rows = TTL_CARD_DESC_MAX_ROWS;
                for (ri = 0; ri < rows; ri++) {
                    post->cardDescLines[ri] = dup_strn(tw.rows[ri].start, tw.rows[ri].byteLen);
                    post->cardDescLineCount++;
                    cardH += inst->miniLineHeight;
                }
                FS3ETextWrap_Free(&tw);
            }
        }

        cardH += 2 * cardPad;
    }

    /* Side-by-side needs the gadget wide enough both by the caller's own
     * >500px rule and to leave a usable text column; otherwise everything
     * (text, then thumbnail, then card) stacks vertically instead. Card
     * and thumbnail share the same scaled width (see TTL_CARD_BASE_W), so
     * there's one shared "right column width" regardless of which of the
     * two (or both) are actually present. */
    hasRight = hasThumb || hasCard;
    if (hasRight) {
        WORD rightW = hasThumb ? previewW : cardW;
        if (inst->gadWidth > 500 && (textW - rightW - avatarGap) >= 32)
            sideBySide = TRUE;
        if (!sideBySide) {
            if (hasThumb && previewW > textW) previewW = textW;
            if (hasCard  && cardW    > textW) cardW    = textW;
        }
    }

    curRelY = TTL_POST_PAD_TOP;

    /* "↺ Name boosted" line (dcMini) — only for reblogs. Mutually
     * exclusive with the notifications-view prefix line below (see
     * TTLPost.notifType's comment) -- never both on the same post. */
    if (post->boostBy && post->boostBy[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->boostBy, TTL_SPAN_BOOSTBY,
                                          curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
        curRelY += inst->miniLineHeight;
    } else if (post->notifType > TTL_NOTIF_MENTION) {
        /* Notifications view's actor/verb prefix line -- see
         * ttl_toot_render's notifVerbFormat table for what's actually
         * drawn. NONE/MENTION (<=TTL_NOTIF_MENTION) draw nothing, so no
         * row is reserved for those. */
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
        LONG bodyBottomY;
        WORD rightW    = hasThumb ? previewW : cardW; /* equal by construction either way */
        WORD bodyTextW = (sideBySide && hasRight) ? (WORD)(textW - rightW - avatarGap) : textW;
        struct URPDrawContext *dcBody = inst->style ? inst->style->dcNormal : NULL;

        post->sensitiveTopY = (WORD)bodyTopY;

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

        bodyBottomY = curRelY;

        /* Thumbnail/card placement: side-by-side stacks them in a shared
         * right column (thumb above card); stacked narrow places them one
         * after another below the body text, in the same order. Either
         * one, both, or neither may be present -- see hasThumb/hasCard
         * above. */
        if (sideBySide) {
            WORD rightX  = (WORD)(textX + bodyTextW + avatarGap);
            LONG rightY  = bodyTopY;

            if (hasThumb && previewW > 0 && previewH > 0) {
                post->previewX = rightX;
                post->previewY = (WORD)rightY;
                post->previewW = previewW;
                post->previewH = previewH;
                rightY += previewH;
                if (hasCard) rightY += avatarGap;
            }
            if (hasCard && cardW > 0 && cardH > 0) {
                post->cardX    = rightX;
                post->cardY    = (WORD)rightY;
                post->cardW    = cardW;
                post->cardH    = cardH;
                post->cardImgH = cardImgH;
                rightY += cardH;
            }
            if (rightY > curRelY) curRelY = rightY;
        } else {
            if (hasThumb && previewW > 0 && previewH > 0) {
                curRelY += avatarGap;
                post->previewX = textX;
                post->previewY = (WORD)curRelY;
                post->previewW = previewW;
                post->previewH = previewH;
                curRelY += previewH;
            }
            if (hasCard && cardW > 0 && cardH > 0) {
                curRelY += avatarGap;
                post->cardX    = textX;
                post->cardY    = (WORD)curRelY;
                post->cardW    = cardW;
                post->cardH    = cardH;
                post->cardImgH = cardImgH;
                curRelY += cardH;
            }
        }

        /* Sensitive-content blur/reveal zone (see TTL_HOT_SENSITIVE_TOGGLE
         * in ttl_toot_render/ttl_toot_activate) -- covers the body text
         * always, plus the media preview when present (post->previewY/H,
         * already computed above), but never the link preview card, which
         * stays visible regardless -- see TTLPost.sensitive's doc comment:
         * only text/media are ever hidden. */
        post->sensitiveBottomY = (WORD)(hasThumb ? (post->previewY + post->previewH) : bodyBottomY);
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

    /* ---- "Follow discussion up" row -- reserved only when this post is
     * itself a reply, ABOVE the "...down" row below (see TTL_HOT_THREAD_UP).
     * Independent of repliesCount: a post can be a reply with no replies of
     * its own (only this row), have replies without being one itself (only
     * the row below), both (both rows), or neither (neither row). ---- */
    post->threadUpRowY = 0;
    if (post->isReply) {
        curRelY += avatarGap;
        post->threadUpRowY = (WORD)curRelY;
        curRelY += inst->miniLineHeight;
    }

    /* ---- Thread indicator: short vertical bar + "..." meaning "this
     * toot has replies, click to see the discussion" -- reserved only
     * when it actually does, right above the action bar. threadRowY
     * stored here and reused as-is by render/build_hotspots, same
     * "store once, never re-derive" rule as pollBlockY above. ---- */
    post->threadRowY = 0;
    if (post->repliesCount > 0) {
        curRelY += avatarGap;
        post->threadRowY = (WORD)curRelY;
        curRelY += inst->miniLineHeight;
    }

    /* ---- Action bar: ↩ Reply N  🔁 Boost N  ⭐/💫 N ----
     * Only the row's height is needed for layout; exact button rects are
     * (re)computed lazily in ttl_post_build_hotspots from
     * ttl_build_action_labels(), the same builder tiles.c draws from, so
     * the two can never drift apart the way two separately-hand-maintained
     * copies would. */
    curRelY += 2;                              /* gap above the action bar */
    post->actionBarY = (WORD)curRelY;
    curRelY += inst->lineHeight + TTL_POST_PAD_BOT;

    /* ---- Embedded quote block: a bordered box (not just a separator
     * line -- a line read as still "part of" this post; a full rectangle
     * reads as a distinct nested block), containing a minimal "nested
     * toot" -- smaller avatar, dcMini throughout (no dcUsername/dcNormal),
     * no action bar/poll/media/card of its own, and no further nested
     * quote even if the quoted status is itself a quote (bounded to one
     * level) -- see TTLPost.hasQuote's doc comment. Indented further right
     * than this post's own avatar column start (padLeft alone would just
     * align with THIS post's own left edge, reading as another row at the
     * same level rather than nested under it) -- width shrinks to match,
     * still derived from this post's own gadWidth-relative margins, not a
     * fixed card-style scale. ---- */
    if (post->hasQuote) {
        struct URPDrawContext *dcMini = inst->style ? inst->style->dcMini : NULL;
        WORD qPad = 4;
        WORD qIndent = (WORD)(padLeft + avatarW / 2);
        WORD qAvatarSize = (WORD)(((LONG)avatarW * TTL_QUOTE_AVATAR_SCALE_NUM) / TTL_QUOTE_AVATAR_SCALE_DEN);
        WORD qTextW, qHeaderH, qTopH, qBodyH;

        curRelY += avatarGap; /* gap between the action bar and the quote box -- see post->actionBarY */

        post->quoteX = qIndent;
        post->quoteW = (WORD)(inst->gadWidth - qIndent - TTL_POST_PAD_RIGHT);
        if (post->quoteW < 32) post->quoteW = 32;
        post->quoteY = (WORD)curRelY;

        qTextW = (WORD)(post->quoteW - (qPad + qAvatarSize + avatarGap) - qPad);
        if (qTextW < 16) qTextW = 16;

        qHeaderH = (WORD)(2 * inst->miniLineHeight); /* username line + "@acct - timestamp" line */
        qTopH = (qAvatarSize > qHeaderH) ? qAvatarSize : qHeaderH;

        qBodyH = 0;
        if (dcMini && post->quoteBody && post->quoteBody[0]) {
            FS3ETextWrap tw;
            if (FS3ETextWrap_Build(&tw, dcMini, inst->miniLineHeight, post->quoteBody, qTextW)) {
                ULONG ri, rows = tw.rowCount;
                if (rows > TTL_QUOTE_BODY_MAX_ROWS) rows = TTL_QUOTE_BODY_MAX_ROWS;
                if (rows > 0)
                    post->quoteBodyLines = (char **)AllocVec(rows * sizeof(char *), MEMF_ANY | MEMF_CLEAR);
                for (ri = 0; ri < rows && post->quoteBodyLines; ri++) {
                    post->quoteBodyLines[ri] = dup_strn(tw.rows[ri].start, tw.rows[ri].byteLen);
                    post->quoteBodyLineCount++;
                    qBodyH += inst->miniLineHeight;
                }
                FS3ETextWrap_Free(&tw);
            }
        }
        if (qBodyH > 0) qBodyH += 2; /* small gap above the wrapped body */

        post->quoteH = (WORD)(2 * qPad + qTopH + qBodyH);

        curRelY += post->quoteH;
        curRelY += avatarGap; /* gap below the quote box, before this post's own bottom separator */
    }

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
extern const UBYTE        ttl_ownActionTypes[2];
extern const char *const  ttl_ownActionLabels[2];

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
    /* Default: the whole of data is visible/drawn at [x,w) -- true for
     * every hotspot except a wrapped body token, which overrides this
     * right after this call -- see ttl_scan_span_tokens/TTLHotSpot.visibleLen. */
    hs->visibleLen = hs->dataLen;
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
        else if (*p == '#') {
            /* A bare '#' only starts a hashtag when nothing but whitespace
             * (or nothing at all -- start of the post) immediately
             * precedes it, matching official Mastodon clients -- otherwise
             * a URL fragment like "example.com/page#section" (no
             * recognized http(s):// prefix to swallow it whole the way
             * the URL branch below does) would wrongly turn "#section"
             * into its own hashtag hotspot. Looked up in sp->bodySrc (the
             * persistent, unwrapped post body), not sp->utf8 (this
             * wrapped row): word-wrap can start a row mid-word (see this
             * function's own doc comment), so "first byte of this row"
             * doesn't necessarily mean "first byte of a word" in the real
             * text. */
            const unsigned char *bodyPos = (const unsigned char *)sp->bodySrc +
                                            (p - (const unsigned char *)sp->utf8);
            if (bodyPos == (const unsigned char *)sp->bodySrc ||
                bodyPos[-1] == ' '  || bodyPos[-1] == '\t' ||
                bodyPos[-1] == '\n' || bodyPos[-1] == '\r')
                hotType = TTL_HOT_HASHTAG;
        }
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

                /* dataLen above is the token's FULL length (bodySrc can run
                 * past this row -- see this function's doc comment), but
                 * only [tokStart, p) of it actually landed on this row's
                 * own [x,w) -- ttl_toot_render's recolor pass must redraw
                 * just that prefix, not the whole (possibly multi-row)
                 * token, or it draws the rest as one overflowing unwrapped
                 * line -- see TTLHotSpot.visibleLen. dataStart and tokStart
                 * are byte-identical up to this row's own end (bodySrc and
                 * sp->utf8 are copies of the same post->body bytes), so
                 * this byte count applies to either buffer equally. */
                post->hotSpots[post->hotSpotCount - 1].visibleLen =
                    (ULONG)(p - tokStart);
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
    WORD avatarW, padLeft, avatarGap, textX;
    BOOL contentHidden = (post->sensitive && !post->contentRevealed);

    post->hotSpotCount = 0;

    if (inst->style && inst->style->avatarSize > 0) {
        avatarW = inst->style->avatarSize;
        padLeft = inst->style->postPadLeft;
        avatarGap = inst->style->avatarGap;
    } else {
        avatarW = 35;
        padLeft = 6;
        avatarGap = 6;
    }
    textX = (WORD)(padLeft + avatarW + avatarGap);

    /* Avatar icon -> profile */
    ttl_hs_add(post, TTL_HOT_AVATAR, padLeft, TTL_POST_PAD_TOP, avatarW, avatarW,
               post->acct, post->acct ? (ULONG)strlen(post->acct) : 0);

    /* Notifications view's actor/verb prefix line -- see TTLPost.notifType's
     * comment for why this is mutually exclusive with "boosted" (never
     * both hotspots on the same post), and ttl_toot_render's
     * notifVerbFormat table for which types actually draw a line
     * (NONE/MENTION don't, so no hotspot either). Same "first line"
     * position the boostBy span would occupy. */
    if (post->notifType > TTL_NOTIF_MENTION && post->notifStatusId && post->notifStatusId[0] &&
        post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT)
    {
        WORD rowW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
        ttl_hs_add(post, TTL_HOT_NOTIF_STATUS, textX, TTL_POST_PAD_TOP,
                   rowW, inst->miniLineHeight,
                   post->notifStatusId, (ULONG)strlen(post->notifStatusId));
    }

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
        } else if (sp->spanType == TTL_SPAN_BODY && !contentHidden) {
            /* Skipped while hidden: the body text itself isn't drawn (see
             * ttl_toot_render), so there are no mention/hashtag/URL glyphs
             * on screen to make clickable -- the one big
             * TTL_HOT_SENSITIVE_TOGGLE hotspot added below covers this
             * whole zone instead. */
            ttl_scan_span_tokens(post, sp);
        }
    }

    if (contentHidden) {
        /* Sensitive and not yet revealed: one hotspot over the whole
         * reserved text+media zone (nothing else drawn in it -- see
         * ttl_draw_sensitive_zone), instead of the normal per-token/media
         * hotspots below. */
        WORD rowW  = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
        WORD zoneH = (WORD)(post->sensitiveBottomY - post->sensitiveTopY);
        if (post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT && zoneH > 0)
            ttl_hs_add(post, TTL_HOT_SENSITIVE_TOGGLE, textX, post->sensitiveTopY,
                       rowW, zoneH, NULL, 0);
    } else if (post->mediaCount > 0 && post->previewW > 0) {
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

    /* Link preview card rectangle -- see TTLPost.hasCard/cardX/Y/W/H.
     * Currently a no-op on activation (see TTL_HOT_CARD's doc comment). */
    if (post->hasCard && post->cardW > 0) {
        ttl_hs_add(post, TTL_HOT_CARD, post->cardX, post->cardY,
                   post->cardW, post->cardH,
                   post->cardUrl, post->cardUrl ? (ULONG)strlen(post->cardUrl) : 0);
    }

    /* "Follow discussion up" row -- see TTLPost.threadUpRowY. Same full-
     * width clickable treatment as the "...down" row below. */
    if (post->isReply && post->threadUpRowY > 0 &&
        post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT)
    {
        WORD rowW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
        ttl_hs_add(post, TTL_HOT_THREAD_UP, textX, post->threadUpRowY,
                   rowW, inst->miniLineHeight, NULL, 0);
    }

    /* "N replies" thread-indicator row -- see TTLPost.threadRowY. Spans
     * the same left-to-right text column as the body, full clickable
     * width rather than just the small vertical-bar-and-dots glyph,
     * easier to hit than the glyph alone. */
    if (post->repliesCount > 0 && post->threadRowY > 0 &&
        post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT)
    {
        WORD rowW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
        ttl_hs_add(post, TTL_HOT_THREAD, textX, post->threadRowY,
                   rowW, inst->miniLineHeight, NULL, 0);
    }

    /* Action bar buttons: same geometry rule ttl_post_layout used to
     * reserve the row (post->height - separator - PAD_BOT - barH), same
     * labels tiles.c draws -- both build from ttl_build_action_labels(),
     * see that function's comment. */
    if (post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT) {
        struct URPDrawContext *dcA = inst->style ? inst->style->dcNormal : NULL;
        WORD barH   = inst->lineHeight;
        WORD y      = post->actionBarY;
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
            {
                /* Reply carries the original author's acct so the caller
                 * can show/address them without a separate lookup (title
                 * text + @-mention prefix -- see
                 * FS3ETootView_SetComposeContext's REPLY kind); Boost/Fave
                 * need no data. */
                const char *hsData = (ttl_actionTypes[a] == TTL_HOT_REPLY && post->acct)
                                    ? post->acct : NULL;
                ULONG hsDataLen = hsData ? (ULONG)strlen(hsData) : 0;
                ttl_hs_add(post, ttl_actionTypes[a], (WORD)(xRight - w), y, w, barH, hsData, hsDataLen);
            }
            xRight = (WORD)(xRight - w - TTL_ACTION_GAP);
        }
    }

    /* Modify/Delete: same bar row, left-aligned from textX, own toots
     * only -- measured from the same ttl_ownActionLabels[] that
     * ttl_toot_render draws, per this codebase's "single shared copy"
     * rule (see that array's definition in fs3etoottimeline_tiles.c). */
    if (post->isOwn && post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT) {
        struct URPDrawContext *dcA = inst->style ? inst->style->dcNormal : NULL;
        WORD barH  = inst->lineHeight;
        WORD y     = post->actionBarY;
        WORD xLeft = textX;
        int  a;

        for (a = 0; a < 2 && post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT; a++) {
            WORD w = 40;
            if (dcA) {
                struct URPTextMetric m;
                LONG nc = 0;
                const char *s = ttl_ownActionLabels[a];
                const unsigned char *q = (const unsigned char *)s;
                while (*q) {
                    unsigned char c = *q;
                    q += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                    nc++;
                }
                URPDC_TextSizeUTF8(dcA, s, nc, &m);
                w = (WORD)(m.width > 0 ? m.width : 40);
            }
            {
                /* Modify carries the raw body so the caller can prefill
                 * the compose window without a separate lookup by
                 * postId; Delete needs no data. */
                const char *hsData = (ttl_ownActionTypes[a] == TTL_HOT_MODIFY && post->body)
                                    ? post->body : NULL;
                ULONG hsDataLen = hsData ? (ULONG)strlen(hsData) : 0;
                ttl_hs_add(post, ttl_ownActionTypes[a], xLeft, y, w, barH, hsData, hsDataLen);
            }
            xLeft = (WORD)(xLeft + w + TTL_ACTION_GAP);
        }
    }

    /* Embedded quote block: one hotspot over the whole rect (not
     * per-element), carrying quoteId as data so it opens THAT status'
     * own discussion -- see TTL_HOT_QUOTECARD's comment. */
    if (post->hasQuote && post->quoteW > 0 && post->quoteH > 0 &&
        post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT)
    {
        ttl_hs_add(post, TTL_HOT_QUOTECARD, post->quoteX, post->quoteY,
                   post->quoteW, post->quoteH,
                   post->quoteId, post->quoteId ? (ULONG)strlen(post->quoteId) : 0);
    }

    /* Revealed sensitive content: real content above already got its own
     * normal hotspots (media/mention/etc, added above under !contentHidden)
     * -- this just adds a small "Hide sensitive content" corner hotspot so
     * there's still a way back, matching ttl_draw_sensitive_zone's
     * top-right corner label. Added LAST so it wins ties in back-to-front
     * hit-testing (see ttl_hit_hotspot) over whatever's directly under it
     * (e.g. a body-text token or the media rect, if either happens to
     * reach that corner). */
    if (post->sensitive && post->contentRevealed &&
        post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT)
    {
        struct URPDrawContext *dcA = inst->style ? inst->style->dcNormal : NULL;
        const char *label = "Hide sensitive content";
        WORD zoneH = (WORD)(post->sensitiveBottomY - post->sensitiveTopY);
        WORD labelH = inst->lineHeight;
        WORD labelW = 160;

        if (labelH > zoneH) labelH = zoneH;
        if (dcA) {
            struct URPTextMetric m;
            LONG nc = utf8_codepoints_range(label, label + strlen(label));
            URPDC_TextSizeUTF8(dcA, label, nc, &m);
            labelW = (WORD)(m.width > 0 ? m.width : labelW);
        }

        {
            WORD rowRight = (WORD)(inst->gadWidth - TTL_POST_PAD_RIGHT);
            WORD labelX   = (WORD)(rowRight - labelW);
            if (labelX < textX) labelX = textX;
            ttl_hs_add(post, TTL_HOT_SENSITIVE_TOGGLE, labelX, post->sensitiveTopY,
                       (WORD)(rowRight - labelX), labelH, NULL, 0);
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
    /* Profile header (see TTLChannel.headerPost) is outside `posts`, so
     * the loop above never frees it -- guarded/no-op for every channel
     * without one. */
    if (channel->headerPost) {
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
/* TTLListTitle_Class -- pinned informational row ("Followers for       */
/* @user") at the top of a followers/following list (see               */
/* TTLPostSetup.isListTitle). Reuses ttl_boundary_layout/render verbatim */
/* -- same single centered accent-colored line TTLLoadNewer_Class/      */
/* TTLLoadOlder_Class already draw -- but unlike those two, this row's  */
/* text is dynamically built per-request (which user's list this is),  */
/* not a static literal owned by the class definition, so it needs its */
/* own alloc (dup's the string) and dispose (frees it), instead of      */
/* ttl_pseudo_post_alloc's borrow-a-literal contract. No hot-spot: this */
/* row is purely informational, never clickable. */
/* ------------------------------------------------------------------ */

TTLPost *ttl_list_title_alloc(const TTLPostSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls           = &TTLListTitle_Class;
    post->dirty         = TRUE;
    post->hotSpotBucket = -1;
    post->hotSpotsDirty = TRUE;

    if (setup)
        post->body = dup_str(setup->body);

    return post;
}

static void ttl_list_title_dispose(TTLPost *post)
{
    if (post->body) FreeVec(post->body);
}

const TTLItemClass TTLListTitle_Class = {
    ttl_boundary_layout,
    ttl_boundary_render,
    NULL,  /* buildHotspots: purely informational, no click target */
    NULL,
    ttl_list_title_dispose
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
