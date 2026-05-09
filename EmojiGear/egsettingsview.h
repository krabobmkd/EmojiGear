#ifndef EgSettingsView_H
#define EgSettingsView_H

/*
 * EgSettingsView.h - Settings window for Petmate Amiga.
 *
 * Contains one group: "Fullscreen Display Mode":
 *   - Checkbox "Use Workbench screen mode"
 *   - Read-only display of the current screen ModeId (hex)
 *   - "Choose..." button that opens an ASL screen mode requester
 *   - Read-only display of the mode description from graphics.library
 *
 * When closed via the close gadget, the window is hidden (not destroyed).
 * Call EgSettingsView_GetModeId / EgSettingsView_GetUseWorkbench to read
 * back current values before or after closing.
 *
 * C89 compatible.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

typedef struct EgSettingsView
{
    Object         *windowObj;           /* BOOPSI window object (persistent) */
    struct Window  *window;              /* Intuition window (NULL when hidden) */

    Object         *mainLayout;          /* top-level layout                   */

    Object         *editorLayout;        /* "Editor display" group              */
    Object         *bgColorPalette;      /* palette gadget: background color    */
    Object         *penColorPalette;     /* palette gadget: pen/text color      */
    Object         *tabSpacesInteger;    /* integer gadget: tab width (2..12)   */
    Object         *visualizeTabsCheck;  /* checkbox: draw tab markers          */
    Object         *tabsAreSpacesCheck;  /* checkbox: Tab key inserts spaces    */

} EgSettingsView;

/* Gadget IDs for this window */
#define GAD_SETTINGS_USEWB          100
#define GAD_SETTINGS_CHOOSEMODE     101
#define GAD_SETTINGS_USEONECLORBG   102
#define GAD_SETTINGS_CHOOSEBGIMAGE  103
#define GAD_SETTINGS_REMOVEBGIMAGE  104
#define GAD_SETTINGS_EDITORBGCOLOR  105
#define GAD_SETTINGS_EDITORPENCOLOR 106
#define GAD_SETTINGS_TABSPACES      107
#define GAD_SETTINGS_VISUALIZETABS  108
#define GAD_SETTINGS_TABSARESPACES  109

/*
 * Initialize the Settings window.
 * Creates the BOOPSI window object but does not open it.
 * Defaults: useWorkbench=TRUE, currentModeId=INVALID_ID (0xFFFFFFFF).
 */
BOOL  EgSettingsView_Init(EgSettingsView *psv, const char *title);

/* Open the Settings window on CurrentMainScreen. No-op if already open. */
void  EgSettingsView_Open(EgSettingsView *psv);

/* Close (hide) the Settings window. No-op if already closed. */
void  EgSettingsView_Close(EgSettingsView *psv);

/* Handle input messages from this window. Call when its signal fires. */
BOOL  EgSettingsView_HandleInput(EgSettingsView *psv);

/* Signal bit mask to OR into Wait(). Returns 0 when window is closed. */
ULONG EgSettingsView_GetSignalMask(EgSettingsView *psv);

/* Get/set the "Use Workbench mode" checkbox state. */
void  EgSettingsView_SetFSModeIdLikeWorkbench(EgSettingsView *psv, ULONG fsUseWBMode);

/* Get/set the screen ModeId. INVALID_ID (0xFFFFFFFF) = not configured. */
void  EgSettingsView_SetModeId(EgSettingsView *psv, ULONG modeId);

/* Set the "Use one color for background" checkbox state. */
void  EgSettingsView_SetUseOneColorBg(EgSettingsView *psv, int useOneColor);

/* Set the background image path display and the AppSettings value. */
void  EgSettingsView_SetBgImagePath(EgSettingsView *psv, const char *path);

/* Dispose all resources (closes window first if open). */
void  EgSettingsView_Dispose(EgSettingsView *psv);

#endif /* EgSettingsView_H */
