/* Automatically generated header (sfdc 1.11d)! Do not edit! */

#ifndef _INLINE_UTF8RASTPORT_H
#define _INLINE_UTF8RASTPORT_H

#ifndef _SFDC_VARARG_DEFINED
#define _SFDC_VARARG_DEFINED
#ifdef __HAVE_IPTR_ATTR__
typedef APTR _sfdc_vararg __attribute__((iptr));
#else
typedef ULONG _sfdc_vararg;
#endif /* __HAVE_IPTR_ATTR__ */
#endif /* _SFDC_VARARG_DEFINED */

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif /* !__INLINE_MACROS_H */

#ifndef UTF8RASTPORT_BASE_NAME
#define UTF8RASTPORT_BASE_NAME URPBase
#endif /* !UTF8RASTPORT_BASE_NAME */

#define URPDC_Create(___name) \
      LP1(0x1e, struct URPDrawContext *, URPDC_Create , CONST STRPTR, ___name, a0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_Retain(___dc) \
      LP1NR(0x24, URPDC_Retain , struct URPDrawContext *, ___dc, a0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_Release(___dc) \
      LP1NR(0x2a, URPDC_Release , struct URPDrawContext *, ___dc, a0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_AddFont(___dc, ___fontPath, ___pointSize, ___flags) \
      LP4(0x30, LONG, URPDC_AddFont , struct URPDrawContext *, ___dc, a0, CONST STRPTR, ___fontPath, a1, LONG, ___pointSize, d0, ULONG, ___flags, d1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_RemoveFont(___dc, ___fontPath, ___pointSize) \
      LP3NR(0x36, URPDC_RemoveFont , struct URPDrawContext *, ___dc, a0, CONST STRPTR, ___fontPath, a1, LONG, ___pointSize, d0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_FlushFonts(___dc) \
      LP1NR(0x3c, URPDC_FlushFonts , struct URPDrawContext *, ___dc, a0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_ChangeFontsSize(___dc, ___nPointSize, ___fontMask) \
      LP3NR(0x42, URPDC_ChangeFontsSize , struct URPDrawContext *, ___dc, a0, ULONG, ___nPointSize, d0, ULONG, ___fontMask, d1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_SetAttribsA(___dc, ___taglist) \
      LP2NR(0x48, URPDC_SetAttribsA , struct URPDrawContext *, ___dc, a0, ULONG *, ___taglist, a1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_SetPreferenceFlags(___dc, ___flags) \
      LP2NR(0x4e, URPDC_SetPreferenceFlags , struct URPDrawContext *, ___dc, a0, ULONG, ___flags, d0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_GetPreferenceFlags(___dc) \
      LP1(0x54, ULONG, URPDC_GetPreferenceFlags , CONST struct URPDrawContext *, ___dc, a0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_FlushGlyphCache(___dc) \
      LP1NR(0x5a, URPDC_FlushGlyphCache , struct URPDrawContext *, ___dc, a0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_SetStyle(___dc, ___styleBits) \
      LP2NR(0x60, URPDC_SetStyle , struct URPDrawContext *, ___dc, a0, ULONG, ___styleBits, d0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_SetDrawColor(___dc, ___textRGB, ___backgroundRGB) \
      LP3NR(0x66, URPDC_SetDrawColor , struct URPDrawContext *, ___dc, a0, ULONG, ___textRGB, d0, ULONG, ___backgroundRGB, d1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_SetDrawColorFromPen(___dc, ___screen, ___txtPen, ___bgPen) \
      LP4NR(0x6c, URPDC_SetDrawColorFromPen , struct URPDrawContext *, ___dc, a0, struct Screen *, ___screen, a1, LONG, ___txtPen, d0, LONG, ___bgPen, d1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_GetFontLineMetrics(___dc, ___out) \
      LP2NR(0x72, URPDC_GetFontLineMetrics , struct URPDrawContext *, ___dc, a0, struct URPTextMetric *, ___out, a1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_TextSizeUTF8(___dc, ___utf8, ___maxChars, ___out) \
      LP4NR(0x78, URPDC_TextSizeUTF8 , struct URPDrawContext *, ___dc, a0, CONST STRPTR, ___utf8, a1, LONG, ___maxChars, d0, struct URPTextMetric *, ___out, a2,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_HorizontalOffsetArrayUTF8(___dc, ___utf8, ___maxChars, ___arrayout) \
      LP4NR(0x7e, URPDC_HorizontalOffsetArrayUTF8 , struct URPDrawContext *, ___dc, a0, CONST STRPTR, ___utf8, a1, LONG, ___maxChars, d0, LONG *, ___arrayout, a2,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDrawTextUTF8(___rp, ___dc, ___pos, ___utf8, ___maxChars) \
      LP5NR(0x84, URPDrawTextUTF8 , struct RastPort *, ___rp, a0, struct URPDrawContext *, ___dc, a1, struct URPTextPos *, ___pos, a2, CONST STRPTR, ___utf8, a3, ULONG, ___maxChars, d0,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_SetDrawScreen(___dc, ___screen) \
      LP2NR(0x8a, URPDC_SetDrawScreen , struct URPDrawContext *, ___dc, a0, struct Screen *, ___screen, a1,\
      , UTF8RASTPORT_BASE_NAME)

#define URPDC_UpdateColorMap(___dc, ___screen) \
      LP2NR(0x90, URPDC_UpdateColorMap , struct URPDrawContext *, ___dc, a0, struct Screen *, ___screen, a1,\
      , UTF8RASTPORT_BASE_NAME)

#endif /* !_INLINE_UTF8RASTPORT_H */
