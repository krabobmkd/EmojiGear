/*
 * egtabs_safe.c – functional tab API for OS3.9 (no clicktab.gadget).
 *
 * Mirrors egtabs.c in logic but drives the FakeTab BOOPSI class instead of
 * the real clicktab.gadget.  No AllocClickTabNode / FreeClickTabNode calls;
 * tab list nodes are plain AllocVec'd struct Node items with ln_Name pointing
 * to the corresponding tabLabels[] buffer.
 *
 * The FakeTab class handles CLICKTAB_Labels (rebuilds button children) and
 * CLICKTAB_Current (stores the active index) in its own OM_SET dispatcher,
 * so this file only needs to maintain the app metadata arrays and call
 * SetGadgetAttrs / SetAttrs with those two attributes.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <gadgets/clicktab.h>   /* CLICKTAB_Current, CLICKTAB_Labels – tag numbers only */
#include <classes/window.h>     /* WM_RETHINK */

#include "emojigear.h"
#include "eglocale.h"
#include "boopsimainwindow.h"
#include "egtabs.h"

#include <gadgets/unitexteditor.h>

/* -------------------------------------------------------------------------
 * Underscore → space (same reason as in egtabs.c)
 * -------------------------------------------------------------------------*/
static void egSafe_ReplaceUnderscores(char *buf)
{
    for (; *buf; buf++)
        if (*buf == '_') *buf = ' ';
}

/* -------------------------------------------------------------------------
 * Label fill – identical logic to egTabsReal_FillLabel, no clicktab dep.
 * -------------------------------------------------------------------------*/
static void egSafe_FillLabel(char *buf, ULONG bufsz, const char *key)
{
    if (!key || key[0] == '\0') {
        strncpy(buf, LOC(MSG_TAB_NEW_FILE), bufsz - 1);
        buf[bufsz - 1] = '\0';
    } else if (strncmp(key, ":n:", 3) == 0) {
        int serial = atoi(key + 3);
        if (serial <= 1)
            strncpy(buf, LOC(MSG_TAB_NEW_FILE), bufsz - 1);
        else
            snprintf(buf, bufsz, "%s %d", LOC(MSG_TAB_NEW_FILE), serial);
        buf[bufsz - 1] = '\0';
    } else {
        const char *fp = (const char *)FilePart((STRPTR)key);
        strncpy(buf, fp ? fp : key, bufsz - 1);
        buf[bufsz - 1] = '\0';
        egSafe_ReplaceUnderscores(buf);
    }
}

/* -------------------------------------------------------------------------
 * Rebuild the FakeTab gadget's button children from the current metadata.
 * Allocates a fresh plain-node list, passes it via CLICKTAB_Labels, then
 * replaces app->tabList.  Old list nodes are freed with FreeVec.
 * -------------------------------------------------------------------------*/
static void egSafe_RebuildGadget(void)
{
    struct List *newList;
    int i;

    if (!app) return;

    /* Always rebuild app->tabList so NewObject() sees the correct labels
     * even when called before app->tabGadget exists (startup sequence). */
    newList = (struct List *)AllocVec(sizeof(struct List), MEMF_ANY | MEMF_CLEAR);
    if (!newList) return;
    NewList(newList);

    for (i = 0; i < app->tabCount; i++) {
        struct Node *node = (struct Node *)AllocVec(sizeof(struct Node), MEMF_CLEAR);
        if (node) {
            egSafe_FillLabel(app->tabLabels[i], sizeof(app->tabLabels[i]),
                             app->tabContextNames[i]);
            node->ln_Name = app->tabLabels[i];
            node->ln_Type = NT_USER;
            AddTail(newList, node);
            app->tabNodes[i] = node;
        }
    }

    /* Update the gadget only when it already exists */
    if (app->tabGadget) {
        if (CurrentMainWindow) {
            SetGadgetAttrs((struct Gadget *)app->tabGadget, CurrentMainWindow, NULL,
                           CLICKTAB_Labels,  (ULONG)newList,
                           CLICKTAB_Current, (ULONG)app->tabCurrentIndex,
                           TAG_END);
            DoMethod(app->window_obj, WM_RETHINK);
        } else {
            SetAttrs(app->tabGadget,
                     CLICKTAB_Labels,  (ULONG)newList,
                     CLICKTAB_Current, (ULONG)app->tabCurrentIndex,
                     TAG_END);
        }
    }

    /* Replace old list */
    if (app->tabList) {
        struct Node *n, *next;
        for (n = app->tabList->lh_Head; (next = n->ln_Succ) != NULL; n = next)
            FreeVec(n);
        FreeVec(app->tabList);
    }
    app->tabList = newList;
}

/* -------------------------------------------------------------------------
 * Forward declaration: CloseCurrentTab calls SwitchTo defined below.
 * -------------------------------------------------------------------------*/
static void egTabsSafe_SwitchTo(int newIdx);

/* =========================================================================
 * API implementations
 * =========================================================================*/

static void egTabsSafe_FillLabel(char *buf, ULONG bufsz, const char *key)
{
    egSafe_FillLabel(buf, bufsz, key);
}

static void egTabsSafe_AddOrSelectTab(const char *contextKey)
{
    int i;
    char *keyCopy;

    if (!app || !contextKey) return;

    /* Already open? Just switch. */
    for (i = 0; i < app->tabCount; i++) {
        if (app->tabContextNames[i] &&
            strcmp(app->tabContextNames[i], contextKey) == 0)
        {
            app->tabCurrentIndex = i;
            if (app->tabGadget && CurrentMainWindow)
                SetGadgetAttrs((struct Gadget *)app->tabGadget,
                               CurrentMainWindow, NULL,
                               CLICKTAB_Current, (ULONG)i,
                               TAG_END);
            else if (app->tabGadget)
                SetAttrs(app->tabGadget, CLICKTAB_Current, (ULONG)i, TAG_END);
            return;
        }
    }

    if (app->tabCount >= EG_MAX_TABS) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(contextKey) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, contextKey);

    app->tabContextNames[app->tabCount] = keyCopy;
    app->tabCurrentIndex                = app->tabCount;
    app->tabCount++;

    egSafe_RebuildGadget();
}

static void egTabsSafe_NewTab(void)
{
    char key[64];
    if (!app) return;
    app->tabNewSerial++;
    sprintf(key, ":n:%d", app->tabNewSerial);
    egTabsSafe_AddOrSelectTab(key);
    SetGdAttrs(app->textEditorObj,
               UTED_CurrentContext, (ULONG)key,
               UTED_Text,           (ULONG)"",
               TAG_END);
}

/* Close the tab at closingIdx, whether or not it is the active tab, and
 * synchronize tab state.  Shared by CloseCurrentTab and CloseTabByNode. */
static void egTabsSafe_CloseTabByIndex(int closingIdx)
{
    int nextIdx, i;
    char *closingKey;

    if (!app) return;
    if (closingIdx < 0 || closingIdx >= app->tabCount) return;

    if (app->tabCount == 1) {
        exit(0);
        return;
    }

    closingKey = app->tabContextNames[closingIdx];

    if (closingIdx == app->tabCurrentIndex) {
        nextIdx = (closingIdx + 1 < app->tabCount) ? closingIdx + 1 : closingIdx - 1;
        egTabsSafe_SwitchTo(nextIdx);
    }

    if (closingKey && closingKey[0])
        SetGdAttrs(app->textEditorObj,
                   UTED_DeleteContext, (ULONG)closingKey,
                   TAG_END);
    if (closingKey)
        FreeVec(closingKey);

    for (i = closingIdx; i < app->tabCount - 1; i++) {
        app->tabContextNames[i] = app->tabContextNames[i + 1];
        app->tabNodes[i]        = app->tabNodes[i + 1];
        memcpy(app->tabLabels[i], app->tabLabels[i + 1], sizeof(app->tabLabels[0]));
    }
    app->tabContextNames[app->tabCount - 1] = NULL;
    app->tabNodes[app->tabCount - 1]        = NULL;
    app->tabCount--;

    if (app->tabCurrentIndex > closingIdx)
        app->tabCurrentIndex--;

    egSafe_RebuildGadget();

    SyncVScroller();
    SyncHScroller();
    UpdateStatusBar();
    BMainWindow_SetTitleLocS(&app->mainwindow,
                             MSG_WINDOW_TITLE_WITHFILE,
                             app->tabLabels[app->tabCurrentIndex]);
}

static void egTabsSafe_CloseCurrentTab(void)
{
    if (!app) return;
    if (app->tabCurrentIndex < 0 || app->tabCurrentIndex >= app->tabCount) return;
    egTabsSafe_CloseTabByIndex(app->tabCurrentIndex);
}

/* Close the tab whose tab node is 'node', regardless of whether it is the
 * currently active tab. */
static void egTabsSafe_CloseTabByNode(struct Node *node)
{
    int i;
    if (!app || !node) return;
    for (i = 0; i < app->tabCount; i++) {
        if (app->tabNodes[i] == node) {
            egTabsSafe_CloseTabByIndex(i);
            return;
        }
    }
}

static void egTabsSafe_RenameCurrentTab(const char *newContextKey)
{
    char *keyCopy;
    if (!app || !newContextKey) return;
    if (app->tabCurrentIndex < 0 || app->tabCurrentIndex >= app->tabCount) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(newContextKey) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, newContextKey);

    FreeVec(app->tabContextNames[app->tabCurrentIndex]);
    app->tabContextNames[app->tabCurrentIndex] = keyCopy;

    SetGdAttrs(app->textEditorObj,
               UTED_RenameContext, (ULONG)newContextKey,
               TAG_END);

    egSafe_RebuildGadget();
}

static void egTabsSafe_SwitchTo(int newIdx)
{
    const char *ctxKey;
    if (!app || newIdx < 0 || newIdx >= app->tabCount) return;
    if (newIdx == app->tabCurrentIndex) return;

    app->tabCurrentIndex = newIdx;
    ctxKey = app->tabContextNames[newIdx] ? app->tabContextNames[newIdx] : "";

    if (app->tabGadget && CurrentMainWindow)
        SetGadgetAttrs((struct Gadget *)app->tabGadget, CurrentMainWindow, NULL,
                       CLICKTAB_Current, (ULONG)newIdx, TAG_END);
    else if (app->tabGadget)
        SetAttrs(app->tabGadget, CLICKTAB_Current, (ULONG)newIdx, TAG_END);

    SetGdAttrs(app->textEditorObj,
               UTED_CurrentContext, (ULONG)ctxKey,
               TAG_END);
    if (CurrentMainWindow)
        RefreshGList(app->textEditorObj, CurrentMainWindow, NULL, 1);
    SyncVScroller();
    SyncHScroller();
    UpdateStatusBar();
    BMainWindow_SetTitleLocS(&app->mainwindow,
                             MSG_WINDOW_TITLE_WITHFILE,
                             app->tabLabels[newIdx]);
}

/* -------------------------------------------------------------------------
 * Exported vtable
 * -------------------------------------------------------------------------*/
const EgTabsAPI EgTabsSafeAPI = {
    egTabsSafe_FillLabel,
    egTabsSafe_AddOrSelectTab,
    egTabsSafe_NewTab,
    egTabsSafe_RenameCurrentTab,
    egTabsSafe_CloseCurrentTab,
    egTabsSafe_CloseTabByNode,
    egTabsSafe_SwitchTo,
};
