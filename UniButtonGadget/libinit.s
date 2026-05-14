;---------------------------------------------------------------------------

        NOLIST

        INCLUDE "exec/types.i"
        INCLUDE "exec/libraries.i"
        INCLUDE "exec/lists.i"
        INCLUDE "exec/alerts.i"
        INCLUDE "exec/initializers.i"
        INCLUDE "exec/resident.i"
        INCLUDE "libraries/dos.i"
        INCLUDE "utility/hooks.i"

        ;INCLUDE "classbase.i"

        LIST

        ; SASC has different include path for lvo (OS2?)
        ; let's just inlines the value we need here
_LVOOpenLibrary		EQU	-552
_LVOCloseLibrary	EQU	-414
_LVOFindTask		EQU	-294
_LVOAlert           EQU	-108
_LVOAllocMem	EQU	-198
_LVOFreeMem     EQU	-210
_LVORemove     EQU   -252

	INCLUDE "intuition/classes.i"

 ; from modern intuition/classes.i, also missing in SASC6.5 includes
 ; hey, this is OS3.0 stuff... why is it missing in SASC 1995 ?
 ifnd cl_Pad
  STRUCTURE ClassLibrary,0
    STRUCT	 cl_Lib,LIB_SIZE	; Embedded library
    UWORD	 cl_Pad			; Align the structure
    APTR	 cl_Class		; Class pointer
    LABEL	 ClassLibrary_SIZEOF
 endc
	; what is actually allocated as a opened library
	; ClassLib -> ClassLibrary -> Library
	; THIS MUST CORRESPOND TO C STRUCT struct ExtClassLib in class_XXXX_private.h
	; it doesn't have to evolve more than this. XXXBase are on the C side,
	; class definition is in C side also, so ... really let this like it is for classes/gadget/datatypes (all are boopsi).
	; for general use library it is another story of course.
	STRUCTURE ExtClassLib,0
		STRUCT	cb_ClassLibrary,ClassLibrary_SIZEOF
		ULONG	cb_SysBase
		;ULONG   cb_UtilityBase
		;ULONG	cb_IntuitionBase
		;ULONG	cb_GfxBase
		; actually needed for expunge
		ULONG	cb_SegList
		ULONG   cb_DOSBase
   LABEL ClassLib_SIZEOF

; important must be the same...
VERSION		EQU	2
REVISION	EQU	0

CALL	MACRO
	jsr	_LVO\1(a6)
	ENDM

;---------------------------------------------------------------------------
	XREF _UniButton_Init
	XREF _UniButton_Exit
	XREF _UNIBUTTON_GetClass

	XREF	_Lib_ID
	XREF	_VersionString
;---------------------------------------------------------------------------

	SECTION CODE

		; needed by the gcc 6.5 trick as the bin entry point.
		XDEF	__start

;---------------------------------------------------------------------------

; First executable location, must return an error to the caller
__start:
Start:
        moveq   #-1,d0
        rts

;-----------------------------------------------------------------------

RomTag:
        DC.W    RTC_MATCHWORD           ; UWORD RT_MATCHWORD
        DC.L    RomTag                  ; APTR  RT_MATCHTAG
        DC.L    EndCode                 ; APTR  RT_ENDSKIP
        DC.B    RTF_AUTOINIT            ; UBYTE RT_FLAGS
        DC.B    VERSION                 ; UBYTE RT_VERSION
        DC.B    NT_LIBRARY              ; UBYTE RT_TYPE
        DC.B    0                       ; BYTE  RT_PRI
        DC.L    _Lib_ID    ; APTR  RT_NAME   libname is same as classid (myclass.class or myclass.gadget)
        DC.L    _VersionString ; APTR  RT_IDSTRING  version string
        DC.L    LibInitTable            ; APTR  RT_INIT

        CNOP    0,4

LibInitTable:
        DC.L    ClassLib_SIZEOF
        DC.L    LibFuncTable
        DC.L    0   ; optional datatable "initializes static data structures" exec/InitStruct "exec/initializers.i"
        DC.L    LibInit

; note: apparently an historic .w relative pmointers mode are supported
; looks like: (list of .l pointers... -1.w (list of .w relative pointers) -1.w) funnyyyyyy
LibFuncTable:
	DC.L	LibOpen
	DC.L	LibClose
	DC.L	LibExpunge
	DC.L	0 ; LibReserved
	DC.L	_UNIBUTTON_GetClass		; 1st public function, for boospi classes, just return class pointer used by NewObject().
	

	DC.L	-1 ; end marker

;-----------------------------------------------------------------------
	XDEF _SysBase
_SysBase:
SysBase:	DC.L 0

; Library Init entry point called when library is first loaded in memory
; On entry, D0 points to library base, A0 has lib seglist, A6 has SysBase
; Returns 0 for failure or the library base for success.
LibInit:
        movem.l a0/a5/d7,-(sp)
        move.l  d0,a5
        move.l  a6,cb_SysBase(a5)
        move.l  a6,SysBase
        move.l  a0,cb_SegList(a5)

    move.w	#VERSION,LIB_VERSION(a5)
    move.w	#REVISION,LIB_REVISION(a5)
    ;test
    clr.w	LIB_OPENCNT(a5)


        move.l  a5,d0
        movem.l (sp)+,a0/a5/d7
        rts


FailInit:
        ;bsr     CloseLibs
        or.l    #AG_OpenLib,d7
        CALL	Alert
        movem.l (sp)+,a0/a5/d7
        moveq   #0,d0
        rts

;-----------------------------------------------------------------------

; Library open entry point called every OpenLibrary()
; On entry, A6 has ClassBase, task switching is disabled
; Returns 0 for failure, or ClassBase for success.
LibOpen:

    ; - - - -  - -
	tst.w	LIB_OPENCNT(a6)
	bne.s	jp2  ; second and following openings jump to succes case.

;krb says: original example was using bsr to reach c funcs,
; but only jsr will make it to other sections with rellocation.
; + sasc call extern C function @function when other compilers (gcc) wants _function
	jsr	_UniButton_Init

	tst.l	d0
	beq.s	jp2  ; success is zero !!!
	; error case
	moveq	#0,d0
	rts
jp2
        addq.w  #1,LIB_OPENCNT(a6)
        bclr    #LIBB_DELEXP,LIB_FLAGS(a6)
        move.l  a6,d0
        rts

;-----------------------------------------------------------------------

; Library close entry point called every CloseLibrary()
; On entry, A6 has ClassBase, task switching is disabled
; Returns 0 normally, or the library seglist when lib should be expunged
;   due to delayed expunge bit being set
LibClose:
	subq.w	#1,LIB_OPENCNT(a6)
;	bne.s	jp3			; if openers, don't remove class
	; zero openers, so try to remove class
	;no need, done at CloseLibrary()  jsr	_CLibClose
;jp3:
	; if delayed expunge bit set, then try to get rid of the library
	btst	#LIBB_DELEXP,LIB_FLAGS(a6)
	bne.s	CloseExpunge

	; delayed expunge not set, so stick around
	moveq	#0,d0
	rts

CloseExpunge:
	; if no library users, then just remove the library
	tst.w	LIB_OPENCNT(a6)
	beq.s	DoExpunge

	; still some library users, so forget about flushing
	bclr	#LIBB_DELEXP,LIB_FLAGS(a6)
	moveq	#0,d0
	rts

;-----------------------------------------------------------------------

; Library expunge entry point called whenever system memory is lacking
; On entry, A6 has ClassBase, task switching is disabled
; Returns the library seglist if the library open count is 0, returns 0
; otherwise and sets the delayed expunge bit.
LibExpunge:
        tst.w   LIB_OPENCNT(a6)
        beq.s   DoExpunge

        tst.l   cl_Class(a6)
        beq.s   DoExpunge

        bset    #LIBB_DELEXP,LIB_FLAGS(a6)
        moveq   #0,d0
        rts

DoExpunge:
        movem.l d2/a5/a6,-(sp)

		; actually delete the class at expunge .
		jsr		_UniButton_Exit

		move.l a6,a5
		move.l cb_SysBase(a5),a6
		move.l cb_SegList(a5),d2
		move.l a5,a1
		CALL	Remove
    ; Close dependent libraries here
		move.l a5,a1
		moveq   #0,d0
		move.w LIB_NEGSIZE(a5),d0
		sub.l d0,a1
		add.w LIB_POSSIZE(a5),d0
		CALL FreeMem
		; Want the seglist at exit ?
		move.l d2,d0

        movem.l (sp)+,d2/a5/a6
        rts

;-----------------------------------------------------------------------

   ; EndCode is a marker that show the end of your code.  Make sure it does not span
   ; sections nor is before the rom tag in memory!  It is ok to put it right after the ROM
   ; tag--that way you are always safe.  I put it here because it happens to be the "right"
   ; thing to do, and I know that it is safe in this case.
EndCode:
    END
