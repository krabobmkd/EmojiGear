/*
 * TextEditor gadget – attribute handlers: OM_NEW, OM_DISPOSE, OM_SET, OM_GET.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <devices/inputevent.h>

#include <proto/bevel.h>
#include <images/bevel.h>
#include <intuition/icclass.h>
#include <string.h>
#include <stdio.h>
#include "unitexteditor_private.h"
#include "bdbprintf.h"
#ifdef STATIC_UTF8RASTPORT
    #include "utf8rastport.h"
#else
    #include <libraries/utf8rastport.h>
    #include <proto/utf8rastport.h>
#endif

static void updateLineHeight(UniTextEditorData *inst);
 ULONG UniTextEditor_OnPreDispose(Class *cl, Object *o);

/* GA_Text is a standard tag in newer gadgetclass versions (OS 3.5+).
 * Provide a fallback definition if the SDK does not declare it. */
#ifndef GA_Text
#define GA_Text (GA_Dummy + 0x0039)
#endif

#define G(o)             ((struct Gadget *)(o))


/* =========================================================================
 * Search helpers
 * =========================================================================
 */

/* Find the last occurrence of needle whose START byte offset is < limitByte
 * within the NUL-terminated haystack.  Returns pointer to start of match,
 * or NULL if none found. */
static const char *uted_rfind_before(const char *haystack, ULONG limitByte,
                                      const char *needle)
{
    ULONG nlen = (ULONG)strlen(needle);
    const char *last = NULL;
    const char *p = haystack;
    if (nlen == 0 || limitByte == 0) return NULL;
    while (1) {
        const char *hit = strstr(p, needle);
        if (!hit) break;
        if ((ULONG)(hit - haystack) >= limitByte) break;
        last = hit;
        p    = hit + 1;
    }
    return last;
}

/* Case-insensitive strstr for ASCII letters; non-ASCII bytes match literally. */
static const char *uted_strcasestr(const char *haystack, const char *needle)
{
    ULONG nlen;
    const char *h;
    const char *n;
    unsigned char hc, nc;

    if (!needle || !*needle) return haystack;
    nlen = (ULONG)strlen(needle);

    for (; *haystack; haystack++) {
        if ((ULONG)strlen(haystack) < nlen) return NULL;
        h = haystack;
        n = needle;
        while (*n) {
            hc = (unsigned char)*h;
            nc = (unsigned char)*n;
            if (hc >= 'A' && hc <= 'Z') hc += ('a' - 'A');
            if (nc >= 'A' && nc <= 'Z') nc += ('a' - 'A');
            if (hc != nc) break;
            h++; n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

/* Dispatch: use strstr or uted_strcasestr depending on case mode. */
static const char *uted_strfind(const char *haystack, const char *needle,
                                 BOOL caseSensitive)
{
    return caseSensitive
        ? strstr(haystack, needle)
        : uted_strcasestr(haystack, needle);
}

/* Reverse-find variant that respects case mode. */
static const char *uted_rfind_before_cs(const char *haystack, ULONG limitByte,
                                         const char *needle, BOOL caseSensitive)
{
    ULONG nlen = (ULONG)strlen(needle);
    const char *last = NULL;
    const char *p = haystack;
    if (nlen == 0 || limitByte == 0) return NULL;
    while (1) {
        const char *hit = uted_strfind(p, needle, caseSensitive);
        if (!hit) break;
        if ((ULONG)(hit - haystack) >= limitByte) break;
        last = hit;
        p    = hit + 1;
    }
    return last;
}

/* Shared post-find helper: set cursor + selection and save last-search info. */
static void uted_apply_search_match(UniTextEditorData *inst,
                                     ULONG foundLine, ULONG foundChar,
                                     ULONG patCharLen, BOOL cursorAtStart)
{
    ULONG endChar = foundChar + patCharLen;

    inst->lastSearchLine    = foundLine;
    inst->lastSearchChar    = foundChar;
    inst->lastSearchEndChar = endChar;
    inst->lastSearchValid   = TRUE;

    inst->selAnchor.line = foundLine;
    inst->selFloat.line  = foundLine;

    if (cursorAtStart) {
        /* backward search: cursor at start, anchor at end */
        inst->selAnchor.ch = endChar;
        inst->selFloat.ch  = foundChar;
    } else {
        /* forward search: cursor at end, anchor at start */
        inst->selAnchor.ch = foundChar;
        inst->selFloat.ch  = endChar;
    }

    inst->cursor       = inst->selFloat;
    inst->hasSelection = (patCharLen > 0);

    inst->refreshStartLine = foundLine;
    inst->refreshEndLine   = foundLine;

    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);
}

/* =========================================================================
 * Context stash helpers
 * =========================================================================
 */

/* Free one heap-allocated stash line (chunks already evicted before stashing). */
static void uted_free_stashed_line(UniTextEditorLine *line)
{
    if (line->utf8)         { FreeVec(line->utf8);         }
    if (line->charXOffsets) { FreeVec(line->charXOffsets); }
    if (line->chunks)       { FreeVec(line->chunks);       }
    FreeVec(line);
}

/* Free all heap content owned by a stash node and the node itself. */
static void uted_stash_free(UTEDTextStash *stash)
{
    UniTextEditorLine *line, *next;
    ULONG i;

    /* Free undo/redo text payloads then the backing arrays */
    if (stash->undoStack) {
        for (i = 0; i < stash->undoMax; i++)
            if (stash->undoStack[i].text) FreeVec(stash->undoStack[i].text);
        FreeVec(stash->undoStack);
    }
    if (stash->redoStack) {
        for (i = 0; i < stash->redoCount; i++)
            if (stash->redoStack[i].text) FreeVec(stash->redoStack[i].text);
        FreeVec(stash->redoStack);
    }

    /* Free wrap map */
    if (stash->wrapMap) FreeVec(stash->wrapMap);

    /* Free line chain (sentinel-free: walk until mln_Succ == NULL) */
    line = stash->firstLine;
    while (line) {
        next = (UniTextEditorLine *)line->node.mln_Succ;
        uted_free_stashed_line(line);
        line = next;
    }

    FreeVec(stash);
}

/* Unlink a stash node from the list and free all its content. */
static void uted_stash_remove(UniTextEditorData *inst, UTEDTextStash *stash)
{
    struct MinNode *pred = stash->node.mln_Pred;
    struct MinNode *succ = stash->node.mln_Succ;
    (void)inst;
    pred->mln_Succ = succ;
    succ->mln_Pred = pred;
    uted_stash_free(stash);
}

/* Find a stash by context name; returns NULL if not found. */
static UTEDTextStash *uted_stash_find(UniTextEditorData *inst, const char *name)
{
    UTEDTextStash *s = (UTEDTextStash *)inst->stashList.mlh_Head;
    while (s->node.mln_Succ) {
        if (strcmp(s->contextName, name) == 0) return s;
        s = (UTEDTextStash *)s->node.mln_Succ;
    }
    return NULL;
}

/* Save current document state into the stash list under currentContextName.
 * Skipped when currentContextName is empty (initial unnamed state).
 * All bitmap chunks are evicted before saving so the pool stays with inst. */
static void uted_stash_save(UniTextEditorData *inst)
{
    UTEDTextStash      *stash;
    UniTextEditorLine  *first, *last;

    if (inst->currentContextName[0] == '\0') return;

    /* Evict all borrowed bitmaps back to the pool */
    {
        UniTextEditorLine *line = (UniTextEditorLine *)inst->lines.mlh_Head;
        while (line->node.mln_Succ) {
            uted_line_evict_chunks(inst, line);
            line = (UniTextEditorLine *)line->node.mln_Succ;
        }
    }

    /* Reuse existing stash entry for this name, or allocate a new one */
    stash = uted_stash_find(inst, inst->currentContextName);
    if (stash) {
        /* Free old content; keep the node in the list */
        UniTextEditorLine *line, *next;
        ULONG i;
        if (stash->undoStack) {
            for (i = 0; i < stash->undoMax; i++)
                if (stash->undoStack[i].text) FreeVec(stash->undoStack[i].text);
            FreeVec(stash->undoStack);
        }
        if (stash->redoStack) {
            for (i = 0; i < stash->redoCount; i++)
                if (stash->redoStack[i].text) FreeVec(stash->redoStack[i].text);
            FreeVec(stash->redoStack);
        }
        if (stash->wrapMap) FreeVec(stash->wrapMap);
        line = stash->firstLine;
        while (line) {
            next = (UniTextEditorLine *)line->node.mln_Succ;
            uted_free_stashed_line(line);
            line = next;
        }
    } else {
        stash = (UTEDTextStash *)AllocVec(sizeof(UTEDTextStash), MEMF_ANY | MEMF_CLEAR);
        if (!stash) return;
        strncpy(stash->contextName, inst->currentContextName,
                UTED_CONTEXT_NAME_MAX - 1);
        /* Append to stash list */
        {
            struct MinNode *tail = inst->stashList.mlh_TailPred;
            stash->node.mln_Succ  = (struct MinNode *)&inst->stashList.mlh_Tail;
            stash->node.mln_Pred  = tail;
            tail->mln_Succ        = (struct MinNode *)stash;
            inst->stashList.mlh_TailPred = (struct MinNode *)stash;
        }
    }

    /* Extract the line chain from inst's MinList.
     * Break the sentinel links; the chain becomes a NULL-terminated sequence. */
    first = (UniTextEditorLine *)inst->lines.mlh_Head;
    last  = (UniTextEditorLine *)inst->lines.mlh_TailPred;
    first->node.mln_Pred = NULL;
    last->node.mln_Succ  = NULL;

    stash->firstLine    = first;
    stash->lastLine     = last;
    stash->lineCount    = inst->lineCount;

    /* Move undo/redo stacks wholesale */
    stash->undoStack  = inst->undoStack;
    stash->redoStack  = inst->redoStack;
    stash->undoMax    = inst->undoMax;
    stash->undoCount  = inst->undoCount;
    stash->undoHead   = inst->undoHead;
    stash->redoCount  = inst->redoCount;

    /* Move wrap map */
    stash->wrapMap      = inst->wrapMap;
    stash->wrapRowCount = inst->wrapRowCount;
    stash->wrapMapAlloc = inst->wrapMapAlloc;
    stash->wrapMapDirty = inst->wrapMapDirty;

    /* Copy cursor/scroll/state */
    stash->cursor       = inst->cursor;
    stash->selAnchor    = inst->selAnchor;
    stash->selFloat     = inst->selFloat;
    stash->hasSelection = inst->hasSelection;
    stash->scrollTopLine = inst->scrollTopLine;
    stash->scrollLeftPx  = inst->scrollLeftPx;
    stash->modified      = inst->modified;
    stash->wordWrap      = inst->wordWrap;

    /* Detach from inst so nothing gets double-freed */
    inst->undoStack  = NULL;
    inst->redoStack  = NULL;
    inst->undoMax    = 0;
    inst->undoCount  = 0;
    inst->undoHead   = 0;
    inst->redoCount  = 0;
    inst->wrapMap      = NULL;
    inst->wrapRowCount = 0;
    inst->wrapMapAlloc = 0;
}

/* Restore a stash into inst and remove the node from the stash list.
 * The stash node itself is freed; its content is now live in inst. */
static void uted_stash_restore(UniTextEditorData *inst, UTEDTextStash *stash)
{
    /* Relink the MinList from the sentinel-free chain */
    inst->lines.mlh_Head        = (struct MinNode *)stash->firstLine;
    inst->lines.mlh_Tail        = NULL;
    inst->lines.mlh_TailPred    = (struct MinNode *)stash->lastLine;
    stash->firstLine->node.mln_Pred = (struct MinNode *)&inst->lines.mlh_Head;
    stash->lastLine->node.mln_Succ  = (struct MinNode *)&inst->lines.mlh_Tail;
    inst->lineCount = stash->lineCount;

    /* Move undo/redo stacks back */
    inst->undoStack  = stash->undoStack;
    inst->redoStack  = stash->redoStack;
    inst->undoMax    = stash->undoMax;
    inst->undoCount  = stash->undoCount;
    inst->undoHead   = stash->undoHead;
    inst->redoCount  = stash->redoCount;

    /* Move wrap map back */
    inst->wrapMap      = stash->wrapMap;
    inst->wrapRowCount = stash->wrapRowCount;
    inst->wrapMapAlloc = stash->wrapMapAlloc;
    inst->wrapMapDirty = stash->wrapMapDirty;

    /* Restore cursor/scroll/state */
    inst->cursor        = stash->cursor;
    inst->selAnchor     = stash->selAnchor;
    inst->selFloat      = stash->selFloat;
    inst->hasSelection  = stash->hasSelection;
    inst->scrollTopLine = stash->scrollTopLine;
    inst->scrollLeftPx  = stash->scrollLeftPx;
    inst->modified      = stash->modified;
    inst->wordWrap      = stash->wordWrap;

    /* Force a full cache invalidation: frees charXOffsets so metrics are
     * rebuilt cleanly on next render, and marks every line dirty.
     * This is equivalent to what uted_invalidate_all_line_caches does and
     * avoids the stale-metrics rendering glitch that occurs when dirty=TRUE
     * lines have all chunks NULL but the render's dirty loop sets dirty=FALSE
     * without actually rendering anything. */
    uted_invalidate_all_line_caches(inst);
    inst->refreshStartLine  = 0;
    inst->refreshEndLine    = (LONG)~0UL;
    inst->lastSearchValid   = FALSE;

    /* Unlink and free the stash node (content is now live, do not free it) */
    {
        struct MinNode *pred = stash->node.mln_Pred;
        struct MinNode *succ = stash->node.mln_Succ;
        pred->mln_Succ = succ;
        succ->mln_Pred = pred;
    }
    FreeVec(stash);
}

/* Switch to a named context: stash current doc, restore or create new doc. */
static void uted_apply_context_switch(UniTextEditorData *inst, const char *newName)
{
    UTEDTextStash *existing;
    UniTextEditorLine *emptyLine;

    if (!newName) newName = "";

    /* No-op if already on this context */
    if (strcmp(inst->currentContextName, newName) == 0) return;

    /* Save current document */
    uted_stash_save(inst);

    /* Look for an existing stash for the new context */
    existing = uted_stash_find(inst, newName);

    if (existing) {
        /* Restore previously saved document */
        uted_stash_restore(inst, existing);
    } else {
        /* New context: initialise an empty document */
        inst->lines.mlh_Head     = (struct MinNode *)&inst->lines.mlh_Tail;
        inst->lines.mlh_Tail     = NULL;
        inst->lines.mlh_TailPred = (struct MinNode *)&inst->lines.mlh_Head;
        inst->lineCount          = 0;

        emptyLine = uted_line_alloc(NULL, 0);
        if (emptyLine) {
            inst->lines.mlh_Head     = (struct MinNode *)emptyLine;
            emptyLine->node.mln_Succ = (struct MinNode *)&inst->lines.mlh_Tail;
            emptyLine->node.mln_Pred = (struct MinNode *)&inst->lines.mlh_Head;
            inst->lines.mlh_TailPred = (struct MinNode *)emptyLine;
            inst->lineCount          = 1;
        }

        /* Fresh undo/redo for the new context */
        uted_undo_resize(inst, inst->undoMax > 0 ? inst->undoMax : 64);

        inst->wrapMap      = NULL;
        inst->wrapRowCount = 0;
        inst->wrapMapAlloc = 0;
        inst->wrapMapDirty = inst->wordWrap;

        inst->cursor        = inst->selAnchor = inst->selFloat =
            (UniTextEditorPos){0, 0};
        inst->hasSelection  = FALSE;
        inst->scrollTopLine = 0;
        inst->scrollLeftPx  = 0;
        inst->modified      = FALSE;
        inst->lastSearchValid = FALSE;
        inst->refreshStartLine = 0;
        inst->refreshEndLine   = (LONG)~0UL;
    }

    strncpy(inst->currentContextName, newName, UTED_CONTEXT_NAME_MAX - 1);
    inst->currentContextName[UTED_CONTEXT_NAME_MAX - 1] = '\0';
}

/* =========================================================================
 * OM_NEW
 * =========================================================================
 */
ULONG UniTextEditor_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    UniTextEditorData *inst;
    Object            *newObj;
    ULONG              mDispose;
    ULONG urpflags = 0;
    UniTextEditorLine *firstLine;

    struct TagItem *ptag;
    ULONG v;

    /* Let gadgetclass handle GA_* tags first */
    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = UTED_DATA(cl, newObj);

    /* Zero-initialise */
    memset(inst, 0, sizeof(UniTextEditorData));

    InitSemaphore(&inst->textSem);
    InitSemaphore(&inst->cacheSem);

    /*krb note, commented the 0 and false values done by memset, to optimize */
    /* Defaults */
    inst->pointSize  = 12;
//    inst->fontFlags  = 0;
    inst->txtPen     = 1;
    inst->bgPen      = 0;
//    inst->modified        = FALSE;
    inst->tabSpaces       = 4;
    // inst->tabsAreSpaces    = FALSE;
    // inst->applyAnsiEscapes = FALSE;
    inst->ansiUnixColors   = TRUE; /* since r2.2, default to true and no more menu entry no one understand */
    inst->vanillaAnsiCode  = UTED_VANILLAKEY_LATIN1;
    inst->maxDisplayLines  = ~0UL;
//    inst->noLineFeed       = FALSE;
    inst->leftMargin       = 2;

    inst->useInternalRawKey = UKM_Internal;
    // inst->lineSpacing      = 0;
    // inst->rightMargin      = 0;
    // inst->topMargin        = 0;
    // inst->bottomMargin     = 0;

//    inst->refreshStartLine = 0;
    inst->refreshEndLine = ~0;

    inst->searchCaseSensitive    = TRUE;
    inst->displayInternalVScroll = TRUE;
//    inst->visibleTabs  = FALSE;
    inst->halfwayPen   = 0;

//    inst->bevel = NULL;
    inst->callerTask = FindTask(NULL);

    /* URPDrawContext font configuration may be external and shared */
    {
        struct URPDrawContext *externalDc=NULL;
         ptag = FindTagItem(UTED_URPDrawContext, msg->ops_AttrList);
        if(ptag) externalDc = (struct URPDrawContext *)ptag->ti_Data;
        if(externalDc)
        {
            URPDC_Retain(externalDc);
            inst->dc = externalDc;

        } else
        {
            inst->dc = URPDC_Create(NULL);

            /* default option for text rendering */
            urpflags = URP_PREF_ANTIALIAS |
                URP_PREF_CLUTMODE_NOMASK |
                URP_PREF_HIGHFILTERING;
            /*... that can be changed by this optional  attrib */
            ptag = FindTagItem(UTED_URPPrefs, msg->ops_AttrList);
            if(ptag) urpflags = ptag->ti_Data;

            URPDC_SetPreferenceFlags(inst->dc,urpflags);

        }
    }

    if (!inst->dc) goto fail;
           /*  */
            updateLineHeight(inst);
    /* may manage bevel box from configuration */
    v = BVS_NONE;
    ptag = FindTagItem(UTED_BevelStyle, msg->ops_AttrList);
    if(ptag) v = ptag->ti_Data;
    if(v != BVS_NONE && BevelBase)
    {
        inst->bevel = NewObject(BEVEL_GetClass(),NULL,
                            BEVEL_Style,v,
                            BEVEL_Transparent,TRUE,
                            TAG_END
                        );
        if(inst->bevel)
        {
            ULONG w,h;
            GetAttr(BEVEL_VertSize,inst->bevel,&w);
            GetAttr(BEVEL_HorizSize,inst->bevel,&h);
            inst->leftMargin += w;
            inst->rightMargin += w;
            inst->topMargin += h;
            inst->bottomMargin += h;
        }
    }



    inst->clipRegion = NewRegion();




    /* Undo/redo stacks – default 64 entries */
    uted_undo_resize(inst, 64);

    /* Init stash list and context name */
    inst->stashList.mlh_Head     = (struct MinNode *)&inst->stashList.mlh_Tail;
    inst->stashList.mlh_Tail     = NULL;
    inst->stashList.mlh_TailPred = (struct MinNode *)&inst->stashList.mlh_Head;
    inst->currentContextName[0]  = '\0';

    /* Init text buffer: one empty line */
    inst->lines.mlh_Head     = (struct MinNode *)&inst->lines.mlh_Tail;
    inst->lines.mlh_Tail     = NULL;
    inst->lines.mlh_TailPred = (struct MinNode *)&inst->lines.mlh_Head;

    firstLine = uted_line_alloc(NULL, 0);
    if (!firstLine) goto fail;

    {
        struct MinNode *tailPred = inst->lines.mlh_TailPred;
        firstLine->node.mln_Succ = (struct MinNode *)&inst->lines.mlh_Tail;
        firstLine->node.mln_Pred = tailPred;
        tailPred->mln_Succ       = (struct MinNode *)firstLine;
        inst->lines.mlh_TailPred = (struct MinNode *)firstLine;
    }
    inst->lineCount = 1;

    /* Apply remaining tags (UTED_PointSize, pens, text, etc.) */
    UniTextEditor_OnSet(cl, newObj, msg);

    return (ULONG)newObj;

fail:
    mDispose = OM_DISPOSE;
    DoSuperMethodA(cl, newObj, (APTR)&mDispose);
    return 0;
}

/* =========================================================================
 * OM_DISPOSE
 * =========================================================================
 */
 ULONG UniTextEditor_OnPreDispose(Class *cl, Object *o)
 {
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UniTextEditorLine *line;
    UniTextEditorLine *next;

    /* Free all stashed contexts */
    {
        UTEDTextStash *stash = (UTEDTextStash *)inst->stashList.mlh_Head;
        UTEDTextStash *snext;
        while (stash->node.mln_Succ) {
            snext = (UTEDTextStash *)stash->node.mln_Succ;
            uted_stash_free(stash);
            stash = snext;
        }
    }

    /* Release undo/redo stacks and all captured text */
    ObtainSemaphore(&inst->textSem);
    ObtainSemaphore(&inst->cacheSem);
    uted_undo_resize(inst, 0);

    /* Release word-wrap map */
    uted_free_wrap_map(inst);

    /* Free all lines (returns borrowed bitmaps to pool) */
    line = (UniTextEditorLine *)inst->lines.mlh_Head;
    while (line->node.mln_Succ) {
        next = (UniTextEditorLine *)line->node.mln_Succ;
        uted_line_free(inst, line);
        line = next;
    }

    /* All bitmaps returned; now release the pool itself */
    uted_pool_free_bitmapcache(&inst->bmPool);
    /* the part that has poblems on OS3.9 */
    uted_pool_free_layer(&inst->bmPool);
    ReleaseSemaphore(&inst->cacheSem);
    ReleaseSemaphore(&inst->textSem);
    if (inst->dc) { URPDC_Release(inst->dc); inst->dc = NULL; }

    if (inst->clipRegion) {
        DisposeRegion( inst->clipRegion );
        inst->clipRegion = NULL;
    }
    if(inst->bevel)
    {
        DisposeObject(inst->bevel);
        inst->bevel = NULL;
    }

 }

ULONG UniTextEditor_OnDispose(Class *cl, Object *o, Msg msg)
{
    UniTextEditor_OnPreDispose(cl,o);
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* Recompute halfwayPen: pen in the screen colormap nearest to the midpoint
 * between txtPen and bgPen.  Falls back to bgPen when no screen is set yet. */
void uted_update_halfway_pen(UniTextEditorData *inst)
{
    ULONG rgb[8];
    //ULONG ncol;
    //int depth;

    if (!inst->screen) { inst->halfwayPen = inst->bgPen; return; }
    GetRGB32(&inst->screen->ViewPort.ColorMap, inst->txtPen, 1, &rgb[0]);
    GetRGB32(&inst->screen->ViewPort.ColorMap, inst->bgPen,  1, &rgb[3]);

    // depth = GetBitMapAttr( inst->screen->RastPort.BitMap,BMA_DEPTH);
    // ncol = 1L<<depth;
    // if(ncol>256) ncol=256;
    inst->halfwayPen = (ULONG)FindColor(&inst->screen->ViewPort.ColorMap,
        ((rgb[0]>>1) + (rgb[3]>>1)),
        ((rgb[1]>>1) + (rgb[4]>>1)),
        ((rgb[2]>>1) + (rgb[5]>>1)),
        -1
        );
}

/* to be used specifically if font height change */
void updateLineHeight(UniTextEditorData *inst)
{
    struct URPTextMetric metric;

    /* Query max ascender+descender across ALL loaded fonts so that emoji
     * glyphs from a secondary font (which have a larger ascender than the
     * primary text font) are fully contained in the line buffer.
     * Fall back to measuring an ASCII sample string when no fonts are loaded. */
    URPDC_GetFontLineMetrics(inst->dc, &metric);
    if (metric.height <= 0)
        URPDC_TextSizeUTF8(inst->dc, "Agpqj", -1, &metric);

    inst->lineHeightBase = metric.height > 0
                           ? (WORD)metric.height
                           : (WORD)inst->pointSize;
    inst->lineHeight = inst->lineHeightBase + inst->lineSpacing;
    inst->lineAscent = metric.baseY  > 0
                       ? (WORD)metric.baseY
                       : inst->lineHeightBase;
    /*force a relayout */
    inst->layoutedHeight = 0;
}

/* =========================================================================
 * OM_SET / OM_UPDATE
 * =========================================================================
 */
ULONG UniTextEditor_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    UniTextEditorData *inst   = UTED_DATA(cl, o);
    struct TagItem *state  = msg->ops_AttrList;
    struct TagItem *tag;
    ULONG           result = 0;
    BOOL            redraw = FALSE;
    BOOL            changepens = FALSE;
    ULONG           scrollTopBefore  = inst->scrollTopLine;
    ULONG           scrollLeftBefore = inst->scrollLeftPx;

    ObtainSemaphore(&inst->textSem);

    /* Process UTED_CurrentContext first so that any UTED_Text tag that
     * follows in the same call operates on the newly activated context. */
    {
        struct TagItem *ctxTag = FindTagItem(UTED_CurrentContext, msg->ops_AttrList);
        if (ctxTag)
            uted_apply_context_switch(inst, (const char *)ctxTag->ti_Data);
    }

    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag)
        {
            case UTED_URPDrawContext:
            if(inst->dc != (struct URPDrawContext *)tag->ti_Data)
            {
                struct URPDrawContext *externalDc=(struct URPDrawContext *)tag->ti_Data;
                /* release previous if there is*/
                if(inst->dc)
                {
                    URPDC_Release(inst->dc);
                }
                if(externalDc)
                {
                    URPDC_Retain(externalDc);
                }
                inst->dc = externalDc;
                /* implies heavy flush */
                updateLineHeight(inst);
                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UTED_PointSize:
            if(inst->pointSize != (ULONG)tag->ti_Data)
            {
                inst->pointSize = (ULONG)tag->ti_Data;
                URPDC_ChangeFontsSize(inst->dc,inst->pointSize,~0);
                updateLineHeight(inst);
                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UTED_FontFlags:
            inst->fontFlags = (ULONG)tag->ti_Data;
            result = 1;
            break;

        case UTED_FlushFonts:
            if (inst->dc) {
                URPDC_FlushFonts(inst->dc);
                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;

        case UTED_AddFont:
        {
            const char *p = (const char *)tag->ti_Data;
            if (p && p[0] && inst->dc) {

                URPDC_AddFont(inst->dc, p,
                              (int)inst->pointSize, inst->fontFlags);
                /* Rebuild line height from updated font metrics */
                updateLineHeight(inst);

                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_URPPrefs:
            if(inst->dc)
            {
                ULONG dcflags = URPDC_GetPreferenceFlags(inst->dc);
                if(dcflags != (ULONG)tag->ti_Data)
                {
                    URPDC_SetPreferenceFlags(inst->dc,tag->ti_Data);
                    uted_invalidate_all_line_caches(inst);
                    redraw = TRUE;
                }
            }
            result = 1;
            break;

        case UTED_TextPen:
            if(inst->txtPen != (ULONG)tag->ti_Data)
            {
                inst->txtPen = (ULONG)tag->ti_Data;
                changepens = TRUE;
                redraw = TRUE;
            }
            redraw = TRUE;
            result = 1;
            break;

        case UTED_BgPen:
            if(inst->bgPen != (ULONG)tag->ti_Data)
            {
                inst->bgPen = (ULONG)tag->ti_Data;
                inst->redrawBevel = TRUE;
                changepens = TRUE;
                redraw = TRUE;
            }
            result = 1;
            break;

        case GA_ReadOnly:
            inst->readOnly = (BOOL)tag->ti_Data;
            result = 1;
            break;

        case GA_Text:  /* alias: same behaviour as UTED_Text */
        case UTED_Text:
        {
            /* Replace entire buffer */
            const char     *text = (const char *)tag->ti_Data;
            UniTextEditorLine *line, *next;
            UniTextEditorLine *newLine;
            const char     *p, *nl;

           // if(inst->callerTask != FindTask(NULL) ) break;

            /* Free existing lines (returns borrowed bitmaps to pool) */
            line = (UniTextEditorLine *)inst->lines.mlh_Head;
            while (line->node.mln_Succ) {
                next = (UniTextEditorLine *)line->node.mln_Succ;
                uted_line_free(inst, line);
                line = next;
            }
            inst->lines.mlh_Head     = (struct MinNode *)&inst->lines.mlh_Tail;
            inst->lines.mlh_Tail     = NULL;
            inst->lines.mlh_TailPred = (struct MinNode *)&inst->lines.mlh_Head;
            inst->lineCount          = 0;

            /* Split text on '\n' and build lines */
            p = text ? text : "";
            do {
                nl = p;
                while (*nl && *nl != '\n') nl++;
                newLine = uted_line_alloc(p, (ULONG)(nl - p));
                if (newLine) {
                    struct MinNode *tailPred = inst->lines.mlh_TailPred;
                    newLine->node.mln_Succ   = (struct MinNode *)&inst->lines.mlh_Tail;
                    newLine->node.mln_Pred   = tailPred;
                    tailPred->mln_Succ       = (struct MinNode *)newLine;
                    inst->lines.mlh_TailPred = (struct MinNode *)newLine;
                    inst->lineCount++;
                }
                p = (*nl == '\n') ? nl + 1 : nl;
            } while (*nl);

            /* Ensure at least one empty line */
            if (inst->lineCount == 0) {
                newLine = uted_line_alloc(NULL, 0);
                if (newLine) {
                    inst->lines.mlh_Head     = (struct MinNode *)newLine;
                    newLine->node.mln_Succ   = (struct MinNode *)&inst->lines.mlh_Tail;
                    newLine->node.mln_Pred   = (struct MinNode *)&inst->lines.mlh_Head;
                    inst->lines.mlh_TailPred = (struct MinNode *)newLine;
                    inst->lineCount = 1;
                }
            }

            inst->cursor.line  = 0;
            inst->cursor.ch    = 0;
            inst->selAnchor    = inst->cursor;
            inst->selFloat     = inst->cursor;
            inst->hasSelection = FALSE;
            inst->scrollTopLine = 0;
            inst->modified     = FALSE;
            if (inst->wordWrap) inst->wrapMapDirty = TRUE;
            /* Loading new text invalidates any prior undo/redo history and search state */
            uted_undo_flush(inst);
            inst->lastSearchValid = FALSE;

            redraw = TRUE;
            result = 1;
            break;
        }

        case UTED_Modified:
            inst->modified = (BOOL)tag->ti_Data;
            result = 1;
            break;

        case UTED_ScrollTop:
        {
            ULONG top = (ULONG)tag->ti_Data;
            ULONG maxTop;
            if (inst->wordWrap && inst->wrapMap && inst->wrapRowCount > 0)
                maxTop = inst->wrapRowCount - 1;
            else
                maxTop = inst->lineCount > 0 ? inst->lineCount - 1 : 0;
            if (top > maxTop) top = maxTop;
            if (inst->scrollTopLine != top)
            {
                inst->scrollTopLine    = top;
                inst->refreshStartLine = 0;
                inst->refreshEndLine   = ~0L;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_ScrollLeft:
        {
            ULONG left = (ULONG)tag->ti_Data;
            if(inst->scrollLeftPx != left)
            {
                inst->scrollLeftPx     = left;
                inst->refreshStartLine = 0;
                inst->refreshEndLine   = ~0L;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_CursorLine:
            UniTextEditor_DoSetCursorPos(cl, o, (ULONG)tag->ti_Data, inst->cursor.ch, FALSE);
            redraw = TRUE;
            result = 1;
            break;

        case UTED_CursorChar:
            UniTextEditor_DoSetCursorPos(cl, o, inst->cursor.line, (ULONG)tag->ti_Data, FALSE);
            redraw = TRUE;
            result = 1;
            break;

        case UTED_TabSpaces:
        {
            ULONG v = (ULONG)tag->ti_Data;
            if (v < 1)  v = 1;
            if (v > 12) v = 12;
            if (v != inst->tabSpaces) {
                inst->tabSpaces = v;
                if (inst->dc) {
                    ULONG _ta[] = {URPDCA_TabSpaces, v, TAG_DONE};
                    URPDC_SetAttribsA(inst->dc, _ta);
                }
                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_MaxNumberOfUndo:
        {
            ULONG v = (ULONG)tag->ti_Data;
            uted_undo_resize(inst, v);
            result = 1;
            break;
        }

        /* ---- Action tags ---- */

        case UTED_InsertText:
            if (!inst->readOnly) {
                UniTextEditor_DoInsertText(cl, o, (const char *)tag->ti_Data, -1);
                redraw = TRUE; result = 1;
            }
            break;

        case UTED_DeleteChar:
            if (!inst->readOnly) {
                UniTextEditor_DoDeleteChar(cl, o, (LONG)tag->ti_Data);
                redraw = TRUE; result = 1;
            }
            break;

        case UTED_MoveCursorX:
        {
            BOOL extend = (BOOL)GetTagData(UTED_CursorExtend, FALSE, msg->ops_AttrList);
            UniTextEditor_DoMoveCursor(cl, o, (LONG)tag->ti_Data, 0L, extend);
            redraw = TRUE; result = 1;
            break;
        }

        case UTED_MoveCursorY:
        {
            BOOL extend = (BOOL)GetTagData(UTED_CursorExtend, FALSE, msg->ops_AttrList);
            UniTextEditor_DoMoveCursor(cl, o, 0L, (LONG)tag->ti_Data, extend);
            redraw = TRUE; result = 1;
            break;
        }

        case UTED_CursorExtend:
            /* companion tag consumed by MoveCursorX/Y via GetTagData */
            break;

        case UTED_ClearSelection:
            UniTextEditor_DoClearSelection(cl, o);
            redraw = TRUE; result = 1;
            break;

        case UTED_SelectAll:
            UniTextEditor_DoSelectAll(cl, o);
            redraw = TRUE; result = 1;
            break;

        case UTED_DeleteSelection:
            if (!inst->readOnly) {
                UniTextEditor_DoDeleteSelection(cl, o);
                redraw = TRUE; result = 1;
            }
            break;

        case UTED_ScrollTo:
        {
            BOOL center = (BOOL)GetTagData(UTED_ScrollToCenter, FALSE, msg->ops_AttrList);
            UniTextEditor_DoScrollTo(cl, o, (ULONG)tag->ti_Data, center);
            redraw = TRUE; result = 1;
            break;
        }

        case UTED_ScrollToCenter:
            /* companion tag consumed by ScrollTo via GetTagData */
            break;

        case UTED_InvalidateLine:
            UniTextEditor_DoInvalidateLine(cl, o, (ULONG)tag->ti_Data);
            redraw = TRUE; result = 1;
            break;

        case UTED_Undo:
            if (!inst->readOnly) {
                UniTextEditor_DoUndo(cl, o);
                uted_undo_notify(cl, o, msg->ops_GInfo);
                redraw = TRUE; result = 1;
            }
            break;

        case UTED_Redo:
            if (!inst->readOnly) {
                UniTextEditor_DoRedo(cl, o);
                uted_undo_notify(cl, o, msg->ops_GInfo);
                redraw = TRUE; result = 1;
            }
            break;


        case UTED_VanillaKeyAnsiCode:
            inst->vanillaAnsiCode = (ULONG)tag->ti_Data;
            result = 1;
            break;

        case UTED_WordWrap:
        {
            BOOL newWrap = tag->ti_Data ? TRUE : FALSE;
            if (newWrap != inst->wordWrap) {
                inst->wordWrap = newWrap;
                if (newWrap) {
                    inst->scrollLeftPx = 0;   /* no horizontal scroll in wrap mode */
                    inst->wrapMapDirty = TRUE;
                } else {
                    uted_free_wrap_map(inst);
                }
                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_MaxDisplayLines:
            inst->maxDisplayLines = (ULONG)tag->ti_Data;

            redraw = TRUE;
            result = 1;
            break;

        case UTED_NoLineFeed:
            inst->noLineFeed = tag->ti_Data ? TRUE : FALSE;
            result = 1;
            break;

        case UTED_SetPrivateActivation:
        {
            ULONG b = (tag->ti_Data) ? TRUE : FALSE;
            if(inst->gadgetActive != b)
            {
                inst->gadgetActive = b;
                redraw = TRUE;
            }
            result = 1;
            }
            break;

        case UTED_LeftMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if(inst->bevel)
            {
                ULONG w;
                GetAttr(BEVEL_VertSize,inst->bevel,&w);
                v += w;
            }

            if (v < 0) v = 0;
            if (v != inst->leftMargin) {
                inst->leftMargin = v;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_LineSpacing:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if (v < 0) v = 0;
            if (v != inst->lineSpacing) {
                inst->lineSpacing = v;
                if (inst->lineHeightBase > 0)
                    inst->lineHeight = inst->lineHeightBase + inst->lineSpacing;
                uted_invalidate_all_line_caches(inst);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_RightMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if(inst->bevel)
            {
                ULONG w;
                GetAttr(BEVEL_VertSize,inst->bevel,&w);
                v += w;
            }
            if (v < 0) v = 0;
            if (v != inst->rightMargin) {
                inst->rightMargin = v;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_TopMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if(inst->bevel)
            {
                ULONG h;
                GetAttr(BEVEL_HorizSize,inst->bevel,&h);
                v += h;
            }
            if (v < 0) v = 0;
            if (v != inst->topMargin) {
                inst->topMargin = v;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_BottomMargin:
        {
            WORD v = (WORD)(ULONG)tag->ti_Data;
            if(inst->bevel)
            {
                ULONG h;
                GetAttr(BEVEL_HorizSize,inst->bevel,&h);
                v += h;
            }
            if (v < 0) v = 0;
            if (v != inst->bottomMargin) {
                inst->bottomMargin = v;
                redraw = TRUE;
            }
            result = 1;
            break;
        }
        case UTED_KeyMessageMode:
            inst->useInternalRawKey = (ULONG)tag->ti_Data;
            break;
        case UTED_InternalRawKey_SendBack:
            inst->rawKeySendBack = tag->ti_Data ? TRUE : FALSE;
            break;
        case UTED_PutRawKey:
        {
            if(!inst->useInternalRawKey)
            {
                result = redraw = uted_manage_rawkey_keycode(cl,o,(ULONG)tag->ti_Data,msg->ops_GInfo);
            }
        } break;
        case UTED_PutVanillaKey:
        {
            if(!inst->useInternalRawKey)
            {
                redraw = result = uted_manage_vanilla_keycode(cl,o,tag->ti_Data, msg->ops_GInfo);
            }
            break;
        }
        case UTED_SearchNext:
        {
            const char *pattern = (const char *)tag->ti_Data;
            if (pattern && *pattern) {
                ULONG patByteLen = (ULONG)strlen(pattern);
                ULONG patCharLen = uted_count_chars(pattern, patByteLen);
                BOOL  found = FALSE;
                ULONG foundLine = 0, foundChar = 0;
                UniTextEditorLine *scanLine;
                ULONG lineIdx;

                /* Search from cursor position forward on current line */
                scanLine = uted_get_line(inst, inst->cursor.line);
                if (scanLine) {
                    ULONG startByte = uted_char_to_byte(scanLine->utf8,
                                          scanLine->byteUsed, inst->cursor.ch);
                    const char *hit = uted_strfind(scanLine->utf8 + startByte,
                                          pattern, inst->searchCaseSensitive);
                    if (hit) {
                        foundLine = inst->cursor.line;
                        foundChar = uted_count_chars(scanLine->utf8,
                                        (ULONG)(hit - scanLine->utf8));
                        found = TRUE;
                    }
                }

                /* Search subsequent lines */
                for (lineIdx = inst->cursor.line + 1;
                     lineIdx < inst->lineCount && !found; lineIdx++) {
                    scanLine = uted_get_line(inst, lineIdx);
                    if (!scanLine) continue;
                    const char *hit = uted_strfind(scanLine->utf8, pattern,
                                          inst->searchCaseSensitive);
                    if (hit) {
                        foundLine = lineIdx;
                        foundChar = uted_count_chars(scanLine->utf8,
                                        (ULONG)(hit - scanLine->utf8));
                        found = TRUE;
                    }
                }

                if (found) {
                    uted_apply_search_match(inst, foundLine, foundChar,
                                            patCharLen, FALSE);
                    redraw = TRUE;
                }
            }
            result = 1;
            break;
        }

        case UTED_SearchPrevious:
        {
            const char *pattern = (const char *)tag->ti_Data;
            if (pattern && *pattern) {
                ULONG patByteLen = (ULONG)strlen(pattern);
                ULONG patCharLen = uted_count_chars(pattern, patByteLen);
                BOOL  found = FALSE;
                ULONG foundLine = 0, foundChar = 0;
                UniTextEditorLine *scanLine;
                LONG  lineIdx;

                /* Search backward on current line: last match starting < cursor */
                scanLine = uted_get_line(inst, inst->cursor.line);
                if (scanLine && inst->cursor.ch > 0) {
                    ULONG limitByte = uted_char_to_byte(scanLine->utf8,
                                          scanLine->byteUsed, inst->cursor.ch);
                    const char *hit = uted_rfind_before_cs(scanLine->utf8,
                                          limitByte, pattern,
                                          inst->searchCaseSensitive);
                    if (hit) {
                        foundLine = inst->cursor.line;
                        foundChar = uted_count_chars(scanLine->utf8,
                                        (ULONG)(hit - scanLine->utf8));
                        found = TRUE;
                    }
                }

                /* Search previous lines from their end */
                for (lineIdx = (LONG)inst->cursor.line - 1;
                     lineIdx >= 0 && !found; lineIdx--) {
                    scanLine = uted_get_line(inst, (ULONG)lineIdx);
                    if (!scanLine) continue;
                    const char *hit = uted_rfind_before_cs(scanLine->utf8,
                                          scanLine->byteUsed, pattern,
                                          inst->searchCaseSensitive);
                    if (hit) {
                        foundLine = (ULONG)lineIdx;
                        foundChar = uted_count_chars(scanLine->utf8,
                                        (ULONG)(hit - scanLine->utf8));
                        found = TRUE;
                    }
                }

                if (found) {
                    uted_apply_search_match(inst, foundLine, foundChar,
                                            patCharLen, TRUE);
                    redraw = TRUE;
                }
            }
            result = 1;
            break;
        }

        case UTED_ReplaceString:
        {
            const char *replaceStr = (const char *)tag->ti_Data;
            if (!replaceStr) replaceStr = "";
            if (inst->lastSearchValid && !inst->readOnly) {
                inst->lastSearchValid = FALSE;
                /* Restore selection to the saved match, then insert replaces it */
                inst->selAnchor.line = inst->lastSearchLine;
                inst->selAnchor.ch   = inst->lastSearchChar;
                inst->selFloat.line  = inst->lastSearchLine;
                inst->selFloat.ch    = inst->lastSearchEndChar;
                inst->cursor         = inst->selFloat;
                inst->hasSelection   = (inst->lastSearchChar != inst->lastSearchEndChar);
                UniTextEditor_DoInsertText(cl, o, replaceStr, -1);
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_SearchCaseSensitive:
            inst->searchCaseSensitive = tag->ti_Data ? TRUE : FALSE;
            result = 1;
            break;

        case UTED_VisibleTabs:
        {
            BOOL newVT = tag->ti_Data ? TRUE : FALSE;
            if (newVT != inst->visibleTabs) {
                inst->visibleTabs = newVT;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_DisplayInternalVScroll:
        {
            BOOL newVal = tag->ti_Data ? TRUE : FALSE;
            if (newVal != inst->displayInternalVScroll) {
                inst->displayInternalVScroll = newVal;
                redraw = TRUE;
            }
            result = 1;
            break;
        }

        case UTED_TabsAreSpaces:
            inst->tabsAreSpaces = tag->ti_Data ? TRUE : FALSE;
            result = 1;
            break;

        case UTED_ApplyAnsiEscapes:
            if ((inst->applyAnsiEscapes ? 1 : 0) != (tag->ti_Data ? 1 : 0)) {
                inst->applyAnsiEscapes = tag->ti_Data ? TRUE : FALSE;
                uted_invalidate_all_line_caches(inst);
                result = 1;
                redraw = TRUE;
            }
            break;

        case UTED_AnsiUnixColors:
            if ((inst->ansiUnixColors ? 1 : 0) != (tag->ti_Data ? 1 : 0)) {
                inst->ansiUnixColors = tag->ti_Data ? TRUE : FALSE;
                uted_invalidate_all_line_caches(inst);
                result = 1;
                redraw = TRUE;
            }
            break;

        case UTED_LineTextToGet:
        {
            ULONG idx = (ULONG)tag->ti_Data;
            if (inst->lineCount > 0 && idx >= inst->lineCount)
                idx = inst->lineCount - 1;
            inst->lineIndexToGet = idx;
            result = 1;
            break;
        }

        case UTED_CurrentContext:
            /* Already handled by the FindTagItem pre-pass above; mark done. */
            result = 1;
            redraw = TRUE;
            break;

        case UTED_RenameContext:
        {
            const char *newName = (const char *)tag->ti_Data;
            if (newName && newName[0] != '\0') {
                /* Rename any stash entry that still has the old name */
                UTEDTextStash *stash = uted_stash_find(inst,
                                                       inst->currentContextName);
                if (stash)
                    strncpy(stash->contextName, newName,
                            UTED_CONTEXT_NAME_MAX - 1);
                /* Rename the live context */
                strncpy(inst->currentContextName, newName,
                        UTED_CONTEXT_NAME_MAX - 1);
                inst->currentContextName[UTED_CONTEXT_NAME_MAX - 1] = '\0';
            }
            result = 1;
            break;
        }

        case UTED_DeleteContext:
        {
            const char *name = (const char *)tag->ti_Data;
            UTEDTextStash *stash;
            if (!name || name[0] == '\0') break;
            /* Refuse to delete the currently active context */
            if (strcmp(inst->currentContextName, name) == 0) break;
            stash = uted_stash_find(inst, name);
            if (stash) uted_stash_remove(inst, stash);
            result = 1;
            break;
        }

        case UTED_FlushDebugOutput:
            flushbdbprint();
            break;

        case UTED_ApplyCopy:
            uted_do_clipboard_copy(cl, o);
            result = 1;
            break;

        case UTED_ApplyCut:
            if (!inst->readOnly) {
                if (uted_do_clipboard_cut(cl, o))
                    redraw = TRUE;
            }
            result = 1;
            break;

        case UTED_ApplyPaste:
            if (!inst->readOnly) {
                if (uted_do_clipboard_paste(cl, o)) {
                    redraw = TRUE;
                }
            }
            result = 1;
            break;

        /* should be done by supeclass, but there's a OS3.9 bu with super method */
        case ICA_TARGET:
            inst->target = (Object*)tag->ti_Data;
            result = 1;
            break;
        /* should be done by supeclass, but there's a OS3.9 bug*/
        case GA_ID:
            inst->ga_id = tag->ti_Data;
            break;
        default: break;
        }
    }

    ReleaseSemaphore(&inst->textSem);

    if(changepens)
    {
        uted_invalidate_all_line_caches(inst);
        /* will flush the indexed color remap cache */
        URPDC_SetDrawColorFromPen(inst->dc,inst->screen,inst->txtPen,inst->bgPen);
        uted_update_halfway_pen(inst);
    }

    if (redraw && msg->ops_GInfo)
        uted_render_self(cl, o, msg->ops_GInfo);

    if (inst->scrollTopLine != scrollTopBefore ||
        inst->scrollLeftPx  != scrollLeftBefore)
    {
        /* Scroll changed: newly visible rows must be rendered fully */
        inst->refreshStartLine = 0;
        inst->refreshEndLine   = ~0L;
        if (msg->ops_GInfo)
            uted_notify(cl, o, msg->ops_GInfo, UTEDN_ScrollChanged, inst->scrollTopLine);
    }

    return result;
}

/* =========================================================================
 * OM_GET
 * =========================================================================
 */
ULONG UniTextEditor_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);

    switch (msg->opg_AttrID)
    {
    case UTED_URPDrawContext:
        *msg->opg_Storage = (ULONG)inst->dc;
        return TRUE;
    case GA_ReadOnly:
        *msg->opg_Storage = (ULONG)inst->readOnly;
        return TRUE;

    case UTED_LineCount:
        *msg->opg_Storage = inst->lineCount;
        return TRUE;

    case UTED_Modified:
        *msg->opg_Storage = (ULONG)inst->modified;
        return TRUE;

    case UTED_ScrollTop:
        *msg->opg_Storage = inst->scrollTopLine;
        return TRUE;

    case UTED_ScrollLeft:
        *msg->opg_Storage = inst->wordWrap ? 0 : inst->scrollLeftPx;
        return TRUE;

    case UTED_MaxLineWidth:
    {
        UniTextEditorLine *line;
        ULONG maxW;

        if (inst->wordWrap) {
            *msg->opg_Storage = (ULONG)inst->gadWidth;
            return TRUE;
        }
        maxW = 0;
        for (line = (UniTextEditorLine *)inst->lines.mlh_Head;
             line->node.mln_Succ;
             line = (UniTextEditorLine *)line->node.mln_Succ)
        {
            if (line->charXOffsets && line->charCount > 0) {
                ULONG w = (ULONG)line->charXOffsets[line->charCount];
                if (w > maxW) maxW = w;
            }
        }
        // trick for test
        *msg->opg_Storage = maxW;
        return TRUE;
    }

    case UTED_VisibleLines:
        *msg->opg_Storage = (ULONG)inst->visibleLines;
        return TRUE;

    case UTED_CursorLine:
        *msg->opg_Storage = inst->cursor.line;
        return TRUE;

    case UTED_CursorChar:
        *msg->opg_Storage = inst->cursor.ch;
        return TRUE;

    case GA_Text:  /* alias: same behaviour as UTED_Text */
    case UTED_Text:
    {
        /* Assemble complete text; caller must FreeVec() the result */
        UniTextEditorLine *line;
        ULONG           totalBytes = 0;
        char           *buf, *ptr;

        for (line = (UniTextEditorLine *)inst->lines.mlh_Head;
             line->node.mln_Succ;
             line = (UniTextEditorLine *)line->node.mln_Succ)
        {
            totalBytes += line->byteUsed;
            if (line->node.mln_Succ->mln_Succ) totalBytes++; /* '\n' */
        }

        buf = (char *)AllocVec(totalBytes + 1, MEMF_ANY);
        if (!buf) { *msg->opg_Storage = 0; return TRUE; }

        ptr = buf;
        for (line = (UniTextEditorLine *)inst->lines.mlh_Head;
             line->node.mln_Succ;
             line = (UniTextEditorLine *)line->node.mln_Succ)
        {
            if (line->byteUsed) {
                CopyMem(line->utf8, ptr, line->byteUsed);
                ptr += line->byteUsed;
            }
            if (line->node.mln_Succ->mln_Succ) *ptr++ = '\n';
        }
        *ptr = '\0';

        *msg->opg_Storage = (ULONG)buf;
        return TRUE;
    }

    case UTED_TabSpaces:
        *msg->opg_Storage = inst->tabSpaces;
        return TRUE;

    case UTED_MaxNumberOfUndo:
        *msg->opg_Storage = inst->undoMax;
        return TRUE;

    case UTED_VanillaKeyAnsiCode:
        *msg->opg_Storage = inst->vanillaAnsiCode;
        return TRUE;

    case UTED_WordWrap:
        *msg->opg_Storage = (ULONG)inst->wordWrap;
        return TRUE;

    case UTED_RenderedLineCount:
        *msg->opg_Storage = (inst->wordWrap && inst->wrapMap)
                            ? inst->wrapRowCount
                            : inst->lineCount;
        return TRUE;

    case UTED_MaxDisplayLines:
        *msg->opg_Storage = inst->maxDisplayLines;
        return TRUE;

    case UTED_NoLineFeed:
        *msg->opg_Storage = (ULONG)inst->noLineFeed;
        return TRUE;

    case UTED_SetPrivateActivation:
        *msg->opg_Storage = (ULONG)inst->gadgetActive;
        return TRUE;

    case UTED_LeftMargin:
        *msg->opg_Storage = (ULONG)(WORD)inst->leftMargin;
        if(inst->bevel)
        {
            ULONG h;
            GetAttr(BEVEL_VertSize,inst->bevel,&h);
            if(h>*msg->opg_Storage)  *msg->opg_Storage -= h;
        }

        return TRUE;

    case UTED_LineSpacing:
        *msg->opg_Storage = (ULONG)(WORD)inst->lineSpacing;
        return TRUE;

    case UTED_RightMargin:
        *msg->opg_Storage = (ULONG)(WORD)inst->rightMargin;
        if(inst->bevel)
        {
            ULONG h;
            GetAttr(BEVEL_VertSize,inst->bevel,&h);
            if(h>*msg->opg_Storage)  *msg->opg_Storage -= h;
        }
        return TRUE;

    case UTED_TopMargin:
        *msg->opg_Storage = (ULONG)(WORD)inst->topMargin;
        if(inst->bevel)
        {
            ULONG w;
            GetAttr(BEVEL_HorizSize,inst->bevel,&w);
            if(w>*msg->opg_Storage)  *msg->opg_Storage -= w;
        }
        return TRUE;

    case UTED_BottomMargin:
        *msg->opg_Storage = (ULONG)(WORD)inst->bottomMargin;
        if(inst->bevel)
        {
            ULONG h;
            GetAttr(BEVEL_HorizSize,inst->bevel,&h);
            if(h>*msg->opg_Storage)  *msg->opg_Storage -= h;
        }
        return TRUE;

        case UTED_GetSelectedText:
        {
            STRPTR r = NULL;
            UniTextEditor_DoGetSelectedText(cl, o, &r);
            *msg->opg_Storage = (ULONG)r;
            return TRUE;
        }

        case UTED_URPPrefs:
        if(inst->dc)
        {
            ULONG dcflags = URPDC_GetPreferenceFlags(inst->dc);
            *msg->opg_Storage = dcflags;
        } else  *msg->opg_Storage = 0;
        return TRUE;

    case UTED_SearchCaseSensitive:
        *msg->opg_Storage = (ULONG)inst->searchCaseSensitive;
        return TRUE;

    case UTED_VisibleTabs:
        *msg->opg_Storage = (ULONG)inst->visibleTabs;
        return TRUE;

    case UTED_TabsAreSpaces:
        *msg->opg_Storage = (ULONG)inst->tabsAreSpaces;
        return TRUE;

    case UTED_DisplayInternalVScroll:
        *msg->opg_Storage = (ULONG)inst->displayInternalVScroll;
        return TRUE;

    case UTED_InternalRawKey_SendBack:
        *msg->opg_Storage = (ULONG)inst->rawKeySendBack;
        return TRUE;

    case UTED_InternalRawKey_Code:
        *msg->opg_Storage = inst->rawKeyLastCode;
        return TRUE;

    case UTED_ApplyAnsiEscapes:
        *msg->opg_Storage = (ULONG)inst->applyAnsiEscapes;
        return TRUE;

    case UTED_AnsiUnixColors:
        *msg->opg_Storage = (ULONG)inst->ansiUnixColors;
        return TRUE;

    case UTED_LineUTF8TextBuffer:
    {
        UniTextEditorLine *line = uted_get_line(inst, inst->lineIndexToGet);
        *msg->opg_Storage = line ? (ULONG)line->utf8 : 0;
        return TRUE;
    }

    case UTED_CurrentContext:
        *msg->opg_Storage = (ULONG)inst->currentContextName;
        return TRUE;
    /*DoSuperMethodA() totally rotten on OS3.9 !!! */
    case GA_Width:
        *msg->opg_Storage = (LONG) G(o)->Width;
        return TRUE;
    case GA_Height:
        *msg->opg_Storage = (LONG) G(o)->Height;
        return TRUE;
    case GA_Top:
        *msg->opg_Storage = (ULONG)((LONG) G(o)->TopEdge);
        return TRUE;
    case GA_Left:
        *msg->opg_Storage = (ULONG)((LONG) G(o)->LeftEdge);
        return TRUE;
    default:
        return DoSuperMethodA(cl, o, (APTR)msg);
    }
}
