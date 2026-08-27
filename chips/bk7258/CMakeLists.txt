# BK7258 CP/AP common chip component.

set(SRCS
    bk7258_irq.c
    bk7258_allocateheap.c
    bk7258_timer.c
    bk7258_ap_boot.c)

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
       bk7258_uart.c
       bk7258_gpio.c)

  if(CONFIG_BK7258_PSRAM)
    list(APPEND SRCS bk7258_psram.c)
  endif()
endif()

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)
