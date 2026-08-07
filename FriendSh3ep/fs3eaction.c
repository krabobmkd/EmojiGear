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
#include "fs3esettingsview.h"
#include "fs3esettings.h"
#include "fs3erequests.h"
#include "fs3eaccounts.h"
#include "clipboard.h"
#include "fs3etimer.h"
#include "TootTimeline/fs3etoottimeline.h"
#include "network_fs3e/fs3enet.h"

/* Forward-declared in friendsh3ep.c */
extern void FS3EApp_ApplyFontSettings(void);
extern void cleanexit(const char *pmessage);
extern BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen);

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
    /* FS3EACTION_VIEW_NOTIF       */ { Action_ViewNotif,      MSG_VIEW_NOTIFICATIONS,            NULL },
    /* FS3EACTION_VIEW_BOOKMARKS   */ { Action_ViewBookmark,  MSG_VIEW_BOOKMARK,        NULL },
    /* FS3EACTION_VIEW_NEWS      */ { Action_ViewNews,     MSG_VIEW_NEWS,           NULL },

   /* FS3EACTION_VIEW_REFRESH   */ { Action_Refresh,     MSG_VIEW_REFRESH,           NULL },

    /* FS3EACTION_TIMELINE_NEXT_TOOT       */ { Action_TimelineNextToot,       MSG_TIMELINE_NEXT_TOOT,       NULL },
    /* FS3EACTION_TIMELINE_TOP             */ { Action_TimelineTop,            MSG_TIMELINE_TOP,             NULL },
    /* FS3EACTION_TIMELINE_AUTOSCROLL_PLAY */ { Action_TimelineAutoscrollPlay, MSG_TIMELINE_AUTOSCROLL_PLAY, NULL },
    /* FS3EACTION_TIMELINE_AUTOSCROLL_STOP */ { Action_TimelineAutoscrollStop, MSG_TIMELINE_AUTOSCROLL_STOP, NULL },
    /* FS3EACTION_TIMELINE_COPY_TEXT       */ { Action_TimelineCopyText,       MSG_TIMELINE_COPY_TEXT,       NULL },

    /* FS3EACTION_USER_COPY_URL      */ { Action_UserCopyProfileURL, MSG_USER_COPY_URL,      NULL },
    /* FS3EACTION_USER_FOLLOW        */ { Action_UserFollow,         MSG_USER_FOLLOW,        NULL },
    /* FS3EACTION_USER_UNFOLLOW      */ { Action_UserUnfollow,       MSG_USER_UNFOLLOW,      NULL },
    /* FS3EACTION_USER_MASK          */ { Action_UserMask,           MSG_USER_MASK,          NULL },
    /* FS3EACTION_USER_UNMASK        */ { Action_UserUnmask,         MSG_USER_UNMASK,        NULL },
    /* FS3EACTION_USER_BLOCK         */ { Action_UserBlock,          MSG_USER_BLOCK,         NULL },
    /* FS3EACTION_USER_UNBLOCK       */ { Action_UserUnblock,        MSG_USER_UNBLOCK,       NULL },
    /* FS3EACTION_USER_BLOCK_SERVER  */ { Action_UserBlockServer,    MSG_USER_BLOCK_SERVER,  NULL },
    /* FS3EACTION_USER_UNBLOCK_SERVER*/ { Action_UserUnblockServer,  MSG_USER_UNBLOCK_SERVER,NULL },

    /* FS3EACTION_SETTINGS_THEME   */ { Action_SettingsTheme,  MSG_SETTINGS_THEME,        NULL },
    /* FS3EACTION_SETTINGS_GENERAL */ { Action_SettingsGeneral,MSG_SETTINGS_GENERAL,      NULL },
    /* FS3EACTION_NETWORK_VIEW     */ { Action_NetworkView,    MSG_NETWORKV_TITLE,        NULL },
    /* FS3EACTION_SETTINGS_FONTSIZEM*/{ Action_FontSizeMinus,  MSG_SETTINGS_FONTSIZEM,    NULL },
    /* FS3EACTION_SETTINGS_FONTSIZEP*/{ Action_FontSizePlus,   MSG_SETTINGS_FONTSIZEP,    NULL },
};

/* Defined further down, next to s_easeTable/FS3ENextTootAnim_Hook --
 * forward-declared here so FS3EAction_Init (this file's one designated
 * one-time-setup entry point) can build the table once at startup. */
static void FS3EEase_BuildTable(void);

/* -------------------------------------------------------------------------
 * Dispatch infrastructure
 * -------------------------------------------------------------------------*/

void FS3EAction_Init(void)
{
    ULONG i;
    for (i = 0; i < FS3EACTION_COUNT; i++)
        s_actions[i].name = LOC(s_actions[i].nameStringID);

    FS3EEase_BuildTable();
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
    if (!FS3EApp_RequireRealAccount()) return TRUE; /* error shown, nothing else to do */
    /* Resets any leftover MODIFY/REPLY compose state from a previous open
     * (postId, title) -- doesn't touch bodyEditor's text itself, so an
     * in-progress draft still survives a close/reopen the way it always
     * has. Mirrors GID_TITLEBAR_NEWTOOT's handling in friendsh3ep.c. */
    FS3ETootView_SetComposeContext(&ctx->tootView, FS3ETOOT_KIND_NEW, NULL);
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
                                  " License GPL  sources at:\n"
                                  "github.com/krabobmkd/EmojiGear/FriendSh3ep"
                                  "Version " FRIENDSH3EP_VERSION "\n\n"
                                  "Built with utf8rastport.library\n"
                                  "and BOOPSI gadgets.";
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
    fs3e_setViewMode(VIEWMODE_User);
    return TRUE;
}

BOOL Action_ViewHome(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_Home);
    return TRUE;
}

BOOL Action_ViewLocal(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_Local);
    return TRUE;
}

BOOL Action_ViewFederated(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_Fed);
    return TRUE;
}

BOOL Action_ViewSearch(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_Search);
    /* in that case, give focus to search bar when possible */
    ctx->searchWordEditor_activateNextRound = 2;

    return TRUE;
}

BOOL Action_ViewNotif(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_Notifs);
    return TRUE;
}
BOOL Action_ViewBookmark(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_Bookmarks);
    return TRUE;
}
BOOL Action_ViewNews(struct App *ctx)
{
    (void)ctx;
    fs3e_setViewMode(VIEWMODE_News);
    return TRUE;
}
/* Menu-triggered counterpart of the F5 key (see friendsh3ep.c's
 * WMHI_RAWKEY handling, key 0x54) -- both converge on the same
 * FS3EApp_RefreshVisibleToots(), same "thin wrapper around the real
 * app-level call" pattern every other Action_View* above already uses
 * for its own key/menu counterpart. */
BOOL Action_Refresh(struct App *ctx)
{
    (void)ctx;
    FS3EApp_RefreshVisibleToots();
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Timeline menu actions. Move to Top (U) and Copy Toot Text (C) are real
 * GadTools CommKeys (see fs3emenu.c -- no NM_COMMANDSTRING), so Intuition
 * fires them itself via the normal WMHI_MENUPICK -> FS3EAction_Execute
 * path, same as every other real-CommKey item (T/Q/F/P...). Next toot and
 * both Autoscroll entries (Space/P) can't be real CommKeys -- Space isn't
 * a sensible one, and P already belongs to Settings->General -- so those
 * are wired directly in friendsh3ep.c's WMHI_RAWKEY instead, calling the
 * very same Action_ functions this table also points at.
 *
 * Next toot (Space, when autoscroll is idle) is implemented -- an
 * animated scroll that brings the post right after the currently
 * first-visible one all the way up to the gadget's own top edge, see
 * FS3ENextTootAnim_Hook/Action_TimelineNextToot below. Move to Top (U)
 * is implemented too -- an instant jump via TTIMELINE_ScrollToNewest,
 * see Action_TimelineTop below. AUTOSCROLL_PLAY/STOP ("Play mode") are
 * implemented too -- a second, independent repeating FS3ETimer
 * (s_playModeTimer) that fires one Next-toot move every
 * playTootTimeSec seconds, see FS3EPlayModeAnim_Hook/
 * Action_TimelineAutoscrollPlay below. Only COPY_TEXT remains a stub --
 * it needs a "currently focused toot" concept TootTimeline doesn't have
 * yet -- that's the one piece of follow-up work this menu/key
 * scaffolding is still preparing for.
 * -------------------------------------------------------------------------*/

/* "Next toot" animated scroll -- see TTIMELINE_NextTootScrollDelta's own
 * doc comment (TootTimeline/fs3etoottimeline.h) for why that metric is
 * computed by TootTimeline itself but the actual scrolling/animation
 * happens out here: the gadget only ever answers "how far", an
 * FS3ETimer (fs3etimer.h) drives *when*, one absolute
 * SetAttrs(TTIMELINE_ScrollY) per animation frame -- never
 * TTIMELINE_ScrollBy, so rounding never accumulates across frames the
 * way repeated relative deltas could.
 *
 * FS3ETimer's hook only receives the FS3ETimer* itself, no separate
 * user-data pointer -- s_nextTootAnim embeds one as its FIRST member so
 * the hook can cast the pointer it's given straight back to the wrapper
 * (same idiom BOOPSI's own INST_DATA(cl,o) uses to reach a subclass's
 * private struct from its base Object*). */
typedef struct {
    FS3ETimer timer;       /* MUST stay first -- see comment above */
    LONG      scrollStart; /* TTIMELINE_ScrollY at the moment Start() was called */
    LONG      scrollDelta; /* TTIMELINE_NextTootScrollDelta at that same moment */
} FS3ENextTootAnim;

static FS3ENextTootAnim s_nextTootAnim;

/* Play mode's own repeating timer -- see FS3EPlayModeAnim_Hook and
 * Action_TimelineAutoscrollPlay/Stop further down. Plain FS3ETimer, no
 * wrapper struct needed (unlike s_nextTootAnim above): its hook needs no
 * extra per-instance state beyond calling the shared next-toot helper. */
static FS3ETimer s_playModeTimer;

/* Ease-in-out lookup table -- turns the animation's linear 0..255
 * "how far through" index into a smoothed 0..255 output that starts
 * slow, accelerates through the middle, and slows back down into the
 * landing (a symmetric quadratic ease, the classic "S-curve"), so
 * FS3ENextTootAnim_Hook below can use s_easeTable[frac] instead of frac
 * itself. All integer, no float -- see FS3EEase_BuildTable's own
 * comment for the exact fixed-point derivation. Built once by
 * FS3EAction_Init(), read-only after that. */
static UBYTE s_easeTable[256];

/* First half (i = 0..127): treat i as the first half of the curve's
 * domain -- doubling it (x2 = i*2) maps that half back onto the FULL
 * 0..254 domain the plain quadratic ease-in shape (y = t^2) is defined
 * over, i.e. "as if i's own 0..127 range were really 0..0.5" per this
 * table's own design note. Squaring x2 and halving it
 * (((i*2)*(i*2))/2, all integer) computes that quadratic shape scaled
 * up by 256 (since x2 itself is already on a 0..255-ish scale, its
 * square lands two scale factors of 256 too high), so the trailing >>8
 * rescales back down to fit an unsigned char. Net effect, folded into
 * one shift: table[i] = (i*2)^2 / 512 = i^2/128.
 *
 * Second half (i = 128..255): rather than repeat a mirrored formula,
 * point-reflect the first half through the curve's own center
 * (127.5, 127.5) -- table[255-k] = 255-table[k] -- which is exactly
 * "the same curve with x and y both inverted", turning the ease-IN
 * shape already built into an ease-OUT tail, so the two halves meet at
 * the middle as one continuous accelerate-then-decelerate curve. */
static void FS3EEase_BuildTable(void)
{
    ULONG i;

    for (i = 0; i <= 127; i++) {
        ULONG x2 = i * 2;
        ULONG sq = x2 * x2;   /* (i*2)^2 */
        s_easeTable[i] = (UBYTE)((sq >> 1) >> 8); /* /2, then rescale into a byte */
    }
    for (i = 128; i <= 255; i++)
        s_easeTable[i] = (UBYTE)(255 - s_easeTable[255 - i]);
}

/* Runs every other vblank (divTick=2 -- ~25Hz@50Hz PAL / ~30Hz@60Hz
 * NTSC, see Action_TimelineNextToot below) for one second. ticks is real
 * elapsed vblanks since Start() (not a call-count -- see FS3ETimerHook's
 * own doc comment in fs3etimer.h), turned here into an 8-bit fixed-point
 * 0..256 "how far through the one-second animation" fraction (256 ==
 * 1.0, i.e. a normalized 0..1 counter as a 0..256 "fixed real"), then
 * run through s_easeTable for a smooth accelerate/decelerate feel
 * instead of a constant-speed linear scroll. linFrac is clamped to 256
 * (not looked up in the 0..255-sized table at that point) rather than
 * trusted to land exactly on it -- ticks can overshoot
 * FS3ETimer_Frequency() by a vblank or two depending on which absolute
 * vblank Start() happened to land on (divTick is checked against the
 * shared global vblank counter, not a per-timer-relative one -- see
 * FS3ETimer_IntServer's own comment in fs3etimer.c) -- so the very last
 * frame always snaps to the exact target (easedFrac=256) instead of
 * either overshooting or landing on the table's own slightly-short
 * table[255]=255. */
static void FS3ENextTootAnim_Hook(FS3ETimer *t, ULONG ticks)
{
    FS3ENextTootAnim *anim = (FS3ENextTootAnim *)t;
    ULONG totalTicks = FS3ETimer_Frequency();
    ULONG linFrac = totalTicks ? (ticks * 256) / totalTicks : 256;
    ULONG easedFrac;
    LONG  newScrollY;

    easedFrac = (linFrac >= 256) ? 256 : s_easeTable[linFrac];

    newScrollY = anim->scrollStart + (anim->scrollDelta * (LONG)easedFrac) / 256;

    if (app && app->tootTimeline)
        SetGdAttrs(app->tootTimeline, TTIMELINE_ScrollY, (ULONG)newScrollY, TAG_DONE);
}

/* Actual "move to next toot" work, shared by both the user-facing
 * Action_TimelineNextToot below and Play mode's own repeating timer
 * (FS3EPlayModeAnim_Hook) -- deliberately NOT the same function: a
 * user-triggered Next Toot must cancel Play mode (see
 * Action_TimelineNextToot), but Play mode's own hook calling this same
 * scroll every interval must NOT cancel itself. */
static BOOL FS3ETimelineNextToot_Internal(struct App *ctx)
{
    LONG scrollNow = 0, delta = 0;

    if (!ctx || !ctx->tootTimeline) return FALSE;

    /* Metrics noted up front, at the scroll position the animation
     * starts from -- see this function's own description in the task
     * that introduced it: ask TootTimeline where it is now and how far
     * it needs to go, then drive the actual scrolling entirely from out
     * here, one SetAttrs per timer tick. */
    GetAttr(TTIMELINE_ScrollY, ctx->tootTimeline, (ULONG *)&scrollNow);
    GetAttr(TTIMELINE_NextTootScrollDelta, ctx->tootTimeline, (ULONG *)&delta);
    if (delta == 0) return TRUE; /* already at the end of content -- nothing to reveal */

    if (!ctx->settings.smoothAutoScroll) {
        /* Smooth auto scroll off: skip the 25Hz eased-animation timer
         * entirely and land directly on the next toot in a single
         * SetAttrs -- same target FS3ENextTootAnim_Hook's last frame
         * would have reached, just without the vblanks in between. */
        FS3ETimer_Stop(&s_nextTootAnim.timer);
        SetGdAttrs(ctx->tootTimeline, TTIMELINE_ScrollY, (ULONG)(scrollNow + delta), TAG_DONE);
        return TRUE;
    }

    s_nextTootAnim.scrollStart = scrollNow;
    s_nextTootAnim.scrollDelta = delta;

    /* divTick=2, totalTicks=one second's worth of real vblanks -- see
     * FS3ENextTootAnim_Hook's own comment for why totalTicks is in raw
     * vblank units (independent of divTick) while divTick alone controls
     * how often the hook actually runs during that second. Restarts
     * cleanly (FS3ETimer_Start's own contract) if Space is pressed again
     * before a previous animation finished, picking a fresh target from
     * wherever the scroll position currently is. */
    FS3ETimer_Start(&s_nextTootAnim.timer, FS3ENextTootAnim_Hook, 2, FS3ETimer_Frequency());
    return TRUE;
}

/* User-facing "Next toot" -- Space (autoscroll idle) or the menu item,
 * see fs3emenu.c/friendsh3ep.c. A deliberate manual Next Toot always
 * cancels Play mode first (most relevant when clicked from the MENU,
 * which -- unlike Space -- isn't itself gated on whether Play mode is
 * currently running): otherwise a manual move here would fight Play
 * mode's own next scheduled advance. See FS3ETimelineNextToot_Internal
 * above for the actual scroll logic, also used by Play mode's own timer
 * hook WITHOUT this cancellation (it must not stop itself). */
BOOL Action_TimelineNextToot(struct App *ctx)
{
    Action_TimelineAutoscrollStop(ctx);
    return FS3ETimelineNextToot_Internal(ctx);
}

/* Play mode's own repeating timer -- see Action_TimelineAutoscrollPlay
 * below for how it's started (divTick = playTootTimeSec seconds' worth
 * of vblanks, totalTicks = 0 i.e. unbounded, so this fires once per
 * interval, forever, until FS3ETimer_Stop()'d). Calls the internal
 * next-toot helper directly, NOT the public Action_TimelineNextToot --
 * that would cancel Play mode via Action_TimelineAutoscrollStop on
 * every single advance, immediately undoing itself after one toot. */
static void FS3EPlayModeAnim_Hook(FS3ETimer *t, ULONG ticks)
{
    (void)t;
    (void)ticks;
    FS3ETimelineNextToot_Internal(app);
}

/* See this function's own doc comment in fs3eaction.h -- stops every
 * FS3ETimer-driven scroll animation/mode that exists: the Next-toot
 * smooth-scroll animation AND Play mode's own repeating timer, plus
 * resets timelineAutoscrollActive so Space's context-dependent dispatch
 * (friendsh3ep.c) correctly falls back to "Next toot" instead of
 * staying stuck offering "Stop autoscroll" for a mode that's no longer
 * actually running. */
void Action_TimelineStopScrollAnimation(void)
{
    FS3ETimer_Stop(&s_nextTootAnim.timer);
    FS3ETimer_Stop(&s_playModeTimer);
    if (app) app->timelineAutoscrollActive = FALSE;
}

/* "Move to Top" -- an instant jump, not animated (unlike Next Toot):
 * TTIMELINE_ScrollToNewest already does exactly this (scrollY =
 * contentTopY, see its own doc comment in fs3etoottimeline.h), generic
 * over whatever the active channel's content actually is (toots,
 * search/account-row list, ...), so there's nothing to compute out here
 * -- just forward to it. That tag's doc comment says it's meant to be
 * sent "right after a channel's first page lands", but that's about
 * when it was originally introduced for, not a real constraint -- the
 * underlying operation is just an immediate, always-safe scrollY
 * assignment. Cancels any in-flight Next-toot animation first, same
 * "a deliberate manual jump wins" reasoning as the arrow-key/wheel/
 * drag-scroll cases in friendsh3ep.c. */
BOOL Action_TimelineTop(struct App *ctx)
{
    if (!ctx || !ctx->tootTimeline) return FALSE;

    Action_TimelineStopScrollAnimation();
    SetGdAttrs(ctx->tootTimeline, TTIMELINE_ScrollToNewest, TRUE, TAG_DONE);
    return TRUE;
}

/* Starts Play mode: an FS3ETimer that fires FS3EPlayModeAnim_Hook once
 * every app->settings.playTootTimeSec seconds (divTick = that many
 * vblanks, totalTicks = 0 i.e. never auto-stops -- see that hook's own
 * comment), each firing doing one Next-toot move. Restarts cleanly
 * (FS3ETimer_Start's own contract) if pressed again while already
 * playing, picking the interval back up from a fresh countdown. */
BOOL Action_TimelineAutoscrollPlay(struct App *ctx)
{
    ULONG secs;

    if (!ctx) return FALSE;

    secs = (ULONG)ctx->settings.playTootTimeSec;
    if (secs < 3) secs = 3; /* matches fs3esettings.h's own clamp, defensive */

    ctx->timelineAutoscrollActive = TRUE;
    FS3ETimer_Start(&s_playModeTimer, FS3EPlayModeAnim_Hook,
                     secs * FS3ETimer_Frequency(), 0);
    return TRUE;
}

BOOL Action_TimelineAutoscrollStop(struct App *ctx)
{
    if (ctx) ctx->timelineAutoscrollActive = FALSE;
    FS3ETimer_Stop(&s_playModeTimer);
    return TRUE;
}

/* Asks TootTimeline itself to copy the "selected" toot's plain UTF-8
 * body to the Amiga clipboard -- see TTIMELINE_CopySelectedText's own
 * doc comment in TootTimeline/fs3etoottimeline.h for what "selected"
 * means (last mouse-down post, or the topmost visible one before any
 * click). Same "just SetAttrs and let the gadget do it" shape as
 * Action_TimelineTop above; TootTimeline owns Clipboard_WriteText() call
 * itself, there's nothing left for this function to do. */
BOOL Action_TimelineCopyText(struct App *ctx)
{
    if (!ctx || !ctx->tootTimeline) return FALSE;

    SetAttrs(ctx->tootTimeline, TTIMELINE_CopySelectedText, TRUE, TAG_DONE);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * User menu actions -- act on whichever profile is open in the Search
 * view's FS3ESEARCH_USER_PROFILE sub-mode (ctx->searchProfileAcct/
 * searchProfileAccountId). A no-op (FALSE) when no profile is open --
 * there's no menu enable/disable wired to the current view yet, so these
 * guard themselves instead of relying on the item being greyed out.
 * -------------------------------------------------------------------------*/

/* Builds the canonical web profile URL from the acct string alone (same
 * local-vs-remote split FS3EApp_OpenProfile already does in reverse to
 * build a "user@server" handle, fs3erequests.c) -- a remote acct
 * ("user@remote.instance") already names its own home instance, a local
 * one (no '@') lives on ours. Mastodon's Account object also carries this
 * as a "url" field server-side, but that's not parsed into
 * FS3EMastodonAccount today; deriving it from the acct string needs no
 * new network plumbing. */
BOOL Action_UserCopyProfileURL(struct App *ctx)
{
    char url[300];
    const char *acct;
    const char *at;

    if (!ctx || !ctx->searchProfileAcct || !ctx->searchProfileAcct[0]) return FALSE;
    acct = ctx->searchProfileAcct;
    at = strchr(acct, '@');

    if (at) {
        snprintf(url, sizeof(url), "https://%s/@%.*s",
                 at + 1, (int)(at - acct), acct);
    } else {
        const char *domain = ctx->accountApiBaseUrl ? ctx->accountApiBaseUrl : "";
        if (strncmp(domain, "https://", 8) == 0) domain += 8;
        else if (strncmp(domain, "http://", 7) == 0) domain += 7;
        snprintf(url, sizeof(url), "https://%s/@%s", domain, acct);
    }

    return Clipboard_WriteText(url);
}

/* Force-follow/unforce-follow, unlike the profile header's own Follow/
 * Unfollow button (a single toggle that reads the current state before
 * deciding which way to flip): these are two separate, explicit menu
 * items, so each just asserts its own direction outright via the same
 * Action_ToggleFollow the button uses -- POSTing .../follow or .../unfollow
 * again when already in that state is a harmless no-op server-side. */
BOOL Action_UserFollow(struct App *ctx)
{
    if (!ctx || !ctx->searchProfileAccountId || !ctx->searchProfileAccountId[0]) return FALSE;
    return Action_ToggleFollow(ctx, ctx->searchProfileAccountId, FALSE);
}

BOOL Action_UserUnfollow(struct App *ctx)
{
    if (!ctx || !ctx->searchProfileAccountId || !ctx->searchProfileAccountId[0]) return FALSE;
    return Action_ToggleFollow(ctx, ctx->searchProfileAccountId, TRUE);
}

/* Mastodon's mute (visibility mask)/block/domain-block endpoints have no
 * request/reply plumbing in network_fs3e/ yet (unlike follow, which
 * FS3ENETQ_FOLLOW already covers) -- stubs until that's added. */
BOOL Action_UserMask(struct App *ctx)
{
    (void)ctx;
    return TRUE;
}

BOOL Action_UserUnmask(struct App *ctx)
{
    (void)ctx;
    return TRUE;
}

BOOL Action_UserBlock(struct App *ctx)
{
    (void)ctx;
    return TRUE;
}

BOOL Action_UserUnblock(struct App *ctx)
{
    (void)ctx;
    return TRUE;
}

BOOL Action_UserBlockServer(struct App *ctx)
{
    (void)ctx;
    return TRUE;
}

BOOL Action_UserUnblockServer(struct App *ctx)
{
    (void)ctx;
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
    if (!ctx) return FALSE;
    FS3ESettingsView_Open(&ctx->settingsView);
    return TRUE;
}

BOOL Action_NetworkView(struct App *ctx)
{
    if (!ctx) return FALSE;
    FS3ENetworkView_Open(&ctx->networkView);
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

/* -------------------------------------------------------------------------
 * Toot actions
 * -------------------------------------------------------------------------*/

BOOL Action_ToggleFavorite(struct App *ctx, const char *postId, BOOL currentlyFavourited)
{
    FS3ENetFavouriteReq *req;

    if (!ctx || !postId || !postId[0]) return FALSE;
    if (!ctx->accountAccessToken || !ctx->accountAccessToken[0]) return FALSE;

    req = FS3ENetFavouriteReq_Alloc(ctx->accountApiBaseUrl, ctx->accountAccessToken,
                                    postId, !currentlyFavourited);
    if (!req) return FALSE;

    if (!FS3EApp_NetSend(FS3ENETQ_FAVORITE, req, sizeof(*req))) {
        FreeVec(req);
        return FALSE;
    }
    return TRUE;
}

BOOL Action_ToggleReblog(struct App *ctx, const char *postId, BOOL currentlyReblogged)
{
    FS3ENetReblogReq *req;

    if (!ctx || !postId || !postId[0]) return FALSE;
    if (!ctx->accountAccessToken || !ctx->accountAccessToken[0]) return FALSE;

    req = FS3ENetReblogReq_Alloc(ctx->accountApiBaseUrl, ctx->accountAccessToken,
                                 postId, !currentlyReblogged);
    if (!req) return FALSE;

    if (!FS3EApp_NetSend(FS3ENETQ_REBLOG, req, sizeof(*req))) {
        FreeVec(req);
        return FALSE;
    }
    return TRUE;
}

BOOL Action_ToggleFollow(struct App *ctx, const char *accountId, BOOL currentlyFollowing)
{
    FS3ENetFollowReq *req;

    if (!ctx || !accountId || !accountId[0]) return FALSE;
    if (!ctx->accountAccessToken || !ctx->accountAccessToken[0]) return FALSE;

    req = FS3ENetFollowReq_Alloc(ctx->accountApiBaseUrl, ctx->accountAccessToken,
                                 accountId, !currentlyFollowing);
    if (!req) return FALSE;

    if (!FS3EApp_NetSend(FS3ENETQ_FOLLOW, req, sizeof(*req))) {
        FreeVec(req);
        return FALSE;
    }
    return TRUE;
}
