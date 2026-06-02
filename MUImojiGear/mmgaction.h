#ifndef MMGACTION_H
#define MMGACTION_H

/*
 * mmgaction.h – File load / save actions for MUImojiGear.
 *
 * Adapted from EmojiGear/egaction.c.  All EmojiGear-specific dependencies
 * (struct App, tabs, BMainWindow, EgMenu_Rebuild) have been removed.
 *
 * Call MmgAction_Init() once after MUI objects are created.
 * Then call the MmgAction_Load/Save functions from the event loop.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include "mmgsettings.h"

/* File load / save actions.
 * These functions access app->editorObj, app->winObj and app->settings
 * directly via the global `app` pointer (muimojiGear.h / muimojiGear.c).
 * No separate Init call is needed. */
BOOL MmgAction_LoadUTF8(void);
BOOL MmgAction_LoadLatin1(void);
BOOL MmgAction_LoadLatin2(void);

BOOL MmgAction_SaveUTF8(void);
BOOL MmgAction_SaveLatin1(void);
BOOL MmgAction_SaveLatin2(void);

#endif /* MMGACTION_H */
