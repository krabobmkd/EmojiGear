/*
 * Egaction.h – table-driven action dispatch for Eg UTF-8 text editor.
 *
 * Actions are triggered from menu picks or keyboard shortcuts.
 * struct App carries all state needed by action functions.
 */

#ifndef EgACTION_H
#define EgACTION_H

#include <exec/types.h>

/* Forward declarations */
typedef struct EgAction EgAction;
struct App;

/* Action function signature */
typedef BOOL (*EgActionFunc)(struct App *ctx);


/* Action definition */
struct EgAction {
    EgActionFunc  func;
    ULONG           nameStringID;   /* MSG_* id used to localize the name */
    const char     *name;           /* cached localized name (set by EgAction_Init) */
};

/* Action IDs */
enum {
    ACTION_FILE_NEW = 0,
    ACTION_FILE_LOAD_UTF8,
    ACTION_FILE_LOAD_LATIN1,
    ACTION_FILE_LOAD_LATIN2,
    ACTION_FILE_CLOSE,

    ACTION_FILE_SAVE_UTF8,
    ACTION_FILE_SAVE_ESCAPED,
    ACTION_FILE_SAVE_LATIN1,
    ACTION_FILE_SAVE_LATIN2,
    ACTION_FILE_ABOUT,
    ACTION_FILE_QUIT,

    ACTION_EDIT_CUT,
    ACTION_EDIT_COPY,
    ACTION_EDIT_COPY_LATIN1,
    ACTION_EDIT_COPY_LATIN2,
    ACTION_EDIT_PASTE,
    ACTION_EDIT_PASTE_LATIN1,
    ACTION_EDIT_PASTE_LATIN2,
    ACTION_EDIT_UNDO,
    ACTION_EDIT_REDO,

    ACTION_EDIT_OPEN_EMOJIBOX,

    ACTION_FIND_WINDOW,
    ACTION_FIND_NEXT,
    ACTION_FIND_PREVIOUS,
    ACTION_GOTO_LINE,

    ACTION_PREVTEXT,
    ACTION_NEXTTEXT,


    ACTION_SETTINGS_WINDOW,
    ACTION_FONTS_WINDOW,
    ACTION_SETTINGS_WORDWRAP,
    ACTION_SETTINGS_FORCEMONOSPACE,
    ACTION_SETTINGS_APPLYANSI,
    ACTION_SETTINGS_DISPLAYUNICODEINFO,
    ACTION_SETTINGS_FONTSIZEP,
    ACTION_SETTINGS_FONTSIZEM,

    ACTION_OPEN_RECENT_0,
    ACTION_OPEN_RECENT_1,
    ACTION_OPEN_RECENT_2,
    ACTION_OPEN_RECENT_3,
    ACTION_OPEN_RECENT_4,
    ACTION_OPEN_RECENT_5,
    ACTION_OPEN_RECENT_6,
    ACTION_OPEN_RECENT_7,
    /* Must be last */
    ACTION_COUNT
};

/* Load from a known path without an ASL requester.
 * load_utf8_from_path sets *actual_encoding to the encoding really used
 * (0=UTF-8/UTF-16, 1=Latin-1 fallback); pass NULL if you don't need it. */
BOOL load_utf8_from_path(struct App *ctx, const char *path, int *actual_encoding);
BOOL load_encoded_from_path(struct App *ctx, const char *path, int encoding);

/* Returns TRUE if buf[0..len-1] is valid UTF-8 (see egaction.c for details). */
BOOL is_valid_utf8(const char *buf, LONG len);

/* Convert a single-byte legacy encoding buffer to a UTF-8 AllocVec string.
 * encoding == 1 -> ISO 8859-1 (Latin-1), encoding == 2 -> ISO 8859-2 (Latin-2).
 * Caller must FreeVec() the result. Returns NULL on allocation failure. */
char *convert_to_utf8(const char *src, LONG srcLen, int encoding);

/* Initialize: caches localized action names from MSG_* strings */
void        EgAction_Init(void);

/* Get action by ID (NULL if out of range) */
EgAction *EgAction_Get(ULONG actionID);

/* Execute an action (returns FALSE if actionID invalid or func is NULL) */
BOOL        EgAction_Execute(ULONG actionID, struct App *ctx);

/* Individual action function declarations */
BOOL Action_FileNew(struct App *ctx);
BOOL Action_FileLoadUTF8(struct App *ctx);
BOOL Action_FileLoadLatin1(struct App *ctx);
BOOL Action_FileLoadLatin2(struct App *ctx);
BOOL Action_FileClose(struct App *ctx);

BOOL Action_FileSaveUTF8(struct App *ctx);
BOOL Action_FileSaveEscaped(struct App *ctx);
BOOL Action_FileSaveLatin1(struct App *ctx);
BOOL Action_FileSaveLatin2(struct App *ctx);
BOOL Action_FileAbout(struct App *ctx);
BOOL Action_FileQuit(struct App *ctx);

BOOL Action_EditCut(struct App *ctx);
BOOL Action_EditCopy(struct App *ctx);
BOOL Action_EditCopyLatin1(struct App *ctx);
BOOL Action_EditCopyLatin2(struct App *ctx);
BOOL Action_EditPaste(struct App *ctx);
BOOL Action_EditPasteLatin1(struct App *ctx);
BOOL Action_EditPasteLatin2(struct App *ctx);
BOOL Action_EditUndo(struct App *ctx);
BOOL Action_EditRedo(struct App *ctx);
BOOL Action_EditOpenEmojiBox(struct App *ctx);

/* this one just open the search window, real Find operation is Action_NavFindNext() */
BOOL Action_NavFindOpen(struct App *ctx);
BOOL Action_NavFindNext(struct App *ctx);
BOOL Action_NavFindPrev(struct App *ctx);
BOOL Action_NavReplace(struct App *ctx);
BOOL Action_NavReplaceAll(struct App *ctx);


BOOL Action_NavGotoLine(struct App *ctx);
BOOL Action_PrevText(struct App *ctx);
BOOL Action_NextText(struct App *ctx);

BOOL Action_SettingsWindow(struct App *ctx);
BOOL Action_FontsWindow(struct App *ctx);

BOOL Action_SettingWordWrap(struct App *ctx);
BOOL Action_SettingForceMonospace(struct App *ctx);
BOOL Action_SettingApplyAnsi(struct App *ctx);
BOOL Action_SettingDisplayUnicodeInfo(struct App *ctx);
BOOL Action_SettingAnsiUnixColors(struct App *ctx);

BOOL Action_SettingsFontSizePlus(struct App *ctx);
BOOL Action_SettingsFontSizeMinus(struct App *ctx);


BOOL Action_OpenRecent0(struct App *ctx);
BOOL Action_OpenRecent1(struct App *ctx);
BOOL Action_OpenRecent2(struct App *ctx);
BOOL Action_OpenRecent3(struct App *ctx);
BOOL Action_OpenRecent4(struct App *ctx);
BOOL Action_OpenRecent5(struct App *ctx);
BOOL Action_OpenRecent6(struct App *ctx);
BOOL Action_OpenRecent7(struct App *ctx);


#endif /* EgACTION_H */
