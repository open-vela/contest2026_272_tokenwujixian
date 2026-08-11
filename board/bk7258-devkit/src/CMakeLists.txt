target_sources(board PRIVATE bk7258_boardinit.c bk7258_bringup.c)
target_include_directories(board PRIVATE ${NUTTX_BOARD_ABS_DIR}/include)
