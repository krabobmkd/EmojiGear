#ifndef FS3ETOOTVIEW_H
#define FS3ETOOTVIEW_H

/*
 * fs3etootview.h - "New toot" sub-window for FriendSh3ep.
 *
 * A classic BOOPSI window.class window mimicking brutaldon's web post
 * composer (brutaldon/templates/main/post_partial.html):
 *   - contextMessage: read-only, no-bevel UniButton (same pattern as
 *     fs3eloginview.c's urlInstructLabel) telling the user what kind of
 *     toot this window is composing (new/modify/poll/reply) -- see
 *     FS3ETootView_SetComposeContext. Not sent to the server; there
 *     is no "subject" concept in Mastodon toots (see FS3ENetPostStatusReq,
 *     which only ever gets an empty spoiler/CW string from this window).
 *   - main body UniTextEditor (multi-line)
 *   - two "Attach Media" rows (attachMediaGF/attachMedia2GF), between
 *     bodyEditor and bottomBar -- most servers accept more than one
 *     attachment per toot.
 *   - bottom bar: visibility chooser, quote-policy chooser, char-count
 *     label, emoji UniButton (emojibox access), "Sensitive content"
 *     checkbox, "Toot" button bottom-right.
 *   - visibilityMeaning: read-only, no-bevel UniButton below the bottom
 *     bar, one line explaining what the currently selected visibility
 *     choice means -- see FS3ETootView_UpdateVisibilityMeaning.
 *
 * Pattern adapted from EmojiGear/egsearchbox.c, see fs3eloginview.h for
 * the analogous login sub-window.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <libraries/utf8rastport.h>

/* Public/Unlisted/Private/Direct, matching brutaldon's PRIVACY_CHOICES. */
#define FS3ETOOT_NUM_VISIBILITIES 4

/* Everybody/Followers only/Me only -- Mastodon's quote_approval_policy
 * ("public"/"followers"/"nobody"), added server-side in Mastodon 4.5. */
#define FS3ETOOT_NUM_QUOTEPOLICIES 3

/* What FS3ETootView_SetComposeContext configures the window to submit when
 * "Toot" is pressed. The contextMessage title text is derived from this +
 * the accompanying FS3ETootComposeParams internally -- callers pick a kind
 * and hand over the relevant ids/text, they no longer set title text
 * directly (see the removed FS3ETootView_SetContextMessage). */
typedef enum FS3ETootKind {
    FS3ETOOT_KIND_NEW = 0, /* blank body, "Creating a new toot" */
    FS3ETOOT_KIND_MODIFY,  /* editing params->postId, bodyEditor prefilled from params->body */
    FS3ETOOT_KIND_POLL,    /* new toot with a poll attached (poll UI itself: future work) */
    FS3ETOOT_KIND_REPLY,   /* replying to params->postId, title shows params->acct */
    FS3ETOOT_KIND_QUOTE,   /* quoting params->postId (own new toot + quoted_status_id),
                             * title shows params->acct -- see TTL_HOT_BOOST's
                             * Boost/Quote choice in friendsh3ep.c */
    FS3ETOOT_KIND_MESSAGE, /* a fresh, non-reply toot addressed at params->acct (no
                             * postId -- unlike REPLY, nothing is being replied to):
                             * bodyEditor prefilled with "@acct ", title shows
                             * params->acct -- see a profile header's "Message"
                             * button (TTL_HOT_MESSAGE) in friendsh3ep.c */
    FS3ETOOT_KIND_MODIFY_BIO /* editing the connected user's own profile
                               * description (Mastodon account "note", not a
                               * status), bodyEditor prefilled from
                               * params->body -- see a profile header's
                               * "Modify" button (TTL_HOT_MODIFY_BIO) in
                               * friendsh3ep.c. params->postId/mediaIds are
                               * unused (there is no status involved). Submit
                               * goes through FS3EApp_SubmitBioUpdate instead
                               * of FS3EApp_SubmitToot -- see
                               * GID_TOOT_SEND_BUTTON in friendsh3ep.c. */
} FS3ETootKind;

/* Max media attachments carried through a compose context (mirrors
 * TootTimeline's TTL_POST_MAX_MEDIA / network_fs3e's FS3ENET_MAX_MEDIA;
 * kept as its own define since this header doesn't depend on either
 * module). */
#define FS3ETOOT_MAX_MEDIA 4

/* Extra data for FS3ETootView_SetComposeContext; which fields matter
 * depends on kind (see FS3ETootKind). Pass params NULL for
 * FS3ETOOT_KIND_NEW/FS3ETOOT_KIND_POLL. */
typedef struct FS3ETootComposeParams {
    const char *postId; /* MODIFY: status being edited. REPLY: status being replied
                          * to. QUOTE: status being quoted. */
    const char *acct;   /* REPLY/QUOTE: "@handle" shown in the title ("Replying to
                          * @handle" / "Quoting @handle's toot"). */
    const char *body;   /* MODIFY: current raw body text, used to prefill bodyEditor.
                          * Unused for QUOTE -- see FS3ETootKind. */

    /* MODIFY: the status's existing media_attachments[].id, NULL past
     * mediaCount -- not shown to the user yet (attached-media display in
     * the compose window is future work), only kept so a later edit-submit
     * can resend them and Mastodon's PUT .../statuses/:id doesn't strip
     * the attachments (it treats media_ids as a full replace-list). */
    const char *mediaIds[FS3ETOOT_MAX_MEDIA];
    ULONG       mediaCount;
} FS3ETootComposeParams;

typedef struct FS3ETootView {
    Object *windowObj;     /* BOOPSI window object (persistent) */
    struct Window *window; /* Intuition window, valid while open */

    LONG left, top, width, height; /* remembered window geometry */

    Object *layout;

    Object *contextMessage; /* read-only: "Creating a new toot" / "Replying to ..." */
    Object *bodyEditor;     /* main toot text, multi-line   */

    /* read-only, one-line UniButton at the bottom of the outer vertical
     * layout, below bottomBar -- shows what the currently selected
     * visibility choice means (see FS3ETootView_UpdateVisibilityMeaning).
     * Kept separate from contextMessage so contextMessage stays a plain
     * "new/reply/modify/poll" label. */
    Object *visibilityMeaning;

    /* What "Toot" is currently set up to submit, and which status (if any)
     * that involves -- see FS3ETootView_SetComposeContext. composePostId
     * and composeMediaIds[] are AllocVec'd copies owned by tv, replaced
     * (old copies freed) on every SetComposeContext call and on Dispose. */
    FS3ETootKind composeKind;
    char        *composePostId;
    char        *composeMediaIds[FS3ETOOT_MAX_MEDIA];
    ULONG        composeMediaCount;

    struct List   visibilityList;
    struct Node  *visibilityNodes[FS3ETOOT_NUM_VISIBILITIES];
    Object       *visibilityChooser;

    /* "Who can quote you" -- always editable (unlike visibilityChooser,
     * Mastodon's edit endpoint does accept quote_approval_policy), but not
     * yet wired into MODIFY: there's no way to prefill it from the status
     * being edited, so sending it on edit would silently reset whatever
     * policy was set when the toot was first posted. Only read by
     * GID_TOOT_SEND_BUTTON's NEW/POLL/REPLY (POST) path -- see
     * friendsh3ep.c. */
    struct List   quotePolicyList;
    struct Node  *quotePolicyNodes[FS3ETOOT_NUM_QUOTEPOLICIES];
    Object       *quotePolicyChooser;

    Object *charCountLabel;
    char    charCountText[32];

    /* "Attach Media" row, between bodyEditor and the bottom bar -- a single
     * GETFILE gadget (see fs3ethemeview.c's font pickers for the same
     * pattern) restricted to FS3ETOOT_ATTACH_MEDIA_PATTERN, plus an "X"
     * button that clears it. Read directly via GETFILE_File at send time
     * (see GID_TOOT_SEND_BUTTON in friendsh3ep.c); no separate copy kept
     * here, same as fs3ethemeview.c's font pickers don't keep one either --
     * app->settings.*FontPath is their copy, and there's no settings field
     * for a one-shot compose-window attachment. */
    Object *attachMediaGF;
    Object *attachMediaClearBtn;

    /* Second attach-media row, identical [getfile][X] pattern, right below
     * the first -- most servers accept more than one attachment per toot.
     * Same "read directly at send time, no separate copy kept here" note
     * as attachMediaGF above applies here too. */
    Object *attachMedia2GF;
    Object *attachMedia2ClearBtn;

    /* "Sensitive content" checkbox, bottomBar, right before tootBtn --
     * Mastodon's `sensitive` flag applies to the whole status (and every
     * attachment in it), not per-attachment, hence one checkbox here
     * rather than one per attach-media row. Read directly via GA_Selected
     * at send time, same as attachMediaGF -- no separate copy kept. */
    Object *sensitiveCheck;

    Object *emojiBtn;

    Object *tootBtn;

    /* Window-local GadTools menu ("Toot": Clear/Cut/Copy UTF8/Paste UTF8/
     * Emoji Box). A classic-window menu strip is attached to the transient
     * struct Window, not the persistent BOOPSI Object *, so it has to be
     * (re)created and attached in FS3ETootView_Open and torn down in
     * FS3ETootView_Close every time -- see fs3etootview.c. */
    struct Menu *menu;
    APTR         menuVisualInfo;

    int reactivateEditor;
} FS3ETootView;

/* Build the BOOPSI window+layout. pointSize is forwarded to the
 * UniTextEditor fields. Returns TRUE on success. */
BOOL FS3ETootView_Create(FS3ETootView *tv, struct URPDrawContext *textDC);

/* Dispose the window object and everything below it. */
void FS3ETootView_Dispose(FS3ETootView *tv);

/* Open (or bring to front) the New toot window on CurrentMainScreen. */
void FS3ETootView_Open(FS3ETootView *tv);

/* Close (hide) the New toot window. No-op if already closed. */
void FS3ETootView_Close(FS3ETootView *tv);

/* Handle input messages from this window. Call when its signal fires. */
BOOL FS3ETootView_HandleInput(FS3ETootView *tv);

/* Signal bit mask to OR into Wait(). Returns 0 when the window is closed. */
ULONG FS3ETootView_GetSignalMask(FS3ETootView *tv);

/* Refreshes left/top/width/height from the live window if currently open
 * (no-op if closed -- those fields already hold the last-known position,
 * set by FS3ETootView_Close). Call before persisting geometry (see
 * fs3esettings.c's TT_TOOTWINDOW), same as FS3EMain_GetWindowPos does for
 * the main window. */
void FS3ETootView_GetWindowPos(FS3ETootView *tv);

/* Recompute the "N chars" label from the current body text length. */
void FS3ETootView_UpdateCharCount(FS3ETootView *tv);

/* Enables/disables tootBtn based on whether an account is currently
 * connected (app->accountAccessToken) -- there's nothing to post to
 * otherwise. Safe to call whether the window is open or closed, and
 * before app itself is fully populated (checked internally). Call
 * whenever the active account changes (see FS3EApp_SetAccount in
 * friendsh3ep.c) and at window open/create, so the button never sits
 * enabled with no account behind it. */
void FS3ETootView_UpdateSendEnabled(FS3ETootView *tv);

/* Configures which action the window is set up to submit (kind) plus any
 * ids/text that action needs (params, NULL for kinds that don't need any --
 * see FS3ETootKind), and derives contextMessage's title text from that
 * internally (UniButton renders multi-line same as fs3eloginview.c's
 * urlInstructLabel, so title text can still span lines if ever needed).
 * FS3ETOOT_KIND_MODIFY also replaces bodyEditor's whole text with
 * params->body (UTED_Text) so editing starts from what's already posted.
 * Safe to call whether the window is open or closed; call before
 * FS3ETootView_Open. */
void FS3ETootView_SetComposeContext(FS3ETootView *tv, FS3ETootKind kind,
                                     const FS3ETootComposeParams *params);

/* WATCH OUT ! if return not NULL , must be FreeVec()'ed */
const char *FS3ETootView_GetUTF8Body(FS3ETootView *tv);

/* CLear the text, used internally as for now */
void FS3ETootView_ClearText(FS3ETootView *tv);

/* 0=public, 1=unlisted, 2=private, 3=direct */
LONG FS3ETootView_GetVisibility(FS3ETootView *tv);

/* 0=public (Everybody), 1=followers (Followers only), 2=nobody (Me only) */
LONG FS3ETootView_GetQuotePolicy(FS3ETootView *tv);

/* TRUE if the "Sensitive content" checkbox is ticked -- maps straight to
 * Mastodon's status-level `sensitive` field, see sensitiveCheck's comment
 * above. */
BOOL FS3ETootView_GetSensitive(FS3ETootView *tv);

/* FS3ETootView_CheckAttachment()'s result -- see its doc comment. */
typedef enum FS3ETootAttachStatus {
    FS3ETOOT_ATTACH_NONE = 0, /* attachMediaGF is empty -- not an error, just nothing to upload */
    FS3ETOOT_ATTACH_OK,       /* outPath/outMimeType filled, ready to upload */
    FS3ETOOT_ATTACH_BADEXT,   /* a file is selected but its extension isn't accepted
                                * (see FS3ETOOT_ATTACH_MEDIA_PATTERN in fs3etootview.c) */
    FS3ETOOT_ATTACH_MISSING,  /* a file is selected but doesn't exist/isn't readable
                                * (moved or deleted since it was picked) */
    FS3ETOOT_ATTACH_TOOBIG    /* a file is selected but exceeds FS3ENET_UPLOAD_MAX_BYTES
                                * (fs3enet.h), or Examine() found it isn't a plain file */
} FS3ETootAttachStatus;

/* Reads attachMediaGF's current selection (GETFILE_File + GETFILE_Drawer),
 * builds the full path into outPath (outPathSize), and validates it:
 * extension against the accepted list, existence/type/size via a
 * synchronous Lock()+Examine() -- safe here since this is only ever called
 * from the main event loop's WMHI_GADGETUP dispatch (a normal task
 * context), never a BOOPSI device-context callback. On FS3ETOOT_ATTACH_OK,
 * *outMimeType is set to a static string (e.g. "image/jpeg") suitable for
 * FS3ENetUploadMediaReq_Alloc(); left untouched for every other result.
 * Called by GID_TOOT_SEND_BUTTON (friendsh3ep.c) before deciding whether to
 * fire FS3ENETQ_UPLOAD_MEDIA first or submit the toot directly. */
FS3ETootAttachStatus FS3ETootView_CheckAttachment(FS3ETootView *tv,
    char *outPath, ULONG outPathSize, const char **outMimeType);

/* Same as FS3ETootView_CheckAttachment, but for the second attach-media row
 * (attachMedia2GF) -- see that field's comment in this header. */
FS3ETootAttachStatus FS3ETootView_CheckAttachment2(FS3ETootView *tv,
    char *outPath, ULONG outPathSize, const char **outMimeType);

#endif /* FS3ETOOTVIEW_H */
