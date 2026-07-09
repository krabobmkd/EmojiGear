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
/* [S]  TTLPostSetup*: prepend a new post at the top of the timeline */
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

typedef struct TTLPostSetup {
    const char *username;    /* original author display name (UTF-8) */
    const char *acct;        /* original author @user@instance (UTF-8) */
    const char *body;        /* post body text (UTF-8) */
    const char *timestamp;   /* short age string, e.g. "3h" (UTF-8) */
    const char *boostBy;     /* booster display name, NULL/"" for originals */
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
    ULONG       mediaCount;   /* 0..TTL_POST_MAX_MEDIA; 0 = no preview rect */
    ULONG       viewModeBits; /* bit i set = also prepend to channel i (see
                                * TTIMELINE_NUM_VIEWMODES); a post can be
                                * added to more than one channel at once,
                                * as an independent copy per channel. */
} TTLPostSetup;

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

/* Opaque handle; cast to TTLHotSpot* from private header if needed */
typedef struct TTLHotSpot TTLHotSpot;

/* ------------------------------------------------------------------ */
/* Class pointer and lifecycle                                          */
/* ------------------------------------------------------------------ */

extern Class *TootTimelineClass;

int  TootTimeline_Init(void);
void TootTimeline_Exit(void);

#endif /* FS3ETOOTTIMELINE_H */
