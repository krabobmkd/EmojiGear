/*
 * unitexteditor_private.h – internal data structures and function prototypes.
 * Do not include outside UniTextEditorGadget/.
 */

#ifndef UNITEXTEDITOR_PRIVATE_H
#define UNITEXTEDITOR_PRIVATE_H

/* =========================================================================
 * Raw Amiga key codes (ie_Code values from IECLASS_RAWKEY InputEvents).
 * Bit 7 (RAWKEY_UP_FLAG) is set on key-release events.
 * =========================================================================
 */
#define RAWKEY_UP_FLAG   0x80
#define RAWKEY_BACKSPACE 0x41
#define RAWKEY_TAB       0x42
#define RAWKEY_DELETE    0x46
#define RAWKEY_LEFT      0x4F
#define RAWKEY_RIGHT     0x4E
#define RAWKEY_UP        0x4C
#define RAWKEY_DOWN      0x4D
#define RAWKEY_PGUP      0x3F
#define RAWKEY_PGDN      0x1F
#define RAWKEY_F1        0x50
#define RAWKEY_F8        0x57

#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <string.h>

#include <gadgets/unitexteditor.h>
#include <graphics/layers.h>
    #include <libraries/utf8rastport.h>
#ifdef STATIC_UTF8RASTPORT
    #include "staticutf8rastport.h"
#else

    #include <proto/utf8rastport.h>
#endif
#include "compilers.h"

/* =========================================================================
 * UniTextEditorPos
 * =========================================================================
 */
typedef struct {
    ULONG line;     /* line index  [0 .. lineCount-1] */
    ULONG ch;       /* char index  [0 .. charCount]   (past-end ok) */
} UniTextEditorPos;

INLINE int uted_pos_cmp(const UniTextEditorPos *a, const UniTextEditorPos *b)
{
    if (a->line != b->line) return (a->line < b->line) ? -1 : 1;
    if (a->ch   != b->ch)   return (a->ch   < b->ch)   ? -1 : 1;
    return 0;
}

/* =========================================================================
 * UTEDBitMapPool – fixed pool of pre-allocated offscreen bitmaps
 *
 * All chunk bitmaps have identical dimensions (LINE_CHUNK_WIDTH × lineHeight)
 * and are fully interchangeable.  Lines borrow entries from the pool when a
 * chunk comes into view and return them when evicted.  This keeps chip RAM
 * usage bounded regardless of document length.
 *
 * Pool size = (visibleLines + 2*POOL_LINE_BUFFER) * chunksPerGadWidth
 * =========================================================================
 */
#define LINE_CHUNK_WIDTH   256   /* pixels per chunk */
#define POOL_LINE_BUFFER     4   /* extra lines above/below viewport kept warm */
#define POOL_H_CHUNK_BUFFER  2   /* extra chunks left/right of viewport kept warm */

typedef struct {
    struct BitMap    **bitmaps;    /* AllocVec'd array of N BitMap pointers */
    struct BitMap   **freeStack;   /* AllocVec'd stack of free bitmap pointers */
    struct Layer_Info *layerInfo;  /* single shared LayerInfo for tile rendering */
    struct Layer      *layer;      /* single shared Layer (LINE_CHUNK_WIDTH x height) */
    struct RastPort   *rp;         /* = layer->rp, used by uted_line_render_chunk */
    ULONG              size;       /* total bitmaps allocated */
    ULONG              freeCount;  /* bitmaps currently available */
    UWORD              height;     /* lineHeight the bitmaps were allocated for */
} UTEDBitMapPool;

/* =========================================================================
 * UTEDUndoEntry – one record in the undo/redo history
 * =========================================================================
 *
 * opType  UTED_UNDO_INSERT : text was inserted  → undo = delete it
 * opType  UTED_UNDO_DELETE : text was deleted   → undo = re-insert it
 *
 * posStart/posEnd denote the affected buffer range (in character indices).
 * For INSERT, [posStart..posEnd] is the inserted range (posEnd = cursor after).
 * For DELETE, posStart is where the deleted text sat; posEnd is the position
 *   one-past the last deleted char (for redo via DoDeleteSelection).
 *
 * text / textBytes hold the inserted or deleted UTF-8 bytes (AllocVec'd).
 * The ring buffer / redo stack own this pointer.
 *
 * cursorBefore / anchorBefore / hasSelBefore are the full cursor + selection
 * state BEFORE the operation; restored by undo so the next redo from the
 * undo stack sees the right pre-op cursor.
 *
 * atomic = TRUE prevents compression with the adjacent entry on the stack.
 * Set for: pastes (multi-char/multi-line), selection deletes, line joins.
 */
#define UTED_UNDO_INSERT  0
#define UTED_UNDO_DELETE  1

typedef struct {
    UBYTE            opType;        /* UTED_UNDO_INSERT or UTED_UNDO_DELETE */
    BYTE             delDir;        /* DELETE only: -1=backspace +1=fwddel 0=selection */
    BOOL             atomic;        /* TRUE = no compression with adjacent entry */
    UniTextEditorPos posStart;
    UniTextEditorPos posEnd;
    char            *text;          /* AllocVec'd UTF-8; owned by stack */
    ULONG            textBytes;     /* byte length (not including NUL) */
    /* Pre-operation cursor/selection state */
    UniTextEditorPos cursorBefore;
    UniTextEditorPos anchorBefore;
    BOOL             hasSelBefore;
} UTEDUndoEntry;

/* =========================================================================
 * UTEDWrapRow – one visual (rendered) row in word-wrap mode
 *
 * In word-wrap mode the render engine splits long logical lines into
 * multiple visual rows.  This struct maps one visual row back to its
 * position in the logical line list.  The wrap map is a flat array of
 * these entries, indexed by visual row number.
 *
 * When wordWrap is FALSE this array is not used; the render engine
 * treats logical line index == visual row index directly.
 * =========================================================================
 */
typedef struct {
    ULONG logicalLine;  /* index of the \n-separated UniTextEditorLine */
    ULONG startChar;    /* first char of this visual row within that line */
    ULONG endChar;      /* one-past-last char (exclusive) on this visual row */
    ULONG startPixel;   /* charXOffsets[startChar], cached for render speed  */
} UTEDWrapRow;

/* =========================================================================
 * UTEDAnsiRun – one styled segment of a line (ANSI escape mode)
 *
 * A line is split into runs by uted_line_build_ansi_runs().  Each run covers
 * a contiguous range of visible (non-escape-sequence) bytes.  The gaps
 * between runs' [startChar..endChar) ranges are escape-sequence chars that
 * are editable but zero-width in the rendered output.
 * =========================================================================
 */
typedef struct {
    ULONG startByte;    /* byte offset of first visible char in this run */
    ULONG endByte;      /* exclusive byte offset (past last visible char) */
    ULONG startChar;    /* codepoint index of startByte */
    ULONG endChar;      /* exclusive codepoint index */
    LONG  fgPen;        /* -1 = use inst->txtPen */
    LONG  bgPen;        /* -1 = use inst->bgPen  */
    UBYTE style;        /* URP_STYLE_* bits */
    UBYTE underline;
    UBYTE reverseVideo;
    UBYTE _pad;
} UTEDAnsiRun;

/* =========================================================================
 * UniTextEditorLine – one logical line
 * =========================================================================
 */
typedef struct UniTextEditorLine {
    struct MinNode  node;

    /* Text content */
    char           *utf8;           /* NUL-terminated UTF-8, AllocVec'd */
    ULONG           byteAlloc;
    ULONG           byteUsed;       /* used bytes NOT counting NUL */
    ULONG           charCount;      /* UTF-8 codepoint count */

    /* Per-character x offsets, AllocVec'd array of (charCount+1) UWORDs.
     * charXOffsets[i] = x-pixel where char i starts.
     * charXOffsets[charCount] = total advance width.
     * NULL = stale, rebuild before use. */
    LONG          *charXOffsets;

    /* Chunked bitmap cache.
     *
     * chunks[i] is a pointer borrowed from the gadget's UTEDBitMapPool, or
     * NULL if that chunk is not currently held.  The pointer array itself is
     * AllocVec'd on first use and grown on demand; it is never shrunk.
     *
     * Lifecycle:
     *   borrow  – pool_take(), stored in chunks[i]
     *   evict   – pool_return(), chunks[i] = NULL  (no FreeBitMap)
     *   free    – uted_line_free_cache() returns all borrowed entries then
     *             frees the pointer array
     */
    struct BitMap   **chunks;    /* dynamically allocated pointer array */
    ULONG            chunkAlloc; /* number of entries currently in chunks[] */
    WORD             chunkHeight;/* lineHeight used when bitmaps were borrowed */

    BOOL             dirty;      /* TRUE = text changed, re-render on next paint */

    /* ANSI escape run list – built lazily by uted_line_build_ansi_runs().
     * NULL when applyAnsiEscapes is FALSE or before first build. */
    UTEDAnsiRun     *ansiRuns;
    ULONG            ansiRunCount;
    ULONG            ansiRunAlloc;
} UniTextEditorLine;

/* =========================================================================
 * UTEDTextStash – one saved document context
 *
 * Invariant for the line list held inside a stash:
 *   firstLine->node.mln_Pred == NULL
 *   lastLine->node.mln_Succ  == NULL
 * All intermediate nodes have valid mln_Pred/mln_Succ pointing to adjacent
 * UniTextEditorLine nodes.  This allows O(1) restore via two sentinel fixups.
 * All bitmap chunks are evicted (chunks[i]==NULL) before stashing, so the
 * bitmap pool stays exclusively with the live gadget instance.
 * =========================================================================
 */
#define UTED_CONTEXT_NAME_MAX  256

typedef struct UTEDTextStash {
    struct MinNode     node;                           /* for inst->stashList */
    char               contextName[UTED_CONTEXT_NAME_MAX];

    /* Line list (sentinel-free chain, see invariant above) */
    UniTextEditorLine *firstLine;
    UniTextEditorLine *lastLine;
    ULONG              lineCount;

    /* Undo/redo – AllocVec'd arrays moved wholesale; entries' text ptrs intact */
    UTEDUndoEntry     *undoStack;
    UTEDUndoEntry     *redoStack;
    ULONG              undoMax;
    ULONG              undoCount;
    ULONG              undoHead;
    ULONG              redoCount;

    /* Wrap map */
    UTEDWrapRow       *wrapMap;
    ULONG              wrapRowCount;
    ULONG              wrapMapAlloc;
    BOOL               wrapMapDirty;

    /* Cursor / scroll / misc state */
    UniTextEditorPos   cursor;
    UniTextEditorPos   selAnchor;
    UniTextEditorPos   selFloat;
    BOOL               hasSelection;
    ULONG              scrollTopLine;
    ULONG              scrollLeftPx;
    BOOL               modified;
    BOOL               wordWrap;
} UTEDTextStash;

/* =========================================================================
 * UniTextEditorData – per-instance data (INST_DATA block)
 * =========================================================================
 */
typedef struct UniTextEditorData {

    /* Per-instance semaphores.
     * Always acquire in order: textSem first, then cacheSem.
     * textSem  – protects line list, text content, undo stack, cursor/selection.
     * cacheSem – protects bmPool and per-line chunks[] arrays. */
    struct SignalSemaphore textSem;
    struct SignalSemaphore cacheSem;

    /* Font & draw context — dc lives for the gadget's whole lifetime */
    struct URPDrawContext *dc;
    struct Task         *callerTask;
    ULONG                 pointSize;   /* stored for UTED_AddFont calls */
    ULONG                 fontFlags;   /* stored for UTED_AddFont calls */
    ULONG                 tabSpaces;   /* forwarded to URPDCA_TabSpaces; default 4 */
    BOOL                  tabsAreSpaces;     /* UTED_TabsAreSpaces: Tab inserts spaces; default FALSE */
    BOOL                  applyAnsiEscapes;  /* UTED_ApplyAnsiEscapes: interpret ANSI escapes; default FALSE */
    BOOL                  ansiUnixColors;    /* UTED_AnsiUnixColors: xterm palette via FindColor(); default FALSE */

    /* allow clipping draw commands */
    struct Region *clipRegion;
    Object  *bevel;
    ULONG   redrawBevel;

    /* Colors */
    ULONG                 txtPen;
    ULONG                 bgPen;
    /* Selection is rendered with COMPLEMENT mode – no pen needed */

    /* Text buffer: doubly-linked MinList, always >= 1 node */
    struct MinList        lines;
    ULONG                 lineCount;

    /* Cursor & selection.
     * selAnchor = fixed end, selFloat = cursor (moving end).
     * When hasSelection==FALSE, selAnchor == cursor. */
    UniTextEditorPos      cursor;
    UniTextEditorPos      selAnchor;
    UniTextEditorPos      selFloat;
    BOOL                  hasSelection;

    /* Mouse drag state */
    BOOL                  dragging;

    /* Directional selection anchor, set on each plain (non-extend) click.
     * clickAnchorChFloor = lo from uted_x_to_char_floor at click time:
     * the index of the char cell the click pixel fell inside.
     * clickAnchorCh      = midpoint cursor result at click time.
     * On drag we choose anchor = floor (right drag) or floor+1 (left drag)
     * so the originally clicked character is always inside the selection. */
    BOOL                  clickAnchorValid;
    ULONG                 clickAnchorLine;
    ULONG                 clickAnchorChFloor;
    ULONG                 clickAnchorCh;

    /* Scroll */
    ULONG                 scrollTopLine;
    ULONG                 scrollLeftPx;   /* horizontal scroll offset in pixels */

    /* Layout — 14 WORDs = 28 bytes, keeps bmPool 4-byte aligned after this block */
    WORD                  lineHeight;     /* lineHeightBase + lineSpacing */
    WORD                  lineHeightBase; /* raw font metric height (ascender + descender) */
    WORD                  lineAscent;     /* pixels from line top to baseline */
    WORD                  lineSpacing;    /* UTED_LineSpacing: extra px below glyphs; default 1 */
    WORD                  gadWidth;
    WORD                  gadHeight;
    WORD                  visibleLines;   /* floor((gadHeight - topMargin - bottomMargin) / lineHeight) */
    WORD                  layoutedWidth;  /* gadWidth at last uted_do_layout call */
    WORD                  layoutedHeight; /* gadHeight at last uted_do_layout call */
    WORD                  leftMargin;     /* UTED_LeftMargin: px gap left of glyphs; default 2 */
    WORD                  rightMargin;    /* UTED_RightMargin: px gap right of glyphs; default 0 */
    WORD                  topMargin;      /* UTED_TopMargin: px gap above first line; default 0 */
    WORD                  bottomMargin;   /* UTED_BottomMargin: px gap below last line; default 0 */
    WORD                  _alignPad0;     /* explicit alignment pad */

    /* Bitmap pool – bounded chip RAM for line chunk bitmaps */
    UTEDBitMapPool        bmPool;
    ULONG                 lastEvictTopLine;   /* scrollTopLine at last eviction sweep */
    ULONG                 lastEvictLeftChunk; /* first visible chunk at last eviction */

    /* Undo ring buffer (bounded FIFO) + redo stack (simple LIFO).
     * Both allocated together by uted_undo_resize().
     * undoStack capacity = redoStack capacity = undoMax.
     * Ring: undoHead = next-write index; oldest = (undoHead-undoCount+undoMax)%undoMax.
     * Redo: flat array, valid indices [0..redoCount-1], top at redoCount-1.
     * undoInProgress suppresses new recording while executing undo/redo ops. */
    UTEDUndoEntry        *undoStack;
    UTEDUndoEntry        *redoStack;
    ULONG                 undoMax;        /* capacity (0 = undo disabled) */
    ULONG                 undoCount;      /* entries currently on undo stack */
    ULONG                 undoHead;       /* ring write position */
    ULONG                 redoCount;      /* entries currently on redo stack */
    BOOL                  undoInProgress; /* TRUE while executing undo/redo */

    /* Word-wrap map
     * Built lazily at the start of GM_RENDER when wrapMapDirty=TRUE.
     * NULL / wrapRowCount==0 when wordWrap is FALSE or before first build. */
    BOOL         wordWrap;
    BOOL         wrapMapDirty;  /* TRUE = rebuild needed before next render */
    UTEDWrapRow *wrapMap;       /* AllocVec'd array [0..wrapRowCount-1]     */
    ULONG        wrapRowCount;  /* total visual rows (== lineCount if !wordWrap) */
    ULONG        wrapMapAlloc;  /* allocated entry count in wrapMap          */

    /* Misc */
    BOOL                  modified;
    BOOL                  gadgetActive;
    BOOL                  isCLUT;         /* TRUE when target screen depth <= 8 */
    BOOL                  readOnly;       /* GA_ReadOnly: no edit/select/cursor; default FALSE */
    BOOL                  noLineFeed;     /* UTED_NoLineFeed: strip \n/\r; Enter → notify */
    ULONG                 maxDisplayLines;/* UTED_MaxDisplayLines: cap gadget height; default ~0UL */
    ULONG                 vanillaAnsiCode; /* encoding for UTED_PutVanillaKey 0xA0-0xFF bytes;
                                           * UTED_VANILLAKEY_LATIN1=1 or UTED_VANILLAKEY_LATIN2=2 */
    struct Screen        *screen;         /* for URPDC_UpdateColorMap + AllocBitMap */

    LONG   refreshStartLine;
    LONG   refreshEndLine;

    /* Index set by UTED_LineTextToGet; used by UTED_LineUTF8TextBuffer get */
    ULONG  lineIndexToGet;

    /* Last successful search result – consumed by UTED_ReplaceString */
    ULONG  lastSearchLine;
    ULONG  lastSearchChar;      /* match start codepoint index (inclusive) */
    ULONG  lastSearchEndChar;   /* match end codepoint index (exclusive)   */
    BOOL   lastSearchValid;     /* TRUE until replaced or a new text is set */
    BOOL   searchCaseSensitive;    /* UTED_SearchCaseSensitive; default TRUE   */
    BOOL   visibleTabs;            /* UTED_VisibleTabs: draw tab marker at baseline; default FALSE */
    BOOL   displayInternalVScroll; /* UTED_DisplayInternalVScroll; default TRUE */
    ULONG  halfwayPen;          /* pen nearest to midpoint between txtPen and bgPen */

    /* Deferred mouse input from input.device context.
     * input.device must not call FreeType/disk APIs (uted_line_build_metrics etc).
     * OnGoActive / OnHandleInput store raw mouse coords here; OnRender replays
     * them via DoHitTest + DoSetCursorPos, which is safe in the app task context.
     *
     * hasClick and hasDrag are independent flags: both may be TRUE when several
     * input events arrive before the next GM_RENDER fires.  The click must be
     * replayed first so it sets the selection anchor (extend=FALSE) before the
     * drag extends from it. */
    BOOL  pendingClick;
    WORD  pendingClickX, pendingClickY;   /* even offset after BOOL(4) */
    ULONG pendingClickQualifiers;
    BOOL  pendingDrag;
    WORD  pendingDragX, pendingDragY;     /* even offset after BOOL(4) */
    UBYTE pendingDragType;                /* UTED_MPEND_DRAG or UTED_MPEND_RELEASE */
    UBYTE _pad0;                          /* explicit alignment pad */

    /* Internal scrollbar drag state.
     * Set in input.device context (OnGoActive/OnHandleInput); replayed in
     * the safe application-task context inside GM_RENDER, exactly like the
     * pendingClick / pendingDrag pair for text interaction.
     * vScrollDragging guards drag-move events so they go to the scrollbar
     * rather than the text selection code path. */
    BOOL  vScrollDragging;                /* TRUE = current drag is on the internal vscrollbar */
    BOOL  pendingVScrollClick;            /* scrollbar click pending replay in GM_RENDER */
    BOOL  pendingVScrollDrag;             /* scrollbar drag/release pending replay in GM_RENDER */
    WORD  pendingVScrollY;                /* gadget-relative Y of the pending scroll event */
    WORD  _padVS;

    /* Named context stash list */
    struct MinList        stashList;                        /* list of UTEDTextStash nodes */
    char                  currentContextName[UTED_CONTEXT_NAME_MAX]; /* "" = none set */

    /* Multi-click state: counts consecutive clicks at the same spot.
     * 1=single, 2=double (select word), 3=triple (select line).
     * lastClick* store the previous click's timestamp and gadget position. */
    UBYTE pendingClickCount;
    UBYTE _pad1;
    ULONG lastClickSec;
    ULONG lastClickMicro;
    WORD  lastClickX;
    WORD  lastClickY;

    /* OS3.9 intuition can't manage OM_NOTIFY with DoSuperMethod... */
    Object *target;
    ULONG  ga_id;

    ULONG useInternalRawKey;
    BOOL  rawKeySendBack;   /* UTED_InternalRawKey_SendBack: notify code after each key in UKM_Internal */
    ULONG rawKeyLastCode;   /* UTED_InternalRawKey_Code: last notified (qualifier<<16)|keycode */

    /* Deferred keyboard input from input.device context.
     * GM_HANDLEINPUT copies raw InputEvents here; GM_RENDER drains them via
     * uted_manageFullRawKey(), which is safe in the application task context. */
#define UTED_PENDING_KEY_MAX 8
    struct InputEvent pendingKeys[UTED_PENDING_KEY_MAX];
    UBYTE             pendingKeyCount;
    UBYTE             _padKey[3];

} UniTextEditorData;

/* pendingDragType values */
#define UTED_MPEND_DRAG    1
#define UTED_MPEND_RELEASE 2

/* Convenience */
#define UTED_DATA(cl, o)  ((UniTextEditorData *)INST_DATA((cl), (o)))

/* =========================================================================
 * Prototypes
 * =========================================================================
 */

/* class_texteditor_lib.c */
/* UniTextEditor_Init / UniTextEditor_Exit declared in unitexteditor.h */

/* class_texteditor_dispatch.c */
ULONG ASM SAVEDS UniTextEditor_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* class_texteditor_attribs.c */
ULONG UniTextEditor_OnNew    (Class *cl, Object *o, struct opSet *msg);
ULONG UniTextEditor_OnDispose(Class *cl, Object *o, Msg msg);
ULONG UniTextEditor_OnSet    (Class *cl, Object *o, struct opSet *msg);
ULONG UniTextEditor_OnGet    (Class *cl, Object *o, struct opGet *msg);

/* class_texteditor_render.c */
ULONG UniTextEditor_OnLayout (Class *cl, Object *o, struct gpLayout *msg);
ULONG UniTextEditor_OnRender (Class *cl, Object *o, struct gpRender *msg);
ULONG UniTextEditor_OnDomain (Class *cl, Object *o, struct gpDomain *msg);

/* class_texteditor_handleinput.c */
ULONG UniTextEditor_OnGoActive    (Class *cl, Object *o, struct gpInput *msg);
ULONG UniTextEditor_OnHandleInput (Class *cl, Object *o, struct gpInput *msg);
ULONG UniTextEditor_OnGoInactive  (Class *cl, Object *o, struct gpGoInactive *msg);

/* class_texteditor_text.c – buffer primitives */
UniTextEditorLine *uted_line_alloc      (const char *utf8, ULONG byteLen);
void               uted_line_free       (UniTextEditorData *inst, UniTextEditorLine *line);
void               uted_line_invalidate (UniTextEditorLine *line);
BOOL               uted_line_insert_bytes(UniTextEditorLine *line, ULONG byteOffset,
                                          const char *utf8, ULONG byteLen);
BOOL               uted_line_delete_bytes(UniTextEditorLine *line,
                                          ULONG byteOffset, ULONG byteLen);
UniTextEditorLine *uted_line_split      (UniTextEditorLine *line, ULONG charIndex);
BOOL               uted_line_join       (UniTextEditorData *inst,
                                         UniTextEditorLine *head, UniTextEditorLine *tail);
ULONG              uted_char_to_byte    (const char *utf8, ULONG byteUsed, ULONG charIndex);
ULONG              uted_count_chars     (const char *utf8, ULONG byteLen);
UniTextEditorLine *uted_get_line        (UniTextEditorData *inst, ULONG index);
void               uted_ensure_cursor_visible  (UniTextEditorData *inst);
void               uted_ensure_cursor_h_visible(UniTextEditorData *inst);
void               uted_invalidate_all_line_caches(UniTextEditorData *inst);

/* Action handlers (also in class_texteditor_text.c) */
ULONG UniTextEditor_DoInsertText     (Class *cl, Object *o, const char *text, LONG length);
ULONG UniTextEditor_DoDeleteChar     (Class *cl, Object *o, LONG delta);
ULONG UniTextEditor_DoMoveCursor     (Class *cl, Object *o, LONG deltaChar, LONG deltaLine, BOOL extend);
ULONG UniTextEditor_DoSetCursorPos   (Class *cl, Object *o, ULONG line, ULONG ch, BOOL extend);
void  UniTextEditor_DoClearSelection (Class *cl, Object *o);
void  UniTextEditor_DoSelectAll      (Class *cl, Object *o);
ULONG UniTextEditor_DoDeleteSelection(Class *cl, Object *o);
void  UniTextEditor_DoScrollTo       (Class *cl, Object *o, ULONG lineIdx, BOOL center);
ULONG UniTextEditor_DoDeleteToLineStart(Class *cl, Object *o);
ULONG UniTextEditor_DoDeleteToLineEnd  (Class *cl, Object *o);
ULONG UniTextEditor_DoGetSelectedText(Class *cl, Object *o, STRPTR *result);
void  UniTextEditor_DoHitTest        (Class *cl, Object *o, WORD x, WORD y, ULONG *lineOut, ULONG *charOut);
void  UniTextEditor_DoInvalidateLine (Class *cl, Object *o, ULONG lineIdx);

/* Clipboard actions (class_unitexteditor_clipboard.c) */
void uted_do_clipboard_copy (Class *cl, Object *o);
BOOL uted_do_clipboard_cut  (Class *cl, Object *o);
BOOL uted_do_clipboard_paste(Class *cl, Object *o);

/* Notify ICA_TARGET with an attribute change */
void uted_notify(Class *cl, Object *o, struct GadgetInfo *gi, ULONG tag, ULONG value);

/* Recompute halfwayPen from the current txtPen, bgPen, and screen colormap */
void uted_update_halfway_pen(UniTextEditorData *inst);

/* Trigger GM_RENDER on self using ObtainGIRPort */
void uted_render_self(Class *cl, Object *o, struct GadgetInfo *gi);

/* class_unitexteditor_undo.c */
void  uted_undo_flush  (UniTextEditorData *inst);
void  uted_undo_resize (UniTextEditorData *inst, ULONG newMax);
void  uted_undo_push   (UniTextEditorData *inst, UTEDUndoEntry *entry);
void  uted_undo_notify (Class *cl, Object *o, struct GadgetInfo *gi);
ULONG UniTextEditor_DoUndo (Class *cl, Object *o);
ULONG UniTextEditor_DoRedo (Class *cl, Object *o);

/* class_texteditor_lines.c */
void  uted_line_build_metrics  (UniTextEditorLine *line, UniTextEditorData *inst);
void  uted_line_build_ansi_runs (UniTextEditorLine *line, UniTextEditorData *inst);
void  uted_line_ensure_ansi_runs(UniTextEditorLine *line, UniTextEditorData *inst);
BOOL  uted_ansi_find_seq       (UniTextEditorLine *line, ULONG charIdx,
                                 ULONG *seqStart, ULONG *seqEnd);
void  uted_line_render_chunk   (UniTextEditorData *inst, UniTextEditorLine *line,
                                 ULONG chunkIdx);
void  uted_line_free_cache     (UniTextEditorData *inst, UniTextEditorLine *line);
void  uted_line_evict_chunks         (UniTextEditorData *inst, UniTextEditorLine *line);
void  uted_line_evict_distant_chunks (UniTextEditorData *inst, UniTextEditorLine *line,
                                      ULONG keepFirst, ULONG keepLast);
ULONG uted_x_to_char           (const UniTextEditorLine *line, WORD pixelX);
ULONG uted_x_to_char_floor     (const UniTextEditorLine *line, WORD pixelX);
void  uted_free_wrap_map       (UniTextEditorData *inst);
void  uted_rebuild_wrap_map    (UniTextEditorData *inst);
ULONG uted_cursor_visual_row   (UniTextEditorData *inst);
BOOL  uted_pool_alloc          (UTEDBitMapPool *pool, ULONG size,
                                 UWORD lineHeight, struct Screen *screen);
BOOL  uted_pool_growalloc      (UTEDBitMapPool *pool, ULONG newSize,
                                 struct Screen *screen);
void  uted_pool_free_bitmapcache           (UTEDBitMapPool *pool);
void  uted_pool_free_layer           (UTEDBitMapPool *pool);
BOOL uted_pool_create_layer(UTEDBitMapPool *pool,int w,int h , struct BitMap *bitmap);

int uted_manage_vanilla_keycode(Class *cl, Object *o,ULONG codedata, struct GadgetInfo *gi);
int uted_manage_rawkey_keycode(Class *cl, Object *o,ULONG codedata, struct GadgetInfo *gi);
int uted_manageFullRawKey(Class *cl, Object *o,
            struct InputEvent *ie,
            struct GadgetInfo *gi
             );
#endif /* UNITEXTEDITOR_PRIVATE_H */
