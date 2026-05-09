#ifndef EGTABS_H
#define EGTABS_H

void EgTabs_FillLabel(char *buf, ULONG bufsz, const char *key);
void EgTabs_AddOrSelectTab(const char *contextKey);
void EgTabs_NewTab(void);
void EgTabs_RenameCurrentTab(const char *newContextKey);
void EgTabs_CloseCurrentTab(void);
void EgTabs_SwitchTo(int newIdx);

#endif /* EGTABS_H */
