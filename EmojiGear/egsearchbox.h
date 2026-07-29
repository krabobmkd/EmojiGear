#ifndef EGSEARCHBOX_H
#define EGSEARCHBOX_H

/*
 * egsearchbox.h - Search & Replace box for EmojiGear.
 *
 * Defines EgSearchBox, a BOOPSI layout subtree that is aggregated into
 * the main App struct and inserted into the main vertical layout.
 * Phase 1: GUI layout only.  Phase 2 will add the search engine.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include "egmenu.h"

typedef struct EgSearchBox {
    Object         *windowObj;           /* BOOPSI window object (persistent) */
    /* instance when opened */
    struct Window *window;

    LONG left, top, width, height;       /* remembered window geometry */

    EgMenu menu; /* same as mainwindow */

    Object *layout;           /* outer vertical layout – add to parent layout */

    Object *searchEditor;     /* single-line UniTextEditor for search term    */
    Object *searchEraseBtn;   /* "X" button to clear search field             */

    Object *replaceEditor;    /* single-line UniTextEditor for replace term   */
    Object *replaceEraseBtn;  /* "X" button to clear replace field            */

    Object *findNextBtn;
    Object *findPrevBtn;
    Object *replaceBtn;
    Object *replaceAllBtn;
    Object *caseSensCheck;
    // Object *closeBtn;
} EgSearchBox;

/*
 * Build the BOOPSI layout subtree.
 * pointSize: font size in pixels, passed through to the UniTextEditors.
 * Returns TRUE on success; sb->layout is NULL on failure.
 */
BOOL EgSearchBox_Create(EgSearchBox *sb, ULONG pointSize/*,ULONG sharedButtonsDrawContext*/);



/* Dispose the layout subtree (only call if not already owned by a parent). */
void EgSearchBox_Dispose(EgSearchBox *sb);



/* Open the Settings window on CurrentMainScreen. No-op if already open. */
void  EgSearchBox_Open(EgSearchBox *sb);

/* Close (hide) the Settings window. No-op if already closed. */
void  EgSearchBox_Close(EgSearchBox *sb);

/* Handle input messages from this window. Call when its signal fires. */
BOOL  EgSearchBox_HandleInput(EgSearchBox *sb);
void  EgSearchBox_HandleBoopsiMessages(EgSearchBox *sb,ULONG sender_ID, struct TagItem *ptag);
/* Signal bit mask to OR into Wait(). Returns 0 when window is closed. */
ULONG EgSearchBox_GetSignalMask(EgSearchBox *sb);

/* return the current search text, DO NO KEEP POINTER !
    can return NULL for any reason. */
const char *EgSearchBox_GetUTF8SearchString(EgSearchBox *sb);

/* return the current replace text, DO NO KEEP POINTER !
      can return NULL for any reason. */
const char *EgSearchBox_GetUTF8ReplaceString(EgSearchBox *sb);

/* Returns TRUE if the case-sensitive checkbox is checked, FALSE otherwise. */
int EgSearchBox_GetCaseSensitive(EgSearchBox *sb);
#endif /* EGSEARCHBOX_H */
