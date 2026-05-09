/*
 * egtabs.c – tab bar management for EmojiGear.
 *
 * Manages multiple editor contexts via the ClickTab gadget.
 * Synthetic untitled context keys start with ':' (impossible in AmigaOS paths).
 * Real file paths are used verbatim as context keys.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <proto/clicktab.h>
#include <gadgets/clicktab.h>

#include "emojigear.h"
#include "eglocale.h"
#include "boopsimainwindow.h"
#include "egtabs.h"

#include <gadgets/unitexteditor.h>

extern struct Library *ClickTabBase;

/* Replace underscores with spaces: BOOPSI treats '_x' as a keyboard
 * accelerator that steals vanilla key events from the editor. */
static void egReplaceUnderscores(char *buf)
{
    for (; *buf; buf++)
        if (*buf == '_') *buf = ' ';
}

/* Switch to tab by absolute index. No-op if already current or out of range. */

/* Fill buf with a human-readable tab label for the given context key.
 * Synthetic keys ":n:N": N==1 → "New File"; N>1 → "New File N".
 * Real paths: just the filename part, with underscores replaced by spaces. */
void EgTabs_FillLabel(char *buf, ULONG bufsz, const char *key)
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
        egReplaceUnderscores(buf);
    }
}

/* Rebuild the clicktab gadget's label list from tabContextNames[]. */
static void egTabsRebuildGadget(void)
{
    struct List *newList;
    int i;

    if (!app || !ClickTabBase) return;

    newList = (struct List *)AllocVec(sizeof(struct List), MEMF_ANY | MEMF_CLEAR);
    if (!newList) return;
    NewList(newList);

    for (i = 0; i < app->tabCount; i++) {
        struct Node *node;
        EgTabs_FillLabel(app->tabLabels[i], sizeof(app->tabLabels[i]),
                       app->tabContextNames[i]);
        node = AllocClickTabNode(
            TNA_Text,   (ULONG)app->tabLabels[i],
            TNA_Number, i,
            TAG_END);
        if (node) {
            AddTail(newList, node);
            app->tabNodes[i] = node;
        }
    }

    if (app->tabGadget && CurrentMainWindow) {
        SetGadgetAttrs((struct Gadget *)app->tabGadget,
                       CurrentMainWindow, NULL,
                       CLICKTAB_Labels,  (ULONG)newList,
                       CLICKTAB_Current, (ULONG)app->tabCurrentIndex,
                       TAG_END);
    } else if (app->tabGadget) {
        SetAttrs(app->tabGadget,
                 CLICKTAB_Labels,  (ULONG)newList,
                 CLICKTAB_Current, (ULONG)app->tabCurrentIndex,
                 TAG_END);
    }

    /* Free old list and its nodes */
    if (app->tabList) {
        struct Node *n, *next;
        for (n = app->tabList->lh_Head; (next = n->ln_Succ) != NULL; n = next)
            FreeClickTabNode(n);
        FreeVec(app->tabList);
    }
    app->tabList = newList;
}

void EgTabs_AddOrSelectTab(const char *contextKey)
{
    int i;
    char *keyCopy;
    struct Node *node;

    if (!app || !contextKey) return;

    /* Search for existing tab with this key */
    for (i = 0; i < app->tabCount; i++) {
        if (app->tabContextNames[i] &&
            strcmp(app->tabContextNames[i], contextKey) == 0)
        {
            /* Already exists — just switch to it */
            app->tabCurrentIndex = i;
            if (app->tabGadget && CurrentMainWindow)
                SetGadgetAttrs((struct Gadget *)app->tabGadget,
                               CurrentMainWindow, NULL,
                               CLICKTAB_Current, (ULONG)i,
                               TAG_END);
            return;
        }
    }

    if (app->tabCount >= EG_MAX_TABS) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(contextKey) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, contextKey);

    EgTabs_FillLabel(app->tabLabels[app->tabCount], sizeof(app->tabLabels[0]),
                   contextKey);
    node = AllocClickTabNode(
        TNA_Text,   (ULONG)app->tabLabels[app->tabCount],
        TNA_Number, app->tabCount,
        TAG_END);
    if (!node) { FreeVec(keyCopy); return; }

    app->tabContextNames[app->tabCount] = keyCopy;
    app->tabNodes[app->tabCount]        = node;
    app->tabCurrentIndex                = app->tabCount;
    app->tabCount++;

    AddTail(app->tabList, node);

    if (app->tabGadget && CurrentMainWindow) {
        SetGadgetAttrs((struct Gadget *)app->tabGadget,
                       CurrentMainWindow, NULL,
                       CLICKTAB_Labels,  (ULONG)app->tabList,
                       CLICKTAB_Current, (ULONG)app->tabCurrentIndex,
                       TAG_END);
    }
}

void EgTabs_NewTab(void)
{
    char key[32];
    if (!app) return;
    app->tabNewSerial++;
    sprintf(key, ":n:%d", app->tabNewSerial);
    EgTabs_AddOrSelectTab(key);
    /* Switch editor to fresh empty context */
    SetGdAttrs(app->textEditorObj,
               UTED_CurrentContext, (ULONG)key,
               UTED_Text,           (ULONG)"",
               TAG_END);
}

void EgTabs_CloseCurrentTab(void)
{
    int closingIdx, nextIdx, i;
    char *closingKey;

    if (!app) return;
    if (app->tabCurrentIndex < 0 || app->tabCurrentIndex >= app->tabCount) return;

    /* Single tab: closing it means quitting */
    if (app->tabCount == 1) {
        exit(0);
        return;
    }

    closingIdx = app->tabCurrentIndex;
    closingKey = app->tabContextNames[closingIdx]; /* borrow – freed below */

    /* Choose a replacement tab: next if available, otherwise previous */
    nextIdx = (closingIdx + 1 < app->tabCount) ? closingIdx + 1 : closingIdx - 1;

    /* Switch to the replacement tab.  This triggers UTED_CurrentContext on
     * the editor, which stashes the closing context under closingKey. */
    EgTabs_SwitchTo(nextIdx);

    /* Now the closing context is safely stashed – delete it from UniTextEditor */
    if (closingKey && closingKey[0])
        SetGdAttrs(app->textEditorObj,
                   UTED_DeleteContext, (ULONG)closingKey,
                   TAG_END);

    /* Free the closing tab's context-name string */
    if (closingKey)
        FreeVec(closingKey);

    /* Compact the three parallel arrays (contextNames, nodes, labels) */
    for (i = closingIdx; i < app->tabCount - 1; i++) {
        app->tabContextNames[i] = app->tabContextNames[i + 1];
        app->tabNodes[i]        = app->tabNodes[i + 1];
        memcpy(app->tabLabels[i], app->tabLabels[i + 1],
               sizeof(app->tabLabels[0]));
    }
    app->tabContextNames[app->tabCount - 1] = NULL;
    app->tabNodes[app->tabCount - 1]        = NULL;
    app->tabCount--;

    /* EgTabs_SwitchTo set tabCurrentIndex = nextIdx.
     * After removing closingIdx the surviving tabs shift left by one when
     * their original index was > closingIdx. */
    if (app->tabCurrentIndex > closingIdx)
        app->tabCurrentIndex--;

    /* Rebuild the clicktab gadget with the updated list */
    egTabsRebuildGadget();

    /* Refresh UI for the now-current tab */
    SyncVScroller();
    SyncHScroller();
    UpdateStatusBar();
    BMainWindow_SetTitleLocS(&app->mainwindow,
                             MSG_WINDOW_TITLE_WITHFILE,
                             app->tabLabels[app->tabCurrentIndex]);
}

void EgTabs_RenameCurrentTab(const char *newContextKey)
{
    char *keyCopy;
    if (!app || !newContextKey) return;
    if (app->tabCurrentIndex < 0 || app->tabCurrentIndex >= app->tabCount) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(newContextKey) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, newContextKey);

    FreeVec(app->tabContextNames[app->tabCurrentIndex]);
    app->tabContextNames[app->tabCurrentIndex] = keyCopy;

    EgTabs_FillLabel(app->tabLabels[app->tabCurrentIndex],
                   sizeof(app->tabLabels[0]), newContextKey);

    /* Rename the UniTextEditor context so stash lookups use the new key */
    SetGdAttrs(app->textEditorObj,
               UTED_RenameContext, (ULONG)newContextKey,
               TAG_END);

    egTabsRebuildGadget();
}

/* Switch to tab by absolute index. No-op if already current or out of range. */
void EgTabs_SwitchTo(int newIdx)
{
    const char *ctxKey;
    if (!app || newIdx < 0 || newIdx >= app->tabCount) return;
    if (newIdx == app->tabCurrentIndex) return;

    app->tabCurrentIndex = newIdx;
    ctxKey = app->tabContextNames[newIdx] ? app->tabContextNames[newIdx] : "";

    if (app->tabGadget && CurrentMainWindow)
        SetGadgetAttrs((struct Gadget *)app->tabGadget,
                       CurrentMainWindow, NULL,
                       CLICKTAB_Current, (ULONG)newIdx,
                       TAG_END);

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
