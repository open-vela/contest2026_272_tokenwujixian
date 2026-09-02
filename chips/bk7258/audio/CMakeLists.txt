# BK7258 audio HAL sources.
#
# DAC (playback) + ADC (analog-MIC capture).  DMIC/DTMF live behind separate
# Kconfig gates and stay out of the compile unit until a matching hardware
# requirement lands.

target_sources(arch PRIVATE bk7258_dac.c bk7258_gdma.c bk7258_adc.c)
target_include_directories(arch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
