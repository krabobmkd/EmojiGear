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

#include "fs3etoottimeline_private.h"

/* ------------------------------------------------------------------ */
/* Apply a tag list to the instance data                                */
/* ------------------------------------------------------------------ */

static void ttl_apply_tags(TTLData *inst, struct TagItem *tags)
{
    struct TagItem *tstate = tags;
    struct TagItem *tag;

    while ((tag = NextTagItem(&tstate))) {
        switch (tag->ti_Tag) {
            case TTIMELINE_DpiHeight:
                inst->dpiHeight = (UWORD)tag->ti_Data;
                break;
            case TTIMELINE_ScrollY:
                inst->pendingScroll  = TRUE;
                inst->pendingScrollY = (LONG)tag->ti_Data;
                break;
            case TTIMELINE_DrawContext:
                inst->dc = (struct URPDrawContext *)tag->ti_Data;
                if (inst->dc) {
                    struct URPTextMetric m;
                    URPDC_GetFontLineMetrics(inst->dc, &m);
                    inst->lineHeight = (WORD)(m.height > 0 ? m.height : 14);
                    inst->lineAscent = (WORD)(m.baseY  > 0 ? m.baseY  : 11);
                }
                ttl_tiles_invalidate_all(inst);
                /* all posts need re-layout with new font metrics */
                ttl_layout_all_posts(inst);
                break;
            case TTIMELINE_Screen:
                inst->screen = (struct Screen *)tag->ti_Data;
                break;
            case TTIMELINE_Style:
                inst->style = (FS3EStyle *)tag->ti_Data;
                ttl_tiles_invalidate_all(inst);
                break;

            case TTIMELINE_AddPost: {
                const TTLPostSetup *setup = (const TTLPostSetup *)tag->ti_Data;
                if (setup) {
                    TTLPost *post = ttl_post_alloc(setup);
                    if (post) {
                        ttl_post_layout(inst, post);

                        if (inst->postCount == 0) {
                            /* Very first post: anchor at Y=0 */
                            inst->contentTopY    = 0;
                            inst->contentBottomY = post->height;
                            post->timelineY      = 0;
                        } else {
                            /* Prepend: new post goes above the current top.
                             * scrollY stays fixed so the user sees the same view. */
                            inst->contentTopY -= post->height;
                            post->timelineY    = inst->contentTopY;
                        }

                        AddHead((struct List *)&inst->posts, (struct Node *)&post->node);
                        inst->postCount++;

                        ttl_tiles_invalidate_range(inst,
                            post->timelineY,
                            post->timelineY + post->height);
                    }
                }
                break;
            }

            case TTIMELINE_ClearPosts:
                ttl_clear_posts(inst);
                break;

            case ICA_TARGET:
                inst->target = (Object *)tag->ti_Data;
                break;
            case GA_ID:
                inst->ga_id = (ULONG)tag->ti_Data;
                break;

            default:
                break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* OM_NEW                                                               */
/* ------------------------------------------------------------------ */

ULONG TTL_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    TTLData  *inst;
    Object   *newObj;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = TTL_DATA(cl, newObj);

    /* Defaults */
    inst->dpiHeight       = 14;
    inst->lineHeight      = 14;
    inst->lineAscent      = 11;
    inst->contentTopY     = 0;
    inst->contentBottomY  = 0;
    inst->scrollY         = 0;
    inst->lastTileWidth   = -1;  /* force pool alloc on first layout */

    NewList((struct List *)&inst->posts);

    ttl_apply_tags(inst, msg->ops_AttrList);

    return (ULONG)newObj;
}

/* ------------------------------------------------------------------ */
/* OM_DISPOSE                                                           */
/* ------------------------------------------------------------------ */

ULONG TTL_OnDispose(Class *cl, Object *o, Msg msg)
{
    TTLData *inst = TTL_DATA(cl, o);

    ttl_clear_posts(inst);
    ttl_tiles_free(inst);

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
    ttl_apply_tags(inst, msg->ops_AttrList);
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
            *msg->opg_Storage = (ULONG)inst->scrollY;
            return 1;
        case TTIMELINE_ContentTopY:
            *msg->opg_Storage = (ULONG)inst->contentTopY;
            return 1;
        case TTIMELINE_ContentBottomY:
            *msg->opg_Storage = (ULONG)inst->contentBottomY;
            return 1;
        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
