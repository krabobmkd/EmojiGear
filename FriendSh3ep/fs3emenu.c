/*
 * fs3emenu.c - GadTools menu management for FriendSh3ep.
 *
 * Pattern adapted from EmojiGear/egmenu.c.
 */

#include "fs3emenu.h"
#include "fs3elocale.h"

#include <stdio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/dos.h>
#include <libraries/gadtools.h>

extern struct Library *GadToolsBase;

/*
 * nm_UserData encoding:
 *   upper 16 bits non-zero → FS3EACTION_* action id encoded via ACTION_UD,
 *                             MSG_* label id in lower 16 bits
 *   upper 16 bits zero     → MSG_* locale id only (title item)
 */
#define ACTION_UD(a)  ((ULONG)((a) + 1) << 16)

#define MENU_TEMPLATE_MAX 32

static struct NewMenu s_menuTemplate[MENU_TEMPLATE_MAX];
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

static void buildMenuTemplate(void)
{
    s_n = 0;

    /* - - - FriendSh3ep - - - */
    ADD(NM_TITLE, NULL, 0,   0, 0, MSG_MENU_FRIENDSH3EP);
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(FS3EACTION_ACCOUNTS) | MSG_MENU_ACCOUNTS);
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(FS3EACTION_NEW_TOOT) | MSG_MENU_NEW_TOOT);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(FS3EACTION_ABOUT)    | MSG_MENU_ABOUT);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "Q", 0, 0, ACTION_UD(FS3EACTION_QUIT)     | MSG_MENU_QUIT);

    /* - - - View - - - */
    ADD(NM_TITLE, NULL, 0, 0, 0, MSG_MENU_VIEW);
    ADD(NM_ITEM,  NULL, "F1", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_USER)      | MSG_VIEW_USER);
    ADD(NM_ITEM,  NULL, "F2", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_HOME)      | MSG_VIEW_HOME);
    ADD(NM_ITEM,  NULL, "F3", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_LOCAL)     | MSG_VIEW_LOCAL);
    ADD(NM_ITEM,  NULL, "F4", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_FEDERATED) | MSG_VIEW_FEDERATED);
    ADD(NM_ITEM,  NULL, "F5", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_SEARCH)    | MSG_VIEW_SEARCH);

    /* - - - Settings - - - */
    ADD(NM_TITLE, NULL, 0, 0, 0, MSG_MENU_SETTINGS);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_SETTINGS_THEME)   | MSG_SETTINGS_THEME);
    ADD(NM_ITEM,  NULL, "P", 0, 0, ACTION_UD(FS3EACTION_SETTINGS_GENERAL) | MSG_SETTINGS_GENERAL);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "Ctrl-", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_SETTINGS_FONTSIZEM) | MSG_SETTINGS_FONTSIZEM);
    ADD(NM_ITEM,  NULL, "Ctrl+", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_SETTINGS_FONTSIZEP) | MSG_SETTINGS_FONTSIZEP);

    ADD(NM_END, NULL, 0, 0, 0, 0);
}

/*
 * Fill nm_Label fields from locale.
 * Both title items (lower 16 bits only) and action items (MSG_* in lower 16
 * bits, action id in upper 16) resolve their label the same way.
 */
static void resolveMenuLabels(struct NewMenu *tmpl)
{
    int i;
    for (i = 0; tmpl[i].nm_Type != NM_END; i++) {
        if (tmpl[i].nm_Label == NM_BARLABEL) continue;
        if (tmpl[i].nm_Label != NULL)        continue;
        tmpl[i].nm_Label = (STRPTR)LOC((ULONG)tmpl[i].nm_UserData & 0xFFFF);
    }
}

BOOL FS3EMenu_Create(FS3EMenu *m, struct Screen *screen, struct Window *window)
{
    if (!m || !screen || !window || !GadToolsBase) return FALSE;

    m->menu       = NULL;
    m->visualInfo = NULL;

    m->visualInfo = GetVisualInfo(screen, TAG_END);
    if (!m->visualInfo) {
        printf("FS3EMenu_Create: GetVisualInfo failed\n");
        return FALSE;
    }

    buildMenuTemplate();
    resolveMenuLabels(s_menuTemplate);

    m->menu = CreateMenus(s_menuTemplate, TAG_END);
    if (!m->menu) {
        printf("FS3EMenu_Create: CreateMenus failed\n");
        FreeVisualInfo(m->visualInfo);
        m->visualInfo = NULL;
        return FALSE;
    }

    if (!LayoutMenus(m->menu, m->visualInfo,
                     GTMN_NewLookMenus, TRUE,
                     TAG_END)) {
        printf("FS3EMenu_Create: LayoutMenus failed\n");
        FreeMenus(m->menu);
        FreeVisualInfo(m->visualInfo);
        m->menu       = NULL;
        m->visualInfo = NULL;
        return FALSE;
    }

    SetMenuStrip(window, m->menu);
    return TRUE;
}

void FS3EMenu_Close(FS3EMenu *m, struct Window *window)
{
    if (!m) return;

    if (window && m->menu)
        ClearMenuStrip(window);

    if (m->menu) {
        FreeMenus(m->menu);
        m->menu = NULL;
    }

    if (m->visualInfo) {
        FreeVisualInfo(m->visualInfo);
        m->visualInfo = NULL;
    }
}

BOOL FS3EMenu_Rebuild(FS3EMenu *m, struct Screen *screen, struct Window *window)
{
    FS3EMenu_Close(m, window);
    return FS3EMenu_Create(m, screen, window);
}

LONG FS3EMenu_ToActionID(FS3EMenu *m, UWORD menuCode)
{
    struct MenuItem *item;
    ULONG udata;

    if (!m || !m->menu) return -1;
    if (menuCode == MENUNULL) return -1;

    item = ItemAddress(m->menu, menuCode);
    if (!item) return -1;

    udata = (ULONG)GTMENUITEM_USERDATA(item);
    if (udata > 0xFFFF)
        return (LONG)((udata >> 16) - 1);

    return -1; /* title item, not an action */
}
