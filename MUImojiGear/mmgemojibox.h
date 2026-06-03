/*
 * mmgemojibox.h – MUImojiGear emoji box: F-key emoji presets + insert popup.
 *
 * Layout (4 modifier banks × 10 F-keys = 40 cells per set):
 *   F1-F10            → row 0  (no modifier)
 *   Shift+F1-F10      → row 1
 *   Alt+F1-F10        → row 2  (Ctrl replaced with Alt: Ctrl goes to MUI menus)
 *   Shift+Alt+F1-F10  → row 3
 *
 * The popup is a second MUIC_Window SubWindow of the Application that can be
 * opened/closed independently.  A MUIC_Cycle selects the active emoji set.
 * A private BOOPSI gadget (EmojiGrid) draws the 11×5 grid and reports clicks
 * through the existing MmgBoopsi notification queue (SIGBREAKF_CTRL_F path).
 * ANSI escape shortcut buttons sit below the grid.
 */

#ifndef MMGEMOJIBOX_H
#define MMGEMOJIBOX_H

#include <exec/types.h>
#include <intuition/classusr.h>   /* Object * */

/* =========================================================================
 * EmojiGrid BOOPSI gadget – private class, tag definitions exported so
 * muimojiGear.c can set up MUIA_Boopsi_Remember and MUIM_Notify.
 * =========================================================================
 */
#define EGRID_Dummy          (TAG_USER | 0x7400)

/* [IS] (const char **) Pointer to 40-entry UTF-8 string array (current set). */
#define EGRID_EmojiSet       (EGRID_Dummy + 1)

/* [G]  (ULONG) Index (0-39) of the last clicked emoji cell; -1 = none.
 *      Read from the main task after RID_EMOJI_GRID_CLICK fires. */
#define EGRID_ClickedIdx     (EGRID_Dummy + 3)

/* [G]  (ULONG) Monotonically increasing click counter.  Use this tag with
 *      MUIA_Boopsi_Remember + MUIM_Notify, MUIV_EveryTime to detect every
 *      click reliably (same-cell double-clicks always increment). */
#define EGRID_ClickSeq       (EGRID_Dummy + 4)

/* [IS] (struct URPDrawContext *) Emoji rendering context.  Set once after
 *      the application window is open (retained by the grid, release on
 *      dispose).  Obtained from app->emojiBtnObj via UBT_URPDrawContext. */
#define EGRID_URPDrawContext (EGRID_Dummy + 5)

/* Number of emoji sets shown in the cycle gadget */
#define EMOJIBOX_NUM_SETS  11

/* =========================================================================
 * MUI Application ReturnID values owned by the emoji box module.
 * Define here so both mmgemojibox.c and muimojiGear.c share the same values.
 * =========================================================================
 */
#define RID_EMOJI_SET_CHANGE  51          /* MUIA_Cycle_Active changed          */
#define RID_EMOJI_ANSI_BASE   52          /* 52..62: one per ANSI button (×11)  */
#define MMG_NUM_ANSI_BTNS     11
#define RID_EMOJI_BOX_MENU    63          /* "Emoji Box" Edit menu item         */

/* =========================================================================
 * Public API
 * =========================================================================
 */

/* Build the emoji box MUIC_Window (call BEFORE MUI Application creation so
 * it can be passed as MUIA_Application_Window).  Returns NULL on failure.
 * The grid BOOPSI class is created here and freed by MmgEmojiBox_Dispose(). */
Object *MmgEmojiBox_BuildWindow(void);

/* Wire MUI notifications (MUIM_Notify for cycle, ANSI buttons, grid click);
 * load the URPDrawContext from app->emojiBtnObj.
 * Call AFTER app->appObj is created. */
void MmgEmojiBox_Init(void);

/* Free the grid BOOPSI class and release the URPDrawContext.
 * Call AFTER MUI_DisposeObject(appObj). */
void MmgEmojiBox_Dispose(void);

/* Open / close the emoji box window (sets MUIA_Window_Open). */
void MmgEmojiBox_Open(void);
void MmgEmojiBox_Close(void);

/* Handle an F-key press from the UTED_InternalRawKey_Code notification.
 *   code      – raw RAWKEY code (0x50..0x59 = F1..F10)
 *   qualifiers – ie_Qualifier bits from the packed notification value     */
void MmgEmojiBox_HandleFKey(UWORD code, UWORD qualifiers);

/* Insert the emoji at grid index <cidx> (0-39) into the active editor.
 * Call from the SIGBREAKF_CTRL_F handler when EGRID_ClickedIdx arrives in
 * the BOOPSI notification queue (pass tag->ti_Data directly as cidx). */
void MmgEmojiBox_HandleGridClick(int cidx);

/* Update the grid gadget to show emoji set <idx> (0..EMOJIBOX_NUM_SETS-1).
 * Call from the main event loop when RID_EMOJI_SET_CHANGE fires. */
void MmgEmojiBox_HandleSetChange(void);

/* Return the ANSI escape string for button index <idx> (0..10).
 * Call from the main event loop when RID_EMOJI_ANSI_BASE+idx fires. */
const char *MmgEmojiBox_GetAnsiString(int idx);

#endif /* MMGEMOJIBOX_H */
