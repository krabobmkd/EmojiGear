/*
 * TextEditor gadget – mouse input handling.
 *
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE run in the input.device
 * context, which must NOT call FreeType, disk I/O, or any blocking API.
 *
 * These three functions therefore do nothing except record the raw mouse
 * coordinates / qualifiers into inst->pending* fields and send a
 * UTEDN_CursorMoved notification.  All actual hit-testing and cursor/
 * selection updates happen at the start of UniTextEditor_OnRender, which
 * runs in the safe application task context.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <devices/inputevent.h>
#include <clib/alib_protos.h>
#include "unitexteditor_private.h"
#include "bdbprintf.h"

#define RAWKEY_F1  0x50
#define RAWKEY_F10  0x59
#define RAWKEY_HELP  0x5F

int uted_manage_rawkey_keycode(Class *cl, Object *o,ULONG codedata, struct GadgetInfo *gi)
{
    int result = 0;
    UniTextEditorData *inst = UTED_DATA(cl, o);
    ULONG  packed    = (ULONG)codedata;
    UWORD  keycode   = (UWORD)(packed & 0xFFFF);
    UWORD  qualifier = (UWORD)((packed >> 16) & 0xFFFF);
    BOOL   shift     = (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                       ? TRUE : FALSE;
    BOOL   ctrl      = (qualifier & IEQUALIFIER_CONTROL) ? TRUE : FALSE;
    BOOL   navHandled = FALSE;

    /* Ignore key-up events */
    if (keycode & RAWKEY_UP_FLAG) return 0;

    /* F1-F8: delegated to app level (emoji etc.), silently ignored here */
    if (keycode >= RAWKEY_F1 && keycode <= RAWKEY_F8) return 0;
    if(keycode == RAWKEY_HELP) return 0;

    /* Navigation: bypass MapRawKey so keymap ANSI-escape mappings
     * (if any) don't accidentally produce text output. */
    switch (keycode) {
    case RAWKEY_TAB: {
        if (!inst->readOnly) {
            BOOL didBlock = FALSE;
            /* Block indent/unindent when selection spans more than one line.
             * Uses low-level uted_line_insert/delete_bytes directly to avoid
             * the selection-replace and undo side-effects of DoInsertText/DoDeleteChar. */
            if (inst->hasSelection) {
                ULONG firstLine = inst->selAnchor.line < inst->selFloat.line
                                ? inst->selAnchor.line : inst->selFloat.line;
                ULONG lastLine  = inst->selAnchor.line > inst->selFloat.line
                                ? inst->selAnchor.line : inst->selFloat.line;
                if (firstLine < lastLine) {
                    ULONG L;
                    didBlock = TRUE;
                    if (!shift) {
                        /* Block indent: insert tab or spaces at byte 0 of each line */
                        const char *insertStr;
                        ULONG insertBytes, insertChars;
                        char spaces[13];
                        if (inst->tabsAreSpaces) {
                            ULONG n = inst->tabSpaces, si;
                            if (n < 1) n = 1; if (n > 12) n = 12;
                            for (si = 0; si < n; si++) spaces[si] = ' ';
                            spaces[n] = '\0';
                            insertStr = spaces; insertBytes = n; insertChars = n;
                        } else {
                            insertStr = "\t"; insertBytes = 1; insertChars = 1;
                        }
                        for (L = firstLine; L <= lastLine; L++) {
                            UniTextEditorLine *curLine = uted_get_line(inst, L);
                            if (!curLine) continue;
                            UniTextEditorPos savedCursor = inst->cursor;
                            UniTextEditorPos savedAnchor = inst->selAnchor;
                            BOOL             savedHasSel = inst->hasSelection;
                            if (uted_line_insert_bytes(curLine, 0, insertStr, insertBytes)) {
                                if (inst->cursor.line    == L) inst->cursor.ch    += insertChars;
                                if (inst->selAnchor.line == L) inst->selAnchor.ch += insertChars;
                                if (inst->selFloat.line  == L) inst->selFloat.ch  += insertChars;
                                if (!inst->undoInProgress && inst->undoMax > 0) {
                                    UTEDUndoEntry ue;
                                    UniTextEditorPos ps, pe;
                                    ps.line = L; ps.ch = 0;
                                    pe.line = L; pe.ch = insertChars;
                                    ue.opType       = UTED_UNDO_INSERT;
                                    ue.delDir       = 0;
                                    ue.atomic       = TRUE;
                                    ue.cursorBefore = savedCursor;
                                    ue.anchorBefore = savedAnchor;
                                    ue.hasSelBefore = savedHasSel;
                                    ue.posStart     = ps;
                                    ue.posEnd       = pe;
                                    ue.textBytes    = insertBytes;
                                    ue.text = (char *)AllocVec(insertBytes + 1, MEMF_ANY);
                                    if (ue.text) {
                                        CopyMem((APTR)insertStr, ue.text, insertBytes);
                                        ue.text[insertBytes] = '\0';
                                    } else {
                                        ue.textBytes = 0;
                                    }
                                    uted_undo_push(inst, &ue);
                                }
                            }
                        }
                    } else {
                        /* Block unindent: remove tab or spaces from byte 0 of each line */
                        for (L = firstLine; L <= lastLine; L++) {
                            UniTextEditorLine *curLine = uted_get_line(inst, L);
                            if (!curLine || !curLine->utf8 || curLine->charCount == 0)
                                continue;
                            const char *s = curLine->utf8;
                            ULONG removeBytes = 0, removeChars = 0;
                            if (inst->tabsAreSpaces) {
                                int nbsp = (int)inst->tabSpaces;
                                int j;
                                BOOL allSpaces;
                                if (nbsp < 1) nbsp = 1; if (nbsp > 12) nbsp = 12;
                                if ((int)curLine->charCount >= nbsp) {
                                    allSpaces = TRUE;
                                    for (j = 0; j < nbsp; j++) {
                                        if (s[j] != ' ') { allSpaces = FALSE; break; }
                                    }
                                    if (allSpaces) {
                                        removeBytes = (ULONG)nbsp;
                                        removeChars = (ULONG)nbsp;
                                    }
                                }
                            } else {
                                if (s[0] == '\t') { removeBytes = 1; removeChars = 1; }
                            }
                            if (removeBytes) {
                                char capBuf[13];
                                UniTextEditorPos savedCursor = inst->cursor;
                                UniTextEditorPos savedAnchor = inst->selAnchor;
                                BOOL             savedHasSel = inst->hasSelection;
                                CopyMem((APTR)s, capBuf, removeBytes);
                                capBuf[removeBytes] = '\0';
                                if (uted_line_delete_bytes(curLine, 0, removeBytes)) {
                                    if (inst->cursor.line == L)
                                        inst->cursor.ch = inst->cursor.ch > removeChars
                                                        ? inst->cursor.ch - removeChars : 0;
                                    if (inst->selAnchor.line == L)
                                        inst->selAnchor.ch = inst->selAnchor.ch > removeChars
                                                           ? inst->selAnchor.ch - removeChars : 0;
                                    if (inst->selFloat.line == L)
                                        inst->selFloat.ch = inst->selFloat.ch > removeChars
                                                          ? inst->selFloat.ch - removeChars : 0;
                                    if (!inst->undoInProgress && inst->undoMax > 0) {
                                        UTEDUndoEntry ue;
                                        UniTextEditorPos ps, pe;
                                        ps.line = L; ps.ch = 0;
                                        pe.line = L; pe.ch = removeChars;
                                        ue.opType       = UTED_UNDO_DELETE;
                                        ue.delDir       = 0;
                                        ue.atomic       = TRUE;
                                        ue.cursorBefore = savedCursor;
                                        ue.anchorBefore = savedAnchor;
                                        ue.hasSelBefore = savedHasSel;
                                        ue.posStart     = ps;
                                        ue.posEnd       = pe;
                                        ue.textBytes    = removeBytes;
                                        ue.text = (char *)AllocVec(removeBytes + 1, MEMF_ANY);
                                        if (ue.text) {
                                            CopyMem(capBuf, ue.text, removeBytes);
                                            ue.text[removeBytes] = '\0';
                                        } else {
                                            ue.textBytes = 0;
                                        }
                                        uted_undo_push(inst, &ue);
                                    }
                                }
                            }
                        }
                    }
                    inst->modified         = TRUE;
                    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
                    inst->refreshStartLine = (LONG)firstLine;
                    inst->refreshEndLine   = (LONG)lastLine;
                    uted_ensure_cursor_visible(inst);
                    uted_ensure_cursor_h_visible(inst);
                    uted_undo_notify(cl, o, gi);
                }
            }
            if (!didBlock) {
                if (shift) {
                    /* Backtab: delete N spaces or one \t before cursor */
                    if (inst->cursor.ch > 0) {
                        UniTextEditorLine *curLine = uted_get_line(inst, inst->cursor.line);
                        if (curLine && curLine->utf8) {
                            const char *s = curLine->utf8;
                            if (inst->tabsAreSpaces) {
                                int nbsp = (int)inst->tabSpaces;
                                int j;
                                BOOL allSpaces;
                                if (nbsp < 1) nbsp = 1;
                                if (nbsp > 12) nbsp = 12;
                                if ((int)inst->cursor.ch >= nbsp) {
                                    ULONG byteOff = uted_char_to_byte(s, curLine->byteUsed,
                                                        inst->cursor.ch - (ULONG)nbsp);
                                    allSpaces = TRUE;
                                    for (j = 0; j < nbsp; j++) {
                                        if (s[byteOff + j] != ' ') { allSpaces = FALSE; break; }
                                    }
                                    if (allSpaces) {
                                        for (j = 0; j < nbsp; j++)
                                            UniTextEditor_DoDeleteChar(cl, o, -1);
                                    }
                                }
                            } else {
                                ULONG byteOff = uted_char_to_byte(s, curLine->byteUsed,
                                                    inst->cursor.ch - 1);
                                if (s[byteOff] == '\t')
                                    UniTextEditor_DoDeleteChar(cl, o, -1);
                            }
                        }
                    }
                } else {
                    /* Tab forward */
                    if (inst->tabsAreSpaces) {
                        char spaces[13];
                        ULONG n = inst->tabSpaces, si;
                        if (n < 1) n = 1; if (n > 12) n = 12;
                        for (si = 0; si < n; si++) spaces[si] = ' ';
                        spaces[n] = '\0';
                        UniTextEditor_DoInsertText(cl, o, spaces, (LONG)n);
                    } else {
                        UniTextEditor_DoInsertText(cl, o, "\t", 1);
                    }
                }
            }
            result = 1;
        }
        navHandled = TRUE;
        break;
    }
    case RAWKEY_BACKSPACE:
        if (shift && !inst->readOnly) {
            UniTextEditor_DoDeleteToLineStart(cl, o);
           result = 1; navHandled = TRUE;
        }
        break;  /* non-shift: fall through to MapRawKey path */
    case RAWKEY_DELETE:
        if (shift && !inst->readOnly) {
            UniTextEditor_DoDeleteToLineEnd(cl, o);
            result = 1; navHandled = TRUE;
        }
        break;  /* non-shift: fall through to MapRawKey path */
    case RAWKEY_LEFT:
        UniTextEditor_DoMoveCursor(cl, o, -1,  0, shift);
        navHandled = TRUE; break;
    case RAWKEY_RIGHT:
        UniTextEditor_DoMoveCursor(cl, o,  1,  0, shift);
        navHandled = TRUE; break;
    case RAWKEY_UP:
        if (ctrl) {
            ULONG curVisRow  = uted_cursor_visual_row(inst);
            ULONG topRow     = inst->scrollTopLine;
            if (curVisRow > topRow) {
                /* move cursor to first visible row */
                UniTextEditor_DoMoveCursor(cl, o, 0,
                    -(LONG)(curVisRow - topRow), shift);
            } else {
                /* already at top of page: scroll one full page up */
                LONG vis = (LONG)(inst->visibleLines > 1 ? inst->visibleLines - 1 : 1);
                UniTextEditor_DoMoveCursor(cl, o, 0, -vis, shift);
            }
        } else {
            UniTextEditor_DoMoveCursor(cl, o,  0, -1, shift);
        }
        navHandled = TRUE; break;
    case RAWKEY_DOWN:
        if (ctrl) {
            ULONG curVisRow  = uted_cursor_visual_row(inst);
            ULONG vis        = (ULONG)(inst->visibleLines > 1 ? inst->visibleLines - 1 : 1);
            ULONG lastVisRow = inst->scrollTopLine + vis;
            if (curVisRow < lastVisRow) {
                /* move cursor to last visible row */
                UniTextEditor_DoMoveCursor(cl, o, 0,
                    (LONG)(lastVisRow - curVisRow), shift);
            } else {
                /* already at bottom of page: scroll one full page down */
                UniTextEditor_DoMoveCursor(cl, o, 0, (LONG)vis, shift);
            }
        } else {
            UniTextEditor_DoMoveCursor(cl, o,  0,  1, shift);
        }
        navHandled = TRUE; break;
    case RAWKEY_PGUP: {
        LONG vis = (LONG)(inst->visibleLines > 1 ? inst->visibleLines - 1 : 1);
        UniTextEditor_DoMoveCursor(cl, o, 0, -vis, shift);
        navHandled = TRUE; break;
    }
    case RAWKEY_PGDN: {
        LONG vis = (LONG)(inst->visibleLines > 1 ? inst->visibleLines - 1 : 1);
        UniTextEditor_DoMoveCursor(cl, o, 0,  vis, shift);
        navHandled = TRUE; break;
    }
    default: break;
    }

    if (navHandled) {
        result = 1;
    }
    return result;

}

/* ISO 8859-2 (Latin-2) codepoint table for bytes 0xA0–0xFF.
 * Bytes 0x80–0x9F are C1 controls; silently ignored below. */
static const ULONG latin2_uni[96] = {
    /* A0 */ 0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
    /* A8 */ 0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
    /* B0 */ 0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
    /* B8 */ 0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
    /* C0 */ 0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    /* C8 */ 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    /* D0 */ 0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    /* D8 */ 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    /* E0 */ 0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    /* E8 */ 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    /* F0 */ 0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    /* F8 */ 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

/*
 *  "locale-translated" keycode managmet
    This is either called:
    - in "external message mode" window level message re-send with UTAD_PutVanillaKey
    - in "internal boopsi message mode"
*/

int uted_manage_vanilla_keycode(Class *cl, Object *o,ULONG codedata, struct GadgetInfo *gi)
{
    int result = 0;
    UniTextEditorData *inst = UTED_DATA(cl, o);
    UBYTE ch       = (UBYTE)(codedata & 0xFF);
    UWORD vqualifier = (UWORD)((codedata >> 16) & 0xFFFF);
    BOOL  vshift   = (vqualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                     ? TRUE : FALSE;

    /* Control characters -> never worked, need rawkey level before */
    // if (ch == 0x1B) {
    //     UniTextEditor_DoClearSelection(cl, o);
    //     redraw = TRUE; result = 1;
    // } else if (ch == 0x01) {
    //     UniTextEditor_DoSelectAll(cl, o);
    //     redraw = TRUE; result = 1;
    // } else

    if (!inst->readOnly) {
        if (ch == 0x08) {
            if (vshift) UniTextEditor_DoDeleteToLineStart(cl, o);
            else        UniTextEditor_DoDeleteChar(cl, o, -1);
            result = 1;
        } else if (ch == 0x7F) {
            if (vshift) UniTextEditor_DoDeleteToLineEnd(cl, o);
            else        UniTextEditor_DoDeleteChar(cl, o,  1);
            result = 1;
        } else if (ch == 0x0D || ch == 0x0A) {
            if (inst->noLineFeed) {
                uted_notify(cl, o, gi, UTEDN_EnterPressed, 0);
                result = 1;
            } else {
                UniTextEditor_DoInsertText(cl, o, "\n", 1);
                result = 1;
            }
        } else if (ch == 0x09) {
            /* Tab / Shift+Tab.  Multi-line selection → block indent/unindent
             * using low-level primitives so the selection is never consumed. */
            BOOL didBlock = FALSE;
            if (inst->hasSelection) {
                ULONG firstLine = inst->selAnchor.line < inst->selFloat.line
                                ? inst->selAnchor.line : inst->selFloat.line;
                ULONG lastLine  = inst->selAnchor.line > inst->selFloat.line
                                ? inst->selAnchor.line : inst->selFloat.line;
                if (firstLine < lastLine) {
                    ULONG L;
                    didBlock = TRUE;
                    if (!vshift) {
                        /* Block indent */
                        const char *insertStr;
                        ULONG insertBytes, insertChars;
                        char spaces[13];
                        if (inst->tabsAreSpaces) {
                            ULONG n = inst->tabSpaces, si;
                            if (n < 1) n = 1; if (n > 12) n = 12;
                            for (si = 0; si < n; si++) spaces[si] = ' ';
                            spaces[n] = '\0';
                            insertStr = spaces; insertBytes = n; insertChars = n;
                        } else {
                            insertStr = "\t"; insertBytes = 1; insertChars = 1;
                        }
                        for (L = firstLine; L <= lastLine; L++) {
                            UniTextEditorLine *curLine = uted_get_line(inst, L);
                            if (!curLine) continue;
                            UniTextEditorPos savedCursor = inst->cursor;
                            UniTextEditorPos savedAnchor = inst->selAnchor;
                            BOOL             savedHasSel = inst->hasSelection;
                            if (uted_line_insert_bytes(curLine, 0, insertStr, insertBytes)) {
                                if (inst->cursor.line    == L) inst->cursor.ch    += insertChars;
                                if (inst->selAnchor.line == L) inst->selAnchor.ch += insertChars;
                                if (inst->selFloat.line  == L) inst->selFloat.ch  += insertChars;
                                if (!inst->undoInProgress && inst->undoMax > 0) {
                                    UTEDUndoEntry ue;
                                    UniTextEditorPos ps, pe;
                                    ps.line = L; ps.ch = 0;
                                    pe.line = L; pe.ch = insertChars;
                                    ue.opType       = UTED_UNDO_INSERT;
                                    ue.delDir       = 0;
                                    ue.atomic       = TRUE;
                                    ue.cursorBefore = savedCursor;
                                    ue.anchorBefore = savedAnchor;
                                    ue.hasSelBefore = savedHasSel;
                                    ue.posStart     = ps;
                                    ue.posEnd       = pe;
                                    ue.textBytes    = insertBytes;
                                    ue.text = (char *)AllocVec(insertBytes + 1, MEMF_ANY);
                                    if (ue.text) {
                                        CopyMem((APTR)insertStr, ue.text, insertBytes);
                                        ue.text[insertBytes] = '\0';
                                    } else {
                                        ue.textBytes = 0;
                                    }
                                    uted_undo_push(inst, &ue);
                                }
                            }
                        }
                    } else {
                        /* Block unindent */
                        for (L = firstLine; L <= lastLine; L++) {
                            UniTextEditorLine *curLine = uted_get_line(inst, L);
                            if (!curLine || !curLine->utf8 || curLine->charCount == 0) continue;
                            const char *s = curLine->utf8;
                            ULONG removeBytes = 0, removeChars = 0;
                            if (inst->tabsAreSpaces) {
                                int nbsp = (int)inst->tabSpaces;
                                int j; BOOL allSpaces;
                                if (nbsp < 1) nbsp = 1; if (nbsp > 12) nbsp = 12;
                                if ((int)curLine->charCount >= nbsp) {
                                    allSpaces = TRUE;
                                    for (j = 0; j < nbsp; j++) {
                                        if (s[j] != ' ') { allSpaces = FALSE; break; }
                                    }
                                    if (allSpaces) { removeBytes = (ULONG)nbsp; removeChars = (ULONG)nbsp; }
                                }
                            } else {
                                if (s[0] == '\t') { removeBytes = 1; removeChars = 1; }
                            }
                            if (removeBytes) {
                                char capBuf[13];
                                UniTextEditorPos savedCursor = inst->cursor;
                                UniTextEditorPos savedAnchor = inst->selAnchor;
                                BOOL             savedHasSel = inst->hasSelection;
                                CopyMem((APTR)s, capBuf, removeBytes);
                                capBuf[removeBytes] = '\0';
                                if (uted_line_delete_bytes(curLine, 0, removeBytes)) {
                                    if (inst->cursor.line == L)
                                        inst->cursor.ch = inst->cursor.ch > removeChars
                                                        ? inst->cursor.ch - removeChars : 0;
                                    if (inst->selAnchor.line == L)
                                        inst->selAnchor.ch = inst->selAnchor.ch > removeChars
                                                           ? inst->selAnchor.ch - removeChars : 0;
                                    if (inst->selFloat.line == L)
                                        inst->selFloat.ch = inst->selFloat.ch > removeChars
                                                          ? inst->selFloat.ch - removeChars : 0;
                                    if (!inst->undoInProgress && inst->undoMax > 0) {
                                        UTEDUndoEntry ue;
                                        UniTextEditorPos ps, pe;
                                        ps.line = L; ps.ch = 0;
                                        pe.line = L; pe.ch = removeChars;
                                        ue.opType       = UTED_UNDO_DELETE;
                                        ue.delDir       = 0;
                                        ue.atomic       = TRUE;
                                        ue.cursorBefore = savedCursor;
                                        ue.anchorBefore = savedAnchor;
                                        ue.hasSelBefore = savedHasSel;
                                        ue.posStart     = ps;
                                        ue.posEnd       = pe;
                                        ue.textBytes    = removeBytes;
                                        ue.text = (char *)AllocVec(removeBytes + 1, MEMF_ANY);
                                        if (ue.text) {
                                            CopyMem(capBuf, ue.text, removeBytes);
                                            ue.text[removeBytes] = '\0';
                                        } else {
                                            ue.textBytes = 0;
                                        }
                                        uted_undo_push(inst, &ue);
                                    }
                                }
                            }
                        }
                    }
                    inst->modified = TRUE;
                    if (inst->wordWrap) inst->wrapMapDirty = TRUE;
                    inst->refreshStartLine = (LONG)firstLine;
                    inst->refreshEndLine   = (LONG)lastLine;
                    uted_ensure_cursor_visible(inst);
                    uted_ensure_cursor_h_visible(inst);
                    uted_undo_notify(cl, o, gi);
                }
            }
            if (!didBlock) {
                /* Single line / no selection: normal tab or backtab */
                if (!vshift) {
                    if (inst->tabsAreSpaces) {
                        char spaces[13];
                        ULONG n = inst->tabSpaces, si;
                        if (n < 1) n = 1; if (n > 12) n = 12;
                        for (si = 0; si < n; si++) spaces[si] = ' ';
                        spaces[n] = '\0';
                        UniTextEditor_DoInsertText(cl, o, spaces, (LONG)n);
                    } else {
                        UniTextEditor_DoInsertText(cl, o, "\t", 1);
                    }
                } else {
                    /* Shift+Tab via vanilla key: backtab */
                    if (inst->cursor.ch > 0) {
                        UniTextEditorLine *curLine = uted_get_line(inst, inst->cursor.line);
                        if (curLine && curLine->utf8) {
                            const char *s = curLine->utf8;
                            if (inst->tabsAreSpaces) {
                                int nbsp = (int)inst->tabSpaces;
                                int j; BOOL allSpaces;
                                if (nbsp < 1) nbsp = 1; if (nbsp > 12) nbsp = 12;
                                if ((int)inst->cursor.ch >= nbsp) {
                                    ULONG byteOff = uted_char_to_byte(s, curLine->byteUsed,
                                                        inst->cursor.ch - (ULONG)nbsp);
                                    allSpaces = TRUE;
                                    for (j = 0; j < nbsp; j++) {
                                        if (s[byteOff + j] != ' ') { allSpaces = FALSE; break; }
                                    }
                                    if (allSpaces)
                                        for (j = 0; j < nbsp; j++)
                                            UniTextEditor_DoDeleteChar(cl, o, -1);
                                }
                            } else {
                                ULONG byteOff = uted_char_to_byte(s, curLine->byteUsed,
                                                    inst->cursor.ch - 1);
                                if (s[byteOff] == '\t')
                                    UniTextEditor_DoDeleteChar(cl, o, -1);
                            }
                        }
                    }
                }
            }
            result = 1;
        } else if (ch == 0x1A) {
            UniTextEditor_DoUndo(cl, o);
            uted_undo_notify(cl, o, gi);
            result = 1;
        } else if (ch == 0x19) {
            UniTextEditor_DoRedo(cl, o);
            uted_undo_notify(cl, o, gi);
            result = 1;
        } else if (ch >= 0x20 && ch < 0x80) {
            /* Printable ASCII – same in all encodings */
            char u[1];
            u[0] = (char)ch;
            UniTextEditor_DoInsertText(cl, o, u, 1);
            result = 1;
        } else if (ch >= 0xA0) {
            /* Extended char: decode to Unicode codepoint then UTF-8 */
            ULONG cp;
            if (inst->vanillaAnsiCode == UTED_VANILLAKEY_LATIN2)
                cp = latin2_uni[ch - 0xA0];
            else
                cp = (ULONG)ch; /* Latin-1: byte == codepoint */

            char u[4];
            ULONG ul;
            if (cp < 0x80) {
                u[0] = (char)cp; ul = 1;
            } else if (cp < 0x800) {
                u[0] = (char)(0xC0 | (cp >> 6));
                u[1] = (char)(0x80 | (cp & 0x3F));
                ul = 2;
            } else {
                u[0] = (char)(0xE0 | (cp >> 12));
                u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u[2] = (char)(0x80 | (cp & 0x3F));
                ul = 3;
            }
            UniTextEditor_DoInsertText(cl, o, u, (LONG)ul);
            result = 1;
        }
        /* 0x80–0x9F: C1 controls, silently ignored */
    }
    return result;
}

int uted_manageFullRawKey(Class *cl, Object *o,
            struct InputEvent *ie,
            struct GadgetInfo *gi
             )
{
    int used = 0;
    //UniTextEditorData *inst = UTED_DATA(cl, o);
   // struct InputEvent *ie   = msg->gpi_IEvent;

    /* F1-F8: delegated to app level (emoji etc.), silently ignored here */
    if (ie->ie_Code >= RAWKEY_F1 && ie->ie_Code <= RAWKEY_F8) return 0;
    if(ie->ie_Code == RAWKEY_HELP) return 0;

    used = uted_manage_rawkey_keycode(cl,o,
        ((ULONG)ie->ie_Code)|(((ULONG)ie->ie_Qualifier)<<16),gi);


    // memset(&ie, 0, sizeof(ie));
    // ie.ie_Class     = IECLASS_RAWKEY;
    // ie.ie_Code      = keycode;
    // ie.ie_Qualifier = qualifier;
    if(!used)
    {
        UBYTE mapbuf[8];
        WORD  nchars;

        nchars = MapRawKey(ie, (STRPTR)mapbuf, (LONG)sizeof(mapbuf), NULL);
        if(nchars>0)
        {
            used = uted_manage_vanilla_keycode(cl,o,
                    mapbuf[0] | (((ULONG)ie->ie_Qualifier)<<16),gi);
        }
    }
    return used;
}

/* =========================================================================
 * Internal scrollbar hit-test
 * Returns TRUE when (x,y) (gadget-relative) falls inside the scrollbar track
 * AND the scrollbar is currently shown (text taller than viewport).
 * The scrollbar is rendered at gadget-relative x: gadWidth-7..gadWidth-4.
 * We use an 8-px wide hit zone (gadWidth-8..gadWidth-1) for easier clicking.
 * =========================================================================
 */
static BOOL uted_hit_vscroll(UniTextEditorData *inst, WORD x, WORD y)
{
    ULONG totalRows;
    if (!inst->displayInternalVScroll) return FALSE;
    totalRows = inst->wordWrap ? inst->wrapRowCount : inst->lineCount;
    if (totalRows <= (ULONG)inst->visibleLines) return FALSE;
    if (x < inst->gadWidth - 8) return FALSE;
    if (y < (WORD)inst->topMargin || y >= inst->gadHeight - (WORD)inst->bottomMargin)
        return FALSE;
    return TRUE;
}

/* =========================================================================
 * GM_GOACTIVE  (left button down) – input.device context, no API calls
 * =========================================================================
 */
ULONG UniTextEditor_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    struct InputEvent *ie   = msg->gpi_IEvent;
    /* Programmatic activation: ActivateGadget() or TAB cycling pass ie=NULL.
     * Set activation state and trigger a redraw so the cursor appears. */
    if (!ie) {
        inst->gadgetActive = TRUE;
        uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
        return GMR_MEACTIVE;
    }
 //   bdbprintf("UniTextEditor_OnGoActive %08x\n",(int)ie->ie_Class);

    if(inst->useInternalRawKey && ie->ie_Class == IECLASS_RAWKEY)
    {
       // bdbprintf("uted OnGoActive %08x %08x %08x\n",ie->ie_Code,ie->ie_Qualifier,ie->ie_position.ie_addr);
        uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
        uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, TRUE);
        return GMR_MEACTIVE | GMR_REUSE;
    }

    if (ie->ie_Class != IECLASS_RAWMOUSE) return GMR_NOREUSE;
    if (ie->ie_Code != IECODE_LBUTTON)    return GMR_NOREUSE;

    /* Multi-click detection: proximity + within system double-click timeout */
    {
        WORD dx = (WORD)(msg->gpi_Mouse.X - inst->lastClickX);
        WORD dy = (WORD)(msg->gpi_Mouse.Y - inst->lastClickY);
        if (dx < 0) dx = (WORD)-dx;
        if (dy < 0) dy = (WORD)-dy;
        if (dx <= 4 && dy <= 4 &&
            DoubleClick(inst->lastClickSec, inst->lastClickMicro,
                        (ULONG)ie->ie_TimeStamp.tv_secs,
                        (ULONG)ie->ie_TimeStamp.tv_micro))
        {
            if (inst->pendingClickCount < 3) inst->pendingClickCount++;
        } else {
            inst->pendingClickCount = 1;
        }
        inst->lastClickSec   = (ULONG)ie->ie_TimeStamp.tv_secs;
        inst->lastClickMicro = (ULONG)ie->ie_TimeStamp.tv_micro;
        inst->lastClickX     = (WORD)msg->gpi_Mouse.X;
        inst->lastClickY     = (WORD)msg->gpi_Mouse.Y;
    }

    /* Scrollbar gets priority over text interaction */
    if (uted_hit_vscroll(inst, msg->gpi_Mouse.X, msg->gpi_Mouse.Y)) {
        inst->vScrollDragging     = TRUE;
        inst->pendingVScrollClick = TRUE;
        inst->pendingVScrollY     = msg->gpi_Mouse.Y;
        inst->pendingDrag         = FALSE;
        inst->dragging            = TRUE;
        inst->gadgetActive        = TRUE;
        uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
        uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, TRUE);
        return GMR_MEACTIVE;
    }

    /* Record click; any previous pending drag is superseded by a new click */
    inst->pendingClick            = TRUE;
    inst->pendingClickX           = msg->gpi_Mouse.X;
    inst->pendingClickY           = msg->gpi_Mouse.Y;
    inst->pendingClickQualifiers  = ie->ie_Qualifier;
    inst->pendingDrag             = FALSE;

    inst->dragging     = TRUE;
    inst->gadgetActive = TRUE;

    uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);

    uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, TRUE);
    return GMR_MEACTIVE;
}





/* =========================================================================
 * GM_HANDLEINPUT  (mouse moves / button release) – input.device context
 * =========================================================================
 */
ULONG UniTextEditor_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    struct InputEvent *ie   = msg->gpi_IEvent;



    if (!ie) return GMR_MEACTIVE;

//bdbprintf("UniTextEditor_OnHandleInput %08x\n",(int)ie->ie_Class);

    if (ie->ie_Class == IECLASS_RAWKEY && !inst->useInternalRawKey) {
        /* External mode: keys belong to the window event loop, not here.
         * Replay so the window IDCMP receives the event. */
        return GMR_REUSE;
    }

    if(inst->useInternalRawKey && ie->ie_Class == IECLASS_RAWKEY)
    {
        if (inst->rawKeySendBack) {
            inst->rawKeyLastCode = ((ULONG)ie->ie_Qualifier << 16) | (ULONG)ie->ie_Code;
            uted_notify(cl, o, msg->gpi_GInfo, UTED_InternalRawKey_Code, inst->rawKeyLastCode);
        }

        /* Amiga+key = menu shortcut: let Intuition handle it, app re-activates after MENUPICK */
        /* also ctrl */
        if (ie->ie_Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND | IEQUALIFIER_CONTROL))
            return GMR_REUSE;


        /* Defer to application-task context (GM_RENDER) where FreeType is safe */
        if (inst->pendingKeyCount < UTED_PENDING_KEY_MAX) {
            inst->pendingKeys[inst->pendingKeyCount] = *ie;
            inst->pendingKeyCount++;
        }
        uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
        return GMR_MEACTIVE;
    }

    if (ie->ie_Class == IECLASS_RAWMOUSE) {

        if (ie->ie_Code == IECODE_LBUTTON) {
            /* New left-button press while we are the active gadget.
             * If the click is outside our bounds, release focus and let
             * intuition replay the event so the real target gets clicked.
             * GMR_REUSE = non-zero (deactivate) + re-insert the event. */
            WORD gw = ((struct Gadget *)o)->Width;
            WORD gh = ((struct Gadget *)o)->Height;
            if (msg->gpi_Mouse.X < 0 || msg->gpi_Mouse.Y < 0 ||
                msg->gpi_Mouse.X >= gw || msg->gpi_Mouse.Y >= gh)
            {
                inst->dragging     = FALSE;
                inst->gadgetActive = FALSE;
                //uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, FALSE);
                return GMR_REUSE;
            }
            /* Scrollbar gets priority over text interaction */
            if (uted_hit_vscroll(inst, msg->gpi_Mouse.X, msg->gpi_Mouse.Y)) {
                inst->vScrollDragging     = TRUE;
                inst->pendingVScrollClick = TRUE;
                inst->pendingVScrollY     = msg->gpi_Mouse.Y;
                inst->pendingDrag         = FALSE;
                inst->dragging            = TRUE;
                uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
                return GMR_MEACTIVE;
            }
            /* Click inside: treat as a new click (cursor repositioning) */
            inst->pendingClick           = TRUE;
            inst->pendingClickX          = msg->gpi_Mouse.X;
            inst->pendingClickY          = msg->gpi_Mouse.Y;
            inst->pendingClickQualifiers = ie->ie_Qualifier;
            inst->pendingDrag            = FALSE;
            inst->dragging               = TRUE;
            uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
            return GMR_MEACTIVE;
        }

        if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
            inst->dragging = FALSE;
            if (inst->vScrollDragging) {
                inst->vScrollDragging    = FALSE;
                inst->pendingVScrollDrag = TRUE;
                inst->pendingVScrollY    = msg->gpi_Mouse.Y;
            } else {
                inst->pendingDrag      = TRUE;
                inst->pendingDragType  = UTED_MPEND_RELEASE;
                inst->pendingDragX     = msg->gpi_Mouse.X;
                inst->pendingDragY     = msg->gpi_Mouse.Y;
            }
            uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
            if (!inst->useInternalRawKey) {
                /* External mode: release Boopsi activation so the window IDCMP
                 * receives keyboard events again.  Do NOT clear private activation —
                 * the app keeps routing keys to us via UTED_PutRawKey. */
                return GMR_NOREUSE;
            }
            /* Internal mode: stay Boopsi-active so RAWKEY events keep arriving. */
            return GMR_MEACTIVE;
        }

        if (ie->ie_Code == IECODE_RBUTTON) {
            /* Right button: release focus and replay so window menus work */
            inst->dragging     = FALSE;
            inst->gadgetActive = FALSE;
            //uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, FALSE);
            return GMR_REUSE;
        }

        if (ie->ie_Code == (IECODE_RBUTTON | IECODE_UP_PREFIX)) {
            /* Right button release: in external-key mode the window owns the
             * keyboard, so deactivate and let the window see the event.
             * In internal mode the app re-activates us after IDCMP_MENUPICK,
             * so there is nothing to do here. */
            if (!inst->useInternalRawKey) {
                inst->dragging     = FALSE;
                inst->gadgetActive = FALSE;
                //no uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, FALSE);
                return GMR_REUSE;
            }
        }

        if (inst->dragging) {
            if (inst->vScrollDragging) {
                inst->pendingVScrollDrag = TRUE;
                inst->pendingVScrollY    = msg->gpi_Mouse.Y;
            } else {
                inst->pendingDrag      = TRUE;
                inst->pendingDragType  = UTED_MPEND_DRAG;
                inst->pendingDragX     = msg->gpi_Mouse.X;
                inst->pendingDragY     = msg->gpi_Mouse.Y;
            }
            uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
        }
    }

    return GMR_MEACTIVE;
}

/* =========================================================================
 * GM_GOINACTIVE – input.device context, no API calls
 * =========================================================================
 */
ULONG UniTextEditor_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    (void)msg;

// bdbprintf("Inactive %08x\n",(int)o);

    inst->dragging           = FALSE;
    inst->clickAnchorValid   = FALSE;
    inst->vScrollDragging    = FALSE;
    /* Discard any pending input that never reached GM_RENDER */
    inst->pendingClick        = FALSE;
    inst->pendingDrag         = FALSE;
    inst->pendingKeyCount     = 0;
    inst->pendingVScrollClick = FALSE;
    inst->pendingVScrollDrag  = FALSE;
    return 0;
}
