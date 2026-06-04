/*
 * TextEditor gadget – GM_LAYOUT and GM_RENDER.
 *
 * Layout (uted_do_layout):
 *   Called from GM_LAYOUT and from GM_RENDER whenever the gadget dimensions
 *   differ from layoutedWidth/layoutedHeight (AddGadget gadgets don't always
 *   get GM_LAYOUT).
 *
 *   - Updates gadWidth/gadHeight/visibleLines/layoutedWidth/layoutedHeight.
 *   - Refreshes the color map when the screen pointer changes; marks all
 *     lines dirty so chunks are re-rendered with the new CLUT mapping.
 *   - Rebuilds the bitmap pool when lineHeight changes (font resize) or
 *     when the pool hasn't been allocated yet.  Pool size is
 *     (visibleLines + 2*POOL_LINE_BUFFER) * chunksPerGadWidth.
 *
 * GM_RENDER:
 *   1. Call uted_do_layout if size or screen changed.
 *   2. Eviction sweep: walk all lines; any line outside
 *      [scrollTopLine - POOL_LINE_BUFFER, scrollTopLine + visibleLines + POOL_LINE_BUFFER]
 *      has its chunk pointers returned to pool.  Runs only when scrollTopLine
 *      changes, so cost is zero during pure typing.
 *   3. For each visible line:
 *      a. If dirty: re-render all currently held chunk bitmaps in-place
 *         (no free/realloc → no chip RAM pressure), then clear dirty.
 *      b. For each chunk covering the visible horizontal range: borrow from
 *         pool + render if not held, then BltBitMapRastPort.
 *   4. Selection COMPLEMENT overlay.
 *   5. Text cursor COMPLEMENT bar.
 *   6. Fill unused area below last line.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <intuition/gadgetclass.h>
#include <devices/inputevent.h>
#include <graphics/gfx.h>
#include <stdio.h>
#include "unitexteditor_private.h"
#include "bdbprintf.h"

static BOOL uted_is_word_char_at(UniTextEditorLine *ln, ULONG ch)
{
    ULONG byteOff;
    unsigned char b;
    if (!ln || ch >= ln->charCount) return FALSE;
    byteOff = uted_char_to_byte(ln->utf8, ln->byteUsed, ch);
    b = (unsigned char)ln->utf8[byteOff];
    return (BOOL)(b != ' ' && b != '\t');
}
#include <proto/alib.h>
#include <proto/utility.h>

#include <proto/bevel.h>
#include <images/bevel.h>



#define G(o) ((struct Gadget *)(o))

/* =========================================================================
 * uted_do_layout  (internal)
 * =========================================================================
 */
static void uted_do_layout( Class *cl, Object *o,
                            WORD               newWidth,
                            WORD               newHeight,
                            struct Screen     *screen)
{
    UniTextEditorData *inst   = UTED_DATA(cl, o);

    /* Word-wrap: width change alters wrap points → must rebuild map */
    if (inst->wordWrap && newWidth != inst->layoutedWidth)
        inst->wrapMapDirty = TRUE;

    inst->layoutedWidth  = newWidth;
    inst->layoutedHeight = newHeight;
    inst->gadWidth  = newWidth;
    inst->gadHeight = newHeight;
    {
        WORD textH = newHeight - inst->topMargin - inst->bottomMargin;
        if (textH < 0) textH = 0;
        inst->visibleLines = (inst->lineHeight > 0)
                             ? (WORD)((textH / inst->lineHeight) + 1)
                             : 0;
    }

    /* Decide whether to rebuild the pool */
    if (inst->lineHeight > 0) {
        /* +1 for partial boundary chunk, +2*POOL_H_CHUNK_BUFFER for warm
         * chunks kept on both sides during horizontal scrolling. */
        ULONG chunksPerLine = ((ULONG)newWidth + LINE_CHUNK_WIDTH - 1)
                              / LINE_CHUNK_WIDTH + 1 + (2 * POOL_H_CHUNK_BUFFER);
        ULONG neededSize    = ((ULONG)inst->visibleLines + 2 * POOL_LINE_BUFFER)
                              * chunksPerLine;

        {
            BOOL noPool        = (!inst->bmPool.bitmaps);
            BOOL heightChanged = (!noPool &&
                                  inst->bmPool.height != (UWORD)inst->lineHeightBase);
            BOOL tooSmall      = (!noPool && !heightChanged &&
                                  inst->bmPool.size < neededSize);
            BOOL screenModechange = (screen != inst->screen);

            if (noPool || heightChanged || tooSmall || screenModechange) {
                UniTextEditorLine *line;
                struct Screen *useScreen = screen ? screen : inst->screen;

                // bdbprintf(" *** rebuildPool noPool=%d heightChanged=%d tooSmall=%d old=%d need=%d\n",
                //           (int)noPool, (int)heightChanged, (int)tooSmall,
                //           (int)inst->bmPool.size, (int)neededSize);

                /* Return all borrowed bitmaps before touching the pool */
    ObtainSemaphore(&inst->cacheSem);
                for (line = (UniTextEditorLine *)inst->lines.mlh_Head;
                     line->node.mln_Succ;
                     line = (UniTextEditorLine *)line->node.mln_Succ)
                    uted_line_evict_chunks(inst, line);

                if (noPool || heightChanged) {
                    /* Line height changed (font resize) or first alloc:
                     * existing bitmaps have the wrong dimensions – rebuild. */
                    uted_pool_free_bitmapcache(&inst->bmPool);
                    uted_pool_alloc(&inst->bmPool, neededSize,
                                    (UWORD)inst->lineHeightBase, useScreen);
                } else {
                    /* Same line height, window grew: reuse existing bitmaps,
                     * allocate only the additional slots needed. */
                    uted_pool_growalloc(&inst->bmPool, neededSize, useScreen);
                }

    ReleaseSemaphore(&inst->cacheSem);
            }
            if(screenModechange)
            {
                if(inst->dc && screen)
                {
                    URPDC_SetDrawScreen(inst->dc, screen);
                }
                uted_update_halfway_pen(inst);

                inst->screen = screen;
            }
        }
    }

   /* when a rastport is given or created rto draw on gadget,
        It is most often delivered with no clipping at layer level.
        Optionally during draw we can force installation of a clipping rect,
        that will cut any graphics.library drawing cals using RastPort.
        Here we refresh a Region geometry that is used as clipping at draw.
      */

    if (inst->clipRegion)
    {
        struct Rectangle framerect;
        framerect.MinX = G(o)->LeftEdge + inst->leftMargin;
        framerect.MinY = G(o)->TopEdge + inst->topMargin;
        framerect.MaxX = G(o)->LeftEdge  + G(o)->Width -1 - inst->rightMargin;
        framerect.MaxY = G(o)->TopEdge + G(o)->Height -1 - inst->bottomMargin;

        /* note you can go wild with Or/And boolean operation on geometry.
           But a single rect (should be) enough at Gadget level.
           Note there are issues on "Virtual.class" up to OS3.2.2 with this.
           One of the reason we don't use Virtual.class.
           */
        ClearRegion(inst->clipRegion);
        OrRectRegion(inst->clipRegion, &framerect);
    }

    if(inst->bevel)
    {
        SetAttrs((Object *)inst->bevel,
            IA_Left,        (ULONG)G(o)->LeftEdge,
            IA_Top,        (ULONG)G(o)->TopEdge,
            IA_Width,      (ULONG)G(o)->Width,
            IA_Height,     (ULONG)G(o)->Height,
            TAG_DONE);
        inst->redrawBevel = TRUE;
    }

}

/* =========================================================================
 * GM_LAYOUT
 * =========================================================================
 */
ULONG UniTextEditor_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    UniTextEditorData *inst   = UTED_DATA(cl, o);
/* layouting from here should not be problematic,
 * but we have AllocBitMap and layers things to do.
   on "some configurations" or video driver this happens
   on the wrong context, so we just ask an external refresh
   from a regular process context , and layouting will ocuur from GM_RENDER.

*/

    if((FindTask(NULL) == inst->callerTask) &&  msg->gpl_GInfo && msg->gpl_GInfo->gi_Screen )
    {

        uted_do_layout(cl,o, G(o)->Width, G(o)->Height, msg->gpl_GInfo->gi_Screen);
        /* MUI integration need scroller refresh at this level */
        // if (msg->gpl_GInfo)
        // {
        //     uted_notify(cl, o, msg->gpl_GInfo, UTEDN_ScrollChanged, inst->scrollTopLine);
        // }
    }else
    if (msg->gpl_GInfo)
    {
        uted_notify(cl, o, msg->gpl_GInfo, UTEDN_ScrollChanged, inst->scrollTopLine);
    }

    return TRUE;
}

/* =========================================================================
 * GM_DOMAIN
 * =========================================================================
 */
ULONG UniTextEditor_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    struct IBox *domain = &msg->gpd_Domain;
    UniTextEditorData *inst = UTED_DATA(cl, o);
    // Set default dimensions
    domain->Left = 0;
    domain->Top = 0;
        //test
            // domain->Width = 32767;
            // domain->Height = 32767;
            // return 1;
    /* Use actual rendered line height (includes descenders) when available;
     * fall back to pointSize before the first font is loaded. */
    ULONG lineH = (inst->lineHeightBase > 0)
                  ? (ULONG)inst->lineHeightBase
                  : inst->pointSize;

    /* let's default as minimum */
    domain->Width = 64+inst->leftMargin+inst->rightMargin;
    domain->Height = lineH + inst->lineSpacing + inst->topMargin + inst->bottomMargin;

    // Adjust based on gpd_Which
    switch (msg->gpd_Which) {
        case GDOMAIN_MINIMUM:
            /* Use default values*/
            break;
        case GDOMAIN_MAXIMUM:
            /* default maximum, watch out values are 2 bytes signed "WORD" */
            domain->Width = 32767;
            domain->Height = 32767;
            if (inst->maxDisplayLines < 65535UL) {

                ULONG maxH = (lineH + inst->lineSpacing)
                             * inst->maxDisplayLines
                             + inst->topMargin
                             + inst->bottomMargin;
                if (maxH > 32767) maxH = 32767;
                msg->gpd_Domain.Height = (WORD)maxH;
            }
            break;
        case GDOMAIN_NOMINAL:
            /* actually rarely used */
        default:
            /* Use default values*/
            break;
    }
    return 1;

}

/* =========================================================================
 * GM_RENDER
 * =========================================================================
 */
ULONG UniTextEditor_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    struct Region *oldClipRegion=NULL;
    UniTextEditorData *inst   = UTED_DATA(cl, o);
    struct RastPort   *rp     = msg->gpr_RPort;
    WORD               left   = G(o)->LeftEdge;
    WORD               top    = G(o)->TopEdge;
    WORD               width  = G(o)->Width;
    WORD               height = G(o)->Height;
    struct Screen     *scr;

    ULONG   lineIdx;
    WORD    absY;
    BOOL    hasSelStart, hasSelEnd;
    const UniTextEditorPos *selStart = NULL, *selEnd = NULL;
    WORD    textLeft;
    WORD    textTop;
    WORD    textWidth;
    WORD    textHeight;
    WORD    bevelLeft;   /* BEVEL_VertSize:  pixels of bevel on left/right sides */
    WORD    bevelTop;    /* BEVEL_HorizSize: pixels of bevel on top/bottom sides */
    BOOL    needLayout;
    BOOL    isFullRefresh;
    ULONG   rStart, rEnd;
    bevelLeft = 0;
    bevelTop  = 0;

    /* forbid out of context rendering - else crash on some video drivers */
    if(!rp || !(msg->gpr_GInfo)) return TRUE;
    scr    = msg->gpr_GInfo->gi_Screen;
    if(!scr) return TRUE;

    /* there is a necessity to not draw from interuptions,
     * device contexts, etc...
     * No idea why, but my A1200 pistorm with OS 3.2.3+ P96 indivision
     * send the resize draw from "Another context"
     * When my UAE also OS3.2.3 , also P96 indi, wether it uses
     * AGA or P96, send it on regular task...
     */
    if(FindTask(NULL) != inst->callerTask)
    {
        /* sorry, but on the right process will you ? */
        uted_notify(cl, o, msg->gpr_GInfo, UTEDN_ScrollChanged, inst->scrollTopLine);
        return TRUE;
    }

    ObtainSemaphore(&inst->textSem);
    ObtainSemaphore(&inst->cacheSem);

// bdbprintf("UniTextEditor_OnRender t:%08x\n",(int)FindTask(NULL));

    if (inst->bevel) {
        ULONG v = 0;
        GetAttr(BEVEL_VertSize,  inst->bevel, &v); bevelLeft = (WORD)v;
        v = 0;
        GetAttr(BEVEL_HorizSize, inst->bevel, &v); bevelTop  = (WORD)v;
    }
    textLeft   = (WORD)((LONG)left  + (LONG)inst->leftMargin);
    textTop    = (WORD)((LONG)top   + (LONG)inst->topMargin);
    textWidth  = (WORD)((LONG)width - (LONG)inst->leftMargin - (LONG)inst->rightMargin);
    textHeight = (WORD)((LONG)height - (LONG)inst->topMargin - (LONG)inst->bottomMargin);
    if (textWidth  < 0) textWidth  = 0;
    if (textHeight < 0) textHeight = 0;


    /* ------------------------------------------------------------------
     * Determine refresh scope BEFORE layout so a geometry change forces
     * a full redraw regardless of the stored line range.
     * gpr_Redraw == GREDRAW_UPDATE  → triggered by SetGadgetAttrs (edit)
     * gpr_Redraw == GREDRAW_REDRAW  → triggered by Intuition (expose, resize)
     * ------------------------------------------------------------------ */
    needLayout = (width  != inst->layoutedWidth  ||
                  height != inst->layoutedHeight ||
                  (scr && scr != inst->screen));

    if (msg->gpr_Redraw != GREDRAW_UPDATE || needLayout) {
        isFullRefresh = TRUE;
        rStart = 0;
        rEnd   = ~0UL;
    } else {
        rStart = (ULONG)inst->refreshStartLine;
        rEnd   = (ULONG)inst->refreshEndLine;
        isFullRefresh = (rStart == 0 && rEnd == ~0UL);
    }
    /* Reset so any subsequent Intuition-triggered redraw is always full */
    inst->refreshStartLine = 0;
    inst->refreshEndLine   = ~0L;

    if (needLayout)
    {
        uted_do_layout(cl, o, width, height, scr);
    }

    /* Rebuild wrap map if text or width changed since last render */
    if (inst->wordWrap && inst->wrapMapDirty)
        uted_rebuild_wrap_map(inst);

    if (!inst->dc || width <= 0 || height <= 0 || inst->lineHeight <= 0) {
        /* lineHeight 0 happens when no font. */
         SetAPen(rp, (LONG)inst->bgPen);
         RectFill(rp, (LONG)left, (LONG)top,
                  (LONG)(left + width - 1), (LONG)(top + height - 1));
    ReleaseSemaphore(&inst->cacheSem);
    ReleaseSemaphore(&inst->textSem);
        return 0;
    }

    /* ------------------------------------------------------------------
     * Replay deferred mouse input from the input.device context.
     *
     * OnGoActive / OnHandleInput must not call FreeType or disk APIs, so
     * they only store raw coordinates.  Here, in the safe application-task
     * context, we perform the actual hit-test (which may trigger
     * uted_line_build_metrics → URPDC_TextSizeUTF8 → FreeType disk I/O)
     * and update the cursor / selection.
     *
     * Order matters: the click (extend=FALSE, sets anchor) must be replayed
     * before any drag (extend=TRUE, moves float), even when both arrived
     * between two consecutive GM_RENDER calls.
     * ------------------------------------------------------------------ */
    /* Drain deferred keyboard events (pushed by GM_HANDLEINPUT in input.device context) */
    if (inst->pendingKeyCount > 0) {
        UBYTE i;
        for (i = 0; i < inst->pendingKeyCount; i++) {
            struct InputEvent *pie = &inst->pendingKeys[i];
            uted_manageFullRawKey(cl, o, pie, msg->gpr_GInfo);          
        }
        inst->pendingKeyCount = 0;
    }

    /* Replay deferred internal scrollbar interaction.
     * Scrollbar clicks and drags are stored as a gadget-relative Y; here we
     * map that Y proportionally to a new scrollTopLine.  Must run before the
     * text click/drag replay so a scroll triggered by the bar does not also
     * reposition the text cursor. */
    if (inst->pendingVScrollClick || inst->pendingVScrollDrag) {
        ULONG totalRows = inst->wordWrap ? inst->wrapRowCount : inst->lineCount;
        if (totalRows > (ULONG)inst->visibleLines && textHeight > 0) {
            LONG relY = (LONG)inst->pendingVScrollY - (LONG)inst->topMargin;
            if (relY < 0) relY = 0;
            if (relY >= (LONG)textHeight) relY = (LONG)textHeight - 1;
            ULONG newTop = (ULONG)((LONG)relY * (LONG)totalRows / (LONG)textHeight);
            ULONG maxTop = totalRows - (ULONG)inst->visibleLines;
            if (newTop > maxTop) newTop = maxTop;
            inst->scrollTopLine = newTop;
            uted_notify(cl, o, msg->gpr_GInfo, UTEDN_ScrollChanged, inst->scrollTopLine);
        }
        inst->pendingVScrollClick = FALSE;
        inst->pendingVScrollDrag  = FALSE;
    }

    if (inst->pendingClick || inst->pendingDrag) {
        ULONG hitLine = 0, hitCh = 0;

        if (inst->pendingClick) {
            BOOL extend = (inst->pendingClickQualifiers &
                           (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                          ? TRUE : FALSE;
            UniTextEditor_DoHitTest(cl, o,
                inst->pendingClickX, inst->pendingClickY,
                &hitLine, &hitCh);

            if (inst->pendingClickCount >= 3) {
                /* Triple click: select entire line */
                UniTextEditorLine *hitLn = uted_get_line(inst, hitLine);
                ULONG lineEnd = hitLn ? hitLn->charCount : 0;
                UniTextEditor_DoSetCursorPos(cl, o, hitLine, 0, FALSE);
                if (hitLine + 1 < inst->lineCount)
                    UniTextEditor_DoSetCursorPos(cl, o, hitLine + 1, 0, TRUE);
                else
                    UniTextEditor_DoSetCursorPos(cl, o, hitLine, lineEnd, TRUE);
                inst->clickAnchorValid = FALSE;

            } else if (inst->pendingClickCount == 2) {
                /* Double click: select word at click position */
                UniTextEditorLine *hitLn = uted_get_line(inst, hitLine);
                if (hitLn && uted_is_word_char_at(hitLn, hitCh)) {
                    ULONG wordStart = hitCh;
                    ULONG wordEnd   = hitCh;
                    while (wordStart > 0 && uted_is_word_char_at(hitLn, wordStart - 1))
                        wordStart--;
                    while (wordEnd < hitLn->charCount && uted_is_word_char_at(hitLn, wordEnd))
                        wordEnd++;
                    UniTextEditor_DoSetCursorPos(cl, o, hitLine, wordStart, FALSE);
                    UniTextEditor_DoSetCursorPos(cl, o, hitLine, wordEnd,   TRUE);
                } else {
                    UniTextEditor_DoSetCursorPos(cl, o, hitLine, hitCh, extend);
                }
                inst->clickAnchorValid = FALSE;

            } else {
                /* Single click: place cursor, record anchor for drag */
                UniTextEditor_DoSetCursorPos(cl, o, hitLine, hitCh, extend);
                if (!extend) {
                    UniTextEditorLine *hitLn = uted_get_line(inst, hitLine);
                    WORD pixX = (WORD)((LONG)inst->pendingClickX
                                       - (LONG)inst->leftMargin
                                       + (LONG)inst->scrollLeftPx);
                    inst->clickAnchorChFloor = (hitLn && hitLn->charXOffsets)
                        ? uted_x_to_char_floor(hitLn, pixX)
                        : hitCh;
                    inst->clickAnchorLine  = hitLine;
                    inst->clickAnchorCh    = hitCh;
                    inst->clickAnchorValid = TRUE;
                } else {
                    /* Shift-click extends from existing anchor; no new floor. */
                    inst->clickAnchorValid = FALSE;
                }
            }
            inst->pendingClick = FALSE;
        }

        if (inst->pendingDrag && inst->pendingClickCount >= 2) {
            /* Word/line selection from double/triple click: the release
             * event must not override the selection we just set. */
            inst->pendingDrag = FALSE;
        }

        if (inst->pendingDrag) {
            UniTextEditor_DoHitTest(cl, o,
                inst->pendingDragX, inst->pendingDragY,
                &hitLine, &hitCh);

            /* Directional anchor: ensure the originally clicked character is
             * always included in the selection regardless of drag direction.
             * Skip when the drag position equals the click cursor position
             * (plain click-and-release without movement → no selection). */
            if (inst->clickAnchorValid &&
                (hitLine != inst->clickAnchorLine ||
                 hitCh   != inst->clickAnchorCh))
            {
                BOOL rightward = (hitLine > inst->clickAnchorLine) ||
                                 (hitLine == inst->clickAnchorLine &&
                                  hitCh   >  inst->clickAnchorChFloor);
                if (rightward) {
                    /* Anchor at left edge of clicked char → char is included */
                    inst->selAnchor.line = inst->clickAnchorLine;
                    inst->selAnchor.ch   = inst->clickAnchorChFloor;
                } else {
                    /* Anchor at right edge of clicked char → char is included */
                    UniTextEditorLine *ancLn =
                        uted_get_line(inst, inst->clickAnchorLine);
                    ULONG ancChRight = inst->clickAnchorChFloor + 1;
                    if (ancLn && ancChRight > ancLn->charCount)
                        ancChRight = ancLn->charCount;
                    inst->selAnchor.line = inst->clickAnchorLine;
                    inst->selAnchor.ch   = ancChRight;
                }
            }

            UniTextEditor_DoSetCursorPos(cl, o, hitLine, hitCh, TRUE);
            inst->pendingDrag = FALSE;
        }
    }


    /* CLUT (indexed palette) screens need special selection/cursor rendering.
     * COMPLEMENT on a palette index produces arbitrary colors, not inversions. */
    {
        ULONG depth = GetBitMapAttr(rp->BitMap, BMA_DEPTH);
        inst->isCLUT = (depth <= 8);
    }


    /* allow final blits on the rastport to be really clipped
      to the gadget frame rectangle. oldClipRegion can be NULL
     but must be restored in all case after draw */
    oldClipRegion = InstallClipRegion( rp->Layer, inst->clipRegion);

    /* ------------------------------------------------------------------
     * Eviction sweep: return chunk bitmaps that are no longer needed back
     * to the pool so they can be reused by newly visible content.
     *
     * Triggers on vertical OR horizontal scroll change.
     *
     * Vertical keep zone  : [scrollTopLine - V_BUFFER, scrollTopLine + visible + V_BUFFER)
     * Horizontal keep zone: [firstChunk - H_BUFFER,    lastChunk + H_BUFFER]
     *
     * Lines outside the vertical zone → evict ALL chunks.
     * Lines inside  the vertical zone → evict only out-of-horizontal-range chunks.
     * ------------------------------------------------------------------ */
    {
        ULONG firstVisChunk  = (ULONG)inst->scrollLeftPx / LINE_CHUNK_WIDTH;
        ULONG lastVisChunk   = ((ULONG)inst->scrollLeftPx + (ULONG)(textWidth > 0 ? textWidth - 1 : 0))
                               / LINE_CHUNK_WIDTH;

        if (inst->scrollTopLine   != inst->lastEvictTopLine ||
            firstVisChunk         != inst->lastEvictLeftChunk)
        {
            ULONG keepVStart, keepVEnd;
            ULONG keepHFirst, keepHLast;
            ULONG idx;
            UniTextEditorLine *line;

            keepVStart = (inst->scrollTopLine > POOL_LINE_BUFFER)
                         ? inst->scrollTopLine - POOL_LINE_BUFFER : 0;
            keepVEnd   = inst->scrollTopLine + (ULONG)inst->visibleLines + POOL_LINE_BUFFER;

            keepHFirst = (firstVisChunk > POOL_H_CHUNK_BUFFER)
                         ? firstVisChunk - POOL_H_CHUNK_BUFFER : 0;
            keepHLast  = lastVisChunk + POOL_H_CHUNK_BUFFER;

            idx  = 0;
            line = (UniTextEditorLine *)inst->lines.mlh_Head;
            while (line->node.mln_Succ) {
                if (line->chunkAlloc > 0) {
                    if (idx < keepVStart || idx >= keepVEnd)
                        uted_line_evict_chunks(inst, line);        /* off-screen line: drop all */
                    else
                        uted_line_evict_distant_chunks(inst, line, /* on-screen line: horizontal trim */
                                                        keepHFirst, keepHLast);
                }
                idx++;
                line = (UniTextEditorLine *)line->node.mln_Succ;
            }

            inst->lastEvictTopLine   = inst->scrollTopLine;
            inst->lastEvictLeftChunk = firstVisChunk;
        }
    }

    /* Determine selection order */
    if (inst->hasSelection) {
        selStart = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) <= 0)
                   ? &inst->selAnchor : &inst->selFloat;
        selEnd   = (selStart == &inst->selAnchor) ? &inst->selFloat : &inst->selAnchor;
    }

    /* -----------------------------------------------------------------------
     * Helper macro: blit visible horizontal chunks of `line` using
     * `effScrollLeft` as the left pixel offset.
     * Declared as a macro so it can be reused in both render loops below.
     * ----------------------------------------------------------------------- */
#define UTED_BLIT_LINE_CHUNKS(line_, effScrollLeft_) \
    do { \
        ULONG firstChunk_ = (ULONG)(effScrollLeft_) / LINE_CHUNK_WIDTH; \
        ULONG lastChunk_  = ((ULONG)(effScrollLeft_) + (ULONG)(textWidth - 1)) \
                            / LINE_CHUNK_WIDTH; \
        ULONG c_; \
        for (c_ = firstChunk_; c_ <= lastChunk_; c_++) { \
            LONG chunkL_ = (LONG)c_ * LINE_CHUNK_WIDTH; \
            LONG chunkR_ = chunkL_ + LINE_CHUNK_WIDTH - 1; \
            LONG visL_   = (LONG)(effScrollLeft_); \
            LONG visR_   = visL_ + (LONG)(textWidth - 1); \
            LONG oStart_ = (chunkL_ > visL_) ? chunkL_ : visL_; \
            LONG oEnd_   = (chunkR_ < visR_) ? chunkR_ : visR_; \
            WORD srcX_, dstX_, blitW_; \
            if (oStart_ > oEnd_) continue; \
            srcX_  = (WORD)(oStart_ - chunkL_); \
            dstX_  = (WORD)(textLeft + (oStart_ - visL_)); \
            blitW_ = (WORD)(oEnd_ - oStart_ + 1); \
            if (!(line_)->chunks || c_ >= (line_)->chunkAlloc || !(line_)->chunks[c_]) \
                uted_line_render_chunk(inst, (line_), c_); \
            if ((line_)->chunks && c_ < (line_)->chunkAlloc && (line_)->chunks[c_]) { \
                BltBitMapRastPort((line_)->chunks[c_], \
                                  (LONG)srcX_, 0, rp, \
                                  (LONG)dstX_, (LONG)absY, \
                                  (LONG)blitW_, (LONG)inst->lineHeightBase, 0xC0); \
            } else { \
                SetAPen(rp, (LONG)inst->bgPen); \
                RectFill(rp, (LONG)dstX_, (LONG)absY, \
                         (LONG)(dstX_ + blitW_ - 1), \
                         (LONG)(absY + inst->lineHeightBase - 1)); \
            } \
        } \
        if (inst->lineSpacing > 0) { \
            SetAPen(rp, (LONG)inst->bgPen); \
            RectFill(rp, (LONG)left, (LONG)(absY + inst->lineHeightBase), \
                     (LONG)(left + width - 1), \
                     (LONG)(absY + inst->lineHeight - 1)); \
        } \
    } while(0)

    /* -----------------------------------------------------------------------
     * Helper macro: draw tab-stop markers for the chars in [firstChar_,lastChar_)
     * of `line_` onto the screen rastport.  `effScrollLeft_` is the pixel offset
     * of the left edge of the logical line's pixel space (scrollLeftPx or
     * wr->startPixel in word-wrap mode).  Must be called with a valid rp and
     * with line_->charXOffsets already populated.
     * ----------------------------------------------------------------------- */
#define UTED_DRAW_TAB_MARKERS(line_, firstChar_, lastChar_, effScrollLeft_) \
    do { \
        if (inst->visibleTabs && (line_)->charXOffsets) { \
            ULONG _tci; \
            ULONG _byteOff = uted_char_to_byte((line_)->utf8, (line_)->byteUsed, (firstChar_)); \
            WORD  _lineY   = (WORD)(absY + inst->lineAscent); \
            SetAPen(rp, (LONG)inst->halfwayPen); \
            SetDrMd(rp, JAM2); \
            for (_tci = (firstChar_); _tci < (lastChar_); _tci++) { \
                unsigned char _b = (unsigned char)(line_)->utf8[_byteOff]; \
                if (_b == 0x09) { \
                    WORD _lx0 = (WORD)(textLeft + (LONG)(line_)->charXOffsets[_tci]     + 2 - (LONG)(effScrollLeft_)); \
                    WORD _lx1 = (WORD)(textLeft + (LONG)(line_)->charXOffsets[_tci + 1] - 4 - (LONG)(effScrollLeft_)); \
                    if (_lx0 < textLeft) _lx0 = textLeft; \
                    if (_lx1 >= textLeft + textWidth) _lx1 = (WORD)(textLeft + textWidth - 1); \
                    if (_lx0 <= _lx1) \
                        RectFill(rp, (LONG)_lx0, (LONG)_lineY, (LONG)_lx1, (LONG)_lineY); \
                } \
                if      (_b < 0x80) _byteOff += 1; \
                else if (_b < 0xE0) _byteOff += 2; \
                else if (_b < 0xF0) _byteOff += 3; \
                else                _byteOff += 4; \
            } \
        } \
    } while(0)

    /* -----------------------------------------------------------------------
     * WORD-WRAP render loop
     * scrollTopLine is a visual row index; wrapMap maps rows to logical lines.
     * ----------------------------------------------------------------------- */
    if (inst->wordWrap && inst->wrapMap && inst->wrapRowCount > 0) {
        ULONG visRow;
        for (visRow = inst->scrollTopLine;
             visRow < inst->scrollTopLine + (ULONG)inst->visibleLines;
             visRow++)
        {
            absY = (WORD)(textTop + (LONG)(visRow - inst->scrollTopLine)
                          * inst->lineHeight);

            if (visRow < inst->wrapRowCount) {
                UTEDWrapRow     *wr   = &inst->wrapMap[visRow];
                UniTextEditorLine *line;
                if (!isFullRefresh &&
                    (wr->logicalLine < rStart || wr->logicalLine > rEnd))
                    continue;
                line = uted_get_line(inst, wr->logicalLine);
                if (!line) break;

                if (line->dirty) {
                    ULONG ci;
                    for (ci = 0; ci < line->chunkAlloc; ci++)
                        if (line->chunks[ci])
                            uted_line_render_chunk(inst, line, ci);
                    line->dirty = FALSE;
                }

                UTED_BLIT_LINE_CHUNKS(line, wr->startPixel);

                /* For non-last visual rows of a logical line, blank the area
                 * right of the wrap point so wrapped text isn't shown twice. */
                if (wr->endChar < line->charCount && line->charXOffsets) {
                    LONG clipX = (LONG)textLeft
                                 + (LONG)line->charXOffsets[wr->endChar]
                                 - (LONG)wr->startPixel;
                    if (clipX < (LONG)(textLeft + textWidth)) {
                        if (clipX < (LONG)textLeft) clipX = (LONG)textLeft;
                        SetAPen(rp, (LONG)inst->bgPen);
                        RectFill(rp, clipX, (LONG)absY,
                                 (LONG)(textLeft + textWidth - 1),
                                 (LONG)(absY + inst->lineHeightBase - 1));
                    }
                }

                UTED_DRAW_TAB_MARKERS(line, wr->startChar, wr->endChar, wr->startPixel);

                /* Selection overlay */
                if (!inst->readOnly && inst->hasSelection && selStart && selEnd) {
                    ULONG logL = wr->logicalLine;
                    if (logL >= selStart->line && logL <= selEnd->line) {
                        ULONG vss = (logL == selStart->line)
                                    ? selStart->ch : wr->startChar;
                        ULONG vse = (logL == selEnd->line)
                                    ? selEnd->ch   : wr->endChar;
                        /* clamp to this visual row's char range */
                        if (vss < wr->startChar) vss = wr->startChar;
                        if (vse > wr->endChar)   vse = wr->endChar;
                        if (vss < vse) {
                            WORD selX1, selX2;
                            if (!line->charXOffsets)
                                uted_line_build_metrics(line, inst);
                            if (line->charXOffsets) {
                                selX1 = (WORD)(textLeft
                                    + (LONG)line->charXOffsets[vss]
                                    - (LONG)wr->startPixel);
                                selX2 = (WORD)(textLeft
                                    + (LONG)line->charXOffsets[vse]
                                    - (LONG)wr->startPixel - 1);
                            } else {
                                selX1 = textLeft;
                                selX2 = (WORD)(textLeft + textWidth - 1);
                            }
                            if (selX1 < textLeft) selX1 = textLeft;
                            if (selX2 >= textLeft + textWidth) selX2 = (WORD)(textLeft + textWidth - 1);
                            if (selX1 <= selX2) {
                                if (!inst->isCLUT) {
                                    SetAPen(rp, 0xFF);
                                    SetDrMd(rp, COMPLEMENT);
                                    RectFill(rp, (LONG)selX1, (LONG)absY,
                                             (LONG)selX2,
                                             (LONG)(absY + inst->lineHeightBase - 1));
                                    SetDrMd(rp, JAM2);
                                } else {
                                    SetAPen(rp, (LONG)inst->txtPen);
                                    SetBPen(rp, (LONG)inst->txtPen);
                                    SetDrMd(rp, JAM2);
                                    RectFill(rp, (LONG)selX1, (LONG)absY,
                                             (LONG)selX2,
                                             (LONG)(absY + inst->lineHeightBase - 1));
                                    if (inst->screen && line->charXOffsets && vse > vss) {
                                        ULONG startByte = uted_char_to_byte(
                                            line->utf8, line->byteUsed, vss);
                                        struct URPTextPos pos;
                                        pos.x = selX1;
                                        pos.y = (WORD)(absY + inst->lineAscent);
                                        URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                            (LONG)inst->bgPen, (LONG)inst->txtPen);
                                        SetAPen(rp, (LONG)inst->bgPen);
                                        SetBPen(rp, (LONG)inst->txtPen);
                                        SetDrMd(rp, JAM2);
                                        URPDrawTextUTF8(rp, inst->dc, &pos,
                                            line->utf8 + startByte, vse - vss);
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                /* Below last visual row */
                if (isFullRefresh || rEnd == ~0UL) {
                    SetAPen(rp, (LONG)inst->bgPen);
                    RectFill(rp, (LONG)left, (LONG)absY,
                             (LONG)(left + width - 1),
                             (LONG)(absY + inst->lineHeight - 1));
                }
            }
        }

        /* Cursor in word-wrap mode */
        if (!inst->readOnly &&
            (isFullRefresh || (inst->cursor.line >= rStart &&
                               inst->cursor.line <= rEnd))) {
            ULONG curVisRow = uted_cursor_visual_row(inst);
            if (curVisRow >= inst->scrollTopLine &&
                curVisRow < inst->scrollTopLine + (ULONG)inst->visibleLines &&
                curVisRow < inst->wrapRowCount)
            {
                UTEDWrapRow *wr   = &inst->wrapMap[curVisRow];
                UniTextEditorLine *cl2 = uted_get_line(inst, wr->logicalLine);
                if (cl2) {
                    WORD cy = (WORD)(textTop + (LONG)(curVisRow - inst->scrollTopLine)
                                    * inst->lineHeight);
                    WORD cx = textLeft;
                    if (!cl2->charXOffsets) uted_line_build_metrics(cl2, inst);
                    if (cl2->charXOffsets && inst->cursor.ch <= cl2->charCount)
                        cx = (WORD)(textLeft
                            + (LONG)cl2->charXOffsets[inst->cursor.ch]
                            - (LONG)wr->startPixel);
                    if (cx >= textLeft && cx < textLeft + textWidth - 1) {
                        if (!inst->isCLUT) {
                            SetAPen(rp, 0xFF); SetDrMd(rp, COMPLEMENT);
                            RectFill(rp, (LONG)cx, (LONG)cy,
                                     (LONG)(cx+1), (LONG)(cy+inst->lineHeightBase-1));
                            SetDrMd(rp, JAM2);
                        } else {
                            SetAPen(rp, (LONG)inst->txtPen); SetDrMd(rp, JAM1);
                            RectFill(rp, (LONG)cx, (LONG)cy,
                                     (LONG)(cx+1), (LONG)(cy+inst->lineHeightBase-1));
                            SetDrMd(rp, JAM2);
                        }
                    }
                }
            }
        }

    } else {

    /* -----------------------------------------------------------------------
     * NON-WRAP render loop (original, unchanged)
     * ----------------------------------------------------------------------- */
    for (lineIdx = inst->scrollTopLine;
         lineIdx < inst->scrollTopLine + (ULONG)inst->visibleLines;
         lineIdx++)
    {
        absY = (WORD)(textTop + (LONG)(lineIdx - inst->scrollTopLine) * inst->lineHeight);

        if (lineIdx < inst->lineCount) {
            ULONG firstChunk, lastChunk, c;
            UniTextEditorLine *line;
            if (!isFullRefresh && (lineIdx < rStart || lineIdx > rEnd))
                continue;
            line = uted_get_line(inst, lineIdx);
            if (!line) break;

            if (line->dirty) {
                ULONG ci;
                for (ci = 0; ci < line->chunkAlloc; ci++) {
                    if (line->chunks[ci])
                        uted_line_render_chunk(inst, line, ci);
                }
                line->dirty = FALSE;
            }


            firstChunk = (ULONG)inst->scrollLeftPx / LINE_CHUNK_WIDTH;
            lastChunk  = (ULONG)(inst->scrollLeftPx + (ULONG)(textWidth - 1))
                         / LINE_CHUNK_WIDTH;

            for (c = firstChunk; c <= lastChunk; c++) {
                LONG chunkL = (LONG)c * LINE_CHUNK_WIDTH;
                LONG chunkR = chunkL + LINE_CHUNK_WIDTH - 1;
                LONG visL   = (LONG)inst->scrollLeftPx;
                LONG visR   = visL + (LONG)(textWidth - 1);
                LONG overlapStart = (chunkL > visL) ? chunkL : visL;
                LONG overlapEnd   = (chunkR < visR) ? chunkR : visR;
                WORD srcX, dstX, blitW;

                if (overlapStart > overlapEnd) continue;

                srcX  = (WORD)(overlapStart - chunkL);
                dstX  = (WORD)(textLeft + (overlapStart - visL));
                blitW = (WORD)(overlapEnd - overlapStart + 1);

                if (!line->chunks || c >= line->chunkAlloc || !line->chunks[c])
                    uted_line_render_chunk(inst, line, c);

                if (line->chunks && c < line->chunkAlloc && line->chunks[c]) {
                    BltBitMapRastPort(line->chunks[c],
                                      (LONG)srcX, 0,
                                      rp,
                                      (LONG)dstX, (LONG)absY,
                                      (LONG)blitW, (LONG)inst->lineHeightBase,
                                      0xC0);
                } else {
                    SetAPen(rp, (LONG)inst->bgPen);
                    RectFill(rp, (LONG)dstX, (LONG)absY,
                             (LONG)(dstX + blitW - 1),
                             (LONG)(absY + inst->lineHeightBase - 1));
                }
            }

            /* Fill lineSpacing gap below text area */
            if (inst->lineSpacing > 0) {
                SetAPen(rp, (LONG)inst->bgPen);
                RectFill(rp, (LONG)left, (LONG)(absY + inst->lineHeightBase),
                         (LONG)(left + width - 1),
                         (LONG)(absY + inst->lineHeight - 1));
            }

            UTED_DRAW_TAB_MARKERS(line, 0, line->charCount, inst->scrollLeftPx);

            /* Selection overlay (skipped in read-only mode) */
            if (!inst->readOnly && inst->hasSelection && selStart && selEnd) {
                ULONG lineSelStart = 0, lineSelEnd = 0;
                hasSelStart = (lineIdx >= selStart->line);
                hasSelEnd   = (lineIdx <= selEnd->line);

                if (hasSelStart && hasSelEnd) {
                    lineSelStart = (lineIdx == selStart->line) ? selStart->ch : 0;
                    lineSelEnd   = (lineIdx == selEnd->line)   ? selEnd->ch
                                                               : line->charCount;

                    if (lineSelStart < lineSelEnd) {
                        WORD selX1, selX2;

                        if (!line->charXOffsets)
                            uted_line_build_metrics(line, inst);

                        if (line->charXOffsets) {
                            if (lineSelStart > line->charCount)
                                lineSelStart = line->charCount;
                            if (lineSelEnd > line->charCount)
                                lineSelEnd = line->charCount;
                            selX1 = (WORD)(textLeft
                                + (LONG)line->charXOffsets[lineSelStart]
                                - (LONG)inst->scrollLeftPx);
                            selX2 = (WORD)(textLeft
                                + (LONG)line->charXOffsets[lineSelEnd]
                                - (LONG)inst->scrollLeftPx - 1);
                        } else {
                            selX1 = textLeft;
                            selX2 = (WORD)(textLeft + textWidth - 1);
                        }

                        if (selX1 < textLeft) selX1 = textLeft;
                        if (selX2 >= textLeft + textWidth) selX2 = (WORD)(textLeft + textWidth - 1);

                        if (selX1 <= selX2) {
                            if (!inst->isCLUT) {
                                /* RTG/RGBA: COMPLEMENT inverts all channels cleanly */
                                SetAPen(rp, 0xFF);
                                SetDrMd(rp, COMPLEMENT);
                                RectFill(rp, (LONG)selX1, (LONG)absY,
                                         (LONG)selX2,
                                         (LONG)(absY + inst->lineHeightBase - 1));
                                SetDrMd(rp, JAM2);
                            } else {
                                /* CLUT: COMPLEMENT on a palette index gives garbage.
                                 * Instead: fill selection bg with txtPen, then
                                 * re-render the selected characters directly on the
                                 * screen rastport with swapped fg/bg pens. */
                                SetAPen(rp, (LONG)inst->txtPen);
                                SetBPen(rp, (LONG)inst->txtPen);
                                SetDrMd(rp, JAM2);
                                RectFill(rp, (LONG)selX1, (LONG)absY,
                                         (LONG)selX2,
                                         (LONG)(absY + inst->lineHeightBase - 1));

                                if (inst->screen && line->charXOffsets
                                    && lineSelEnd > lineSelStart)
                                {
                                    ULONG startByte = uted_char_to_byte(
                                        line->utf8, line->byteUsed, lineSelStart);
                                    struct URPTextPos pos;
                                    pos.x = (WORD)(textLeft
                                        + (LONG)line->charXOffsets[lineSelStart]
                                        - (LONG)inst->scrollLeftPx);
                                    pos.y = (WORD)(absY + inst->lineAscent);

                                    URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                        (LONG)inst->bgPen, (LONG)inst->txtPen);
                                    SetAPen(rp, (LONG)inst->bgPen);
                                    SetBPen(rp, (LONG)inst->txtPen);
                                    SetDrMd(rp, JAM2);
                                    URPDrawTextUTF8(rp, inst->dc, &pos,
                                        line->utf8 + startByte,
                                        lineSelEnd - lineSelStart);
                                }
                            }
                        }
                    }
                }
            }

        } else {
            /* Below last line: background */
            if (isFullRefresh || rEnd == ~0UL) {
                SetAPen(rp, (LONG)inst->bgPen);
                RectFill(rp, (LONG)left, (LONG)absY,
                         (LONG)(left + width - 1),
                         (LONG)(absY + inst->lineHeight - 1));
            }
        }
    }

    /* Cursor: COMPLEMENT vertical bar (skipped in read-only mode).
     * 2px wide when this instance is privately activated, 1px when not. */
    if (!inst->readOnly &&
        (isFullRefresh || (inst->cursor.line >= rStart && inst->cursor.line <= rEnd)) &&
        inst->cursor.line >= inst->scrollTopLine &&
        inst->cursor.line < inst->scrollTopLine + (ULONG)inst->visibleLines &&
        inst->cursor.line < inst->lineCount)
    {
        UniTextEditorLine *cline = uted_get_line(inst, inst->cursor.line);
        if (cline) {
            WORD cy = (WORD)(textTop + (LONG)(inst->cursor.line - inst->scrollTopLine)
                              * inst->lineHeight);
            WORD cx = textLeft;
            WORD cxEnd;

            if (!cline->charXOffsets)
                uted_line_build_metrics(cline, inst);

            if (cline->charXOffsets && inst->cursor.ch <= cline->charCount)
                cx = (WORD)(textLeft
                            + (LONG)cline->charXOffsets[inst->cursor.ch]
                            - (LONG)inst->scrollLeftPx);

            /* 2px wide when activated (focused), 1px when not */
            cxEnd = inst->gadgetActive ? (WORD)(cx + 1) : cx;

            if (cx >= textLeft && cx < textLeft + textWidth - 1) {
                if (!inst->isCLUT) {
                    SetAPen(rp, 0xFF);
                    SetDrMd(rp, COMPLEMENT);
                    RectFill(rp,
                             (LONG)cx, (LONG)cy,
                             (LONG)cxEnd, (LONG)(cy + inst->lineHeightBase - 1));
                    SetDrMd(rp, JAM2);
                } else {
                    /* CLUT: solid bar in txtPen; JAM1 so we only set the pen
                     * pixels and don't disturb surrounding background. */
                    SetAPen(rp, (LONG)inst->txtPen);
                    SetDrMd(rp, JAM1);
                    RectFill(rp,
                             (LONG)cx, (LONG)cy,
                             (LONG)cxEnd, (LONG)(cy + inst->lineHeightBase - 1));
                    SetDrMd(rp, JAM2);
                }
            }
        }
    }

    } /* end non-wrap else */

    /* Fill remaining pixels below last rendered line (within text area) */
    if (isFullRefresh || rEnd == ~0UL) {
        WORD usedH = (WORD)(inst->visibleLines * inst->lineHeight);
        WORD textBottom = (WORD)(textTop + textHeight);
        if ((WORD)(textTop + usedH) < textBottom) {
            SetAPen(rp, (LONG)inst->bgPen);
            RectFill(rp,
                     (LONG)left, (LONG)(textTop + usedH),
                     (LONG)(left + width - 1), (LONG)(textBottom - 1));
        }
    }


    /* important to pass back oldClipRegion */
    InstallClipRegion( rp->Layer,oldClipRegion);

    /* Fill margin strips on full refresh, when inner clipping disabled ! */

    /*on some amiga configuration
     * redrawing all is mandatory
     * if (inst->redrawBevel)*/
    {
        if(inst->bevel)
        {
            DrawImage(rp,inst->bevel,0,0);
        }
        inst->redrawBevel = FALSE;

        SetAPen(rp, (LONG)inst->bgPen);

        if ( inst->leftMargin > bevelLeft) {
            RectFill(rp,
                     (LONG)(left + bevelLeft),
                     (LONG)(top  +  inst->topMargin),
                     (LONG)(textLeft - 1),
                     (LONG)(top  + height - inst->bottomMargin - 1));
        }

        if (inst->topMargin > bevelTop)
            RectFill(rp, (LONG)left+bevelLeft, (LONG)top+bevelTop,
                     (LONG)(left + width -bevelLeft - 1),
                     (LONG)(top  + inst->topMargin - 1));

        if (inst->bottomMargin > bevelTop)
            RectFill(rp, (LONG)left+bevelLeft,
                        (LONG)(top + height - inst->bottomMargin),

                     (LONG)(left + width -bevelLeft - 1),
                     (LONG)(top +height - bevelTop - 1));
        if ( inst->rightMargin > bevelLeft ) {
            RectFill(rp,
                     (LONG)(left + width - inst->rightMargin),
                     (LONG)(top  +  inst->topMargin),
                     (LONG)(left + width - bevelLeft - 1),
                     (LONG)(top  + height - inst->bottomMargin - 1));


        }
    }


    /* Internal vertical scroll indicator ----------------------------------------
     * Drawn last (over text, after bevel/margins) so nothing covers it.
     * Only shown when text is taller than the visible area.
     * Geometry: 4px wide, 1px gap from the right border of the gadget.
     * Thumb height and Y position are proportional to the visible fraction
     * and scroll position within the total rendered line count.           */
    if (inst->displayInternalVScroll && textHeight > 0) {
        ULONG totalRows = inst->wordWrap ? inst->wrapRowCount : inst->lineCount;
        if (totalRows > (ULONG)inst->visibleLines) {
            WORD thumbH = (WORD)((LONG)textHeight * (LONG)inst->visibleLines
                                 / (LONG)totalRows);
            WORD thumbY = (WORD)((LONG)textHeight * (LONG)inst->scrollTopLine
                                 / (LONG)totalRows);
            if (thumbH < 3) thumbH = 3;
            if (thumbY < 0) thumbY = 0;
            if (thumbY + thumbH > textHeight) thumbY = (WORD)(textHeight - thumbH);

            SetAPen(rp, (LONG)inst->txtPen);
            SetDrMd(rp, JAM1);
            RectFill(rp,
                (LONG)(left + width - 7), (LONG)(textTop + thumbY),
                (LONG)(left + width - 4), (LONG)(textTop + thumbY + thumbH - 1));
        }
    }

    ReleaseSemaphore(&inst->cacheSem);
    ReleaseSemaphore(&inst->textSem);
    return 0;
}
