# BK7258 DevKit board component boundary.
#
# CP and AP are separate component configurations. This file intentionally
# does not export an app.bin/app1.bin, load vendor inputs or package
# all-app.bin. The selected custom-board directory is already the scope of
# this CMakeLists, so do not gate sources/linker selection on a secondary
# Kconfig symbol that may not be exported before CMake evaluates this file.
add_subdirectory(src)
set_property(GLOBAL PROPERTY LD_SCRIPT
             "${NUTTX_BOARD_ABS_DIR}/scripts/flash.ld")
target_link_options(nuttx PRIVATE "-Wl,--build-id=none")

# L1 only: export the raw CP component and its inspection artifacts. L2
# packaging is intentionally explicit and remains outside the NuttX build.
set(BK7258_COMPONENT_DIR "${CMAKE_BINARY_DIR}/bk7258")
set(BK7258_L1_REPORT "${BK7258_COMPONENT_DIR}/l1-validation.json")
add_custom_command(
  OUTPUT
    "${BK7258_COMPONENT_DIR}/app.bin"
    "${BK7258_COMPONENT_DIR}/nuttx.elf"
    "${BK7258_COMPONENT_DIR}/nuttx.map"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BK7258_COMPONENT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/nuttx.bin"
          "${BK7258_COMPONENT_DIR}/app.bin"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/nuttx"
          "${BK7258_COMPONENT_DIR}/nuttx.elf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/nuttx.map"
          "${BK7258_COMPONENT_DIR}/nuttx.map"
  DEPENDS
    nuttx_post
    "${CMAKE_BINARY_DIR}/nuttx.bin"
    "${CMAKE_BINARY_DIR}/nuttx"
    "${CMAKE_BINARY_DIR}/nuttx.map"
  COMMENT "Exporting BK7258 CP L1 component artifacts")

add_custom_command(
  OUTPUT "${BK7258_L1_REPORT}"
  COMMAND ${Python3_EXECUTABLE}
          "${NUTTX_BOARD_ABS_DIR}/tools/validate_bk7258_cp_image.py"
          --profile "${NUTTX_BOARD_ABS_DIR}/packaging/profiles/bk7258-devkit-l0-vendor-ap.json"
          --elf "${BK7258_COMPONENT_DIR}/nuttx.elf"
          --raw "${BK7258_COMPONENT_DIR}/app.bin"
          --report "${BK7258_L1_REPORT}"
  DEPENDS
    "${BK7258_COMPONENT_DIR}/app.bin"
    "${BK7258_COMPONENT_DIR}/nuttx.elf"
    "${BK7258_COMPONENT_DIR}/nuttx.map"
    "${NUTTX_BOARD_ABS_DIR}/tools/validate_bk7258_cp_image.py"
    "${NUTTX_BOARD_ABS_DIR}/packaging/profiles/bk7258-devkit-l0-vendor-ap.json"
  COMMENT "Validating BK7258 CP L1 ELF and raw image")

add_custom_target(nuttx_post_build DEPENDS "${BK7258_L1_REPORT}")
