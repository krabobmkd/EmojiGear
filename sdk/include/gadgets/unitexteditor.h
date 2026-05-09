/*
 * unitexteditor.h – public API for the TextEditor BOOPSI gadget
 *
 * A scrollable, UTF-8 text editor gadget using utf8rastport for rendering.
 * Lines are pre-rendered to individual cached BitMaps and re-blitted on
 * scroll/refresh.  Per-character x-offsets enable pixel-accurate selection.
 * Selected text is highlighted with a configurable background color.
 *
 * Inherits from "gadgetclass".
 * Build with: link utf8rastport + freetype + libpng + zlib
 */

#ifndef UNITEXTEDITOR_H
#define UNITEXTEDITOR_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/classes.h>
#include <utility/tagitem.h>

/* =========================================================================
 * Attribute tags   (base TAG_USER | 0x0500)
 * =========================================================================
 *
 * Access key:
 *   I = settable at NewObject (OM_NEW)
 *   S = settable at runtime   (OM_SET)
 *   G = readable              (OM_GET)
 */

#define UTED_Dummy              (TAG_USER | 0x7100)

/* [IS] (ULONG) Font point size in screen pixels.  Default: 12.
 *              Stored for subsequent UTED_AddFont calls; does not reload fonts, it affect next AddFont. */
#define UTED_PointSize          (UTED_Dummy + 1)

/* [IS] (ULONG) URP_PREF_* flags forwarded to URPDC_SetPreferenceFlags().
 *              Default: URP_PREF_ANTIALIAS | URP_PREF_CLUTMODE_NOMASK */
#define UTED_URPPrefs           (UTED_Dummy + 2)

/* [IS] (ULONG) Intuition pen number for text foreground.  Default: 1. */
#define UTED_TextPen            (UTED_Dummy + 3)

/* [IS] (ULONG) Intuition pen number for text background.  Default: 0. */
#define UTED_BgPen              (UTED_Dummy + 4)

/* [IS] (ULONG) URPFONT_* flags stored for subsequent UTED_AddFont calls.
 *              Default: 0. (actualluy no flag for this) */
#define UTED_FontFlags          (UTED_Dummy + 5)

/* [IS] (STRPTR) Load a font into the draw context via URPDC_AddFont(), using
 *               the current UTED_PointSize and UTED_FontFlags.  The path is
 *               passed directly to URPDC_AddFont() — not copied by the gadget.
 *               Multiple UTED_AddFont tags in one OM_SET/OM_NEW call append
 *               fonts in order, enabling the utf8rastport fallback chain. */
#define UTED_AddFont            (UTED_Dummy + 6)

/* [IS] (ignored) Unload all fonts from the draw context and flush the glyph
 *                cache.  Equivalent to calling URPDC_FlushFonts() directly.
 *                Typically followed by one or more UTED_AddFont tags. */
#define UTED_FlushFonts         (UTED_Dummy + 7)

// (removed)     (UTED_Dummy + 8)

/* [IS] (ULONG) Selection highlight background as 0x00RRGGBB.
 *              Default: 0x000000AA (medium blue). */
#define UTED_SelectionColor     (UTED_Dummy + 7)

/* [IS] (ULONG) Text cursor color as 0x00RRGGBB.
 *              Default: 0x00FFFFFF (white). */
#define UTED_CursorColor        (UTED_Dummy + 8)

/* [IS] (STRPTR) Replace entire text buffer with a NUL-terminated UTF-8 string.
 *  [G] Returns an AllocVec()'d copy of the current buffer; caller must FreeVec(). */
#define UTED_Text               (UTED_Dummy + 9)

/* [G]  (BOOL) TRUE if the buffer has been modified since the last UTED_Text set
 *             or since UTED_Modified was last cleared via OM_SET(UTED_Modified,FALSE). */
#define UTED_Modified           (UTED_Dummy + 10)

/* [G]  (ULONG) Total number of lines in the buffer (always >= 1). */
#define UTED_LineCount          (UTED_Dummy + 11)

/* [ISG] (ULONG) 0-based index of the first fully visible line (scroll position). */
#define UTED_ScrollTop          (UTED_Dummy + 12)

/* [G]  (ULONG) Number of lines that fit fully in the current gadget height. */
#define UTED_VisibleLines       (UTED_Dummy + 13)

/* [ISG] (ULONG) 0-based line index of the insertion cursor. */
#define UTED_CursorLine         (UTED_Dummy + 14)

/* [ISG] (ULONG) 0-based codepoint (character) index of the cursor within its line. */
#define UTED_CursorChar         (UTED_Dummy + 15)

/* [ISG] (ULONG) Horizontal scroll offset in pixels.  Default: 0. */
#define UTED_ScrollLeft         (UTED_Dummy + 16)

/* [G]  (ULONG) Width in pixels of the widest line that has built metrics.
 *              Returns 0 if no line has been rendered yet.
 *              O(n_lines) scan – call after typing, not on every render. */
#define UTED_MaxLineWidth       (UTED_Dummy + 17)

/* [ISG] (ULONG) Number of space-widths rendered for each TAB character.
 *               Valid range [1..12].  Default: 4.
 *               Forwarded to URPDC_SetAttribsA(URPDCA_TabSpaces).
 *               Invalidates all line caches and redraws. */
#define UTED_TabSpaces          (UTED_Dummy + 18)

/* [ISG] (ULONG) Maximum number of undo/redo history entries.
 *               Setting this flushes all current history.
 *               0 = undo disabled.  Default: 64. */
#define UTED_MaxNumberOfUndo    (UTED_Dummy + 19)

/* =========================================================================
 * Action attribute tags
 * =========================================================================
 * These replace the old UTEDM_ method IDs.  Trigger editor actions by
 * including them in a SetGadgetAttrs() call.  Intuition creates a valid
 * GadgetInfo before dispatching OM_SET, so the gadget can call
 * ObtainGIRPort() and repaint itself correctly.
 *
 * Companion tags (UTED_CursorExtend, UTED_ScrollToCenter) must appear in
 * the same SetGadgetAttrs call as the action tag they modify.
 */

/* [S] (STRPTR)  Insert NUL-terminated UTF-8 text at the cursor.
 *               Newlines split lines.  Deletes any active selection first. */
#define UTED_InsertText      (UTED_Dummy + 20)

/* [S] (LONG→ULONG)  Delete one character: -1 = backspace, +1 = forward-delete.
 *                   Deletes any active selection instead if one exists. */
#define UTED_DeleteChar      (UTED_Dummy + 21)

/* [S] (LONG→ULONG)  Move cursor horizontally by delta codepoints.
 *                   Positive = right, negative = left.
 *                   Pair with UTED_CursorExtend to extend selection. */
#define UTED_MoveCursorX     (UTED_Dummy + 22)

/* [S] (LONG→ULONG)  Move cursor vertically by delta lines.
 *                   Positive = down, negative = up.
 *                   Pair with UTED_CursorExtend to extend selection. */
#define UTED_MoveCursorY     (UTED_Dummy + 23)

/* [S] (BOOL)  Companion for UTED_MoveCursorX / UTED_MoveCursorY.
 *             TRUE = extend selection anchor; FALSE = clear selection first. */
#define UTED_CursorExtend    (UTED_Dummy + 24)

/* [S] (any)  Collapse selection to cursor position. */
#define UTED_ClearSelection  (UTED_Dummy + 25)

/* [S] (any)  Select entire buffer contents. */
#define UTED_SelectAll       (UTED_Dummy + 26)

/* [S] (any)  Delete selected text.  No-op if no selection. */
#define UTED_DeleteSelection (UTED_Dummy + 27)

/* [G] (STRPTR)  AllocVec'd copy of selected text as NUL-terminated UTF-8.
 *               Returns NULL if no selection.  Caller must FreeVec(). */
#define UTED_GetSelectedText (UTED_Dummy + 28)

/* [S] (ULONG)  Scroll viewport to make line index visible. */
#define UTED_ScrollTo        (UTED_Dummy + 29)

/* [S] (BOOL)  Companion for UTED_ScrollTo.
 *             TRUE = center the target line in the viewport. */
#define UTED_ScrollToCenter  (UTED_Dummy + 30)

/* [S] (ULONG)  Invalidate one line cache (rebuilds on next GM_RENDER).
 *              Pass UTED_ALLLINES to invalidate everything. */
#define UTED_InvalidateLine  (UTED_Dummy + 31)

/* Sentinel value meaning "all lines" for UTED_InvalidateLine. */
#define UTED_ALLLINES        (~0UL)

/* [S] (any)  Undo the last edit.  No-op if no undo history available. */
#define UTED_Undo            (UTED_Dummy + 32)

/* [S] (any)  Redo the last undone edit.  No-op if no redo history available. */
#define UTED_Redo            (UTED_Dummy + 33)

/* [S] (ULONG) Forward a raw Intuition keyboard event for processing.
 *             Encode as: ((Qualifier << 16) | RawKeyCode).
 *             Key-up events (RawKeyCode bit 7 set) are silently ignored.
 *
 *             Call this from your app's IDCMP RAWKEY / IDCMPHook handler so
 *             that keyboard logic runs in the safe application task context
 *             rather than inside GM_GOACTIVE / GM_HANDLEINPUT.
 *
 *             Handled inside the gadget:
 *               - Arrow keys, Page Up/Down (with Shift to extend selection)
 *               - Backspace, Delete, Return, Tab
 *               - Escape (clear selection), Ctrl+A (select all)
 *               - Ctrl+Z (undo), Ctrl+Y (redo)
 *               - Printable characters (via MapRawKey + Latin-1 → UTF-8)
 *
 *             NOT handled here (delegate to the application):
 *               - F1-F8 and their Shift/Ctrl variants (emoji, macros, etc.)
 *
 *             Encoding note: MapRawKey() returns bytes in the active keymap's
 *             native encoding.  See UTED_VanillaKeyAnsiCode / UTED_PutVanillaKey
 *             for correct Eastern European keymap support. */
#define UTED_PutRawKey       (UTED_Dummy + 34)

/* [S] (ULONG) Forward an already-translated vanilla key byte (0–255) at the
 *             cursor.  Encode as: ((Qualifier << 16) | VanillaKeyByte).
 *             The low byte is the WMHI_VANILLAKEY result byte; Intuition
 *             performs dead-key and Shift/Alt composition before issuing
 *             IDCMP_VANILLAKEY, so the byte is the final character in the
 *             keymap's native 8-bit encoding.  Qualifiers (from
 *             WINDOW_Qualifier) are packed in the upper 16 bits, matching
 *             the UTED_PutRawKey encoding.
 *
 *             Control characters (0x00–0x1F, 0x7F) are handled identically
 *             to UTED_PutRawKey's control-char logic:
 *               0x08  Backspace    0x09  Tab       0x0A/0x0D  Return
 *               0x1B  Escape (clear selection)     0x01  Ctrl+A (select all)
 *               0x1A  Ctrl+Z (undo)                0x19  Ctrl+Y (redo)
 *               0x7F  Delete
 *
 *             Tab (0x09) behaviour depends on UTED_TabsAreSpaces: when TRUE,
 *             inserts UTED_TabSpaces space characters instead of a literal \t.
 *
 *             Printable ASCII (0x20–0x7E) is inserted directly.
 *             Bytes 0x80–0xFF are decoded to Unicode according to
 *             UTED_VanillaKeyAnsiCode (default: Latin-1 / ISO 8859-1).
 *             Bytes 0x80–0x9F (C1 control range) are silently ignored. */
#define UTED_PutVanillaKey   (UTED_Dummy + 35)

/* [ISG] (ULONG) Encoding used by UTED_PutVanillaKey for bytes 0xA0–0xFF.
 *               UTED_VANILLAKEY_LATIN1 (1, default) = ISO 8859-1
 *               UTED_VANILLAKEY_LATIN2 (2)          = ISO 8859-2
 *               Set this once at startup after probing the locale/keymap. */
#define UTED_VanillaKeyAnsiCode (UTED_Dummy + 36)

#define UTED_VANILLAKEY_LATIN1  1
#define UTED_VANILLAKEY_LATIN2  2

/* [ISG] (BOOL)  When TRUE, long lines are visually wrapped at word boundaries
 *               (or at the character boundary if no space/tab fits).
 *               The logical buffer (and cursor coords) remain in \n-separated
 *               line space; only the rendering adds extra visual rows.
 *               Setting this flushes the wrap map and triggers a full redraw.
 *               Default: FALSE. */
#define UTED_WordWrap           (UTED_Dummy + 37)

/* [G]  (ULONG)  Total number of *rendered* rows:
 *               - WordWrap FALSE: same as UTED_LineCount (\n count).
 *               - WordWrap TRUE:  total visual rows after wrapping.
 *               Use this value (not UTED_LineCount) for scrollbar sizing
 *               when WordWrap is active. */
#define UTED_RenderedLineCount  (UTED_Dummy + 38)

/* [IS] (ULONG)  Maximum number of display lines.  The gadget caps its
 *               rendered height to (MaxDisplayLines * lineHeight) pixels.
 *               Default: ~0UL (unlimited). */
#define UTED_MaxDisplayLines    (UTED_Dummy + 39)

/* [IS] (BOOL)   When TRUE, \n and \r are silently stripped from all
 *               inserted text, and pressing Enter sends UTEDN_EnterPressed
 *               instead of inserting a newline.  Useful for single-line
 *               or short-sentence input gadgets.  Default: FALSE. */
#define UTED_NoLineFeed         (UTED_Dummy + 40)

/* [ISG] (ULONG) Left pixel margin: glyphs start this many pixels from the
 *               gadget's left edge.  The margin strip is filled with the
 *               background pen.  Default: 2. */
#define UTED_LeftMargin         (UTED_Dummy + 41)

/* [ISG] (ULONG) Extra pixels added below each line of glyphs.
 *               Effectively makes lineHeight = fontMetricHeight + lineSpacing.
 *               Invalidates all line caches and redraws.  Default: 1. */
#define UTED_LineSpacing        (UTED_Dummy + 42)

/* [ISG] (ULONG) Right pixel margin: content area ends this many pixels from
 *               the gadget's right edge.  Default: 0. */
#define UTED_RightMargin        (UTED_Dummy + 43)

/* [ISG] (ULONG) Top pixel margin: first line starts this many pixels from
 *               the gadget's top edge.  Default: 0. */
#define UTED_TopMargin          (UTED_Dummy + 44)

/* [ISG] (ULONG) Bottom pixel margin: content area ends this many pixels from
 *               the gadget's bottom edge.  Default: 0. */
#define UTED_BottomMargin       (UTED_Dummy + 45)



/* [IG] (ULONG) bevel style for optional bevel. Value is enum in bevel.h default is BVS_NONE */
#define UTED_BevelStyle       (UTED_Dummy + 46)

/* [ISG] If given at init, allow to share the URPDrawContext that
 * retain a given fonts fallback configuration and glyph cache.
 * these are the objects managed by utf8rastport.library, you can also
 * create one with it and pass it. If URPDrawContext is not passed at init,
 * the gadget will create one. If you want to have same font configuration
 * on 2 editors, let the first one create it, then get the URPDrawContext from it,
 * and use it as init for the second. If a URPDrawContext you don't have to use
 * UTED_FlushFonts, UTED_AddFont, ... acrtually affects the UniTextEditor current
 * URPDrawContext, so you may just prepare many and switch them to a UniTextEditor
 * with it.
 * Note when passed by NewObject or SetAttribs, the URPDrawContext will
 * be retained by lib utf8rastport URPDC_Retain(), else it is created with
 * URPDC_Create()
*/
#define UTED_URPDrawContext       (UTED_Dummy + 47)

/* [ISG]  Workaround , we manage activation privately from external.
 * just bool to tell if we're activated or not.
*/
#define UTED_SetPrivateActivation       (UTED_Dummy + 48)

/* [S]  (ULONG) Set the line index used by the next UTED_LineUTF8TextBuffer get.
 *              0-based; out-of-range values clamp to the last valid line. */
#define UTED_LineTextToGet              (UTED_Dummy + 49)

/* [G]  (STRPTR) Returns a direct pointer to the internal NUL-terminated UTF-8
 *               buffer of the line last selected with UTED_LineTextToGet.
 *               The pointer is valid only until the next edit operation on that
 *               line.  Do NOT FreeVec() the result. */
#define UTED_LineUTF8TextBuffer         (UTED_Dummy + 50)

/* =========================================================================
 * Search & Replace
 * =========================================================================
 */

/* Position inside the text buffer (line + codepoint column, both 0-based). */
typedef struct UTEDTextPosition {
    ULONG line;
    ULONG col;
} UTEDTextPosition;

/* [S] (STRPTR) Search forward from the current cursor position for the given
 *              NUL-terminated UTF-8 pattern.  If found, the match is selected
 *              and the cursor moves to the END of the match.  If not found,
 *              cursor and selection are unchanged.
 *              Detect success by comparing UTED_CursorLine / UTED_CursorChar
 *              before and after this tag is set. */
#define UTED_SearchNext          (UTED_Dummy + 51)

/* [S] (STRPTR) Search backward from the current cursor position for the given
 *              NUL-terminated UTF-8 pattern.  If found, the match is selected
 *              and the cursor moves to the START of the match.  If not found,
 *              cursor and selection are unchanged. */
#define UTED_SearchPrevious      (UTED_Dummy + 52)

/* [S] (STRPTR) Replace the match saved by the last successful
 *              UTED_SearchNext / UTED_SearchPrevious call with this
 *              NUL-terminated UTF-8 string.  No-op if no prior search
 *              result is pending or if the gadget is read-only.
 *              Pass "" to delete the match without inserting text. */
#define UTED_ReplaceString       (UTED_Dummy + 53)

/* [ISG] (BOOL) When TRUE (default), UTED_SearchNext and UTED_SearchPrevious
 *              perform a byte-exact (case-sensitive) match.
 *              When FALSE, matching is case-insensitive for ASCII letters
 *              (a-z / A-Z); non-ASCII bytes are always compared literally. */
#define UTED_SearchCaseSensitive (UTED_Dummy + 54)

/* [ISG] (BOOL) When TRUE, draw a faint horizontal line inside each tab space
 *              at the text baseline, using a pen halfway between the text and
 *              background pens.  Default: FALSE. */
#define UTED_VisibleTabs         (UTED_Dummy + 55)

/* [ISG] (STRPTR) Named editing context – typically an absolute file path.
 *
 * SET: Switch the gadget to the named context.
 *   - If the name is new, the current document is stashed under the old name
 *     and a fresh empty document is activated.
 *   - If the name already has a stashed document, the current document is
 *     stashed and the previously saved document is restored (O(1) pointer swap
 *     + two sentinel fixups).
 *   - If the name equals the current context name, this is a no-op.
 *   - A subsequent UTED_Text tag in the same SetAttrs call will overwrite the
 *     just-activated (or just-restored) content; use this for file-load:
 *       SetAttrs(o, UTED_CurrentContext, path, UTED_Text, content, TAG_END)
 *
 * GET: Returns a direct pointer to the internal context-name buffer (do NOT
 *      FreeVec the result); returns "" when no context has been set.
 */
#define UTED_CurrentContext      (UTED_Dummy + 56)

/* [S] (STRPTR) Rename the current context in place.
 *
 * Updates the gadget's internal context name without triggering a
 * stash/restore cycle.  Use this after a "Save As" operation so that
 * subsequent tab switches and stash lookups use the new file path as key.
 * No-op when no context has been set.  Also updates any existing stash
 * entry that shares the old name (handles the edge case where the same
 * document was stashed under the old name by a previous switch).
 */
#define UTED_RenameContext       (UTED_Dummy + 57)

/* [ISG] (BOOL)  When TRUE, pressing Tab inserts UTED_TabSpaces space
 *               characters instead of a literal TAB character (\t).
 *               Applied by both UTED_PutRawKey and UTED_PutVanillaKey.
 *               Default: FALSE. */
#define UTED_TabsAreSpaces       (UTED_Dummy + 58)

/* [ISG] (BOOL)  When TRUE, ANSI escape sequences embedded in the text
 *               are interpreted during rendering (colours, styles, etc.).
 *               Default: FALSE. */
#define UTED_ApplyAnsiEscapes    (UTED_Dummy + 59)

/* [S] (STRPTR) Delete a stashed (non-current) context by name.
 *              Frees all memory owned by the stash (lines, undo/redo, wrap map).
 *              No-op when the name matches the currently active context or when
 *              no stash with that name exists.  The caller must switch to a
 *              different context first (e.g. via UTED_CurrentContext) before
 *              deleting the one that was previously active. */
#define UTED_DeleteContext       (UTED_Dummy + 60)

/* [ISG] (BOOL)  Selects the colour-mapping strategy used when
 *               UTED_ApplyAnsiEscapes is TRUE.
 *
 *   FALSE (default) — Amiga mode:
 *       SGR colour codes 30-37 / 40-47 map directly to Intuition pen
 *       numbers 0-7.  Fast, no library calls.  Use when the text was
 *       authored with AmigaDOS *E[ sequences.
 *
 *   TRUE — Unix/xterm mode:
 *       Colour codes are resolved against the xterm 256-colour palette
 *       using graphics.library FindColor() on the current screen.
 *       Supports:
 *         30-37 / 40-47   standard 8 ANSI colours
 *         90-97 / 100-107 bright (high-intensity) variants
 *         38;5;N / 48;5;N xterm 256-colour (most common in Unix output)
 *         38;2;r;g;b      true-colour (24-bit, mapped via FindColor)
 *       Use when loading text produced by Unix tools (e.g. wttr.in,
 *       ls --color, grep --color).
 *
 *   Changing this attribute invalidates all line caches (full redraw).
 *   Default: FALSE. */
#define UTED_AnsiUnixColors      (UTED_Dummy + 61)


/* =========================================================================
 * Notification attribute tags   (base TAG_USER | 0x05C0)
 *
 * These are sent via OM_NOTIFY (DoSuperMethodA) from within the gadget.
 * Connect them to a target object or read them in IDCMP_IDCMPUPDATE events.
 * =========================================================================
 */

#define UTEDN_Dummy             (TAG_USER | 0x05C0)

/* Cursor moved or selection changed.  Value = new cursor line index. */
#define UTEDN_CursorMoved       (UTEDN_Dummy + 1)

/* Text content changed.  Value = line index where the edit occurred. */
#define UTEDN_TextChanged       (UTEDN_Dummy + 2)

/* Scroll position changed.  Value = new scrollTopLine index. */
#define UTEDN_ScrollChanged     (UTEDN_Dummy + 3)

/* Undo availability changed.  Value = TRUE if undo is possible. */
#define UTEDN_UndoAvailable     (UTEDN_Dummy + 4)

/* Redo availability changed.  Value = TRUE if redo is possible. */
#define UTEDN_RedoAvailable     (UTEDN_Dummy + 5)

/* Enter key pressed while UTED_NoLineFeed is TRUE.  Value = 0. */
#define UTEDN_EnterPressed      (UTEDN_Dummy + 6)




/* =========================================================================
 * Class management
 * =========================================================================
 */
 /* only available in static link mode */
#ifdef USE_STATIC_UNITEXTEDITOR
    /* Global class pointer – valid between UniTextEditor_Init() and UniTextEditor_Exit() */
    extern Class *UniTextEditorClass;

    /* Call once at application startup.  Returns TRUE on success. */
    int  UniTextEditor_Init(void);

    /* Call at application shutdown to free the class. */
    void UniTextEditor_Exit(void);
#endif

#endif /* UNITEXTEDITOR_H */
