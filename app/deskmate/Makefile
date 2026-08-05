############################################################################
# DeskMate device client for contest team 272
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = deskmate
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_272_DESKMATE)

MAINSRC = deskmate_main.c deskmate_client.c deskmate_controller.c deskmate_console.c deskmate_mqtt.c

include $(APPDIR)/Application.mk
