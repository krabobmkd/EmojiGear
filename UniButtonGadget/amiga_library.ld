/*
 * Linker script for AmigaOS m68k shared library (.gadget / .library).
 *
 * The romtag scanner expects the RTC_MATCHWORD ($4AFC) to be findable in the
 * loaded image, and the Exec library loader calls whatever is at offset 0
 * (which must return -1 to reject direct execution).  Both constraints require
 * the __libinit section from libinit.o to appear at the very start of the
 * output before any other code.
 *
 * Usage (CMakeLists.txt):
 *   target_link_options(... PRIVATE -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/amiga_library.ld)
 */

SECTIONS
{
    /* Start at 0; the AmigaOS hunk loader relocates at load time. */
    . = 0;

    .text :
    {
        /* __libinit section from libinit.s must be first:
         * moveq #-1,d0 / rts  +  RomTag struct  +  LibFuncTable. */
        *(__libinit)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(4);
    .data :
    {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(4);
    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
