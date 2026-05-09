#ifndef BOOPSIMAINWINDOW_H
#define BOOPSIMAINWINDOW_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/classusr.h>
#include "egmenu.h"
#include "compilers.h"
// to get recent menu list
//#include "appsettings.h"

/*
    Extends a bit Window Boopsi behaviour, so:
     - We can switch WB Window to fullscreen window
     - Find the window position back when returning to WB.
     - have a single function to set WB Window Title, or screen title if fullscreen.(TODO)
     - Manage recreating the menu each time we change mode or iconify/uniconify.
     - manage color remap issues when changing screen.(TODO)

  Window boopsi object and attached gadgets are persistant and reconfigurable.
   when Intuition Screens and Windows are transient.

*/

/* structure */
typedef struct BoopsiMainWindow {
    LONG top,left,width,height;
  //  int fullscreen; // keep state when hidding.

    struct Screen *lockedScreen; /* when using window on WB or else */
//    struct Screen *fullPubScreen;  /* when using our own public screen, reallocated. */

    EgMenu menu; /* GadTools menu */

    //struct DrawInfo *drawInfo; // informations on how to draw on the screen, passed to gagdets.
    char title[80];

} BoopsiMainWindow;

/* not managed yet, used in boopsimainwindow for recent files list:  TODO */
struct AppSettings;


/* once at init */
void BMainWindow_Init(struct BoopsiMainWindow *mw);
void BMainWindow_SwitchToFullScreen(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings);
void BMainWindow_SwitchToWB(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings);
/* would either set the window title or Screen title according to mode */
void BMainWindow_SetTitle(struct BoopsiMainWindow *mw, const char *title);

/* use a locale enum for title */
void BMainWindow_SetTitleLoc(struct BoopsiMainWindow *mw, int iMessage);
/* use a locale enum for title + replace %s in locale */
void BMainWindow_SetTitleLocS(struct BoopsiMainWindow *mw, int iMessage, const char *paramString);

/* at uniconify */
void BMainWindow_Show(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings);
void BMainWindow_Toggle(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings);

void BMainWindow_GetWindowPos(struct BoopsiMainWindow *mw,Object *window_obj);


/* at iconify or quitting */
void BMainWindow_Close(struct BoopsiMainWindow *mw,Object *window_obj, int iconify);

/* Get value for WA_BackFill and GA_BackFill, return a Layer drawing hook casted as ULONG */
ULONG BMainWindow_GetBackFillFromSettings(struct AppSettings *appSettings);

/* SetGadgetAttrs, but check CurrentMainWindow is currently opened, use SetGadgetAttrs() or SetAttrs() accordingly. */
void SetGdAttrsA(Object *g, CONST struct TagItem * tags);
void SetGdAttrs(Object *g, ULONG tag, ... );


/* Intuition level Window */
extern struct Window *CurrentMainWindow;

/* Intuition level Screen, Can be either the WB locked screen, or our private screen */
extern struct Screen *CurrentMainScreen;

#endif /* PmMenu_H */
