# BK7258 CP/AP common chip component.

set(SRCS
    bk7258_irq.c
    bk7258_allocateheap.c
    bk7258_timer.c
    bk7258_flash.c
    bk7258_ap_boot.c
    bk7258_gpio.c
    bk7258_trng.c
    bk7258_ca.c)

if(CONFIG_ARM_MPU)
  list(APPEND SRCS bk7258_mpuinit.c)
endif()

if(CONFIG_ARCH_HAVE_MULTICPU)
  list(APPEND SRCS bk7258_cpuindex.c)
endif()

if(CONFIG_SMP)
  list(APPEND SRCS bk7258_smp.c)
endif()

if(CONFIG_BK7258_MAILBOX)
  list(APPEND SRCS bk7258_mailbox.c)
endif()

if(CONFIG_BK7258_MB_IPC_RPMSG)
  if(CONFIG_BK7258_COMPONENT_CP)
   set(BK7258_MB_IPC_ROLE ${NUTTX_CHIP_ABS_DIR}/mb_ipc/armino)
    set(BK7258_MB_IPC_SOURCES
        mb_ipc/armino_cp_mailbox_channel.c
        mb_ipc/armino_cp_mb_chnl_buff.c
        mb_ipc/armino_cp_mb_ipc.c)
  else()
    set(BK7258_MB_IPC_ROLE ${NUTTX_CHIP_ABS_DIR}/mb_ipc/armino_ap)
    set(BK7258_MB_IPC_SOURCES
        mb_ipc/armino_ap_mailbox_channel.c
        mb_ipc/armino_ap_mb_chnl_buff.c
        mb_ipc/armino_ap_mb_ipc.c
        mb_ipc/bk_mailbox_rpmsg_ap_rtos.c)
  endif()

  list(APPEND SRCS
       mb_ipc/bk_mailbox_rpmsg.c
       ${BK7258_MB_IPC_SOURCES})
endif()

if(CONFIG_BK7258_GDMA)
  list(APPEND SRCS bk7258_dma.c)
endif()

if(CONFIG_BK7258_COMPONENT_AP)
  list(APPEND SRCS bk7258_ap_start.c)
else()
  list(APPEND SRCS
       bk7258_start.c
       bk7258_sysctrl.c
       bk7258_clock.c
       bk7258_lowputc.c
       bk7258_uart.c
       bk7258_wdt.c
       bk7258_reset_reason.c
       bk7258_gpio_hal.c
       bk7258_uart_hal.c
       bk7258_uart_driver.c
       bk7258_sys_touch_driver.c
       bk7258_stack_base.c
       bk7258_dd.c
       bk7258_driver.c
       bk7258_drv_model.c
       bk7258_arm.c
       bk7258_trap_base.c
       bk7258_sys_audio_driver.c
       bk7258_aon_rtc_hal.c
       bk7258_printf.c)

  if(CONFIG_BK7258_PSRAM)
    list(APPEND SRCS bk7258_psram.c)
  endif()
endif()

# The Armino baseline is compiled with -fsigned-char (its manifest sets it for
# every unit), so a port that handles char values must be too or it is not the
# same code: bk7258_trap_base.c's emergency_uart_write_string() turns
# ldrsb.w into ldrb without it, which changes what the UART FIFO write gets for
# bytes >= 0x80.  NuttX-side files are deliberately not touched.
if(CONFIG_BK7258_COMPONENT_CP)
  # TARGET_DIRECTORY: `arch` is created in arch/arm/src, and a source-file
  # property set here only reaches a target created in this directory
  # otherwise.
  set_property(SOURCE bk7258_trap_base.c TARGET_DIRECTORY arch
               PROPERTY COMPILE_OPTIONS "-fsigned-char")
endif()

target_sources(arch PRIVATE ${SRCS})

# Include directories shared by `arch` and the per-module aux libraries
# created below it.  Kept as a variable so the aux libraries compile against
# the same headers arch does.
set(BK7258_CHIP_MODULE_INCLUDES
    ${NUTTX_CHIP_ABS_DIR}/include)

target_include_directories(arch PRIVATE ${BK7258_CHIP_MODULE_INCLUDES})

# bk7258_gpio.c implements the Armino bk_gpio_* face (see the file's
# "Armino GPIO API face" section) and must take the signatures from the
# baseline's own contract header so the existing callers need no change.
# Computed locally: BK7258_ARMINO_BASELINE_DIR is a subdirectory-local
# set() inside wifi_armino/ and is invisible here.
#
# CP only: the face is under #ifndef CONFIG_BK7258_COMPONENT_AP, and on AP
# these paths shadow mb_ipc/armino_ap/include/driver/mailbox_channel.h (both
# declare the channel enum, with CP0_MB_CHNL_IPC vs CP1_MB_CHNL_IPC).
# bk7258_wdt.c and bk7258_reset_reason.c provide the Armino WDT and
# reset-reason faces; their signatures come from the baseline's own headers
# (driver/wdt.h, driver/aon_wdt.h, wdt_hal.h, aon_wdt_hal.h, reset_reason.h),
# so those header directories must be visible from `arch`.
if(CONFIG_BK7258_COMPONENT_CP)
  target_include_directories(arch PRIVATE
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/soc
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/soc/common/hal/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/soc/common/soc/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/soc/bk7258/hal
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/soc/bk7258/soc
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/include/bk_private
                             # The legacy directory has to come before
                             # wifi/hal_port/include: both carry a
                             # drv_model_pub.h, hal_port's is a trimmed copy
                             # without DD_DEV_TYPE_END / the handle masks, and
                             # drv_model.h (our port's signature source) needs
                             # the full one.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/include/bk_private/legacy
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/pmu
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/wdt
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/aon_wdt
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/reset_reason
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/arch/cm33/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/bk_common/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/bk_system
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/uart
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/bk_common/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/bk7258
                             # bk7258_driver.c and bk7258_dd.c keep their
                             # authority signatures from dd_pub.h and
                             # drv_model.h in this directory.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/common
                             # compat/bk7258_armino_shell_api.c carries the AT
                             # front end's entry points now that
                             # components/at_server is excluded, so it needs the
                             # at_server headers for the types they use.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/at_server
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/at_server/_at_server
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/at_server/_at_server_port
                             # bk7258_trap_base.c is the exception/dump core:
                             # cm_backtrace_fault() and the rtos_dump_*
                             # helpers it calls come from these two components.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/coredump/cm_backtrace
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/bk_rtos/include
                             # bk7258_arm.c is the Armino CM33 core face and
                             # keeps the authority's bodies: SCB, the CMSIS
                             # atomics (__LDAEX/__STREXW/__STL) and
                             # NVIC_GetAllPendingIRQ all come from CMSIS.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/cmsis/CMSIS_5/CMSIS/Core/Include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/cmsis/CMSIS_5/Device/Beken/armstar
                             # easyflash_init()'s declaration (bk_ef.h ->
                             # easyflash.h); the EasyFlash units themselves
                             # stay in the baseline.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/easy_flash
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/easy_flash/easy_flash_V4.X/inc
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/sys_ctrl
                             ${NUTTX_CHIP_ABS_DIR}/pm/nuttx_port/include
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/projects/wifi/iperf/cp/config/bk7258
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/build/bk7258/iperf/bk7258/config
                             # bk7258_stack_base.c asks whether an address is in
                             # the application partition, so it needs the same
                             # generated CONFIG_APPLICATION_PARTITION_OFFSET
                             # <driver/flash_partition.h> reaches in the baseline.
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/boards/bk7258/partitions
                             ${NUTTX_CHIP_ABS_DIR}/armino_baseline/build/bk7258/iperf/partitions)
endif()

if(CONFIG_BK7258_MB_IPC_RPMSG)
  target_include_directories(arch PRIVATE
                             ${NUTTX_CHIP_ABS_DIR}/wifi/hal_port/include
                             ${NUTTX_CHIP_ABS_DIR}/mb_ipc
                             ${NUTTX_CHIP_ABS_DIR}/mb_ipc/include
                             ${BK7258_MB_IPC_ROLE}/include
                             ${BK7258_MB_IPC_ROLE}/middleware/driver/mailbox)
endif()

if(CONFIG_AUDIO_BK7258)
  add_subdirectory(audio)
endif()

if(CONFIG_BK7258_COMPONENT_CP)
  add_subdirectory(temp_detect)
endif()

add_subdirectory(wifi_armino)
add_subdirectory(wlan_ctrl)
