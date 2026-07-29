/*
 * class_unitexteditor_undo.c – undo/redo ring buffer + execution.
 *
 * Design:
 *   - Undo stack: fixed-capacity ring buffer (FIFO eviction of oldest).
 *   - Redo stack: flat array (LIFO), same capacity, cleared on new edit.
 *   - Compression: consecutive single-char inserts / backspaces / fwd-deletes
 *     on the same line merge into one entry so a single undo step rolls back
 *     the whole typing run.
 *   - Atomic entries (paste, selection-delete, line-join) never compress.
 *   - undoInProgress flag prevents recording while executing undo/redo ops.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>
#include "unitexteditor_private.h"

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

static void undo_ring_put(UniTextEditorData *inst, UTEDUndoEntry *entry);

static void entry_free_text(UTEDUndoEntry *e)
{
    if (e->text) { FreeVec(e->text); e->text = NULL; e->textBytes = 0; }
}

static void undo_ring_flush(UniTextEditorData *inst)
{
    ULONG i;
    if (inst->undoStack) {
        for (i = 0; i < inst->undoMax; i++)
            entry_free_text(&inst->undoStack[i]);
    }
    inst->undoCount = 0;
    inst->undoHead  = 0;
}

static void redo_stack_flush(UniTextEditorData *inst)
{
    ULONG i;
    if (inst->redoStack) {
        for (i = 0; i < inst->redoCount; i++)
            entry_free_text(&inst->redoStack[i]);
    }
    inst->redoCount = 0;
}

/* Return pointer to most-recent undo entry (top of ring), or NULL if empty. */
static UTEDUndoEntry *undo_top(UniTextEditorData *inst)
{
    if (!inst->undoStack || inst->undoCount == 0) return NULL;
    return &inst->undoStack[(inst->undoHead + inst->undoMax - 1) % inst->undoMax];
}

/* =========================================================================
 * Public stack management
 * =========================================================================
 */

/* Flush both undo and redo stacks without releasing their backing memory. */
void uted_undo_flush(UniTextEditorData *inst)
{
    undo_ring_flush(inst);
    redo_stack_flush(inst);
}

/* Resize (and flush) both stacks.  Pass 0 to free and disable undo. */
void uted_undo_resize(UniTextEditorData *inst, ULONG newMax)
{
    /* Flush before freeing so text AllocVecs are released first */
    uted_undo_flush(inst);

    if (inst->undoStack) { FreeVec(inst->undoStack); inst->undoStack = NULL; }
    if (inst->redoStack) { FreeVec(inst->redoStack); inst->redoStack = NULL; }
    inst->undoMax  = 0;
    inst->undoCount = 0;
    inst->undoHead  = 0;
    inst->redoCount = 0;

    if (newMax == 0) return;

    inst->undoStack = (UTEDUndoEntry *)AllocVec(
        newMax * sizeof(UTEDUndoEntry), MEMF_ANY | MEMF_CLEAR);
    inst->redoStack = (UTEDUndoEntry *)AllocVec(
        newMax * sizeof(UTEDUndoEntry), MEMF_ANY | MEMF_CLEAR);

    if (!inst->undoStack || !inst->redoStack) {
        if (inst->undoStack) { FreeVec(inst->undoStack); inst->undoStack = NULL; }
        if (inst->redoStack) { FreeVec(inst->redoStack); inst->redoStack = NULL; }
        return;
    }
    inst->undoMax = newMax;
}

/* =========================================================================
 * uted_undo_push
 *
 * Push entry onto undo ring.  Ownership of entry->text is transferred.
 * Flushes redo stack (new edit invalidates redo history).
 * Tries to compress with the current top entry before pushing.
 * If ring is full, evicts the oldest entry (with its text).
 * =========================================================================
 */
void uted_undo_push(UniTextEditorData *inst, UTEDUndoEntry *entry, BOOL groupWithPrev)
{
    UTEDUndoEntry *top;
    char          *newText;

    entry->groupWithPrev = groupWithPrev;

    if (!inst->undoStack || inst->undoMax == 0) {
        entry_free_text(entry);
        return;
    }

    /* Permanent identity for UTED_IsModified (see unitexteditor_private.h's
     * UTEDUndoEntry.seq doc comment). Assigned unconditionally here, even
     * though a compression merge below may discard it unused - a skipped
     * counter value is harmless, uniqueness is all that matters. */
    entry->seq = ++inst->undoSeqCounter;

    /* Every new edit clears redo.
     * NOTE: DoRedo bypasses this function for its ring-push (see undo_ring_put)
     * so that remaining redo entries are preserved. */
    redo_stack_flush(inst);

    /* -----------------------------------------------------------------------
     * Compression: try to merge single-char non-atomic edits of the same
     * type on the same line with the current top entry.
     *
     * Excludes the entry currently marking the UTED_IsModified "clean" point
     * (top->seq == inst->savedSeq): merging into it in place would silently
     * grow the saved entry's content without changing its identity, so a
     * single keystroke right after a save could go undetected as a
     * modification. Forcing a fresh entry there instead keeps every edit
     * after that point on a distinct, comparable seq.
     * ----------------------------------------------------------------------- */
    if (!entry->atomic && entry->text && entry->textBytes > 0 && inst->undoCount > 0) {
        top = undo_top(inst);

        if (top && !top->atomic && top->opType == entry->opType
            && top->posStart.line == entry->posStart.line
            && top->seq != inst->savedSeq)
        {
            BOOL merged = FALSE;

            if (entry->opType == UTED_UNDO_INSERT
                && entry->posStart.col == top->posEnd.col
                && entry->posStart.line == top->posEnd.line)
            {
                /* Consecutive insert: append new text to top */
                newText = (char *)AllocVec(
                    top->textBytes + entry->textBytes + 1, MEMF_ANY);
                if (newText) {
                    CopyMem(top->text,   newText,                  top->textBytes);
                    CopyMem(entry->text, newText + top->textBytes, entry->textBytes);
                    newText[top->textBytes + entry->textBytes] = '\0';
                    FreeVec(top->text);
                    top->text       = newText;
                    top->textBytes += entry->textBytes;
                    top->posEnd     = entry->posEnd;
                    merged = TRUE;
                }
            }
            else if (entry->opType == UTED_UNDO_DELETE && entry->delDir == -1
                     && entry->posEnd.col   == top->posStart.col
                     && entry->posEnd.line == top->posStart.line)
            {
                /* Consecutive backspace: prepend new text to top */
                newText = (char *)AllocVec(
                    top->textBytes + entry->textBytes + 1, MEMF_ANY);
                if (newText) {
                    CopyMem(entry->text, newText,                   entry->textBytes);
                    CopyMem(top->text,   newText + entry->textBytes, top->textBytes);
                    newText[top->textBytes + entry->textBytes] = '\0';
                    FreeVec(top->text);
                    top->text       = newText;
                    top->textBytes += entry->textBytes;
                    top->posStart   = entry->posStart;
                    merged = TRUE;
                }
            }
            else if (entry->opType == UTED_UNDO_DELETE && entry->delDir == 1
                     && entry->posStart.col   == top->posStart.col
                     && entry->posStart.line == top->posStart.line)
            {
                /* Consecutive fwd-delete: append new text to top */
                newText = (char *)AllocVec(
                    top->textBytes + entry->textBytes + 1, MEMF_ANY);
                if (newText) {
                    CopyMem(top->text,   newText,                  top->textBytes);
                    CopyMem(entry->text, newText + top->textBytes, entry->textBytes);
                    newText[top->textBytes + entry->textBytes] = '\0';
                    FreeVec(top->text);
                    top->text       = newText;
                    top->textBytes += entry->textBytes;
                    top->posEnd     = entry->posEnd;
                    merged = TRUE;
                }
            }

            if (merged) {
                entry_free_text(entry);
                return;
            }
        }
    }

    /* -----------------------------------------------------------------------
     * Push to ring.  If full, evict oldest (= slot at undoHead since full
     * means undoHead wrapped around to the oldest slot).
     * ----------------------------------------------------------------------- */
    undo_ring_put(inst, entry);
}

/* Write one entry directly into the undo ring – no redo flush, no compression.
 * Used by DoRedo so that sibling redo entries are not accidentally wiped. */
static void undo_ring_put(UniTextEditorData *inst, UTEDUndoEntry *entry)
{
    if (inst->undoCount == inst->undoMax) {
        entry_free_text(&inst->undoStack[inst->undoHead]);
        /* undoCount stays at undoMax; we overwrite oldest */
    } else {
        inst->undoCount++;
    }

    inst->undoStack[inst->undoHead] = *entry;
    inst->undoHead = (inst->undoHead + 1) % inst->undoMax;
    /* entry->text ownership is now in the ring; do not free externally */
}

/* =========================================================================
 * uted_undo_notify
 * =========================================================================
 */
void uted_undo_notify(Class *cl, Object *o, struct GadgetInfo *gi)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    uted_notify(cl, o, gi, UTEDN_UndoAvailable,
                (inst->undoCount > 0) ? TRUE : FALSE);
    uted_notify(cl, o, gi, UTEDN_RedoAvailable,
                (inst->redoCount > 0) ? TRUE : FALSE);
}

/* =========================================================================
 * DoUndo
 *
 * Consumes one whole group per call (see UTEDUndoEntry.groupWithPrev):
 * a multi-line block indent/unindent pushes one entry per affected line,
 * but must undo/redo as a single user-visible step. Each iteration below is
 * exactly the single-entry logic this function used to have; the loop just
 * keeps going while the entry just popped says it was glued to the one
 * before it, stopping once it pops a group leader (groupWithPrev==FALSE) or
 * the stack runs dry (e.g. the rest of the group was evicted long ago).
 * =========================================================================
 */
ULONG UniTextEditor_DoUndo(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UTEDUndoEntry      e;
    BOOL               continueGroup;
    ULONG              didAny = 0;

    do {
        if (!inst->undoStack || inst->undoCount == 0) break;

        /* Pop top entry from ring */
        inst->undoHead = (inst->undoHead + inst->undoMax - 1) % inst->undoMax;
        e = inst->undoStack[inst->undoHead];
        inst->undoStack[inst->undoHead].text = NULL; /* ownership in local e */
        inst->undoCount--;

        continueGroup = e.groupWithPrev;

        /* Execute inverse operation with recording suppressed */
        inst->undoInProgress = TRUE;

        if (e.opType == UTED_UNDO_INSERT) {
            /* Undo an insert: select and delete the inserted range */
            if (uted_pos_cmp(&e.posStart, &e.posEnd) != 0) {
                inst->cursor       = e.posStart;
                inst->selAnchor    = e.posStart;
                inst->selFloat     = e.posEnd;
                inst->hasSelection = TRUE;
                UniTextEditor_DoDeleteSelection(cl, o);
            }
        } else {
            /* Undo a delete: re-insert the captured text at posStart */
            if (e.text && e.textBytes > 0) {
                inst->cursor       = e.posStart;
                inst->selAnchor    = e.posStart;
                inst->selFloat     = e.posStart;
                inst->hasSelection = FALSE;
                UniTextEditor_DoInsertText(cl, o, e.text, (LONG)e.textBytes);
            }
        }

        inst->undoInProgress = FALSE;

        /* Restore full cursor/selection state to pre-edit snapshot. Only
         * the last iteration's restore survives, which is exactly right:
         * that's the group leader's pre-edit state, i.e. the state before
         * the whole block operation. */
        inst->cursor       = e.cursorBefore;
        inst->selAnchor    = e.anchorBefore;
        inst->selFloat     = e.cursorBefore;
        inst->hasSelection = e.hasSelBefore;

        /* Push entry to redo stack (same entry, text ownership transferred) */
        if (inst->redoStack && inst->redoCount < inst->undoMax) {
            inst->redoStack[inst->redoCount] = e;
            inst->redoCount++;
            e.text = NULL; /* redo stack now owns it */
        }
        if (e.text) { FreeVec(e.text); }

        didAny = 1;
    } while (continueGroup);

    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    return didAny;
}

/* =========================================================================
 * DoRedo
 *
 * Mirrors DoUndo's grouping: after redoing entry e, peek at what would be
 * redone *next* (the new top of the redo stack) - if that entry says it was
 * grouped with the one before it (i.e. with e, which we just redid),
 * continue the loop instead of returning, so the whole group is re-applied
 * as a single user-visible step.
 * =========================================================================
 */
ULONG UniTextEditor_DoRedo(Class *cl, Object *o)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UTEDUndoEntry      e;
    UniTextEditorPos   preCursor;
    UniTextEditorPos   preAnchor;
    BOOL               preHasSel;
    BOOL               continueGroup;
    ULONG              didAny = 0;

    do {
        if (!inst->redoStack || inst->redoCount == 0) break;

        /* Pop top from redo stack */
        inst->redoCount--;
        e = inst->redoStack[inst->redoCount];
        inst->redoStack[inst->redoCount].text = NULL;

        continueGroup = (inst->redoCount > 0) &&
                        inst->redoStack[inst->redoCount - 1].groupWithPrev;

        /* Save cursor state before redo (will become cursorBefore in new undo entry) */
        preCursor = inst->cursor;
        preAnchor = inst->selAnchor;
        preHasSel = inst->hasSelection;

        /* Re-execute the original operation with recording suppressed */
        inst->undoInProgress = TRUE;

        if (e.opType == UTED_UNDO_INSERT) {
            /* Redo an insert: re-insert text at posStart */
            if (e.text && e.textBytes > 0) {
                inst->cursor       = e.posStart;
                inst->selAnchor    = e.posStart;
                inst->selFloat     = e.posStart;
                inst->hasSelection = FALSE;
                UniTextEditor_DoInsertText(cl, o, e.text, (LONG)e.textBytes);
            }
        } else {
            /* Redo a delete: re-delete the range [posStart..posEnd] */
            if (uted_pos_cmp(&e.posStart, &e.posEnd) != 0) {
                inst->cursor       = e.posStart;
                inst->selAnchor    = e.posStart;
                inst->selFloat     = e.posEnd;
                inst->hasSelection = TRUE;
                UniTextEditor_DoDeleteSelection(cl, o);
            }
        }

        inst->undoInProgress = FALSE;

        /* Push back to undo stack with updated cursorBefore = state before redo.
         * Use undo_ring_put (not uted_undo_push) so the remaining redo entries
         * are NOT flushed. Mark atomic so this entry never compresses;
         * groupWithPrev is preserved as-is (whole-struct copy above). */
        e.cursorBefore = preCursor;
        e.anchorBefore = preAnchor;
        e.hasSelBefore = preHasSel;
        e.atomic       = TRUE;
        if (inst->undoStack && inst->undoMax > 0)
            undo_ring_put(inst, &e);
        else
            entry_free_text(&e);

        didAny = 1;
    } while (continueGroup);

    uted_ensure_cursor_visible(inst);
    uted_ensure_cursor_h_visible(inst);

    return didAny;
}

/* =========================================================================
 * uted_undo_current_seq - backs UTED_IsModified (see class_unitexteditor_attribs.c)
 * =========================================================================
 */
ULONG uted_undo_current_seq(UniTextEditorData *inst)
{
    UTEDUndoEntry *top = undo_top(inst);
    return top ? top->seq : 0;
}
