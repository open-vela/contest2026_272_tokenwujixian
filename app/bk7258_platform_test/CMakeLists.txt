# BK7258 reusable platform API and register readback test.
if(CONFIG_LVX_USE_DEMO_CONTEST2026_272_BK7258_PLATFORM_TEST)
  nuttx_add_application(
    NAME bk7258_platform_test
    SRCS bk7258_platform_test_main.c
    STACKSIZE 4096)
endif()
