/*
 * mmgtabs.c – Tab management for MUImojiGear.
 *
 * Owns the tab state and implements all tab operations.
 * Interacts with the MUI/BOOPSI layer through three object pointers
 * (mmgRegisterObj, mmgEditorObj, mmgWinObj) that muimojiGear.c sets
 * after object creation, and a UI-refresh callback (mmgOnUIChanged).
 */

#include "mmgtabs.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <libraries/mui.h>
#include <gadgets/unitexteditor.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../EmojiGear/eglocale.h"

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

/* Retrieve the raw Intuition gadget and window pointers from MUI objects.
 * Needed for SetGadgetAttrs() / RefreshGList() which require the real
 * Intuition structs, not the MUI wrapper objects. */
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

    getRawEditorWin(&g, &w);
    if (g && w) {
        SetGadgetAttrs(g, w, NULL,
                       UTED_CurrentContext, (ULONG)ctxKey, TAG_DONE);
        RefreshGList(g, w, NULL, 1);
    }

    /* Sync Register visual selection; guard against notification feedback loop */
    if (mmgRegisterObj) {
        GetAttr(MUIA_Group_ActivePage, mmgRegisterObj, &activePage);
        if ((int)activePage != newIdx)
            SetAttrs(mmgRegisterObj, MUIA_Group_ActivePage, (ULONG)newIdx, TAG_DONE);
    }

    if (mmgOnUIChanged) mmgOnUIChanged();
}

void MmgTabs_Add(const char *key)
{
    char   *keyCopy;
    Object *newPage;
    int     newIdx;
    struct Gadget *g; struct Window *w;

    if (!key || mmgTabCount >= MMG_MAX_TABS) return;
    if (!mmgRegisterObj) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(key) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, key);

    MmgTabs_FillLabel(mmgTabLabels[mmgTabCount], sizeof(mmgTabLabels[0]), key);
    mmgTabContextNames[mmgTabCount] = keyCopy;
    mmgTabTitles[mmgTabCount]       = mmgTabLabels[mmgTabCount];
    mmgTabTitles[mmgTabCount + 1]   = NULL;

    /* Empty placeholder page: VertWeight 0 so it contributes no height */
    newPage = MUI_NewObjectA(MUIC_Rectangle,
        (struct TagItem []) {
            { MUIA_VertWeight, 0 },
            { TAG_DONE,        0 }
        });
    if (!newPage) {
        FreeVec(keyCopy);
        mmgTabContextNames[mmgTabCount] = NULL;
        mmgTabTitles[mmgTabCount]       = NULL;
        return;
    }

    newIdx = mmgTabCount;
    mmgTabCount++;

    /* Insert the new page and update titles atomically */
    DoMethod(mmgRegisterObj, MUIM_Group_InitChange);
    DoMethod(mmgRegisterObj, OM_ADDMEMBER, (ULONG)newPage);
    SetAttrs(mmgRegisterObj, MUIA_Register_Titles, (ULONG)mmgTabTitles, TAG_DONE);
    DoMethod(mmgRegisterObj, MUIM_Group_ExitChange);

    /* Switch editor to the fresh empty context */
    mmgTabCurrentIndex = newIdx;
    getRawEditorWin(&g, &w);
    if (g && w) {
        SetGadgetAttrs(g, w, NULL,
                       UTED_CurrentContext, (ULONG)mmgTabContextNames[newIdx],
                       UTED_Text,           (ULONG)"",
                       TAG_DONE);
        RefreshGList(g, w, NULL, 1);
    }
    SetAttrs(mmgRegisterObj, MUIA_Group_ActivePage, (ULONG)newIdx, TAG_DONE);

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
