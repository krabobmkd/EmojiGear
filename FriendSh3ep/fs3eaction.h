#ifndef FS3EACTION_H
#define FS3EACTION_H

/*
 * fs3eaction.h - Table-driven action dispatch for FriendSh3ep.
 *
 * Actions are triggered by menu picks (FS3EMenu_ToActionID → FS3EAction_Execute)
 * or directly from button handlers.
 *
 * Two separate enums are used and must be kept independent:
 *   MSG_*        in fs3elocale.h  – locale string IDs
 *   FS3EActionID here             – action IDs
 *
 * The menu encodes both with ACTION_UD (defined in fs3emenu.c):
 *   nm_UserData = ACTION_UD(FS3EActionID) | MSG_label_id
 */

#include <exec/types.h>

struct App; /* forward decl */

typedef BOOL (*FS3EActionFunc)(struct App *app);

typedef struct {
    FS3EActionFunc  func;
    ULONG           nameStringID; /* MSG_* locale id for the action's name */
    const char     *name;         /* cached localized name (set by FS3EAction_Init) */
} FS3EAction;

/* Action IDs — one per user-triggerable operation */
typedef enum {
    /* FriendSh3ep menu */
    FS3EACTION_ACCOUNTS = 0,
    FS3EACTION_NEW_TOOT,
    FS3EACTION_ABOUT,
    FS3EACTION_QUIT,

    /* View menu */
    FS3EACTION_VIEW_USER,
    FS3EACTION_VIEW_HOME,
    FS3EACTION_VIEW_LOCAL,
    FS3EACTION_VIEW_FEDERATED,

    FS3EACTION_VIEW_SEARCH,
    FS3EACTION_VIEW_NOTIF,
    FS3EACTION_VIEW_BOOKMARKS,
    FS3EACTION_VIEW_NEWS,

    /* Settings menu */
    FS3EACTION_SETTINGS_THEME,
    FS3EACTION_SETTINGS_GENERAL,
    FS3EACTION_SETTINGS_FONTSIZEM,
    FS3EACTION_SETTINGS_FONTSIZEP,

    /* Must be last */
    FS3EACTION_COUNT
} FS3EActionID;

/* Cache localized action names. Call once after FS3ELocale_Init(). */
void        FS3EAction_Init(void);

/* Look up an action by ID. Returns NULL if out of range. */
FS3EAction *FS3EAction_Get(ULONG actionID);

/* Dispatch: run action actionID with ctx. Returns FALSE if invalid. */
BOOL        FS3EAction_Execute(ULONG actionID, struct App *ctx);

/* Individual action function declarations */
BOOL Action_Accounts(struct App *ctx);
BOOL Action_NewToot(struct App *ctx);
BOOL Action_About(struct App *ctx);
BOOL Action_Quit(struct App *ctx);

BOOL Action_ViewUser(struct App *ctx);
BOOL Action_ViewHome(struct App *ctx);
BOOL Action_ViewLocal(struct App *ctx);
BOOL Action_ViewFederated(struct App *ctx);
BOOL Action_ViewSearch(struct App *ctx);
BOOL Action_ViewNotif(struct App *ctx);
BOOL Action_ViewBookmark(struct App *ctx);
BOOL Action_ViewNews(struct App *ctx);

BOOL Action_SettingsTheme(struct App *ctx);
BOOL Action_SettingsGeneral(struct App *ctx);
BOOL Action_FontSizeMinus(struct App *ctx);
BOOL Action_FontSizePlus(struct App *ctx);

#endif /* FS3EACTION_H */
