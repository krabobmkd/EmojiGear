/*
 * mmgtabs.c – Tab management for MUImojiGear.
 *
 * Design: the MUIC_Register object (mmgRegisterObj) owns the editor as a
 * direct child – i.e., the editor IS one of the Register pages.  All other
 * pages are lightweight Rectangle placeholders.  On a tab switch, we use
 * MUIM_Group_MoveMember to move the editorObj to the new active page index
 * so it is always the visible page, then switch UTED_CurrentContext to load
 * the right text buffer.
 *
 * This avoids the "empty pages take up half the layout" problem that occurs
 * when the editor lives outside the Register: here every page has the same
 * maximum size (the editor's size when it is in that slot), so MUI's page
 * area is always correctly sized.
 */

#include "mmgtabs.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <libraries/mui.h>
#include <gadgets/unitexteditor.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../EmojiGear/eglocale.h"
#include "muimojiGear.h"   /* MUI_NewObjectB */

/* =========================================================================
 * Tab state
 * =========================================================================
 */
int    mmgTabCount        = 0;
int    mmgTabCurrentIndex = 0;
int    mmgTabNewSerial    = 0;
char   mmgTabLabels[MMG_MAX_TABS][64];
STRPTR mmgTabTitles[MMG_MAX_TABS + 1];
char  *mmgTabContextNames[MMG_MAX_TABS];

/* =========================================================================
 * MUI object pointers – filled by muimojiGear.c after object creation
 * =========================================================================
 */
Object *mmgRegisterObj = NULL;
Object *mmgEditorObj   = NULL;
Object *mmgWinObj      = NULL;

/* UI refresh callback */
void (*mmgOnUIChanged)(void) = NULL;

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

static void getRawEditorWin(struct Gadget **gOut, struct Window **wOut)
{
    *gOut = NULL; *wOut = NULL;
    if (mmgEditorObj) GetAttr(MUIA_Boopsi_Object, mmgEditorObj, (ULONG *)gOut);
    if (mmgWinObj)    GetAttr(MUIA_Window_Window,  mmgWinObj,   (ULONG *)wOut);
}

/* =========================================================================
 * Public API
 * =========================================================================
 */

int MmgTabs_Init(void)
{
    mmgTabContextNames[0] = (char *)AllocVec(8, MEMF_ANY);
    if (!mmgTabContextNames[0]) return 0;

    mmgTabNewSerial = 1;
    strcpy(mmgTabContextNames[0], ":n:1");
    MmgTabs_FillLabel(mmgTabLabels[0], sizeof(mmgTabLabels[0]), ":n:1");
    mmgTabTitles[0]   = mmgTabLabels[0];
    mmgTabTitles[1]   = NULL;
    mmgTabCount       = 1;
    mmgTabCurrentIndex = 0;
    return 1;
}

void MmgTabs_FillLabel(char *buf, ULONG bufsz, const char *key)
{
    if (!key || key[0] == '\0') {
        strncpy(buf, LOC(MSG_TAB_NEW_FILE), bufsz - 1);
    } else if (strncmp(key, ":n:", 3) == 0) {
        int n = atoi(key + 3);
        if (n <= 1)
            strncpy(buf, LOC(MSG_TAB_NEW_FILE), bufsz - 1);
        else
            snprintf(buf, bufsz, "%s %d", LOC(MSG_TAB_NEW_FILE), n);
    } else {
        const char *fp = strrchr(key, '/');
        fp = fp ? fp + 1 : key;
        strncpy(buf, fp, bufsz - 1);
        { char *p = buf; while (*p) { if (*p == '_') *p = ' '; p++; } }
    }
    buf[bufsz - 1] = '\0';
}

void MmgTabs_SwitchTo(int newIdx)
{
    struct Gadget *g; struct Window *w;
    const char    *ctxKey;
    ULONG          activePage = 0;

    if (newIdx < 0 || newIdx >= mmgTabCount || newIdx == mmgTabCurrentIndex)
        return;

    mmgTabCurrentIndex = newIdx;
    ctxKey = mmgTabContextNames[newIdx] ? mmgTabContextNames[newIdx] : "";

    /* Move editor to position newIdx within the Register so it becomes the
     * visible page.  pos=0 → first child; pos=N → after Nth child (1-based).
     * Both map to 0-indexed final position newIdx. */
    DoMethod(mmgRegisterObj, MUIM_Group_InitChange);
    DoMethod(mmgRegisterObj, MUIM_Group_MoveMember,
             (ULONG)mmgEditorObj, (ULONG)newIdx);
    /* Only update ActivePage if the Register hasn't already changed it
     * (e.g., we were called from the ActivePage notification). */
    GetAttr(MUIA_Group_ActivePage, mmgRegisterObj, &activePage);
    if ((int)activePage != newIdx)
        SetAttrs(mmgRegisterObj, MUIA_Group_ActivePage, (ULONG)newIdx, TAG_DONE);
    DoMethod(mmgRegisterObj, MUIM_Group_ExitChange);

    getRawEditorWin(&g, &w);
    if (g && w) {
        SetGadgetAttrs(g, w, NULL,
                       UTED_CurrentContext, (ULONG)ctxKey, TAG_DONE);
        RefreshGList(g, w, NULL, 1);
    }

    if (mmgOnUIChanged) mmgOnUIChanged();
}

void MmgTabs_Add(const char *key)
{
    Object *placeholder;
    char   *keyCopy;
    int     newIdx;
    struct Gadget *g; struct Window *w;

    if (!key || mmgTabCount >= MMG_MAX_TABS) return;
    if (!mmgRegisterObj || !mmgEditorObj) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(key) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, key);

    MmgTabs_FillLabel(mmgTabLabels[mmgTabCount], sizeof(mmgTabLabels[0]), key);
    mmgTabContextNames[mmgTabCount] = keyCopy;
    mmgTabTitles[mmgTabCount]       = mmgTabLabels[mmgTabCount];
    mmgTabTitles[mmgTabCount + 1]   = NULL;

    /* Placeholder that occupies the new slot in the Register child list when
     * the editor is elsewhere.  MUI disposes it when the application exits. */
    placeholder = MUI_NewObjectB(MUIC_Rectangle, TAG_DONE);
    if (!placeholder) {
        FreeVec(keyCopy);
        mmgTabContextNames[mmgTabCount] = NULL;
        mmgTabTitles[mmgTabCount]       = NULL;
        return;
    }

    newIdx = mmgTabCount;
    mmgTabCount++;
    mmgTabCurrentIndex = newIdx;

    /* Atomically: append placeholder, refresh titles, move editor to newIdx,
     * set the active page.  InitChange suppresses redraws during the change. */
    DoMethod(mmgRegisterObj, MUIM_Group_InitChange);
    DoMethod(mmgRegisterObj, MUIM_Group_AddTail,    (ULONG)placeholder);
    SetAttrs(mmgRegisterObj, MUIA_Register_Titles,  (ULONG)mmgTabTitles, TAG_DONE);
    DoMethod(mmgRegisterObj, MUIM_Group_MoveMember,
             (ULONG)mmgEditorObj, (ULONG)newIdx);
    SetAttrs(mmgRegisterObj, MUIA_Group_ActivePage, (ULONG)newIdx, TAG_DONE);
    DoMethod(mmgRegisterObj, MUIM_Group_ExitChange);

    /* Switch editor to the fresh empty context */
    getRawEditorWin(&g, &w);
    if (g && w) {
        SetGadgetAttrs(g, w, NULL,
                       UTED_CurrentContext, (ULONG)mmgTabContextNames[newIdx],
                       UTED_Text,           (ULONG)"",
                       TAG_DONE);
        RefreshGList(g, w, NULL, 1);
    }

    if (mmgOnUIChanged) mmgOnUIChanged();
}

void MmgTabs_NewFile(void)
{
    char key[32];
    mmgTabNewSerial++;
    snprintf(key, sizeof(key), ":n:%d", mmgTabNewSerial);
    MmgTabs_Add(key);
}

void MmgTabs_Close(void)
{
    int i;
    for (i = 0; i < mmgTabCount; i++) {
        FreeVec(mmgTabContextNames[i]);
        mmgTabContextNames[i] = NULL;
    }
    mmgTabCount = 0;
}
