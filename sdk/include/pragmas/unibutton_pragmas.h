/* Automatically generated header (sfdc 1.11f)! Do not edit! */
#ifndef PRAGMAS_UNIBUTTON_PRAGMAS_H
#define PRAGMAS_UNIBUTTON_PRAGMAS_H

/*
**   $VER: unibutton_pragmas.h $VER: unibutton_lib.sfd 1.0 $VER: unibutton_lib.sfd 1.0
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
 #pragma libcall UniButtonBase UNIBUTTON_GetClass 1e 00
#endif /* __CLIB_PRAGMA_LIBCALL */
#ifdef __CLIB_PRAGMA_AMICALL
 #pragma amicall(UniButtonBase, 0x1e, UNIBUTTON_GetClass())
#endif /* __CLIB_PRAGMA_AMICALL */

#endif /* PRAGMAS_UNIBUTTON_PRAGMAS_H */
