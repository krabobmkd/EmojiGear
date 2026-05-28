/*
 * egmenu.c – GadTools menu management for Eg UTF-8 text editor.
 */

#include "egmenu.h"
#include "egaction.h"
#include "eglocale.h"
#include "emojigear.h"

#include <stdio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/dos.h>
#include <libraries/gadtools.h>
#include <gadgets/unitexteditor.h>

extern struct Library *GadToolsBase;

/*
 * nm_UserData encoding:
 *   upper 16 bits non-zero  →  ACTION_* id in upper 16 bits (ACTION_UD macro)
 *   upper 16 bits zero      →  MSG_* locale id in lower 16 bits (title/branch)
 */
#define ACTION_UD(a)  ((ULONG)((a) + 1) << 16)

#define MENU_TEMPLATE_MAX 64

static struct NewMenu s_menuTemplate[MENU_TEMPLATE_MAX];
static char s_recentLabels[APPSETTINGS_MAX_RECENT][40];
static int s_n;

static void addEntry(UBYTE type, STRPTR label, STRPTR key,
                     UWORD flags, LONG mutex, ULONG udata)
{
    struct NewMenu *e = &s_menuTemplate[s_n++];
    e->nm_Type          = type;
    e->nm_Label         = label;
    e->nm_CommKey       = key;
    e->nm_Flags         = flags;
    e->nm_MutualExclude = mutex;
    e->nm_UserData      = (APTR)udata;
}

#define ADD(t,l,k,f,m,u)  addEntry((t),(STRPTR)(l),(STRPTR)(k),(f),(m),(ULONG)(u))
#define BAR(type)          ADD((type), NM_BARLABEL, 0, 0, 0, 0)

static void buildMenuTemplate(const AppSettings *as)
{
    int r;
    s_n = 0;

    /* - - - Project - - - */
    ADD(NM_TITLE, NULL, 0,   0, 0, MSG_MENU_PROJECT);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(ACTION_FILE_NEW));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "O", 0, 0, ACTION_UD(ACTION_FILE_LOAD_UTF8));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_FILE_LOAD_LATIN1));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_FILE_LOAD_LATIN2));

    /* Open Recent submenu – only shown when there are entries */
    if (as && as->recentCount > 0) {
       // BAR(NM_ITEM);
        ADD(NM_ITEM, NULL, 0, 0, 0, MSG_OPEN_RECENT); /* branch: resolved by locale */
        for (r = 0; r < as->recentCount && r < APPSETTINGS_MAX_RECENT; r++) {
            const char *encName = (as->recentEncodings[r] == 1) ? "Latin-1" :
                                  (as->recentEncodings[r] == 2) ? "Latin-2" : "UTF-8";
            const char *fp = FilePart((STRPTR)as->recentFiles[r]);

            snprintf(&s_recentLabels[r][0], 39,
                     "%s (%s)", fp ? fp : "?", encName);
            s_recentLabels[r][39] = 0;

            ADD(NM_SUB, (STRPTR)&s_recentLabels[r][0], 0, 0, 0,
                ACTION_UD(ACTION_OPEN_RECENT_0 + r));
        }
    }
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "Q", 0, 0, ACTION_UD(ACTION_FILE_CLOSE));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "S", 0, 0, ACTION_UD(ACTION_FILE_SAVE_UTF8));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_FILE_SAVE_ESCAPED));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_FILE_SAVE_LATIN1));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_FILE_SAVE_LATIN2));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(ACTION_FILE_ABOUT));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(ACTION_FILE_QUIT));

    /* - - - Edit - - - */
    ADD(NM_TITLE, NULL, 0,   0, 0, MSG_MENU_EDIT);
    ADD(NM_ITEM,  NULL, "X", 0, 0, ACTION_UD(ACTION_EDIT_CUT));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "C", 0, 0, ACTION_UD(ACTION_EDIT_COPY));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_EDIT_COPY_LATIN1));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_EDIT_COPY_LATIN2));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "V", 0, 0, ACTION_UD(ACTION_EDIT_PASTE));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_EDIT_PASTE_LATIN1));
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(ACTION_EDIT_PASTE_LATIN2));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "Z", 0, 0, ACTION_UD(ACTION_EDIT_UNDO));
    ADD(NM_ITEM,  NULL, "Y", 0, 0, ACTION_UD(ACTION_EDIT_REDO));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "E", 0, 0, ACTION_UD(ACTION_EDIT_OPEN_EMOJIBOX));

    /* - - - Navigate - - - */
    ADD(NM_TITLE, NULL, 0,   0, 0, MSG_MENU_NAVIGATE );
    ADD(NM_ITEM,  NULL, "F", 0, 0, ACTION_UD(ACTION_FIND_WINDOW));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "N", 0, 0, ACTION_UD(ACTION_FIND_NEXT));
    ADD(NM_ITEM,  NULL, "P", 0, 0, ACTION_UD(ACTION_FIND_PREVIOUS));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "G", 0, 0, ACTION_UD(ACTION_GOTO_LINE));
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "Ctrl+Shift+Tab", NM_COMMANDSTRING, 0, ACTION_UD(ACTION_PREVTEXT));
    ADD(NM_ITEM,  NULL, "Ctrl+Tab", NM_COMMANDSTRING, 0, ACTION_UD(ACTION_NEXTTEXT));
    /* - - - Settings - - - */
    ADD(NM_TITLE, NULL, 0,   0, 0, MSG_SETTINGS);
    ADD(NM_ITEM,  NULL, "K", 0, 0, ACTION_UD(ACTION_SETTINGS_WINDOW));
    ADD(NM_ITEM,  NULL, "T", 0, 0, ACTION_UD(ACTION_FONTS_WINDOW));
    ADD(NM_ITEM,  NULL, 0, CHECKIT|MENUTOGGLE|CHECKED, 0, ACTION_UD(ACTION_SETTINGS_WORDWRAP));
    ADD(NM_ITEM,  NULL, 0, CHECKIT|MENUTOGGLE, 0, ACTION_UD(ACTION_SETTINGS_FORCEMONOSPACE));
    ADD(NM_ITEM,  NULL, 0, CHECKIT|MENUTOGGLE, 0, ACTION_UD(ACTION_SETTINGS_APPLYANSI));
/*old, removed    ADD(NM_ITEM,  NULL, 0, CHECKIT|MENUTOGGLE, 0, ACTION_UD(ACTION_SETTINGS_ANSI_UNIX_COLORS));*/
    ADD(NM_ITEM,  NULL, "Ctrl-", NM_COMMANDSTRING, 0, ACTION_UD(ACTION_SETTINGS_FONTSIZEM));
    ADD(NM_ITEM,  NULL, "Ctrl+", NM_COMMANDSTRING, 0, ACTION_UD(ACTION_SETTINGS_FONTSIZEP));

    ADD(NM_END, NULL, 0, 0, 0, 0);

}

/*
 * Fill nm_Label fields from locale (titles) and cached action names (items).
 */
static void resolveMenuLabels(struct NewMenu *tmpl)
{
    int i;
    for (i = 0; tmpl[i].nm_Type != NM_END; i++) {
        if (tmpl[i].nm_Label == NM_BARLABEL) continue;
        if (tmpl[i].nm_Label != NULL)        continue;
        {
            ULONG udata = (ULONG)tmpl[i].nm_UserData;
            if (udata > 0xFFFF) {
                /* ACTION_* item */
                ULONG actionID = (udata - (1 << 16)) >> 16;
                EgAction *a  = EgAction_Get(actionID);
                tmpl[i].nm_Label = (STRPTR)(a && a->name ? a->name : "???");
            } else {
                /* MSG_* title/branch */
                tmpl[i].nm_Label = (STRPTR)LOC(udata);
            }
        }
    }
}

BOOL EgMenu_Create(EgMenu *bm, struct Screen *screen, struct Window *window, AppSettings *settings)
{
    int i;
    ULONG wordWrap = 0;

    if (!bm || !screen || !window || !GadToolsBase) return FALSE;

    bm->menu       = NULL;
    bm->visualInfo = NULL;

    bm->visualInfo = GetVisualInfo(screen, TAG_END);
    if (!bm->visualInfo) {
        printf("EgMenu_Create: GetVisualInfo failed\n");
        return FALSE;
    }

    buildMenuTemplate(settings);

    /* Sync the Word Wrap checkmark from appSettings */
    wordWrap = (app && app->appSettings.wordWrap) ? 1 : 0;
    for (i = 0; s_menuTemplate[i].nm_Type != NM_END; i++) {
        ULONG udata = (ULONG)s_menuTemplate[i].nm_UserData;
        if (udata > 0xFFFF && ((udata - (1 << 16)) >> 16) == ACTION_SETTINGS_WORDWRAP) {
            if (wordWrap)
                s_menuTemplate[i].nm_Flags |=  CHECKED;
            else
                s_menuTemplate[i].nm_Flags &= ~CHECKED;
            break;
        }
    }

    /* Sync the Force Monospace checkmark from appSettings */
    {
        int monoOn = (app && app->appSettings.monospace) ? 1 : 0;
        for (i = 0; s_menuTemplate[i].nm_Type != NM_END; i++) {
            ULONG udata = (ULONG)s_menuTemplate[i].nm_UserData;
            if (udata > 0xFFFF && ((udata - (1 << 16)) >> 16) == ACTION_SETTINGS_FORCEMONOSPACE) {
                if (monoOn)
                    s_menuTemplate[i].nm_Flags |=  CHECKED;
                else
                    s_menuTemplate[i].nm_Flags &= ~CHECKED;
                break;
            }
        }
    }

    resolveMenuLabels(s_menuTemplate);

    bm->menu = CreateMenus(s_menuTemplate, TAG_END);
    if (!bm->menu) {
        printf("EgMenu_Create: CreateMenus failed\n");
        FreeVisualInfo(bm->visualInfo);
        bm->visualInfo = NULL;
        return FALSE;
    }

    if (!LayoutMenus(bm->menu, bm->visualInfo,
                     GTMN_NewLookMenus, TRUE,
                     TAG_END)) {
        printf("EgMenu_Create: LayoutMenus failed\n");
        FreeMenus(bm->menu);
        FreeVisualInfo(bm->visualInfo);
        bm->menu       = NULL;
        bm->visualInfo = NULL;
        return FALSE;
    }

    SetMenuStrip(window, bm->menu);
    return TRUE;
}

void EgMenu_Close(EgMenu *bm, struct Window *window)
{
    if (!bm) return;

    if (window && bm->menu)
        ClearMenuStrip(window);

    if (bm->menu) {
        FreeMenus(bm->menu);
        bm->menu = NULL;
    }

    if (bm->visualInfo) {
        FreeVisualInfo(bm->visualInfo);
        bm->visualInfo = NULL;
    }
}

struct MenuItem *EgMenu_FindItemByActionID(EgMenu *bm, ULONG actionID)
{
    struct Menu *menu;
    if (!bm || !bm->menu) return NULL;
    for (menu = bm->menu; menu; menu = menu->NextMenu) {
        struct MenuItem *item;
        for (item = menu->FirstItem; item; item = item->NextItem) {
            ULONG udata = (ULONG)GTMENUITEM_USERDATA(item);
            if (udata > 0xFFFF) {
                ULONG id = (udata - (1 << 16)) >> 16;
                if (id == actionID) return item;
            }
        }
    }
    return NULL;
}

LONG EgMenu_ToActionID(EgMenu *bm, UWORD menuCode)
{
    struct MenuItem *item;
    ULONG udata;

    if (!bm || !bm->menu) return -1;
    if (menuCode == MENUNULL) return -1;

    item = ItemAddress(bm->menu, menuCode);
    if (!item) return -1;

    udata = (ULONG)GTMENUITEM_USERDATA(item);
    if (udata > 0xFFFF)
        return (LONG)((udata - (1 << 16)) >> 16);

    return -1; /* title/branch item, not an action */
}

BOOL EgMenu_Rebuild(EgMenu *bm, struct Screen *screen, struct Window *window, AppSettings *settings)
{
    EgMenu_Close(bm, window);
    return EgMenu_Create(bm, screen, window, settings);
}
