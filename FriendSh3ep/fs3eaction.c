/*
 * fs3eaction.c - Action dispatch and implementations for FriendSh3ep.
 *
 * Pattern adapted from EmojiGear/egaction.c.
 *
 * Each FS3EActionID maps to one FS3EAction entry: a function pointer plus the
 * MSG_* id that names the action.  FS3EAction_Execute() is the single call site
 * for the WMHI_MENUPICK handler in friendsh3ep.c (and any future key shortcuts).
 */

#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>

#include "fs3eaction.h"
#include "fs3elocale.h"
#include "friendsh3ep.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "fs3ethemeview.h"
#include "fs3esettings.h"

/* Forward-declared in friendsh3ep.c */
extern void FS3EApp_ApplyFontSettings(void);
extern void cleanexit(const char *pmessage);

#define FONTSIZE_MIN  8
#define FONTSIZE_MAX 24
#define FONTSIZE_STEP  2

/* -------------------------------------------------------------------------
 * Action table – MUST stay in sync with enum FS3EActionID in fs3eaction.h
 * -------------------------------------------------------------------------*/
static FS3EAction s_actions[FS3EACTION_COUNT] = {
    /* FS3EACTION_ACCOUNTS         */ { Action_Accounts,       MSG_MENU_ACCOUNTS,         NULL },
    /* FS3EACTION_NEW_TOOT         */ { Action_NewToot,        MSG_MENU_NEW_TOOT,         NULL },
    /* FS3EACTION_ABOUT            */ { Action_About,          MSG_MENU_ABOUT,            NULL },
    /* FS3EACTION_QUIT             */ { Action_Quit,           MSG_MENU_QUIT,             NULL },

    /* FS3EACTION_VIEW_USER        */ { Action_ViewUser,       MSG_VIEW_USER,             NULL },
    /* FS3EACTION_VIEW_HOME        */ { Action_ViewHome,       MSG_VIEW_HOME,             NULL },
    /* FS3EACTION_VIEW_LOCAL       */ { Action_ViewLocal,      MSG_VIEW_LOCAL,            NULL },
    /* FS3EACTION_VIEW_FEDERATED   */ { Action_ViewFederated,  MSG_VIEW_FEDERATED,        NULL },
    /* FS3EACTION_VIEW_SEARCH      */ { Action_ViewSearch,     MSG_VIEW_SEARCH,           NULL },

    /* FS3EACTION_SETTINGS_THEME   */ { Action_SettingsTheme,  MSG_SETTINGS_THEME,        NULL },
    /* FS3EACTION_SETTINGS_GENERAL */ { Action_SettingsGeneral,MSG_SETTINGS_GENERAL,      NULL },
    /* FS3EACTION_SETTINGS_FONTSIZEM*/{ Action_FontSizeMinus,  MSG_SETTINGS_FONTSIZEM,    NULL },
    /* FS3EACTION_SETTINGS_FONTSIZEP*/{ Action_FontSizePlus,   MSG_SETTINGS_FONTSIZEP,    NULL },
};

/* -------------------------------------------------------------------------
 * Dispatch infrastructure
 * -------------------------------------------------------------------------*/

void FS3EAction_Init(void)
{
    ULONG i;
    for (i = 0; i < FS3EACTION_COUNT; i++)
        s_actions[i].name = LOC(s_actions[i].nameStringID);
}

FS3EAction *FS3EAction_Get(ULONG actionID)
{
    if (actionID >= FS3EACTION_COUNT) return NULL;
    return &s_actions[actionID];
}

BOOL FS3EAction_Execute(ULONG actionID, struct App *ctx)
{
    FS3EAction *a = FS3EAction_Get(actionID);
    if (!a || !a->func) return FALSE;
    return a->func(ctx);
}

/* -------------------------------------------------------------------------
 * FriendSh3ep menu actions
 * -------------------------------------------------------------------------*/

BOOL Action_Accounts(struct App *ctx)
{
    if (!ctx) return FALSE;
    FS3ELoginView_Open(&ctx->loginView);
    return TRUE;
}

BOOL Action_NewToot(struct App *ctx)
{
    if (!ctx) return FALSE;
    FS3ETootView_Open(&ctx->tootView);
    return TRUE;
}

BOOL Action_About(struct App *ctx)
{
    struct EasyStruct es;
    (void)ctx;

    es.es_StructSize   = sizeof(es);
    es.es_Flags        = 0;
    es.es_Title        = (UBYTE *)"About FriendSh3ep";
    es.es_TextFormat   = (UBYTE *)"FriendSh3ep\n"
                                  "A Mastodon client for AmigaOS\n"
                                  "Version " FRIENDSH3EP_VERSION "\n\n"
                                  "Built with utf8rastport.library\n"
                                  "and ReAction gadgets.";
    es.es_GadgetFormat = (UBYTE *)"OK";

    EasyRequestArgs(NULL, &es, NULL, NULL);
    return TRUE;
}

BOOL Action_Quit(struct App *ctx)
{
    (void)ctx;
    cleanexit(NULL);
    return TRUE; /* not reached */
}

/* -------------------------------------------------------------------------
 * View menu actions
 * -------------------------------------------------------------------------*/

BOOL Action_ViewUser(struct App *ctx)
{
    (void)ctx;
    /* TODO: switch timeline to user view */
    return TRUE;
}

BOOL Action_ViewHome(struct App *ctx)
{
    (void)ctx;
    /* TODO: switch timeline to home view */
    return TRUE;
}

BOOL Action_ViewLocal(struct App *ctx)
{
    (void)ctx;
    /* TODO: switch timeline to local view */
    return TRUE;
}

BOOL Action_ViewFederated(struct App *ctx)
{
    (void)ctx;
    /* TODO: switch timeline to federated view */
    return TRUE;
}

BOOL Action_ViewSearch(struct App *ctx)
{
    (void)ctx;
    /* TODO: open search view */
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Settings menu actions
 * -------------------------------------------------------------------------*/

BOOL Action_SettingsTheme(struct App *ctx)
{
    if (!ctx) return FALSE;
    FS3EThemeView_Open(&ctx->themeView);
    return TRUE;
}

BOOL Action_SettingsGeneral(struct App *ctx)
{
    (void)ctx;
    /* TODO: open general settings window */
    return TRUE;
}

BOOL Action_FontSizeMinus(struct App *ctx)
{
    if (!ctx) return FALSE;

    if (ctx->settings.fontPointSize > FONTSIZE_MIN)
        ctx->settings.fontPointSize -= FONTSIZE_STEP;
    FS3EApp_ApplyFontSettings();
    return TRUE;
}

BOOL Action_FontSizePlus(struct App *ctx)
{
    if (!ctx) return FALSE;

    if (ctx->settings.fontPointSize < FONTSIZE_MAX)
        ctx->settings.fontPointSize += FONTSIZE_STEP;
    FS3EApp_ApplyFontSettings();
    return TRUE;
}
