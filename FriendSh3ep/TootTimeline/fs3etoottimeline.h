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

/* ------------------------------------------------------------------ */
/* Notification tags  (sent to ICA_TARGET via OM_NOTIFY)               */
/* ------------------------------------------------------------------ */

/* Scroll domain changed (posts were prepended); value = new contentTopY */
#define TTIMELINE_ScrollDomainChanged (TTIMELINE_Base + 20)
/* User clicked a post; value = post index (0 = topmost) */
#define TTIMELINE_PostClicked         (TTIMELINE_Base + 21)
/* User activated a hot-spot; value = (ULONG)TTLHotSpot* */
#define TTIMELINE_HotSpotActivated    (TTIMELINE_Base + 22)
/* Ask full redraw from correct process */
#define TTIMELINE_ProcessRefresh        (TTIMELINE_Base + 23)


/* ------------------------------------------------------------------ */
/* Post content descriptor  (passed via TTIMELINE_AddPost)             */
/* The gadget copies all strings; the caller owns the struct.          */
/* ------------------------------------------------------------------ */

typedef struct TTLPostSetup {
    const char *username;   /* display name (UTF-8) */
    const char *acct;       /* @user@instance (UTF-8) */
    const char *body;       /* post body text (UTF-8) */
    const char *timestamp;  /* short age string, e.g. "3h" (UTF-8) */
    ULONG       viewModeBits; /* bit i set = also prepend to channel i (see
                                * TTIMELINE_NUM_VIEWMODES); a post can be
                                * added to more than one channel at once,
                                * as an independent copy per channel. */
} TTLPostSetup;

/* ------------------------------------------------------------------ */
/* Hot-spot types  (forwarded in TTLHotSpotActivated notification)     */
/* ------------------------------------------------------------------ */

#define TTL_HOT_AVATAR   0
#define TTL_HOT_IMAGE    1
#define TTL_HOT_URL      2
#define TTL_HOT_HASHTAG  3
#define TTL_HOT_MENTION  4

/* Opaque handle; cast to TTLHotSpot* from private header if needed */
typedef struct TTLHotSpot TTLHotSpot;

/* ------------------------------------------------------------------ */
/* Class pointer and lifecycle                                          */
/* ------------------------------------------------------------------ */

extern Class *TootTimelineClass;

int  TootTimeline_Init(void);
void TootTimeline_Exit(void);

#endif /* FS3ETOOTTIMELINE_H */
