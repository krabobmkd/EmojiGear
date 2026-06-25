#include "offscreenbm.h"

#include <proto/graphics.h>
#include <proto/layers.h>

void OffscreenBitMap_Init(OffscreenBitMap *ofsbm,
                          int pixelWidth, int pixelHeight,
                          int depth, int bmFlags,
                          struct BitMap *friendBitmapForMode)
{
    if (!ofsbm) return;
    ofsbm->_bm = NULL;
    if (friendBitmapForMode) depth = friendBitmapForMode->Depth;
    ofsbm->_bm = AllocBitMap(pixelWidth, pixelHeight, depth, bmFlags,
                              friendBitmapForMode);
    if (!ofsbm->_bm) return;
    InitRastPort(&ofsbm->_srp);
    ofsbm->_srp.BitMap = ofsbm->_bm;
    ofsbm->_w = pixelWidth;
    ofsbm->_h = pixelHeight;
}

void OffscreenBitMap_Close(OffscreenBitMap *ofsbm)
{
    if (ofsbm->_bm) {
        WaitBlit();
        FreeBitMap(ofsbm->_bm);
    }
    ofsbm->_bm = NULL;
    ofsbm->_w = ofsbm->_h = 0;
}
