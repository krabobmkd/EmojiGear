/*
 * mmgaction.c – File load / save actions for MUImojiGear.
 *
 * Adapted from EmojiGear/egaction.c.  Removed EmojiGear-specific code:
 *   - No struct App / tabs / BMainWindow / EgMenu_Rebuild
 *   - No UTED_CurrentContext (single-page editor)
 *   - AslBase, IntuitionBase etc. declared extern from muimojiGear.c
 *
 * Encoding support:
 *   Load: UTF-8 (with UTF-16 BOM fallback) + ISO 8859-1/2, with
 *         automatic Latin-1 fallback when the file is not valid UTF-8.
 *   Save: UTF-8 verbatim; Latin-1 / Latin-2 with \xNN escapes for
 *         code points that cannot be represented in the target encoding.
 *
 * lastDir is saved to AppSettings after every successful ASL request
 * and is offered as the initial directory on the next request.
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mmgaction.h"
#include "muimojiGear.h"       /* struct App *app, App_GetRawEditorWin, App_UpdateStatus */

#include "../EmojiGear/eglocale.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <dos/dos.h>
#include <libraries/mui.h>     /* MUIA_Boopsi_Object, MUIA_Window_Window constants */
#include <libraries/asl.h>
#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>

/* Library bases are opened (and owned) by muimojiGear.c */
extern struct Library       *AslBase;
extern struct IntuitionBase *IntuitionBase;

extern struct Task *mmgMainTask;

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

/* Delegate to the shared App_GetRawEditorWin for consistency. */
static void getRaw(struct Gadget **gOut, struct Window **wOut)
{
    App_GetRawEditorWin(gOut, wOut);
}

/* Ask ASL for a file path.  Uses app->settings.lastDir as initial drawer.
 * Returns an AllocVec'd full path on success, NULL on cancel / error.
 * Updates app->settings.lastDir on success. */
static char *asl_request_file(const char *title, BOOL saveMode)
{
    struct FileRequester *req;
    char  *result  = NULL;
    BOOL ok;
    const char *initDir = (app && app->settings.lastDir) ? app->settings.lastDir : NULL;
    struct Window *rawW = NULL;

    if (!AslBase) return NULL;

    /* Get the raw Intuition window so ASL opens on the right screen.
     * ASLFR_PrivateIDCMP is critical for MUI (and any app with a shared
     * IDCMP): without it ASL shares the parent window's IDCMP port and
     * consumes IDCMP_REFRESHWINDOW events that MUI is waiting for, leaving
     * the window frozen / glitched until the next resize forces a repaint.
     * (NDK3.5 asl.doc: "your window will lose IDCMP_REFRESHWINDOW events")
     * ASLFR_SleepWindow blocks gadget/menu activity in the parent while the
     * requester is open, which is the correct OS behaviour. */
    if (app && app->winObj)
        GetAttr(MUIA_Window_Window, app->winObj, (ULONG *)&rawW);


    req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
            TAG_END);
    if (!req) return NULL;

    ok = (BOOL)AslRequestTags(req,
            rawW ? ASLFR_Window      : TAG_IGNORE, (ULONG)rawW,
            ASLFR_TitleText,      (ULONG)title,
            ASLFR_DoSaveMode,      (ULONG)saveMode,
           initDir ? ASLFR_InitialDrawer : TAG_IGNORE,
            initDir ? (ULONG)initDir    : 0UL,
            ASLFR_PrivateIDCMP, TRUE,

            TAG_END);


    // req = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
    //         ASLFR_TitleText,    (ULONG)title,
    //         ASLFR_DoSaveMode,   (ULONG)saveMode,
    //         ASLFR_RejectIcons,  TRUE,
    //   //      ASLFR_PrivateIDCMP, TRUE,          /* own IDCMP – never shares MUI's port */
    //   //      rawW ? ASLFR_Window      : TAG_IGNORE, (ULONG)rawW,
    //  //       rawW ? ASLFR_SleepWindow : TAG_IGNORE, (ULONG)TRUE,
    //         initDir ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)initDir,
    //         TAG_DONE);
    // if (!req) return NULL;

    // if (AslRequest(req, NULL))

    if(ok){
        ULONG dirLen  = (ULONG)strlen(req->fr_Drawer);
        ULONG fileLen = (ULONG)strlen(req->fr_File);
        ULONG total   = dirLen + 1 + fileLen + 1;

  //re,test      if (app) AppSettings_SetLastDir(&app->settings, req->fr_Drawer);

        result = (char *)AllocVec(total, MEMF_ANY);
        if (result) {
            strcpy(result, req->fr_Drawer);
            if (dirLen > 0 &&
                result[dirLen - 1] != '/' &&
                result[dirLen - 1] != ':')
                strcat(result, "/");
            strcat(result, req->fr_File);
        }
    }

    FreeAslRequest(req);
    if(rawW)
    {
        /* silly , but unlock the freeze bug under os3.9 */
        SizeWindow(rawW,-1,-1);
        SizeWindow(rawW,1,1);
        ActivateWindow(rawW);

    }

    return result;
}

/* Normalize line endings in-place: CRLF → LF, lone CR → LF. */
static void normalize_line_endings(char *buf, LONG *len)
{
    LONG r, w;
    for (r = 0, w = 0; r < *len; r++) {
        if (buf[r] == '\r') {
            buf[w++] = '\n';
            if (r + 1 < *len && buf[r + 1] == '\n') r++;
        } else {
            buf[w++] = buf[r];
        }
    }
    buf[w] = '\0';
    *len = w;
}

/* Convert a UTF-16 buffer (BOM already stripped) to UTF-8.
 * be=1 → big-endian, be=0 → little-endian.
 * Returns AllocVec'd NUL-terminated UTF-8 string; *outLen = byte count.
 * Caller must FreeVec the result. */
static char *utf16_to_utf8(const char *src, LONG srcBytes, int be, LONG *outLen)
{
    LONG  maxOut = 3 * (srcBytes / 2) + 1;
    char *out = (char *)AllocVec((ULONG)maxOut, MEMF_ANY);
    char *p;
    LONG  i;

    if (!out) return NULL;
    p = out;

    for (i = 0; i + 1 < srcBytes; i += 2) {
        unsigned int hi = (unsigned char)src[be ? i     : i + 1];
        unsigned int lo = (unsigned char)src[be ? i + 1 : i    ];
        unsigned int cp = (hi << 8) | lo;

        /* Surrogate pair */
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < srcBytes) {
            unsigned int hi2 = (unsigned char)src[be ? i+2 : i+3];
            unsigned int lo2 = (unsigned char)src[be ? i+3 : i+2];
            unsigned int sur = (hi2 << 8) | lo2;
            if (sur >= 0xDC00 && sur <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (sur - 0xDC00);
                i += 2;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }

        if      (cp < 0x80)    { *p++ = (char)cp; }
        else if (cp < 0x800)   { *p++ = (char)(0xC0|(cp>>6));  *p++ = (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000) { *p++ = (char)(0xE0|(cp>>12)); *p++ = (char)(0x80|((cp>>6)&0x3F)); *p++ = (char)(0x80|(cp&0x3F)); }
        else {
            *p++ = (char)(0xF0|(cp>>18));
            *p++ = (char)(0x80|((cp>>12)&0x3F));
            *p++ = (char)(0x80|((cp>>6)&0x3F));
            *p++ = (char)(0x80|(cp&0x3F));
        }
    }
    *p = '\0';
    *outLen = (LONG)(p - out);
    return out;
}

/* Returns TRUE when buf[0..len-1] is valid UTF-8. */
static BOOL is_valid_utf8(const char *buf, LONG len)
{
    LONG i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)buf[i++];
        int extra;
        if (c < 0x80) continue;
        if (c < 0xC2) return FALSE;
        else if (c < 0xE0) extra = 1;
        else if (c < 0xF0) extra = 2;
        else if (c < 0xF5) extra = 3;
        else               return FALSE;
        while (extra--) {
            if (i >= len || ((unsigned char)buf[i] & 0xC0) != 0x80) return FALSE;
            i++;
        }
    }
    return TRUE;
}

/* ISO 8859-2 → Unicode table for bytes 0x80..0xFF.
 * Bytes 0x80..0x9F are C1 controls (identity, same as Latin-1). */
static const unsigned short latin2_unicode[128] = {
    /* 0x80-0x9F: C1 controls */
    0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087,
    0x0088,0x0089,0x008A,0x008B,0x008C,0x008D,0x008E,0x008F,
    0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097,
    0x0098,0x0099,0x009A,0x009B,0x009C,0x009D,0x009E,0x009F,
    /* 0xA0-0xFF: Latin-2 specific */
    0x00A0,0x0104,0x02D8,0x0141,0x00A4,0x013D,0x015A,0x00A7,
    0x00A8,0x0160,0x015E,0x0164,0x0179,0x00AD,0x017D,0x017B,
    0x00B0,0x0105,0x02DB,0x0142,0x00B4,0x013E,0x015B,0x02C7,
    0x00B8,0x0161,0x015F,0x0165,0x017A,0x02DD,0x017E,0x017C,
    0x0154,0x00C1,0x00C2,0x0102,0x00C4,0x0139,0x0106,0x00C7,
    0x010C,0x00C9,0x0118,0x00CB,0x011A,0x00CD,0x00CE,0x010E,
    0x0110,0x0143,0x0147,0x00D3,0x00D4,0x0150,0x00D6,0x00D7,
    0x0158,0x016E,0x00DA,0x0170,0x00DC,0x00DD,0x0162,0x00DF,
    0x0155,0x00E1,0x00E2,0x0103,0x00E4,0x013A,0x0107,0x00E7,
    0x010D,0x00E9,0x0119,0x00EB,0x011B,0x00ED,0x00EE,0x010F,
    0x0111,0x0144,0x0148,0x00F3,0x00F4,0x0151,0x00F6,0x00F7,
    0x0159,0x016F,0x00FA,0x0171,0x00FC,0x00FD,0x0163,0x02D9
};

/* Convert single-byte encoding (1=Latin-1, 2=Latin-2) to UTF-8.
 * Returns AllocVec'd NUL-terminated UTF-8 string; caller FreeVec's. */
static char *convert_to_utf8(const char *src, LONG srcLen, int encoding)
{
    char *out = (char *)AllocVec((ULONG)(srcLen * 2 + 1), MEMF_ANY);
    const unsigned char *s = (const unsigned char *)src;
    const unsigned char *end = s + srcLen;
    char *d;

    if (!out) return NULL;
    d = out;

    while (s < end) {
        unsigned int cp = *s++;
        if (encoding == 2 && cp >= 0x80)
            cp = (unsigned int)latin2_unicode[cp - 0x80];
        if      (cp < 0x80)  { *d++ = (char)cp; }
        else if (cp < 0x800) { *d++ = (char)(0xC0|(cp>>6)); *d++ = (char)(0x80|(cp&0x3F)); }
        else { *d++ = (char)(0xE0|(cp>>12)); *d++ = (char)(0x80|((cp>>6)&0x3F)); *d++ = (char)(0x80|(cp&0x3F)); }
    }
    *d = '\0';
    return out;
}

/* Decode one UTF-8 codepoint from *pp (up to end), advance *pp.
 * Returns '?' for invalid / overlong sequences. */
static unsigned int utf8_decode_one(const char **pp, const char *end)
{
    const unsigned char *p = (const unsigned char *)*pp;
    unsigned int cp;
    if (p >= (const unsigned char *)end) { (*pp)++; return '?'; }
    if (*p < 0x80) {
        cp = *p++;
    } else if (*p < 0xE0) {
        if (p+1 >= (const unsigned char *)end) { *pp=(const char*)p+1; return '?'; }
        cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
    } else if (*p < 0xF0) {
        if (p+2 >= (const unsigned char *)end) { *pp=(const char*)p+1; return '?'; }
        cp = ((*p&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F); p += 3;
    } else {
        if (p+3 >= (const unsigned char *)end) { *pp=(const char*)p+1; return '?'; }
        cp = ((*p&0x07)<<18)|((p[1]&0x3F)<<12)|((p[2]&0x3F)<<6)|(p[3]&0x3F); p += 4;
    }
    *pp = (const char *)p;
    return cp;
}

/* Convert UTF-8 → ISO 8859-1.  Code points > 0xFF become \xNN sequences.
 * Returns AllocVec'd NUL-terminated string; *outLen = byte count. */
static char *utf8_to_latin1(const char *src, ULONG srcLen, ULONG *outLen)
{
    static const char hex[] = "0123456789ABCDEF";
    char       *out = (char *)AllocVec(4 * srcLen + 1, MEMF_ANY);
    const char *p   = src, *end = src + srcLen;
    ULONG       n   = 0;

    if (!out) return NULL;
    while (p < end) {
        const char  *ps = p;
        unsigned int cp = utf8_decode_one(&p, end);
        if (cp <= 0xFF) {
            out[n++] = (char)(unsigned char)cp;
        } else {
            const char *q;
            for (q = ps; q < p; q++) {
                unsigned char b = (unsigned char)*q;
                out[n++]='\\'; out[n++]='x';
                out[n++]=hex[b>>4]; out[n++]=hex[b&0xF];
            }
        }
    }
    out[n] = '\0';
    if (outLen) *outLen = n;
    return out;
}

/* Convert UTF-8 → ISO 8859-2.  Unmappable code points become \xNN sequences. */
static char *utf8_to_latin2(const char *src, ULONG srcLen, ULONG *outLen)
{
    static const char hex[] = "0123456789ABCDEF";
    char       *out = (char *)AllocVec(4 * srcLen + 1, MEMF_ANY);
    const char *p   = src, *end = src + srcLen;
    ULONG       n   = 0;
    int         k;

    if (!out) return NULL;
    while (p < end) {
        const char  *ps = p;
        unsigned int cp = utf8_decode_one(&p, end);
        if (cp < 0x80) {
            out[n++] = (char)(unsigned char)cp;
        } else {
            int found = -1;
            for (k = 0; k < 128; k++) {
                if (latin2_unicode[k] == (unsigned short)cp) { found = k + 0x80; break; }
            }
            if (found >= 0) {
                out[n++] = (char)(unsigned char)found;
            } else {
                const char *q;
                for (q = ps; q < p; q++) {
                    unsigned char b = (unsigned char)*q;
                    out[n++]='\\'; out[n++]='x';
                    out[n++]=hex[b>>4]; out[n++]=hex[b&0xF];
                }
            }
        }
    }
    out[n] = '\0';
    if (outLen) *outLen = n;
    return out;
}

/* =========================================================================
 * Shared load path: open file, detect/strip BOM, normalise endings,
 * validate or convert encoding, push text to editor.
 * encoding=0 → try UTF-8 (auto-fallback to Latin-1)
 * encoding=1 → force Latin-1 decode
 * encoding=2 → force Latin-2 decode
 * Returns the effective encoding used (may differ from requested for enc=0).
 * =========================================================================
 */
static BOOL load_file(const char *path, int encoding, int *actual_enc)
{
    BPTR   fh;
    char  *buf  = NULL;
    char  *conv = NULL;
    LONG   size, got, textLen;
    char  *text;
    struct Gadget *rawG = NULL;
    struct Window *rawW = NULL;
    BOOL   ok = FALSE;

    if (actual_enc) *actual_enc = encoding;

    getRaw(&rawG, &rawW);

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_OPENFILE), path);
        return FALSE;
    }
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size < 0) size = 0;

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

    if (encoding == 0) {
        /* UTF-8 path: strip BOM, handle UTF-16, fall back to Latin-1 */
        if (got >= 2 &&
            (unsigned char)text[0] == 0xFE && (unsigned char)text[1] == 0xFF) {
            conv = utf16_to_utf8(text + 2, got - 2, 1, &textLen);
        } else if (got >= 2 &&
                   (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE) {
            conv = utf16_to_utf8(text + 2, got - 2, 0, &textLen);
        } else if (got >= 3 &&
                   (unsigned char)text[0] == 0xEF &&
                   (unsigned char)text[1] == 0xBB &&
                   (unsigned char)text[2] == 0xBF) {
            text += 3; textLen -= 3;
        }

        if (conv) { text = conv; }

        normalize_line_endings(text, &textLen);

        if (!conv && !is_valid_utf8(text, textLen)) {
            /* Not valid UTF-8: decode silently as Latin-1 */
            char *latin1 = convert_to_utf8(text, textLen, 1);
            if (!latin1) {
                printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
                FreeVec(buf);
                return FALSE;
            }
            if (actual_enc) *actual_enc = 1;
            if(rawW)
                SetGadgetAttrs(rawG,rawW,NULL, UTED_Text, (ULONG)latin1, TAG_DONE);
            else
                SetAttrs(rawG, UTED_Text, (ULONG)latin1, TAG_DONE);
            FreeVec(latin1);
        } else {
            if(rawW)
            {
             printf("before SetGadgetAttrs UTED_Text\n");
                SetGadgetAttrs(rawG,rawW,NULL, UTED_Text, (ULONG)text, TAG_DONE);
             printf("aft SetGadgetAttrs UTED_Text\n");
            }
            else
                SetAttrs(rawG, UTED_Text, (ULONG)text, TAG_DONE);
        }

    } else {
        /* Explicit Latin-1 or Latin-2 load */
        char *utf8 = convert_to_utf8(text, textLen, encoding);
        if (!utf8) {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
            FreeVec(buf);
            return FALSE;
        }
        normalize_line_endings(utf8, &textLen); /* textLen reused as scratch */
        //re
        // if(rawW)
        //     SetGadgetAttrs(rawG,rawW,NULL, UTED_Text, (ULONG)utf8, TAG_DONE);
        // else
        //     SetAttrs(rawG, UTED_Text, (ULONG)utf8, TAG_DONE);

        FreeVec(utf8);
    }

    ok = TRUE;

    // if (rawG && rawW) RefreshGList(rawG, rawW, NULL, 1);
    printf("bef App_UpdateStatus\n");
    App_UpdateStatus();
    printf("aft App_UpdateStatus\n");
    if (conv) FreeVec(conv);
    FreeVec(buf);

 //doesnt help
// DoMethod(app->appObj, MUIM_Application_CheckRefresh);

    printf("ok\n");
  //test  if (mmgMainTask) Signal(mmgMainTask, SIGBREAKF_CTRL_F);
    return ok;
}

/* =========================================================================
 * Shared save path: get editor text, convert, write to file.
 * encoding=0 → UTF-8 verbatim
 * encoding=1 → Latin-1 (unmappable → \xNN)
 * encoding=2 → Latin-2 (unmappable → \xNN)
 * =========================================================================
 */
static BOOL save_file(const char *title, int encoding)
{
    char  *path;
    BPTR   fh;
    char  *text  = NULL;
    ULONG  textPtr = 0;
    char  *conv  = NULL;
    ULONG  convLen = 0;
    BOOL   ok = FALSE;
    struct Gadget *rawG = NULL;
    struct Window *rawW = NULL;

    if (!app->editorObj) return FALSE;

    path = asl_request_file(title, TRUE);
    if (!path) return FALSE;

    /* Get text from editor (AllocVec'd – must FreeVec) */
    getRaw(&rawG, &rawW);
    if (rawG)
        GetAttr(UTED_Text, (Object *)rawG, &textPtr);
    else
        GetAttr(UTED_Text, app->editorObj, &textPtr);
    text = (char *)textPtr;
    if (!text) { FreeVec(path); return FALSE; }

    if (encoding == 0) {
        conv    = text;
        convLen = (ULONG)strlen(text);
        text    = NULL; /* avoid double-free: conv == text ptr */
    } else {
        conv = (encoding == 2)
             ? utf8_to_latin2(text, (ULONG)strlen(text), &convLen)
             : utf8_to_latin1(text, (ULONG)strlen(text), &convLen);
        FreeVec(text); text = NULL;
        if (!conv) {
            printf("%s\n", LOC(MSG_ERROR_NOMEMORY));
            FreeVec(path);
            return FALSE;
        }
    }

    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
    } else {
        if (Write(fh, conv, (LONG)convLen) == (LONG)convLen) {
            /* Clear modified flag */
            SetAttrs(app->editorObj, UTED_Modified, FALSE, TAG_DONE);
            AppSettings_AddRecentFile(&app->settings, path, encoding);
            ok = TRUE;
        } else {
            printf("%s: %s\n", LOC(MSG_ERROR_SAVEFILE), path);
        }
        Close(fh);
    }

    FreeVec(conv);
    FreeVec(path);

    if (ok) App_UpdateStatus();
    return ok;
}

/* =========================================================================
 * Public action functions
 * =========================================================================
 */

BOOL MmgAction_LoadUTF8(void)
{
    char *path;
    int   enc = 0;
    BOOL  ok;
    if (!app->editorObj) return FALSE;
    path = asl_request_file(LOC(MSG_FILE_LOAD_UTF8), FALSE);
 //path = "/examples_text/test_utf8_4_story.txt";

    if (!path) return FALSE;
    ok = load_file(path, 0, &enc);
    if (ok) AppSettings_AddRecentFile(&app->settings, path, enc);

   FreeVec(path);
    return ok;
}

BOOL MmgAction_LoadLatin1(void)
{
    char *path;
    BOOL  ok;
    if (!app->editorObj) return FALSE;
    path = asl_request_file(LOC(MSG_FILE_LOAD_LATIN1), FALSE);
    if (!path) return FALSE;
    ok = load_file(path, 1, NULL);
    if (ok) AppSettings_AddRecentFile(&app->settings, path, 1);
    FreeVec(path);
    return ok;
}

BOOL MmgAction_LoadLatin2(void)
{
    char *path;
    BOOL  ok;
    if (!app->editorObj) return FALSE;
    path = asl_request_file(LOC(MSG_FILE_LOAD_LATIN2), FALSE);
    if (!path) return FALSE;
    ok = load_file(path, 2, NULL);
    if (ok) AppSettings_AddRecentFile(&app->settings, path, 2);
    FreeVec(path);
    return ok;
}

BOOL MmgAction_SaveUTF8(void)   { return save_file(LOC(MSG_FILE_SAVE_UTF8),   0); }
BOOL MmgAction_SaveLatin1(void) { return save_file(LOC(MSG_FILE_SAVE_LATIN1), 1); }
BOOL MmgAction_SaveLatin2(void) { return save_file(LOC(MSG_FILE_SAVE_LATIN2), 2); }
