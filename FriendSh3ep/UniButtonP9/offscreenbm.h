#ifndef OFFSCREENBM_H
#define OFFSCREENBM_H
/*
 * Amiga offscreen BitMap+RastPort for safe offscreen drawing with clipping.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>

typedef struct sOffscreenBitMap {
    struct BitMap *_bm;
    struct RastPort _srp;
    int _w, _h;
    int _imageState;
    int _bgpen;
} OffscreenBitMap;

void OffscreenBitMap_Init(OffscreenBitMap *ofsbm,
                          int pixelWidth, int pixelHeight,
                          int depth, int bmFlags,
                          struct BitMap *friendBitmapForMode);

void OffscreenBitMap_Close(OffscreenBitMap *ofsbm);

#ifdef __cplusplus
}
#endif
#endif /* OFFSCREENBM_H */
