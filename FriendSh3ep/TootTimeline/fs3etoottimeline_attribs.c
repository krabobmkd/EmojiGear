/*
 * TootTimeline – OM_NEW, OM_DISPOSE, OM_SET, OM_GET.
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <proto/graphics.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <string.h>

#include "fs3etoottimeline_private.h"
#include "../bdbprintf.h"
/* ------------------------------------------------------------------ */
/* Apply a tag list to the instance data                                */
/* ------------------------------------------------------------------ */

ULONG ttl_apply_tags(Class *cl, Object *o, struct opSet *msg, int couldRefreshDraw)
{

    TTLData *inst = TTL_DATA(cl, o);
    struct TagItem *tags = msg->ops_AttrList;
    struct TagItem *tstate = tags;
    struct TagItem *tag;
    int redraw = FALSE;
    int used = 0;

    /* AddPost/AppendPost/ClearPosts (and everything else here) touch
     * channel post lists / scroll state that GM_RENDER and GM_HANDLEINPUT
     * also touch, possibly from a different task -- see the listSem
     * comment in fs3etoottimeline_private.h. Released before the
     * nested GM_RENDER call below so that call re-acquires independently
     * rather than relying on same-task semaphore nesting. */
    ObtainSemaphore(&inst->listSem);

    while ((tag = NextTagItem(&tstate))) {
        switch (tag->ti_Tag) {
            case TTIMELINE_DpiHeight:
                if(inst->dpiHeight != (UWORD)tag->ti_Data)
                {
                    inst->dpiHeight = (UWORD)tag->ti_Data;
                    inst->layoutToDo = TRUE;
                    redraw = TRUE;
                    used = 1;
                }
                break;
            case TTIMELINE_ScrollY:
                /* Applies to whichever channel is active when GM_RENDER
                 * next runs (see TTL_OnRender) -- same deferred-apply
                 * mechanism as before, now scoped to ttl_active(inst). */
                if(inst->pendingScrollY != (LONG)tag->ti_Data)
                {
                    inst->pendingScroll  = TRUE;
                    inst->pendingScrollY = (LONG)tag->ti_Data;
                    redraw = TRUE;
                    used = 1;
                }
                break;
            // case TTIMELINE_Screen:
            // bdbprintf(" ***** set screen:%08x\n",(int)tag->ti_Data);
            //     inst->screen = (struct Screen *)tag->ti_Data;
            //     used = 1;
            //     break;
            case TTIMELINE_Style: {
                struct URPTextMetric m;
                inst->style = (FS3EStyle *)tag->ti_Data;


                // /* Cache font metrics from each draw context */
                if (inst->style && inst->style->dcNormal) {
                    URPDC_GetFontLineMetrics(inst->style->dcNormal, &m);
                    inst->lineHeight = (WORD)(m.height > 0 ? m.height : 14);
                    inst->lineAscent = (WORD)(m.baseY  > 0 ? m.baseY  : 11);
                } else {
                    inst->lineHeight = 14; inst->lineAscent = 11;
                }
                if (inst->style && inst->style->dcUsername) {
                    URPDC_GetFontLineMetrics(inst->style->dcUsername, &m);
                    inst->nameLineHeight = (WORD)(m.height > 0 ? m.height : 16);
                    inst->nameLineAscent = (WORD)(m.baseY  > 0 ? m.baseY  : 13);
                } else {
                    inst->nameLineHeight = inst->lineHeight;
                    inst->nameLineAscent = inst->lineAscent;
                }
                if (inst->style && inst->style->dcMini) {
                    URPDC_GetFontLineMetrics(inst->style->dcMini, &m);
                    inst->miniLineHeight = (WORD)(m.height > 0 ? m.height : 10);
                    inst->miniLineAscent = (WORD)(m.baseY  > 0 ? m.baseY  :  8);
                } else {
                    inst->miniLineHeight = inst->lineHeight;
                    inst->miniLineAscent = inst->lineAscent;
                }
                /* Force ttl_do_layout to treat this as a width change on next
                 * render: frees stale tile bitmaps, reallocates fresh ones,
                 * and re-runs ttl_layout_all_posts + ttl_rebuild_ypositions so
                 * post heights reflect the new line metrics. */
                inst->lastTileWidth = -1;
                inst->layoutToDo = TRUE;
                redraw = TRUE;
                used = 1;

                break;
            }

            case TTIMELINE_AddPost: {
                const TTLPostSetup *setup = (const TTLPostSetup *)tag->ti_Data;
                if (setup) {
                    ULONG ch;
                    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++) {
                        TTLChannel *channel;
                        TTLPost    *post;

                        if (!(setup->viewModeBits & (1UL << ch))) continue;

                        /* Independent copy per targeted channel: a post's
                         * node can only ever be linked into one list, and
                         * its timelineY is meaningless outside the
                         * channel it was laid out for. FOLLOW/FOLLOW_REQUEST
                         * notifications carry no status at all, so they get
                         * TTLNotifFollow_Class's much shorter row instead of
                         * a toot -- see TTLPostSetup.notifType. */
                        post = (setup->notifType == TTL_NOTIF_FOLLOW ||
                                setup->notifType == TTL_NOTIF_FOLLOW_REQUEST)
                             ? ttl_notif_follow_alloc(setup) : ttl_post_alloc(setup);
                        if (!post) continue;

                        channel = &inst->channels[ch];
                        if (post->cls && post->cls->layout)
                            post->cls->layout(inst, post);

                        if (channel->postCount == 0) {
                            /* Very first real post in this channel: anchor
                             * at Y=0 -- the list is still empty here, so a
                             * bare AddHead is safe (ttl_channel_insert_top
                             * assumes a non-empty list, see its comment). */
                            channel->contentTopY    = 0;
                            channel->contentBottomY = post->height;
                            post->timelineY         = 0;
                            AddHead((struct List *)&channel->posts, (struct Node *)&post->node);
                            channel->postCount++;
                            /* Now that there's real content, pin the
                             * "look for something new" / "load more…" rows
                             * around it. */
                            ttl_channel_add_boundaries(inst, channel);
                        } else {
                            /* Prepend: new post goes above the current top
                             * (below a pinned "load newer" row, if any).
                             * scrollY stays fixed so the user sees the same
                             * view. */
                            ttl_channel_insert_top(inst, channel, post);
                            channel->postCount++;
                        }

                        /* Tiles only ever reflect the active channel. */
                        if (ch == inst->viewMode)
                            ttl_tiles_invalidate_range(inst,
                                post->timelineY,
                                post->timelineY + post->height);

                        redraw = TRUE;
                        used = 1;
                    }
                }
                break;
            }

            case TTIMELINE_AppendPost: {
                const TTLPostSetup *setup = (const TTLPostSetup *)tag->ti_Data;
                if (setup) {
                    ULONG ch;
                    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++) {
                        TTLChannel *channel;
                        TTLPost    *post;

                        if (!(setup->viewModeBits & (1UL << ch))) continue;

                        post = (setup->notifType == TTL_NOTIF_FOLLOW ||
                                setup->notifType == TTL_NOTIF_FOLLOW_REQUEST)
                             ? ttl_notif_follow_alloc(setup) : ttl_post_alloc(setup);
                        if (!post) continue;

                        channel = &inst->channels[ch];
                        if (post->cls && post->cls->layout)
                            post->cls->layout(inst, post);

                        if (channel->postCount == 0) {
                            /* Shouldn't normally happen (an older-page
                             * reply implies there was already something to
                             * paginate from), but stay safe/consistent with
                             * AddPost's own bootstrap rather than touching
                             * an empty list's head/tail as real nodes. */
                            channel->contentTopY    = 0;
                            channel->contentBottomY = post->height;
                            post->timelineY         = 0;
                            AddHead((struct List *)&channel->posts, (struct Node *)&post->node);
                            channel->postCount++;
                            ttl_channel_add_boundaries(inst, channel);
                        } else {
                            /* Append: new post goes below the current
                             * bottom (above a pinned "load older" row, if
                             * any). */
                            ttl_channel_insert_bottom(inst, channel, post);
                            channel->postCount++;
                        }

                        if (ch == inst->viewMode)
                            ttl_tiles_invalidate_range(inst,
                                post->timelineY,
                                post->timelineY + post->height);

                        redraw = TRUE;
                        used = 1;
                    }
                }
                break;
            }

            case TTIMELINE_ScrollToNewest: {
                TTLChannel *active = ttl_active(inst);
                TTLPost    *head   = (TTLPost *)active->posts.mlh_Head;
                LONG        targetY = active->contentTopY;

                /* Land on the newest real post, not the pinned "look for
                 * something new" row above it (which now sits at
                 * contentTopY once a channel has one -- see
                 * ttl_channel_add_boundaries). */
                if (head->node.mln_Succ && head->cls == &TTLLoadNewer_Class) {
                    TTLPost *next = (TTLPost *)head->node.mln_Succ;
                    if (next->node.mln_Succ) targetY = next->timelineY;
                }

                active->scrollY = targetY;
                inst->pendingScroll  = FALSE; /* an explicit jump wins over any queued drag-scroll */
                redraw = TRUE;
                used = 1;
                break;
            }

            case TTIMELINE_UpdatePost: {
                const TTLPostUpdate *upd = (const TTLPostUpdate *)tag->ti_Data;
                if (upd && upd->postId && upd->postId[0]) {
                    ULONG ch;
                    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++) {
                        TTLChannel *channel = &inst->channels[ch];
                        TTLPost    *post;

                        /* A post's id is unique within one channel's list
                         * (it's the same Mastodon status, added at most
                         * once per channel by TTIMELINE_AddPost/
                         * AppendPost), but the same status can be present
                         * in more than one channel independently -- see
                         * TTLPostSetup.viewModeBits -- so every channel
                         * must be searched, not just the active one. */
                        for (post = (TTLPost *)channel->posts.mlh_Head;
                             post->node.mln_Succ;
                             post = (TTLPost *)post->node.mln_Succ)
                        {
                            if (!post->postId ||
                                strcmp(post->postId, upd->postId) != 0)
                                continue;

                            /* Delta, not overwrite -- see the TTLPostUpdate
                             * comment in fs3etoottimeline.h for why. */
                            if ((upd->flags & TTL_POSTUPD_FAVOURITED) &&
                                post->favourited != upd->favourited)
                            {
                                if (upd->favourited) post->favouritesCount++;
                                else if (post->favouritesCount > 0) post->favouritesCount--;
                                post->favourited = upd->favourited;
                            }
                            if ((upd->flags & TTL_POSTUPD_REBLOGGED) &&
                                post->reblogged != upd->reblogged)
                            {
                                if (upd->reblogged) post->reblogsCount++;
                                else if (post->reblogsCount > 0) post->reblogsCount--;
                                post->reblogged = upd->reblogged;
                            }
                            post->dirty         = TRUE;
                            post->hotSpotsDirty = TRUE; /* label widths may have changed */

                            if (ch == inst->viewMode)
                                ttl_tiles_invalidate_range(inst,
                                    post->timelineY, post->timelineY + post->height);
                            redraw = TRUE;
                            break; /* unique within this channel's list */
                        }
                    }
                    used = 1;
                }
                break;
            }

            case TTIMELINE_RemovePost: {
                const char *postId = (const char *)tag->ti_Data;
                if (postId && postId[0]) {
                    ULONG ch;
                    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++) {
                        TTLChannel *channel = &inst->channels[ch];
                        TTLPost    *post;

                        /* Same "search every channel" reasoning as
                         * TTIMELINE_UpdatePost above -- the same status
                         * can be present in more than one channel
                         * independently (see TTLPostSetup.viewModeBits). */
                        for (post = (TTLPost *)channel->posts.mlh_Head;
                             post->node.mln_Succ;
                             post = (TTLPost *)post->node.mln_Succ)
                        {
                            if (!post->postId ||
                                strcmp(post->postId, postId) != 0)
                                continue;

                            /* Removing a post shifts every post below it,
                             * unlike TTIMELINE_UpdatePost's in-place field
                             * patch -- rebuild the whole channel's Y
                             * positions, not just invalidate this post's
                             * old rect. */
                            Remove((struct Node *)&post->node);
                            ttl_post_free(inst, post);
                            if (channel->postCount > 0) channel->postCount--;
                            ttl_rebuild_ypositions(inst, ch);

                            if (ch == inst->viewMode)
                                ttl_tiles_invalidate_all(inst);
                            redraw = TRUE;
                            break; /* unique within this channel's list */
                        }
                    }
                    used = 1;
                }
                break;
            }

            case TTIMELINE_ShowProfile: {
                const TTLProfileHeaderSetup *setup = (const TTLProfileHeaderSetup *)tag->ti_Data;
                bdbprintf_now("ShowProfile: enter setup=%08lx acct=%s\n",
                          (unsigned long)setup, (setup && setup->acct) ? setup->acct : "?");
                if (setup) {
                    TTLChannel *channel = &inst->channels[TTL_SEARCH_CHANNEL];
                    TTLPost    *header;

                    bdbprintf_now("ShowProfile: clearing channel, old headerPost=%08lx\n",
                              (unsigned long)channel->headerPost);
                    ttl_clear_channel(inst, TTL_SEARCH_CHANNEL);
                    bdbprintf_now("ShowProfile: cleared\n");

                    header = ttl_profile_header_alloc(setup);
                    bdbprintf_now("ShowProfile: alloc'd header=%08lx\n", (unsigned long)header);
                    if (header) {
                        TTLPost *loadOlder;

                        if (header->cls && header->cls->layout)
                            header->cls->layout(inst, header);
                        bdbprintf_now("ShowProfile: header laid out, height=%ld\n", (long)header->height);

                        /* The header is NEVER linked into channel->posts
                         * -- it lives only via channel->headerPost (see
                         * that field's comment, and TTIMELINE_ShowProfile's
                         * comment in fs3etoottimeline.h). It must NOT be
                         * AddHead'd into the list too: ttl_clear_channel's
                         * ordinary RemHead loop would then free it once as
                         * an ordinary list member, and the headerPost-
                         * specific free right below would free the exact
                         * same pointer a second time -- a double-free that
                         * corrupts the allocator and crashes on the next
                         * profile switch, not this one (confirmed by a
                         * captured trace: ttl_profile_header_dispose firing
                         * twice on the same pointer). */
                        header->timelineY = 0;

                        channel->headerPost     = header;
                        channel->contentTopY    = header->height; /* where the LIST's own content starts */
                        channel->contentBottomY = header->height;
                        channel->scrollY        = 0;
                        /* Deliberate sentinel, not a real toot count --
                         * makes TTIMELINE_AppendPost's postCount==0
                         * bootstrap check false from here on, so the
                         * profile's own toots correctly take the normal
                         * ttl_channel_insert_bottom path below instead of
                         * re-bootstrapping on top of the header. Also
                         * means ttl_channel_add_boundaries() (which would
                         * add a "look for something new" row we don't
                         * want here -- see the header comment) never
                         * runs for this channel; the pagination row is
                         * added explicitly below instead. */
                        channel->postCount = 1;

                        /* Pagination row only ("look for something new"
                         * doesn't apply to a single profile's toot
                         * history). channel->posts is genuinely empty at
                         * this point (the header is deliberately never a
                         * member of it -- see above), so this is a bare
                         * AddTail, the exact same bootstrap pattern
                         * TIMELINE_AddPost's very-first-real-post case
                         * uses: ttl_channel_insert_bottom's own doc
                         * comment requires a non-empty list as a
                         * precondition (it reads mlh_TailPred->cls, which
                         * on a genuinely empty MinList doesn't point at a
                         * TTLPost at all). Once this lands, the list is
                         * non-empty and every subsequent real toot page
                         * correctly goes through the ordinary
                         * ttl_channel_insert_bottom below (unmodified),
                         * gluing above this LoadOlder tail as usual. */
                        loadOlder = ttl_pseudo_post_alloc(&TTLLoadOlder_Class,
                            "Load more\xE2\x80\xA6" /* "Load more…" */);
                        if (loadOlder) {
                            if (loadOlder->cls && loadOlder->cls->layout)
                                loadOlder->cls->layout(inst, loadOlder);
                            loadOlder->timelineY = channel->contentBottomY;
                            AddTail((struct List *)&channel->posts, (struct Node *)&loadOlder->node);
                            channel->contentBottomY += loadOlder->height;
                        }
                        bdbprintf_now("ShowProfile: loadOlder=%08lx inserted, headerPost was %08lx\n",
                                  (unsigned long)loadOlder, (unsigned long)channel->headerPost);

                        if (TTL_SEARCH_CHANNEL == inst->viewMode)
                            ttl_tiles_invalidate_all(inst);
                        redraw = TRUE;
                    }
                    bdbprintf_now("ShowProfile: done, channel->headerPost=%08lx\n",
                              (unsigned long)channel->headerPost);
                    used = 1;
                }
                break;
            }

            case TTIMELINE_UpdateProfileFollow: {
                const TTLProfileFollowUpdate *upd = (const TTLProfileFollowUpdate *)tag->ti_Data;
                TTLChannel *channel = &inst->channels[TTL_SEARCH_CHANNEL];
                if (upd && upd->accountId && channel->headerPost &&
                    channel->headerPost->postId &&
                    strcmp(channel->headerPost->postId, upd->accountId) == 0)
                {
                    TTLPost *header = channel->headerPost;

                    /* Delta, not overwrite -- same reasoning as
                     * TTL_POSTUPD_FAVOURITED (see TTLPostUpdate's
                     * comment): don't trust a server-echoed count, the
                     * Relationship object doesn't even carry one anyway. */
                    if (header->following != upd->following) {
                        if (upd->following) header->followersCount++;
                        else if (header->followersCount > 0) header->followersCount--;
                        header->following = upd->following;
                    }
                    header->dirty         = TRUE;
                    header->hotSpotsDirty = TRUE;

                    if (TTL_SEARCH_CHANNEL == inst->viewMode)
                        ttl_tiles_invalidate_range(inst,
                            header->timelineY, header->timelineY + header->height);
                    redraw = TRUE;
                }
                used = 1;
                break;
            }

            case TTIMELINE_ClearPosts:
                ttl_clear_posts(inst);
                redraw = TRUE;
                used = 1;
                break;

            case TTIMELINE_ClearAllChannels:
            {
                ULONG ch;
                for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++)
                    ttl_clear_channel(inst, ch);
                redraw = TRUE;
                used = 1;
                break;
            }

            case TTIMELINE_ViewMode:
                /* Out-of-range values are ignored. Waiting is purely
                 * "the newly active channel's post list is empty" (see
                 * ttl_is_waiting) -- no separate flag needed. */
                if ((ULONG)tag->ti_Data < TTIMELINE_NUM_VIEWMODES &&
                    inst->viewMode != (ULONG)tag->ti_Data)
                {
                    inst->viewMode = (ULONG)tag->ti_Data;
                    /* Tiles only ever reflect one channel; a switch always
                     * invalidates them all and re-clamps scrollY (via
                     * layoutToDo -> ttl_do_layout) for the new channel. */
                    ttl_tiles_invalidate_all(inst);
                    inst->layoutToDo = TRUE;
                    redraw = TRUE;
                }
                used = 1;
                break;

            case TTIMELINE_WaitText: {
                const char *newText = (const char *)tag->ti_Data;
                if (inst->waitText) { FreeVec(inst->waitText); inst->waitText = NULL; }
                if (newText) {
                    ULONG len = (ULONG)strlen(newText);
                    inst->waitText = (char *)AllocVec(len + 1, MEMF_ANY);
                    if (inst->waitText) CopyMem((APTR)newText, inst->waitText, len + 1);
                }
                redraw = TRUE;
                used = 1;
                break;
            }

            case TTIMELINE_AvatarImages:
                inst->avatarImages = (struct AvatarImages *)tag->ti_Data;
                used = 1;
                break;

            case TTIMELINE_InvalidateImages:
                /* No layout/height change -- just redraw the currently
                 * active tiles so they pick up whatever image just
                 * finished loading into the cache. */
                ttl_tiles_invalidate_all(inst);
                redraw = TRUE;
                used = 1;
                break;

            case ICA_TARGET:
                inst->target = (Object *)tag->ti_Data;
                used = 1;
                break;
            case GA_ID:
                inst->ga_id = (ULONG)tag->ti_Data;
                used = 1;
                break;

            default:
                break;
        }
    }

    ReleaseSemaphore(&inst->listSem);

    if(couldRefreshDraw && msg->ops_GInfo && redraw)
    {
        struct RastPort *rp = ObtainGIRPort(msg->ops_GInfo);
        if (rp) {
            struct gpRender gpr;
            gpr.MethodID   = GM_RENDER;
            gpr.gpr_GInfo  = msg->ops_GInfo;
            gpr.gpr_RPort  = rp;
            gpr.gpr_Redraw = GREDRAW_REDRAW;
            DoMethodA(o, (Msg)&gpr);
            ReleaseGIRPort(rp);
        }

    }

    return used;
}

/* ------------------------------------------------------------------ */
/* OM_NEW                                                               */
/* ------------------------------------------------------------------ */

ULONG TTL_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    TTLData  *inst;
    Object   *newObj;
    ULONG     ch;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = TTL_DATA(cl, newObj);

    /* Defaults */
    inst->dpiHeight       = 14;
    inst->lineHeight      = 14;
    inst->lineAscent      = 11;
    inst->lastTileWidth   = -1;  /* force pool alloc on first layout */
    inst->layoutToDo = TRUE;

    /* Must be a valid channels[] index -- ttl_active() dereferences it
     * unconditionally, and nothing requires the caller to ever set
     * TTIMELINE_ViewMode explicitly. */
    inst->viewMode = 0;

    inst->callerTask = FindTask(NULL);

    InitSemaphore(&inst->listSem);

    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++)
        NewList((struct List *)&inst->channels[ch].posts);

    /* Hot-spot pool: all buckets start free (see the TTLHotSpot comment
     * in fs3etoottimeline_private.h). INST_DATA is zeroed by the class
     * system, but that's relied on implicitly enough elsewhere that it's
     * worth being explicit about the invariant here too. */
    for (ch = 0; ch < TTL_HOTSPOT_POOL_TOOTS; ch++)
        inst->hotSpotBucketOwner[ch] = NULL;
    inst->hotSpotNextBucket = 0;

    ttl_apply_tags(cl,newObj, msg,FALSE);

    return (ULONG)newObj;
}

/* ------------------------------------------------------------------ */
/* OM_DISPOSE                                                           */
/* ------------------------------------------------------------------ */

ULONG TTL_OnDispose(Class *cl, Object *o, Msg msg)
{
    TTLData *inst = TTL_DATA(cl, o);
    ULONG    ch;

    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++)
        ttl_clear_channel(inst, ch);
    ttl_tiles_free(inst);
    if (inst->waitText) { FreeVec(inst->waitText); inst->waitText = NULL; }

    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* ------------------------------------------------------------------ */
/* OM_SET / OM_UPDATE                                                   */
/* ------------------------------------------------------------------ */

ULONG TTL_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    TTLData *inst = TTL_DATA(cl, o);
    ULONG    rc;

    rc = DoSuperMethodA(cl, o, (APTR)msg);
    if( ttl_apply_tags(cl,o, msg,TRUE) ) rc = 1;
    return rc;
}

/* ------------------------------------------------------------------ */
/* OM_GET                                                               */
/* ------------------------------------------------------------------ */

ULONG TTL_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    TTLData *inst = TTL_DATA(cl, o);

    switch (msg->opg_AttrID) {
        case TTIMELINE_ScrollY:
            *msg->opg_Storage = (ULONG)ttl_active(inst)->scrollY;
            return 1;
        case TTIMELINE_ContentTopY:
            *msg->opg_Storage = (ULONG)ttl_active(inst)->contentTopY;
            return 1;
        case TTIMELINE_ContentBottomY:
            *msg->opg_Storage = (ULONG)ttl_active(inst)->contentBottomY;
            return 1;
        case TTIMELINE_ViewMode:
            *msg->opg_Storage = inst->viewMode;
            return 1;
        case TTIMELINE_WaitText:
            *msg->opg_Storage = (ULONG)inst->waitText;
            return 1;
        case TTIMELINE_LastHotSpotString:
            *msg->opg_Storage = inst->lastHotSpotStr[0] ? (ULONG)inst->lastHotSpotStr : 0;
            return 1;
        case TTIMELINE_LastHotSpotPostId:
            *msg->opg_Storage = inst->lastHotSpotPostId[0] ? (ULONG)inst->lastHotSpotPostId : 0;
            return 1;
        case TTIMELINE_NewestPostId: {
            /* Head-to-tail: first post with a known id is the newest one --
             * skips any non-toot pinned row (postId NULL), see the tag's
             * doc comment in fs3etoottimeline.h. Locked: this walk can run
             * concurrently with a GM_HANDLEINPUT hit-test, or any other
             * BOOPSI method on this gadget (SetAttrs included) dispatched
             * from a different task -- see the listSem comment in the
             * private header. Copies the found postId into a gadget-owned
             * buffer (inst->lastNewestPostId) *before* releasing the
             * semaphore, and returns a pointer to that instead of straight
             * into the post -- otherwise the instant the semaphore is
             * released, nothing stops that exact post from being freed
             * (e.g. TTIMELINE_ShowProfile clearing the channel) before the
             * caller gets around to actually reading through the pointer
             * this returns. Same reasoning as lastHotSpotPostId. */
            TTLPost *p;
            const char *found = NULL;
            ObtainSemaphore(&inst->listSem);
            for (p = (TTLPost *)ttl_active(inst)->posts.mlh_Head;
                 p->node.mln_Succ; p = (TTLPost *)p->node.mln_Succ)
            {
                if (p->postId && p->postId[0]) { found = p->postId; break; }
            }
            if (found) {
                strncpy(inst->lastNewestPostId, found, sizeof(inst->lastNewestPostId) - 1);
                inst->lastNewestPostId[sizeof(inst->lastNewestPostId) - 1] = '\0';
            } else {
                inst->lastNewestPostId[0] = '\0';
            }
            ReleaseSemaphore(&inst->listSem);
            *msg->opg_Storage = inst->lastNewestPostId[0] ? (ULONG)inst->lastNewestPostId : 0;
            return 1;
        }
        case TTIMELINE_OldestPostId: {
            /* Head-to-tail, keeping the last match: the oldest post with a
             * known id -- skips a pinned "load more" row at the tail. Same
             * copy-before-release reasoning as TTIMELINE_NewestPostId
             * above. */
            TTLPost    *p;
            const char *found = NULL;
            ObtainSemaphore(&inst->listSem);
            for (p = (TTLPost *)ttl_active(inst)->posts.mlh_Head;
                 p->node.mln_Succ; p = (TTLPost *)p->node.mln_Succ)
            {
                if (p->postId && p->postId[0]) found = p->postId;
            }
            if (found) {
                strncpy(inst->lastOldestPostId, found, sizeof(inst->lastOldestPostId) - 1);
                inst->lastOldestPostId[sizeof(inst->lastOldestPostId) - 1] = '\0';
            } else {
                inst->lastOldestPostId[0] = '\0';
            }
            ReleaseSemaphore(&inst->listSem);
            *msg->opg_Storage = inst->lastOldestPostId[0] ? (ULONG)inst->lastOldestPostId : 0;
            return 1;
        }
        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
