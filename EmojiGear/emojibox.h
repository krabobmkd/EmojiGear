#ifndef EMOJIBOX_H
#define EMOJIBOX_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

/* Forward declarations to avoid circular include with emojigear.h */
struct App;
struct URPDrawContext;

/* Raw key codes for F1-F10 */
#define EMOJIBOX_RAWKEY_F1   0x50
#define EMOJIBOX_RAWKEY_F8   0x57
#define EMOJIBOX_RAWKEY_F9   0x58
#define EMOJIBOX_RAWKEY_F10  0x59

/* Number of emoji sets */
#define EMOJIBOX_NUM_SETS  11

/* -------------------------------------------------------------------------
 * EmojiBoxWindow – persistent state for the emoji-table popup window.
 * Lives inside the App struct. Call EmojiBoxWindow_Init once at startup,
 * EmojiBoxWindow_Dispose once at shutdown.  Open/Close are called each
 * time the user shows or hides the window.
 * -------------------------------------------------------------------------*/
typedef struct EmojiBoxWindow {
    Object         *windowObj;                      /* BOOPSI window object  */
    struct Window  *window;                         /* Intuition window      */

    LONG            left, top, width, height;       /* remembered window geometry */

    Object         *chooser;                        /* set-selector          */
    Object         *gridGadget;                     /* emoji grid            */
    Object         *mainLayout;                     /* root layout           */

    struct List     chooserList;                    /* list of chooser nodes */
    struct Node    *chooserNodes[EMOJIBOX_NUM_SETS];

    int             currentSetIdx;                  /* 0..EMOJIBOX_NUM_SETS-1 */
    Class          *gridClass;                      /* private gadget class  */
    struct URPDrawContext *dc;                       /* shared font context   */
} EmojiBoxWindow;

/* One-time initialisation (does not open the window). */
void EmojiBoxWindow_Init(EmojiBoxWindow *ebw);

/* Free all resources (closes window first if open). */
void EmojiBoxWindow_Dispose(EmojiBoxWindow *ebw);

/* Open the window on CurrentMainScreen. No-op if already open. */
void EmojiBoxWindow_Open(EmojiBoxWindow *ebw);

/* Close (hide) the window. No-op if already closed. */
void EmojiBoxWindow_Close(EmojiBoxWindow *ebw);

/* Drain WM_HANDLEINPUT messages. Call when the window's signal fires. */
BOOL EmojiBoxWindow_HandleInput(EmojiBoxWindow *ebw);

/* Signal mask to OR into Wait(). Returns 0 when the window is closed. */
ULONG EmojiBoxWindow_GetSignalMask(EmojiBoxWindow *ebw);

/* -------------------------------------------------------------------------
 * F-key emoji insertion (original functionality, kept here)
 * -------------------------------------------------------------------------*/
BOOL EmojiBox_HandleFKey(struct App *ctx, ULONG code, ULONG qualifiers,
                         struct Window *win);

#endif /* EMOJIBOX_H */
