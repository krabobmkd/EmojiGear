/*
 * TootTimeline – GM_HITTEST, GM_GOACTIVE, GM_HANDLEINPUT, GM_GOINACTIVE.
 *
 * Scroll interaction
 * ------------------
 * Button press   → GoActive: record dragStart{GadX,GadY,ScrollY}.
 * Mouse move     → HandleInput: compute dy, set pendingScrollY, notify
 *                  TTIMELINE_ProcessRefresh to get blitted at the new
 *                  position.
 * Button release → HandleInput: end drag, return GMR_NOREUSE.
 *
 * Click vs. scroll
 * ----------------
 * There's no separate "drag mode": every button-down starts a live
 * scroll (mouse-move already updates pendingScrollY while the button is
 * held). On button-up we decide, after the fact, whether that was
 * actually a click: total movement on both axes must stay under
 * TTL_CLICK_SLOP pixels, AND the down-point and up-point must resolve to
 * the exact same hot-spot (same post, same hot-spot index) -- e.g. a tiny
 * jitter that starts on a link but ends just outside it is still treated
 * as a (no-op) scroll, not a click. See ttl_hit_hotspot / ttl_activate_hotspot.
 *
 * Scroll-wheel (future)
 * ----------------------
 * IECODE_UP_PREFIX / IECODE_DOWN_PREFIX from IECLASS_RAWMOUSE wheel
 * events are handled here when the gadget is active.
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include <devices/inputevent.h>

#include "fs3etoottimeline_private.h"
#include "../bdbprintf.h"

/* Max total movement (either axis) between button-down and button-up for
 * the gesture to still count as a click rather than a scroll. */
#define TTL_CLICK_SLOP 8

/* ------------------------------------------------------------------ */
/* Scroll helper: clamp and store pending scroll                        */
/* ------------------------------------------------------------------ */

static void ttl_set_scroll(TTLData *inst, LONG newScrollY)
{
    TTLChannel *active = ttl_active(inst);
    LONG minScroll = ttl_channel_min_scroll(active);
    LONG maxScroll = active->contentBottomY - inst->gadHeight;
    if (maxScroll < minScroll) maxScroll = minScroll;
    if (newScrollY < minScroll) newScrollY = minScroll;
    if (newScrollY > maxScroll) newScrollY = maxScroll;
    inst->pendingScroll  = TRUE;
    inst->pendingScrollY = newScrollY;
}

/* ------------------------------------------------------------------ */
/* Hit-test helpers (future click / text selection)                    */
/* ------------------------------------------------------------------ */

/* Find the post whose Y range contains timelineY, or NULL, in the
 * currently active channel. A profile header (see TTLChannel.headerPost)
 * is deliberately not a member of the channel's post list, so it needs
 * its own check here first -- guarded, and always NULL/no-op for every
 * channel except Search with a profile loaded. */
static TTLPost *ttl_hit_post(TTLData *inst, LONG timelineY)
{
    TTLPost *post;
    TTLChannel *active = ttl_active(inst);

    if (active->headerPost &&
        timelineY >= active->headerPost->timelineY &&
        timelineY <  active->headerPost->timelineY + active->headerPost->height)
        return active->headerPost;

    for (post = (TTLPost *)active->posts.mlh_Head;
         post->node.mln_Succ;
         post = (TTLPost *)post->node.mln_Succ)
    {
        if (timelineY >= post->timelineY &&
            timelineY <  post->timelineY + post->height)
            return post;
    }
    return NULL;
}

/* Resolve a gadget-relative (gadX, gadY) point to the hot-spot under it,
 * if any. Hot-spots are pool-backed and lazily built (see
 * ttl_post_ensure_hotspots) -- gadX/gadY only ever come from a point the
 * user just clicked, which is on screen, so this is a cheap freshness
 * check rather than a real rebuild in practice. */
static BOOL ttl_hit_hotspot(TTLData *inst, WORD gadX, WORD gadY,
                             TTLPost **outPost, UBYTE *outIndex)
{
    LONG     timelineY = ttl_active(inst)->scrollY + gadY;
    TTLPost *post       = ttl_hit_post(inst, timelineY);
    WORD     relY, relX;
    UBYTE    i;

    if (!post) return FALSE;

    ttl_post_ensure_hotspots(inst, post);

    relY = (WORD)(timelineY - post->timelineY);
    relX = gadX;

    /* Reverse order: TTL_HOT_MEDIA_PREV/NEXT are added after (and drawn on
     * top of) TTL_HOT_IMAGE, nested inside its rect as small arrow zones --
     * scanning forward would always match the enclosing image hotspot
     * first and the arrows would never be reachable. Last-added wins,
     * matching draw order (topmost hit-tests first). */
    for (i = post->hotSpotCount; i > 0; i--) {
        TTLHotSpot *hs = &post->hotSpots[i - 1];
        if (relX >= hs->x && relX < hs->x + hs->w &&
            relY >= hs->y && relY < hs->y + hs->h)
        {
            *outPost  = post;
            *outIndex = i - 1;
            return TRUE;
        }
    }
    return FALSE;
}

/* The link-click action. Every activation is forwarded to external code
 * via ttl_notify_hotspot() -- TTIMELINE_HotSpotNotify (value=hs->type)
 * plus the hot-spot's string data and the owning post's Mastodon status
 * id, both as persistent gadget-owned buffers external code can read off
 * the same notify's tag list (or via TTIMELINE_LastHotSpotString/PostId
 * GetAttr afterwards) -- generic across every item kind, so it happens
 * here rather than in a per-class hook. Whatever the clicked item's own
 * class wants to do locally beyond that (see TTL_HOT_MEDIA_PREV/NEXT
 * below) is item->cls->activate's job -- see the TTLItemClass comment in
 * fs3etoottimeline_private.h. */
static void ttl_activate_hotspot(TTLData *inst, Class *cl, Object *o,
                                  struct GadgetInfo *gi,
                                  TTLPost *post, TTLHotSpot *hs)
{
    ttl_notify_hotspot(cl, o, gi, hs->type, hs->data, hs->dataLen, post->postId,
                        post->favourited, post->following, post->mediaIdsJoined);

    if (post->cls && post->cls->activate)
        post->cls->activate(inst, cl, o, gi, post, hs);
}

/* TTLItemClass.activate for TTLToot_Class: TTL_HOT_MEDIA_PREV/NEXT browse
 * to the prev/next attachment within the post's single preview rect and
 * ask for a redraw. Every other hot-spot type has no local reaction --
 * the generic notify above (ttl_notify_hotspot) is already everything
 * external code needs. */
void ttl_toot_activate(TTLData *inst, Class *cl, Object *o,
                        struct GadgetInfo *gi, TTLPost *post, TTLHotSpot *hs)
{
    if ((hs->type == TTL_HOT_MEDIA_PREV || hs->type == TTL_HOT_MEDIA_NEXT) &&
        post->mediaCount > 1)
    {

        if (hs->type == TTL_HOT_MEDIA_PREV)
            post->mediaCurrentIndex = (post->mediaCurrentIndex == 0)
                                     ? post->mediaCount - 1
                                     : post->mediaCurrentIndex - 1;
        else
            post->mediaCurrentIndex = (post->mediaCurrentIndex + 1) % post->mediaCount;

        /* The TTL_HOT_IMAGE hot-spot's data (and the drawn thumbnail)
         * follow mediaCurrentIndex, so both must refresh -- hotSpotsDirty
         * makes the next ttl_post_ensure_hotspots() rebuild pick up the
         * new index; tile invalidation + notify make that "next" happen
         * now instead of whenever this post's tile next redraws anyway. */
        post->hotSpotsDirty = TRUE;
        ttl_tiles_invalidate_range(inst, post->timelineY, post->timelineY + post->height);
        ttl_notify(cl, o, gi, TTIMELINE_ProcessRefresh, TRUE);
    }
}

/* ------------------------------------------------------------------ */
/* GM_HITTEST                                                           */
/*                                                                      */
/* Bottom-right TTL_RESIZE_HANDLE × TTL_RESIZE_HANDLE corner → prime  */
/* the resize globals and return 0 so the gadget is NOT activated and  */
/* WMHI_MOUSEMOVE events flow to the main loop unobstructed.           */
/* Any other position → GMR_GADGETHIT for normal scroll activation.    */
/* ------------------------------------------------------------------ */

ULONG TTL_OnHitTest(Class *cl, Object *o, struct gpHitTest *msg)
{
    struct GadgetInfo *gi  = msg->gpht_GInfo;
    WORD               relX = msg->gpht_Mouse.X;
    WORD               relY = msg->gpht_Mouse.Y;
    WORD               gadW = G(o)->Width;
    WORD               gadH = G(o)->Height;

    (void)cl;

    if (relX >= gadW - TTL_RESIZE_HANDLE &&
        relY >= gadH - TTL_RESIZE_HANDLE &&
        gi && gi->gi_Window && gi->gi_Screen)
    {
        /* GM_HITTEST can run on a different task than the main loop that
         * reads these globals on WMHI_MOUSEMOVE (see the listSem comment
         * in fs3etoottimeline_private.h for the general cross-task
         * hazard) -- windowResizeActive is set LAST, after every companion
         * field, so the main loop never observes Active==TRUE alongside a
         * stale/partial group of the other fields below. A single CPU
         * with no store reordering guarantees writes complete in this
         * program order, so that ordering alone (no semaphore needed for
         * a plain "publish this flag last" pattern) is enough here. */
        windowResizeStartSX     = gi->gi_Screen->MouseX;
        windowResizeStartSY     = gi->gi_Screen->MouseY;
        windowResizeStartW      = gi->gi_Window->Width;
        windowResizeStartH      = gi->gi_Window->Height;
        windowResizeLastTargetW = gi->gi_Window->Width;
        windowResizeLastTargetH = gi->gi_Window->Height;
        windowResizeActive      = TRUE;
        return 0;   /* don't activate — let IDCMP see the mouse events */
    }

    return GMR_GADGETHIT;
}

/* ------------------------------------------------------------------ */
/* GM_GOACTIVE                                                          */
/* ------------------------------------------------------------------ */

ULONG TTL_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    TTLData *inst = TTL_DATA(cl, o);

    *msg->gpi_Termination = 0;

    /* No list to scroll while showing the waiting screen (see
     * TTIMELINE_ViewMode / ttl_is_waiting) -- refuse activation entirely. */
    if (ttl_is_waiting(inst))
        return GMR_NOREUSE;

    if (msg->gpi_IEvent &&
        msg->gpi_IEvent->ie_Class == IECLASS_RAWMOUSE &&
        (msg->gpi_IEvent->ie_Code & ~IECODE_UP_PREFIX) == IECODE_LBUTTON)
    {
        inst->dragActive      = TRUE;
        inst->dragStartGadX   = msg->gpi_Mouse.X;
        inst->dragStartGadY   = msg->gpi_Mouse.Y;
        inst->dragStartScrollY = ttl_active(inst)->scrollY;
        return GMR_MEACTIVE;
    }

    return GMR_NOREUSE;
}

/* ------------------------------------------------------------------ */
/* GM_HANDLEINPUT                                                       */
/* ------------------------------------------------------------------ */

ULONG TTL_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    TTLData          *inst  = TTL_DATA(cl, o);
    struct InputEvent *ie   = msg->gpi_IEvent;

    *msg->gpi_Termination = 0;

    if (!ie) return GMR_MEACTIVE;

    if (ie->ie_Class == IECLASS_RAWMOUSE) {
        UWORD code = ie->ie_Code & ~IECODE_UP_PREFIX;

        if (code == IECODE_LBUTTON && (ie->ie_Code & IECODE_UP_PREFIX)) {
            /* Button released -- end the live scroll. If total movement
             * stayed small AND the down-point and up-point land on the
             * exact same hot-spot, treat it as a click instead. */
            WORD dx = (WORD)(msg->gpi_Mouse.X - inst->dragStartGadX);
            WORD dy = (WORD)(msg->gpi_Mouse.Y - inst->dragStartGadY);
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;

            inst->dragActive = FALSE;

            if (dx < TTL_CLICK_SLOP && dy < TTL_CLICK_SLOP) {
                TTLPost *downPost, *upPost;
                UBYTE    downIdx,  upIdx;
                BOOL     downHit, upHit;

                /* GM_HANDLEINPUT can run on a different task than
                 * GM_RENDER/TTIMELINE_AddPost -- see the listSem comment
                 * in fs3etoottimeline_private.h. Held across both hit
                 * tests and the activation itself so a post can't be
                 * spliced/removed out from under downPost/upPost between
                 * the two hits or before dereferencing them. */
                ObtainSemaphore(&inst->listSem);

                downHit = ttl_hit_hotspot(inst, inst->dragStartGadX,
                                           inst->dragStartGadY,
                                           &downPost, &downIdx);
                upHit   = ttl_hit_hotspot(inst, msg->gpi_Mouse.X,
                                           msg->gpi_Mouse.Y,
                                           &upPost, &upIdx);

                if (downHit && upHit && downPost == upPost && downIdx == upIdx) {
                    TTLHotSpot *hs = &upPost->hotSpots[upIdx];

                    ttl_activate_hotspot(inst, cl, o, msg->gpi_GInfo, upPost, hs);
                }

                ReleaseSemaphore(&inst->listSem);
            }
            return GMR_NOREUSE;
        }

        if (inst->dragActive && ie->ie_Code == IECODE_NOBUTTON) {
            /* Mouse move while dragging: scroll. ttl_set_scroll reads
             * active->contentTopY/contentBottomY, which a relayout on the
             * render/main-task side can be rewriting concurrently under
             * listSem -- this branch previously wasn't locked at all
             * (only the click-resolution branch below was), the same
             * class of cross-task race the list-corruption fix addressed;
             * see the listSem comment in fs3etoottimeline_private.h. */
            WORD dy = (WORD)(msg->gpi_Mouse.Y - inst->dragStartGadY);

            ObtainSemaphore(&inst->listSem);
            ttl_set_scroll(inst, inst->dragStartScrollY - dy);
            ReleaseSemaphore(&inst->listSem);

            ttl_notify(cl,o,msg->gpi_GInfo, TTIMELINE_ProcessRefresh,TRUE);
            //ttl_render_self(cl, o, msg->gpi_GInfo);
            return GMR_MEACTIVE;
        }
    }

    return GMR_MEACTIVE;
}

/* ------------------------------------------------------------------ */
/* GM_GOINACTIVE                                                        */
/* ------------------------------------------------------------------ */

ULONG TTL_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
{
    TTLData *inst = TTL_DATA(cl, o);
    (void)msg;
    inst->dragActive = FALSE;
    return 0;
}
