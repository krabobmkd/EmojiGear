/*
 * testmuiuted.c – MUI app embedding UniTextEditor + UniButton BOOPSI gadgets
 *
 * Layout:
 *   vertical group
 *   ├─ UniTextEditor  (MUIC_Boopsi, fills most space)
 *   └─ horizontal group
 *        ├─ "📋 Copy"   UniButton (MUIC_Boopsi)
 *        ├─ "📌 Paste"  UniButton (MUIC_Boopsi)
 *        └─ "Quit"      MUI Text button
 *
 * BOOPSI NOTIFICATION ROUTING (ICA_TARGET pattern):
 *   All three BOOPSI gadgets share the same notifyTargetObj as ICA_TARGET.
 *   The notifyDispatch identifies senders by GA_ID and sets per-gadget flags:
 *     editorChanged   – editor text/scroll/cursor changed  → redraw editor
 *     btnCopyPressed  – copy button clicked                → copy selection
 *     btnPastePressed – paste button clicked               → paste buffer
 *   It then signals SIGBREAKF_CTRL_F to the main task.
 *   The event loop waits on that signal and processes all pending flags in
 *   application-task context (safe for Intuition/graphics calls).
 *
 * ACTIVATION:
 *   UKM_Internal: editor handles keyboard directly via GM_HANDLEINPUT.
 *   MUIA_Window_ActiveObject gives the editor initial focus after open.
 *   MUI switches focus when another object is clicked.
 *
 * MUI_NewObjectB:
 *   GCC O1+ inlines MUI_NewObject and discards varargs. noinline fixes this.
 *   See MUI_NewObject_GCC_bug.txt for the full analysis.
 *
 * Usage: testmuiuted [font.ttf] [pointsize]
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>  /* GA_Selected */
#include <intuition/icclass.h>      /* ICA_TARGET  */
#include <images/bevel.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <libraries/mui.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>
#include <gadgets/unibutton.h>
#include <proto/unibutton.h>

/* --- library bases (must be global for proto/inline headers) --- */
struct Library       *MUIMasterBase     = NULL;
struct GfxBase       *GfxBase           = NULL;
struct IntuitionBase *IntuitionBase     = NULL;
struct Library       *UniTextEditorBase = NULL;
struct Library       *UniButtonBase     = NULL;
struct Library       *UtilityBase       = NULL;

/* ---------------------------------------------------------------------------
 * MUI_NewObjectB – GCC-safe replacement for the SDK's MUI_NewObject.
 * Root cause: -finline at O1 inlines the __inline body and -ftree-dce
 * discards the '...' args.  __attribute__((noinline)) forces a real m68k
 * stack frame so &tags addresses the full taglist.
 * See MUI_NewObject_GCC_bug.txt for the full analysis.
 * --------------------------------------------------------------------------- */
static Object * __attribute__((noinline))
MUI_NewObjectB(const char *cl, Tag tags, ...)
{
    return MUI_NewObjectA(cl, (struct TagItem *) &tags);
}

/* ---------------------------------------------------------------------------
 * BOOPSI notification target – shared ICA_TARGET for all gadgets
 *
 * All BOOPSI gadgets point their ICA_TARGET here.  The dispatcher reads
 * GA_ID to identify the sender and sets per-gadget boolean flags, then
 * signals SIGBREAKF_CTRL_F so the main-loop Wait() wakes immediately.
 *
 * Safety: this runs in the Intuition input-handler task context
 * (GM_HANDLEINPUT), so only Signal() and FindTagItem() are safe here.
 * All actual processing (Intuition, graphics, AllocVec) happens later
 * in the main task when it drains the flags after the CTRL_F signal.
 * --------------------------------------------------------------------------- */

#ifdef __GNUC__
#  define REGARG(reg, decl) decl __asm(#reg)
#else
#  define REGARG(reg, decl) register __##reg decl
#endif

typedef ULONG (*RAWFUNC)();

/* GA_IDs for all BOOPSI gadgets that share notifyTargetObj */
#define ID_UNITEXTEDITOR  1
#define ID_COPY_BTN       2
#define ID_PASTE_BTN      3

static struct Task *mainTask      = NULL;
static BOOL  editorChanged        = FALSE;
static BOOL  btnCopyPressed       = FALSE;
static BOOL  btnPastePressed      = FALSE;
static Class  *notifyClass        = NULL;
static Object *notifyTargetObj    = NULL;

static ULONG notifyDispatch(
    REGARG(a0, Class *cl),
    REGARG(a2, Object *obj),
    REGARG(a1, Msg msg))
{
    switch (msg->MethodID) {
    case OM_NEW:
        return DoSuperMethodA(cl, obj, msg);

    case OM_NOTIFY:
    case OM_UPDATE: {
        struct opUpdate *upd = (struct opUpdate *)msg;
        struct TagItem  *tag;
        ULONG senderID = 0;

        tag = FindTagItem(GA_ID, upd->opu_AttrList);
        if (tag) senderID = (ULONG)tag->ti_Data;

        switch (senderID) {
        case ID_COPY_BTN:
            tag = FindTagItem(GA_Selected, upd->opu_AttrList);
            if (tag && tag->ti_Data) btnCopyPressed = TRUE;
            break;
        case ID_PASTE_BTN:
            tag = FindTagItem(GA_Selected, upd->opu_AttrList);
            if (tag && tag->ti_Data) btnPastePressed = TRUE;
            break;
        default: /* ID_UNITEXTEDITOR or anything else */
            editorChanged = TRUE;
            break;
        }

        if (mainTask) Signal(mainTask, SIGBREAKF_CTRL_F);
        return 0;
    }

    default:
        return DoSuperMethodA(cl, obj, msg);
    }
}

static int initNotifyTarget(void)
{
    mainTask = FindTask(NULL);

    notifyClass = MakeClass(NULL, "modelclass", NULL, 0, 0);
    if (!notifyClass) return 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
    notifyClass->cl_Dispatcher.h_Entry = (HOOKFUNC)notifyDispatch;
#pragma GCC diagnostic pop

    notifyTargetObj = (Object *)NewObject(notifyClass, NULL, TAG_DONE);
    if (!notifyTargetObj) { FreeClass(notifyClass); notifyClass = NULL; return 0; }
    return 1;
}

static void closeNotifyTarget(void)
{
    if (notifyTargetObj) { DisposeObject(notifyTargetObj); notifyTargetObj = NULL; }
    if (notifyClass)     { FreeClass(notifyClass);         notifyClass     = NULL; }
}

/* --------------------------------------------------------------------------- */

#define ID_QUIT 10


static const char INITIAL_TEXT[] =
    "Hello from UniTextEditor inside MUI!\n"
    "This BOOPSI gadget sits in a MUI layout.\n"
    "\n"
    "Select text then press \xF0\x9F\x93\x8B to copy,\n"
    "then \xF0\x9F\x93\x8C to paste it back.\n"
    "Emoji: \xF0\x9F\x98\x80 \xF0\x9F\x90\xB8 \xF0\x9F\x8E\x89 \xE2\x98\x83\n";

int main(int argc, char *argv[])
{
    const char *fontPath  = "LiberationSans-Regular.ttf";
    int         pointSize = 16;
    int         exitCode  = 0;

    Object *editor_obj = NULL;
    Object *copy_btn   = NULL;
    Object *paste_btn  = NULL;
    Object *quit_btn   = NULL;
    Object *hgroup     = NULL;
    Object *vgroup     = NULL;
    Object *win_obj    = NULL;
    Object *app_obj    = NULL;
    ULONG   URPDrawContext = NULL; /* to share unicode draw context between gadgets */

    if (argc > 1) fontPath  = argv[1];
    if (argc > 2) pointSize = atoi(argv[2]);
    if (pointSize < 8)  pointSize = 8;
    if (pointSize > 72) pointSize = 72;

    /* --- open libraries --- */
    UtilityBase = OpenLibrary("utility.library", 37L);
    if (!UtilityBase) { puts("ERROR: utility.library v37+"); exitCode = 1; goto cleanup; }

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) { puts("ERROR: graphics.library v39+"); exitCode = 1; goto cleanup; }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) { puts("ERROR: intuition.library v39+"); exitCode = 1; goto cleanup; }

    UniTextEditorBase = OpenLibrary("gadgets/unitexteditor.gadget", 2L);
    if (!UniTextEditorBase) { puts("ERROR: gadgets/unitexteditor.gadget"); exitCode = 1; goto cleanup; }

    UniButtonBase = OpenLibrary("gadgets/unibutton.gadget", 1L);
    if (!UniButtonBase) { puts("ERROR: gadgets/unibutton.gadget"); exitCode = 1; goto cleanup; }

    MUIMasterBase = OpenLibrary("muimaster.library", 19L);
    if (!MUIMasterBase) { puts("ERROR: muimaster.library v19+ (MUI 3.8+)"); exitCode = 1; goto cleanup; }

    if (!initNotifyTarget()) {
        puts("ERROR: cannot create notification target");
        exitCode = 1; goto cleanup;
    }

    /* --- editor (MUIC_Boopsi wrapping unitexteditor.gadget) --- */
    editor_obj = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,     (ULONG)UNITEXTEDITOR_GetClass(),
        MUIA_Boopsi_MinWidth,  200,
        MUIA_Boopsi_MinHeight, 150,
        MUIA_Boopsi_Smart,     TRUE,
        MUIA_Boopsi_Remember,  UTED_PointSize,
        MUIA_Boopsi_Remember,  UTED_KeyMessageMode,
        MUIA_Frame,            MUIV_Frame_InputList,
        GA_Left,   0, GA_Top,    0, GA_Width, 0, GA_Height, 0,
        GA_ID,                 ID_UNITEXTEDITOR,
        ICA_TARGET,            (ULONG)notifyTargetObj,
        UTED_PointSize,        (ULONG)pointSize,
        UTED_AddFont,          (ULONG)fontPath,
        UTED_AddFont,          (ULONG)"NotoColorEmoji32.ttf",
        UTED_KeyMessageMode,   UKM_Internal,
        UTED_BevelStyle,       BVS_NONE,
        UTED_LeftMargin,       4,
        UTED_RightMargin,      4,
        UTED_TopMargin,        3,
        UTED_BottomMargin,     3,
        UTED_Text,             (ULONG)INITIAL_TEXT,
        TAG_DONE);
    printf("editor_obj: %08x\n", (unsigned int)editor_obj);

    if(editor_obj)
    {   /* get the draw context with all fonts glyphs so we share it with buttons */
        /* can we get boopsi level attribs from mui level object ? -> yes ! Magic ! */
        GetAttr(UTED_URPDrawContext,editor_obj,&URPDrawContext);
    }

    /* --- copy button (MUIC_Boopsi wrapping unibutton.gadget) ---
     * Each button has its own URPDrawContext (fonts loaded independently).
     * GA_Text is the UniButton label tag.  The emoji + text label shows the
     * button's purpose visually even without reading the English word.      */
    copy_btn = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,     (ULONG)UNIBUTTON_GetClass(),
        MUIA_Boopsi_MinWidth,  60,
        MUIA_Boopsi_MinHeight, 30,
        MUIA_Boopsi_Smart,     TRUE,
        MUIA_Frame,            MUIV_Frame_None,  /* UniButton draws its own bevel */
        GA_Left,   0, GA_Top,  0, GA_Width, 0, GA_Height, 0,
        GA_ID,                 ID_COPY_BTN,
        ICA_TARGET,            (ULONG)notifyTargetObj,
        UBT_BevelStyle,        BVS_BUTTON,
        /* re-use unicode to rastport drawcontext */
        UBT_URPDrawContext,     URPDrawContext,
        /* so no need to recreate a full draw context like this:
         UBT_PointSize,         (ULONG)pointSize,
         UBT_AddFont,           (ULONG)fontPath,
         UBT_AddFont,           (ULONG)"NotoColorEmoji32.ttf",
        */
        GA_Text,               (ULONG)"\xF0\x9F\x93\x8B Copy",   /* 📋 Copy  */
        TAG_DONE);
    printf("copy_btn:   %08x\n", (unsigned int)copy_btn);

    /* --- paste button --- */
    paste_btn = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,     (ULONG)UNIBUTTON_GetClass(),
        MUIA_Boopsi_MinWidth,  60,
        MUIA_Boopsi_MinHeight, 30,
        MUIA_Boopsi_Smart,     TRUE,
        MUIA_Frame,            MUIV_Frame_None,
        GA_Left,   0, GA_Top,  0, GA_Width, 0, GA_Height, 0,
        GA_ID,                 ID_PASTE_BTN,
        ICA_TARGET,            (ULONG)notifyTargetObj,
        UBT_BevelStyle,        BVS_BUTTON,
        /* re-use unicode to rastport drawcontext */
        UBT_URPDrawContext,     URPDrawContext,
        /* so no need to recreate a full draw context like this:
         UBT_PointSize,         (ULONG)pointSize,
         UBT_AddFont,           (ULONG)fontPath,
         UBT_AddFont,           (ULONG)"NotoColorEmoji32.ttf",
        */

        GA_Text,               (ULONG)"\xF0\x9F\x93\x8C Paste",  /* 📌 Paste */
        TAG_DONE);
    printf("paste_btn:  %08x\n", (unsigned int)paste_btn);

    /* --- quit button (pure MUI, no BOOPSI) --- */
    quit_btn = MUI_NewObjectB(MUIC_Text,
        MUIA_Text_Contents, (ULONG)"\033c\033bQuit\033n",
        MUIA_Frame,         MUIV_Frame_Button,
        MUIA_InputMode,     MUIV_InputMode_RelVerify,
        MUIA_Background,    MUII_ButtonBack,
        TAG_DONE);
    printf("quit_btn:   %08x\n", (unsigned int)quit_btn);

    /* --- bottom bar: copy | paste | quit --- */
    hgroup = MUI_NewObjectB(MUIC_Group,
        MUIA_Group_Horiz, TRUE,
        MUIA_Group_Child, (ULONG)copy_btn,
        MUIA_Group_Child, (ULONG)paste_btn,
        MUIA_Group_Child, (ULONG)quit_btn,
        TAG_DONE);
    printf("hgroup:     %08x\n", (unsigned int)hgroup);

    /* --- outer vertical layout: editor on top, toolbar at bottom --- */
    vgroup = MUI_NewObjectB(MUIC_Group,
        MUIA_Group_Child, (ULONG)editor_obj,
        MUIA_Group_Child, (ULONG)hgroup,
        TAG_DONE);
    printf("vgroup:     %08x\n", (unsigned int)vgroup);

    win_obj = MUI_NewObjectB(MUIC_Window,
        MUIA_Window_Title,      (ULONG)"MUI UniTextEditor + UniButton test",
        MUIA_Window_ID,         MAKE_ID('W','O','O','T'),
        MUIA_Window_RootObject, (ULONG)vgroup,
        TAG_DONE);
    printf("win_obj:    %08x\n", (unsigned int)win_obj);

    app_obj = MUI_NewObjectB(MUIC_Application,
        MUIA_Application_Title,       (ULONG)"TestMUIUTED",
        MUIA_Application_Version,     (ULONG)"$VER: testmuiuted 1.0 (2026)",
        MUIA_Application_Description, (ULONG)"MUI UniTextEditor + UniButton BOOPSI test",
        MUIA_Application_Base,        (ULONG)"TESTMUIUTED",
        MUIA_Application_Window,      (ULONG)win_obj,
        TAG_DONE);
    printf("app_obj:    %08x\n", (unsigned int)app_obj);

    if (!editor_obj || !copy_btn || !paste_btn || !quit_btn ||
        !hgroup || !vgroup || !win_obj || !app_obj) {
        puts("ERROR: cannot create MUI object hierarchy");
        exitCode = 1; goto cleanup;
    }

    /* --- MUI notifications for close / quit --- */
    DoMethod(win_obj,  MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             app_obj, 2, MUIM_Application_ReturnID, ID_QUIT);
    DoMethod(quit_btn, MUIM_Notify, MUIA_Pressed, FALSE,
             app_obj, 2, MUIM_Application_ReturnID, ID_QUIT);

    /* --- open window, give editor initial keyboard focus --- */
    SetAttrs(win_obj,
        MUIA_Window_Open,         TRUE,
        MUIA_Window_ActiveObject, (ULONG)editor_obj,
        TAG_DONE);

    /* --- event loop --- */
    {
        ULONG sigs    = 0;
        BOOL  running = TRUE;

        while (running) {
            ULONG r = DoMethod(app_obj, MUIM_Application_NewInput, &sigs);
            switch (r) {
            case ID_QUIT:
            case MUIV_Application_ReturnID_Quit:
                running = FALSE;
                break;
            }

            if (running && sigs) {
                sigs = Wait(sigs | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);

                if (sigs & SIGBREAKF_CTRL_C)
                    running = FALSE;

                if (sigs & SIGBREAKF_CTRL_F) {
                    /* All BOOPSI notifications arrive here.
                     * Get the raw Intuition window + editor gadget pointers
                     * once — both are needed for RefreshGList/SetGadgetAttrs. */
                    Object       *o_uted = NULL;
                    struct Window *w     = NULL;
                    GetAttr(MUIA_Boopsi_Object, editor_obj, (ULONG *)&o_uted);
                    GetAttr(MUIA_Window_Window,  win_obj,    (ULONG *)&w);

                    /* editor content changed → redraw */
                    if (editorChanged && o_uted && w) {
                        editorChanged = FALSE;
                        RefreshGList((struct Gadget *)o_uted, w, NULL, 1);
                    }

                    /* copy button → delegate to UTED_ApplyCopy.
                     * The gadget reads the selection and writes UTF-8 to the
                     * system clipboard (clipboard.device) internally. */
                    if (btnCopyPressed && o_uted && w) {
                        btnCopyPressed = FALSE;
                        SetGadgetAttrs((struct Gadget *)o_uted, w, NULL,
                            UTED_ApplyCopy, TRUE,
                            TAG_DONE);
                        puts("copy: done");
                    }

                    /* paste button → delegate to UTED_ApplyPaste.
                     * The gadget reads the clipboard and inserts at cursor,
                     * then redraws itself (redraw=TRUE inside OM_SET).
                     * No manual RefreshGList needed. */
                    if (btnPastePressed && o_uted && w) {
                        btnPastePressed = FALSE;
                        SetGadgetAttrs((struct Gadget *)o_uted, w, NULL,
                            UTED_ApplyPaste, TRUE,
                            TAG_DONE);
                        SetAttrs(win_obj, MUIA_Window_ActiveObject,
                                 (ULONG)editor_obj, TAG_DONE);
                        puts("paste: done");
                    }
                }
            }
        }
    }

cleanup:
    closeNotifyTarget();

    if (app_obj)
        MUI_DisposeObject(app_obj);  /* cascades through all children */
    else {
        /* partial failure: dispose whatever was created without a parent yet */
        if (win_obj)    MUI_DisposeObject(win_obj);
        else if (vgroup) MUI_DisposeObject(vgroup);
        else {
            if (hgroup) MUI_DisposeObject(hgroup); /* owns copy/paste/quit */
            else {
                if (copy_btn)  MUI_DisposeObject(copy_btn);
                if (paste_btn) MUI_DisposeObject(paste_btn);
                if (quit_btn)  MUI_DisposeObject(quit_btn);
            }
            if (editor_obj) MUI_DisposeObject(editor_obj);
        }
    }

    if (MUIMasterBase)     CloseLibrary(MUIMasterBase);
    if (UniButtonBase)     CloseLibrary(UniButtonBase);
    if (UniTextEditorBase) CloseLibrary(UniTextEditorBase);
    if (IntuitionBase)     CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)           CloseLibrary((struct Library *)GfxBase);
    if (UtilityBase)       CloseLibrary(UtilityBase);

    return exitCode;
}
