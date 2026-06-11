#include "boopsimainwindow.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/graphics.h>
#include <proto/datatypes.h>
#include <proto/amigaguide.h>
#include <proto/cybergraphics.h>
#include <cybergraphics/cybergraphics.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <graphics/displayinfo.h>   /* INVALID_ID */
#include <graphics/gfx.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>

#include <proto/clicktab.h>
#include <gadgets/clicktab.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/window.h>
#include <classes/window.h>


#include "egmenu.h"
#include "egaction.h"
#include "eglocale.h"
#include "emojigear.h"
#include "borderscrollers.h"
#include "egsearchbox.h"
#include "egfontsview.h"
#include "closebutton.h"
#include <gadgets/unitexteditor.h>
#include <proto/requester.h>
#include <classes/requester.h>
#include "compilers.h"
#include "bdbprintf.h"

#include <stdio.h>
#include <string.h>
// This is the intuition level Window, on OS3 it's recreated when iconizing/reopening !
// when  iconizing/reopening BOOPSI objects are kept, but Intuition level instances and buffers are wiped out.
// Yet, it's needed for most Gadget method calls, and this is not retained by boopsi objects.
// note there could be many windows.
struct Window *CurrentMainWindow=NULL;

/* can be either the WB locked screen, or our private screen */
struct Screen *CurrentMainScreen=NULL;

extern struct Library *CyberGfxBase;

/* external window management */
void OpenSettingsWindow()
{
    if(!app) return;
    EgSettingsView_Open(&app->settingsView);
}
/* external window management */
void CloseSettingsWindow()
{
    if(!app) return;

    /* Sync screen mode settings back to AppSettings and save */
    if(app->settingsView.window)
    {

        EgSettingsView_Close(&app->settingsView);

    }
    if(app->fontsView.window)
    {
        AppSettings_Save(&app->appSettings);
        EgFontsView_Close(&app->fontsView);
    }
    if(app->emojiBoxWindow.window)
    {
        EmojiBoxWindow_Close(&app->emojiBoxWindow);
    }

    /* also close help subwindow */
#ifdef HELP_USE_AGLIB
    if(app->amigaGuideHandle )
    {
        CloseAmigaGuide(app->amigaGuideHandle);
        app->amigaGuideHandle = NULL;
    }
#endif
    /* would close requester, but ther'es nio method for this
     if(app->aboutRequester)
     {
     	DoMethod(obj, ..., , TAG_DONE)
     }*/
}


// void BMainWindow_Init(struct BoopsiMainWindow *mw)
// {
//     mw->title[0] = 0;
//     mw->closeImage = NULL;
//     mw->drawInfo = NULL;
// //    if(!mw) return;
// //    mw->lockedScreen = LockPubScreen(NULL);
// //    if(mw->lockedScreen)
// //    {
// //        mw->drawInfo = GetScreenDrawInfo(app->lockedScreen);
// //    }

// }


/* would either set the window title or Screen title according to mode
 private, assume title is in mw->title */
static void BMainWindow_SetTitleInternal(struct BoopsiMainWindow *mw)
{
    /* if fullscreen open, update */
    // if(mw->fullscreen && mw->fullPubScreen && CurrentMainScreen)
    // {
    //     /* seriously ? validate this. */
    //     SetAttrs((Object *)CurrentMainScreen,
    //                     SA_Title,&mw->title[0],NULL);

    // } else
     if(/*!mw->fullscreen &&*/ mw->lockedScreen && CurrentMainWindow)
    {
        /* if wb window open, update */
        SetWindowTitles(CurrentMainWindow,&mw->title[0],NULL);
    }
}

/* would either set the window title or Screen title according to mode */
void BMainWindow_SetTitle(struct BoopsiMainWindow *mw, const char *title)
{
    strncpy(&mw->title[0],title,sizeof(mw->title)-1);
    mw->title[sizeof(mw->title)-1] = 0;
    BMainWindow_SetTitleInternal(mw);

}

/* use a locale enum for title */
void BMainWindow_SetTitleLoc(struct BoopsiMainWindow *mw, int iMessage)
{
    strncpy(&mw->title[0],LOC(iMessage),sizeof(mw->title)-1);
    mw->title[sizeof(mw->title)-1] = 0;
    BMainWindow_SetTitleInternal(mw);
}
/* use a locale enum for title + replace %s in locale */
void BMainWindow_SetTitleLocS(struct BoopsiMainWindow *mw, int iMessage, const char *paramString)
{
    snprintf(&mw->title[0],sizeof(mw->title)-1,LOC(iMessage),paramString);
    mw->title[sizeof(mw->title)-1] = 0;
    BMainWindow_SetTitleInternal(mw);
}


void BMainWindow_Show(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings)
{
    if(!mw) return;
    // if(mw->fullscreen)
    // {
    //     BMainWindow_SwitchToFullScreen(mw,window_obj,appSettings);
    // } else
    {   // WB window
        BMainWindow_SwitchToWB(mw,window_obj,appSettings);
    }
}
// void BMainWindow_Toggle(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings)
// {
//     if(!mw) return;
//     if(!mw->fullscreen)
//     {
//         BMainWindow_SwitchToFullScreen(mw,window_obj,appSettings);
//     } else
//     {   // WB window
//         BMainWindow_SwitchToWB(mw,window_obj,appSettings);
//     }
// }
/* at iconify or close */
void BMainWindow_Close(struct BoopsiMainWindow *mw,Object *window_obj, int iconify)
{
    if(!mw) return;

    CloseSettingsWindow();
    EgSearchBox_Close(&app->searchBox);

    if(CurrentMainWindow)
    {
        // save window position
        /*if(!mw->fullPubScreen)*/ BMainWindow_GetWindowPos(mw,window_obj);
        EgMenu_Close(&mw->menu, CurrentMainWindow);
        if(iconify)
        {
            DoMethod(window_obj, WM_ICONIFY, NULL);
        } else
        {
            DoMethod(window_obj, WM_CLOSE );
        }
        /* the intuition level "struct window *" is no more,
          we can delete the gadtools-attached gadgets */
        BorderScroll_Exit(&app->borderScroll);




        CurrentMainWindow = NULL;
    }
    if(mw->closeImage)
    {
        DisposeObject(mw->closeImage);
        mw->closeImage = NULL;
        SetAttrs(app->tabGadget,CLICKTAB_CloseImage,NULL,TAG_END);
    }

    /* if was in fullscreen mode */
    // if(mw->fullPubScreen)
    // {
    //     CloseScreen(mw->fullPubScreen);
    //     mw->fullPubScreen = NULL;
    // }
    if(mw->lockedScreen)
    {
        UnlockPubScreen(0, mw->lockedScreen);
        mw->lockedScreen = NULL;
    }
    if(mw->drawInfo && CurrentMainScreen)
    {
        FreeScreenDrawInfo( CurrentMainScreen , mw->drawInfo );
        mw->drawInfo = NULL;
    }

    CurrentMainScreen = NULL;
}

UWORD rrggbbToPenIndex(struct Screen *scr, ULONG rrggbb);

/* Used in both WB window and fullscreen cases. doing WM_OPEN implies:
    - recreating and refreshing the menu.
*/
void GenericOpenWindow(BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings, int addScrollers)
{
    if(CurrentMainWindow) return; /* if already exists don't open */
    if(!CurrentMainScreen) return; /* need an active screen */

    /* Load button fonts before WM_OPEN so the layout's GM_DOMAIN call gets
     * a correct minimum height (requires screen font to be known first). */
    extern void UpdateButtonFontsFromSettings(void);
    UpdateButtonFontsFromSettings();

    CurrentMainWindow = (struct Window *)DoMethod(window_obj, WM_OPEN, NULL);

    if(!CurrentMainWindow) return;

    /* Create and attach menus */
    if (!EgMenu_Create(&mw->menu, CurrentMainScreen, CurrentMainWindow,
                       app ? &app->appSettings : NULL)) {
       // printf("Warning: Could not create menus\n");
       return;
    }

    /* Rebuild menus with recent files from loaded settings */
    // if (AppSettings_GetRecentCount(appSettings) > 0) {
    //     EgMenu_Rebuild(&mw->menu, CurrentMainScreen, CurrentMainWindow, appSettings );
    // }
    if(!mw->drawInfo)
    {
        mw->drawInfo = GetScreenDrawInfo(CurrentMainScreen);
    }

    if(addScrollers && app->borderScroll.dArrowBtn == NULL)
    {
        BorderScroll_Init(&app->borderScroll,mw->drawInfo,CurrentMainScreen);

    }

    if(addScrollers && app->borderScroll.dArrowBtn )
    {
        /* with boopsi on, it must be added after 1... maybe 0 is the whole boopsi layout */
        AddGList( CurrentMainWindow, app->borderScroll.dArrowBtn, 1, /*Numgad*/6, NULL );
        RefreshGList((struct Gadget *)app->borderScroll.dArrowBtn, CurrentMainWindow, NULL, 6);
    }

    /* palette are relatives to screens*/
    if (CurrentMainScreen && app->textEditorObj) {
        UWORD bgPen  = 0;
        UWORD txtPen = 1;

        bgPen  = rrggbbToPenIndex(CurrentMainScreen, app->appSettings.editorBgColor);
        txtPen = rrggbbToPenIndex(CurrentMainScreen, app->appSettings.editorPenColor);
        SetGadgetAttrs(app->textEditorObj,
                            CurrentMainWindow,NULL,
                            UTED_TextPen,txtPen & 0x00ff,
                            UTED_BgPen,bgPen  & 0x00ff,
                            TAG_END);

        //ActivateGadget(app->textEditorObj,CurrentMainWindow,NULL);
    }

    /* needed for tab tabs*/
    if(!mw->closeImage)
    {
        mw->closeImage = CloseButton_CreateImage(CurrentMainScreen);

    }
    if(app->tabGadget && mw->closeImage)
        SetGadgetAttrs(app->tabGadget,CurrentMainWindow,NULL,
                    CLICKTAB_CloseImage,(ULONG)mw->closeImage,
                    TAG_END);


    /* test for OS3.9 layout problems */
    // {
    //     struct Gadget *mlayout=NULL;
    //     GetAttr(WINDOW_ParentGroup,window_obj,(ULONG *)&mlayout);

    //     if(mlayout)
    //     {
    //         RethinkLayout( mlayout,CurrentMainWindow,NULL,TRUE);

    //         // SetGadgetAttrs(mlayout,CurrentMainWindow,NULL,
    //         //     GA_Width,w,
    //         //     GA_Height,h,
    //         //     TAG_END
    //         //         );
    //     }
    // }

  //  ActivateGadget((struct Gadget *)app->textEditorObj,CurrentMainWindow,NULL);


}
/* This function realloc pens if screen changed  */
extern void UpdatePensToCurrentMainScreen();


void BMainWindow_GetWindowPos(struct BoopsiMainWindow *mw,Object *window_obj)
{
    /* save window position if there is one */
    GetAttr(WA_Top,window_obj,&mw->top);
    GetAttr(WA_Left,window_obj,&mw->left);
    GetAttr(WA_Width,window_obj,&mw->width);
    GetAttr(WA_Height,window_obj,&mw->height);
    if(mw->width<128) mw->width=128;
    if(mw->height<64) mw->height=64;
}

// void BMainWindow_SwitchToFullScreen(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings)
// {
//     int x1,y1,w,h;
//     struct Screen *myScreen;

// /*
// SA_Pens is also used to decide that a screen is ready to support
// the full-blown "new look" graphics. If you want the 3D embossed look,
//  you must provide this tag, and the ti_Data value cannot be NULL.
//  If it points to a "minimal" array, containing just the terminator ~0,
//   you can specify "new look" without providing any values for the pen array.
// */

//     if(!mw || !window_obj) return;
//     if(mw->fullPubScreen) return; // already ok

//     CloseSettingsWindow();
//     EgSearchBox_Close(&app->searchBox);

//     if(CurrentMainWindow)
//     {
//         /* save window position if there is one */
//         BMainWindow_GetWindowPos(mw,window_obj);
//         if(app->borderScroll.dArrowBtn)
//         {
//             RemoveGList( CurrentMainWindow, app->borderScroll.dArrowBtn, 6 );
//         }
//         EgMenu_Close(&mw->menu, CurrentMainWindow);
//         DoMethod(window_obj, WM_CLOSE );
//         BorderScroll_Exit(&app->borderScroll);
//         CurrentMainWindow = NULL;


//         if(mw->lockedScreen)
//         {
//             UnlockPubScreen(0, mw->lockedScreen);
//             mw->lockedScreen = NULL;
//         }
//         CurrentMainScreen = NULL;
//     }

//     /* Use the user-configured mode ID when available, otherwise inherit WB. */
//     if (appSettings &&
//         !appSettings->screenModeIdLikeWorkbench &&
//         appSettings->screenModeId != INVALID_ID)
//     {
//         ULONG isCLut=FALSE;
//         ULONG ndepth=8;
//         ULONG iscyber = ((CyberGfxBase != NULL) && IsCyberModeID( appSettings->screenModeId));
//         // if(!iscyber)
//         // {
//         //     ndepth = 8;
//         // }
//         //  || GetCyberIDAttr(CYBRIDATTR_DEPTH)<=8 ) isCLut=TRUE;

//         myScreen = OpenScreenTags(NULL,
//             SA_Type,      PUBLICSCREEN,
//             SA_PubName,   (ULONG)"EmojiGear",
//             SA_DisplayID, (ULONG)appSettings->screenModeId,
//             SA_Depth, 4, /* is ok for even OCS/ECS machine in highres */
//             SA_Title,     (ULONG)&mw->title[0],
//            // SA_Colors32, (ULONG)&app->style.paletteRGB32[0],
//            // SA_Pens,(ULONG)&fakepens[0],
//             TAG_DONE);
//     } else {
//         myScreen = OpenScreenTags(NULL,
//             SA_Type,          PUBLICSCREEN,
//             SA_PubName,       (ULONG)"EmojiGear",
//             SA_LikeWorkbench, TRUE,
//             SA_Title,         (ULONG)&mw->title[0],
//             TAG_DONE);
//     }
//     if(!myScreen) return; /* likely because chipram is not infinite*/

//     mw->fullPubScreen = myScreen;

//     CurrentMainScreen = myScreen;

//     /* get screen dimension */
//     x1 =0;
//     y1 = myScreen->BarHeight;
//     w = myScreen->Width;
//     h = myScreen->Height - y1;

//     /* reconfigure persistant boopsi window object while closed */
//     SetAttrs(window_obj,
//         WA_CustomScreen,(ULONG)myScreen,
//         // WA_Borderless, FALSE,
//         // WA_SizeGadget,FALSE,
//         // WA_DepthGadget,FALSE,
//         // WA_CloseGadget,TRUE,
//      //    WA_DragBar,TRUE,
//    //     WA_Title,NULL,
//         WA_Flags,WFLG_ACTIVATE | WFLG_SMART_REFRESH ,
//    //     WA_Backdrop,TRUE,

//    //     WINDOW_IconifyGadget, TRUE,
//         // WA_Top,y1+1,
//         // WA_Left,x1,
//         // WA_Width,w-32, /* need some more pixel at window level so the backdrop UI takes all place */
//         // WA_Height,h-32,
//         TAG_END);

//     /* re-open */
//     GenericOpenWindow( mw, window_obj, appSettings, TRUE );

//     if(CurrentMainWindow)
//     {
//         struct Gadget *mlayout=NULL;
//         GetAttr(WINDOW_ParentGroup,window_obj,(ULONG *)&mlayout);
//  printf("got:%08x\n",mlayout);
//         // if(mlayout)
//         // {
//         //     SetGadgetAttrs(mlayout,CurrentMainWindow,NULL,
//         //         GA_Width,w - app->borderScroll.sizeW -32,
//         //         GA_Height,h- app->borderScroll.sizeH-32,
//         //         TAG_END
//         //             );
//         // }
//     }
//     mw->fullscreen = TRUE;

// }





void BMainWindow_SwitchToWB(struct BoopsiMainWindow *mw,Object *window_obj,struct AppSettings *appSettings)
{
    ULONG backfill;
    int x1,y1,w,h;
    if(!mw || !window_obj) return;

    CloseSettingsWindow();
    EgSearchBox_Close(&app->searchBox);

    /* close backdrop window at boopsi level */
    if(CurrentMainWindow)
    {
        EgMenu_Close(&mw->menu, CurrentMainWindow);

        if(app->borderScroll.dArrowBtn)
        {
            RemoveGList( CurrentMainWindow, app->borderScroll.dArrowBtn, 6 );
        }

        DoMethod(window_obj, WM_CLOSE );
        /* CLICKTAB_CloseImage is deleted by BorderScroll_Exit() and must be redone for all new screen. */
        SetAttrs(app->tabGadget,CLICKTAB_CloseImage,NULL,TAG_END);

        BorderScroll_Exit(&app->borderScroll);
        CurrentMainWindow = NULL;
    }

    /* close extra screen */
    // if(mw->fullPubScreen)
    // {
    //     CloseScreen(mw->fullPubScreen);
    //     mw->fullPubScreen = NULL;
    //     CurrentMainScreen = NULL;
    // }

    if(!mw->lockedScreen)
    {
        mw->lockedScreen = LockPubScreen(NULL);
    }
    if(! mw->lockedScreen) return;

    CurrentMainScreen =  mw->lockedScreen;

    /* if dimension has been kept by settings or screen switch, recover them */
    if(mw->width>0)
    {
        x1 = mw->left;
        y1 = mw->top;
        w = mw->width;
        h = mw->height;
    } else
    {
        /* else some default */
        x1 = 40;
        y1 = 40;
        w = 320;
        h= 240;
    }

    /* reconfigure persistant boopsi window object while closed */
    SetAttrs(window_obj,
        WA_CustomScreen,(ULONG)mw->lockedScreen,
        WA_Borderless, FALSE,
        WA_Backdrop,FALSE,
        WA_Flags,WFLG_ACTIVATE | WFLG_SMART_REFRESH,

       // WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_SIZEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WA_DragBar,TRUE,
        WA_SizeGadget,TRUE,
        WA_DepthGadget,TRUE,
        WA_CloseGadget,TRUE,

        WA_SizeBBottom,TRUE, /* both ! */
        WA_SizeBRight,TRUE,

        WA_ReportMouse, TRUE,
        WA_Title,(ULONG)&mw->title[0],
        WINDOW_IconifyGadget, TRUE,
        WA_Top,y1,
        WA_Left,x1,
        WA_Width,w,
        WA_Height,h,
        WA_BackFill,backfill,
        TAG_END);


//    mw->fullscreen = FALSE;


    /* re-open */
    GenericOpenWindow( mw, window_obj, appSettings, TRUE );

    /* There may exists more screens than just WB when closing fs. make sure WB the visible screen. */
    ScreenToFront(CurrentMainScreen);

}

void SetGdAttrsA(Object *g, CONST struct TagItem * tags)
{
    if(CurrentMainWindow)
    {
        SetGadgetAttrsA((struct Gadget *)g,CurrentMainWindow,NULL,tags);
    } else
    {
        //bdbprintf("some call with no window\n");
        SetAttrsA(g,tags);
    }
}

void  __attribute__((noinline))
    SetGdAttrs(Object *g, ULONG tag, ... ) {
    SetGdAttrsA(g, ( struct TagItem *)&tag);
}
