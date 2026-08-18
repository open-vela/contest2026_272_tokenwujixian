# BK7258 DevKit board component boundary.
#
# CP and AP are separate component configurations. This file intentionally
# does not export an app.bin/app1.bin, load vendor inputs or package
# all-app.bin. The selected custom-board directory is already the scope of
# this CMakeLists, so do not gate sources/linker selection on a secondary
# Kconfig symbol that may not be exported before CMake evaluates this file.
add_subdirectory(src)

if(CONFIG_BK7258_COMPONENT_AP)
  set(BK7258_LINKER_SCRIPT "flash_ap.ld")
  set(BK7258_COMPONENT_DIR "${CMAKE_BINARY_DIR}/bk7258_ap")
  set(BK7258_COMPONENT_BIN "app1.bin")
  set(BK7258_COMPONENT_LABEL "AP")
else()
  set(BK7258_LINKER_SCRIPT "flash.ld")
  set(BK7258_COMPONENT_DIR "${CMAKE_BINARY_DIR}/bk7258")
  set(BK7258_COMPONENT_BIN "app.bin")
  set(BK7258_COMPONENT_LABEL "CP")
endif()

set_property(GLOBAL PROPERTY LD_SCRIPT
             "${NUTTX_BOARD_ABS_DIR}/scripts/${BK7258_LINKER_SCRIPT}")
target_link_options(nuttx PRIVATE "-Wl,--build-id=none")

# L1 only: export the selected raw component and its inspection artifacts. L2
# packaging is intentionally explicit and remains outside the NuttX build.
#
# Follow the upstream board convention for post-build work: a nuttx_post_build
# custom target plus add_custom_command(TARGET ... POST_BUILD), as used by
# rp2040, spresense and sim. Dependencies are expressed on targets only, never
# on generated file paths, and side-effect files are declared via BYPRODUCTS.
#
# This matters for nuttx.map in particular. It is only a side effect of the link
# step -- CONFIG_DEBUG_LINK_MAP makes the arch layer append -Wl,-Map=nuttx.map
# and no CMake rule declares it as an OUTPUT -- so naming it as a file-level
# dependency makes ninja reject the whole graph ("missing and no known rule to
# make it") on a clean build, and only appears to work while a stale map
# survives from an earlier link. Ordering here comes from the framework, which
# wires nuttx_post_build after nuttx_post once this target exists.
set(BK7258_L1_REPORT "${BK7258_COMPONENT_DIR}/l1-validation.json")

add_custom_target(
  nuttx_post_build
  DEPENDS nuttx
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Exporting and validating the BK7258 ${BK7258_COMPONENT_LABEL} L1 component")

add_custom_command(
  TARGET nuttx_post_build
  POST_BUILD
  BYPRODUCTS
    "${BK7258_COMPONENT_DIR}/${BK7258_COMPONENT_BIN}"
    "${BK7258_COMPONENT_DIR}/nuttx.elf"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BK7258_COMPONENT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different nuttx.bin
          "${BK7258_COMPONENT_DIR}/${BK7258_COMPONENT_BIN}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different nuttx
          "${BK7258_COMPONENT_DIR}/nuttx.elf"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Exporting BK7258 ${BK7258_COMPONENT_LABEL} L1 component artifacts")

if(CONFIG_DEBUG_LINK_MAP)
  add_custom_command(
    TARGET nuttx_post_build
    POST_BUILD
    BYPRODUCTS "${BK7258_COMPONENT_DIR}/nuttx.map"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different nuttx.map
            "${BK7258_COMPONENT_DIR}/nuttx.map"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Exporting BK7258 CP link map")
endif()

if(CONFIG_BK7258_COMPONENT_AP)
  add_custom_command(
    TARGET nuttx_post_build
    POST_BUILD
    BYPRODUCTS "${BK7258_L1_REPORT}"
    COMMAND ${Python3_EXECUTABLE}
            "${NUTTX_BOARD_ABS_DIR}/tools/validate_bk7258_ap_image.py"
            --elf "${BK7258_COMPONENT_DIR}/nuttx.elf"
            --raw "${BK7258_COMPONENT_DIR}/${BK7258_COMPONENT_BIN}"
            --config "${CMAKE_BINARY_DIR}/.config"
            --report "${BK7258_L1_REPORT}"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Validating BK7258 OpenVela AP L1 ELF and raw image")
else()
  add_custom_command(
    TARGET nuttx_post_build
    POST_BUILD
    BYPRODUCTS "${BK7258_L1_REPORT}"
    COMMAND ${Python3_EXECUTABLE}
            "${NUTTX_BOARD_ABS_DIR}/tools/validate_bk7258_cp_image.py"
            --profile "${NUTTX_BOARD_ABS_DIR}/packaging/profiles/bk7258-devkit-l0-vendor-ap.json"
            --elf "${BK7258_COMPONENT_DIR}/nuttx.elf"
            --raw "${BK7258_COMPONENT_DIR}/${BK7258_COMPONENT_BIN}"
            --config "${CMAKE_BINARY_DIR}/.config"
            --report "${BK7258_L1_REPORT}"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Validating BK7258 CP L1 ELF and raw image")
endif()
