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

    /* Menu items (kept for DoMethod notifications) */
    Object    *miNewFile;
    Object    *miLoadUTF8, *miLoadLat1, *miLoadLat2;
    Object    *miSaveUTF8, *miSaveLat1, *miSaveLat2;
    Object    *miQuit;
    Object    *miCut, *miCopy, *miPaste, *miUndo, *miRedo;

    /* Application settings (font paths, rendering flags, …) */
    AppSettings settings;
};

/* Single global instance – defined in muimojiGear.c */
extern struct App *app;

/* =========================================================================
 * Utility functions used by multiple modules
 * =========================================================================
 */

/* Retrieve the raw Intuition gadget + window from the MUI Boopsi wrapper.
 * Needed for SetGadgetAttrs / RefreshGList calls on the editor. */
void App_GetRawEditorWin(struct Gadget **gOut, struct Window **wOut);

/* Refresh the status bar from the editor's current UTED_Modified state. */
void App_UpdateStatus(void);

#endif /* MUIMOJIGEAR_H */
