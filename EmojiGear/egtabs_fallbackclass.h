#ifndef EGTABS_FALLBACKCLASS_H
#define EGTABS_FALLBACKCLASS_H

#include <intuition/classes.h>

/* Custom OM_SET attribute: LONG delta (-1 or +1) shifts the visible tab window */
#define FAKETAB_Dummy     (TAG_USER + 0x0F000)
#define FAKETAB_ScrollBy  (FAKETAB_Dummy + 1)

void   EgFakeTab_Init(void);
void   EgFakeTab_Free(void);
Class *EgFakeTab_GetClass(void);

#endif /* EGTABS_FALLBACKCLASS_H */
