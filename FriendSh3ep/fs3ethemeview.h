#ifndef FS3ETHEMEVIEW_H
#define FS3ETHEMEVIEW_H

/*
 * fs3ethemeview.h - Font & Theme Settings window for FriendSh3ep.
 *
 * Layout:
 *   Options group (horizontal):
 *     [ ] Antialias when possible    [ ] Emoji quality scaling
 *   Fonts group (vertical):
 *     Primary font:    [path display........] [X]
 *     Fallback font 1: [path display........] [X]
 *     Fallback font 2: [path display........] [X]
 *     Emoji font:      [path display........] [X]
 *   Presets group (horizontal):
 *     [Low quality]  [High quality]  [Monospace]
 *   Theme group (vertical):
 *     Theme: [chooser▼]
 *
 * Uses getfile.gadget (GetFileBase must be open) and chooser.gadget
 * (ChooserBase must be open).
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

/* Maximum theme slots for chooser nodes (populated later). */
#define FS3ETHEMEVIEW_MAX_THEMES  32

typedef struct FS3EThemeView {
    Object        *windowObj;
    struct Window *window;

    LONG left, top, width, height;

    Object *mainLayout;

    /* Options group */
    Object *antialiasCheck;
    Object *emojiQualityCheck;

    /* Font path getfile gadgets */
    Object *primaryFontGF;
    Object *fallback1FontGF;
    Object *fallback2FontGF;
    Object *emojiFontGF;       /* UI emoji font (buttons/navbar) */
    Object *colorEmojiFontGF;  /* color emoji font (timeline)    */

    /* Preset buttons */
    Object *presetLowBtn;
    Object *presetHQBtn;
    Object *presetMonoBtn;

    /* Theme chooser */
    Object     *themeChooser;
    struct List themeList;
    struct Node *themeNodes[FS3ETHEMEVIEW_MAX_THEMES];
    int          themeCount;

} FS3EThemeView;

BOOL  FS3EThemeView_Create(FS3EThemeView *tv, const char *title);
void  FS3EThemeView_Dispose(FS3EThemeView *tv);
void  FS3EThemeView_Open(FS3EThemeView *tv);
void  FS3EThemeView_Close(FS3EThemeView *tv);
BOOL  FS3EThemeView_HandleInput(FS3EThemeView *tv);
ULONG FS3EThemeView_GetSignalMask(FS3EThemeView *tv);

#endif /* FS3ETHEMEVIEW_H */
