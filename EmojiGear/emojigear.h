#ifndef EMOJIGEAR_H
#define EMOJIGEAR_H

/*
 * EmojiGear Amiga UTF-8 Unicode Text Editor
 * Main application header: App struct and tool state.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/classusr.h>
#include <libraries/amigaguide.h>

#include "egmenu.h"
#include "emojigearsettings.h"
#include "boopsimainwindow.h"
#include "egsettingsview.h"
#include "egfontsview.h"
#include "borderscrollers.h"
#include "egsearchbox.h"
#include <gadgets/unibutton.h>
#include "emojibox.h"

#define EMOJIGEAR_VERSION "3.1"

#define EG_MAX_TABS 20

#define PIPE_INPUT_BUF 1024

/* Application struct */
struct App {
    Object *window_obj;
    struct MsgPort *app_port; /* This is actually needed for iconizing, to still receive message */

    BoopsiMainWindow mainwindow;

    EgSettingsView settingsView; /* Project Settings window */
    EgFontsView    fontsView;    /* Font Settings window */

    Object *textEditorObj;    /* the UniTextEditor instance */
    Object *activeEditorObj;  /* which of the 3 editors currently has keyboard focus */

    Object *mainvlayout;

    /* Status bar */
    Object *statusBarLayout;
    Object *statusBarLabel;
    Object *statusBarEmojiBtn;  /* UniButton with smiley emoji at right of status bar */

    Object *aboutRequester; /* allocated once when asked first */
    char aboutrequestertext[2048];

    /* gadtools level window border scrollers */
    sGtBorderScroll borderScroll;

    /* Application-level settings (temp dir, recent files) */
    AppSettings appSettings;

    Object *gotoLineRequester;

    EgSearchBox    searchBox;
    EmojiBoxWindow emojiBoxWindow;

    /* Tab bar */
    Object      *tabGadget;

    struct List *tabList;
    struct Node *tabNodes[EG_MAX_TABS];        /* AllocClickTabNode'd nodes      */
    char        *tabContextNames[EG_MAX_TABS]; /* AllocVec'd context key each    */
    char         tabLabels[EG_MAX_TABS][48];   /* label strings for tab nodes    */
    int          tabCount;
    int          tabCurrentIndex;
    int          tabNewSerial;                 /* counter for synthetic ":n:N" keys */

    char pipeBuf[PIPE_INPUT_BUF];

};

void UpdateStatusBar();
void SyncVScroller(void);
void SyncHScroller(void);
void UpdateEditorFontsFromSettings(void);

#include "egtabs.h"

void OpenSearchBox();
void CloseSearchBox();
void OpenEmojiBoxWindow(void);
void CloseEmojiBoxWindow(void);

extern struct App *app;

#endif /* PETMATE_H */
