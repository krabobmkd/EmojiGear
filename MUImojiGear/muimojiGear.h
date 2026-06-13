#ifndef MUIMOJIGEAR_H
#define MUIMOJIGEAR_H

/*
 * muimojiGear.h – Central application state for MUImojiGear.
 *
 * All modules that need access to MUI objects or app settings include this
 * header and reference the single global `app` pointer, mirroring the
 * EmojiGear pattern (emojigear.h / extern struct App *app).
 */

#include <exec/types.h>
#include <intuition/classusr.h>   /* Object * */
#include "mmgsettings.h"

/* =========================================================================
 * Application struct – owns every MUI object and the settings block.
 * =========================================================================
 */
struct App {
    /* Core MUI objects */
    Object    *appObj;
    Object    *winObj;
    Object    *editorObj;
    Object    *statusObj;
    Object    *vScrollBar;
    Object    *hScrollBar;
    Object    *emojiBtnObj;

    /* Font settings sub-window */
    Object    *fontViewWinObj;
    Object    *miFontSettings;

    /* Emoji box sub-window objects */
    Object    *emojiBoxWinObj;   /* MUIC_Window – second SubWindow of the App */
    Object    *emojiBoxCycleObj; /* MUIC_Cycle  – emoji set selector           */
    Object    *emojiBoxGridObj;  /* MUIC_Boopsi – EmojiGrid gadget             */

    /* Menu items (kept for DoMethod notifications) */
    Object    *miNewFile;
    Object    *miLoadUTF8, *miLoadLat1, *miLoadLat2;
    Object    *miSaveUTF8, *miSaveLat1, *miSaveLat2;
    Object    *miQuit;
    Object    *miOpenRecent;                              /* "Open Recent" parent submenu item */
    Object    *miRecentItem[APPSETTINGS_MAX_RECENT];      /* one per slot */
    char       recentItemLabels[APPSETTINGS_MAX_RECENT][64]; /* stable buffers for MUI title ptrs */

    Object    *miColorPreset[6];     /* Colors menu – one item per preset (0 = System colors) */
    Object    *miSettFontSizePlus,  *miSettFontSizeMinus;
    Object    *miToggleAntialias,   *miToggleMonospace, *miToggleWordWrap;
    Object    *miToggleApplyAnsi,   *miToggleVisualizeTabs, *miToggleTabsAreSpaces;
    Object    *miToggleDisplayUnicodeInfo;
    Object    *miCut, *miCopy, *miCopyLat1, *miCopyLat2;
    Object    *miPaste, *miPasteLat1, *miPasteLat2;
    Object    *miUndo, *miRedo;
    Object    *miEmojiBox;

    /* Application settings (font paths, rendering flags, …) */
    AppSettings settings;
};

/* Single global instance – defined in muimojiGear.c */
extern struct App *app;

/* GCC-safe MUI_NewObject wrapper – defined in muimojiGear.c, shared across modules */
Object *MUI_NewObjectB(const char *cl, Tag tags, ...);

/* =========================================================================
 * Utility functions used by multiple modules
 * =========================================================================
 */

/* Retrieve the raw Intuition gadget + window from the MUI Boopsi wrapper.
 * Needed for SetGadgetAttrs / RefreshGList calls on the editor. */
void App_GetRawEditorWin(struct Gadget **gOut, struct Window **wOut);

/* Refresh the status bar from the editor's current UTED_Modified state. */
void App_UpdateStatus(void);

/* Re-sync both scrollbar domains from the current editor state.
 * Call after any operation that may change line count or visible area
 * (word-wrap toggle, font size change, etc.). */
void App_SyncScrollbars(void);

/* Re-activate the editor via MUIA_Window_ActiveObject after a menu action
 * has stripped BOOPSI keyboard focus from the gadget. */
void App_ReactivateEditor(void);

#endif /* MUIMOJIGEAR_H */
