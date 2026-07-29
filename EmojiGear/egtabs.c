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
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <proto/clicktab.h>
#include <gadgets/clicktab.h>

#include "emojigear.h"
#include "eglocale.h"
#include "boopsimainwindow.h"
#include "egtabs.h"
#include "egnotify.h"
#include "egaction.h"

#include <gadgets/unitexteditor.h>


extern struct Library *ClickTabBase;

/* Replace underscores with spaces: BOOPSI treats '_x' as a keyboard
 * accelerator that steals vanilla key events from the editor. */
static void egReplaceUnderscores(char *buf)
{
    for (; *buf; buf++)
        if (*buf == '_') *buf = ' ';
}

/* TRUE if key matches a real-path tab (not the current one being added/
 * renamed, which by the time FillLabel is called for it either isn't in
 * tabContextNames[] yet - AddOrSelectTab - or already holds the same key -
 * RenameCurrentTab - so a plain linear scan finds the right slot either
 * way, or correctly finds none for a brand-new tab, which is exactly the
 * "not modified yet" state a freshly loaded/saved file should show). */
static BOOL egTabsReal_IsKeyModified(const char *key)
{
    int i;
    if (!app || !key) return FALSE;
    for (i = 0; i < app->tabCount; i++) {
        if (app->tabContextNames[i] && strcmp(app->tabContextNames[i], key) == 0)
            return app->tabModified[i];
    }
    return FALSE;
}

/* Fill buf with a human-readable tab label for the given context key.
 * Synthetic keys ":n:N": N==1 → "New File"; N>1 → "New File N".
 * Real paths: filename part (underscores replaced by spaces), prefixed
 * with "*" when the file differs from what's on disk (see EgTabs_SyncModified). */
static void egTabsReal_FillLabel(char *buf, ULONG bufsz, const char *key)
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
        const char *name = fp ? fp : key;
        if (egTabsReal_IsKeyModified(key))
            snprintf(buf, bufsz, "*%s", name);
        else
            strncpy(buf, name, bufsz - 1);
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
        egTabsReal_FillLabel(app->tabLabels[i], sizeof(app->tabLabels[i]),
                       app->tabContextNames[i]);
        node = AllocClickTabNode(
          //doesntwork TNA_CloseGadget, TRUE,
            TNA_Text,   (ULONG)app->tabLabels[i],
            TNA_Number, i,
            TNA_CloseGadget,TRUE,
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

void EgTabs_SyncModified(int idx, BOOL isModified)
{
    if (!app || idx < 0 || idx >= app->tabCount) return;
    isModified = isModified ? TRUE : FALSE;
    if (app->tabModified[idx] == isModified) return;
    app->tabModified[idx] = isModified;
    egTabsRebuildGadget();
}

static void egTabsReal_AddOrSelectTab(const char *contextKey)
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

    egTabsReal_FillLabel(app->tabLabels[app->tabCount], sizeof(app->tabLabels[0]),
                   contextKey);

    node = AllocClickTabNode(
      //doesntwork?  TNA_CloseGadget, TRUE,
        TNA_Text,   (ULONG)app->tabLabels[app->tabCount],
        TNA_Number, app->tabCount,
        TNA_CloseGadget,TRUE,
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

static void egTabsReal_NewTab(void)
{
    char key[64];
    if (!app) return;
    app->tabNewSerial++;
    sprintf(key, ":n:%d", app->tabNewSerial);
    egTabsReal_AddOrSelectTab(key);
    /* Switch editor to fresh empty context */
    SetGdAttrs(app->textEditorObj,
               UTED_CurrentContext, (ULONG)key,
               UTED_Text,           (ULONG)"",
               TAG_END);
}

/* forward declaration needed: CloseCurrentTab calls SwitchTo defined below */
static void egTabsReal_SwitchTo(int newIdx);

/* Ask whether to save tab idx's changes before it closes, if (and only if)
 * it's attached to a real file and currently modified - unmodified and
 * untitled tabs close silently, same as always. Returns FALSE if the close
 * must be aborted: the user asked to save first but the save failed, so
 * discarding the tab now would silently lose those changes. */
static BOOL egTabsReal_ConfirmClose(int idx)
{
    const char *key;
    struct EasyStruct es;
    ULONG argArray[1];
    LONG  choice;

    if (!app || idx < 0 || idx >= app->tabCount) return TRUE;

    key = app->tabContextNames[idx];
    if (!key || !key[0] || key[0] == ':' || !app->tabModified[idx])
        return TRUE;

    argArray[0] = (ULONG)key;

    es.es_StructSize   = sizeof(struct EasyStruct);
    es.es_Flags        = 0;
    es.es_Title        = (UBYTE *)LOC(MSG_CLOSE_CONFIRM_TITLE);
    es.es_TextFormat   = (UBYTE *)LOC(MSG_CLOSE_CONFIRM_BODY);
    es.es_GadgetFormat = (UBYTE *)LOC(MSG_CLOSE_CONFIRM_GADGETS);

    choice = EasyRequestArgs(CurrentMainWindow, &es, NULL, argArray);
    if (choice != 1) return TRUE; /* "Don't Save": proceed, discard changes */

    /* "Save": Action_FileSave() only ever acts on the *current* tab, since
     * that's the only context whose text actually lives in the editor
     * gadget right now - bring this one into focus first if it isn't. */
    if (idx != app->tabCurrentIndex)
        egTabsReal_SwitchTo(idx);

    return Action_FileSave(app);
}

/* Close the tab at closingIdx, whether or not it is the active tab, and
 * synchronize tab state.  Shared by CloseCurrentTab and CloseTabByNode. */
static void egTabsReal_CloseTabByIndex(int closingIdx)
{
    int nextIdx, i;
    char *closingKey;

    if (!app) return;
    if (closingIdx < 0 || closingIdx >= app->tabCount) return;

    /* Single tab: closing it means quitting the app - go through the
     * app-wide quit confirmation (covers this tab, worded for "quitting"
     * rather than "closing") instead of a redundant per-tab one below. */
    if (app->tabCount == 1) {
        EgAction_ConfirmAndQuit(app);
        return; /* only reached if the user backed out (a save failed) */
    }

    if (!egTabsReal_ConfirmClose(closingIdx))
        return; /* user asked to save first but it failed - keep the tab open */

    closingKey = app->tabContextNames[closingIdx]; /* borrow – freed below */

    if (closingIdx == app->tabCurrentIndex) {
        /* Choose a replacement tab: next if available, otherwise previous */
        nextIdx = (closingIdx + 1 < app->tabCount) ? closingIdx + 1 : closingIdx - 1;

        /* Switch to the replacement tab.  This triggers UTED_CurrentContext
         * on the editor, which stashes the closing context under closingKey. */
        egTabsReal_SwitchTo(nextIdx);
    }

    /* Now the closing context is safely stashed – delete it from UniTextEditor */
    if (closingKey && closingKey[0])
        SetGdAttrs(app->textEditorObj,
                   UTED_DeleteContext, (ULONG)closingKey,
                   TAG_END);

    /* Stop watching the closing tab's file, if any watch was armed */
    EgNotify_End(closingIdx);

    /* Free the closing tab's context-name string */
    if (closingKey)
        FreeVec(closingKey);

    /* Compact the four parallel arrays (contextNames, nodes, labels, notify) */
    for (i = closingIdx; i < app->tabCount - 1; i++) {
        app->tabContextNames[i] = app->tabContextNames[i + 1];
        app->tabNodes[i]        = app->tabNodes[i + 1];
        memcpy(app->tabLabels[i], app->tabLabels[i + 1],
               sizeof(app->tabLabels[0]));
        app->tabNotify[i]   = app->tabNotify[i + 1];
        app->tabEncoding[i] = app->tabEncoding[i + 1];
        app->tabModified[i] = app->tabModified[i + 1];
    }
    app->tabContextNames[app->tabCount - 1] = NULL;
    app->tabNodes[app->tabCount - 1]        = NULL;
    app->tabNotify[app->tabCount - 1]       = NULL;
    app->tabEncoding[app->tabCount - 1]     = 0;
    app->tabModified[app->tabCount - 1]     = FALSE;
    app->tabCount--;

    /* After removing closingIdx the surviving tabs shift left by one when
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

static void egTabsReal_CloseCurrentTab(void)
{
    if (!app) return;
    if (app->tabCurrentIndex < 0 || app->tabCurrentIndex >= app->tabCount) return;
    egTabsReal_CloseTabByIndex(app->tabCurrentIndex);
}

/* Close the tab whose ClickTab node is 'node' (e.g. from CLICKTAB_NodeClosed),
 * regardless of whether it is the currently active tab. */
static void egTabsReal_CloseTabByNode(struct Node *node)
{
    int i;
    if (!app || !node) return;
    for (i = 0; i < app->tabCount; i++) {
        if (app->tabNodes[i] == node) {
            egTabsReal_CloseTabByIndex(i);
            return;
        }
    }
}

static void egTabsReal_RenameCurrentTab(const char *newContextKey)
{
    char *keyCopy;
    if (!app || !newContextKey) return;
    if (app->tabCurrentIndex < 0 || app->tabCurrentIndex >= app->tabCount) return;

    keyCopy = (char *)AllocVec((ULONG)strlen(newContextKey) + 1, MEMF_ANY);
    if (!keyCopy) return;
    strcpy(keyCopy, newContextKey);

    FreeVec(app->tabContextNames[app->tabCurrentIndex]);
    app->tabContextNames[app->tabCurrentIndex] = keyCopy;

    egTabsReal_FillLabel(app->tabLabels[app->tabCurrentIndex],
                   sizeof(app->tabLabels[0]), newContextKey);

    /* Rename the UniTextEditor context so stash lookups use the new key */
    SetGdAttrs(app->textEditorObj,
               UTED_RenameContext, (ULONG)newContextKey,
               TAG_END);

    egTabsRebuildGadget();
}

/* Switch to tab by absolute index. No-op if already current or out of range. */
static void egTabsReal_SwitchTo(int newIdx)
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

const EgTabsAPI EgTabsRealAPI = {
    egTabsReal_FillLabel,
    egTabsReal_AddOrSelectTab,
    egTabsReal_NewTab,
    egTabsReal_RenameCurrentTab,
    egTabsReal_CloseCurrentTab,
    egTabsReal_CloseTabByNode,
    egTabsReal_SwitchTo,
};

const EgTabsAPI *EgTabsOps = &EgTabsRealAPI;
