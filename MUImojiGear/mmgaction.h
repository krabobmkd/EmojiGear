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
BOOL MmgAction_LoadFromPath(const char *path, int encoding);
BOOL MmgAction_LoadUTF8(void);
BOOL MmgAction_LoadLatin1(void);
BOOL MmgAction_LoadLatin2(void);

BOOL MmgAction_SaveUTF8(void);
BOOL MmgAction_SaveLatin1(void);
BOOL MmgAction_SaveLatin2(void);

BOOL MmgAction_FontSizePlus(void);
BOOL MmgAction_FontSizeMinus(void);

/* Boolean setting toggles – read MUIA_Menuitem_Checked, update app->settings, apply */
/* Apply a named color preset (index 0-4) to the editor */
BOOL MmgAction_ApplyColorPreset(int idx);

/* Rebuild the "Open Recent" submenu items from app->settings.recentFiles[].
 * Call after any load/save that changes the recent list, and once at startup. */
void MmgAction_RebuildRecentMenu(void);

/* Open the recent file at the given slot (0 = most recent). */
BOOL MmgAction_OpenRecentFile(int slot);

BOOL MmgAction_ToggleAntialias(void);
BOOL MmgAction_ToggleMonospace(void);
BOOL MmgAction_ToggleWordWrap(void);
BOOL MmgAction_ToggleApplyAnsi(void);
BOOL MmgAction_ToggleVisualizeTabs(void);
BOOL MmgAction_ToggleTabsAreSpaces(void);

BOOL MmgAction_CopyLatin1(void);
BOOL MmgAction_CopyLatin2(void);
BOOL MmgAction_PasteLatin1(void);
BOOL MmgAction_PasteLatin2(void);

void MmgAction_ApplyColorFromSettings();

#endif /* MMGACTION_H */
