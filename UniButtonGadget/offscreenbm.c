#include "offscreenbm.h"

#include <proto/graphics.h>
#include <proto/layers.h>


/*
 * Amiga offscreen BitMap+Layer+RastPort
 * When using graphics.library drawing functions,
 * you can easily draw on a BitMap allocated with AllocBitMap
 * and a rastport with InitRastPort(), but it will not manage
 * rectangle clipping:
 * in that case, any pixel written out of the rectangle
 * will trash memory.
 *
 * Here is the correct way to init and close BitMap+rastport+Layer,
 * that graphics drawing calls will carefully border clip.
 */

void OffscreenBitMap_Init(OffscreenBitMap *ofsbm, 
		int pixelWidth, int pixelHeight , 
		int depth,
		int bmFlags,
		struct BitMap *friendBitmapForMode)
{
	
	if(!ofsbm) return;
    ofsbm->_layer = NULL;
    ofsbm->_layerinfo = NULL;
    ofsbm->_rp = NULL;
    ofsbm->_bm = NULL;
	// the way to create RastPort that actually clips drawing.
	if(friendBitmapForMode) depth = friendBitmapForMode->Depth;
	ofsbm->_bm = AllocBitMap(pixelWidth,pixelHeight,depth,bmFlags, friendBitmapForMode);
	if(!ofsbm->_bm) return;

	ofsbm->_layerinfo = NewLayerInfo();
	if(!ofsbm->_layerinfo) {  OffscreenBitMap_Close(ofsbm); return; }

	ofsbm->_layer = CreateUpfrontLayer(ofsbm->_layerinfo, ofsbm->_bm, 0, 0, pixelWidth - 1, pixelHeight - 1, 0, NULL);

	if(!ofsbm->_layer) {  OffscreenBitMap_Close(ofsbm); return; }
	ofsbm->_rp = ofsbm->_layer->rp;
	
}
void OffscreenBitMap_Close(OffscreenBitMap *ofsbm)
{
    if(ofsbm->_layer) DeleteLayer (0,ofsbm->_layer);
    ofsbm->_layer = NULL;
    if(ofsbm->_layerinfo) DisposeLayerInfo(ofsbm->_layerinfo);
    ofsbm->_layerinfo = NULL;
    ofsbm->_rp = NULL;
    if(ofsbm->_bm) FreeBitMap(ofsbm->_bm);
    ofsbm->_bm = NULL;	
}
