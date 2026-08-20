target_sources(board PRIVATE
               bk7258_boardinit.c
               bk7258_bringup.c
               bk7258_ap_control.c)

if(CONFIG_RPTUN)
  target_sources(board PRIVATE bk7258_rptun.c)
endif()

if(CONFIG_LCD_JD9853 OR CONFIG_LCD_GC9D01)
  target_sources(board PRIVATE jd9853.c bk7258_spi_bitbang.c bk7258_spi_hw.c bk7258_lcd.c gc9d01.c)
endif()

target_include_directories(board PRIVATE ${NUTTX_BOARD_ABS_DIR}/include
                                      ${NUTTX_CHIP_ABS_DIR}/include)
