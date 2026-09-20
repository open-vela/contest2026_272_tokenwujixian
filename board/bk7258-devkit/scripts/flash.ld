/****************************************************************************
 * board/bk7258-devkit/scripts/flash.ld
 *
 * Secure CP linker script for the SHA-256-locked L0 replacement profile.
 * Flash is CPU-visible XIP, not the physical CRC-expanded package offset.
 ****************************************************************************/

#include <nuttx/config.h>

/* The RAM window is taken from Kconfig so that this script and the component
 * defconfig cannot drift apart, and so an AP script can keep the same shape
 * while describing a different window.
 *
 * The chip header is deliberately not included here: it pulls in stdint.h and
 * its UINT32_C() suffixes, and the linker accepts neither. Cross-domain guards
 * against PWR_MNG/SWAP therefore live in C, where that header is usable.
 *
 * Flash stays literal. CONFIG_FLASH_START/CONFIG_FLASH_SIZE are 0 in this
 * configuration, and the vector base is already cross-checked against the
 * packaging profile by the L1 validator.
 */

MEMORY
{
  flash (rx)  : ORIGIN = 0x02010000, LENGTH = 0x00150000
  sram  (rwx) : ORIGIN = CONFIG_RAM_START, LENGTH = CONFIG_RAM_SIZE
}

OUTPUT_ARCH(arm)
EXTERN(_vectors)
ENTRY(__start)

BK7258_IDLE_STACK_RESERVATION = 0x800;

SECTIONS
{
  .text :
  {
    _stext = ABSOLUTE(.);
    . = ALIGN(512);
    KEEP(*(.vectors))
    *(.text .text.*)
    *(.fixup)
    *(.gnu.warning)
    *(.rodata .rodata.*)
    *(.gnu.linkonce.t.*)
    *(.glue_7)
    *(.glue_7t)
    *(.got)
    *(.gcc_except_table)
    *(.gnu.linkonce.r.*)
    _etext = ABSOLUTE(.);
  } > flash

  .init_section : ALIGN(4)
  {
    _sinit = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
    _einit = ABSOLUTE(.);
  } > flash

  .ARM.extab : ALIGN(4)
  {
    *(.ARM.extab*)
  } > flash

  .ARM.exidx : ALIGN(4)
  {
    __exidx_start = ABSOLUTE(.);
    *(.ARM.exidx*)
    __exidx_end = ABSOLUTE(.);
  } > flash

  _eronly = ABSOLUTE(.);

  .data : ALIGN(4)
  {
    _sdata = ABSOLUTE(.);
    *(.data .data.*)
    *(.gnu.linkonce.d.*)

    /* Armino marks some objects __attribute__((section(".dtcm_sec_data"))) to
     * place them in the SoC's DTCM.  This port has no DTCM region -- MEMORY
     * declares only sram and flash -- so without this line the linker kept
     * .dtcm_sec_data as an orphan output section placed immediately after
     * .data: inside ordinary SRAM, but OUTSIDE _sdata.._edata.
     *
     * That is a silent-corruption trap rather than a layout detail.  The
     * startup copy loop walks _sdata.._edata only (bk7258_start.c:114-115) and
     * .bss clearing starts at _sbss, so anything landing in the gap between
     * them is neither initialised from flash nor zeroed -- it keeps whatever
     * reset left behind, with no diagnostic anywhere.
     *
     * Board-observed consequence: ipc_route_tbl (mb_ipc.c:179) sat at
     * 0x28070778 while _edata was 0x28070238, so mb_chnl_open() read
     * log_chnl == 0, computed phy_chnl_idx 0 != 1 and returned BK_ERR_PARAM.
     * That is the "[AMP] CP mailbox IPC init failed: -4097" line.
     *
     * Folded here rather than by dropping the attribute in
     * mb_ipc/authority/middleware/driver/mailbox/mb_ipc.c: that file is
     * imported authority source and stays byte-identical.  Nothing is lost by
     * the fold, because the DTCM those objects were meant for does not exist in
     * this port either way. */

    *(.dtcm_sec_data .dtcm_sec_data.*)
    CONSTRUCTORS
    . = ALIGN(4);
    _edata = ABSOLUTE(.);
  } > sram AT > flash

  .noinit (NOLOAD) : ALIGN(4)
  {
    *(.noinit .noinit.*)
    . = ALIGN(8);
    _enoinit = ABSOLUTE(.);
  } > sram

  .bss : ALIGN(4)
  {
    _sbss = ABSOLUTE(.);
    *(.bss .bss.*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = ABSOLUTE(.);
  } > sram

  /* Link options disable build-id generation. If another toolchain adds an
   * allocatable orphan section before .text, the vector-base assertion below
   * must fail the link instead of producing a silently shifted raw image. */

  ASSERT((ORIGIN(flash) & 0x1ff) == 0,
         "BK7258 CP vector base must be 512-byte aligned")
  ASSERT(_vectors == ORIGIN(flash),
         "BK7258 CP vector table must begin at the image base")
  ASSERT(CONFIG_BK7258_RPMSG_SHM_ADDR + CONFIG_BK7258_RPMSG_SHM_SIZE ==
         ORIGIN(sram),
         "BK7258 CP RAM must begin after RPMSG_SHM")
  ASSERT(ORIGIN(sram) + LENGTH(sram) == 0x2809f700,
         "BK7258 CP RAM must end where PWR_MNG begins")
  ASSERT(_enoinit <= _sbss,
         "BK7258 CP .noinit must remain below .bss and the idle stack")
  ASSERT(_ebss + BK7258_IDLE_STACK_RESERVATION <= ORIGIN(sram) + LENGTH(sram),
         "BK7258 CP SRAM exhausted by static sections and idle stack")

  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .comment 0 : { *(.comment) }
  .debug_abbrev 0 : { *(.debug_abbrev) }
  .debug_info 0 : { *(.debug_info) }
  .debug_line 0 : { *(.debug_line) }
}
