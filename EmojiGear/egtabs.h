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

#endif /* EGTABS_H */
