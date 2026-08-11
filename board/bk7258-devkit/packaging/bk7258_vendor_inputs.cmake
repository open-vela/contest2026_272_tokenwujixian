# BK7258 local vendor-input contract.
#
# The L2 package target will call bk7258_load_vendor_inputs() before invoking
# the external Beken packer. Keep this helper inert for L1 CP/AP component
# builds so source-only configuration and static checks do not require vendor
# binaries on every developer machine.

function(bk7258_load_vendor_inputs)
  # CMAKE_SOURCE_DIR is <workspace>/nuttx for the standard OpenVela CMake
  # invocation, so its parent is the workspace root.
  set(_bk7258_inputs_file
      "${CMAKE_SOURCE_DIR}/../local/bk7258-vendor-inputs.cmake")

  if(NOT EXISTS "${_bk7258_inputs_file}")
    message(FATAL_ERROR
      "BK7258 L2 packaging requires ${_bk7258_inputs_file}. Copy "
      "${NUTTX_BOARD_ABS_DIR}/packaging/bk7258-vendor-inputs.cmake.example "
      "there and set the local external asset paths. Do not use environment "
      "variables to select the packaging profile.")
  endif()

  include("${_bk7258_inputs_file}")

  foreach(_bk7258_required_var
          BK7258_ARMINO_SDK_ROOT
          BK7258_BEKEN_GENIE_BUILD_ROOT)
    if(NOT DEFINED ${_bk7258_required_var} OR
       "${${_bk7258_required_var}}" STREQUAL "")
      message(FATAL_ERROR
        "${_bk7258_inputs_file} must set ${_bk7258_required_var}.")
    endif()

    if(NOT IS_DIRECTORY "${${_bk7258_required_var}}")
      message(FATAL_ERROR
        "${_bk7258_required_var} does not name an existing directory: "
        "${${_bk7258_required_var}}")
    endif()
  endforeach()

  set(BK7258_ARMINO_SDK_ROOT "${BK7258_ARMINO_SDK_ROOT}" PARENT_SCOPE)
  set(BK7258_BEKEN_GENIE_BUILD_ROOT
      "${BK7258_BEKEN_GENIE_BUILD_ROOT}" PARENT_SCOPE)
endfunction()
