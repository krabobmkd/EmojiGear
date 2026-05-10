/*
 * Egaction.c – action implementations for Eg UTF-8 text editor.
 *
 * File I/O actions (Load/Save) use an ASL file requester.
 * AslBase must be opened in main.c before EgAction_Execute is called.
 */

#include "egaction.h"
#include "eglocale.h"
#include "emojigear.h"
#include "egmenu.h"

#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <libraries/asl.h>
#include <dos/dos.h>
#include <devices/clipboard.h>
#include <clib/alib_protos.h>
#include <stdlib.h>
#include "egsettingsview.h"
#include "egfontsview.h"
#include <gadgets/unitexteditor.h>
#include <proto/requester.h>
#include <classes/requester.h>
#include "egtabs.h"


extern struct Library *AslBase;

/* -------------------------------------------------------------------------
 * Action table
 * -------------------------------------------------------------------------*/
static EgAction s_actions[ACTION_COUNT] = {
    /* ACTION_FILE_NEW       */ { Action_FileNew,        MSG_FILE_NEW,          NULL },
    /* ACTION_FILE_LOAD_UTF8 */ { Action_FileLoadUTF8,   MSG_FILE_LOAD_UTF8,    NULL },
    /* ACTION_FILE_LOAD_LATIN1*/{ Action_FileLoadLatin1, MSG_FILE_LOAD_LATIN1,  NULL },
    /* ACTION_FILE_LOAD_LATIN2*/{ Action_FileLoadLatin2, MSG_FILE_LOAD_LATIN2,  NULL },
    /* ACTION_FILE_LOAD_LATIN2*/{ Action_FileClose, MSG_FILE_CLOSE,  NULL },
    /* ACTION_FILE_SAVE_UTF8    */ { Action_FileSaveUTF8,    MSG_FILE_SAVE_UTF8,    NULL },
    /* ACTION_FILE_SAVE_ESCAPED */ { Action_FileSaveEscaped, MSG_FILE_SAVE_ESCAPED, NULL },
    /* ACTION_FILE_SAVE_LATIN1  */ { Action_FileSaveLatin1,  MSG_FILE_SAVE_LATIN1,  NULL },
    /* ACTION_FILE_SAVE_LATIN2  */ { Action_FileSaveLatin2,  MSG_FILE_SAVE_LATIN2,  NULL },
    /* ACTION_FILE_ABOUT        */ { Action_FileAbout,       MSG_FILE_ABOUT,        NULL },
    /* ACTION_FILE_QUIT         */ { Action_FileQuit,        MSG_MENU_QUIT,         NULL },
    /* ACTION_EDIT_CUT          */ { Action_EditCut,         MSG_EDIT_CUT,          NULL },
    /* ACTION_EDIT_COPY         */ { Action_EditCopy,        MSG_EDIT_COPY,         NULL },
    /* ACTION_EDIT_COPY_LATIN1  */ { Action_EditCopyLatin1,  MSG_EDIT_COPY_LATIN1,  NULL },
    /* ACTION_EDIT_COPY_LATIN2  */ { Action_EditCopyLatin2,  MSG_EDIT_COPY_LATIN2,  NULL },
    /* ACTION_EDIT_PASTE        */ { Action_EditPaste,       MSG_EDIT_PASTE,        NULL },
    /* ACTION_EDIT_PASTE_LATIN1 */ { Action_EditPasteLatin1, MSG_EDIT_PASTE_LATIN1, NULL },
    /* ACTION_EDIT_PASTE_LATIN2 */ { Action_EditPasteLatin2, MSG_EDIT_PASTE_LATIN2, NULL },
    /* ACTION_EDIT_UNDO         */ { Action_EditUndo,        MSG_EDIT_UNDO,         NULL },
    /* ACTION_EDIT_REDO         */ { Action_EditRedo,        MSG_EDIT_REDO,         NULL },

    /* ACTION_EDIT_OPEN_EMOJIBOX         */ { Action_EditOpenEmojiBox,        MSG_EDIT_EMOJIBOX,         NULL },


    /* ACTION_FIND_WINDOW   */ { Action_NavFindOpen,  MSG_NAV_FIND,         NULL },
    /* ACTION_FIND_NEXT   */ { Action_NavFindNext,  MSG_NAV_FINDNEXT,         NULL },
    /* ACTION_FIND_PREVIOUS   */ { Action_NavFindPrev,  MSG_NAV_FINDPREV,         NULL },
    /* ACTION_GOTO_LINE   */ { Action_NavGotoLine,  MSG_NAV_GOTOL,         NULL },

    /* ACTION_PREVTEXT   */ { Action_PrevText,  MSG_NAV_PREVTEXT,         NULL },
    /* ACTION_NEXTTEXT   */ { Action_NextText,  MSG_NAV_NEXTTEXT,         NULL },


    /* ACTION_SETTINGS_WINDOW        */ { Action_SettingsWindow,        MSG_SETTINGS_DOTS,          NULL },
    /* ACTION_FONTS_WINDOW           */ { Action_FontsWindow,           MSG_FONTSETTINGS_DOTS,      NULL },
    /* ACTION_SETTINGS_WORDWRAP      */ { Action_SettingWordWrap,        MSG_SETTINGS_WORDWRAP,      NULL },
    /* ACTION_SETTINGS_FORCEMONOSPACE*/ { Action_SettingForceMonospace,  MSG_SETTINGS_FORCEMONOSPACE,NULL },
    /* ACTION_SETTINGS_APPLYANSI     */ { Action_SettingApplyAnsi,       MSG_SETTINGS_APPLYANSI,          NULL },
    /* ACTION_SETTINGS_ANSI_UNIX_COLORS */ { Action_SettingAnsiUnixColors, MSG_SETTINGS_ANSI_UNIX_COLORS, NULL },
    /* ACTION_SETTINGS_FONTSIZEP     */ { Action_SettingsFontSizePlus,   MSG_SETTING_FONTSIZEP,      NULL },
    /* ACTION_SETTINGS_FONTSIZEM     */ { Action_SettingsFontSizeMinus,  MSG_SETTING_FONTSIZEM,      NULL },


    /* ACTION_OPEN_RECENT_0 */ { Action_OpenRecent0, MSG_OPEN_RECENT, NULL },
    /* ACTION_OPEN_RECENT_1 */ { Action_OpenRecent1, MSG_OPEN_RECENT, NULL },
    /* ACTION_OPEN_RECENT_2 */ { Action_OpenRecent2, MSG_OPEN_RECENT, NULL },
    /* ACTION_OPEN_RECENT_3 */ { Action_OpenRecent3, MSG_OPEN_RECENT, NULL },

    /* ACTION_OPEN_RECENT_0 */ { Action_OpenRecent4, MSG_OPEN_RECENT, NULL },
    /* ACTION_OPEN_RECENT_1 */ { Action_OpenRecent5, MSG_OPEN_RECENT, NULL },
    /* ACTION_OPEN_RECENT_2 */ { Action_OpenRecent6, MSG_OPEN_RECENT, NULL },
    /* ACTION_OPEN_RECENT_3 */ { Action_OpenRecent7, MSG_OPEN_RECENT, NULL },
};
void EgAction_Init(void)
{
    ULONG i;
    for (i = 0; i < ACTION_COUNT; i++)
        s_actions[i].name = LOC(s_actions[i].nameStringID);
}

EgAction *EgAction_Get(ULONG actionID)
{
    if (actionID >= ACTION_COUNT) return NULL;
    return &s_actions[actionID];
}

BOOL EgAction_Execute(ULONG actionID, struct App *ctx)
{
    EgAction *a = EgAction_Get(actionID);
    if (!a || !a->func) return FALSE;
    return a->func(ctx);
}

/* -------------------------------------------------------------------------
 * Action: New – clear the editor buffer
 * -------------------------------------------------------------------------*/
BOOL Action_FileNew(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;
    EgTabs_NewTab();
    BMainWindow_SetTitleLoc(&app->mainwindow, MSG_WINDOW_TITLE);
    UpdateStatusBar();
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Helper: ask ASL for a file path.
 * Returns a newly AllocVec'd string on success, NULL on cancel/error.
 * Caller must FreeVec the result.
 * -------------------------------------------------------------------------*/
static char *asl_request_file(struct Window *win, const char *title, BOOL saveMode)
{
    struct FileRequester *req;
    char *result = NULL;

    if (!AslBase) return NULL;

    {
        const char *initDir = (app && app->appSettings.lastDir) ?
                               app->appSettings.lastDir : NULL;
        req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
                  ASLFR_Window,       (ULONG)win,
                  ASLFR_TitleText,    (ULONG)title,
                  ASLFR_DoSaveMode,   (ULONG)saveMode,
                  ASLFR_RejectIcons,  TRUE,
                  initDir ? ASLFR_InitialDrawer : TAG_IGNORE,
                  initDir ? (ULONG)initDir       : 0UL,
                  TAG_DONE);
    }
    if (!req) return NULL;

    if (AslRequest(req, NULL)) {
        ULONG dirLen  = strlen(req->fr_Drawer);
        ULONG fileLen = strlen(req->fr_File);
        ULONG totalLen = dirLen + 1 + fileLen + 1; /* slash + NUL */

        if (app)
            AppSettings_SetLastDir(&app->appSettings, req->fr_Drawer);

        result = (char *)AllocVec(totalLen, MEMF_ANY);
        if (result) {
            strcpy(result, req->fr_Drawer);
            if (dirLen > 0 && result[dirLen - 1] != '/' && result[dirLen - 1] != ':')
                strcat(result, "/");
            strcat(result, req->fr_File);
        }
    }

    FreeAslRequest(req);
    return result;
}

/* -------------------------------------------------------------------------
 * Normalize line endings in-place: \r\n → \n, lone \r → \n.
 * Updates *len to the new (possibly shorter) byte count.
 * The buffer must be writable and have room for a NUL at position *len.
 * -------------------------------------------------------------------------*/
static void normalize_line_endings(char *buf, LONG *len)
{
    LONG r, w;
    for (r = 0, w = 0; r < *len; r++) {
        if (buf[r] == '\r') {
            buf[w++] = '\n';
            if (r + 1 < *len && buf[r + 1] == '\n')
                r++; /* absorb the LF of a CRLF pair */
        } else {
            buf[w++] = buf[r];
        }
    }
    buf[w] = '\0';
    *len = w;
}

/* -------------------------------------------------------------------------
 * Convert a UTF-16 buffer (BE or LE, BOM already stripped) to a freshly
 * AllocVec'd NUL-terminated UTF-8 string.
 *   be=1  → big-endian (byte order: high, low)
 *   be=0  → little-endian (byte order: low, high)
 * Surrogate pairs are handled for code points above U+FFFF.
 * Invalid surrogates are replaced with U+FFFD.
 * Returns NULL on allocation failure. *outLen is set to the UTF-8 byte count.
 * Caller must FreeVec() the result.
 * -------------------------------------------------------------------------*/
static char *utf16_to_utf8(const char *src, LONG srcBytes, int be, LONG *outLen)
{
    /* Worst case: every UTF-16 unit encodes to 3 UTF-8 bytes. */
    LONG   maxOut = 3 * (srcBytes / 2) + 1;
    char  *out    = (char *)AllocVec((ULONG)maxOut, MEMF_ANY);
    char  *p;
    LONG   i;

    if (!out) return NULL;
    p = out;

    for (i = 0; i + 1 < srcBytes; i += 2) {
        unsigned int hi   = (unsigned char)src[i];
        unsigned int lo   = (unsigned char)src[i + 1];
        unsigned int unit = be ? ((hi << 8) | lo) : ((lo << 8) | hi);
        unsigned int cp;

        if (unit >= 0xD800 && unit <= 0xDBFF) {
            /* High surrogate: must be followed by a low surrogate */
            if (i + 3 < srcBytes) {
                unsigned int hi2    = (unsigned char)src[i + 2];
                unsigned int lo2    = (unsigned char)src[i + 3];
                unsigned int unit2  = be ? ((hi2 << 8) | lo2) : ((lo2 << 8) | hi2);
                if (unit2 >= 0xDC00 && unit2 <= 0xDFFF) {
                    cp  = 0x10000u + ((unit - 0xD800u) << 10) + (unit2 - 0xDC00u);
                    i  += 2; /* consume both units */
                } else {
                    cp = 0xFFFD; /* unpaired high surrogate */
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            cp = 0xFFFD; /* lone low surrogate */
        } else {
            cp = unit;
        }

        /* Encode cp to UTF-8 */
        if (cp < 0x80) {
            *p++ = (char)cp;
        } else if (cp < 0x800) {
            *p++ = (char)(0xC0 | (cp >> 6));
            *p++ = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            *p++ = (char)(0xE0 | (cp >> 12));
            *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
            *p++ = (char)(0x80 | (cp & 0x3F));
        } else {
            *p++ = (char)(0xF0 | (cp >> 18));
            *p++ = (char)(0x80 | ((cp >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((cp >> 6)  & 0x3F));
            *p++ = (char)(0x80 | (cp & 0x3F));
        }
    }
    *p = '\0';
    if (outLen) *outLen = (LONG)(p - out);
    return out;
}

/* forward declaration – defined after the Latin table below */
static char *convert_to_utf8(const char *src, LONG srcLen, int encoding);

/* Returns TRUE if buf[0..len-1] is valid UTF-8.
 * Any stray 0x80-0xFF byte that is not part of a legal multi-byte sequence
 * causes an immediate FALSE, which is exactly what Latin-1 files trigger. */
static BOOL is_valid_utf8(const char *buf, LONG len)
{
    LONG i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)buf[i++];
        int extra;
        if (c < 0x80) continue;
        if (c < 0xC2) return FALSE;       /* 0x80-0xC1: stray continuation or overlong */
        else if (c < 0xE0) extra = 1;
        else if (c < 0xF0) extra = 2;
        else if (c < 0xF5) extra = 3;
        else               return FALSE;  /* 0xF5-0xFF: invalid lead byte */
        while (extra--) {
            if (i >= len || ((unsigned char)buf[i] & 0xC0) != 0x80) return FALSE;
            i++;
        }
    }
    return TRUE;
}

/* Load a UTF-8/UTF-16 file from an already-known path into the editor.
 * If the file is not valid UTF-8 (e.g. a Latin-1 file opened by mistake),
 * falls back silently to Latin-1 decoding so the editor never sees broken bytes. */
BOOL load_utf8_from_path(struct App *ctx, const char *path, int *actual_encoding)
{
    BPTR   fh;
    char  *buf = NULL;
    char  *converted = NULL;
    LONG   size, got, textLen;
    char  *text;
    BOOL   ok = FALSE;

    if (actual_encoding) *actual_encoding = 0;
    if (!ctx || !ctx->textEditorObj) return FALSE;

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_OPENFILE), path);
        return FALSE;
    }

    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);

    buf = (char *)AllocVec((ULONG)(size + 1), MEMF_ANY);
    if (!buf) {
        printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
        Close(fh);
        return FALSE;
    }

    got = Read(fh, buf, size);
    Close(fh);

    if (got < 0) {
        printf("%s: %s\n", LOC(MSG_ERROR_OPENFILE), path);
        FreeVec(buf);
        return FALSE;
    }

    buf[got] = '\0';
    text    = buf;
    textLen = got;

    /* --- BOM detection -------------------------------------------------- */
    if (got >= 2 &&
        (unsigned char)text[0] == 0xFE && (unsigned char)text[1] == 0xFF)
    {
        converted = utf16_to_utf8(text + 2, got - 2, 1, &textLen);
        if (!converted) {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
            FreeVec(buf);
            return FALSE;
        }
        text = converted;
    }
    else if (got >= 2 &&
             (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE)
    {
        converted = utf16_to_utf8(text + 2, got - 2, 0, &textLen);
        if (!converted) {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
            FreeVec(buf);
            return FALSE;
        }
        text = converted;
    }
    else if (got >= 3 &&
             (unsigned char)text[0] == 0xEF &&
             (unsigned char)text[1] == 0xBB &&
             (unsigned char)text[2] == 0xBF)
    {
        text    += 3;
        textLen -= 3;
    }

    normalize_line_endings(text, &textLen);

    /* UTF-16 BOM paths already produced valid UTF-8 via utf16_to_utf8().
     * For all other paths, validate before handing to the editor — a
     * Latin-1 file opened here would contain bare 0x80-0xFF bytes that
     * are illegal UTF-8 and crash the gadget.  Fall back to Latin-1. */
    if (!converted && !is_valid_utf8(text, textLen)) {
        char *latin1 = convert_to_utf8(text, textLen, 1);
        if (!latin1) {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
            FreeVec(buf);
            return FALSE;
        }
        if (actual_encoding) *actual_encoding = 1;
        SetGdAttrs(ctx->textEditorObj,
            UTED_CurrentContext, (ULONG)path,
            UTED_Text,           (ULONG)latin1,
            TAG_DONE);
        FreeVec(latin1);
    } else {
        SetGdAttrs(ctx->textEditorObj,
            UTED_CurrentContext, (ULONG)path,
            UTED_Text,           (ULONG)text,
            TAG_DONE);
    }
    EgTabs_AddOrSelectTab(path);
    BMainWindow_SetTitleLocS(&app->mainwindow, MSG_WINDOW_TITLE_WITHFILE, path);
    ok = TRUE;

    if (converted) FreeVec(converted);
    FreeVec(buf);
    UpdateStatusBar();
    return ok;
}

BOOL Action_FileLoadUTF8(struct App *ctx)
{
    char *path;
    BOOL  ok;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    path = asl_request_file(CurrentMainWindow, LOC(MSG_FILE_LOAD_UTF8), FALSE);
    if (!path) return FALSE;

    {
        int encoding_used = 0;
        ok = load_utf8_from_path(ctx, path, &encoding_used);
        if (ok) {
            AppSettings_AddRecentFile(&ctx->appSettings, path, encoding_used);
            EgMenu_Rebuild(&ctx->mainwindow.menu, CurrentMainScreen, CurrentMainWindow,
                           &ctx->appSettings);
        }
    }
    FreeVec(path);
    return ok;
}

/* -------------------------------------------------------------------------
 * ISO 8859-2 (Latin-2) → Unicode code-point table, for bytes 0x80..0xFF.
 *
 * Bytes 0x80..0x9F are the same C1 controls as Latin-1 (identity mapping).
 * Bytes 0xA0..0xFF map to the Unicode code points listed in ISO 8859-2.
 * All code points are < U+0800, so every entry encodes to 1 or 2 UTF-8 bytes.
 * -------------------------------------------------------------------------*/
static const unsigned short latin2_unicode[128] = {
    /* 0x80-0x9F: C1 controls, identity (same as Latin-1) */
    0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087,
    0x0088,0x0089,0x008A,0x008B,0x008C,0x008D,0x008E,0x008F,
    0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097,
    0x0098,0x0099,0x009A,0x009B,0x009C,0x009D,0x009E,0x009F,
    /* 0xA0-0xAF */
    0x00A0,0x0104,0x02D8,0x0141,0x00A4,0x013D,0x015A,0x00A7,
    0x00A8,0x0160,0x015E,0x0164,0x0179,0x00AD,0x017D,0x017B,
    /* 0xB0-0xBF */
    0x00B0,0x0105,0x02DB,0x0142,0x00B4,0x013E,0x015B,0x02C7,
    0x00B8,0x0161,0x015F,0x0165,0x017A,0x02DD,0x017E,0x017C,
    /* 0xC0-0xCF */
    0x0154,0x00C1,0x00C2,0x0102,0x00C4,0x0139,0x0106,0x00C7,
    0x010C,0x00C9,0x0118,0x00CB,0x011A,0x00CD,0x00CE,0x010E,
    /* 0xD0-0xDF */
    0x0110,0x0143,0x0147,0x00D3,0x00D4,0x0150,0x00D6,0x00D7,
    0x0158,0x016E,0x00DA,0x0170,0x00DC,0x00DD,0x0162,0x00DF,
    /* 0xE0-0xEF */
    0x0155,0x00E1,0x00E2,0x0103,0x00E4,0x013A,0x0107,0x00E7,
    0x010D,0x00E9,0x0119,0x00EB,0x011B,0x00ED,0x00EE,0x010F,
    /* 0xF0-0xFF */
    0x0111,0x0144,0x0148,0x00F3,0x00F4,0x0151,0x00F6,0x00F7,
    0x0159,0x016F,0x00FA,0x0171,0x00FC,0x00FD,0x0163,0x02D9,
};

/* -------------------------------------------------------------------------
 * Convert a single-byte legacy encoding buffer to a UTF-8 AllocVec string.
 *
 * encoding == 1 → ISO 8859-1 (Latin-1): code point = byte value (identity)
 * encoding == 2 → ISO 8859-2 (Latin-2): code point from latin2_unicode table
 *
 * All resulting code points are < U+0800, so each source byte expands to at
 * most 2 UTF-8 bytes.  Output buffer is AllocVec(srcLen*2+1, MEMF_ANY).
 * Caller must FreeVec() the result.  Returns NULL on allocation failure.
 * -------------------------------------------------------------------------*/
static char *convert_to_utf8(const char *src, LONG srcLen, int encoding)
{
    char *out, *p;
    LONG  i;

    out = (char *)AllocVec((ULONG)(srcLen * 2 + 1), MEMF_ANY);
    if (!out) return NULL;

    p = out;
    for (i = 0; i < srcLen; i++) {
        unsigned int b  = (unsigned char)src[i];
        unsigned int cp = b; /* Latin-1: code point == byte value */

        if (encoding == 2 && b >= 0x80)
            cp = (unsigned int)latin2_unicode[b - 0x80];

        if (cp < 0x80) {
            *p++ = (char)cp;
        } else {
            /* U+0080..U+07FF: 2-byte UTF-8 */
            *p++ = (char)(0xC0 | (cp >> 6));
            *p++ = (char)(0x80 | (cp & 0x3F));
        }
    }
    *p = '\0';
    return out;
}

/* Load a single-byte encoded file from a known path into the editor.
 * encoding: 1=Latin-1, 2=Latin-2 */
BOOL load_encoded_from_path(struct App *ctx, const char *path, int encoding)
{
    BPTR   fh;
    char  *raw = NULL;
    char  *utf8 = NULL;
    LONG   size, got;
    BOOL   ok = FALSE;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_OPENFILE), path);
        return FALSE;
    }

    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);

    raw = (char *)AllocVec((ULONG)(size + 1), MEMF_ANY);
    if (!raw) {
        printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
        Close(fh);
        return FALSE;
    }

    got = Read(fh, raw, size);
    Close(fh);

    if (got >= 0) {
        raw[got] = '\0';
        normalize_line_endings(raw, &got);
        utf8 = convert_to_utf8(raw, got, encoding);
        if (utf8) {
            SetGdAttrs(ctx->textEditorObj,
                UTED_CurrentContext, (ULONG)path,
                UTED_Text,           (ULONG)utf8,
                TAG_DONE);
            EgTabs_AddOrSelectTab(path);
            BMainWindow_SetTitleLocS(&app->mainwindow, MSG_WINDOW_TITLE_WITHFILE, path);
            FreeVec(utf8);
            ok = TRUE;
        } else {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
        }
    } else {
        printf("%s: %s\n", LOC(MSG_ERROR_OPENFILE), path);
    }

    FreeVec(raw);
    UpdateStatusBar();
    return ok;
}

static BOOL load_encoded(struct App *ctx, ULONG titleMsgID, int encoding)
{
    char *path;
    BOOL  ok;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    path = asl_request_file(CurrentMainWindow, LOC(titleMsgID), FALSE);
    if (!path) return FALSE;

    ok = load_encoded_from_path(ctx, path, encoding);
    if (ok) {
        AppSettings_AddRecentFile(&ctx->appSettings, path, encoding);
        EgMenu_Rebuild(&ctx->mainwindow.menu, CurrentMainScreen, CurrentMainWindow,
                       &ctx->appSettings);
    }
    FreeVec(path);
    return ok;
}

BOOL Action_FileLoadLatin1(struct App *ctx)
{
    return load_encoded(ctx, MSG_FILE_LOAD_LATIN1, 1);
}

BOOL Action_FileLoadLatin2(struct App *ctx)
{
    return load_encoded(ctx, MSG_FILE_LOAD_LATIN2, 2);
}

BOOL Action_FileClose(struct App *ctx)
{
    EgTabs_CloseCurrentTab();
    return TRUE;
}


/* -------------------------------------------------------------------------
 * Action: Open Recent file (by slot index in appSettings.recentFiles)
 * -------------------------------------------------------------------------*/
static BOOL open_recent_file(struct App *ctx, int slot)
{
    const char *path;
    int         encoding;
    BOOL        ok;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    path = AppSettings_GetRecentFile(&ctx->appSettings, slot);
    if (!path) return FALSE;

    encoding = AppSettings_GetRecentEncoding(&ctx->appSettings, slot);

    if (encoding == 0)
        ok = load_utf8_from_path(ctx, path, &encoding);
    else
        ok = load_encoded_from_path(ctx, path, encoding);

    if (ok) {
        AppSettings_AddRecentFile(&ctx->appSettings, path, encoding);
        EgMenu_Rebuild(&ctx->mainwindow.menu, CurrentMainScreen, CurrentMainWindow,
                       &ctx->appSettings);
    }
    return ok;
}

BOOL Action_OpenRecent0(struct App *ctx) { return open_recent_file(ctx, 0); }
BOOL Action_OpenRecent1(struct App *ctx) { return open_recent_file(ctx, 1); }
BOOL Action_OpenRecent2(struct App *ctx) { return open_recent_file(ctx, 2); }
BOOL Action_OpenRecent3(struct App *ctx) { return open_recent_file(ctx, 3); }
BOOL Action_OpenRecent4(struct App *ctx) { return open_recent_file(ctx, 4); }
BOOL Action_OpenRecent5(struct App *ctx) { return open_recent_file(ctx, 5); }
BOOL Action_OpenRecent6(struct App *ctx) { return open_recent_file(ctx, 6); }
BOOL Action_OpenRecent7(struct App *ctx) { return open_recent_file(ctx, 7); }

/* -------------------------------------------------------------------------
 * Action: Save As UTF-8 – ask for a file and write the editor contents
 * -------------------------------------------------------------------------*/
BOOL Action_FileSaveUTF8(struct App *ctx)
{
    char       *path;
    BPTR        fh;
    char       *text = NULL;
    ULONG       textPtr = 0;
    BOOL        ok = FALSE;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    path = asl_request_file(CurrentMainWindow, LOC(MSG_FILE_SAVE_UTF8), TRUE);
    if (!path) return FALSE;

    /* Get text from editor (AllocVec'd – must FreeVec) */
    GetAttr(UTED_Text, ctx->textEditorObj, &textPtr);
    text = (char *)textPtr;
    if (!text) {
        FreeVec(path);
        return FALSE;
    }

    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
        FreeVec(text);
        FreeVec(path);
        return FALSE;
    }

    {
        LONG len = (LONG)strlen(text);
        if (Write(fh, text, len) == len) {
            SetGdAttrs(ctx->textEditorObj, UTED_Modified, FALSE, TAG_DONE);
            EgTabs_RenameCurrentTab(path);
            BMainWindow_SetTitleLocS(&app->mainwindow, MSG_WINDOW_TITLE_WITHFILE, path);
            AppSettings_AddRecentFile(&ctx->appSettings, path, 0);
            EgMenu_Rebuild(&ctx->mainwindow.menu, CurrentMainScreen, CurrentMainWindow,
                           &ctx->appSettings);
            ok = TRUE;
        } else {
            printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
        }
    }

    Close(fh);
    FreeVec(text);
    FreeVec(path);
    UpdateStatusBar();
    return ok;
}

/* -------------------------------------------------------------------------
 * Action: Save UTF-8 As Escaped ASCII
 *
 * All bytes < 0x80 are written as-is.
 * All bytes >= 0x80 are written as \xNN (lowercase hex, e.g. \xc3\xa9 for é).
 * Worst case: every byte expands to 4 chars, so we allocate srcLen*4+1.
 * -------------------------------------------------------------------------*/
BOOL Action_FileSaveEscaped(struct App *ctx)
{
    char       *path;
    BPTR        fh;
    char       *text = NULL;
    ULONG       textPtr = 0;
    char       *escaped = NULL;
    BOOL        ok = FALSE;
    static const char hex[] = "0123456789abcdef";

    if (!ctx || !ctx->textEditorObj) return FALSE;

    path = asl_request_file(CurrentMainWindow, LOC(MSG_FILE_SAVE_ESCAPED), TRUE);
    if (!path) return FALSE;

    GetAttr(UTED_Text, ctx->textEditorObj, &textPtr);
    text = (char *)textPtr;
    if (!text) { FreeVec(path); return FALSE; }

    {
        ULONG srcLen = (ULONG)strlen(text);
        escaped = (char *)AllocVec(srcLen * 4 + 1, MEMF_ANY);
        if (!escaped) {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
            FreeVec(text);
            FreeVec(path);
            return FALSE;
        }

        {
            const unsigned char *s = (const unsigned char *)text;
            char *d = escaped;
            while (*s) {
                if (*s < 0x80) {
                    *d++ = (char)*s;
                } else {
                    *d++ = '\\';
                    *d++ = 'x';
                    *d++ = hex[(*s >> 4) & 0x0F];
                    *d++ = hex[*s & 0x0F];
                }
                s++;
            }
            *d = '\0';
        }
    }

    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
    } else {
        LONG len = (LONG)strlen(escaped);
        if (Write(fh, escaped, len) == len) {
            SetGdAttrs(ctx->textEditorObj, UTED_Modified, FALSE, TAG_DONE);
            EgTabs_RenameCurrentTab(path);
            BMainWindow_SetTitleLocS(&app->mainwindow, MSG_WINDOW_TITLE_WITHFILE, path);
            ok = TRUE;
        } else {
            printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
        }
        Close(fh);
    }

    FreeVec(escaped);
    FreeVec(text);
    FreeVec(path);
    UpdateStatusBar();
    return ok;
}

/* -------------------------------------------------------------------------
 * Action: About – open (or switch to) a read-only about tab in the editor.
 * Text is UTF-8 so emojis render via UniTextEditor.
 * -------------------------------------------------------------------------*/
BOOL Action_FileAbout(struct App *ctx)
{
    static const char aboutKey[] = ":About EmojiGear";
    int i;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    /* If the about tab is already open, just switch to it */
    for (i = 0; i < ctx->tabCount; i++) {
        if (ctx->tabContextNames[i] &&
            strcmp(ctx->tabContextNames[i], aboutKey) == 0)
        {
            EgTabs_SwitchTo(i);
            return TRUE;
        }
    }

    /* Build the about text into the staging buffer */
    snprintf(ctx->aboutrequestertext, sizeof(ctx->aboutrequestertext) - 1,

    /* ---- header row ---- */
    "\xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8 \xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8"
    " \xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8 \xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8"
    " \xF0\x9F\x90\xA6\n\n"

    /* ---- poem ---- */
    "  At long last, upon the ancient Amiga screen,\n"
    "  words of every tongue now bloom and gleam.\n"
    "  A Unicode text editor \xe2\x80\x94 here, at last \xe2\x80\x94\n"
    "  where every glyph and emoji holds fast. \xF0\x9F\x8C\xB7\n\n"

    /* ---- title ---- */
    "     \xe2\x9c\xa8  EmojiGear v%s  \xe2\x9c\xa8\n"
    "     UTF-8 Unicode Text Editor for AmigaOS 3\n\n"

    /* ---- credits ---- */
    "  Author  : Krb\n"
    "  License : GPL\n\n"
    "  Built upon open roots, free as the spring breeze:\n\n"
    "  \xF0\x9F\x8C\xBF utf8rastport.library  (LGPL)\n"
    "       embedding FreeType2, libpng and zlib\n"
    "  \xF0\x9F\x8C\xBF UniTextEditor  (BOOPSI gadget, krb, License LGPL)\n"
    "  \xF0\x9F\x8C\xBF UniButton      (BOOPSI gadget, krb, License LGPL)\n\n"
    " This app install those fonts, you may install other ones:\n\n"
    "  LiberationSans-Regular.ttf Ascender Corporation, license GPL\n"
    "  NotoColorEmoji32.ttf       NoTo, SIL Open Font License 1.1\n"
    "  UbuntuMono-Regular.ttf     Canonical Ltd, Ubuntu Font Licence Version 1.0 (UFL)\n"
    "  OpenMoji-black-glyf.ttf    HfG Schwäbisch Gmünd, Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)\n"
    "\n"

    /* ---- links ---- */
    "\xF0\x9F\x90\xA6 Source code blossoms at:\n"
    "     https://github.com/krabobmkd/EmojiGear\n\n"
    "\xF0\x9F\x8C\xB1 Plant your bugs and feature wishes here:\n"
    "     https://github.com/krabobmkd/EmojiGear/issues\n\n"

    /* ---- footer row ---- */
    "  \xF0\x9F\x8C\xB7 \xF0\x9F\x8C\xBC \xF0\x9F\x8C\xBB"
    " May your words fly on the Amiga breeze forever."
    " \xF0\x9F\x8C\xBB \xF0\x9F\x8C\xBC \xF0\x9F\x8C\xB7\n\n"
    "\xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8 \xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8"
    " \xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8 \xF0\x9F\x90\xA6 \xF0\x9F\x8C\xB8"
    " \xF0\x9F\x90\xA6\n",

    EMOJIGEAR_VERSION);

    EgTabs_AddOrSelectTab(aboutKey);
    SetGdAttrs(ctx->textEditorObj,
               UTED_CurrentContext, (ULONG)aboutKey,
               UTED_Text,           (ULONG)ctx->aboutrequestertext,
               TAG_END);
    if (CurrentMainWindow)
        RefreshGList(ctx->textEditorObj, CurrentMainWindow, NULL, 1);
    SyncVScroller();
    SyncHScroller();
    BMainWindow_SetTitleLocS(&app->mainwindow, MSG_WINDOW_TITLE_WITHFILE,
                             "About EmojiGear");
    UpdateStatusBar();
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Action: Quit
 * -------------------------------------------------------------------------*/
BOOL Action_FileQuit(struct App *ctx)
{
    exit(0);
    return TRUE;
}

/* =========================================================================
 * Clipboard helpers
 * =========================================================================
 */

/* Write a byte buffer to the clipboard as IFF FTXT / CHRS.
 * The bytes are stored verbatim inside the CHRS chunk — callers choose the
 * encoding (UTF-8, Latin-1, or Latin-2) before calling. */
static BOOL clipboard_write(const char *text, ULONG len)
{
    struct MsgPort   *port;
    struct IOClipReq  ior;
    UBYTE            *buf;
    ULONG             padLen, formSize, bufSize;
    BOOL              ok = FALSE;

    if (!text || len == 0) return FALSE;

    padLen   = (len + 1) & ~1UL;       /* CHRS data rounded up to even bytes */
    formSize = 4 + 8 + padLen;         /* "FTXT" + "CHRS"+size + data         */
    bufSize  = 8 + formSize;           /* "FORM"+size + body                   */

    buf = (UBYTE *)AllocVec(bufSize, MEMF_ANY | MEMF_CLEAR);
    if (!buf) return FALSE;

    /* FORM header */
    buf[0]='F'; buf[1]='O'; buf[2]='R'; buf[3]='M';
    buf[4]=(UBYTE)(formSize>>24); buf[5]=(UBYTE)(formSize>>16);
    buf[6]=(UBYTE)(formSize>>8);  buf[7]=(UBYTE)(formSize);
    /* FTXT form type */
    buf[8]='F'; buf[9]='T'; buf[10]='X'; buf[11]='T';
    /* CHRS chunk */
    buf[12]='C'; buf[13]='H'; buf[14]='R'; buf[15]='S';
    buf[16]=(UBYTE)(len>>24); buf[17]=(UBYTE)(len>>16);
    buf[18]=(UBYTE)(len>>8);  buf[19]=(UBYTE)(len);
    CopyMem((APTR)(ULONG)text, &buf[20], len);

    port = CreateMsgPort();
    if (!port) { FreeVec(buf); return FALSE; }

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0) == 0) {
        ior.io_Command = CMD_WRITE;
        ior.io_ClipID  = 0;
        ior.io_Offset  = 0;
        ior.io_Data    = (STRPTR)buf;
        ior.io_Length  = bufSize;
        DoIO((struct IORequest *)&ior);

        ior.io_Command = CBD_POST;
        DoIO((struct IORequest *)&ior);

        ok = (ior.io_Error == 0);
        CloseDevice((struct IORequest *)&ior);
    }

    DeleteMsgPort(port);
    FreeVec(buf);
    return ok;
}

/* Read raw bytes from the clipboard CHRS chunk.
 * Returns an AllocVec'd NUL-terminated buffer, or NULL if the clipboard
 * is empty, not IFF FTXT, or on error.  Caller must FreeVec the result.
 * The bytes are returned verbatim — callers interpret the encoding. */
static char *clipboard_read(void)
{
    struct MsgPort   *port;
    struct IOClipReq  ior;
    UBYTE             hdr[8];
    UBYTE            *body  = NULL;
    char             *result = NULL;
    ULONG             formSize;

    port = CreateMsgPort();
    if (!port) return NULL;

    memset(&ior, 0, sizeof(ior));
    ior.io_Message.mn_ReplyPort = port;
    ior.io_Message.mn_Length    = sizeof(ior);

    if (OpenDevice("clipboard.device", 0, (struct IORequest *)&ior, 0)) {
        DeleteMsgPort(port);
        return NULL;
    }

    /* Read first 8 bytes: "FORM" + 4-byte big-endian body size */
    ior.io_Command = CMD_READ;
    ior.io_Offset  = 0;
    ior.io_Data    = (STRPTR)hdr;
    ior.io_Length  = 8;
    DoIO((struct IORequest *)&ior);

    if (ior.io_Actual < 8)                                    goto done;
    if (hdr[0]!='F'||hdr[1]!='O'||hdr[2]!='R'||hdr[3]!='M') goto done;

    formSize = ((ULONG)hdr[4]<<24)|((ULONG)hdr[5]<<16)|
               ((ULONG)hdr[6]<<8) |(ULONG)hdr[7];

    if (formSize < 4 || formSize > 4UL*1024UL*1024UL)         goto done;

    body = (UBYTE *)AllocVec(formSize, MEMF_ANY);
    if (!body)                                                 goto done;

    /* Read body (FTXT type + chunks) */
    ior.io_Command = CMD_READ;
    ior.io_Offset  = 8;
    ior.io_Data    = (STRPTR)body;
    ior.io_Length  = formSize;
    DoIO((struct IORequest *)&ior);

    if (ior.io_Actual < 4)                                      goto done;
    if (body[0]!='F'||body[1]!='T'||body[2]!='X'||body[3]!='T') goto done;

    /* Linear scan for the first CHRS chunk */
    {
        ULONG pos    = 4;
        ULONG actual = ior.io_Actual;

        while (pos + 8 <= actual) {
            ULONG cid = ((ULONG)body[pos  ]<<24)|((ULONG)body[pos+1]<<16)|
                        ((ULONG)body[pos+2]<< 8)|(ULONG)body[pos+3];
            ULONG csz = ((ULONG)body[pos+4]<<24)|((ULONG)body[pos+5]<<16)|
                        ((ULONG)body[pos+6]<< 8)|(ULONG)body[pos+7];
            pos += 8;

            if (cid == 0x43485253UL) { /* 'CHRS' */
                ULONG avail = (actual > pos) ? (actual - pos) : 0;
                ULONG len   = (csz < avail) ? csz : avail;
                if (len > 0) {
                    result = (char *)AllocVec(len + 1, MEMF_ANY);
                    if (result) {
                        CopyMem(&body[pos], result, len);
                        result[len] = '\0';
                    }
                }
                break;
            }

            pos += (csz + 1) & ~1UL; /* advance past chunk, even-padded */
        }
    }

done:
    if (body) FreeVec(body);
    CloseDevice((struct IORequest *)&ior);
    DeleteMsgPort(port);
    return result;
}

/* =========================================================================
 * UTF-8 → single-byte encoding converters
 * =========================================================================
 */

/* Decode one UTF-8 codepoint from *pp (up to end), advance *pp.
 * Returns the codepoint, or '?' for invalid/overlong sequences. */
static unsigned int utf8_decode_one(const char **pp, const char *end)
{
    const unsigned char *p = (const unsigned char *)*pp;
    unsigned int cp;
    if (p >= (const unsigned char *)end) { (*pp)++; return '?'; }
    if (*p < 0x80) {
        cp = *p++;
    } else if (*p < 0xE0) {
        if (p+1 >= (const unsigned char *)end) { *pp = (const char *)p+1; return '?'; }
        cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        p += 2;
    } else if (*p < 0xF0) {
        if (p+2 >= (const unsigned char *)end) { *pp = (const char *)p+1; return '?'; }
        cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        p += 3;
    } else {
        if (p+3 >= (const unsigned char *)end) { *pp = (const char *)p+1; return '?'; }
        cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
             ((p[2] & 0x3F) <<  6)|  (p[3] & 0x3F);
        p += 4;
    }
    *pp = (const char *)p;
    return cp;
}

/* Convert UTF-8 → ISO 8859-1 (Latin-1).
 * Code points > 0xFF become \xNN\xNN... (one \xNN per UTF-8 source byte).
 * Returns AllocVec'd NUL-terminated string; caller FreeVec's.
 * *outLen set to byte count (excluding NUL). */
static char *utf8_to_latin1(const char *src, ULONG srcLen, ULONG *outLen)
{
    static const char hex[] = "0123456789ABCDEF";
    char       *out;
    const char *p   = src;
    const char *end = src + srcLen;
    ULONG       n   = 0;

    out = (char *)AllocVec(4 * srcLen + 1, MEMF_ANY); /* worst case 4:1 (\xNN per byte) */
    if (!out) return NULL;

    while (p < end) {
        const char  *p_start = p;
        unsigned int cp = utf8_decode_one(&p, end);
        if (cp <= 0xFF) {
            out[n++] = (char)(unsigned char)cp;
        } else {
            const char *q;
            for (q = p_start; q < p; q++) {
                unsigned char b = (unsigned char)*q;
                out[n++] = '\\';
                out[n++] = 'x';
                out[n++] = hex[b >> 4];
                out[n++] = hex[b & 0xF];
            }
        }
    }
    out[n] = '\0';
    if (outLen) *outLen = n;
    return out;
}

/* Convert UTF-8 → ISO 8859-2 (Latin-2).
 * Code points not representable in Latin-2 become \xNN\xNN... (one \xNN per UTF-8 source byte).
 * Returns AllocVec'd NUL-terminated string; caller FreeVec's.
 * *outLen set to byte count (excluding NUL). */
static char *utf8_to_latin2(const char *src, ULONG srcLen, ULONG *outLen)
{
    static const char hex[] = "0123456789ABCDEF";
    char       *out;
    const char *p   = src;
    const char *end = src + srcLen;
    ULONG       n   = 0;
    int         k;

    out = (char *)AllocVec(4 * srcLen + 1, MEMF_ANY); /* worst case 4:1 (\xNN per byte) */
    if (!out) return NULL;

    while (p < end) {
        const char  *p_start = p;
        unsigned int cp = utf8_decode_one(&p, end);
        if (cp < 0x80) {
            out[n++] = (char)(unsigned char)cp;
        } else {
            /* Reverse-lookup in latin2_unicode table (entries 0x80..0xFF) */
            int found = -1;
            for (k = 0; k < 128; k++) {
                if (latin2_unicode[k] == (unsigned short)cp) {
                    found = k + 0x80;
                    break;
                }
            }
            if (found >= 0) {
                out[n++] = (char)(unsigned char)found;
            } else {
                const char *q;
                for (q = p_start; q < p; q++) {
                    unsigned char b = (unsigned char)*q;
                    out[n++] = '\\';
                    out[n++] = 'x';
                    out[n++] = hex[b >> 4];
                    out[n++] = hex[b & 0xF];
                }
            }
        }
    }
    out[n] = '\0';
    if (outLen) *outLen = n;
    return out;
}

/* =========================================================================
 * Save Latin-1 / Latin-2 helper
 * =========================================================================
 */

/* Shared save path for legacy single-byte encodings.
 * Gets the full editor text, converts from UTF-8 to the target encoding,
 * and writes to a user-selected file.
 * encoding 1 → ISO 8859-1 (Latin-1), encoding 2 → ISO 8859-2 (Latin-2). */
static BOOL save_encoded(struct App *ctx, ULONG titleMsgID, int encoding)
{

    char      *path;
    BPTR       fh;
    char      *text  = NULL;
    ULONG      textPtr = 0;
    char      *conv  = NULL;
    ULONG      convLen = 0;
    BOOL       ok = FALSE;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    path = asl_request_file(CurrentMainWindow, LOC(titleMsgID), TRUE);
    if (!path) return FALSE;

    GetAttr(UTED_Text, ctx->textEditorObj, &textPtr);
    text = (char *)textPtr;
    if (!text) { FreeVec(path); return FALSE; }

    conv = (encoding == 2)
        ? utf8_to_latin2(text, (ULONG)strlen(text), &convLen)
        : utf8_to_latin1(text, (ULONG)strlen(text), &convLen);

    FreeVec(text);

    if (!conv) {
        printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
        FreeVec(path);
        return FALSE;
    }

    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
    } else {
        if (Write(fh, conv, (LONG)convLen) == (LONG)convLen) {
            SetGdAttrs(ctx->textEditorObj, UTED_Modified, FALSE, TAG_DONE);
            EgTabs_RenameCurrentTab(path);
            BMainWindow_SetTitleLocS(&app->mainwindow, MSG_WINDOW_TITLE_WITHFILE, path);
            AppSettings_AddRecentFile(&ctx->appSettings, path, encoding);
            EgMenu_Rebuild(&ctx->mainwindow.menu, CurrentMainScreen, CurrentMainWindow,
                           &ctx->appSettings);
            ok = TRUE;
        } else {
            printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
        }
        Close(fh);
    }

    FreeVec(conv);
    FreeVec(path);
    UpdateStatusBar();
    return ok;
}

BOOL Action_FileSaveLatin1(struct App *ctx)
{
    return save_encoded(ctx, MSG_FILE_SAVE_LATIN1, 1);
}

BOOL Action_FileSaveLatin2(struct App *ctx)
{
    return save_encoded(ctx, MSG_FILE_SAVE_LATIN2, 2);
}

/* =========================================================================
 * Edit actions
 * =========================================================================
 */

BOOL Action_EditCut(struct App *ctx)
{
    char      *selText = NULL;
    BOOL       ok = FALSE;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    GetAttr(UTED_GetSelectedText, ctx->activeEditorObj, (ULONG *)&selText);
    if (!selText) return FALSE; /* nothing selected */

    ok = clipboard_write(selText, (ULONG)strlen(selText));
    FreeVec(selText);

    if (ok) {
        if(ctx->activeEditorObj == app->textEditorObj)
        {
            SetGdAttrs(ctx->activeEditorObj, UTED_DeleteSelection, 0UL, TAG_END);
        } else if(app->searchBox.window)
        {   /* those for the search box */
            SetGadgetAttrs(ctx->activeEditorObj,app->searchBox.window,NULL,
                UTED_DeleteSelection, 0UL, TAG_END);
        }
        UpdateStatusBar();
    }
    return ok;
}

BOOL Action_EditCopy(struct App *ctx)
{
    char      *selText = NULL;
    BOOL       ok;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    GetAttr(UTED_GetSelectedText, ctx->activeEditorObj, (ULONG *)&selText);
    if (!selText) return FALSE;

    ok = clipboard_write(selText, (ULONG)strlen(selText));
    FreeVec(selText);
    return ok;
}

BOOL Action_EditCopyLatin1(struct App *ctx)
{
    char      *selText = NULL;
    char      *conv;
    ULONG      convLen = 0;
    BOOL       ok;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    GetAttr(UTED_GetSelectedText, ctx->activeEditorObj, (ULONG *)&selText);
    if (!selText) return FALSE;

    conv = utf8_to_latin1(selText, (ULONG)strlen(selText), &convLen);
    FreeVec(selText);
    if (!conv) return FALSE;

    ok = clipboard_write(conv, convLen);
    FreeVec(conv);
    return ok;
}

BOOL Action_EditCopyLatin2(struct App *ctx)
{
    char      *selText = NULL;
    char      *conv;
    ULONG      convLen = 0;
    BOOL       ok;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    GetAttr(UTED_GetSelectedText, ctx->activeEditorObj, (ULONG *)&selText);
    if (!selText) return FALSE;

    conv = utf8_to_latin2(selText, (ULONG)strlen(selText), &convLen);
    FreeVec(selText);
    if (!conv) return FALSE;

    ok = clipboard_write(conv, convLen);
    FreeVec(conv);
    return ok;
}

BOOL Action_EditPaste(struct App *ctx)
{
    char      *text;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    text = clipboard_read();

    if (!text) return FALSE;

        if(ctx->activeEditorObj == app->textEditorObj)
        {
    SetGdAttrs(ctx->activeEditorObj, UTED_InsertText, (ULONG)text, TAG_END);
        } else if(app->searchBox.window)
        {   /* those for the search box */

    SetGadgetAttrs(ctx->activeEditorObj,app->searchBox.window,NULL,
            UTED_InsertText, (ULONG)text, TAG_END
            );
        }


    FreeVec(text);
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_EditPasteLatin1(struct App *ctx)
{
    char      *raw;
    char      *utf8;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    raw = clipboard_read();
    if (!raw) return FALSE;

    utf8 = convert_to_utf8(raw, (LONG)strlen(raw), 1);
    FreeVec(raw);
    if (!utf8) return FALSE;

        if(ctx->activeEditorObj == app->textEditorObj)
        {
    SetGdAttrs(ctx->activeEditorObj, UTED_InsertText, (ULONG)utf8, TAG_END);
        } else if(app->searchBox.window)
        {   /* those for the search box */
    SetGadgetAttrs(ctx->activeEditorObj,app->searchBox.window,NULL,
            UTED_InsertText, (ULONG)utf8, TAG_END
            );
        }
    FreeVec(utf8);
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_EditPasteLatin2(struct App *ctx)
{
    char      *raw;
    char      *utf8;

    if (!ctx || !ctx->activeEditorObj) return FALSE;

    raw = clipboard_read();
    if (!raw) return FALSE;

    utf8 = convert_to_utf8(raw, (LONG)strlen(raw), 2);
    FreeVec(raw);
    if (!utf8) return FALSE;

        if(ctx->activeEditorObj == app->textEditorObj)
        {
    SetGdAttrs(ctx->activeEditorObj, UTED_InsertText, (ULONG)utf8, TAG_END);
        } else if(app->searchBox.window)
        {   /* those for the search box */
    SetGadgetAttrs(ctx->activeEditorObj,app->searchBox.window,NULL,
            UTED_InsertText, (ULONG)utf8, TAG_END
            );
        }

    FreeVec(utf8);
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_EditUndo(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;
    SetGdAttrs(ctx->textEditorObj, UTED_Undo, 0UL, TAG_END);
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_EditRedo(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;
    SetGdAttrs(ctx->textEditorObj, UTED_Redo, 0UL, TAG_END);
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_EditOpenEmojiBox(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;


    OpenEmojiBoxWindow();
    return TRUE;
}



/* this one just open the window, real Find operation is Action_NavFindNext() */
BOOL Action_NavFindOpen(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;

        OpenSearchBox();

    return TRUE;
}

const char *EgSearchBox_GetUTF8SearchString(EgSearchBox *sb);
const char *EgSearchBox_GetUTF8ReplaceString(EgSearchBox *sb);
int EgSearchBox_GetCaseSensitive(EgSearchBox *sb);

BOOL Action_NavFindNext(struct App *ctx)
{
    const char *searchString;
    ULONG searchIsCaseSensitive;
    ULONG lineBefore, charBefore, lineAfter, charAfter;

    if (!ctx || !ctx->textEditorObj) return FALSE;
    searchString = EgSearchBox_GetUTF8SearchString(&app->searchBox);
    if (!searchString || *searchString == 0) return TRUE;

    searchIsCaseSensitive = EgSearchBox_GetCaseSensitive(&app->searchBox);
    SetGdAttrs(ctx->textEditorObj,
        UTED_SearchCaseSensitive, searchIsCaseSensitive, TAG_END);

    GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineBefore);
    GetAttr(UTED_CursorChar, ctx->textEditorObj, &charBefore);

    SetGdAttrs(ctx->textEditorObj, UTED_SearchNext, (ULONG)searchString, TAG_END);

    GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
    GetAttr(UTED_CursorChar, ctx->textEditorObj, &charAfter);

    if (lineAfter == lineBefore && charAfter == charBefore) {
        /* Not found forward — wrap to start of document and try again */
        SetGdAttrs(ctx->textEditorObj,
            UTED_CursorLine, 0UL,
            UTED_CursorChar, 0UL,
            TAG_END);
        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineBefore);
        GetAttr(UTED_CursorChar, ctx->textEditorObj, &charBefore);
        SetGdAttrs(ctx->textEditorObj, UTED_SearchNext, (ULONG)searchString, TAG_END);
        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
        GetAttr(UTED_CursorChar, ctx->textEditorObj, &charAfter);
        if (lineAfter == lineBefore && charAfter == charBefore) {
            DisplayBeep(CurrentMainScreen); /* not found anywhere */
            UpdateStatusBar();
            return TRUE;
        }
    }

    SetGdAttrs(ctx->textEditorObj,
        UTED_ScrollTo, lineAfter,
        UTED_ScrollToCenter, TRUE,
        TAG_END);
    SyncVScroller();
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_NavFindPrev(struct App *ctx)
{
    const char *searchString;
    ULONG searchIsCaseSensitive;
    ULONG lineBefore, charBefore, lineAfter, charAfter;
    ULONG lineCount;

    if (!ctx || !ctx->textEditorObj) return FALSE;
    searchString = EgSearchBox_GetUTF8SearchString(&app->searchBox);
    if (!searchString || *searchString == 0) return TRUE;

    searchIsCaseSensitive = EgSearchBox_GetCaseSensitive(&app->searchBox);
    SetGdAttrs(ctx->textEditorObj,
        UTED_SearchCaseSensitive, searchIsCaseSensitive, TAG_END);

    GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineBefore);
    GetAttr(UTED_CursorChar, ctx->textEditorObj, &charBefore);

    SetGdAttrs(ctx->textEditorObj, UTED_SearchPrevious, (ULONG)searchString, TAG_END);

    GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
    GetAttr(UTED_CursorChar, ctx->textEditorObj, &charAfter);

    if (lineAfter == lineBefore && charAfter == charBefore) {
        /* Not found backward — wrap to end of document and try again */
        lineCount = 0;
        GetAttr(UTED_LineCount, ctx->textEditorObj, &lineCount);
        if (lineCount > 0) lineCount--;
        SetGdAttrs(ctx->textEditorObj,
            UTED_CursorLine, lineCount,
            UTED_CursorChar, 999999UL,
            TAG_END);
        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineBefore);
        GetAttr(UTED_CursorChar, ctx->textEditorObj, &charBefore);
        SetGdAttrs(ctx->textEditorObj, UTED_SearchPrevious, (ULONG)searchString, TAG_END);
        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
        GetAttr(UTED_CursorChar, ctx->textEditorObj, &charAfter);
        if (lineAfter == lineBefore && charAfter == charBefore) {
            DisplayBeep(CurrentMainScreen); /* not found anywhere */
            UpdateStatusBar();
            return TRUE;
        }
    }

    SetGdAttrs(ctx->textEditorObj,
        UTED_ScrollTo, lineAfter,
        UTED_ScrollToCenter, TRUE,
        TAG_END);
    SyncVScroller();
    UpdateStatusBar();
    return TRUE;
}

BOOL Action_NavReplace(struct App *ctx)
{
    const char *searchString, *replaceString;
    ULONG searchIsCaseSensitive;
    ULONG lineBefore, charBefore, lineAfter, charAfter;

    if (!ctx || !ctx->textEditorObj) return FALSE;
    searchString = EgSearchBox_GetUTF8SearchString(&app->searchBox);
    if (!searchString || *searchString == 0) return TRUE;
    replaceString = EgSearchBox_GetUTF8ReplaceString(&app->searchBox);
    if (!replaceString) replaceString = "";

    searchIsCaseSensitive = EgSearchBox_GetCaseSensitive(&app->searchBox);
    SetGdAttrs(ctx->textEditorObj,
        UTED_SearchCaseSensitive, searchIsCaseSensitive, TAG_END);

    /* Replace the currently highlighted match (no-op if none pending) */
    SetGdAttrs(ctx->textEditorObj, UTED_ReplaceString, (ULONG)replaceString, TAG_END);

    /* Find the next occurrence */
    GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineBefore);
    GetAttr(UTED_CursorChar, ctx->textEditorObj, &charBefore);

    SetGdAttrs(ctx->textEditorObj, UTED_SearchNext, (ULONG)searchString, TAG_END);

    GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
    GetAttr(UTED_CursorChar, ctx->textEditorObj, &charAfter);

    if (lineAfter == lineBefore && charAfter == charBefore) {
        DisplayBeep(CurrentMainScreen);
    } else {
        SetGdAttrs(ctx->textEditorObj,
            UTED_ScrollTo, lineAfter,
            UTED_ScrollToCenter, TRUE,
            TAG_END);
        SyncVScroller();
    }
    UpdateStatusBar();
    return TRUE;

}

BOOL Action_NavReplaceAll(struct App *ctx)
{
    const char *searchString, *replaceString;
    ULONG replaceCount = 0;
    ULONG searchIsCaseSensitive;
    ULONG lineBefore, charBefore, lineAfter, charAfter;

    if (!ctx || !ctx->textEditorObj) return FALSE;
    searchString = EgSearchBox_GetUTF8SearchString(&app->searchBox);
    if (!searchString || *searchString == 0) return TRUE;
    replaceString = EgSearchBox_GetUTF8ReplaceString(&app->searchBox);
    if (!replaceString) replaceString = "";

    searchIsCaseSensitive = EgSearchBox_GetCaseSensitive(&app->searchBox);
    SetGdAttrs(ctx->textEditorObj,
        UTED_SearchCaseSensitive, searchIsCaseSensitive, TAG_END);

    /* Start from the beginning of the document */
    SetGdAttrs(ctx->textEditorObj,
        UTED_CursorLine, 0UL,
        UTED_CursorChar, 0UL,
        TAG_END);

    while (replaceCount < 100000UL) {
        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineBefore);
        GetAttr(UTED_CursorChar, ctx->textEditorObj, &charBefore);

        SetGdAttrs(ctx->textEditorObj, UTED_SearchNext, (ULONG)searchString, TAG_END);

        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
        GetAttr(UTED_CursorChar, ctx->textEditorObj, &charAfter);

        if (lineAfter == lineBefore && charAfter == charBefore)
            break; /* no more occurrences */

        SetGdAttrs(ctx->textEditorObj, UTED_ReplaceString, (ULONG)replaceString, TAG_END);
        replaceCount++;
    }

    if (replaceCount > 0) {
        GetAttr(UTED_CursorLine, ctx->textEditorObj, &lineAfter);
        SetGdAttrs(ctx->textEditorObj,
            UTED_ScrollTo, lineAfter,
            UTED_ScrollToCenter, TRUE,
            TAG_END);
        SyncVScroller();
        SyncHScroller();
        UpdateStatusBar();
    } else {
        DisplayBeep(CurrentMainScreen);
    }
    return TRUE;
}

BOOL Action_NavGotoLine(struct App *ctx)
{
    ULONG lineCount = 0;
    ULONG returnCode = 0;
    ULONG number = 0;
    Object *req;
    char bodyBuf[80];
    char gadBuf[64];
    LONG line0;

    if (!ctx || !ctx->textEditorObj) return FALSE;
    if (!CurrentMainWindow) return FALSE;

    GetAttr(UTED_LineCount, ctx->textEditorObj, &lineCount);
    if (lineCount == 0) lineCount = 1;

    snprintf(bodyBuf,79, LOC(MSG_GOTOL_PROMPT), (unsigned long)lineCount);
    snprintf(gadBuf,63,  "%s|%s", LOC(MSG_GOTOL_OK), LOC(MSG_GOTOL_CANCEL));

    if(!app->gotoLineRequester)
    {

        app->gotoLineRequester = NewObject(REQUESTER_GetClass(), NULL,
            REQ_Type,      (ULONG)REQTYPE_INTEGER,
            REQ_TitleText, (ULONG)LOC(MSG_GOTOL_TITLE),
            REQ_BodyText,  (ULONG)bodyBuf,
            REQ_GadgetText,(ULONG)gadBuf,
            // REQI_Minimum,  1L,
            // REQI_Maximum,  (LONG)lineCount,
            // REQI_Number,   1L,
            TAG_END);
    }
    if (!app->gotoLineRequester) return FALSE;

    GetAttr(UTED_CursorLine,app->textEditorObj,(ULONG*)&line0);
    number = line0 +1;

    SetAttrs(app->gotoLineRequester,
        REQI_Minimum,  1L,
        REQI_Maximum,  (LONG)lineCount,
        REQI_Number,number,
        TAG_END);

    OpenRequester(app->gotoLineRequester, CurrentMainWindow);

    GetAttr(REQ_ReturnCode, app->gotoLineRequester, &returnCode);
    if (returnCode) {

        GetAttr(REQI_Number, app->gotoLineRequester, &number);
        if (number < 1)         number = 1;
        if (number > lineCount) number = lineCount;
        line0 = (LONG)number - 1; /* 0-based */
        SetGdAttrs(ctx->textEditorObj,
            UTED_CursorLine, (ULONG)line0,
            UTED_CursorChar, 0UL,
            UTED_ScrollTo,   (ULONG)line0,
            TAG_DONE);
    }

    return (BOOL)(returnCode != 0);
}

BOOL Action_PrevText(struct App *ctx)
{
    if(ctx->tabCount<=1) return TRUE;
    EgTabs_SwitchTo((ctx->tabCurrentIndex - 1 +
                   ctx->tabCount) % ctx->tabCount);
    return TRUE;
}
BOOL Action_NextText(struct App *ctx)
{
    if(ctx->tabCount<=1) return TRUE;
    EgTabs_SwitchTo((app->tabCurrentIndex + 1) % app->tabCount);
    return TRUE;
}

BOOL Action_SettingsWindow(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;

    EgSettingsView_Open(&app->settingsView);

    return TRUE;
}

BOOL Action_FontsWindow(struct App *ctx)
{
    if (!ctx) return FALSE;

    EgFontsView_Open(&app->fontsView);

    return TRUE;
}
BOOL Action_SettingWordWrap(struct App *ctx)
{
    struct MenuItem *item;
    BOOL newWrap;

    if (!ctx || !ctx->textEditorObj) return FALSE;

    /* MENUTOGGLE has already flipped the CHECKED bit; read the new state. */
    item    = EgMenu_FindItemByActionID(&ctx->mainwindow.menu, ACTION_SETTINGS_WORDWRAP);
    newWrap = item ? ((item->Flags & CHECKED) != 0) : FALSE;

    app->appSettings.wordWrap = newWrap ? 1 : 0;

    SetGadgetAttrs((struct Gadget *)ctx->textEditorObj,
        CurrentMainWindow, NULL,
        UTED_WordWrap, (ULONG)newWrap,
        TAG_END);
    if (CurrentMainWindow)
        RefreshGList(ctx->textEditorObj, CurrentMainWindow, NULL, 1);
    SyncVScroller();
    SyncHScroller();
    UpdateStatusBar();
    return TRUE;
}
BOOL Action_SettingForceMonospace(struct App *ctx)
{
    struct MenuItem *item;
    if (!ctx || !ctx->textEditorObj) return FALSE;

    /* MENUTOGGLE has already flipped the CHECKED bit; read the new state. */
    item = EgMenu_FindItemByActionID(&ctx->mainwindow.menu, ACTION_SETTINGS_FORCEMONOSPACE);
    app->appSettings.monospace = (item && (item->Flags & CHECKED)) ? 1 : 0;

    UpdateEditorFontsFromSettings();
    if (CurrentMainWindow)
        RefreshGList(ctx->textEditorObj, CurrentMainWindow, NULL, 1);
    SyncVScroller();
    SyncHScroller();
    return TRUE;
}
BOOL Action_SettingApplyAnsi(struct App *ctx)
{
    struct MenuItem *item;
    BOOL newVal;
    if (!ctx || !ctx->textEditorObj) return FALSE;

    /* MENUTOGGLE has already flipped the CHECKED bit; read the new state. */
    item   = EgMenu_FindItemByActionID(&ctx->mainwindow.menu, ACTION_SETTINGS_APPLYANSI);
    newVal = (item && (item->Flags & CHECKED)) ? TRUE : FALSE;
    ctx->appSettings.applyAnsi = newVal ? 1 : 0;

    SetGadgetAttrs((struct Gadget *)ctx->textEditorObj,
        CurrentMainWindow, NULL,
        UTED_ApplyAnsiEscapes, (ULONG)newVal,
        TAG_END);
    if (CurrentMainWindow)
        RefreshGList(ctx->textEditorObj, CurrentMainWindow, NULL, 1);
    SyncVScroller();
    SyncHScroller();
    return TRUE;
}
BOOL Action_SettingAnsiUnixColors(struct App *ctx)
{
    struct MenuItem *item;
    BOOL newVal;
    if (!ctx || !ctx->textEditorObj) return FALSE;

    item   = EgMenu_FindItemByActionID(&ctx->mainwindow.menu, ACTION_SETTINGS_ANSI_UNIX_COLORS);
    newVal = (item && (item->Flags & CHECKED)) ? TRUE : FALSE;
    ctx->appSettings.ansiUnixColors = newVal ? 1 : 0;

    SetGadgetAttrs((struct Gadget *)ctx->textEditorObj,
        CurrentMainWindow, NULL,
        UTED_AnsiUnixColors, (ULONG)newVal,
        TAG_END);
    if (CurrentMainWindow)
        RefreshGList(ctx->textEditorObj, CurrentMainWindow, NULL, 1);
    SyncVScroller();
    SyncHScroller();
    return TRUE;
}

const int fontSizeTable[]      = { 8, 12, 16, 20, 24, 28, 32 };
const int fontSizeTableCount   = (int)(sizeof(fontSizeTable)/sizeof(fontSizeTable[0]));

BOOL Action_SettingsFontSizePlus(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;
    if (ctx->appSettings.currentFontSizeIndex >= fontSizeTableCount - 1) return FALSE;
    ctx->appSettings.currentFontSizeIndex++;
    SetGdAttrs(ctx->textEditorObj, UTED_PointSize, (ULONG)fontSizeTable[ctx->appSettings.currentFontSizeIndex], TAG_END);
    SyncVScroller();
    SyncHScroller();
    return TRUE;
}

BOOL Action_SettingsFontSizeMinus(struct App *ctx)
{
    if (!ctx || !ctx->textEditorObj) return FALSE;
    if (ctx->appSettings.currentFontSizeIndex <= 0) return FALSE;
    ctx->appSettings.currentFontSizeIndex--;
    SetGdAttrs(ctx->textEditorObj, UTED_PointSize, (ULONG)fontSizeTable[ctx->appSettings.currentFontSizeIndex], TAG_END);
    SyncVScroller();
    SyncHScroller();
    return TRUE;
}

// BOOL Action_SettingsSwitchFullScreen(struct App *ctx)
// {
//     if (!ctx || !ctx->textEditorObj) return FALSE;

//     BMainWindow_Toggle(&app->mainwindow,app->window_obj,&app->appSettings);

//     return TRUE;
// }

