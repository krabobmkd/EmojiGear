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

    FS3EACTION_VIEW_REFRESH,

    /* Timeline menu -- see fs3elocale.h's MSG_MENU_TIMELINE comment for
     * the Space/AUTOSCROLL_STOP-vs-NEXT_TOOT key-sharing note. */
    FS3EACTION_TIMELINE_NEXT_TOOT,
    FS3EACTION_TIMELINE_TOP,
    FS3EACTION_TIMELINE_AUTOSCROLL_PLAY,
    FS3EACTION_TIMELINE_AUTOSCROLL_STOP,
    FS3EACTION_TIMELINE_COPY_TEXT,

    /* User menu -- acts on whichever profile is open in the Search
     * view's FS3ESEARCH_USER_PROFILE sub-mode. */
    FS3EACTION_USER_COPY_URL,
    FS3EACTION_USER_FOLLOW,
    FS3EACTION_USER_UNFOLLOW,
    FS3EACTION_USER_MASK,
    FS3EACTION_USER_UNMASK,
    FS3EACTION_USER_BLOCK,
    FS3EACTION_USER_UNBLOCK,
    FS3EACTION_USER_BLOCK_SERVER,
    FS3EACTION_USER_UNBLOCK_SERVER,

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
BOOL Action_Refresh(struct App *ctx);

/* Timeline menu -- see FS3EACTION_TIMELINE_* in the enum above.
 * NEXT_TOOT is implemented (an FS3ETimer-driven animated scroll, see
 * Action_TimelineNextToot's own comment in fs3eaction.c). TOP is
 * implemented too (an instant TTIMELINE_ScrollToNewest jump, see
 * Action_TimelineTop). AUTOSCROLL_PLAY/STOP ("Play mode") are
 * implemented too (a second, independent repeating FS3ETimer that fires
 * one Next-toot move every settings.playTootTimeSec seconds, see
 * FS3EPlayModeAnim_Hook in fs3eaction.c). Only COPY_TEXT is still a
 * stub (returns TRUE, no-op): it needs a "currently focused toot"
 * concept TootTimeline doesn't have yet. Menu entries + key wiring are
 * in place for all five either way -- TOP/COPY_TEXT (U/C) are real
 * Amiga+Key CommKeys (see fs3emenu.c), reaching here via the normal
 * WMHI_MENUPICK path; NEXT_TOOT/AUTOSCROLL_* (Space/P) are plain
 * keypresses instead, wired directly in friendsh3ep.c's WMHI_RAWKEY
 * (Space can't be a sensible CommKey, P already belongs to
 * Settings->General). */
BOOL Action_TimelineNextToot(struct App *ctx);
BOOL Action_TimelineTop(struct App *ctx);
BOOL Action_TimelineAutoscrollPlay(struct App *ctx);
BOOL Action_TimelineAutoscrollStop(struct App *ctx);
BOOL Action_TimelineCopyText(struct App *ctx);

/* Stops whichever FS3ETimer-driven scroll animation is currently running
 * (today: just "Next toot"'s, see s_nextTootAnim in fs3eaction.c; meant
 * to also cover the future continuous autoscroll timer once
 * Action_TimelineAutoscrollPlay is implemented, so callers of this
 * function never need to know which animation(s) exist). A harmless
 * no-op if nothing is currently animating. Call whenever the user
 * manually scrolls by some OTHER means, so a still-running animation
 * doesn't fight the user's own input -- see its call sites in
 * friendsh3ep.c (Up/Down arrow keys, and TootTimeline's own
 * TTIMELINE_ScrollStarted notification for its internal mouse-drag
 * scroll). Not itself an FS3EActionFunc/menu entry. */
void Action_TimelineStopScrollAnimation(void);

/* User menu -- see FS3EACTION_USER_* in the enum above. Follow/Unfollow/
 * CopyProfileURL are fully implemented (existing Action_ToggleFollow /
 * Clipboard_WriteText plumbing, nothing new needed). Mask/Unmask/Block/
 * Unblock (user and server) are stubs: Mastodon's mute/block/domain-block
 * endpoints have no request/reply plumbing in network_fs3e/ yet -- that's
 * the "following dev" work these entries are scaffolding for. */
BOOL Action_UserCopyProfileURL(struct App *ctx);
BOOL Action_UserFollow(struct App *ctx);
BOOL Action_UserUnfollow(struct App *ctx);
BOOL Action_UserMask(struct App *ctx);
BOOL Action_UserUnmask(struct App *ctx);
BOOL Action_UserBlock(struct App *ctx);
BOOL Action_UserUnblock(struct App *ctx);
BOOL Action_UserBlockServer(struct App *ctx);
BOOL Action_UserUnblockServer(struct App *ctx);

BOOL Action_SettingsTheme(struct App *ctx);
BOOL Action_SettingsGeneral(struct App *ctx);
BOOL Action_FontSizeMinus(struct App *ctx);
BOOL Action_FontSizePlus(struct App *ctx);

/* -------------------------------------------------------------------------
 * Toot actions -- one specific toot is always involved, so these take
 * extra parameters and don't fit FS3EActionFunc/s_actions[]. Called
 * directly from wherever the target toot is already known: today that's
 * TootTimeline's TTL_HOT_FAVORITE click (friendsh3ep.c); once a "This
 * Toot" context menu exists (see todo.txt) it calls the very same
 * function for its Fave entry, so the two can never drift apart.
 * -------------------------------------------------------------------------*/

/* Toggles favourite state on postId (POSTs .../favourite if
 * !currentlyFavourited, else .../unfavourite) and sends the request to the
 * network process. Returns FALSE immediately on a local problem (no
 * account, alloc failure, send failure); the actual server-side result
 * arrives later as an FS3ENETQ_FAVORITE reply, handled in friendsh3ep.c by
 * updating TootTimeline via TTIMELINE_UpdatePost. */
BOOL Action_ToggleFavorite(struct App *ctx, const char *postId, BOOL currentlyFavourited);

/* Same shape as Action_ToggleFavorite, one specific account instead of one
 * specific toot: toggles follow state on accountId (POSTs .../follow if
 * !currentlyFollowing, else .../unfollow). The server-side result arrives
 * later as an FS3ENETQ_FOLLOW reply, handled in friendsh3ep.c by updating
 * TootTimeline's profile header via TTIMELINE_UpdateProfileFollow. Called
 * from TootTimeline's TTL_HOT_FOLLOW click today; a future "User" context
 * menu (see todo.txt) would call the same function for its Follow entry. */
BOOL Action_ToggleFollow(struct App *ctx, const char *accountId, BOOL currentlyFollowing);

#endif /* FS3EACTION_H */
