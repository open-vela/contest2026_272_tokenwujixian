/****************************************************************************
 * board/bk7258-devkit/scripts/flash_ap.ld
 *
 * Secure, single-core OpenVela AP linker script.  Physical CPU1 enters the
 * vector table at the beginning of the AP raw payload.
 ****************************************************************************/

#include <nuttx/config.h>

MEMORY
{
  flash (rx)  : ORIGIN = 0x02160000, LENGTH = 0x002a0000
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
    _evectors = ABSOLUTE(.);
    . = ORIGIN(flash) + 0x200;
    KEEP(*(.ap_contract))
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

  /* Physical CPU2 (AP SMP secondary core) boot vector table.  It lives in AP
   * XIP and is programmed into the CPU2 boot-offset field by up_cpu_start().
   * Only entries [0] (initial MSP) and [1] (reset PC) are consumed at reset;
   * every other slot points at the shared direct exception handler. */

  .vectors_core1 : ALIGN(512)
  {
    _vectors_core1 = ABSOLUTE(.);
    KEEP(*(.vectors_core1))
    _evectors_core1 = ABSOLUTE(.);
  } > flash

  .init_section : ALIGN(4)
  {
    _sinit = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
    _einit = ABSOLUTE(.);
  } > flash

  .ARM.extab : ALIGN(4) { *(.ARM.extab*) } > flash
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

  ASSERT((ORIGIN(flash) & 0x1ff) == 0,
         "BK7258 AP vector base must be 512-byte aligned")
  ASSERT(_vectors == ORIGIN(flash),
         "BK7258 AP vector table must begin at the image base")
  ASSERT(_evectors <= ORIGIN(flash) + 0x200,
         "BK7258 AP vector table overlaps the image contract")
  ASSERT(g_bk7258_ap_image_contract == ORIGIN(flash) + 0x200,
         "BK7258 AP image contract must remain at offset 0x200")
  ASSERT((_vectors_core1 & 0x1ff) == 0,
         "BK7258 AP CPU2 vector table must be 512-byte aligned")
  ASSERT(_vectors_core1 >= ORIGIN(flash) &&
         _evectors_core1 <= ORIGIN(flash) + LENGTH(flash),
         "BK7258 AP CPU2 vector table must stay inside AP XIP")
  ASSERT(_evectors_core1 <= _etext || _vectors_core1 >= _etext,
         "BK7258 AP CPU2 vector table must not straddle _etext")
  ASSERT(ORIGIN(sram) == 0x28010000 && LENGTH(sram) == 0x00054000,
         "BK7258 AP must use the all-OpenVela 336 KiB RAM window")
  ASSERT(ORIGIN(sram) + LENGTH(sram) == CONFIG_BK7258_RPMSG_SHM_ADDR,
         "BK7258 AP RAM must end where RPMSG_SHM begins")
  ASSERT(CONFIG_BK7258_RPMSG_SHM_ADDR + CONFIG_BK7258_RPMSG_SHM_SIZE ==
         0x2806ec00,
         "BK7258 RPMSG_SHM must end where locked CP RAM begins")
  ASSERT(_enoinit <= _sbss,
         "BK7258 AP .noinit must remain below .bss and the idle stack")
  ASSERT(_ebss + BK7258_IDLE_STACK_RESERVATION <= ORIGIN(sram) + LENGTH(sram),
         "BK7258 AP SRAM exhausted by static sections and idle stack")

  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .comment 0 : { *(.comment) }
  .debug_abbrev 0 : { *(.debug_abbrev) }
  .debug_info 0 : { *(.debug_info) }
  .debug_line 0 : { *(.debug_line) }
}
