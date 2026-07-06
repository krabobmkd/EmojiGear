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
                         * channel it was laid out for. */
                        post = ttl_post_alloc(setup);
                        if (!post) continue;

                        channel = &inst->channels[ch];
                        ttl_post_layout(inst, post);

                        if (channel->postCount == 0) {
                            /* Very first post in this channel: anchor at Y=0 */
                            channel->contentTopY    = 0;
                            channel->contentBottomY = post->height;
                            post->timelineY         = 0;
                        } else {
                            /* Prepend: new post goes above the current top.
                             * scrollY stays fixed so the user sees the same view. */
                            channel->contentTopY -= post->height;
                            post->timelineY        = channel->contentTopY;
                        }

                        AddHead((struct List *)&channel->posts, (struct Node *)&post->node);
                        channel->postCount++;

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

            case TTIMELINE_ClearPosts:
                ttl_clear_posts(inst);
                redraw = TRUE;
                used = 1;
                break;

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

    for (ch = 0; ch < TTIMELINE_NUM_VIEWMODES; ch++)
        NewList((struct List *)&inst->channels[ch].posts);

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
        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
