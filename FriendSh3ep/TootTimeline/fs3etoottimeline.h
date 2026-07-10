#ifndef FS3ETOOTTIMELINE_H
#define FS3ETOOTTIMELINE_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include "../fs3estyle.h"

/* ------------------------------------------------------------------ */
/* Tag base and attribute tags                                          */
/* ------------------------------------------------------------------ */

#define TTIMELINE_Base      (TAG_USER | 0x53530UL)

/* Number of parallel post lists ("channels"), one per fs3eViewMode value
 * (see friendsh3ep.h) -- TTIMELINE_ViewMode selects which one is
 * currently displayed/scrolled, and TTLPostSetup.viewModeBits (bit i =
 * channel i) selects which one(s) a given TTIMELINE_AddPost targets, so
 * one post can appear in more than one channel. Must match fs3eViewMode's
 * VIEWMODE_NumberOf. */
#define TTIMELINE_NUM_VIEWMODES 8
/* Channel index TTIMELINE_ShowProfile/TTIMELINE_UpdateProfileFollow
 * always target -- must match fs3eViewMode's VIEWMODE_Search (see
 * friendsh3ep.h), same "must match" convention as TTIMELINE_NUM_VIEWMODES
 * above. TootTimeline stays self-contained (no friendsh3ep.h include),
 * so this is a documented numeric coupling, not a shared symbol. */
#define TTL_SEARCH_CHANNEL 4

/* [IS] UWORD: row-height DPI factor (default 14) */
#define TTIMELINE_DpiHeight      (TTIMELINE_Base + 0)
/* [ISG] LONG: timeline Y currently at gadget top (scroll position) */
#define TTIMELINE_ScrollY        (TTIMELINE_Base + 1)
/* [G]  LONG: timeline Y of the top of the topmost post */
#define TTIMELINE_ContentTopY    (TTIMELINE_Base + 2)
/* [G]  LONG: timeline Y one pixel past the bottom of the last post */
#define TTIMELINE_ContentBottomY (TTIMELINE_Base + 3)
/* [IS] struct Screen*: screen for AllocBitMap and colour map */
/* took from drawinfo #define TTIMELINE_Screen         (TTIMELINE_Base + 5)*/
/* [S]  TTLPostSetup*: prepend a new post at the top of the timeline,
 * landing just below the pinned "Look for something new" row if the
 * channel already has one (see TTLLoadNewer_Class) -- real content never
 * ends up above it. */
#define TTIMELINE_AddPost        (TTIMELINE_Base + 6)
/* [S]  any: remove all posts and free resources */
#define TTIMELINE_ClearPosts     (TTIMELINE_Base + 7)
/* [IS] FS3EStyle*: color theme; gadget keeps the pointer, does not own it */
#define TTIMELINE_Style          (TTIMELINE_Base + 8)
/* [IS] ULONG: fs3eViewMode value (see friendsh3ep.h), 0..TTIMELINE_NUM_VIEWMODES-1
 * (out-of-range values are ignored). Selects which channel's post list is
 * displayed/scrolled. Setting this always enters "waiting" mode -- see
 * ttl_is_waiting() in fs3etoottimeline_private.h for the exact rule.
 * Waiting mode shows TTIMELINE_Style's waitImage + TTIMELINE_WaitText
 * centered instead of the scrollable post list, and disables scroll
 * input. Cleared by the next TTIMELINE_AddPost that targets this channel
 * (or immediately, if that channel's post list is already non-empty). */
#define TTIMELINE_ViewMode       (TTIMELINE_Base + 9)
/* [IS] STRPTR: UTF-8 "waiting" sentence drawn (in the normal body font)
 * below the waitImage in waiting mode. Gadget copies the string. Falls
 * back to a built-in default if never set / set to NULL. */
#define TTIMELINE_WaitText       (TTIMELINE_Base + 10)
/* [IS] AvatarImages*: avatar bitmap cache; gadget reads from it during render */
#define TTIMELINE_AvatarImages   (TTIMELINE_Base + 11)
/* [S] any: an avatar or media preview image just became ready in the
 * cache (AvatarImages_ThumbReady() or equivalent) -- invalidates every
 * currently-active tile bitmap so the next render redraws them with the
 * now-available image, replacing whatever placeholder they drew before it
 * arrived. Cheap: does not touch post layout/heights, and only tiles that
 * are actually in the current warm (visible+buffered) window get
 * re-rendered -- see ttl_tiles_invalidate_all(). Callers should send this
 * exactly once per "an image arrived" event (e.g. from the thumbnail
 * process reply handler), not poll the cache on a timer. */
#define TTIMELINE_InvalidateImages (TTIMELINE_Base + 12)
/* [G] STRPTR: hs->data from the most recently activated hot-spot, copied
 * into a persistent buffer the gadget owns (valid until the next
 * activation overwrites it) -- NULL for hot-spot types that carry no
 * string (Reply/Boost/Fave, media prev/next arrows). The same pointer is
 * also carried as this tag's value in the TTIMELINE_HotSpotNotify
 * notification, so a listener normally just reads it off that taglist
 * rather than calling GetAttr separately -- this exists for callers that
 * want it outside of handling that specific notify. */
#define TTIMELINE_LastHotSpotString  (TTIMELINE_Base + 13)
/* [G] STRPTR: Mastodon status id (see TTLPostSetup.postId) of the post
 * the most recently activated hot-spot belongs to, or NULL if that post
 * had none. Same buffer/pointer as the accompanying
 * TTIMELINE_HotSpotNotify notification's TTIMELINE_LastHotSpotPostId tag. */
#define TTIMELINE_LastHotSpotPostId  (TTIMELINE_Base + 14)
/* [G] STRPTR: Mastodon status id (see TTLPostSetup.postId) of the active
 * channel's newest real post -- skips any non-toot pinned boundary row
 * (e.g. a "look for something new" item, which has no postId; see
 * TTLItemClass in fs3etoottimeline_private.h). NULL if the channel has no
 * post with a known id yet. Read this right before paginating newer
 * (min_id=this value) so the request is contiguous with what's already
 * loaded. */
#define TTIMELINE_NewestPostId       (TTIMELINE_Base + 15)
/* [G] STRPTR: same, but the active channel's oldest real post (skips a
 * "load more" pinned row at the bottom). Read this right before
 * paginating older (max_id=this value). */
#define TTIMELINE_OldestPostId       (TTIMELINE_Base + 16)
/* [S] TTLPostSetup*: append a new post at the bottom of the timeline
 * (mirrors TTIMELINE_AddPost's prepend-at-top), landing just above the
 * pinned "Load more…" row if the channel already has one -- used for
 * older-page pagination results, which are contiguous with (i.e. arrive
 * immediately below) whatever the channel already has at its bottom. */
#define TTIMELINE_AppendPost         (TTIMELINE_Base + 17)
/* [S] any: jump the active channel's scroll position to its newest post
 * (scrollY = contentTopY) -- for opening a channel already showing the
 * most recent content instead of whatever it was last scrolled to.
 * Pagination (AddPost/AppendPost past the very first post) never moves
 * scrollY on its own, so callers should only send this right after a
 * channel's first-ever page of posts lands. */
#define TTIMELINE_ScrollToNewest     (TTIMELINE_Base + 18)
/* [S] TTLPostUpdate*: a status just changed on the server (Fave/Boost
 * toggle reply, or any future live update) -- every channel's copy of the
 * post with a matching postId (TTLPostUpdate.postId) is updated, and its
 * tile is invalidated for redraw if the channel holding it is currently
 * active. Silently a no-op if no channel currently has that postId (the
 * post scrolled out and got evicted, the reply raced a ClearPosts, ...).
 * See TTLPostUpdate/TTL_POSTUPD_* below. */
#define TTIMELINE_UpdatePost         (TTIMELINE_Base + 19)
/* [S] TTLProfileHeaderSetup*: show a user profile in the Search channel --
 * clears the Search channel and seeds it with a pinned header (avatar,
 * display name, bio, follower/following counts, Follow/Unfollow button)
 * followed by that user's own toots as a normal paginated list. Always
 * targets the Search channel specifically (fs3eViewMode's VIEWMODE_Search)
 * -- unlike TTLPostSetup.viewModeBits, there is no multi-channel option,
 * since a profile view is inherently single-channel. The header is NOT a
 * member of the channel's post list (so it can never be displaced by
 * pagination/insert logic shared with every other channel) -- see
 * TTLChannel.headerPost in fs3etoottimeline_private.h. */
#define TTIMELINE_ShowProfile        (TTIMELINE_Base + 26)
/* [S] TTLProfileFollowUpdate*: the Search channel's current profile
 * header's following state (and, on a real transition, its
 * followersCount by a local +1/-1 delta -- same reasoning as
 * TTL_POSTUPD_FAVOURITED) just changed on the server (a Relationship
 * fetch or a Follow/Unfollow reply). Silent no-op if the Search channel
 * has no header right now, or its header is for a different account id
 * than accountId (a stale reply racing a new profile being opened). */
#define TTIMELINE_UpdateProfileFollow (TTIMELINE_Base + 27)

/* ------------------------------------------------------------------ */
/* Notification tags  (sent to ICA_TARGET via OM_NOTIFY)               */
/* ------------------------------------------------------------------ */

/* Scroll domain changed (posts were prepended); value = new contentTopY */
#define TTIMELINE_ScrollDomainChanged (TTIMELINE_Base + 20)
/* User clicked a post; value = post index (0 = topmost) */
#define TTIMELINE_PostClicked         (TTIMELINE_Base + 21)
/* Superseded by TTIMELINE_HotSpotNotify: value was (ULONG)TTLHotSpot*, an
 * internal pointer external code had no safe way to actually use. No
 * longer sent. */
#define TTIMELINE_HotSpotActivated    (TTIMELINE_Base + 22)
/* User activated a hot-spot; value = hs->type (TTL_HOT_*). The same
 * OM_NOTIFY's tag list also carries TTIMELINE_LastHotSpotString (the
 * hot-spot's data string, e.g. an @handle/#tag/URL, or NULL) and
 * TTIMELINE_LastHotSpotPostId (that post's Mastodon status id, or NULL)
 * -- read them via FindTagItem on the notify message rather than a
 * separate GetAttr call. See ttl_notify_hotspot() in
 * fs3etoottimeline_tiles.c. */
#define TTIMELINE_HotSpotNotify       (TTIMELINE_Base + 24)
/* [G] BOOL: same OM_NOTIFY tag list as TTIMELINE_HotSpotNotify above --
 * TRUE if the post the just-activated hot-spot belongs to is currently
 * favourited by the connected user. Meaningless (FALSE) for hot-spot
 * types with no owning post (TTL_HOT_LOAD_NEWER/OLDER) or that aren't
 * about a favourite state at all -- lets a TTL_HOT_FAVORITE handler
 * decide POST .../favourite vs .../unfavourite without a separate
 * lookup back into the post list. */
#define TTIMELINE_LastHotSpotFavourited (TTIMELINE_Base + 25)
/* [G] BOOL: same OM_NOTIFY tag list as TTIMELINE_HotSpotNotify -- TRUE if
 * the *profile header's* own account is currently followed by the
 * connected user, at the moment its TTL_HOT_FOLLOW hot-spot was clicked.
 * Meaningless (FALSE) for every other hot-spot type/owning post -- lets a
 * TTL_HOT_FOLLOW handler decide POST .../follow vs .../unfollow without a
 * separate lookup, same reasoning as TTIMELINE_LastHotSpotFavourited. */
#define TTIMELINE_LastHotSpotFollowing  (TTIMELINE_Base + 28)
/* Ask full redraw from correct process */
#define TTIMELINE_ProcessRefresh        (TTIMELINE_Base + 23)


/* ------------------------------------------------------------------ */
/* Post content descriptor  (passed via TTIMELINE_AddPost)             */
/* The gadget copies all strings; the caller owns the struct.          */
/* ------------------------------------------------------------------ */

/* Max media attachments a post carries a preview URL for (matches
 * FS3ENET_MAX_MEDIA / Mastodon's own 4-attachment cap). mediaCount>1
 * gets prev/next arrow hot-spots so the user can browse them one at a
 * time within a single preview rect -- see TTL_HOT_MEDIA_PREV/NEXT. */
#define TTL_POST_MAX_MEDIA 4

/* Parallels network_fs3e/fs3enet.h's enum FS3ENetMediaKind, but kept as
 * its own small set of defines rather than a shared header: TootTimeline
 * is a self-contained BOOPSI class that doesn't include fs3enet.h.
 * friendsh3ep.c sits at the boundary and maps one to the other when
 * building a TTLPostSetup from an FS3ENetStatus, same as it already does
 * for every other field. TTL_MEDIA_KIND_AUDIO gets a "play" hot-spot
 * (TTL_HOT_PLAY_AUDIO) in the preview rect instead of a fetched thumbnail
 * -- see ttl_toot_build_hotspots. */
#define TTL_MEDIA_KIND_IMAGE   0
#define TTL_MEDIA_KIND_VIDEO   1
#define TTL_MEDIA_KIND_GIFV    2
#define TTL_MEDIA_KIND_AUDIO   3
#define TTL_MEDIA_KIND_UNKNOWN 4

/* Max poll ("survey") options a post carries (matches
 * FS3ENET_MAX_POLL_OPTIONS / Mastodon's own cap). pollOptionCount==0
 * means the post has no poll. Mastodon disallows a status having both
 * a poll and media_attachments, so mediaCount/pollOptionCount are
 * mutually exclusive in practice -- the layout treats them as
 * alternatives, never both. Only CLOSED/result rendering is
 * implemented for now (bars + percentages); voting on an open poll
 * is a later session's work, no TTL_HOT_ type exists for it yet. */
#define TTL_POST_MAX_POLL_OPTIONS 8

typedef struct TTLPostSetup {
    const char *username;    /* original author display name (UTF-8) */
    const char *acct;        /* original author @user@instance (UTF-8) */
    const char *body;        /* post body text (UTF-8) */
    const char *timestamp;   /* short age string, e.g. "3h" (UTF-8) */
    const char *boostBy;     /* booster display name, NULL/"" for originals */
    const char *boostByAcct; /* booster @user@instance, NULL/"" for originals -- what
                               * clicking the "X boosted" line actually opens (see
                               * TTL_HOT_AVATAR on TTL_SPAN_BOOSTBY) */
    const char *avatarURL;   /* CDN URL of original author's avatar */
    const char *postId;      /* Mastodon status id string, for hot-spot activation
                               * notifications (TTL_HOT_REPLY/BOOST/FAVORITE need to
                               * know which status to act on) -- see
                               * TTIMELINE_LastHotSpotPostId. NULL/"" if unknown. */
    const char *mediaUrls[TTL_POST_MAX_MEDIA]; /* attachment preview URLs;
                               * NULL past mediaCount. Gadget copies each
                               * string and drives its own fetch/thumbnail/
                               * draw pipeline the same way it does for
                               * avatars -- see AvatarImages_GetMedia. */
    ULONG       mediaKinds[TTL_POST_MAX_MEDIA]; /* TTL_MEDIA_KIND_* per slot,
                               * same indexing as mediaUrls. */
    ULONG       mediaCount;   /* 0..TTL_POST_MAX_MEDIA; 0 = no preview rect */
    ULONG       viewModeBits; /* bit i set = also prepend to channel i (see
                                * TTIMELINE_NUM_VIEWMODES); a post can be
                                * added to more than one channel at once,
                                * as an independent copy per channel. */

    /* Action-bar counts/state, shown next to the Reply/Boost/Fave buttons
     * and (favourited) toggling the Fave glyph between empty/full star --
     * see ttl_build_action_labels() in fs3etoottimeline_tiles.c. */
    ULONG       repliesCount;
    ULONG       reblogsCount;
    ULONG       favouritesCount;
    BOOL        favourited;
    BOOL        reblogged;

    /* Poll ("survey"), closed/result rendering only -- see
     * TTL_POST_MAX_POLL_OPTIONS above. pollOptionCount==0 = no poll. */
    const char *pollOptionTitles[TTL_POST_MAX_POLL_OPTIONS];
    ULONG       pollOptionVotes[TTL_POST_MAX_POLL_OPTIONS];
    ULONG       pollOptionCount;
    ULONG       pollVotesCount;
    BOOL        pollExpired;
    BOOL        pollMultiple;
} TTLPostSetup;

/* ------------------------------------------------------------------ */
/* Post state update descriptor (passed via TTIMELINE_UpdatePost)      */
/* The gadget does not copy/own postId -- it's only read for the       */
/* duration of the SetAttrs call, same lifetime rule as TTLPostSetup's */
/* strings.                                                            */
/*                                                                      */
/* flags says which of favourited/reblogged this update actually       */
/* carries -- a caller only ever knows for certain the ONE state its    */
/* own action's endpoint just confirmed (e.g. a favourite/unfavourite   */
/* reply confirms favourited, nothing about reblogged), and must not    */
/* blindly touch the other. The corresponding count (favouritesCount/   */
/* reblogsCount) is NOT part of this struct: the gadget derives it      */
/* itself as a +1/-1 delta on the post's own current count when the     */
/* boolean actually flips, rather than trusting a server-echoed number  */
/* that (see FS3EMastodon_Favourite's comment) isn't reliably present   */
/* on every instance's response -- copying it verbatim once silently    */
/* zeroed this toot's OTHER counts on every favourite toggle. */
/* ------------------------------------------------------------------ */

#define TTL_POSTUPD_FAVOURITED (1UL << 0) /* apply favourited + delta favouritesCount by ±1 */
#define TTL_POSTUPD_REBLOGGED  (1UL << 1) /* apply reblogged  + delta reblogsCount  by ±1 */

typedef struct TTLPostUpdate {
    const char *postId;      /* which post (every channel's copy is updated) */
    ULONG       flags;       /* TTL_POSTUPD_* -- which fields below to apply */
    BOOL        favourited;  /* new state; used iff flags & TTL_POSTUPD_FAVOURITED */
    BOOL        reblogged;   /* new state; used iff flags & TTL_POSTUPD_REBLOGGED */
} TTLPostUpdate;

/* ------------------------------------------------------------------ */
/* Profile header descriptor (passed via TTIMELINE_ShowProfile)        */
/* The gadget copies every string; the caller owns the struct, same    */
/* lifetime rule as TTLPostSetup.                                      */
/* ------------------------------------------------------------------ */

typedef struct TTLProfileHeaderSetup {
    const char *accountId;    /* Mastodon account id -- see TTIMELINE_UpdateProfileFollow */
    const char *username;     /* display name (UTF-8) */
    const char *acct;         /* "@user@instance" (UTF-8) */
    const char *avatarURL;    /* CDN URL, same fetch/cache pipeline as a toot's avatar */
    const char *bio;          /* HTML-already-stripped bio text (UTF-8), word-wrapped and
                                * scanned for @mention/#hashtag/URL hot-spots exactly like
                                * a toot body */
    ULONG       followersCount;
    ULONG       followingCount;
    BOOL        following;    /* connected user already follows this account */
    BOOL        showFollow;   /* FALSE hides the Follow/Unfollow hot-spot entirely --
                                * for viewing your own profile, where following yourself
                                * makes no sense */
} TTLProfileHeaderSetup;

/* ------------------------------------------------------------------ */
/* Profile-header follow-state update (passed via                      */
/* TTIMELINE_UpdateProfileFollow) -- see that tag's comment.            */
/* ------------------------------------------------------------------ */

typedef struct TTLProfileFollowUpdate {
    const char *accountId;   /* must match the current header's account, else no-op */
    BOOL        following;   /* new state */
} TTLProfileFollowUpdate;

/* ------------------------------------------------------------------ */
/* Hot-spot types  (forwarded in TTLHotSpotActivated notification)     */
/*                                                                      */
/* Single authoritative list -- used to live split across this header  */
/* and fs3etoottimeline_private.h with two different numeric mappings  */
/* for the same names; that footgun is gone now, this is the only      */
/* definition. */
/* ------------------------------------------------------------------ */

#define TTL_HOT_AVATAR      0  /* avatar image, or the @handle line -- opens the author's profile (data = "@user@instance") */
#define TTL_HOT_MENTION     1  /* @mention token inside the body text (data = "@handle" as it appears in the text) */
#define TTL_HOT_HASHTAG     2  /* #hashtag token inside the body text (data = "#tag") */
#define TTL_HOT_URL         3  /* http(s):// URL inside the body text (data = the URL) */
#define TTL_HOT_IMAGE       4  /* media preview rectangle (see TTLPostSetup.mediaUrls/mediaCount) */
#define TTL_HOT_REPLY       5
#define TTL_HOT_BOOST       6
#define TTL_HOT_FAVORITE    7
#define TTL_HOT_MEDIA_PREV  8  /* left-arrow zone inside the preview rect; only present when mediaCount>1 */
#define TTL_HOT_MEDIA_NEXT  9  /* right-arrow zone inside the preview rect; only present when mediaCount>1 */
#define TTL_HOT_LOAD_NEWER  10 /* pinned "Look for something new" row was clicked; data/postId are NULL */
#define TTL_HOT_LOAD_OLDER  11 /* pinned "Load more…" row reached the bottom of the viewport (or was
                                 * clicked, if it ever grows a hot-spot later); data/postId are NULL */
#define TTL_HOT_PLAY_AUDIO  12 /* preview rect for an audio attachment (TTL_MEDIA_KIND_AUDIO) -- no
                                 * thumbnail is ever fetched for these; data = the attachment URL */
#define TTL_HOT_FOLLOW      13 /* profile header's Follow/Unfollow label; data/postId are NULL --
                                 * see TTIMELINE_LastHotSpotFollowing for the account's current state */

/* Opaque handle; cast to TTLHotSpot* from private header if needed */
typedef struct TTLHotSpot TTLHotSpot;

/* ------------------------------------------------------------------ */
/* Class pointer and lifecycle                                          */
/* ------------------------------------------------------------------ */

extern Class *TootTimelineClass;

int  TootTimeline_Init(void);
void TootTimeline_Exit(void);

#endif /* FS3ETOOTTIMELINE_H */
