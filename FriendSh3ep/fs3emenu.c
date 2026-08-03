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

/* Bumped from 32 for the new Timeline/User menus (see buildMenuTemplate
 * below) -- addEntry() has no bounds check, so this must stay ahead of
 * the actual ADD()/BAR() call count (42 as of this change, including the
 * final NM_END terminator) or it silently overwrites whatever static
 * data follows s_menuTemplate[]. Left some headroom for future entries. */
#define MENU_TEMPLATE_MAX 56

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

/* 0-based position of the "User" NM_TITLE among all titles built by
 * buildMenuTemplate below (FriendSh3ep, View, Timeline, User, Settings)
 * -- OnMenu/OffMenu (see FS3EMenu_SetUserMenuEnabled) address a title by
 * this positional index, Intuition has no by-name lookup. Must stay in
 * sync if a title is ever added/removed/reordered before "User". */
#define FS3E_MENU_TITLE_USER 3

static void buildMenuTemplate(void)
{
    s_n = 0;

    /* - - - FriendSh3ep - - - */
    ADD(NM_TITLE, NULL, 0,   0, 0, MSG_MENU_FRIENDSH3EP);
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(FS3EACTION_ACCOUNTS) | MSG_MENU_ACCOUNTS);
    ADD(NM_ITEM,  NULL, "T",   0, 0, ACTION_UD(FS3EACTION_NEW_TOOT) | MSG_MENU_NEW_TOOT);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, 0,   0, 0, ACTION_UD(FS3EACTION_ABOUT)    | MSG_MENU_ABOUT);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "Q", 0, 0, ACTION_UD(FS3EACTION_QUIT)     | MSG_MENU_QUIT);

    /* - - - View - - - */
    ADD(NM_TITLE, NULL, 0, 0, 0, MSG_MENU_VIEW);
    /* the following mimics the viewmode enum */
    ADD(NM_ITEM,  NULL, "F1", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_USER)      | MSG_VIEW_USER);
    ADD(NM_ITEM,  NULL, "F2", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_HOME)      | MSG_VIEW_HOME);
    ADD(NM_ITEM,  NULL, "F3", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_LOCAL)     | MSG_VIEW_LOCAL);
    ADD(NM_ITEM,  NULL, "F4", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_FEDERATED) | MSG_VIEW_FEDERATED);
    ADD(NM_ITEM,  NULL, "F", 0, 0, ACTION_UD(FS3EACTION_VIEW_SEARCH)    | MSG_VIEW_SEARCH);
    ADD(NM_ITEM,  NULL, "F6", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_NOTIF)     | MSG_VIEW_NOTIFICATIONS);
    ADD(NM_ITEM,  NULL, "F7", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_BOOKMARKS) | MSG_VIEW_BOOKMARK);
    ADD(NM_ITEM,  NULL, "F8", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_NEWS)    | MSG_VIEW_NEWS);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "F5", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_VIEW_REFRESH) | MSG_VIEW_REFRESH);

    /* - - - Timeline - - -
     * U and C are real GadTools CommKeys (no NM_COMMANDSTRING -- same
     * "bare single letter" convention as T/Q/F above), so Intuition fires
     * them itself as Amiga+U/Amiga+C via the normal WMHI_MENUPICK path,
     * no WMHI_RAWKEY handling needed for those two.
     *
     * Space and P stay plain (unmodified) key shortcuts instead: Space
     * can't be a sensible CommKey, and P is already Settings->General's
     * CommKey (see below) so Autoscroll Play keeps the bare-keypress
     * route rather than colliding with it. NM_COMMANDSTRING here is just
     * decorative label text for those two, same convention F1-F8/F5/
     * "Ctrl-"/"Ctrl+" already use; the real handling is
     * friendsh3ep.c's WMHI_RAWKEY (keys 0x40 Space, 0x19 P), calling the
     * same Action_Timeline* functions these menu picks would call.
     * Space is intentionally reused for two mutually-exclusive-by-state
     * items (NEXT_TOOT / AUTOSCROLL_STOP) -- see MSG_MENU_TIMELINE's own
     * comment in fs3elocale.h. */
    ADD(NM_TITLE, NULL, 0, 0, 0, MSG_MENU_TIMELINE);
    ADD(NM_ITEM,  NULL, "Space", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_TIMELINE_NEXT_TOOT)       | MSG_TIMELINE_NEXT_TOOT);
    ADD(NM_ITEM,  NULL, "U",     0,                0, ACTION_UD(FS3EACTION_TIMELINE_TOP)             | MSG_TIMELINE_TOP);
    ADD(NM_ITEM,  NULL, "P",     NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_TIMELINE_AUTOSCROLL_PLAY) | MSG_TIMELINE_AUTOSCROLL_PLAY);
    ADD(NM_ITEM,  NULL, "Space", NM_COMMANDSTRING, 0, ACTION_UD(FS3EACTION_TIMELINE_AUTOSCROLL_STOP) | MSG_TIMELINE_AUTOSCROLL_STOP);
    BAR(NM_ITEM);
    ADD(NM_ITEM,  NULL, "C",     0,                0, ACTION_UD(FS3EACTION_TIMELINE_COPY_TEXT)       | MSG_TIMELINE_COPY_TEXT);

    /* - - - User - - -
     * Acts on whichever profile is open in the Search view's
     * FS3ESEARCH_USER_PROFILE sub-mode -- no key equivalents, no dynamic
     * enable/disable yet (see each Action_User* function's own comment
     * in fs3eaction.c for the no-op-if-nothing-open guard that covers
     * this for now). */
    ADD(NM_TITLE, NULL, 0, 0, 0, MSG_MENU_USER);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_COPY_URL)      | MSG_USER_COPY_URL);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_FOLLOW)        | MSG_USER_FOLLOW);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_UNFOLLOW)      | MSG_USER_UNFOLLOW);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_MASK)         | MSG_USER_MASK);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_UNMASK)       | MSG_USER_UNMASK);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_BLOCK)        | MSG_USER_BLOCK);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_UNBLOCK)      | MSG_USER_UNBLOCK);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_BLOCK_SERVER)  | MSG_USER_BLOCK_SERVER);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_USER_UNBLOCK_SERVER)| MSG_USER_UNBLOCK_SERVER);

    /* - - - Settings - - - */
    ADD(NM_TITLE, NULL, 0, 0, 0, MSG_MENU_SETTINGS);
    ADD(NM_ITEM,  NULL, 0, 0, 0, ACTION_UD(FS3EACTION_SETTINGS_THEME)   | MSG_SETTINGS_THEME);
    ADD(NM_ITEM,  NULL, "P", 0, 0, ACTION_UD(FS3EACTION_SETTINGS_GENERAL) | MSG_SETTINGS_GENERAL);
    ADD(NM_ITEM,  NULL, "N", 0, 0, ACTION_UD(FS3EACTION_NETWORK_VIEW) | MSG_NETWORKV_TITLE);
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
        return FALSE;
    }

    buildMenuTemplate();
    resolveMenuLabels(s_menuTemplate);

    m->menu = CreateMenus(s_menuTemplate, TAG_END);
    if (!m->menu) {
        FreeVisualInfo(m->visualInfo);
        m->visualInfo = NULL;
        return FALSE;
    }

    if (!LayoutMenus(m->menu, m->visualInfo,
                     GTMN_NewLookMenus, TRUE,
                     TAG_END)) {
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

/* Enable/disable the whole "User" menu title at once (OnMenu/OffMenu on
 * the title itself, NOITEM/NOSUB -- not each item individually), so a
 * disabled menu can't even be pulled down, not just show greyed entries.
 * Safe to call every time regardless of whether the state actually
 * changed -- OnMenu/OffMenu just set/clear one flag on the live menu
 * strip, cheap either way. See FS3EApp_CheckConnectionState in
 * friendsh3ep.c, the single place this is called from: it re-derives
 * "is the Search view showing someone else's profile" after every state
 * change that could affect the answer (view switches, profile opens,
 * network replies), rather than this function being sprinkled across
 * every individual call site that could change it. */
void FS3EMenu_SetUserMenuEnabled(FS3EMenu *m, struct Window *window, BOOL enabled)
{
    ULONG menuNum;

    if (!m || !m->menu || !window) return;

    menuNum = FULLMENUNUM(FS3E_MENU_TITLE_USER, NOITEM, NOSUB);
    if (enabled) OnMenu(window, menuNum);
    else         OffMenu(window, menuNum);
}
