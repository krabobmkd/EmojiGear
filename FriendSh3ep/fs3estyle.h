#ifndef FS3ESTYLE_H
#define FS3ESTYLE_H

/*
 * fs3estyle — FriendSh3ep color theme system.
 *
 * Each color role is defined by its RGB value (the design intent) and
 * tracked at runtime as an Amiga pen index.  On indexed screens (ECS/AGA)
 * ObtainBestPenA reserves a palette entry; on RTG screens with many colors
 * it usually succeeds immediately.  If allocation fails, FindColor returns
 * the closest existing entry.
 *
 * Usage
 * -----
 *   FS3EStyle_InitDefaults(&fs3eStyle);          // set RGB values + pen=1
 *   FS3EStyle_ApplyColors(&fs3eStyle, screen);   // obtain pens from ColorMap
 *   ... draw using FS3E_PEN(&fs3eStyle, FS3E_COLOR_TEXT) ...
 *   FS3EStyle_ApplyColors(&fs3eStyle, screen);   // theme change: re-applies
 *   FS3EStyle_ReleasePens(&fs3eStyle);           // at exit or screen close
 *
 * Color values are 0x00RRGGBB (alpha byte ignored).
 */

#include <exec/types.h>
#include <intuition/screens.h>

/* ------------------------------------------------------------------ */
/* FS3EManagedColor — one color role: RGB definition + runtime pen     */
/* ------------------------------------------------------------------ */

typedef struct {
    ULONG rgbcolor;   /* 0x00RRGGBB — theme definition value */
    WORD  pen;        /* pen index for SetAPen/SetRPAttrs; 1 until ApplyColors */
    WORD  allocated;  /* 1 = obtained via ObtainBestPenA; 0 = FindColor result */
} FS3EManagedColor;

/* ------------------------------------------------------------------ */
/* Color role enumeration                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    FS3E_COLOR_BUTTON_BG = 0,       /* normal button / nav-bar button background */
    FS3E_COLOR_BUTTON_SELECTED_BG,  /* pressed / active button background */
    FS3E_COLOR_TIMELINE_BG,         /* TootTimeline empty background */
    FS3E_COLOR_USERNAME,            /* display name of the poster */
    FS3E_COLOR_HASHTAG,             /* hashtag highlight in post body */
    FS3E_COLOR_ACCENT,              /* separators, borders, avatar placeholder, resize grip */
    FS3E_COLOR_TEXT,                /* main body text */
    FS3E_COLOR_TEXT_DIM,            /* secondary text: @acct, timestamps */
    FS3E_COLOR_COUNT                /* must be last */
} FS3EColorRole;

/* ------------------------------------------------------------------ */
/* FS3EStyle — the complete runtime color theme                        */
/* ------------------------------------------------------------------ */

typedef struct {
    FS3EManagedColor colors[FS3E_COLOR_COUNT];
    struct Screen   *screen;  /* screen whose ColorMap currently owns the pens;
                               * NULL means no pens are allocated */
} FS3EStyle;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/* Set all rgbcolor fields to the built-in dark theme defaults.
 * Does not touch pens — safe to call before any screen is available. */
void FS3EStyle_InitDefaults(FS3EStyle *st);

/* Obtain Amiga pens for every color role from scr->ViewPort.ColorMap.
 * Releases any previously held pens first, so safe to call on theme change
 * or when switching screens. */
void FS3EStyle_ApplyColors(FS3EStyle *st, struct Screen *scr);

/* Release all ObtainBestPenA-allocated pens back to the ColorMap.
 * Call before closing the screen or at program exit. */
void FS3EStyle_ReleasePens(FS3EStyle *st);

/* Convenience accessor: pen index for a given role */
#define FS3E_PEN(st, role)  ((st)->colors[(role)].pen)

#endif /* FS3ESTYLE_H */
