#ifndef MMGTABS_H
#define MMGTABS_H

/*
 * mmgtabs.h – Tab management for MUImojiGear.
 *
 * Owns the tab state (count, labels, context keys, MUIA_Register_Titles array)
 * and all operations on it (init, add, switch, close).
 *
 * Contract with muimojiGear.c:
 *   1. Call MmgTabs_Init() BEFORE creating MUI objects – it fills
 *      mmgTabTitles[] and mmgTabContextNames[] which are needed as
 *      initialisation arguments for the Register and the editor.
 *   2. After the MUI objects exist, set mmgRegisterObj, mmgEditorObj,
 *      mmgWinObj and mmgOnUIChanged so tab operations can update the UI.
 *   3. Call MmgTabs_Close() at application exit to free context strings.
 */

#include <exec/types.h>
#include <intuition/classusr.h>

#define MMG_MAX_TABS 20

/* ---- Tab state (defined in mmgtabs.c, read by muimojiGear.c) ----------- */
extern int    mmgTabCount;
extern int    mmgTabCurrentIndex;
extern int    mmgTabNewSerial;
extern char   mmgTabLabels[MMG_MAX_TABS][64];  /* persistent label strings  */
extern STRPTR mmgTabTitles[MMG_MAX_TABS + 1];   /* NULL-terminated, passed to
                                                   MUIA_Register_Titles      */
extern char  *mmgTabContextNames[MMG_MAX_TABS]; /* AllocVec'd context keys   */

/* ---- MUI object pointers (set by muimojiGear.c after object creation) -- */
extern Object *mmgRegisterObj;  /* MUIC_Register                            */
extern Object *mmgEditorObj;    /* MUIC_Boopsi wrapping UniTextEditor        */
extern Object *mmgWinObj;       /* MUIC_Window                               */

/* ---- UI refresh callback (set by muimojiGear.c) ------------------------ */
/* Called by tab operations after state changes so status bar / window title
 * are kept in sync.  Set this before calling any tab operation that may
 * change visible state. */
extern void (*mmgOnUIChanged)(void);

/* ---- Public API --------------------------------------------------------- */

/* Initialise tab state with one synthetic "New File" tab.
 * Fills mmgTabTitles[0] and mmgTabContextNames[0].
 * Returns 0 on allocation failure. */
int  MmgTabs_Init(void);

/* Build a human-readable label from a context key into buf[bufsz].
 * Synthetic ":n:N" → "New File" / "New File N".
 * Real path → filename component, underscores replaced by spaces. */
void MmgTabs_FillLabel(char *buf, ULONG bufsz, const char *key);

/* Switch the editor context and Register visual selection to newIdx.
 * No-op if newIdx is already current or out of range.
 * Requires mmgRegisterObj, mmgEditorObj, mmgWinObj to be set. */
void MmgTabs_SwitchTo(int newIdx);

/* Add a new tab with the given context key and switch to it.
 * Requires mmgRegisterObj, mmgEditorObj, mmgWinObj to be set. */
void MmgTabs_Add(const char *key);

/* Open a fresh synthetic "New File N" context as a new tab. */
void MmgTabs_NewFile(void);

/* Free all AllocVec'd context-name strings. */
void MmgTabs_Close(void);

#endif /* MMGTABS_H */
