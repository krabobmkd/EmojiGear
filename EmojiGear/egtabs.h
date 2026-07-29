#ifndef EGTABS_H
#define EGTABS_H

typedef struct EgTabsAPI {
    void (*FillLabel)(char *buf, ULONG bufsz, const char *key);
    void (*AddOrSelectTab)(const char *contextKey);
    void (*NewTab)(void);
    void (*RenameCurrentTab)(const char *newContextKey);
    void (*CloseCurrentTab)(void);
    void (*CloseTabByNode)(struct Node *node);
    void (*SwitchTo)(int newIdx);
} EgTabsAPI;

extern const EgTabsAPI EgTabsRealAPI;
extern const EgTabsAPI EgTabsSafeAPI;

extern const EgTabsAPI *EgTabsOps;

#define EgTabs_FillLabel(b,s,k)    EgTabsOps->FillLabel(b,s,k)
#define EgTabs_AddOrSelectTab(k)   EgTabsOps->AddOrSelectTab(k)
#define EgTabs_NewTab()            EgTabsOps->NewTab()
#define EgTabs_RenameCurrentTab(k) EgTabsOps->RenameCurrentTab(k)
#define EgTabs_CloseCurrentTab()   EgTabsOps->CloseCurrentTab()
#define EgTabs_CloseTabByNode(n)   EgTabsOps->CloseTabByNode(n)
#define EgTabs_SwitchTo(i)         EgTabsOps->SwitchTo(i)

/* Update tab slot idx's last-known UTED_IsModified state and, only if it
 * actually changed, rebuild the tab bar labels (so the "*" prefix added by
 * FillLabel's real-path case appears/disappears). A no-op when isModified
 * already matches the stored value - callers are expected to call this on
 * every keystroke-ish event, so this guard is what keeps the tab bar from
 * being rebuilt constantly while typing. Not part of EgTabsAPI: the
 * underlying rebuild is backend-specific, but the tabModified[] bookkeeping
 * itself is shared and simple enough to not need a second vtable slot. */
void EgTabs_SyncModified(int idx, BOOL isModified);

#endif /* EGTABS_H */
