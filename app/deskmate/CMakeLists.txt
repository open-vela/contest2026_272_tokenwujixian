# DeskMate device client for contest team 272.
if(CONFIG_LVX_USE_DEMO_CONTEST2026_272_DESKMATE)
  nuttx_add_application(
    NAME deskmate
    SRCS deskmate_main.c deskmate_client.c deskmate_controller.c deskmate_console.c deskmate_mqtt.c
    STACKSIZE 8192)
endif()
