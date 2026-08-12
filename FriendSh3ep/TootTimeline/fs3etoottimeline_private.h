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
#include <exec/semaphores.h>
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
#include "../fs3etextwrap.h"

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

/* Vertical padding constants (not font-dependent, kept as fixed values) */
#define TTL_POST_PAD_TOP   4   /* pixels above content in a post */
#define TTL_POST_PAD_BOT   5   /* pixels below content (before separator) */
#define TTL_POST_PAD_RIGHT 6

/* TTL_POST_PAD_LEFT and TTL_AVATAR_GAP are no longer constants: they are
 * stored in FS3EStyle.postPadLeft / FS3EStyle.avatarGap, computed from the
 * current font size by FS3EStyle_SetFontSize / FS3EStyle_InitDefaults. */

/* Media preview rectangle: fixed base size, scaled by the same
 * avatarSize/TTL_AVATAR_BASE_SIZE ratio FS3EStyle uses to grow the avatar
 * with font size (see compute_layout() in fs3estyle.c: avatarSize ==
 * TTL_AVATAR_BASE_SIZE at the default 12pt font). */
#define TTL_PREVIEW_BASE_W      200
#define TTL_PREVIEW_BASE_H      150
#define TTL_AVATAR_BASE_SIZE     35

/* Link preview card box: same width scale as the media preview rect above
 * (TTL_CARD_BASE_W == TTL_PREVIEW_BASE_W, deliberately) so a thumbnail
 * stacked above a card in the side-by-side right column never needs
 * independent width reconciliation -- only heights differ. Height is NOT
 * a fixed constant (unlike the preview rect): it's computed dynamically in
 * ttl_toot_layout from the image strip (scaled by TTL_CARD_IMAGE_BASE_H,
 * same ratio as the avatar/preview) plus however many real wrapped text
 * rows the title/description actually need, capped at
 * TTL_CARD_TITLE_MAX_ROWS/TTL_CARD_DESC_MAX_ROWS. */
#define TTL_CARD_BASE_W          TTL_PREVIEW_BASE_W
#define TTL_CARD_IMAGE_BASE_H     90
#define TTL_CARD_TITLE_MAX_ROWS    2
#define TTL_CARD_DESC_MAX_ROWS     2

/* Embedded quote block's own body text, word-wrapped the same way the card
 * title/description are (FS3ETextWrap) -- see TTLPost.quoteBodyLines. Unlike
 * the card's title/description (a fixed-size array, deliberately capped
 * short), the quote body is AllocVec'd to exactly how many rows
 * FS3ETextWrap_Build actually reports, so a normal-length toot is never
 * truncated -- computing that count costs nothing extra, FS3ETextWrap_Build
 * already returns it before any row is stored. This cap is purely a
 * pathological-input safety net (some custom instance allowing far more
 * than Mastodon's usual ~500 chars, wrapped into a narrow column), not a
 * real-world limit. Unlike the card, the quote block's own avatar is
 * scaled smaller (see TTL_QUOTE_AVATAR_SCALE_NUM/DEN in ttl_toot_layout) to
 * read as visually subordinate to the outer post's own avatar. */
#define TTL_QUOTE_BODY_MAX_ROWS    200
#define TTL_QUOTE_AVATAR_SCALE_NUM 2
#define TTL_QUOTE_AVATAR_SCALE_DEN 3

/* Poll ("survey") results block: fixed pixel height for each option's
 * proportional result bar (drawn below its title/percentage text row)
 * and the gap before the next option -- not font-dependent, same
 * treatment as the padding constants above. */
#define TTL_POLL_BAR_H      6
#define TTL_POLL_ROW_GAP    3

/* Gap in pixels between action-bar buttons (Reply / Boost / Fave) */
#define TTL_ACTION_GAP            8

/* Max bytes (incl. NUL) of one action-bar label built by
 * ttl_build_action_labels() -- glyph + word + " " + a ULONG's worth of
 * digits comfortably fits (worst case "\xF0\x9F\x94\x81 Boost 4294967295"
 * is 24 bytes). */
#define TTL_ACTION_LABEL_MAX      32

/* Prev/next arrow hit-zone width within a multi-image preview rect (see
 * TTL_HOT_MEDIA_PREV/NEXT) -- a fraction of the rect's own width, clamped
 * to a sane range so it's neither a sliver on a tiny rect nor absurdly
 * wide on a large one. One shared formula so ttl_post_build_hotspots
 * (fs3etoottimeline_posts.c) and the arrow-glyph draw in
 * fs3etoottimeline_tiles.c can never disagree on where the zone is. */
#define TTL_MEDIA_ARROW_MIN_W     8
#define TTL_MEDIA_ARROW_MAX_W    24

INLINE WORD ttl_media_arrow_width(WORD previewW)
{
    WORD w = (WORD)(previewW / 4);
    if (w > TTL_MEDIA_ARROW_MAX_W) w = TTL_MEDIA_ARROW_MAX_W;
    if (w < TTL_MEDIA_ARROW_MIN_W) w = TTL_MEDIA_ARROW_MIN_W;
    return w;
}

/* Forward declaration — full type in ../avatarimages.h, included by
 * files that actually call AvatarImages_Get(). */
struct AvatarImages;

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
/*                                                                      */
/* A toot's body can carry a few dozen @mention/#hashtag/URL tokens, so */
/* hot-spots are NOT individually AllocVec'd: every TTLData owns a      */
/* fixed pool (hotSpotPool[TTL_HOTSPOT_POOL_TOOTS][TTL_HOTSPOT_MAX_PER_ */
/* TOOT]) and each post that's actually drawn into a tile is lent one   */
/* bucket (TTLPost.hotSpots points into the pool) -- see                */
/* ttl_post_ensure_hotspots() in fs3etoottimeline_posts.c. Buckets are  */
/* handed out round-robin, so only posts overlapping *currently         */
/* rendered* tiles are guaranteed to have hot-spots at any given        */
/* moment; that's fine since nothing off-screen can be clicked.         */
/*                                                                      */
/* `data`/`dataLen` are a *borrowed* pointer + byte length into text    */
/* that's already owned elsewhere (post->acct/boostBy, or a             */
/* TTLTextSpan's utf8 buffer for body tokens) -- not NUL-terminated,    */
/* not AllocVec'd here, no truncation. This is safe because hot-spots   */
/* are only ever read right after ttl_post_ensure_hotspots() has just   */
/* rebuilt them from the post's *current* strings/spans (see            */
/* hotSpotsDirty): whatever they point into is guaranteed alive at that */
/* moment, and a relayout that would free/replace it also marks         */
/* hotSpotsDirty so a stale pointer never gets read in between.         */
/* ------------------------------------------------------------------ */

#define TTL_HOTSPOT_MAX_PER_TOOT   32
#define TTL_HOTSPOT_POOL_TOOTS      8   /* 8 * 32 == 256 total pool slots */

struct TTLHotSpot {
    WORD        x, y, w, h;  /* pixel rect; x/w absolute gadget X, y/h relative to post->timelineY */
    UBYTE       type;        /* TTL_HOT_* (see fs3etoottimeline.h) */
    const char *data;        /* borrowed, not NUL-terminated; NULL if not applicable (Reply/Boost/Fave/media) */
    ULONG       dataLen;     /* byte length of data -- the full click-target text (see
                               * ttl_scan_span_tokens: for a body @mention/#hashtag/URL this
                               * is re-measured from the persistent, unwrapped post->body via
                               * TTLTextSpan.bodySrc, so a click anywhere on the visible
                               * fragment still opens/targets the WHOLE token even if
                               * word-wrap split it across rows -- never truncated to fit [x,w). */
    ULONG       visibleLen;  /* byte length of the PREFIX of data that's actually drawn at
                               * [x,w) on this one row -- equal to dataLen for every hotspot
                               * except a body token that word-wrap continues past this row's
                               * own end (see ttl_scan_span_tokens); ttl_toot_render's
                               * mention/hashtag/URL recolor pass must draw only this many
                               * bytes, or it redraws the token's full, unwrapped text as one
                               * overflowing line instead of respecting the wrap. */
};

/* ------------------------------------------------------------------ */
/* TTLTextSpan — one rendered text line for hit-testing / selection     */
/* ------------------------------------------------------------------ */

#define TTL_SPAN_USERNAME  0
#define TTL_SPAN_ACCT      1
#define TTL_SPAN_BODY      2
#define TTL_SPAN_TIMESTAMP 3
#define TTL_SPAN_BOOSTBY   4

/* Hot-spot types (TTL_HOT_*) now live only in fs3etoottimeline.h -- see
 * that header for why having two definitions here was a footgun. */

typedef struct {
    struct MinNode  node;
    LONG   postRelY;      /* Y of span top, relative to post->timelineY */
    WORD   x;             /* left pixel X within the gadget */
    WORD   width;         /* rendered advance width in pixels */
    WORD   height;        /* line height */
    WORD   ascent;        /* pixels from span top to text baseline */
    UBYTE  spanType;      /* TTL_SPAN_* */
    char  *utf8;          /* AllocVec'd text (this row's own copy, used for drawing/measuring) */
    ULONG  byteLen;
    LONG  *charXOffsets;  /* AllocVec'd array of (charCount+1) LONG values */
    ULONG  charCount;

    /* TTL_SPAN_BODY only: pointer into the post's own persistent
     * post->body at the byte offset this row started at (NULL for other
     * span types). Byte-for-byte identical content to `utf8` for this
     * row, but -- unlike utf8 -- it keeps going past this row's own
     * byteLen into whatever text originally followed in post->body, since
     * post->body is one contiguous, unwrapped buffer that outlives any
     * individual relayout. Used to recover a token's full length when
     * word-wrap happened to cut it mid-word across two rows -- see
     * ttl_scan_span_tokens(). */
    const char *bodySrc;
} TTLTextSpan;

/* ------------------------------------------------------------------ */
/* TTLItemClass — vtable that makes a TTLPost node "one of several kinds */
/* of list row" instead of always being a toot. A toot is the first and */
/* still the richest implementation (TTLToot_Class, defined in           */
/* fs3etoottimeline_posts.c); non-toot rows (load-more/load-newer        */
/* boundary items, later notifications/news) reuse the exact same        */
/* TTLPost node shape, tile/scroll/hit-testing machinery, and hot-spot   */
/* pool, just via a different class pointer. Every TTLPost sets its      */
/* ->cls exactly once, at allocation.                                    */
/* ------------------------------------------------------------------ */

struct TTLPost;
typedef struct TTLData TTLData;

typedef struct TTLItemClass {
    /* Compute item->height and (if it has any) rebuild item->textSpans.
     * Called whenever the item is added, or the gadget width/font
     * changes (see ttl_layout_all_posts). */
    void (*layout)(TTLData *inst, struct TTLPost *item);

    /* Draw the item into rp at (item->timelineY - tileBaseY). Called once
     * per visible item per tile (re)render, right after
     * ttl_post_ensure_hotspots -- see ttl_render_tile. The tile
     * background fill and the post-bottom separator line are drawn by
     * the generic caller, not here. */
    void (*render)(TTLData *inst, struct RastPort *rp,
                    struct TTLPost *item, LONG tileBaseY);

    /* (Re)build item->hotSpots[0..hotSpotCount) in place -- caller
     * (ttl_post_ensure_hotspots) has already pointed item->hotSpots at a
     * pool bucket. May leave hotSpotCount at 0 (e.g. a proximity-
     * triggered item with no click target). */
    void (*buildHotspots)(TTLData *inst, struct TTLPost *item);

    /* React to one of this item's own hot-spots being clicked, after the
     * generic ttl_notify_hotspot() (TTIMELINE_HotSpotNotify) has already
     * fired for every item kind alike. NULL if the class has nothing
     * kind-specific to do locally (the notify alone is enough). */
    void (*activate)(TTLData *inst, Class *cl, Object *o,
                      struct GadgetInfo *gi, struct TTLPost *item,
                      TTLHotSpot *hs);

    /* Free whatever content the item owns beyond the bare TTLPost node
     * (strings, media URLs, ...). NULL if there's nothing beyond that. */
    void (*dispose)(struct TTLPost *item);
} TTLItemClass;

/* ------------------------------------------------------------------ */
/* TTLPost — one row entry in the timeline (a toot, or another item     */
/* kind -- see TTLItemClass above)                                      */
/* ------------------------------------------------------------------ */

typedef struct TTLPost {
    struct MinNode  node;         /* doubly-linked in one TTLChannel.posts, newest=head */

    const TTLItemClass *cls;      /* set once at allocation; never NULL */

    LONG   timelineY;             /* Y of post top in timeline coordinates */
    LONG   height;                /* computed pixel height of this post */
    BOOL   dirty;                 /* TRUE = overlapping tiles need re-render */

    /* Content strings (all AllocVec'd copies) */
    char  *username;
    char  *acct;
    char  *body;
    char  *timestamp;
    char  *boostBy;    /* booster display name, NULL for original posts */
    char  *boostByAcct; /* booster @user@instance, NULL for original posts -- see TTLPostSetup.boostByAcct */
    char  *avatarURL;  /* CDN URL; used to trigger deferred download */
    char  *postId;     /* Mastodon status id, NULL if unknown; see TTLPostSetup.postId */
    char  *targetId;   /* id hot-spot actions target, NULL if unknown; see TTLPostSetup.targetId */

    /* Media attachment preview URLs (AllocVec'd copies), NULL past
     * mediaCount. mediaCurrentIndex is which one is currently shown in
     * the single preview rect below -- browsed via TTL_HOT_MEDIA_PREV/
     * NEXT, not separate rects per image. mediaKinds is TTL_MEDIA_KIND_*
     * per slot, same indexing -- TTL_MEDIA_KIND_AUDIO gets a
     * TTL_HOT_PLAY_AUDIO hot-spot instead of a fetched thumbnail, see
     * ttl_toot_build_hotspots/ttl_toot_render. */
    char  *mediaUrls[TTL_POST_MAX_MEDIA];
    /* Real, playable file URL per slot (AllocVec'd copies), same indexing
     * as mediaUrls -- see TTLPostSetup.mediaAudioUrls for why this is a
     * separate array (a TTL_MEDIA_KIND_AUDIO slot with cover art has
     * mediaUrls[i] pointing at that cover IMAGE, not the audio; this is
     * always the actual file). ttl_activate_hotspot() reads
     * mediaAudioUrls[mediaCurrentIndex] instead of mediaUrls[...] for
     * TTL_HOT_PLAY_AUDIO specifically. NULL past mediaCount, same as
     * mediaUrls. */
    char  *mediaAudioUrls[TTL_POST_MAX_MEDIA];
    ULONG  mediaKinds[TTL_POST_MAX_MEDIA];
    ULONG  mediaCount;
    ULONG  mediaCurrentIndex;

    /* Comma-joined media_attachments[].id list (AllocVec'd), built once
     * from TTLPostSetup.mediaIds at alloc time; NULL if mediaCount==0. Not
     * split back into a per-slot array here since nothing in TootTimeline
     * itself needs per-item access -- this only exists to travel, whole,
     * through ttl_notify_hotspot()/TTIMELINE_LastHotSpotMediaIds to a
     * TTL_HOT_MODIFY handler outside the gadget. See TTLPostSetup.mediaIds. */
    char  *mediaIdsJoined;

    /* Action-bar counts/state -- see the matching TTLPostSetup fields. */
    ULONG  repliesCount;
    ULONG  reblogsCount;
    ULONG  favouritesCount;
    BOOL   favourited;
    BOOL   reblogged;
    BOOL   quotable;   /* see TTLPostSetup.quotable */
    BOOL   isReply;    /* see TTLPostSetup.isReply */
    BOOL   isOwn;      /* see TTLPostSetup.isOwn */
    BOOL   isThreadReply; /* see TTLPostSetup.isThreadReply */

    /* Sensitive-content blur/reveal -- see TTLPostSetup.sensitive's doc
     * comment. sensitive is copied straight from the server; contentRevealed
     * is purely local/transient UI state (never sent anywhere, reset back to
     * FALSE by ttl_post_refresh_fields like every other server-driven field
     * refresh -- see its own comment). Deliberately does NOT change
     * post->height: ttl_toot_layout always reserves the body/media area at
     * full size regardless of this flag, so toggling it is a pure render-time
     * decision (ttl_toot_render) needing only a tile-range invalidate, never
     * a relayout -- see TTL_HOT_SENSITIVE_TOGGLE in ttl_toot_activate. */
    BOOL   sensitive;
    BOOL   contentRevealed;
    /* [sensitiveTopY, sensitiveBottomY) in post-relative Y -- the blur/
     * reveal zone computed once by ttl_toot_layout, reused as-is by
     * ttl_toot_render/ttl_toot_build_hotspots, same "store once" rule as
     * previewX/Y/W/H etc. above. Meaningless when !sensitive. */
    WORD   sensitiveTopY;
    WORD   sensitiveBottomY;

    /* Notifications view -- see the matching TTLPostSetup fields. Mutually
     * exclusive with boostBy/boostByAcct above by construction (nothing
     * ever populates both on the same post -- boostBy comes from a plain
     * timeline fetch, notifType from a notifications fetch), so
     * ttl_toot_layout/render treat them as alternatives sharing the same
     * "first line" position rather than reserving a second row. */
    ULONG  notifType;      /* see TTLPostSetup.notifType */
    char  *notifActorName; /* AllocVec'd copy */
    char  *notifActorAcct; /* AllocVec'd copy */
    char  *notifStatusId;  /* AllocVec'd copy */

    /* followersCount/followingCount/countsRowY: TTLProfileHeader_Class
     * only, unused (0) on every other row kind. following/followedBy are
     * shared with TTLAccountRow_Class too (see below). postId doubles as
     * this row's Mastodon *account* id on both of these row kinds -- a
     * different ID namespace than a toot's status id, but the same
     * "identifying string for this row" role. TTLPostUpdate/
     * TTIMELINE_UpdatePost only ever searches channel->posts, never
     * channel->headerPost, so a profile header's postId can never be
     * accidentally matched/patched by a TTL_POSTUPD_RELATIONSHIP update
     * meant for an account-row list item, even if the two ids coincide. */
    ULONG  followersCount;
    ULONG  followingCount;
    BOOL   following;   /* TTLProfileHeader_Class: connected user follows this
                          * account. TTLAccountRow_Class: connected user follows
                          * this row's account (currently unused by the
                          * "Follows you" badge, which only reads followedBy --
                          * see ttl_account_row_layout -- but patched in by
                          * TTL_POSTUPD_RELATIONSHIP regardless, for parity
                          * with followedBy and any future use). */
    BOOL   followedBy;  /* TTLAccountRow_Class only: this row's account follows
                          * the connected user -- drives the "Follows you"
                          * badge. Always FALSE on a profile header row (never
                          * patched there; the header uses its own singular
                          * FS3ENETQ_RELATIONSHIP/TTIMELINE_UpdateProfileFollow
                          * path instead). */
    WORD   countsRowY;     /* post-relative Y of the "N Followers   N Following" line,
                             * computed once by ttl_profile_header_layout and reused
                             * as-is by build_hotspots -- never re-derived, same
                             * "store once" rule as pollBlockY/threadRowY. */

    /* Media preview rect, in the same post-relative coordinates as
     * TTLTextSpan.postRelY; computed by ttl_post_layout. previewW==0 means
     * "no preview to draw" (mediaCount==0, or it didn't fit). */
    WORD   previewX, previewY, previewW, previewH;

    /* Link preview card -- see TTLPostSetup.hasCard's doc comment.
     * Independent of the media preview above (NOT mutually exclusive);
     * both can be present and stack in the same right-hand column (thumb
     * over card) when side-by-side, or one after another when stacked
     * narrow -- see ttl_toot_layout. cardW==0 means "no card to draw"
     * (hasCard FALSE). cardImgH is the image strip's own height within
     * the box (0 if the card has no image); computed once by
     * ttl_toot_layout, reused as-is by render/build_hotspots, same
     * "store once, never re-derive" rule as previewX/Y/W/H above. */
    BOOL   hasCard;
    char  *cardUrl, *cardTitle, *cardDescription, *cardProviderName, *cardImageUrl;
    WORD   cardX, cardY, cardW, cardH;
    WORD   cardImgH;

    /* Word-wrapped title/description, computed once by ttl_toot_layout via
     * FS3ETextWrap (same tool the toot body already uses) against cardW,
     * capped at TTL_CARD_TITLE_MAX_ROWS/TTL_CARD_DESC_MAX_ROWS rows -- any
     * further wrapped rows are simply not kept (truncation, not ellipsis).
     * Each entry is its own AllocVec'd, NUL-terminated copy (NOT a
     * TTLTextSpan/textSpans entry: card text isn't selectable, just drawn
     * directly by ttl_render_tile, so it doesn't need the hit-testing
     * machinery that list exists for). NULL past *LineCount. */
    char  *cardTitleLines[TTL_CARD_TITLE_MAX_ROWS];
    ULONG  cardTitleLineCount;
    char  *cardDescLines[TTL_CARD_DESC_MAX_ROWS];
    ULONG  cardDescLineCount;

    /* Embedded quote block -- see TTLPostSetup.hasQuote's doc comment.
     * quoteW==0 means "no quote block to draw" (hasQuote FALSE). Rect
     * (quoteX/Y/W/H) is post-relative, computed once by ttl_toot_layout,
     * reused as-is by render/build_hotspots -- same "store once" rule as
     * cardX/Y/W/H above. Unlike the card, width is NOT the fixed
     * TTL_CARD_BASE_W scale: it spans nearly this post's own full content
     * width (see ttl_toot_layout), since the block reads as "this post,
     * nested" rather than a side attachment. */
    BOOL   hasQuote;
    char  *quoteId, *quoteAuthorName, *quoteAuthorAcct, *quoteAvatarURL, *quoteBody, *quoteTimestamp;
    WORD   quoteX, quoteY, quoteW, quoteH;

    /* Word-wrapped quoteBody, computed once by ttl_toot_layout via
     * FS3ETextWrap against quoteW -- same "own AllocVec'd copies, not
     * textSpans" reasoning as cardTitleLines/cardDescLines above (not
     * selectable/hit-testable, just drawn). Unlike those two (small fixed
     * arrays), quoteBodyLines is itself AllocVec'd, sized to exactly
     * quoteBodyLineCount entries (FS3ETextWrap_Build reports the row count
     * up front, so no guessing/over-allocating is needed) -- capped only
     * at TTL_QUOTE_BODY_MAX_ROWS as a pathological-input safety net, not a
     * real truncation limit. NULL when quoteBodyLineCount==0. */
    char **quoteBodyLines;
    ULONG  quoteBodyLineCount;

    /* Poll ("survey"), closed/result rendering only -- see
     * TTLPostSetup.pollOptionTitles. pollOptionCount==0 means no poll on
     * this post; mutually exclusive with mediaCount>0 in practice (see
     * TTL_POST_MAX_POLL_OPTIONS comment). pollBlockY is the post-relative
     * Y of the poll block's top, computed once by ttl_toot_layout and
     * reused as-is by render/build_hotspots -- never re-derived, same
     * fix as the profile Follow-button-row bug. */
    char  *pollOptionTitles[TTL_POST_MAX_POLL_OPTIONS];
    ULONG  pollOptionVotes[TTL_POST_MAX_POLL_OPTIONS];
    ULONG  pollOptionCount;
    ULONG  pollVotesCount;
    BOOL   pollExpired;
    BOOL   pollMultiple;
    WORD   pollBlockY;

    /* "N replies" thread-indicator row (vertical bar + "...", see
     * TTL_HOT_THREAD) -- post-relative Y of its top, computed once by
     * ttl_toot_layout and reused as-is by render/build_hotspots, same
     * "store once, never re-derive" rule as pollBlockY just above. 0 when
     * repliesCount==0 (no row reserved at all in that case). */
    WORD   threadRowY;

    /* "Follow discussion up" row (see TTL_HOT_THREAD_UP) -- same "store
     * once" rule as threadRowY above, drawn ABOVE it when both are
     * present. 0 when !isReply (no row reserved). */
    WORD   threadUpRowY;

    /* Action bar's (Reply/Boost/Fave, +Modify/Delete for own toots) top Y,
     * post-relative, computed once by ttl_toot_layout and reused as-is by
     * render/build_hotspots -- same "store once" rule as pollBlockY/
     * threadRowY above. MUST be stored rather than re-derived from
     * post->height (the old "post->height - 1 - TTL_POST_PAD_BOT -
     * lineHeight" formula): that only worked back when the action bar was
     * always the very last thing before the closing pad/separator, which
     * stopped being true once the embedded quote block (see hasQuote) can
     * be appended below it -- re-deriving from the now-larger post->height
     * would place the action bar at the bottom of the quote block instead
     * of above it. */
    WORD   actionBarY;

    struct MinList  textSpans;    /* list of TTLTextSpan (wrapped body lines, for hit-testing/selection) */

    /* Hot-spots: NOT owned here -- see the TTLHotSpot comment above.
     * hotSpots is NULL, or a pointer into inst->hotSpotPool[hotSpotBucket]. */
    TTLHotSpot *hotSpots;
    UBYTE       hotSpotCount;
    WORD        hotSpotBucket;   /* -1 if no bucket currently assigned */
    BOOL        hotSpotsDirty;   /* TRUE = layout changed, must rebuild before use */
} TTLPost;

/* ------------------------------------------------------------------ */
/* TTLChannel — one independent post list (see TTIMELINE_NUM_VIEWMODES) */
/* ------------------------------------------------------------------ */

typedef struct {
    struct MinList posts;         /* newest = head */
    ULONG          postCount;

    LONG    scrollY;              /* timeline Y at gadget top */
    LONG    contentTopY;          /* Y of topmost post's top edge */
    LONG    contentBottomY;       /* Y one pixel past the last post's bottom edge */

    /* TRUE once the pinned "load older" row (TTLLoadOlder_Class, see
     * ttl_channel_add_boundaries) has already fired its proximity-
     * triggered TTL_HOT_LOAD_OLDER notify for the current bottom-of-list
     * position -- latches so GM_RENDER doesn't refire it every redraw
     * while the user stays scrolled to the bottom waiting for a reply.
     * Reset to FALSE whenever new older content actually lands (see
     * ttl_channel_insert_bottom), so the next arrival at the (new)
     * bottom can trigger again. */
    BOOL    olderLoadTriggered;

    /* TTLProfileHeader_Class instance pinned above this channel's post
     * list, or NULL. Deliberately NOT a member of `posts` (see
     * TTIMELINE_ShowProfile's comment in fs3etoottimeline.h) -- this is a
     * bare, separately-tracked pointer purely so the follow/unfollow
     * update path (TTIMELINE_UpdateProfileFollow) has O(1) access without
     * a list search. Every channel except Search leaves this NULL
     * forever; set by TTIMELINE_ShowProfile, cleared by ttl_clear_channel. */
    TTLPost *headerPost;
} TTLChannel;

/* ------------------------------------------------------------------ */
/* TTLData — per-instance INST_DATA block                               */
/* ------------------------------------------------------------------ */

typedef struct TTLData {
    /* Screen for AllocBitMap and colour mapping */
    struct Screen *screen;
    /* layout and draw are only allowed on the process that own the gadget */
    struct Task         *callerTask;

    /* BOOPSI gadget methods are not all guaranteed to run on the same
     * task: GM_HANDLEINPUT for an active gadget can be dispatched from
     * input.device's own task chain (see the comment at the top of
     * fs3etoottimeline_input.c), while TTIMELINE_AddPost/AppendPost and
     * GM_RENDER normally run on callerTask -- so a click's hit-test walk
     * can run concurrently, on a different task, with a post being
     * spliced into the very same channels[].posts list. Every place that
     * walks or mutates a channel's post list (insert/remove, tile
     * render's post loop, GM_LAYOUT's relayout, hit-testing, the
     * OldestPostId/NewestPostId GetAttr walks) must hold this for the
     * duration -- see ttl_apply_tags/TTL_OnGet/TTL_OnRender/
     * TTL_OnHandleInput. AmigaOS SignalSemaphores nest safely for the
     * same task re-obtaining, but locked sections here still release
     * before making any synchronous nested BOOPSI call (e.g. the
     * GM_RENDER kicked off at the end of ttl_apply_tags) to keep that
     * invariant simple rather than relying on it. */
    struct SignalSemaphore listSem;

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

    /* ---- Channels: one independent post list + scroll/content extent
     * per fs3eViewMode slot (see TTIMELINE_NUM_VIEWMODES). Only
     * channels[viewMode] is ever displayed/scrolled/tiled at a time --
     * see ttl_active() below. ---- */
    TTLChannel channels[TTIMELINE_NUM_VIEWMODES];

    /* ---- Hot-spot pool (see the TTLHotSpot comment in this header) ---- */
    TTLHotSpot  hotSpotPool[TTL_HOTSPOT_POOL_TOOTS][TTL_HOTSPOT_MAX_PER_TOOT];
    TTLPost    *hotSpotBucketOwner[TTL_HOTSPOT_POOL_TOOTS];  /* NULL = free */
    ULONG       hotSpotNextBucket;                           /* round-robin cursor */

    /* ---- Last hot-spot activation (see TTIMELINE_LastHotSpotString/
     * PostId and ttl_notify_hotspot() in fs3etoottimeline_tiles.c) ----
     * Owned copies, not just cached pointers: hs->data may point into a
     * TTLTextSpan's row buffer, which gets freed on the next relayout
     * (see the TTLHotSpot comment above); a post's own postId outlives
     * that, but not the post itself scrolling away and being freed. Since
     * "last activated" is meant to stay readable via GetAttr until the
     * *next* activation regardless of what happens to the post it came
     * from, ttl_notify_hotspot() copies both into these gadget-owned
     * buffers up front rather than handing out pointers into someone
     * else's memory.
     *
     * lastHotSpotStr is AllocVec'd, not a fixed buffer, same "unbounded"
     * reasoning as selectedText below: TTL_HOT_MODIFY carries the full
     * toot body here (see ttl_toot_build_hotspots() in
     * fs3etoottimeline_posts.c) so the compose window can prefill it for
     * editing, and Mastodon toot bodies have no fixed byte cap -- a fixed
     * buffer here previously truncated (silently, mid-UTF-8-sequence)
     * any body past its size, which multi-byte characters like emoji hit
     * far sooner than the visible character count would suggest. NULL =
     * no click yet, or the click carried no data (e.g. TTL_HOT_DELETE). */
    char  *lastHotSpotStr;
    char   lastHotSpotPostId[64];
    char   lastHotSpotMediaIds[96]; /* comma-joined, see TTIMELINE_LastHotSpotMediaIds --
                                      * 96 bytes comfortably covers TTL_POST_MAX_MEDIA (4)
                                      * ids at Mastodon's usual ~20-digit snowflake length */
    char   lastHotSpotAcct[128];    /* @user@instance, see TTIMELINE_LastHotSpotAcct --
                                      * same size as AVATAR_ACCT_SIZE (avatarimages.h) */
    char  *lastHotSpotAudioUrl;     /* see TTIMELINE_LastHotSpotAudioUrl -- AllocVec'd,
                                      * not a fixed buffer, same "a URL has no useful
                                      * fixed cap" reasoning as lastHotSpotStr above.
                                      * NULL = no click yet, or that attachment has no
                                      * separately-known real file URL. */

    /* ---- "Selected" toot for TTIMELINE_CopySelectedText (see its own
     * doc comment in fs3etoottimeline.h) ---- AllocVec'd owned copy of
     * the last-clicked post's body, not a fixed buffer like
     * lastHotSpotStr above: toot bodies are unbounded (Mastodon instances
     * can raise the default 500-char limit), and not a borrowed TTLPost*
     * either -- same "post can scroll away and be freed" reasoning as
     * lastHotSpotPostId. NULL = no click yet (TTIMELINE_CopySelectedText
     * then falls back to the topmost visible post). Set in
     * TTL_OnGoActive (fs3etoottimeline_input.c), read in
     * ttl_apply_tags (fs3etoottimeline_attribs.c), freed in
     * TTL_OnDispose.
     *
     * selectedAuthorName/selectedAuthorAcct are the same clicked post's
     * username/acct, captured as owned copies alongside selectedText at
     * the same button-down (same reasoning: the TTLPost itself can scroll
     * away and be freed before TTIMELINE_CopySelectedText is set) --
     * TTIMELINE_CopySelectedText prepends "<name>\n<acct>\n\n" to the body
     * using these. Either can be NULL (a pseudo post/header row has no
     * author) -- the caller substitutes "" rather than skipping the
     * line, so the body always lands on a predictable line. */
    char  *selectedText;
    char  *selectedAuthorName;
    char  *selectedAuthorAcct;

    /* ---- TTIMELINE_OldestPostId/NewestPostId (see TTL_OnGet) ----
     * Same "owned copy, not a borrowed pointer" reasoning as
     * lastHotSpotPostId just above: the old code found the post while
     * holding listSem, then released it and handed the CALLER a raw
     * pointer straight into that post's own postId field -- valid at the
     * moment of the copy, but with no guarantee it still is by the time
     * the caller actually reads through it (any other task's BOOPSI call
     * on this same gadget, e.g. another TTIMELINE_ShowProfile clearing
     * and freeing that very post, can run in between). Copying into a
     * gadget-owned buffer while still holding the semaphore closes that
     * window the same way ttl_notify_hotspot() already does. */
    char   lastOldestPostId[64];
    char   lastNewestPostId[64];

    /* ---- Text selection state ---- */
    TTLPost     *selPost;
    TTLTextSpan *selSpanA;
    TTLTextSpan *selSpanB;
    ULONG        selCharA;    /* char index within selSpanA */
    ULONG        selCharB;    /* char index within selSpanB */
    BOOL         hasSelection;

    /* ---- Drag / scroll input (written in HandleInput, consumed in Render) ---- */
    BOOL   dragActive;
    WORD   dragStartGadX;    /* gadget-relative X at button-down */
    WORD   dragStartGadY;    /* gadget-relative Y at button-down */
    LONG   dragStartScrollY;

    BOOL   pendingScroll;
    LONG   pendingScrollY;   /* new scrollY to apply in GM_RENDER */

    /* ---- TTIMELINE_ActionOnDoubleClick (see fs3etoottimeline.h) ---- */
    BOOL   actionOnDoubleClick;

    /* ---- Double-click gating for hot-spot activation (used only when
     * actionOnDoubleClick is TRUE, see TTL_OnHandleInput in
     * fs3etoottimeline_input.c) ---- Tracks the most recent qualifying
     * click (matched down/up hot-spot, not a drag) that did NOT activate
     * yet, purely to detect a second click on the same target within the
     * timing window DoubleClick() (intuition.library) checks. Identifies
     * the target by hot-spot type + a bounded copy of its data bytes +
     * the owning post's id, NOT by TTLPost pointer or hs->data pointer:
     * hs->data may point into a TTLTextSpan row buffer freed on the next
     * relayout (same reasoning as lastHotSpotStr above), and a post can
     * scroll away and be freed too -- both plausible in the gap between
     * two clicks of a double-click, unlike within a single click's own
     * down/up match. pendingClickArmed FALSE means "no candidate armed"
     * (can't just test pendingClickData[0] for that -- data is legitimately
     * NULL/empty for button-style hot-spots like Reply/Boost/Fave). */
    BOOL   pendingClickArmed;
    UBYTE  pendingClickType;
    char   pendingClickPostId[64]; /* same size as lastHotSpotPostId above */
    char   pendingClickData[256];  /* raw bytes, NOT NUL-terminated -- see
                                     * TTLHotSpot.data's own comment */
    ULONG  pendingClickDataLen;
    ULONG  pendingClickSecs;
    ULONG  pendingClickMicros;

    /* ---- Color theme (not owned; pointer set via TTIMELINE_Style) ---- */
    FS3EStyle *style;

    /* ---- Notification target (ICA workaround) ---- */
    Object *target;
    ULONG   ga_id;

    /* Do layout from right process and render function */
    ULONG layoutToDo;
    WORD  LastLayoutedWidth,LastLayoutedHeight;

    /* ---- Waiting screen (see TTIMELINE_ViewMode/WaitText) ----
     * See ttl_is_waiting() below: waiting is purely "the active channel's
     * post list is empty" -- switching to a channel that already has
     * posts shows them immediately, switching to an empty one waits. */
    ULONG viewMode;           /* index into channels[]; always < TTIMELINE_NUM_VIEWMODES */
    char *waitText;           /* AllocVec'd; NULL/empty -> ttl_render_wait's default */

    /* ---- Avatar bitmap cache (not owned; pointer set via TTIMELINE_AvatarImages) ---- */
    struct AvatarImages *avatarImages;

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

/* The currently displayed/scrolled channel (see TTIMELINE_ViewMode).
 * inst->viewMode is always kept < TTIMELINE_NUM_VIEWMODES (bounds-checked
 * where it's set), so this is always a valid slot. */
INLINE TTLChannel *ttl_active(TTLData *inst)
{
    return &inst->channels[inst->viewMode];
}

/* TRUE if the timeline should show the waiting screen instead of the
 * scrollable post list: the active channel's post list is empty. A
 * profile header (see TTLChannel.headerPost) counts as content -- the
 * TTIMELINE_ShowProfile handler seeds postCount=1 as soon as it's
 * inserted specifically so this is FALSE from that moment on, showing
 * the header immediately rather than a "waiting" placeholder while the
 * profile's own toots are still loading. */
INLINE BOOL ttl_is_waiting(TTLData *inst)
{
    return (BOOL)(ttl_active(inst)->postCount == 0);
}

/* Lowest scrollY a channel may be clamped to. contentTopY is where the
 * *list*'s own content starts (see ttl_rebuild_ypositions) -- with a
 * profile header (outside the list, see TTLChannel.headerPost) that's
 * header->height, not 0, so the true scrollable top is header->height
 * further up, letting the header itself scroll into view. Guarded/no-op
 * (returns contentTopY unchanged) for every channel without a header.
 * Shared by every scrollY clamp site so they can't drift apart. */
INLINE LONG ttl_channel_min_scroll(const TTLChannel *channel)
{
    return channel->contentTopY - (channel->headerPost ? channel->headerPost->height : 0);
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
TTLPost *ttl_pseudo_post_alloc (const TTLItemClass *cls, const char *label);
void     ttl_post_free         (TTLData *inst, TTLPost *post);
void     ttl_post_refresh_fields(TTLPost *post, const TTLPostSetup *setup);
void     ttl_layout_all_posts  (TTLData *inst);
void     ttl_clear_channel     (TTLData *inst, ULONG ch);
void     ttl_clear_posts       (TTLData *inst);
void     ttl_rebuild_ypositions(TTLData *inst, ULONG ch);
void     ttl_post_ensure_hotspots(TTLData *inst, TTLPost *post);

/* Layout/hot-spot helpers shared with fs3etoottimeline_profile.c's
 * profile header (name/acct/counts spans, word-wrapped bio, hot-spot
 * registration, and the @mention/#hashtag/URL scanner toot bodies
 * already use) -- see each function's own comment in fs3etoottimeline_posts.c. */
void         ttl_clear_textspans    (TTLPost *post);
LONG         ttl_count_wrapped_lines(TTLData *inst, const char *utf8, WORD maxW);
TTLTextSpan *ttl_span_alloc         (const char *utf8, UBYTE spanType,
                                      LONG postRelY, WORD x, TTLData *inst);
TTLTextSpan *ttl_span_from_row      (const FS3ETextRow *row, WORD x,
                                      LONG postRelY, TTLData *inst);
void         ttl_hs_add             (TTLPost *post, UBYTE type, WORD x, WORD y, WORD w, WORD h,
                                      const char *data, ULONG dataLen);
void         ttl_scan_span_tokens   (TTLPost *post, TTLTextSpan *sp);

/* Boundary-aware insertion, used by both TTIMELINE_AddPost (top) and
 * TTIMELINE_AppendPost (bottom) -- lands real content between a channel's
 * pinned TTLLoadNewer_Class/TTLLoadOlder_Class rows and the rest, instead
 * of ever ending up above/below them. Caller must guarantee the channel's
 * post list is already non-empty (i.e. not the very-first-post bootstrap
 * case, which anchors at Y=0 itself -- see TTIMELINE_AddPost). */
void     ttl_channel_insert_top   (TTLData *inst, TTLChannel *channel, TTLPost *post);
void     ttl_channel_insert_bottom(TTLData *inst, TTLChannel *channel, TTLPost *post);
/* Pin the "look for something new" / "load more" rows around a channel's
 * first-ever real post, right after it's been added. No-op fields left at
 * zero/NULL are fine to skip on allocation failure -- see the .c file. */
void     ttl_channel_add_boundaries(TTLData *inst, TTLChannel *channel);

/* TTLToot_Class member functions that live outside posts.c (the file that
 * defines the TTLToot_Class table itself) -- see the TTLItemClass comment
 * above for what each does. */
extern const TTLItemClass TTLToot_Class;
void     ttl_toot_render  (TTLData *inst, struct RastPort *rp,
                           TTLPost *item, LONG tileBaseY);          /* fs3etoottimeline_tiles.c */

/* Missing-avatar placeholder: solid accentPen square with an inset,
 * unfilled bgPen rectangle frame -- shared by ttl_toot_render above,
 * ttl_notif_follow_render (fs3etoottimeline_notif.c) and
 * ttl_profile_header_render (fs3etoottimeline_profile.c). */
void     ttl_draw_avatar_placeholder(struct RastPort *rp, WORD ax, WORD ay, WORD as,
                                     LONG accentPen, LONG bgPen);    /* fs3etoottimeline_tiles.c */
void     ttl_toot_activate(TTLData *inst, Class *cl, Object *o,
                           struct GadgetInfo *gi, TTLPost *item,
                           TTLHotSpot *hs);                         /* fs3etoottimeline_input.c */

/* Builds this post's 3 action-bar label strings (Reply/Boost/Fave, each
 * with its count; Fave's glyph toggles empty/full star by post->favourited)
 * into caller-owned buffers -- the single shared source both
 * ttl_post_build_hotspots (fs3etoottimeline_posts.c, sizes the hot-spot
 * rects) and ttl_toot_render above (draws them) read from, so a click
 * always lines up with what's on screen. */
void     ttl_build_action_labels(const TTLPost *post,
                                  char labels[3][TTL_ACTION_LABEL_MAX]); /* fs3etoottimeline_tiles.c */

/* Pinned boundary-row classes (see ttl_channel_add_boundaries). Both share
 * ttl_toot_render's sibling ttl_boundary_render (fs3etoottimeline_tiles.c)
 * -- a centered single-line label drawn from post->body. Neither needs
 * .activate: TTLLoadNewer_Class's one hot-spot (TTL_HOT_LOAD_NEWER) is
 * enough on its own via the generic ttl_notify_hotspot() every hot-spot
 * click already fires; TTLLoadOlder_Class has no hot-spot at all -- it is
 * triggered by scroll proximity in TTL_OnRender instead (see
 * fs3etoottimeline_render.c), which calls ttl_notify_hotspot() directly. */
extern const TTLItemClass TTLLoadNewer_Class;
extern const TTLItemClass TTLLoadOlder_Class;
void     ttl_boundary_render(TTLData *inst, struct RastPort *rp,
                              TTLPost *item, LONG tileBaseY);       /* fs3etoottimeline_tiles.c */

/* Profile header row (see TTIMELINE_ShowProfile) -- deliberately NOT
 * pinned via ttl_channel_add_boundaries/insert_top like the two classes
 * above: it lives outside `channel->posts` entirely (TTLChannel.headerPost),
 * seeded directly by the TTIMELINE_ShowProfile handler. Defined in the new
 * fs3etoottimeline_profile.c. */
extern const TTLItemClass TTLProfileHeader_Class;
TTLPost *ttl_profile_header_alloc(const TTLProfileHeaderSetup *setup); /* fs3etoottimeline_profile.c */

/* Account-only notification row (FOLLOW/FOLLOW_REQUEST -- see
 * TTLPostSetup.notifType) -- unlike TTLProfileHeader_Class above, this IS
 * an ordinary member of channel->posts, added via TTIMELINE_AddPost/
 * AppendPost like a toot, just via this class pointer instead of
 * TTLToot_Class. Defined in the new fs3etoottimeline_notif.c. */
extern const TTLItemClass TTLNotifFollow_Class;
TTLPost *ttl_notif_follow_alloc(const TTLPostSetup *setup); /* fs3etoottimeline_notif.c */

extern const TTLItemClass TTLAccountRow_Class;
TTLPost *ttl_account_row_alloc(const TTLPostSetup *setup); /* fs3etoottimeline_accountrow.c */

/* Pinned "Followers for @user" / "Followed by @user" title row -- see
 * TTLPostSetup.isListTitle. IS an ordinary member of channel->posts (same
 * as TTLNotifFollow_Class above), unlike TTLProfileHeader_Class's
 * separate headerPost slot -- it's meant to scroll away with the list
 * like any other row, not stay pinned outside it. Defined alongside
 * TTLLoadNewer_Class/TTLLoadOlder_Class in fs3etoottimeline_posts.c,
 * whose ttl_boundary_layout/render it reuses. */
extern const TTLItemClass TTLListTitle_Class;
TTLPost *ttl_list_title_alloc(const TTLPostSetup *setup); /* fs3etoottimeline_posts.c */

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
/* Richer variant for hot-spot activation: copies data/postId into the
 * gadget's owned lastHotSpotStr/lastHotSpotPostId buffers (see the
 * TTLData comment), then sends one OM_UPDATE carrying
 * TTIMELINE_HotSpotNotify=type plus both buffer pointers as additional
 * tags. data/dataLen may be NULL/0 (hot-spot types with no string);
 * postId may be NULL ("" or unknown status id). favourited is carried as
 * TTIMELINE_LastHotSpotFavourited -- pass FALSE when postId is NULL (no
 * owning post to have a favourited state). following is carried as
 * TTIMELINE_LastHotSpotFollowing the same way, for TTL_HOT_FOLLOW clicks
 * on a profile header -- pass FALSE for every other item kind. reblogged is
 * carried as TTIMELINE_LastHotSpotReblogged the same way, for TTL_HOT_BOOST
 * clicks -- pass FALSE for every other item kind. quotable is carried as
 * TTIMELINE_LastHotSpotQuotable the same way, also for TTL_HOT_BOOST
 * clicks. mediaIds is
 * copied into lastHotSpotMediaIds and carried as
 * TTIMELINE_LastHotSpotMediaIds the same way as data/postId -- NULL for
 * every item kind but a toot (see TTLPost.mediaIdsJoined). acct is copied
 * into lastHotSpotAcct and carried as TTIMELINE_LastHotSpotAcct the same
 * way -- the owning post's original author @user@instance (TTLPost.acct),
 * or NULL/"" if unknown; currently only meaningful for TTL_HOT_IMAGE (see
 * fs3emediaview.c's "Save Media..." filename), but populated for every
 * item kind that has a post since it costs nothing extra. audioUrl is
 * copied into lastHotSpotAudioUrl and carried as
 * TTIMELINE_LastHotSpotAudioUrl the same way -- the owning post's
 * mediaAudioUrls[mediaCurrentIndex] (TTLPost.mediaAudioUrls), or NULL/""
 * if unknown; only meaningful for TTL_HOT_PLAY_AUDIO on a slot that also
 * has cover art (see TTLPostSetup.mediaAudioUrls' own doc comment). */
void     ttl_notify_hotspot(Class *cl, Object *o, struct GadgetInfo *gi,
                             UBYTE type, const char *data, ULONG dataLen,
                             const char *postId, BOOL favourited, BOOL following,
                             BOOL reblogged, BOOL quotable,
                             const char *mediaIds, const char *acct,
                             const char *audioUrl);
/* only process have right to send render, ask with notify ProcessREfresh
 * void     ttl_render_self  (Class *cl, Object *o, struct GadgetInfo *gi);
*/
#endif /* FS3ETOOTTIMELINE_PRIVATE_H */
