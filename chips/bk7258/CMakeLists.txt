# BK7258 secure CP / CPU0 chip component.

set(SRCS
    bk7258_start.c
    bk7258_clock.c
    bk7258_irq.c
    bk7258_allocateheap.c
    bk7258_lowputc.c
    bk7258_uart.c
    bk7258_timer.c
    bk7258_gpio.c)

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)
