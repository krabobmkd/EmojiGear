/* Automatically generated header (sfdc 1.11f)! Do not edit! */
#ifndef PRAGMAS_UTF8RASTPORT_PRAGMAS_H
#define PRAGMAS_UTF8RASTPORT_PRAGMAS_H

/*
**   $VER: utf8rastport_pragmas.h $VER: utf8rastport_lib.sfd 1.0 (06.05.2026) $VER: utf8rastport_lib.sfd 1.0 (06.05.2026)
**
**   Direct ROM interface (pragma) definitions.
**
**   Copyright (c) 2001 Amiga, Inc.
**       All Rights Reserved
*/

#if defined(LATTICE) || defined(__SASC) || defined(_DCC)
#ifndef __CLIB_PRAGMA_LIBCALL
#define __CLIB_PRAGMA_LIBCALL
#endif /* __CLIB_PRAGMA_LIBCALL */
#else /* __MAXON__, __STORM__ or AZTEC_C */
#ifndef __CLIB_PRAGMA_AMICALL
#define __CLIB_PRAGMA_AMICALL
#endif /* __CLIB_PRAGMA_AMICALL */
#endif /* */

#if defined(__SASC_60) || defined(__STORM__)
#ifndef __CLIB_PRAGMA_TAGCALL
#define __CLIB_PRAGMA_TAGCALL
#endif /* __CLIB_PRAGMA_TAGCALL */
#endif /* __MAXON__, __STORM__ or AZTEC_C */

#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_Create 1e 801
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x1e, URPDC_Create(a0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_Retain 24 801
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x24, URPDC_Retain(a0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_Release 2a 801
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x2a, URPDC_Release(a0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_AddFont 30 109804
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x30, URPDC_AddFont(a0,a1,d0,d1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_RemoveFont 36 09803
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x36, URPDC_RemoveFont(a0,a1,d0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_FlushFonts 3c 801
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x3c, URPDC_FlushFonts(a0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_ChangeFontsSize 42 10803
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x42, URPDC_ChangeFontsSize(a0,d0,d1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_SetAttribsA 48 9802
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x48, URPDC_SetAttribsA(a0,a1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_SetPreferenceFlags 4e 0802
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x4e, URPDC_SetPreferenceFlags(a0,d0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_GetPreferenceFlags 54 801
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x54, URPDC_GetPreferenceFlags(a0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_FlushGlyphCache 5a 801
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x5a, URPDC_FlushGlyphCache(a0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_SetStyle 60 0802
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x60, URPDC_SetStyle(a0,d0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_SetDrawColor 66 10803
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x66, URPDC_SetDrawColor(a0,d0,d1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_SetDrawColorFromPen 6c 109804
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x6c, URPDC_SetDrawColorFromPen(a0,a1,d0,d1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_GetFontLineMetrics 72 9802
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x72, URPDC_GetFontLineMetrics(a0,a1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_TextSizeUTF8 78 a09804
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x78, URPDC_TextSizeUTF8(a0,a1,d0,a2))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_HorizontalOffsetArrayUTF8 7e a09804
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x7e, URPDC_HorizontalOffsetArrayUTF8(a0,a1,d0,a2))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDrawTextUTF8 84 0ba9805
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x84, URPDrawTextUTF8(a0,a1,a2,a3,d0))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_SetDrawScreen 8a 9802
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x8a, URPDC_SetDrawScreen(a0,a1))
#endif /* __CLIB_PRAGMA_AMICALL */
#ifdef __CLIB_PRAGMA_LIBCALL
 #pragma libcall URPBase URPDC_UpdateColorMap 90 9802
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(URPBase, 0x90, URPDC_UpdateColorMap(a0,a1))
#endif /* __CLIB_PRAGMA_AMICALL */

#endif /* PRAGMAS_UTF8RASTPORT_PRAGMAS_H */
