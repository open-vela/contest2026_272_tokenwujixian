# BK7258 CP/AP common chip component.

set(SRCS
    bk7258_irq.c
    bk7258_allocateheap.c
    bk7258_timer.c
    bk7258_ap_boot.c
    bk7258_gpio.c)

if(CONFIG_ARCH_HAVE_MULTICPU)
  list(APPEND SRCS bk7258_cpuindex.c)
endif()

if(CONFIG_SMP)
  list(APPEND SRCS bk7258_smp.c)
endif()

if(CONFIG_BK7258_MAILBOX)
  list(APPEND SRCS bk7258_mailbox.c)
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
       bk7258_uart.c)

  if(CONFIG_BK7258_PSRAM)
    list(APPEND SRCS bk7258_psram.c)
  endif()
endif()

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)

if(CONFIG_AUDIO_BK7258)
  add_subdirectory(audio)
endif()
