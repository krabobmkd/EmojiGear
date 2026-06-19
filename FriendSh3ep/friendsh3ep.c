/*
 * FriendSh3ep - a Mastodon client for AmigaOS.
 *
 * Main application: library init, BOOPSI window+layout creation, event
 * loop, cleanup. Structured after EmojiGear/emojigear.c: a struct App
 * holding every persistent BOOPSI object, an OpenLibrary() table, and a
 * Wait()/WM_HANDLEINPUT main loop. fs3eboopsimainwindow.c handles
 * open/close/iconify of the window, fs3eboopsimessage.c redirects BOOPSI
 * notifications from gadgets (ICA_TARGET) to this main process.
 *
 * See ARCHITECTURE.md for the overall design and roadmap.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <clib/alib_protos.h>

#include <intuition/screens.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/dos.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <proto/unibutton.h>
#include <gadgets/unibutton.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <proto/locale.h>
#include <libraries/locale.h>

#include <workbench/startup.h>

#include "compilers.h"
#include "bdbprintf.h"
#include "fs3egadgetid.h"

#include "friendsh3ep.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "fs3elocale.h"

#include "network_fs3e/fs3enet.h"

const char *pVersion = "$VER: FriendSh3ep " FRIENDSH3EP_VERSION;

struct Task *myTask = NULL;

/* Library bases */
struct GfxBase       *GfxBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library       *UtilityBase   = NULL;

/* BOOPSI class bases */
struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *LabelBase = NULL;
struct Library *ChooserBase = NULL;
struct Library *UniTextEditorBase = NULL;
struct Library *UniButtonBase = NULL;

/* locale.library - opened separately, soft failure (English fallback) */
struct LocaleBase *LocaleBase = NULL;

/* Library table for automated opening/closing.
 * For minimal library version numbers, see EmojiGear/emojigear.c. */
typedef struct {
    const char     *name;
    ULONG           version;
    struct Library **base;
} LibraryEntry;

static LibraryEntry libraryTable[] = {
    /* System libraries */
    {"graphics.library",  40, (struct Library **)&GfxBase},
    {"intuition.library", 40, (struct Library **)&IntuitionBase},
    {"utility.library",   40, &UtilityBase},

    /* BOOPSI class libraries */
    {"window.class",          42, &WindowBase},
    {"gadgets/layout.gadget",  42, &LayoutBase},
    {"gadgets/button.gadget",  42, &ButtonBase},
    {"images/label.image",     42, &LabelBase},
    {"gadgets/chooser.gadget", 44, &ChooserBase},

    /* shared gadgets (see ../UniTextEditorGadget, ../UniButtonGadget) */
    {"gadgets/unitexteditor.gadget", 4, &UniTextEditorBase},
    {"gadgets/unibutton.gadget",     4, &UniButtonBase},

    {NULL, 0, NULL} /* Terminator */
};

struct App *app = NULL;

/* Forward declarations */
void exitclose(void);

void cleanexit(const char *pmessage)
{
    if (pmessage) printf("%s\n", pmessage);
    exit(0);
}

/* - - - - - - - - - - - - - - - - - - - MAIN - - - - - - - - - - - - - - - */

int main(int argc, char **argv)
{
    if (SysBase->LibNode.lib_Version < 40) {
        printf("FriendSh3ep needs OS3.9 (v40) or OS3.2, you may upgrade.\n");
        return 1;
    }

    atexit(&exitclose);

    /* Open all required libraries via table */
    {
        LibraryEntry *entry;

        for (entry = libraryTable; entry->name != NULL; entry++) {

            *(entry->base) = OpenLibrary(entry->name, entry->version);

            if (!*(entry->base)) {
                printf("Can't open %s v%u\n", entry->name, (unsigned int)entry->version);
                return 1;
            }
        }
    }

    myTask = FindTask(NULL);

    /* Open locale.library (optional - soft failure) */
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);

    /* Initialize locale (English built-in) */
    FS3ELocale_Init("FriendSh3ep.catalog", 0);

    /* Allocate app struct (zero-initialized) */
    app = (struct App *)AllocVec(sizeof(struct App), MEMF_CLEAR);
    if (!app) cleanexit("Can't allocate app");

    /* Initialize the BOOPSI message target model: gadgets use
     * ICA_TARGET=TargetInstance, OM_NOTIFY is queued and signalled back
     * to myTask (SIGBREAKF_CTRL_F), drained below in the main loop. */
    if (!FS3EMsg_Init()) cleanexit("Can't create BOOPSI message target");

    /* Start the network process (see network_fs3e/fs3enet.c). The GUI talks
     * to it only through netRequestPort; replies will be wired into the
     * main loop once the login UI lands. */
    app->netRequestPort = FS3ENet_Start();
    if (!app->netRequestPort)
        printf("FriendSh3ep: failed to start network process - continuing without it\n");

    /* ---- Classic BOOPSI sub-windows (own dragbar/close/depth gadgets),
     * opened on demand from the borderless main window. ---- */

    if (!FS3ELoginView_Create(&app->loginView, 14))
        cleanexit("Can't create login view");

    if (!FS3ETootView_Create(&app->tootView, 14))
        cleanexit("Can't create New toot view");

    /* ---- Placeholder content: title label + login button ---- */

    app->titleLabel = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,         TRUE,
        BUTTON_BevelStyle,   BVS_NONE,
        BUTTON_Transparent,  TRUE,
        BUTTON_Justification, BCJ_CENTER,
        GA_Text,             (ULONG)LOC(MSG_WINDOW_TITLE),
        TAG_END);
    if (!app->titleLabel) cleanexit("Can't create title label");

    app->loginButton = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GID_LOGIN_BUTTON,
        GA_RelVerify, TRUE,
        ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_LOGIN_BUTTON),
        TAG_END);
    if (!app->loginButton) cleanexit("Can't create login button");

    app->newTootButton = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GID_NEWTOOT_BUTTON,
        GA_RelVerify, TRUE,
        ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_NEWTOOT_BUTTON),
        TAG_END);
    if (!app->newTootButton) cleanexit("Can't create new toot button");

    /* ---- Main layout ---- */

    app->mainlayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,

        LAYOUT_AddChild, (ULONG)app->titleLabel,
            CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, (ULONG)app->loginButton,
            CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, (ULONG)app->newTootButton,
            CHILD_WeightedHeight, 0,

        TAG_END);
    if (!app->mainlayout) cleanexit("Can't create layout");

    /* Create message port (needed for iconizing, to still receive messages) */
    app->app_port = CreateMsgPort();

    /* ---- Create the window object ---- */
    app->window_obj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   40,
        WA_Top,    40,
        WA_Width,  400,
        WA_Height, 200,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE | IDCMP_RAWKEY,
        WA_Flags,
        WFLG_BORDERLESS |
            WFLG_DRAGBAR |
          //  WFLG_DEPTHGADGET |
          //  WFLG_CLOSEGADGET |
        //   WFLG_SIZEGADGET |
        //   WFLG_SIZEBRIGHT |
           WFLG_ACTIVATE |
           WFLG_SMART_REFRESH,
     //   WA_Title,  (ULONG)"FriendSh3ep",
WA_Title, NULL,
        WINDOW_ParentGroup,  (ULONG)app->mainlayout,
      //  WINDOW_IconifyGadget, TRUE,
        WINDOW_IconTitle,    (ULONG)"FriendSh3ep",
        WINDOW_AppPort,      (ULONG)app->app_port, /* needed for iconizing */

        TAG_END);
    if (!app->window_obj) cleanexit("Can't create window");

 //   FS3EMain_SetTitle(&app->mainwindow, "FriendSh3ep v" FRIENDSH3EP_VERSION);
    FS3EMain_Show(&app->mainwindow, app->window_obj);
    if (!CurrentMainWindow) cleanexit("Can't open window");

#define reflags_subjectEditor 1
#define reflags_bodyEditor 2

    /* - - - Input Event Loop - - - */
    {
        ULONG winsignal;
        BOOL  ok = TRUE;
        ULONG refreshFlags=0;

        GetAttr(WINDOW_SigMask, app->window_obj, &winsignal);

        while (ok)
        {
            ULONG result, waitedSignals, currentSignals;
            ULONG loginSig, tootSig;

            flushbdbprint();

            /* sub-window signal bits are 0 while their window is closed */
            loginSig = FS3ELoginView_GetSignalMask(&app->loginView);
            tootSig  = FS3ETootView_GetSignalMask(&app->tootView);

            waitedSignals = winsignal | loginSig | tootSig |
                (1L << app->app_port->mp_SigBit) |
                SIGBREAKF_CTRL_C |
                SIGBREAKF_CTRL_F;

            currentSignals = Wait(waitedSignals);

            /* exit app at any moment from Ctrl-C signal, atexit() magic does anything needed. */
            if (currentSignals & SIGBREAKF_CTRL_C) exit(0);

            while ((result = DoMethod(app->window_obj, WM_HANDLEINPUT, NULL))
                   != WMHI_LASTMSG)
            {
                flushbdbprint();
                switch (result & WMHI_CLASSMASK)
                {
                    case WMHI_CLOSEWINDOW:
                        ok = FALSE;
                        break;

                    case WMHI_GADGETUP:
                    {
                        /* Queue gadget-up event for dispatch on main task context,
                         * alongside OM_NOTIFY messages from ICA_TARGET gadgets. */
                        ULONG senderId = result & WMHI_GADGETMASK;

                        BoopsiDelay_BeginMessage(DelayQueue, senderId);
                        BoopsiDelay_AddTag(DelayQueue, WMHI_GADGETUP, 1);
                        BoopsiDelay_EndMessage(DelayQueue);
                        break;
                    }

                    case WMHI_ICONIFY:
                        FS3EMain_Close(&app->mainwindow, app->window_obj, TRUE);
                        break;

                    case WMHI_UNICONIFY:
                        FS3EMain_Show(&app->mainwindow, app->window_obj);
                        if (!CurrentMainWindow) cleanexit("can't re-open window");
                        break;
                    case WMHI_RAWKEY:
                    {
                     //   ULONG qualifiers=0;
                     //   GetAttr(WINDOW_Qualifier,sb->windowObj,&qualifiers);
                        ULONG key = (result & 0x07f);
                       // ULONG isUp = (result & 0x080);

                        if(key == 0x45) {
                           ok = FALSE;
                        }
                        // if(app->activeEditorObj == sb->searchEditor ||
                        //     app->activeEditorObj == sb->replaceEditor)
                        // {
                        //     if (!EmojiBox_HandleFKey(app, key, qualifiers,sb->window))
                        //     {
                        //         SetGadgetAttrs(app->activeEditorObj,
                        //             sb->window,NULL,
                        //             UTED_PutRawKey,key|(qualifiers<<16),TAG_END);
                        //     }
                        // }
                    }
                    break;
                    default:
                        break;
                }
            } /* end WM_HANDLEINPUT loop */

            /* Sub-windows (login, new toot, ...) handle their own input
             * and queue WMHI_GADGETUP / OM_NOTIFY into DelayQueue too. */
            FS3ELoginView_HandleInput(&app->loginView);
            FS3ETootView_HandleInput(&app->tootView);

            /* Process delayed BOOPSI notifications: WMHI_GADGETUP events
             * (queued above) and OM_NOTIFY from gadgets with
             * ICA_TARGET=TargetInstance land here, serialised on the main
             * task so it is safe to call any Intuition/gadget API. */
            if (DelayQueue && BoopsiDelay_HasMessages(DelayQueue)) {
                struct TagItem *msg;

                while ((msg = BoopsiDelay_NextMessage(DelayQueue)) != NULL) {
                    struct TagItem *ptag;
                    ULONG sender_ID = 0;

                    ptag = FindTagItem(GA_ID, msg);
                    if (ptag) sender_ID = ptag->ti_Data;

                    switch (sender_ID) {

                        case GID_LOGIN_BUTTON:
                            FS3ELoginView_Open(&app->loginView);
                            break;

                        case GID_NEWTOOT_BUTTON:
                            FS3ETootView_Open(&app->tootView);
                            break;

                        case GID_LOGIN_LOGIN_BUTTON:
                        {
                            const char *server = FS3ELoginView_GetUTF8Server(&app->loginView);
                            const char *user   = FS3ELoginView_GetUTF8User(&app->loginView);
                            const char *code   = FS3ELoginView_GetUTF8Code(&app->loginView);

                            bdbprintf("FriendSh3ep: login requested "
                                      "(server=%s user=%s code=%s)\n",
                                      server ? server : "",
                                      user   ? user   : "",
                                      code   ? code   : "");
                            /* TODO: FS3ENETQ_LOGIN_START / LOGIN_FINISH via netRequestPort */
                            break;
                        }

                        case GID_TOOT_SEND_BUTTON:
                        {
                            const char *subject = FS3ETootView_GetUTF8Subject(&app->tootView);
                            const char *body    = FS3ETootView_GetUTF8Body(&app->tootView);
                            LONG visibility     = FS3ETootView_GetVisibility(&app->tootView);

                            bdbprintf("FriendSh3ep: toot requested "
                                      "(visibility=%ld subject=%s body=%s)\n",
                                      (long)visibility,
                                      subject ? subject : "",
                                      body    ? body    : "");
                            /* TODO: FS3ENETQ_POST_STATUS via netRequestPort */
                            break;
                        }
                        case GID_TOOT_BODY_EDITOR:
                            refreshFlags |= reflags_bodyEditor;
                        break;
                        case GID_TOOT_SUBJECT_EDITOR:
                            refreshFlags |= reflags_subjectEditor;
                        break;

                        default:
                            break;
                    }
                } /* end loop boopsi messages */
                if((refreshFlags & reflags_subjectEditor) && app->tootView.window)
                {
                    RefreshGList((struct Gadget *)app->tootView.subjectEditor,
                                             app->tootView.window,NULL,1);
                }
                if((refreshFlags & reflags_bodyEditor) && app->tootView.window)
                {
                    RefreshGList((struct Gadget *)app->tootView.bodyEditor,
                                             app->tootView.window,NULL,1);
                }

            } /* end loop per wait */

        } /* end main loop */
    }

    return 0;
}

/* - - - - - - - - - - - - - - - - - CLEANUP - - - - - - - - - - - - - - - - */

void exitclose(void)
{
    if (app)
    {
        FS3ELoginView_Dispose(&app->loginView);
        FS3ETootView_Dispose(&app->tootView);

        if (app->window_obj)
        {
            FS3EMain_Close(&app->mainwindow, app->window_obj, 0);
            /* this cascades disposal of mainlayout, titleLabel, loginButton */
            DisposeObject(app->window_obj);
        }

        if (app->netRequestPort)
        {
            struct MsgPort *netReplyPort = CreateMsgPort();
            if (netReplyPort)
            {
                FS3ENet_Stop(app->netRequestPort, netReplyPort);
                DeleteMsgPort(netReplyPort);
            }
        }

        FS3EMsg_Close();

        if (app->app_port)
            DeleteMsgPort(app->app_port);

        FreeVec(app);
        app = NULL;
    }

    /* Close locale */
    FS3ELocale_Close();
    if (LocaleBase) {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
    }

    /* Close all libraries in reverse order */
    {
        LibraryEntry *entry;
        int i;

        for (i = 0; libraryTable[i].name != NULL; i++)
            ;

        for (i = i - 1; i >= 0; i--) {
            entry = &libraryTable[i];
            if (*(entry->base)) {
                CloseLibrary(*(entry->base));
                *(entry->base) = NULL;
            }
        }
    }
}
