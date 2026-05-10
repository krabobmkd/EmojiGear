/* Automatically generated header (sfdc 1.11f)! Do not edit! */

#ifndef CLIB_UTF8RASTPORT_PROTOS_H
#define CLIB_UTF8RASTPORT_PROTOS_H

/*
**   $VER: utf8rastport_protos.h $VER: utf8rastport_lib.sfd 1.0 (06.05.2026) $VER: utf8rastport_lib.sfd 1.0 (06.05.2026)
**
**   C prototypes. For use with 32 bit integers only.
**
**   Copyright (c) 2001 Amiga, Inc.
**       All Rights Reserved
*/

#include <exec/libraries.h>
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <utility/tagitem.h>
#include <intuition/screens.h>
#include <libraries/utf8rastport.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* "utf8rastport.library" */
/*
------ DrawContext lifecycle ------
*/

struct URPDrawContext * URPDC_Create(CONST STRPTR name);
VOID URPDC_Retain(struct URPDrawContext * dc);
VOID URPDC_Release(struct URPDrawContext * dc);

/*------ Font management ------*/

LONG URPDC_AddFont(struct URPDrawContext * dc, CONST STRPTR fontPath, LONG pointSize, ULONG flags);
VOID URPDC_RemoveFont(struct URPDrawContext * dc, CONST STRPTR fontPath, LONG pointSize);
VOID URPDC_FlushFonts(struct URPDrawContext * dc);
VOID URPDC_ChangeFontsSize(struct URPDrawContext * dc, ULONG nPointSize, ULONG fontMask);

/*------ DrawContext attributes ------*/

VOID URPDC_SetAttribsA(struct URPDrawContext * dc, ULONG * taglist);

/*------ Preferences and style ------*/

VOID URPDC_SetPreferenceFlags(struct URPDrawContext * dc, ULONG flags);
ULONG URPDC_GetPreferenceFlags(CONST struct URPDrawContext * dc);
VOID URPDC_FlushGlyphCache(struct URPDrawContext * dc);
VOID URPDC_SetStyle(struct URPDrawContext * dc, ULONG styleBits);

/*------ Colour ------*/

VOID URPDC_SetDrawColor(struct URPDrawContext * dc, ULONG textRGB, ULONG backgroundRGB);
VOID URPDC_SetDrawColorFromPen(struct URPDrawContext * dc, struct Screen * screen, LONG txtPen, LONG bgPen);

/*------ Measurement ------*/

VOID URPDC_GetFontLineMetrics(struct URPDrawContext * dc, struct URPTextMetric * out);
VOID URPDC_TextSizeUTF8(struct URPDrawContext * dc, CONST STRPTR utf8, LONG maxChars, struct URPTextMetric * out);
VOID URPDC_HorizontalOffsetArrayUTF8(struct URPDrawContext * dc, CONST STRPTR utf8, LONG maxChars, LONG * arrayout);

/*------ Drawing ------*/

VOID URPDrawTextUTF8(struct RastPort * rp, struct URPDrawContext * dc, struct URPTextPos * pos, CONST STRPTR utf8, ULONG maxChars);

/*------ CLUT / palette support ------*/

VOID URPDC_SetDrawScreen(struct URPDrawContext * dc, struct Screen * screen);
VOID URPDC_UpdateColorMap(struct URPDrawContext * dc, struct Screen * screen);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CLIB_UTF8RASTPORT_PROTOS_H */
