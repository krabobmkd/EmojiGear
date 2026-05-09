#ifndef OFFSCREENBM_H
#define OFFSCREENBM_H
/*
 * Amiga offscreen BitMap+Layer+RastPort 
 *
 * Here is the correct way to init and close BitMap+rastport+Layer,
 * that graphics drawing calls will carefully border clip.  
 */
 
#ifdef __cplusplus
extern "C" {
#endif
 
#include <graphics/gfx.h>
#include <graphics/layers.h>

typedef struct sOffscreenBitMap {
	   // -- temp raster for less glitch when drawings
    struct BitMap *_bm; //< direct BitMap access, but most likely you use _rp.
    struct RastPort *_rp; //< what is used as entry by graphics drawing functions that manages clipping.
    struct Layer_Info *_layerinfo; //< likely private but could be used.
    struct Layer *_layer; //< likely private but could be used.
} OffscreenBitMap; 

/**
 * Ctreate a BitMap with a Clipping Layer and restport 
 *  if friendBitmapForMode not null, will mimic BitMap mode, will possibly create a RTG BitMap, or classic planar,
 *  depth is not used in that case. if friendBitmapForMode is NULL, you must provide depth>0.
 *  bmFlags same as AllocBitMap() flags: BMF_CLEAR
 */
void OffscreenBitMap_Init(OffscreenBitMap *ofsbm, 
					int pixelWidth, int pixelHeight ,
					int depth,int bmFlags,
					struct BitMap *friendBitmapForMode );

void OffscreenBitMap_Close(OffscreenBitMap *ofsbm);

#ifdef __cplusplus
}
#endif
#endif /* OFFSCREENBM_H */
