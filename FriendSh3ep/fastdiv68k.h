#ifndef FASTDIV68K_H
#define FASTDIV68K_H
#include <exec/types.h>
#include "compilers.h"
/*
 * fastdiv68k.h - fast 32/16 -> 16 bit integer division on 68000/68010.
 *
 * $VER: fastdiv68k.h 1.0 (13.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Local copy of /home/krabob/Code/Amiga/amigapetmate/src/fastdiv68k.h,
 * adapted for FriendSh3ep (scalepixelarraybilinear.c's fixed-point
 * per-axis step calculation -- see that file for the caller-guaranteed
 * "quotient always fits 16 bits" precondition that makes DivuW() safe
 * to use there).
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The 68000 CPU has a native signed 32-by-16 divide instruction:
 *
 *   divs.w  Dn, Dm
 *
 * which divides the 32-bit signed integer in Dm by the 16-bit signed integer
 * in Dn and stores the 16-bit quotient in the low word of Dm (remainder in
 * the high word).  This is fast (typically ~90-140 cycles on 68000, versus
 * a > 200 cycle math-library call for a 32/32 divide).
 *
 * ANSI C integer promotion rules require that both operands of "/" are
 * promoted to the same type.  Because the dividend is LONG, the divisor
 * is also promoted to LONG, and every C compiler generates a 32/32 divide.
 * The 68000 has no 32/32 divide instruction; the compiler therefore calls a
 * math-library function (e.g. __DIVSLL in amiga.lib / libm).  Starting with
 * the 68020, divs.l handles 32/32 natively so the issue disappears there.
 */

/* ------------------------------------------------------------------ */
/* 1a. GCC 2.x (e.g. GCC 2.95 native Amiga)                          */
/*     - no trailing empty clobber list                               */
/*     - read/write operand split as "=d"/"0" (operand shifts to %2) */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) && (__GNUC__ < 3)

static __inline__ int DivsW(int n, short dvs)
{
    __asm__ ("divs.w  %2,%0\n\text.l %0" : "=d"(n) : "0"(n), "d"(dvs));
    return n;
}
static __inline__ unsigned int DivuW(unsigned int n, unsigned short dvs)
{
    __asm__ ("divu.w  %2,%0\n\tswap %0\n\tclr.w %0\n\tswap %0" : "=d"(n) : "0"(n), "d"(dvs));
    return n;
}

/* ------------------------------------------------------------------ */
/* 1b. GCC 3+ / GCC 6.x "Bebbo" targeting mc68000 (not mc68020+)     */
/* ------------------------------------------------------------------ */
#elif defined(__GNUC__)

static inline int DivsW(int  n, short dvs)
{
    asm  ("divs.w  %1,%0\n\text.l %0\n" : "+d"(n) : "d"(dvs) : );
    return n;
}
static inline unsigned int DivuW(unsigned int  n,unsigned short dvs)
{
    asm  ("divu.w  %1,%0\n\tswap %0\n\tclr.w %0\n\tswap %0" : "+d"(n) : "d"(dvs) : );
    return n;
}
/* ------------------------------------------------------------------ */
/* 2. SAS/C 6.x                                                        */
/* ------------------------------------------------------------------ */
#elif  defined(__SASC)
INLINE LONG DivsW(LONG n, WORD dvs)
{
    register __d0 LONG _num = n;
    register __d1 WORD _div = dvs;
#asm
    divs.w  d1,d0
    ext.l d0
#endasm
    (void)_div;         /* suppress SAS/C "unused variable" warning */
    return (LONG)_num;
}
INLINE ULONG DivuW(ULONG n, UWORD dvs)
{
    register __d0 ULONG _num = n;
    register __d1 UWORD _div = dvs;
#asm
    divu.w  d1,d0
    swap d0
    clr.w d0
    swap d0
#endasm
    (void)_div;         /* suppress SAS/C "unused variable" warning */
    return (LONG)_num;
}
#else

INLINE LONG DivsW(LONG n, WORD d)
{
    return (LONG)(n / (LONG)d);
}
INLINE ULONG DivuW(ULONG n, UWORD d)
{
    return (LONG)(n / (LONG)d);
}
#endif /* compiler selection - DivW */


#endif /* FASTDIV68K_H */
