/*
 * TextEditor gadget – UTF-8 buffer primitives + TXEDM method handlers.
 *
 * All functions that modify a line's text set dirty=TRUE and free
 * charXOffsets + cache so they are rebuilt on the next render.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <clib/alib_protos.h>
#include <string.h>
#include <stdio.h>
#include "unitexteditor_private.h"

/* =========================================================================
 * UTF-8 utilities
 * =========================================================================
 */

/* Return byte length of one UTF-8 sequence starting with byte c. */
INLINE ULONG utf8_seqlen(unsigned char c)
{
    if (c < 0x80) return 1;
    if (c < 0xC0) return 1; /* continuation byte (0x80-0xBF): skip 1 to re-sync */
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

ULONG uted_count_chars(const char *utf8, ULONG byteLen)
{
    ULONG count = 0, i = 0;
    while (i < byteLen) {
        i += utf8_seqlen((unsigned char)utf8[i]);
        count++;
    }
    return count;
}

/* Map codepoint index → byte offset.  Returns byteUsed for past-end. */
ULONG uted_char_to_byte(const char *utf8, ULONG byteUsed, ULONG charIndex)
{
    ULONG i = 0, byteOff = 0;
    while (byteOff < byteUsed && i < charIndex) {
        byteOff += utf8_seqlen((unsigned char)utf8[byteOff]);
        i++;
    }
    return byteOff;
}

/* =========================================================================
 * MinList helpers
 * =========================================================================
 */

INLINE void uted_list_init(struct MinList *list)
{
    list->mlh_Head     = (struct MinNode *)&list->mlh_Tail;
    list->mlh_Tail     = NULL;
    list->mlh_TailPred = (struct MinNode *)&list->mlh_Head;
}

INLINE void uted_list_add_tail(struct MinList *list, UniTextEditorLine *node)
{
    struct MinNode *tailPred = list->mlh_TailPred;
    node->node.mln_Succ      = (struct MinNode *)&list->mlh_Tail;
    node->node.mln_Pred      = tailPred;
    tailPred->mln_Succ       = (struct MinNode *)node;
    list->mlh_TailPred       = (struct MinNode *)node;
}

/* Insert node AFTER pred. */
INLINE void uted_list_insert_after(UniTextEditorLine *pred, UniTextEditorLine *node)
{
    struct MinNode *next  = pred->node.mln_Succ;
    node->node.mln_Succ   = next;
    node->node.mln_Pred   = (struct MinNode *)pred;
    pred->node.mln_Succ   = (struct MinNode *)node;
    next->mln_Pred        = (struct MinNode *)node;
}

INLINE void uted_list_remove(UniTextEditorLine *node)
{
    struct MinNode *succ = node->node.mln_Succ;
    struct MinNode *pred = node->node.mln_Pred;
    pred->mln_Succ = succ;
    succ->mln_Pred = pred;
}

/* =========================================================================
 * Line allocation / deallocation
 * =========================================================================
 */

#define LINE_ALLOC_EXTRA  64   /* extra bytes to reduce realloc frequency */

UniTextEditorLine *uted_line_alloc(const char *utf8, ULONG byteLen)
{
    UniTextEditorLine *line;
    ULONG alloc;

    line = (UniTextEditorLine *)AllocVec(sizeof(UniTextEditorLine), MEMF_ANY | MEMF_CLEAR);
    if (!line) return NULL;

    alloc = byteLen + LINE_ALLOC_EXTRA;
    line->utf8 = (char *)AllocVec(alloc, MEMF_ANY);
    if (!line->utf8) { FreeVec(line); return NULL; }

    if (utf8 && byteLen > 0)
        CopyMem((APTR)utf8, line->utf8, byteLen);

    line->utf8[byteLen] = '\0';
    line->byteAlloc     = alloc;
    line->byteUsed      = byteLen;
    line->charCount     = uted_count_chars(line->utf8, byteLen);
    line->dirty         = TRUE;
    return line;
}

void uted_line_invalidate(UniTextEditorLine *line)
{
    if (line->charXOffsets) { FreeVec(line->charXOffsets); line->charXOffsets = NULL; }
    if (line->ansiRuns)     { FreeVec(line->ansiRuns);     line->ansiRuns = NULL;
                               line->ansiRunCount = 0;      line->ansiRunAlloc = 0; }
    line->dirty = TRUE;
    /* Chunk bitmaps are NOT freed here.  On next GM_RENDER the dirty flag
     * causes all allocated chunks to be re-rendered in-place, avoiding
     * AllocBitMap/FreeBitMap churn that fragments chip RAM. */
}

void uted_line_free(UniTextEditorData *inst, UniTextEditorLine *line)
{
    if (!line) return;
    uted_line_free_cache(inst, line); /* return bitmaps to pool + free ptr array */
    if (line->charXOffsets) { FreeVec(line->charXOffsets); line->charXOffsets = NULL; }
    if (line->utf8) { FreeVec(line->utf8); line->utf8 = NULL; }
    FreeVec(line);
}

/* =========================================================================
 * Text mutation
 * =========================================================================
 */

BOOL uted_line_insert_bytes(UniTextEditorLine *line,
                             ULONG byteOffset,
                             const char *utf8, ULONG byteLen)
{
    ULONG newUsed = line->byteUsed + byteLen;

    if (newUsed + 1 > line->byteAlloc) {
        /* Reallocate larger buffer */
        ULONG newAlloc = newUsed + LINE_ALLOC_EXTRA;
        char *newBuf = (char *)AllocVec(newAlloc, MEMF_ANY);
        if (!newBuf) return FALSE;
        CopyMem(line->utf8, newBuf, byteOffset);
        CopyMem((APTR)utf8, newBuf + byteOffset, byteLen);
        CopyMem(line->utf8 + byteOffset,
                newBuf + byteOffset + byteLen,
                line->byteUsed - byteOffset + 1); /* +1 for NUL */
        FreeVec(line->utf8);
        line->utf8      = newBuf;
        line->byteAlloc = newAlloc;
    } else {
        /* Shift tail right then insert */
        ULONG tailLen = line->byteUsed - byteOffset + 1; /* includes NUL */
        memmove(line->utf8 + byteOffset + byteLen,
                line->utf8 + byteOffset,
                tailLen);
        CopyMem((APTR)utf8, line->utf8 + byteOffset, byteLen);
    }

    line->byteUsed  += byteLen;
    line->charCount += uted_count_chars(utf8, byteLen);
    uted_line_invalidate(line);
    return TRUE;
}

BOOL uted_line_delete_bytes(UniTextEditorLine *line, ULONG byteOffset, ULONG byteLen)
{
    if (byteOffset + byteLen > line->byteUsed) return FALSE;

    /* Count chars being removed */
    ULONG removedChars = uted_count_chars(line->utf8 + byteOffset, byteLen);

    /* Shift tail left (includes NUL) */
    memmove(line->utf8 + byteOffset,
            line->utf8 + byteOffset + byteLen,
            line->byteUsed - byteOffset - byteLen + 1);

    line->byteUsed  -= byteLen;
    line->charCount -= removedChars;
    uted_line_invalidate(line);
    return TRUE;
}

/* Split line at charIndex: [0..charIndex) stays in line,
 * [charIndex..end) moves to a new tail line (not added to list). */
UniTextEditorLine *uted_line_split(UniTextEditorLine *line, ULONG charIndex)
{
    ULONG splitByte = uted_char_to_byte(line->utf8, line->byteUsed, charIndex);
    ULONG tailLen   = line->byteUsed - splitByte;

    UniTextEditorLine *tail = uted_line_alloc(line->utf8 + splitByte, tailLen);
    if (!tail) return NULL;

    /* Truncate head */
    line->utf8[splitByte] = '\0';
    line->byteUsed  = splitByte;
    line->charCount = charIndex;
    uted_line_invalidate(line);
    return tail;
}

/* Append tail's text onto head, remove tail from list, free it. */
BOOL uted_line_join(UniTextEditorData *inst, UniTextEditorLine *head, UniTextEditorLine *tail)
{
    BOOL ok = uted_line_insert_bytes(head, head->byteUsed,
                                      tail->utf8, tail->byteUsed);
    if (!ok) return FALSE;
    uted_list_remove(tail);
    uted_line_free(inst, tail);
    inst->lineCount--;
    return TRUE;
}

/* =========================================================================
 * List navigation helpers
 * =========================================================================
 */

UniTextEditorLine *uted_get_line(UniTextEditorData *inst, ULONG index)
{
    UniTextEditorLine *line = (UniTextEditorLine *)inst->lines.mlh_Head;
    while (line->node.mln_Succ && index > 0) {
        line = (UniTextEditorLine *)line->node.mln_Succ;
        index--;
    }
    return line->node.mln_Succ ? line : NULL;
}

/* =========================================================================
 * Scroll helpers
 * =========================================================================
 */

void uted_invalidate_all_line_caches(UniTextEditorData *inst)
{
    UniTextEditorLine *line;
    for (line = (UniTextEditorLine *)inst->lines.mlh_Head;
         line->node.mln_Succ;
         line = (UniTextEditorLine *)line->node.mln_Succ)
    {
        uted_line_invalidate(line);
    }
    /* Metrics will be rebuilt, so wrap points change too */
    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
}

void uted_ensure_cursor_visible(UniTextEditorData *inst)
{
    ULONG visRow;

    if (inst->visibleLines <= 0) return;

    /* In wordWrap mode scrollTopLine is a visual row index, not a logical
     * line index.  Use the wrap map to find the cursor's visual row.
     * When the map is dirty/absent we fall back to the logical line index. */
    visRow = uted_cursor_visual_row(inst);

    if (visRow < inst->scrollTopLine) {
        inst->scrollTopLine = visRow;
    } else if (visRow >= inst->scrollTopLine + (ULONG)inst->visibleLines) {
        inst->scrollTopLine = visRow - (ULONG)inst->visibleLines + 1;
    }
}

void uted_ensure_cursor_h_visible(UniTextEditorData *inst)
{
    UniTextEditorLine *line;
    LONG cursorPx, gadW, cursorInGad;

    /* Word-wrap: cursor always visible horizontally; no scroll needed */
    if (inst->wordWrap) { inst->scrollLeftPx = 0; return; }

    if (inst->gadWidth <= 0) return;

    line = uted_get_line(inst, inst->cursor.line);
    if (!line) return;

    if (!line->charXOffsets && inst->dc)
        uted_line_build_metrics(line, inst);
    if (!line->charXOffsets) return;
    if (inst->cursor.ch > line->charCount) return;

    cursorPx    = (LONG)line->charXOffsets[inst->cursor.ch];
    gadW        = (LONG)inst->gadWidth - (LONG)inst->leftMargin - (LONG)inst->rightMargin;
    if (gadW < 0) gadW = 0;
    cursorInGad = cursorPx - (LONG)inst->scrollLeftPx;

    /* Cursor at or past the right margin (within 32px of right edge):
     * pin cursor to gadW-32.  Typing right re-fires every char (cursor stays
     * pinned).  Moving left takes cursor to gadW-40 which is < gadW-32, so
     * this condition does NOT re-fire — no spurious left scroll. */
    if (cursorInGad >= gadW - 32) {
        LONG newLeft = cursorPx - (gadW - 32);
        inst->scrollLeftPx = (newLeft > 0) ? (ULONG)newLeft : 0;
    }
    /* Cursor within 64px of left edge (and viewport is scrolled right):
     * scroll left so cursor lands 96px from left — outside the 64px band,
     * preventing immediate left-retrigger.  ch==0 snaps to column 0. */
    else if (cursorInGad < 64 && inst->scrollLeftPx > 0) {
        if (inst->cursor.ch == 0) {
            inst->scrollLeftPx = 0;
        } else {
            LONG newLeft = cursorPx - 96;
            inst->scrollLeftPx = (newLeft > 0) ? (ULONG)newLeft : 0;
        }
    }
}

/* =========================================================================
 * Notify & self-render helpers
 * =========================================================================
 */

void uted_notify(Class *cl, Object *o, struct GadgetInfo *gi, ULONG tag, ULONG value)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    struct opUpdate nmsg;
    ULONG tags[5];



    tags[0] = GA_ID;
    tags[1] = 0;
    //good on os3.2 GetAttr(GA_ID, o, &tags[1]);
    tags[1] = inst->ga_id;

 //     bdbprintf("uted_notify GA_ID %08x !inst->target %08x\n",(int)ie->ie_Class);


    if (!tags[1] || !inst->target) return;

    tags[2] = tag;
    tags[3] = value;
    tags[4] = TAG_DONE;
/* good on os3.2, and 1992 boopsi compliant
    nmsg.MethodID     = OM_NOTIFY;
    nmsg.opu_AttrList = (struct TagItem *)tags;
    nmsg.opu_GInfo    = gi;
    nmsg.opu_Flags    = 0;
    DoSuperMethodA(cl, (APTR)o, (Msg)&nmsg);
    */
/*
    OS3.9 boopsi can't send
    notification by the "DoSuperMethodA" and GetAttr(GA_ID,...) mecanism
*/
    nmsg.MethodID     = OM_UPDATE;
    nmsg.opu_AttrList = (struct TagItem *)tags;
    nmsg.opu_GInfo    = gi;
    nmsg.opu_Flags    = 0;
    DoMethodA(inst->target,(Msg)&nmsg);

}

void uted_render_self(Class *cl, Object *o, struct GadgetInfo *gi)
{
    struct RastPort *rp;
    struct gpRender  rmsg;

    (void)cl;
    rp = ObtainGIRPort(gi);
    if (!rp) return;

    rmsg.MethodID   = GM_RENDER;
    rmsg.gpr_GInfo  = gi;
    rmsg.gpr_RPort  = rp;
    rmsg.gpr_Redraw = GREDRAW_UPDATE;
    DoMethodA(o, (Msg)(APTR)&rmsg);

    ReleaseGIRPort(rp);
}

/* =========================================================================
 * Action handlers
 * =========================================================================
 */

/* -------------------------------------------------------------------------
 * DoInsertText – inserts UTF-8 text at cursor.  '\n' splits lines.
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoInsertText(Class *cl, Object *o, const char *text, LONG length)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    ULONG           len;
    UniTextEditorLine *curLine;
    ULONG           byteOff;
    const char     *p, *end, *nl;
    ULONG           segLen;
    ULONG nbNewLines = 0;
    UniTextEditorPos  posBeforeInsert;

    if (!text) return 0;
    len = (length >= 0) ? (ULONG)length : (ULONG)strlen(text);
    if (!len) return 0;

    /* Delete selection first if any (records its own undo entry)
     but not for tab key (tab with selection has its own handling)
    */
    if (inst->hasSelection && !(length == 1 && (*text) == 0x09))
        UniTextEditor_DoDeleteSelection(cl, o);

    /* Cursor position right before the insert (= after any sel-delete) */
    posBeforeInsert = inst->cursor;

    inst->refreshStartLine = inst->cursor.line;

    curLine = uted_get_line(inst, inst->cursor.line);
    if (!curLine) return 0;

    byteOff = uted_char_to_byte(curLine->utf8, curLine->byteUsed, inst->cursor.ch);

    p   = text;
    end = text + len;

    while (p < end) {
        nl = p;
        while (nl < end && *nl != '\n' && *nl != '\r') nl++;
        segLen = (ULONG)(nl - p);

        if (segLen > 0) {
            if (!uted_line_insert_bytes(curLine, byteOff, p, segLen))
                break;
            byteOff         += segLen;
            inst->cursor.ch += uted_count_chars(p, segLen);
        }

        if (nl < end) {
            if (inst->noLineFeed) {
                /* noLineFeed mode: keep \n/\r in buffer, don't split lines */
                if (!uted_line_insert_bytes(curLine, byteOff, nl, 1))
                    break;
                byteOff++;
                inst->cursor.ch++;
                p = nl + 1;
            } else {
                /* Normal mode: split line on \n */
                nbNewLines++;
                UniTextEditorLine *tail = uted_line_split(curLine, inst->cursor.ch);
                if (tail) {
                    uted_list_insert_after(curLine, tail);
                    inst->lineCount++;
                    inst->cursor.line++;
                    inst->cursor.ch = 0;
                    curLine = tail;
                    byteOff = 0;
                }
                p = nl + 1;
            }
        } else {
            p = nl;
        }
    }
    if(nbNewLines == 0) inst->refreshEndLine = inst->refreshStartLine;
    else inst->refreshEndLine = ~0;

    inst->modified         = TRUE;
    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
    inst->selAnchor        = inst->cursor;
    inst->selFloat         = inst->cursor;
    inst->hasSelection     = FALSE;
    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    /* Record undo entry for this insert */
    if (!inst->undoInProgress && inst->undoMax > 0) {
        UTEDUndoEntry e;
        BOOL          hasNL = FALSE;
        ULONG         i;

        for (i = 0; i < len; i++) {
            if (text[i] == '\n') { hasNL = TRUE; break; }
        }
        e.opType       = UTED_UNDO_INSERT;
        e.delDir       = 0;
        e.atomic       = hasNL || (len > 1); /* paste / multi-char = atomic */
        e.cursorBefore = posBeforeInsert;
        e.anchorBefore = posBeforeInsert;
        e.hasSelBefore = FALSE;
        e.posStart     = posBeforeInsert;
        e.posEnd       = inst->cursor;
        e.textBytes    = len;
        e.text         = (char *)AllocVec(len + 1, MEMF_ANY);
        if (e.text) {
            CopyMem((APTR)text, e.text, len);
            e.text[len] = '\0';
        } else {
            e.textBytes = 0;
        }
        uted_undo_push(inst, &e);
        uted_undo_notify(cl, o, NULL);
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * DoDeleteChar  (backspace / delete)
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoDeleteChar(Class *cl, Object *o, LONG delta)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UTEDUndoEntry      e;
    BOOL               doRecord = FALSE;
    char               capBuf[5]; /* max UTF-8 seq = 4 bytes + NUL */

    /* If selection active, delegate (DoDeleteSelection records its own entry) */
    if (inst->hasSelection)
        return UniTextEditor_DoDeleteSelection(cl, o);

    memset(&e, 0, sizeof(e));
    e.cursorBefore = inst->cursor;
    e.anchorBefore = inst->selAnchor;
    e.hasSelBefore = FALSE;

    /* In ANSI mode: if the target char is inside an escape sequence, delete
     * the entire sequence atomically instead of one char at a time. */
    if (inst->applyAnsiEscapes && !inst->hasSelection) {
        UniTextEditorLine *line = uted_get_line(inst, inst->cursor.line);
        if (line) {
            ULONG checkCh = (delta == -1 && inst->cursor.ch > 0)
                            ? inst->cursor.ch - 1
                            : inst->cursor.ch;
            ULONG seqStart = 0, seqEnd = 0;
            uted_line_ensure_ansi_runs(line, inst);
            if (uted_ansi_find_seq(line, checkCh, &seqStart, &seqEnd)) {
                ULONG bStart = uted_char_to_byte(line->utf8, line->byteUsed, seqStart);
                ULONG bEnd   = uted_char_to_byte(line->utf8, line->byteUsed, seqEnd);
                ULONG bLen   = bEnd - bStart;
                e.opType        = UTED_UNDO_DELETE;
                e.delDir        = (BYTE)delta;
                e.atomic        = TRUE;
                e.posStart.line = inst->cursor.line;
                e.posStart.ch   = seqStart;
                e.posEnd.line   = inst->cursor.line;
                e.posEnd.ch     = seqEnd;
                e.textBytes     = bLen;
                e.text          = (char *)AllocVec(bLen + 1, MEMF_ANY);
                if (e.text) {
                    CopyMem(line->utf8 + bStart, e.text, bLen);
                    e.text[bLen] = '\0';
                }
                doRecord = TRUE;
                uted_line_delete_bytes(line, bStart, bLen);
                inst->cursor.ch = seqStart;
                inst->refreshStartLine = inst->refreshEndLine = inst->cursor.line;
                inst->modified = TRUE;
                if (inst->wordWrap) inst->wrapMapDirty = TRUE;
                goto delete_done;
            }
        }
    }

    if (delta == -1) {
        /* Backspace */
        if (inst->cursor.ch > 0) {
            UniTextEditorLine *line = uted_get_line(inst, inst->cursor.line);
            if (line) {
                ULONG bEnd   = uted_char_to_byte(line->utf8, line->byteUsed, inst->cursor.ch);
                ULONG bStart = uted_char_to_byte(line->utf8, line->byteUsed, inst->cursor.ch - 1);
                ULONG bLen   = bEnd - bStart;
                /* Capture char before deleting */
                CopyMem(line->utf8 + bStart, capBuf, bLen);
                e.opType        = UTED_UNDO_DELETE;
                e.delDir        = -1;
                e.atomic        = FALSE;
                e.posStart.line = inst->cursor.line;
                e.posStart.ch   = inst->cursor.ch - 1;
                e.posEnd        = inst->cursor;
                e.textBytes     = bLen;
                e.text          = (char *)AllocVec(bLen + 1, MEMF_ANY);
                if (e.text) { CopyMem(capBuf, e.text, bLen); e.text[bLen] = '\0'; }
                doRecord = TRUE;
                uted_line_delete_bytes(line, bStart, bLen);
                inst->cursor.ch--;


                inst->refreshStartLine = inst->refreshEndLine = inst->cursor.line;
            }
        } else if (inst->cursor.line > 0) {
            /* Line join – atomic (the "deleted char" is the implicit newline) */
            UniTextEditorLine *prev = uted_get_line(inst, inst->cursor.line - 1);
            UniTextEditorLine *curr = uted_get_line(inst, inst->cursor.line);
            if (prev && curr) {
                ULONG prevChars = prev->charCount;
                e.opType        = UTED_UNDO_DELETE;
                e.delDir        = -1;
                e.atomic        = TRUE;
                e.posStart.line = inst->cursor.line - 1;
                e.posStart.ch   = prevChars;
                e.posEnd.line   = inst->cursor.line;
                e.posEnd.ch     = 0;
                e.textBytes     = 1;
                e.text          = (char *)AllocVec(2, MEMF_ANY);
                if (e.text) { e.text[0] = '\n'; e.text[1] = '\0'; }
                doRecord = TRUE;
                uted_line_join(inst, prev, curr);
                inst->cursor.line--;
                inst->cursor.ch = prevChars;


                inst->refreshStartLine = inst->cursor.line;
                inst->refreshEndLine = ~0;
            }
        }
    } else {
        /* Forward delete */
        UniTextEditorLine *line = uted_get_line(inst, inst->cursor.line);
        if (line && inst->cursor.ch < line->charCount) {
            ULONG bStart = uted_char_to_byte(line->utf8, line->byteUsed, inst->cursor.ch);
            ULONG bEnd   = uted_char_to_byte(line->utf8, line->byteUsed, inst->cursor.ch + 1);
            ULONG bLen   = bEnd - bStart;
            CopyMem(line->utf8 + bStart, capBuf, bLen);
            e.opType        = UTED_UNDO_DELETE;
            e.delDir        = 1;
            e.atomic        = FALSE;
            e.posStart      = inst->cursor;
            e.posEnd.line   = inst->cursor.line;
            e.posEnd.ch     = inst->cursor.ch + 1;
            e.textBytes     = bLen;
            e.text          = (char *)AllocVec(bLen + 1, MEMF_ANY);
            if (e.text) { CopyMem(capBuf, e.text, bLen); e.text[bLen] = '\0'; }
            doRecord = TRUE;
            uted_line_delete_bytes(line, bStart, bLen);

            inst->refreshStartLine = inst->cursor.line;
            inst->refreshEndLine = inst->cursor.line;

        } else if (line && inst->cursor.ch == line->charCount &&
                   inst->cursor.line + 1 < inst->lineCount)
        {
            /* Line join (fwd) – atomic */
            UniTextEditorLine *next = uted_get_line(inst, inst->cursor.line + 1);
            if (next) {
                e.opType        = UTED_UNDO_DELETE;
                e.delDir        = 1;
                e.atomic        = TRUE;
                e.posStart      = inst->cursor;
                e.posEnd.line   = inst->cursor.line + 1;
                e.posEnd.ch     = 0;
                e.textBytes     = 1;
                e.text          = (char *)AllocVec(2, MEMF_ANY);
                if (e.text) { e.text[0] = '\n'; e.text[1] = '\0'; }
                doRecord = TRUE;
                uted_line_join(inst, line, next);


                inst->refreshStartLine = inst->cursor.line;
                inst->refreshEndLine = ~0;
            }
        }
    }

    inst->modified     = TRUE;
    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;
    inst->hasSelection = FALSE;
    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

delete_done:
    if (doRecord && !inst->undoInProgress && inst->undoMax > 0) {
        uted_undo_push(inst, &e);
        uted_undo_notify(cl, o, NULL);
    } else if (e.text) {
        FreeVec(e.text);
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * DoDeleteToLineStart  (Shift+Backspace)
 * Delete everything from the start of the current line up to the cursor.
 * Cursor moves to column 0.  No-op at column 0 or in read-only mode.
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoDeleteToLineStart(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UniTextEditorLine *line;
    UTEDUndoEntry      e;
    ULONG bEnd;

    if (inst->readOnly) return 0;

    /* Clear any active selection first */
    inst->hasSelection = FALSE;
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;

    line = uted_get_line(inst, inst->cursor.line);
    if (!line || inst->cursor.ch == 0) return 0;

    bEnd = uted_char_to_byte(line->utf8, line->byteUsed, inst->cursor.ch);
    if (bEnd == 0) return 0;

    memset(&e, 0, sizeof(e));
    e.opType        = UTED_UNDO_DELETE;
    e.delDir        = -1;
    e.atomic        = TRUE;
    e.posStart.line = inst->cursor.line;
    e.posStart.ch   = 0;
    e.posEnd        = inst->cursor;
    e.textBytes     = bEnd;
    e.text          = (char *)AllocVec(bEnd + 1, MEMF_ANY);
    if (e.text) { CopyMem(line->utf8, e.text, bEnd); e.text[bEnd] = '\0'; }
    e.cursorBefore  = inst->cursor;
    e.anchorBefore  = inst->selAnchor;
    e.hasSelBefore  = FALSE;

    uted_line_delete_bytes(line, 0, bEnd);
    inst->cursor.ch    = 0;
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;
    inst->modified     = TRUE;
    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
    inst->refreshStartLine = inst->refreshEndLine = (LONG)inst->cursor.line;
    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    if (!inst->undoInProgress && inst->undoMax > 0) {
        uted_undo_push(inst, &e);
        uted_undo_notify(cl, o, NULL);
    } else if (e.text) {
        FreeVec(e.text);
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * DoDeleteToLineEnd  (Shift+Delete)
 * Delete everything from the cursor to the end of the current line.
 * Cursor stays at the same column (now at end of line).  No-op at end of
 * line or in read-only mode.
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoDeleteToLineEnd(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UniTextEditorLine *line;
    UTEDUndoEntry      e;
    ULONG bStart, bLen;

    if (inst->readOnly) return 0;

    /* Clear any active selection first */
    inst->hasSelection = FALSE;
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;

    line = uted_get_line(inst, inst->cursor.line);
    if (!line || inst->cursor.ch >= line->charCount) return 0;

    bStart = uted_char_to_byte(line->utf8, line->byteUsed, inst->cursor.ch);
    bLen   = line->byteUsed - bStart;
    if (bLen == 0) return 0;

    memset(&e, 0, sizeof(e));
    e.opType        = UTED_UNDO_DELETE;
    e.delDir        = 1;
    e.atomic        = TRUE;
    e.posStart      = inst->cursor;
    e.posEnd.line   = inst->cursor.line;
    e.posEnd.ch     = line->charCount;
    e.textBytes     = bLen;
    e.text          = (char *)AllocVec(bLen + 1, MEMF_ANY);
    if (e.text) { CopyMem(line->utf8 + bStart, e.text, bLen); e.text[bLen] = '\0'; }
    e.cursorBefore  = inst->cursor;
    e.anchorBefore  = inst->selAnchor;
    e.hasSelBefore  = FALSE;

    uted_line_delete_bytes(line, bStart, bLen);
    /* cursor.ch is already correct: content after it was deleted */
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;
    inst->modified     = TRUE;
    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
    inst->refreshStartLine = inst->refreshEndLine = (LONG)inst->cursor.line;
    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    if (!inst->undoInProgress && inst->undoMax > 0) {
        uted_undo_push(inst, &e);
        uted_undo_notify(cl, o, NULL);
    } else if (e.text) {
        FreeVec(e.text);
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * DoMoveCursor
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoMoveCursor(Class *cl, Object *o, LONG deltaChar, LONG deltaLine, BOOL extend)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);

    if (!extend) {
        inst->hasSelection = FALSE;
        inst->selAnchor    = inst->cursor;
    }

    /* Horizontal */
    if (deltaChar != 0) {
        UniTextEditorLine *line = uted_get_line(inst, inst->cursor.line);
        if (deltaChar > 0) {
            ULONG step = (ULONG)deltaChar;
            while (step-- && line) {
                if (inst->cursor.ch < line->charCount) {
                    inst->cursor.ch++;

    
                    inst->refreshStartLine = inst->refreshEndLine = inst->cursor.line;

                } else if (inst->cursor.line + 1 < inst->lineCount) {
                    inst->cursor.line++;
                    inst->cursor.ch = 0;
                    line = uted_get_line(inst, inst->cursor.line);

    
                    inst->refreshStartLine = inst->cursor.line -1;
                    inst->refreshEndLine = inst->cursor.line;
                }
            }
        } else {
            ULONG step = (ULONG)(-deltaChar);
            while (step-- && line) {
                if (inst->cursor.ch > 0) {
                    inst->cursor.ch--;

    
                    inst->refreshStartLine = inst->refreshEndLine = inst->cursor.line;

                } else if (inst->cursor.line > 0) {
                    inst->cursor.line--;
                    line = uted_get_line(inst, inst->cursor.line);
                    if (line) inst->cursor.ch = line->charCount;

    
                    inst->refreshStartLine = inst->cursor.line;
                    inst->refreshEndLine = inst->cursor.line +1;
                }
            }
        }
    }

    /* Vertical */
    if (deltaLine != 0) {
        if (inst->wordWrap && inst->wrapMap && inst->wrapRowCount > 0) {
            /* In wordWrap mode move through visual rows, not logical lines */
            ULONG curVisRow  = uted_cursor_visual_row(inst);
            UTEDWrapRow *curWR = &inst->wrapMap[curVisRow];
            ULONG targetVisRow;
            ULONG curAbsPx = 0; /* cursor absolute pixel X in logical line */

            {
                UniTextEditorLine *cl2 = uted_get_line(inst, curWR->logicalLine);
                if (cl2 && cl2->charXOffsets && inst->cursor.ch <= cl2->charCount)
                    curAbsPx = (ULONG)cl2->charXOffsets[inst->cursor.ch];
            }

            if (deltaLine > 0) {
                ULONG step = (ULONG)deltaLine;
                targetVisRow = (curVisRow + step < inst->wrapRowCount)
                               ? curVisRow + step : inst->wrapRowCount - 1;
            } else {
                ULONG step = (ULONG)(-deltaLine);
                targetVisRow = (step <= curVisRow) ? curVisRow - step : 0;
            }

            {
                UTEDWrapRow *twr   = &inst->wrapMap[targetVisRow];
                UniTextEditorLine *tline = uted_get_line(inst, twr->logicalLine);
                ULONG newCh = twr->startChar;

                if (tline && tline->charXOffsets) {
                    /* Find char closest to curAbsPx, clamped to this visual row */
                    newCh = uted_x_to_char(tline, (WORD)curAbsPx);
                    if (newCh < twr->startChar) newCh = twr->startChar;
                    if (newCh > twr->endChar)   newCh = twr->endChar;
                    /* On a non-last row endChar belongs to next row; stay within */
                    if (twr->endChar < tline->charCount && newCh == twr->endChar)
                        newCh = twr->endChar > 0 ? twr->endChar - 1 : 0;
                }


                inst->refreshStartLine = (twr->logicalLine < inst->cursor.line)
                                         ? twr->logicalLine : inst->cursor.line;
                inst->refreshEndLine   = (twr->logicalLine > inst->cursor.line)
                                         ? twr->logicalLine : inst->cursor.line;
                inst->cursor.line = twr->logicalLine;
                inst->cursor.ch   = newCh;
            }
        } else {
            /* Normal (non-wrap) vertical movement by logical lines */
            if (deltaLine > 0) {
                ULONG step = (ULONG)deltaLine;

                inst->refreshStartLine = inst->cursor.line;
                if (inst->cursor.line + step >= inst->lineCount)
                    inst->cursor.line = inst->lineCount > 0 ? inst->lineCount - 1 : 0;
                else
                    inst->cursor.line += step;
                inst->refreshEndLine = inst->cursor.line;
            } else {
                ULONG step = (ULONG)(-deltaLine);

                inst->refreshEndLine = inst->cursor.line;
                if (step > inst->cursor.line) inst->cursor.line = 0;
                else                          inst->cursor.line -= step;
                inst->refreshStartLine = inst->cursor.line;
            }
            /* Clamp char to new line length */
            {
                UniTextEditorLine *line = uted_get_line(inst, inst->cursor.line);
                if (line && inst->cursor.ch > line->charCount)
                    inst->cursor.ch = line->charCount;
            }
        }
    }

    if (extend) {
        inst->selFloat     = inst->cursor;
        inst->hasSelection = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) != 0);
    } else {
        inst->selAnchor = inst->cursor;
        inst->selFloat  = inst->cursor;
    }

    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);
    uted_notify(cl, o, NULL, UTEDN_CursorMoved, inst->cursor.line);
    return 1;
}

/* -------------------------------------------------------------------------
 * DoSetCursorPos
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoSetCursorPos(Class *cl, Object *o, ULONG line, ULONG ch, BOOL extend)
{
    UniTextEditorData *inst    = UTED_DATA(cl, o);
    UniTextEditorLine *ln;
    UniTextEditorPos   newPos;

    if (!extend) {
        inst->hasSelection = FALSE;
        inst->selAnchor    = inst->cursor;
    }

    newPos.line = line < inst->lineCount ? line : inst->lineCount - 1;
    ln = uted_get_line(inst, newPos.line);
    if (!ln) return 0;
    newPos.ch = ch <= ln->charCount ? ch : ln->charCount;

    inst->cursor = newPos;

    if (extend) {
        inst->selFloat     = inst->cursor;
        inst->hasSelection = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) != 0);
    } else {
        inst->selAnchor = inst->cursor;
        inst->selFloat  = inst->cursor;
    }

    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);
    return 1;
}

/* -------------------------------------------------------------------------
 * DoClearSelection
 * ------------------------------------------------------------------------- */
void UniTextEditor_DoClearSelection(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);

    if( inst->hasSelection )
    {
        const UniTextEditorPos  *startPos, *endPos;
        startPos = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) <= 0)
                   ? &inst->selAnchor : &inst->selFloat;
        endPos   = (startPos == &inst->selAnchor) ? &inst->selFloat : &inst->selAnchor;

        inst->refreshStartLine = startPos->line;
        inst->refreshEndLine = endPos->line;
    }
    inst->hasSelection = FALSE;
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;
}

/* -------------------------------------------------------------------------
 * DoSelectAll
 * ------------------------------------------------------------------------- */
void UniTextEditor_DoSelectAll(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UniTextEditorLine *lastLine;
    (void)cl;

    inst->selAnchor.line = 0;
    inst->selAnchor.ch   = 0;

    lastLine = uted_get_line(inst, inst->lineCount > 0 ? inst->lineCount - 1 : 0);
    inst->selFloat.line  = inst->lineCount > 0 ? inst->lineCount - 1 : 0;
    inst->selFloat.ch    = lastLine ? lastLine->charCount : 0;

    inst->cursor       = inst->selFloat;
    inst->hasSelection = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) != 0);
    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    inst->refreshStartLine = 0;
    inst->refreshEndLine = ~0;
}

/* -------------------------------------------------------------------------
 * DoDeleteSelection
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoDeleteSelection(Class *cl, Object *o)
{
    UniTextEditorData       *inst = UTED_DATA(cl, o);
    const UniTextEditorPos  *startPos, *endPos;
    UniTextEditorLine       *startLine, *endLine;
    ULONG                 startByte, endByte;
    /* undo bookkeeping */
    UniTextEditorPos  undoPosStart, undoPosEnd;
    UniTextEditorPos  savedCursor, savedAnchor;
    BOOL              savedHasSel;
    char             *undoText  = NULL;
    ULONG             undoBytes = 0;

    if (!inst->hasSelection) return 0;

    startPos = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) <= 0)
               ? &inst->selAnchor : &inst->selFloat;
    endPos   = (startPos == &inst->selAnchor) ? &inst->selFloat : &inst->selAnchor;

    inst->refreshStartLine = startPos->line;
    inst->refreshEndLine = ~0;

    /* Capture state before any modification */
    undoPosStart = *startPos;
    undoPosEnd   = *endPos;
    savedCursor  = inst->cursor;
    savedAnchor  = inst->selAnchor;
    savedHasSel  = inst->hasSelection;

    if (!inst->undoInProgress && inst->undoMax > 0)
        undoBytes = UniTextEditor_DoGetSelectedText(cl, o, &undoText);

    startLine = uted_get_line(inst, startPos->line);
    endLine   = uted_get_line(inst, endPos->line);
    if (!startLine || !endLine) return 0;

    if (startPos->line == endPos->line) {
        /* Single line selection */
        startByte = uted_char_to_byte(startLine->utf8, startLine->byteUsed, startPos->ch);
        endByte   = uted_char_to_byte(startLine->utf8, startLine->byteUsed, endPos->ch);
        uted_line_delete_bytes(startLine, startByte, endByte - startByte);
    } else {
        /* Multi-line selection:
         * 1. Delete tail of startLine from startPos->ch onward
         * 2. Delete whole intermediate lines
         * 3. Delete head of endLine up to endPos->ch
         * 4. Join startLine and endLine */

        startByte = uted_char_to_byte(startLine->utf8, startLine->byteUsed, startPos->ch);
        uted_line_delete_bytes(startLine, startByte,
                               startLine->byteUsed - startByte);

        /* Delete intermediate + end line head */
        {
            ULONG lineIdx = startPos->line + 1;
            while (lineIdx <= endPos->line) {
                UniTextEditorLine *victim = uted_get_line(inst, startPos->line + 1);
                if (!victim) break;
                if (lineIdx == endPos->line) {
                    /* Trim head of last line, then join */
                    endByte = uted_char_to_byte(victim->utf8, victim->byteUsed, endPos->ch);
                    uted_line_delete_bytes(victim, 0, endByte);
                    if (!uted_line_join(inst, startLine, victim)) {
                        /* Join failed (alloc error): remove victim manually
                         * to keep the list and lineCount consistent. */
                        uted_list_remove(victim);
                        uted_line_free(inst, victim);
                        inst->lineCount--;
                    }
                } else {
                    uted_list_remove(victim);
                    uted_line_free(inst, victim);
                    inst->lineCount--;
                }
                lineIdx++;
            }
        }
    }

    inst->cursor       = *startPos;
    inst->selAnchor    = inst->cursor;
    inst->selFloat     = inst->cursor;
    inst->hasSelection = FALSE;
    inst->modified     = TRUE;
    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    /* Record undo for this selection delete */
    if (!inst->undoInProgress && inst->undoMax > 0 && undoText) {
        UTEDUndoEntry e;
        e.opType       = UTED_UNDO_DELETE;
        e.delDir       = 0;
        e.atomic       = TRUE;
        e.cursorBefore = savedCursor;
        e.anchorBefore = savedAnchor;
        e.hasSelBefore = savedHasSel;
        e.posStart     = undoPosStart;
        e.posEnd       = undoPosEnd;
        e.text         = undoText;
        e.textBytes    = undoBytes;
        uted_undo_push(inst, &e);
        uted_undo_notify(cl, o, NULL);
    } else if (undoText) {
        FreeVec(undoText);
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * DoScrollTo
 * ------------------------------------------------------------------------- */
void UniTextEditor_DoScrollTo(Class *cl, Object *o, ULONG lineIdx, BOOL center)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    ULONG            line = lineIdx < inst->lineCount ? lineIdx : inst->lineCount - 1;
    (void)cl;

    if (center && inst->visibleLines > 1) {
        ULONG half = (ULONG)inst->visibleLines / 2;
        inst->scrollTopLine = (line >= half) ? line - half : 0;
    } else {
        inst->scrollTopLine = line;
    }
}

/* -------------------------------------------------------------------------
 * DoGetSelectedText
 * ------------------------------------------------------------------------- */
ULONG UniTextEditor_DoGetSelectedText(Class *cl, Object *o, STRPTR *result)
{
    UniTextEditorData       *inst = UTED_DATA(cl, o);
    const UniTextEditorPos  *startPos, *endPos;
    UniTextEditorLine       *line;
    ULONG                 totalBytes = 0;
    ULONG                 lineIdx;
    char                 *buf, *ptr;
    (void)cl;

    if (!result) return 0;
    *result = NULL;
    if (!inst->hasSelection) return 0;

    startPos = (uted_pos_cmp(&inst->selAnchor, &inst->selFloat) <= 0)
               ? &inst->selAnchor : &inst->selFloat;
    endPos   = (startPos == &inst->selAnchor) ? &inst->selFloat : &inst->selAnchor;

    /* Measure total bytes */
    for (lineIdx = startPos->line; lineIdx <= endPos->line; lineIdx++) {
        line = uted_get_line(inst, lineIdx);
        if (!line) break;
        ULONG from = (lineIdx == startPos->line) ? startPos->ch : 0;
        ULONG to   = (lineIdx == endPos->line)   ? endPos->ch   : line->charCount;
        ULONG b1   = uted_char_to_byte(line->utf8, line->byteUsed, from);
        ULONG b2   = uted_char_to_byte(line->utf8, line->byteUsed, to);
        totalBytes += b2 - b1;
        if (lineIdx < endPos->line) totalBytes++; /* '\n' */
    }

    buf = (char *)AllocVec(totalBytes + 1, MEMF_ANY);
    if (!buf) return 0;

    ptr = buf;
    for (lineIdx = startPos->line; lineIdx <= endPos->line; lineIdx++) {
        line = uted_get_line(inst, lineIdx);
        if (!line) break;
        ULONG from = (lineIdx == startPos->line) ? startPos->ch : 0;
        ULONG to   = (lineIdx == endPos->line)   ? endPos->ch   : line->charCount;
        ULONG b1   = uted_char_to_byte(line->utf8, line->byteUsed, from);
        ULONG b2   = uted_char_to_byte(line->utf8, line->byteUsed, to);
        CopyMem(line->utf8 + b1, ptr, b2 - b1);
        ptr += b2 - b1;
        if (lineIdx < endPos->line) *ptr++ = '\n';
    }
    *ptr = '\0';

    *result = buf;
    return totalBytes;
}

/* -------------------------------------------------------------------------
 * DoHitTest  (pixel → buffer position)
 * ------------------------------------------------------------------------- */
void UniTextEditor_DoHitTest(Class *cl, Object *o, WORD x, WORD y, ULONG *lineOut, ULONG *charOut)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    ULONG           lineIdx;
    UniTextEditorLine *line;
    (void)cl;

    if (!lineOut || !charOut) return;
    if (inst->lineHeight <= 0) { *lineOut = 0; *charOut = 0; return; }

    if (inst->wordWrap && inst->wrapMap && inst->wrapRowCount > 0) {
        /* Word-wrap: Y maps to a visual row; resolve back to logical line. */
        ULONG visRow;
        UTEDWrapRow *wr;
        LONG relY = (LONG)y - (LONG)inst->topMargin;
        if (relY < 0) relY = 0;
        visRow = inst->scrollTopLine + (ULONG)(relY / inst->lineHeight);
        if (visRow >= inst->wrapRowCount)
            visRow = inst->wrapRowCount - 1;

        wr = &inst->wrapMap[visRow];
        *lineOut = wr->logicalLine;

        line = uted_get_line(inst, wr->logicalLine);
        if (!line) { *charOut = wr->startChar; return; }

        if (!line->charXOffsets && inst->dc)
            uted_line_build_metrics(line, inst);

        if (line->charXOffsets) {
            /* Pixel X relative to gadget left → absolute pixel in logical line */
            LONG adjX = (LONG)x - (LONG)inst->leftMargin;
            ULONG absPx  = (ULONG)(adjX < 0 ? 0 : adjX) + wr->startPixel;
            ULONG ch = uted_x_to_char(line, (WORD)absPx);
            /* Clamp to this visual row's char range */
            if (ch < wr->startChar) ch = wr->startChar;
            if (ch > wr->endChar)   ch = wr->endChar;
            *charOut = ch;
        } else {
            *charOut = wr->startChar;
        }
        return;
    }

    /* Non-wrap: Y maps directly to logical line index */
    {
        LONG relY = (LONG)y - (LONG)inst->topMargin;
        if (relY < 0) relY = 0;
        lineIdx = inst->scrollTopLine + (ULONG)(relY / inst->lineHeight);
        if (lineIdx >= inst->lineCount)
            lineIdx = inst->lineCount > 0 ? inst->lineCount - 1 : 0;
    }

    *lineOut = lineIdx;

    line = uted_get_line(inst, lineIdx);
    if (!line) { *charOut = 0; return; }

    if (!line->charXOffsets && inst->dc)
        uted_line_build_metrics(line, inst);

    if (line->charXOffsets) {
        LONG pixX = (LONG)x - (LONG)inst->leftMargin + (LONG)inst->scrollLeftPx;
        if (pixX < 0) pixX = 0;
        *charOut = uted_x_to_char(line, (WORD)pixX);
    } else
        *charOut = 0;
}

/* -------------------------------------------------------------------------
 * DoInvalidateLine
 * ------------------------------------------------------------------------- */
void UniTextEditor_DoInvalidateLine(Class *cl, Object *o, ULONG lineIdx)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    (void)cl;

    if (lineIdx == UTED_ALLLINES) {
        UniTextEditorLine *line;
        for (line = (UniTextEditorLine *)inst->lines.mlh_Head;
             line->node.mln_Succ != NULL;
             line = (UniTextEditorLine *)line->node.mln_Succ)
        {
            uted_line_invalidate(line);
        }
    } else {
        UniTextEditorLine *line = uted_get_line(inst, lineIdx);
        if (line) uted_line_invalidate(line);
    }
}
