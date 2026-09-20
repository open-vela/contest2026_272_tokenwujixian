# BK7258 Wi-Fi skeleton component (team-owned NuttX integration).

if(CONFIG_BK7258_WIFI)

  set(BK7258_WIFI_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
  set(BK7258_ARMINO_ROOT ${BK7258_WIFI_ROOT}/armino)
  set(BK7258_ARMINO_WIFI ${BK7258_ARMINO_ROOT}/glue/bk_wifi)
  set(BK7258_ARMINO_WPA ${BK7258_ARMINO_ROOT}/wpa_supplicant)
  set(BK7258_WIFI_GLUE ${BK7258_WIFI_ROOT}/hal_port)

  # Include directories shared by `arch` and the bk7258_wpa aux library.
  set(BK7258_WIFI_INCLUDES
      ${BK7258_WIFI_ROOT}
      ${BK7258_WIFI_ROOT}/hal_port
      ${BK7258_WIFI_ROOT}/../aon_pmu
      ${BK7258_WIFI_GLUE}/include
      ${BK7258_WIFI_GLUE}/include/wpa_compat
      ${BK7258_ARMINO_ROOT}/include
      ${BK7258_ARMINO_ROOT}/include/common
      ${BK7258_ARMINO_ROOT}/glue/bk_phy/include
      ${BK7258_ARMINO_WIFI}/include
      ${BK7258_ARMINO_WIFI}/include/bk_private
      ${BK7258_ARMINO_WIFI}/include/generated
      ${BK7258_ARMINO_WPA}/src/common
      ${BK7258_ARMINO_WPA}/src/ap
      ${BK7258_ARMINO_WPA}/src/utils
      ${BK7258_ARMINO_WPA}/src/drivers
      ${BK7258_ARMINO_WPA}/src
      ${BK7258_ARMINO_WPA}/wpa_supplicant
      ${BK7258_ARMINO_WPA}/hostapd
      ${BK7258_ARMINO_ROOT}/board/bk7258/vnd_cal/include
      ${BK7258_ARMINO_WPA}
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../../board/bk7258-devkit/include)

  set(SRCS
      bk7258_wifi_lower.c
      bk7258_wifi_osal.c
      bk7258_wifi_packet.c
      hal_port/pbuf_shim.c
      bk7258_wifi_hw.c
      bk7258_wifi_adapter.c
      ../aon_pmu/aon_pmu_armino_hal.c
      ../aon_pmu/aon_pmu_armino_driver.c
      hal_port/hw_driver_shim.c
      hal_port/system_shim.c
      hal_port/rtos_ext_shim.c
      hal_port/rtos_compat_shim.c
      hal_port/bk_workqueue_nuttx.c
      hal_port/phy_calibration_shim.c
      hal_port/analog_shim.c
      hal_port/feature_shim.c
      hal_port/flash_shim.c
      hal_port/misc_shim.c
      hal_port/event_shim.c
      hal_port/scan_diagnostics.c
      hal_port/m2_snapshot.c
      hal_port/platform_shim.c
      hal_port/hal_port_lpdoze.c
      # vnd_cal comes from armino/board/bk7258/vnd_cal below,
      # which is byte-identical to authority cp/middleware/boards/bk7258.  Do
      # not add a hal_port transcription of it again: we had one, both copies
      # reached SRCS, and libarch.a ended up with two definitions of
      # vnd_cal_version, vnd_cal_set_epa_config and vnd_cal_version_log.  No
      # duplicate-symbol error surfaced because archive member extraction stops
      # at the first definition satisfying the reference, so which copy won
      # depended on archive order.
      #
      # power_clk_rf_init has no port here on purpose: the authoritative bk7258
      # build leaves CONFIG_POWER_CLOCK_RF unset, so it never runs there.  See
      # the comment at bk7258_wifi_lower.c:2226.
      hal_port/hal_port_dvfs.c
      hal_port/hal_port_sys_int_power.c
      hal_port/hal_port_mac.c
      hal_port/hal_port_bandgap.c
      hal_port/funcs_fill.c
      hal_port/os_shim.c
      hal_port/net_param_shim.c
      armino/glue/bk_wifi/src/rwnx_params.c
      armino/glue/bk_wifi/src/rwnx_td.c
      armino/glue/bk_wifi/src/rwnx_txq.c
      # VIF/STA index helpers used by rw_task.c, rwnx_tx.c and rwnx_rx.c above.
      # The wrapper existed but was never listed here, which is why rwm_mgmt_*
      # and rwm_get_monitor_vif_idx appeared as undefined symbols.
      armino/glue/bk_wifi/src/rwnx_utils.c
      # Monitor callback registry + management-frame filter. Backs the
      # unconditional rwnx_rx.c (wifi_monitor_get_cb) and libwifi.a's rxl_cntrl
      # (rx_mgmt_filter), so it belongs at the same level as its consumers.
      armino/glue/bk_wifi/src/lsig_monitor.c
      armino/glue/bk_wifi/src/rw_ieee80211.c
      armino/glue/bk_wifi/src/rwnx_tx.c
      armino/glue/bk_wifi/src/rwnx_rx.c
      hal_port/vendor_sources/rw_task.c
      armino/glue/bk_wifi/src/wifi_v2.c
      armino/glue/bk_wifi/src/wifi_config.c
      armino/glue/bk_wifi/src/net_param.c
      armino/glue/bk_wifi/src/rw_msg_tx.c
      armino/glue/bk_wifi/src/rw_msg_rx.c
      hal_port/vendor_sources/wifi_init.c
      armino/glue/bk_phy/src/bk_phy_adapter.c
      armino/board/bk7258/vnd_cal/vnd_cal.c
      armino/glue/bk_wifi/src/skbuff.c
      armino/glue/bk_phy/src/bk_rf_adapter.c
      armino/glue/bk_phy/src/bk_phy_api.c
      armino/glue/bk_phy/src/bk_phy_notify.c)
  list(APPEND SRCS hal_port/netif_shim.c)

  # These source-backed WPA units require the complete Armino eloop/socket
  # contract and are relevant only when runtime bring-up is explicitly
  # selected.  Keep them available without forcing that incomplete
  # contract into the default, runtime-disabled CP image.
  if(CONFIG_BK7258_WIFI_VENDOR_RUNTIME)
    set(BK7258_WPA_STA_SOURCES
        armino/wpa_supplicant/src/common/ieee802_11_common.c
        armino/wpa_supplicant/src/common/wpa_common.c
        armino/wpa_supplicant/src/common/wpa_psk_cache.c
        armino/wpa_supplicant/src/drivers/driver_beken.c
        armino/wpa_supplicant/src/drivers/driver_common.c
        armino/wpa_supplicant/src/drivers/drivers.c
        armino/wpa_supplicant/src/l2_packet/l2_packet_none.c
        armino/wpa_supplicant/src/rsn_supp/pmksa_cache.c
        armino/wpa_supplicant/src/rsn_supp/wpa.c
        armino/wpa_supplicant/src/rsn_supp/wpa_ie.c
        armino/wpa_supplicant/src/utils/common.c
        armino/wpa_supplicant/src/utils/crc32.c
        armino/wpa_supplicant/src/utils/os_none.c
        armino/wpa_supplicant/src/utils/wpa_debug.c
        armino/wpa_supplicant/src/utils/wpabuf.c
        armino/wpa_supplicant/wpa_supplicant/bssid_ignore.c
        armino/wpa_supplicant/wpa_supplicant/bss.c
        armino/wpa_supplicant/wpa_supplicant/config.c
        armino/wpa_supplicant/wpa_supplicant/config_none.c
        armino/wpa_supplicant/wpa_supplicant/events.c
        armino/wpa_supplicant/wpa_supplicant/notify.c
        armino/wpa_supplicant/wpa_supplicant/op_classes.c
        armino/wpa_supplicant/wpa_supplicant/sme.c
        armino/wpa_supplicant/wpa_supplicant/wmm_ac.c
        armino/wpa_supplicant/wpa_supplicant/wpa_scan.c
        armino/wpa_supplicant/wpa_supplicant/wpa_supplicant.c
        armino/wpa_supplicant/wpa_supplicant/wpas_glue.c
        armino/wpa_supplicant/wpa_supplicant/wnm_sta.c
        # Crypto closure, mirroring the SDK's own gating. CONFIG_USE_MBEDTLS,
        # CONFIG_PSA_MBEDTLS and CONFIG_WPA3 are all unset in this profile, so
        # the SDK selects its internal implementations instead of the
        # mbedtls/wolfssl variants. These files were already vendored but never
        # listed here - the same build-set omission class as rwnx_utils.c.
        armino/wpa_supplicant/src/crypto/aes-siv.c
        armino/wpa_supplicant/src/crypto/aes-unwrap.c
        armino/wpa_supplicant/src/crypto/aes-wrap.c
        armino/wpa_supplicant/src/crypto/dh_group5.c
        armino/wpa_supplicant/src/crypto/dh_groups.c
        armino/wpa_supplicant/src/crypto/sha1-prf.c
        armino/wpa_supplicant/src/crypto/sha256-kdf.c
        armino/wpa_supplicant/src/crypto/sha256-prf.c
        armino/wpa_supplicant/src/crypto/sha384-kdf.c
        armino/wpa_supplicant/src/crypto/sha384-prf.c
        armino/wpa_supplicant/src/crypto/sha512-kdf.c
        armino/wpa_supplicant/src/crypto/sha512-prf.c
        armino/wpa_supplicant/src/crypto/tls_none.c
        # Internal crypto branch (CONFIG_USE_MBEDTLS unset).
        armino/wpa_supplicant/src/crypto/aes-cbc.c
        armino/wpa_supplicant/src/crypto/aes-ctr.c
        armino/wpa_supplicant/src/crypto/aes-internal.c
        armino/wpa_supplicant/src/crypto/aes-internal-dec.c
        armino/wpa_supplicant/src/crypto/aes-internal-enc.c
        armino/wpa_supplicant/src/crypto/aes-omac1.c
        armino/wpa_supplicant/src/crypto/crypto_internal-modexp.c
        armino/wpa_supplicant/src/crypto/md5.c
        armino/wpa_supplicant/src/crypto/md5-internal.c
        armino/wpa_supplicant/src/crypto/rc4.c
        armino/wpa_supplicant/src/crypto/sha1.c
        armino/wpa_supplicant/src/crypto/sha1-internal.c
        armino/wpa_supplicant/src/crypto/sha1-pbkdf2.c
        armino/wpa_supplicant/src/crypto/sha384.c
        armino/wpa_supplicant/src/crypto/sha384-internal.c
        armino/wpa_supplicant/src/crypto/sha512.c
        armino/wpa_supplicant/src/crypto/sha512-internal.c
        # CONFIG_WPA3 unset, so sha256 comes from the internal set too.
        armino/wpa_supplicant/src/crypto/sha256.c
        armino/wpa_supplicant/src/crypto/sha256-internal.c)
    # These wpa_supplicant units are compiled verbatim.  bk_prelude.h, which
    # reproduces the definitions upstream injects globally, is force-included
    # through the aux library's own compile options instead of a generated
    # wrapper translation unit.
    #
    # Why an aux library and not per-source COMPILE_OPTIONS on `arch`: source
    # file properties are only visible in the directory scope that created the
    # target (nuttx/arch for `arch`, this file lives below it), and on CMake
    # 3.22 set_source_files_properties ... DIRECTORY from a descendant scope
    # silently attaches nothing.  nuttx_add_aux_library exists for exactly
    # this limitation (see its comment in cmake/nuttx_add_library.cmake): the
    # objects are created here where the options apply, and the library is
    # linked into `arch` below.
    #
    # Do NOT add the prelude to the `arch` target's own compile options: that
    # would alter every NuttX architecture translation unit.
    nuttx_add_aux_library(bk7258_wpa ${BK7258_WPA_STA_SOURCES})
    target_compile_options(bk7258_wpa PRIVATE
        -include ${BK7258_WIFI_GLUE}/include/bk_prelude.h)
    # bk_prelude.h reaches <arm_internal.h> -> <chip.h> and the Armino soc
    # headers, which the `arch` target carries as PRIVATE include dirs (see
    # chips/bk7258/CMakeLists.txt and nuttx/arch/arm/src/CMakeLists.txt:30);
    # this library is linked INTO arch, so it must add the same directories
    # itself.  The variables are visible here because chips/bk7258 is
    # add_subdirectory'd from that very scope.
    target_include_directories(bk7258_wpa PRIVATE
        ${BK7258_CHIP_MODULE_INCLUDES}
        ${NUTTX_CHIP_ABS_DIR}
        ${CMAKE_SOURCE_DIR}/arch/arm/src/common
        ${ARCH_SUBDIR}
        ${ARCH_SUBDIR_ARM}
        ${BK7258_WIFI_INCLUDES})
    list(APPEND SRCS
         armino/wpa_supplicant/bk_patch/fake_socket.c
         armino/wpa_supplicant/src/utils/eloop.c
         armino/wpa_supplicant/bk_patch/signal.c
         armino/wpa_supplicant/bk_patch/sk_intf.c
         # The WPA <-> MAC ioctl bridge and its transport. Both are STA
         # providers: driver_beken.c reaches hapd_intf_ioctl() through
         # ddrv.c:ioctl_inet(), and hapd_intf_ke_rx_handle() is what actually
         # transmits the STA MLME/EAPOL frames queued by wpa_supplicant.
         # They depend on the WPA socket contract, so they stay runtime-only.
         armino/wpa_supplicant/bk_patch/ddrv.c
         armino/glue/bk_wifi/src/hostapd_intf.c
         # STA association command, called by hostapd_intf.c:974
         # (wpa_send_assoc_req). Runtime-only because its only consumer is.
         armino/glue/bk_wifi/src/sa_station.c
         # Team-owned WPA handler queue object + accessors. Upstream keeps these
         # in hostapd/main_none.c, but compiling that file would drag in the
         # whole hostapd provider closure; the queue itself is shared by
         # wpa_supplicant and hostapd, so we own the four primitives instead.
         hal_port/wpa_queue_shim.c
         armino/wpa_supplicant/wpa_supplicant/ctrl_iface.c
         armino/wpa_supplicant/wpa_supplicant/main_supplicant.c)
  endif()

  target_sources(arch PRIVATE ${SRCS})
  target_include_directories(arch PRIVATE ${BK7258_WIFI_INCLUDES})
  if(CONFIG_BK7258_WIFI_VENDOR_RUNTIME)
    target_link_libraries(arch PRIVATE bk7258_wpa)
  endif()

  # The wrapper translation units establish the Armino/WPA configuration
  # contract locally. Do not add its macros or forced includes to `arch`, which
  # would alter every NuttX architecture translation unit.
  foreach(required_header
          ${BK7258_ARMINO_WIFI}/include/generated/lmac_msg.h
          ${BK7258_ARMINO_WIFI}/include/generated/lmac_mac.h
          ${BK7258_ARMINO_WIFI}/include/generated/lmac_tx.h)
    if(NOT EXISTS ${required_header})
      message(FATAL_ERROR "Missing BK7258 generated Wi-Fi header: ${required_header}")
    endif()
  endforeach()

  if(CONFIG_BK7258_WIFI_VENDOR_LINK)
    set(VENDOR_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prebuilts)
    foreach(lib libwifi.a libbk_phy.a)
      if(NOT EXISTS ${VENDOR_PREBUILT_DIR}/${lib})
        message(FATAL_ERROR "Missing pinned BK7258 vendor archive: ${VENDOR_PREBUILT_DIR}/${lib}")
      endif()
      nuttx_add_extra_library(${VENDOR_PREBUILT_DIR}/${lib})
    endforeach()
  endif()

endif()
