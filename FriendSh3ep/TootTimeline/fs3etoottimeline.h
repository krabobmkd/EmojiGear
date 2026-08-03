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
/* [S] LONG: scroll the active channel by this many pixels, relative to
 * its current position -- negative moves up/toward newer content,
 * positive moves down/toward older content. Meant for wheel-mouse
 * handling (see WMHI_RAWKEY keys 0x7A/0x7B in friendsh3ep.c): each notch
 * sends one small SetAttrs rather than the caller having to read
 * TTIMELINE_ScrollY first and compute an absolute target itself. Shares
 * TTIMELINE_ScrollY's deferred-apply mechanism (applied/clamped at the
 * next GM_RENDER, see inst->pendingScrollY) and is based on whatever that
 * pending value already is, so several ScrollBy calls in a row (a fast
 * wheel spin, more than one notch before a render happens) accumulate
 * correctly instead of each one reading a stale already-applied position. */
#define TTIMELINE_ScrollBy       (TTIMELINE_Base + 35)
/* [G] LONG: pixel scroll distance needed to bring the NEXT post (the one
 * right after whichever post is currently first visible -- i.e. the
 * topmost post at least partially inside the viewport, its own top may
 * already be scrolled past) up to the very top of the gadget, its own
 * top edge landing exactly on scrollY -- i.e. currentScrollY + this
 * value is the target for TTIMELINE_ScrollY (and, deliberately, the
 * new scrollY simply equals that next post's own timelineY). 0 if
 * there's no such post (the first-visible post is already the last one,
 * or the channel is empty). Walks the active channel's post list
 * top-to-bottom (posts are already Y-ordered): first finds the topmost
 * post whose bottom edge is still below scrollY (the "first visible"
 * one, per above), then takes ITS successor -- cheap for realistic
 * timeline lengths, and only ever queried once per "Next toot" press,
 * not per frame. Purely a "how far" query -- TootTimeline itself never
 * animates the scroll; see Action_TimelineNextToot (fs3eaction.c) for
 * the external driver (an FS3ETimer, fs3etimer.h) that reads this once,
 * then eases scrollY there over a fixed duration via repeated
 * TTIMELINE_ScrollY sets. */
#define TTIMELINE_NextTootScrollDelta (TTIMELINE_Base + 36)
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
/* [G] STRPTR: comma-joined media_attachments[].id list (see
 * TTLPostSetup.mediaIds) of the post the most recently activated hot-spot
 * belongs to, or NULL/"" if that post has no attachments. Same buffer/
 * pointer as the accompanying TTIMELINE_HotSpotNotify notification's tag of
 * the same name -- currently only meaningful for TTL_HOT_MODIFY, which
 * needs these ids to resend as media_ids[] on a PUT edit so existing
 * attachments survive a text-only edit (see FS3ENetStatus.fmas_MediaIds). */
#define TTIMELINE_LastHotSpotMediaIds (TTIMELINE_Base + 30)
/* [G] STRPTR: original author @user@instance (see TTLPostSetup.acct) of
 * the post the most recently activated hot-spot belongs to, or NULL/"" if
 * unknown. Same buffer/pointer as the accompanying TTIMELINE_HotSpotNotify
 * notification's tag of the same name -- currently only read for
 * TTL_HOT_IMAGE, to build a meaningful default filename for "Save
 * Media..." (see fs3emediaview.c). */
#define TTIMELINE_LastHotSpotAcct     (TTIMELINE_Base + 32)
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
/* [S] any: jump the active channel's scroll position to the very top --
 * scrollY = ttl_channel_min_scroll(active), i.e. 0 (showing the pinned
 * header, see TTIMELINE_ShowProfile) if the channel has one, else
 * contentTopY (skipping the pinned "look for something new" row, if
 * any, straight to the newest real post) -- for opening a channel
 * already showing the most recent content instead of whatever it was
 * last scrolled to, or for an explicit "Move to Top" jump (see
 * Action_TimelineTop, fs3eaction.c) once a channel is already showing
 * content. Pagination (AddPost/AppendPost past the very first post)
 * never moves scrollY on its own. */
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
/* [S] any: like TTIMELINE_ClearPosts, but every channel (0..
 * TTIMELINE_NUM_VIEWMODES-1), not just the currently active one -- for
 * switching the connected account, where every channel's posts belong to
 * the account being left and must not linger once a different account's
 * data starts arriving. The active channel's waiting state re-evaluates
 * the same way TTIMELINE_ClearPosts's does (empty post list -> waiting). */
#define TTIMELINE_ClearAllChannels    (TTIMELINE_Base + 29)
/* [S] STRPTR: Mastodon status id (see TTLPostSetup.postId) of a status that
 * was just deleted on the server -- the matching post is unlinked and
 * freed from EVERY channel that has it (same "can be in more than one
 * channel at once" reasoning as TTIMELINE_UpdatePost, see
 * TTLPostSetup.viewModeBits), each affected channel's Y positions are
 * rebuilt, and the active channel's tiles are invalidated for redraw.
 * Silently a no-op if no channel currently has that postId (already
 * evicted, or the reply raced a ClearPosts) -- see TTL_HOT_DELETE. */
#define TTIMELINE_RemovePost          (TTIMELINE_Base + 31)
/* [S] TTLVisiblePosts*: fills in (caller-owned, AllocVec'd copies -- see
 * TTLVisiblePostEntry below) the ids of every post currently on-screen
 * (scrollY..scrollY+gadHeight) in the ACTIVE channel only, up to
 * TTL_VISIBLE_POSTS_MAX. Read-only query, doesn't mutate anything -- see
 * FS3EApp_RefreshVisibleToots(), the F5 "refresh what's on screen" feature. */
#define TTIMELINE_GetVisiblePosts     (TTIMELINE_Base + 33)
/* [S] TTLPostSetup*: an already-displayed post's data was just re-fetched
 * from the server (F5 refresh) -- every channel's copy of the post with a
 * matching postId is patched from setup's status-derived fields (content,
 * author display, avatar, media, counts, poll), NOT its notification-
 * specific fields (notifType/notifActorName/notifActorAcct/notifStatusId
 * are left untouched -- a plain status refetch carries none of that) or
 * isThreadReply/viewModeBits (placement, not content). Forces a full
 * relayout (content/media changes can grow or shrink height, unlike
 * TTIMELINE_UpdatePost's small deltas) -- see ttl_post_refresh_fields().
 * Silently a no-op if no channel currently has that postId, same as
 * TTIMELINE_UpdatePost/RemovePost. */
#define TTIMELINE_RefreshPost         (TTIMELINE_Base + 34)

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
 * hot-spot's data string, e.g. an @handle/#tag/URL, or NULL),
 * TTIMELINE_LastHotSpotPostId (that post's Mastodon status id, or NULL),
 * TTIMELINE_LastHotSpotMediaIds (that post's comma-joined attachment
 * ids, or NULL), and TTIMELINE_LastHotSpotAcct (that post's original
 * author @user@instance, or NULL) -- read them via FindTagItem on the
 * notify message rather than a separate GetAttr call. See
 * ttl_notify_hotspot() in fs3etoottimeline_tiles.c. */
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
/* [G] BOOL: same OM_NOTIFY tag list as TTIMELINE_HotSpotNotify -- TRUE if
 * the post the just-activated hot-spot belongs to is currently reblogged
 * (boosted) by the connected user. Meaningless (FALSE) for hot-spot types
 * with no owning post -- lets a TTL_HOT_BOOST handler decide POST
 * .../reblog vs .../unreblog without a separate lookup, same reasoning as
 * TTIMELINE_LastHotSpotFavourited. */
#define TTIMELINE_LastHotSpotReblogged  (TTIMELINE_Base + 39)
/* [G] BOOL: same OM_NOTIFY tag list as TTIMELINE_HotSpotNotify -- TRUE if
 * the connected user is currently allowed to Quote the post the
 * just-activated hot-spot belongs to (see TTLPost.quotable/
 * FS3ENetStatus.fmas_Quotable). Meaningless (FALSE) for hot-spot types
 * with no owning post -- lets a TTL_HOT_BOOST handler decide whether to
 * offer a Boost/Quote choice at all. */
#define TTIMELINE_LastHotSpotQuotable   (TTIMELINE_Base + 40)
/* Ask full redraw from correct process */
#define TTIMELINE_ProcessRefresh        (TTIMELINE_Base + 23)
/* The gadget's own internal mouse-drag scroll (click+drag on the post
 * list itself, see fs3etoottimeline_input.c's GM_HANDLEINPUT) just
 * changed scrollY -- value is unused (always TRUE), this tag firing at
 * all IS the notification. Sent once per drag-move sample that actually
 * scrolls (so potentially several times over one drag gesture, not just
 * once at its start -- any external code reacting to this must be safe
 * to call repeatedly, e.g. FS3ETimer_Stop() is a harmless no-op on an
 * already-stopped timer). Distinct from TTIMELINE_ProcessRefresh (fired
 * for several unrelated "please redraw" reasons, not scroll-specific) --
 * lets external code that manages its OWN scroll animation (see
 * fs3eaction.c's "Next toot" FS3ETimer-driven scroll) know to cancel
 * itself the moment the user starts scrolling by hand, so the two don't
 * fight over scrollY. TootTimeline itself has no notion of any such
 * external animation -- this is purely "I was just scrolled by a drag",
 * nothing more. */
#define TTIMELINE_ScrollStarted         (TTIMELINE_Base + 37)

/* [S] (any -- setting it at all is the trigger, like
 * TTIMELINE_ScrollToNewest) Copy the "selected" toot's plain UTF-8 body
 * text to the Amiga clipboard (via Clipboard_WriteText(), see
 * clipboard.h). There's no persistent multi-toot selection concept here:
 * "selected" is just whichever post the user's last mouse-down landed on
 * (see TTL_OnGoActive in fs3etoottimeline_input.c -- recorded on button
 * DOWN, even if that press then turns into a drag-scroll, not just on a
 * clean click), or, before any click has happened yet, the topmost
 * currently visible post/header. No-op if there is nothing to copy (e.g.
 * an empty timeline). See Action_TimelineCopyText in fs3eaction.c. */
#define TTIMELINE_CopySelectedText      (TTIMELINE_Base + 38)

/* [G,S] BOOL: TRUE = a hot-spot's action (link click, avatar, image,
 * Reply/Boost/Fave button, etc. -- see ttl_activate_hotspot in
 * fs3etoottimeline_input.c) only fires on the SECOND click landing on the
 * same hot-spot within the system's configured double-click timing (see
 * DoubleClick(), intuition.library) -- a lone click just arms a pending
 * candidate and does nothing yet. FALSE (default) = the original
 * single-click behavior: any qualifying click (matched down/up hot-spot,
 * not a scroll-drag) fires immediately. Either way, distinguishing a
 * click from a drag-scroll (TTL_CLICK_SLOP) is unaffected -- this only
 * gates what happens once something has already been decided to be a
 * click. "Same hot-spot" across the two clicks is matched by type + the
 * hot-spot's data bytes + owning post id, not by TTLPost/hs->data
 * pointers -- see TTLData.pendingClick* in fs3etoottimeline_private.h for
 * why (both can be freed/reused between the two clicks). */
#define TTIMELINE_ActionOnDoubleClick   (TTIMELINE_Base + 41)


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

/* Parallels network_fs3e/fs3enet.h's enum FS3ENetNotifType. Kept as its
 * own small set of defines rather than a shared header, same reasoning as
 * TTL_MEDIA_KIND_* above. TTL_NOTIF_NONE is a real, distinct zero value
 * (not reused as "unset MENTION") specifically so ordinary toots -- which
 * also leave notifType at its zeroed default -- can be told apart from an
 * actual mention notification, which legitimately wants notifType set but
 * draws no prefix line either (see ttl_toot_render's notifVerbFormat
 * table: NONE and MENTION both map to "no line", for different reasons --
 * NONE because there's nothing to say, MENTION because the byline already
 * shows the mentioning author). FOLLOW/FOLLOW_REQUEST are only ever seen
 * by TTIMELINE_AddPost/AppendPost (choosing TTLNotifFollow_Class instead
 * of TTLToot_Class -- see fs3etoottimeline_attribs.c) and by
 * TTLNotifFollow_Class's own layout/render itself picking "followed you"
 * vs "requested to follow you" -- ttl_toot_render's notifVerbFormat table
 * never sees them, that class is never used for a FOLLOW-type post. */
#define TTL_NOTIF_NONE           0
#define TTL_NOTIF_MENTION        1
#define TTL_NOTIF_REBLOG         2
#define TTL_NOTIF_FAVOURITE      3
#define TTL_NOTIF_POLL           4
#define TTL_NOTIF_UPDATE         5
#define TTL_NOTIF_FOLLOW         6
#define TTL_NOTIF_FOLLOW_REQUEST 7

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
    const char *postId;      /* Mastodon status id string -- this timeline ROW's own
                               * id, used for pagination (TTIMELINE_NewestPostId/
                               * OldestPostId) and to find/patch this exact row later
                               * (TTIMELINE_UpdatePost/RemovePost/RefreshPost). For a
                               * genuine reblog-unwrapped entry this is the REBLOG
                               * WRAPPER's own id, NOT the id to interact with -- see
                               * targetId for that. NULL/"" if unknown. */
    const char *targetId;    /* the id hot-spot activation notifications carry as
                               * TTIMELINE_LastHotSpotPostId (TTL_HOT_REPLY/BOOST/
                               * FAVORITE/MODIFY/DELETE/THREAD all act on this, not
                               * postId) -- the ORIGINAL status' id for a reblog
                               * wrapper, same as postId for everything else.
                               * Mastodon's interaction endpoints operate on real
                               * content, never a reblog's own wrapper row. NULL/""
                               * falls back to postId (see ttl_activate_hotspot). */
    const char *mediaUrls[TTL_POST_MAX_MEDIA]; /* attachment preview URLs;
                               * NULL past mediaCount. Gadget copies each
                               * string and drives its own fetch/thumbnail/
                               * draw pipeline the same way it does for
                               * avatars -- see AvatarImages_GetMedia. */
    ULONG       mediaKinds[TTL_POST_MAX_MEDIA]; /* TTL_MEDIA_KIND_* per slot,
                               * same indexing as mediaUrls. */
    const char *mediaIds[TTL_POST_MAX_MEDIA]; /* attachment ids (see
                               * FS3ENetStatus.fmas_MediaIds), same indexing
                               * as mediaUrls -- not shown, only kept so a
                               * later PUT-edit of an own toot can resend
                               * them and not lose the attachments. NULL
                               * past mediaCount. */
    ULONG       mediaCount;   /* 0..TTL_POST_MAX_MEDIA; 0 = no preview rect */
    ULONG       viewModeBits; /* bit i set = also prepend to channel i (see
                                * TTIMELINE_NUM_VIEWMODES); a post can be
                                * added to more than one channel at once,
                                * as an independent copy per channel. */

    /* TRUE: this is an account row (search results, followers/following
     * list), not a toot -- TTIMELINE_AddPost/AppendPost dispatch to
     * ttl_account_row_alloc() instead of ttl_post_alloc()/
     * ttl_notif_follow_alloc(), checked before notifType. Only username/
     * acct/postId are read; every other TTLPostSetup field is ignored for
     * this kind of row (no avatar, no body, no counts, no action bar --
     * see TTLAccountRow_Class in fs3etoottimeline_accountrow.c). postId
     * doubles as this row's Mastodon *account* id here (same convention
     * TTLProfileHeaderSetup.accountId/TTLPost.postId already use on the
     * profile header) -- it's the match key TTL_POSTUPD_RELATIONSHIP
     * uses to patch in the "Follows you" badge once a batch relationship
     * fetch replies. */
    BOOL        isAccountRow;

    /* TRUE: this is a plain informational title row (e.g. "Followers for
     * @user"), pinned as the topmost element of a followers/following
     * list so it's clear whose list is being shown -- checked BEFORE
     * isAccountRow, dispatches to ttl_list_title_alloc() instead. Only
     * `body` is read (the already-formatted title text); every other
     * TTLPostSetup field is ignored, same "wholly different kind of row"
     * treatment isAccountRow gets. Not interactive (no hot-spot), not
     * an account row -- see TTLListTitle_Class in fs3etoottimeline_posts.c. */
    BOOL        isListTitle;

    /* Action-bar counts/state, shown next to the Reply/Boost/Fave buttons
     * and (favourited) toggling the Fave glyph between empty/full star --
     * see ttl_build_action_labels() in fs3etoottimeline_tiles.c. */
    ULONG       repliesCount;
    ULONG       reblogsCount;
    ULONG       favouritesCount;
    BOOL        favourited;
    BOOL        reblogged;

    /* TRUE if the connected user is currently allowed to Quote this post
     * (server-computed from the post author's quote_approval_policy plus
     * the follow relationship -- see FS3ENetStatus.fmas_Quotable's doc
     * comment in fs3enet.h). Drives whether TTL_HOT_BOOST's click handler
     * offers a Boost/Quote choice or just boosts directly -- see
     * TTIMELINE_LastHotSpotQuotable. */
    BOOL        quotable;

    /* TRUE if this post is itself a reply (server's in_reply_to_id is
     * non-null) -- see FS3ENetStatus.fmas_IsReply's doc comment. Drives
     * whether a "Follow discussion up" affordance is shown alongside the
     * existing "...down" one (see TTL_HOT_THREAD_UP), independent of
     * repliesCount (a post can be a reply AND have its own replies, have
     * neither, or just one of the two). */
    BOOL        isReply;

    /* Embedded quote (Mastodon 4.4+) -- the status THIS post is itself
     * quoting, if any. hasQuote FALSE means every other quoteXxx field
     * below is unused -- see FS3ENetStatus.fmas_HasQuote's doc comment for
     * exactly which server states populate this. Rendered as a minimal
     * avatar+name+body+timestamp block below a separator line at the
     * bottom of the post (see ttl_toot_layout/ttl_toot_render) -- never
     * its own action bar/media/poll/card, and never a further nested quote
     * even if the quoted status is itself a quote (bounded to one level). */
    BOOL        hasQuote;
    const char *quoteId;           /* quoted status' own id -- TTL_HOT_QUOTECARD target */
    const char *quoteAuthorName;
    const char *quoteAuthorAcct;
    const char *quoteAvatarURL;
    const char *quoteBody;
    const char *quoteTimestamp;

    /* TRUE if this post's original author is the connected user (caller
     * compares the status author's acct against the active account, e.g.
     * FS3EApp_HandleNetReply's FS3ENETQ_TIMELINE case) -- adds the
     * left-aligned Modify/Delete text buttons to the action bar
     * (TTL_HOT_MODIFY/TTL_HOT_DELETE) alongside the always-present
     * right-aligned Reply/Boost/Fave. FALSE (the default) for anyone
     * else's toot, and for a boost of someone else's toot even if you're
     * the one who boosted it -- boosting doesn't make the original
     * content yours to edit/delete. */
    BOOL        isOwn;

    /* TRUE marks this post as one of the replies in a "discussion mode"
     * view (see TTL_HOT_THREAD) -- draws a full-height vertical accent
     * line down the tile's left edge, otherwise laid out identically to
     * any other toot. FALSE for the discussion's own main toot (the first
     * item) and for every ordinary timeline/profile post. */
    BOOL        isThreadReply;

    /* Notifications view (see TTL_HOT_THREAD... er, TTL_HOT_NOTIF_STATUS
     * below): notifType TTL_NOTIF_NONE (the zeroed default) means this is
     * an ordinary toot and the other notif* fields are unused. Otherwise
     * notifActorName/Acct identify who triggered the notification (drawn
     * as a prefix line above the username row, same mechanism as
     * boostBy above but generalized -- see ttl_toot_render's
     * notifVerbFormat table) and notifStatusId is the embedded status's
     * own id, used for the prefix line's click action (opens that status'
     * discussion -- see TTL_HOT_NOTIF_STATUS). Deliberately NOT reusing
     * postId for this: postId must stay the *notification's* own id so
     * TTIMELINE_OldestPostId/NewestPostId-driven pagination keeps
     * working unmodified. */
    ULONG       notifType;
    const char *notifActorName;
    const char *notifActorAcct;
    const char *notifStatusId;

    /* Poll ("survey"), closed/result rendering only -- see
     * TTL_POST_MAX_POLL_OPTIONS above. pollOptionCount==0 = no poll. */
    const char *pollOptionTitles[TTL_POST_MAX_POLL_OPTIONS];
    ULONG       pollOptionVotes[TTL_POST_MAX_POLL_OPTIONS];
    ULONG       pollOptionCount;
    ULONG       pollVotesCount;
    BOOL        pollExpired;
    BOOL        pollMultiple;

    /* Link preview card -- server-generated (see FS3ENetStatus.fmas_HasCard's
     * doc comment in fs3enet.h), independent of mediaCount/pollOptionCount
     * above -- NOT mutually exclusive with either. hasCard==FALSE means the
     * other cardXxx fields below are unused. */
    BOOL        hasCard;
    const char *cardUrl;          /* the linked article's own URL */
    const char *cardTitle;
    const char *cardDescription;
    const char *cardProviderName; /* site name, e.g. "The Verge" */
    const char *cardImageUrl;     /* "" if the card has no image */
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
#define TTL_POSTUPD_REPLIES    (1UL << 2) /* bump repliesCount by +1 -- a reply to this post
                                            * was just successfully posted. Unlike the two
                                            * flags above, this can change the post's height
                                            * (repliesCount 0->1 reserves the thread-indicator
                                            * row, see TTL_HOT_THREAD/threadRowY), so it forces
                                            * a full relayout rather than an in-place patch --
                                            * see this flag's handling in TTIMELINE_UpdatePost. */
#define TTL_POSTUPD_RELATIONSHIP (1UL << 3) /* apply following + followedBy -- postId here is
                                            * an *account* id, not a status id (see
                                            * TTLAccountRow_Class's postId doubling as account
                                            * id). Used to patch in the "Follows you" badge on
                                            * an account-row list item once a batch
                                            * FS3ENETQ_RELATIONSHIPS reply lands; doesn't affect
                                            * row height, only a redraw. */

typedef struct TTLPostUpdate {
    const char *postId;      /* which post (every channel's copy is updated) */
    ULONG       flags;       /* TTL_POSTUPD_* -- which fields below to apply */
    BOOL        favourited;  /* new state; used iff flags & TTL_POSTUPD_FAVOURITED */
    BOOL        reblogged;   /* new state; used iff flags & TTL_POSTUPD_REBLOGGED */
    BOOL        following;   /* new state; used iff flags & TTL_POSTUPD_RELATIONSHIP */
    BOOL        followedBy;  /* new state; used iff flags & TTL_POSTUPD_RELATIONSHIP */
} TTLPostUpdate;

/* ------------------------------------------------------------------ */
/* Visible-posts query descriptor (passed via TTIMELINE_GetVisiblePosts) */
/* Caller zeroes the struct, gadget fills entries[]/count in place; the  */
/* gadget AllocVec's a copy of each id string, so the caller must        */
/* FreeVec() every non-NULL postId/statusId once done with them -- unlike*/
/* every other tag in this file, data flows gadget -> caller here.       */
/* ------------------------------------------------------------------ */

#define TTL_VISIBLE_POSTS_MAX 24 /* generous cap for one screen's worth of visible toots */

typedef struct TTLVisiblePostEntry {
    char *postId;   /* TTLPost.postId -- match key for a later TTIMELINE_RefreshPost */
    char *statusId; /* actual Mastodon status id to GET -- same as postId except in the
                      * notifications view (see TTLPostSetup.notifStatusId); NULL/""
                      * if this row has no status to refresh (e.g. a bare follow
                      * notification) -- such rows are simply not added to entries[]. */
} TTLVisiblePostEntry;

typedef struct TTLVisiblePosts {
    TTLVisiblePostEntry entries[TTL_VISIBLE_POSTS_MAX];
    ULONG               count;
} TTLVisiblePosts;

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
#define TTL_HOT_REPLY       5  /* action bar's "Reply" button; postId is the status being
                                 * replied to, data/dataLen carry its original author's acct
                                 * (so the caller can show/address them and prefill the
                                 * @-mention without a lookup -- see
                                 * FS3ETootView_SetComposeContext's REPLY kind) */
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
#define TTL_HOT_MODIFY      14 /* action bar's "Modify" text button, own toots only (post->isOwn) --
                                 * left-aligned, opposite the Reply/Boost/Fave group; postId is the
                                 * status to edit, data/dataLen carry its raw body text (so the caller
                                 * can prefill the compose window without a lookup -- see
                                 * FS3ETootView_SetComposeContext's MODIFY kind). The accompanying
                                 * TTIMELINE_LastHotSpotMediaIds notify tag carries the same status's
                                 * attachment ids, needed to resend on a PUT edit. */
#define TTL_HOT_DELETE      15 /* action bar's "Delete" text button, own toots only (post->isOwn) --
                                 * left-aligned, next to TTL_HOT_MODIFY; data is NULL, postId is the
                                 * status to delete */
#define TTL_HOT_THREAD      16 /* "Follow discussion" affordance row -- a short vertical bar +
                                 * text shown only when post->repliesCount>0, just above the
                                 * action bar (see TTLPost.threadRowY). data is NULL, postId is
                                 * the status whose discussion to open -- see
                                 * FS3EApp_OpenDiscussion(statusId, FALSE) (descendants only, NOT
                                 * this post's own ancestors -- see TTL_HOT_THREAD_UP for that). */
#define TTL_HOT_NOTIF_STATUS 17 /* notifications view's actor/verb prefix line (see
                                 * TTLPostSetup.notifType/notifActorName/notifActorAcct) --
                                 * data/dataLen carry notifStatusId (the embedded status, NOT
                                 * postId, which for a notification row is the *notification's*
                                 * own id, needed unchanged for pagination -- see
                                 * TTLPostSetup.notifStatusId), so clicking it opens that status'
                                 * discussion the same way TTL_HOT_THREAD does, just reading
                                 * hotSpotString instead of hotSpotId. Only present for
                                 * status-bearing notification types with a verb line at all (not
                                 * TTL_NOTIF_NONE/MENTION -- see ttl_toot_render's notifVerbFormat
                                 * table). Account-only (follow/follow_request) notification rows
                                 * are a different item
                                 * class (TTLNotifFollow_Class) and reuse TTL_HOT_AVATAR instead. */
#define TTL_HOT_CARD         18 /* link-preview card rectangle (see TTLPostSetup.hasCard/cardXxx) --
                                 * data = cardUrl. Currently a no-op on activation -- opening the
                                 * linked URL (clipboard copy / optional browser-launch library) is
                                 * deferred to a later session, same as TTL_HOT_PLAY_AUDIO's stub. */
#define TTL_HOT_FOLLOWERS_LIST 19 /* profile header's "N Followers" half of its counts line
                                 * (see ttl_profile_header_build_hotspots) -- data/postId are
                                 * NULL, the handler reads app->searchProfileAccountId
                                 * app-side (same as TTL_HOT_FOLLOW) and calls
                                 * FS3EApp_ShowFollowers(). */
#define TTL_HOT_FOLLOWING_LIST 20 /* same line's "N Following" half; calls FS3EApp_ShowFollowing(). */
#define TTL_HOT_QUOTECARD    21 /* embedded quote block rectangle (see TTLPost.hasQuote/quoteXxx) --
                                 * data/dataLen carry quoteId (the QUOTED status' own id, NOT
                                 * postId, which stays this post's own id for pagination -- same
                                 * "data carries the OTHER status' id" convention as
                                 * TTL_HOT_NOTIF_STATUS). Clicking it opens that quoted status'
                                 * own discussion via FS3EApp_OpenDiscussion(hotSpotString, FALSE),
                                 * same as TTL_HOT_THREAD/TTL_HOT_NOTIF_STATUS. */
#define TTL_HOT_THREAD_UP    22 /* "Follow discussion up" affordance row -- shown only when
                                 * post->isReply (this post is itself a reply to something),
                                 * independent of TTL_HOT_THREAD/repliesCount (a post can have
                                 * either, both, or neither) -- see TTLPost.threadUpRowY, drawn
                                 * ABOVE TTL_HOT_THREAD's row when both are present. data is NULL,
                                 * postId is THIS post's own id -- fetching its context already
                                 * returns the full ancestor chain regardless of which specific
                                 * ancestor started it (see FS3ENetStatus.fmas_IsReply). Opens the
                                 * SAME discussion as TTL_HOT_THREAD would, but via
                                 * FS3EApp_OpenDiscussion(hotSpotId, TRUE) -- ancestors AND
                                 * descendants, the whole thread, not just this post's replies. */
#define TTL_HOT_MESSAGE      23 /* profile header's "Message" button -- same row as
                                 * TTL_HOT_FOLLOW (Follow/Unfollow), right-aligned instead of
                                 * left; shown/hidden under the same condition (hidden on a
                                 * self-profile). data/postId are NULL -- the handler reads
                                 * app->searchProfileAcct app-side (same as TTL_HOT_FOLLOW
                                 * reads app->searchProfileAccountId) and opens the compose
                                 * window with FS3ETOOT_KIND_MESSAGE. */

/* Opaque handle; cast to TTLHotSpot* from private header if needed */
typedef struct TTLHotSpot TTLHotSpot;

/* ------------------------------------------------------------------ */
/* Class pointer and lifecycle                                          */
/* ------------------------------------------------------------------ */

extern Class *TootTimelineClass;

int  TootTimeline_Init(void);
void TootTimeline_Exit(void);

#endif /* FS3ETOOTTIMELINE_H */
