#ifndef EGFONTSVIEW_H
#define EGFONTSVIEW_H

/*
 * EgFontsView.h - Font Settings window for EmojiGear.
 *
 * Layout:
 *   Options group (horizontal):
 *     [ ] Antialias when possible    [ ] Emoji Quality scaling
 *   Fonts group (vertical):
 *     Primary Font:    [path display........] [...]
 *     Fallback Font 1: [path display........] [...]
 *     Fallback Font 2: [path display........] [...]
 *     Emoji Font:      [path display........] [...]
 *   Presets group (horizontal):
 *     [Low-end 2 colors]  [High Quality]  [High Monospace]
 *
 * File requesters filter for .ttf and .odt files only.
 * Uses getfile.gadget (GetFileBase must be open).
 * C89 compatible.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

typedef struct EgFontsView
{
    Object        *windowObj;        /* BOOPSI window object (persistent) */
    struct Window *window;           /* Intuition window (NULL when hidden) */

    Object        *mainLayout;

    /* Options group */
    Object        *antialiasCheck;
    Object        *emojiQualityCheck;

    /* Font path getfile gadgets */
    Object        *primaryFontGF;
    Object        *fallback1FontGF;
    Object        *fallback2FontGF;
    Object        *emojiFontGF;

    /* Preset buttons */
    Object        *presetLowBtn;
    Object        *presetHQBtn;
    Object        *presetMonoBtn;

} EgFontsView;

/* Gadget IDs */
#define GAD_FONTS_ANTIALIAS        200
#define GAD_FONTS_EMOJIQUALITY     201
#define GAD_FONTS_PRIMARY_FONT     202
#define GAD_FONTS_FALLBACK1_FONT   203
#define GAD_FONTS_FALLBACK2_FONT   204
#define GAD_FONTS_EMOJI_FONT       205
#define GAD_FONTS_PRESET_LOW       206
#define GAD_FONTS_PRESET_HQ        207
#define GAD_FONTS_PRESET_MONO      208
#define GAD_FONTS_PRIMARY_CLEAR    209
#define GAD_FONTS_FALLBACK1_CLEAR  210
#define GAD_FONTS_FALLBACK2_CLEAR  211
#define GAD_FONTS_EMOJI_CLEAR      212

BOOL  EgFontsView_Init(EgFontsView *pfv, const char *title);
void  EgFontsView_Open(EgFontsView *pfv);
void  EgFontsView_Close(EgFontsView *pfv);
BOOL  EgFontsView_HandleInput(EgFontsView *pfv);
ULONG EgFontsView_GetSignalMask(EgFontsView *pfv);
void  EgFontsView_Dispose(EgFontsView *pfv);

#endif /* EGFONTSVIEW_H */
