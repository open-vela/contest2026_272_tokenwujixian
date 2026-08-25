# BK7258 GDMA0 SRAM validation command.
if(CONFIG_LVX_USE_DEMO_CONTEST2026_272_BK7258_GDMA_TEST)
  nuttx_add_application(
    NAME bk7258_gdma_test
    SRCS bk7258_gdma_test_main.c
    STACKSIZE 2048)
endif()
