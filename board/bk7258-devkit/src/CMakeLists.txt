target_sources(board PRIVATE
               bk7258_boardinit.c
               bk7258_bringup.c
               bk7258_ap_control.c)

if(CONFIG_RPTUN)
  target_sources(board PRIVATE bk7258_rptun.c)
endif()
target_include_directories(board PRIVATE ${NUTTX_BOARD_ABS_DIR}/include)
