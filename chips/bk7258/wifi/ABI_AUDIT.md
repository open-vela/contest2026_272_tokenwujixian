# BK7258 Wi-Fi vendor ABI audit (P0 artifact)

> Status update 2026-08-26: archive hashes and generated ABI contracts remain
> valid. The NuttX integration now has a source-backed STA-only runtime probe;
> it reaches final link analysis but is not yet link-complete or hardware
> validated. SoftAP/P2P/WPS are intentionally outside this closure.

Source: Armino SDK `release/v3.1.1`, commit `d2ded037798530175e5dc5cde6fa1878f5d5ef35`.

## 1. Vendor binary inventory

All paths relative to `/home/czp/armino/bk_avdk_smp`:

| Library | SHA-256 | Notes |
| --- | --- | --- |
| `cp/components/bk_libs/bk7258/libs/libwifi.a` | `6e7e018c884674ced3305d241138eae004c4c3d8568123573c24ec9c71d37b0c` | MAC/UMAC/LMAC core |
| `cp/components/bk_libs/bk7258/libs/libbk_phy.a` | `fb3e8f6a6782f6fab6edde59ec95417c0da1da2ab8263230848535b34e4a6979` | RF/PHY calibration |
| `cp/components/bk_libs/bk7258/libs/libcom_phy.a` | `208aa99e8b794d587c8161a7b861efebbe2c47789f85e3c025189d712455e0c1` | Bluetooth-only branch, not linked with libbk_phy |
| `cp/components/bk_libs/bk7258/libs/libwifi_nspe.a` | `564f563984cf4d3bad3b101283420c470a665b2bed3dbd6b40bf767558b1e94a` | non-secure variant |
| `cp/components/bk_libs/bk7258/libs/libbk_phy_nspe.a` | `17bd0d0e81a1f01aed634e26283b3c502466586e319ff13c1a62fea302c695cf` | non-secure variant |

Wi-Fi-enabled profile links `libwifi.a` + `libbk_phy.a` (+ optional
`libbk_phy_info.a`); `libcom_phy.a` is the Bluetooth-only branch and must not
be linked unconditionally alongside `libbk_phy.a`.

## 2. Undefined symbol classification (`libwifi.a`)

`nm -u` reports **899** undefined symbols. Classification:

| Category | ~Count | Examples | NuttX disposition |
| --- | --- | --- | --- |
| OSAL (`rtos_*`/`os_*`) | 19 | `rtos_create_thread`, `rtos_init_queue`, `rtos_disable_int`, `os_malloc_debug` | `bk7258_wifi_osal.c` |
| Platform capability tables | 2 | `g_wifi_funcs`, `g_wifi_vars` | constructed by the integration branch |
| Vendor glue (MAC/UMAC/LMAC inter-module) | ~326 | `rwnxl_init`, `ke_msg_send`, `bmsg_tx_sender`, `mm_*`, `me_*` | kept Beken integration source |
| DHCP/ARP state recognition | 7 | `me_dhcp_done_handler`, `me_check_is_dhcp_done`, `me_send_arp_ind` | NuttX IP-ready bridge |
| libc / math / runtime | remainder | `memcpy`, `malloc`, `ceil`, `__aeabi_*` | NuttX libc |

## 3. Protocol-stack boundary conclusion

`libwifi.a` has **no direct undefined reference** to `pbuf_*`, `netif_*`,
`dhcp_*`, `tcp_*`, `udp_*`, `socket`, or lwIP. The seven `me_*dhcp*`/`me_*arp*`
symbols are internal DHCP-frame/ARP state recognition for power-save and
coexistence, not a DHCP client. Platform capability enters through
`g_wifi_funcs`/`g_wifi_vars`.

Consequence: replacing Armino lwIP/DHCP/IP with the NuttX network stack is an
architecturally sound main path (plan §11.4). What must be preserved is the
vendor MAC data path, WPA/EAPOL path, and a pbuf-shaped packet ABI — not the
Armino TCP/IP stack.

## 4. Unresolved items (block linking)

1. **Compiler/FPU ABI**: vendor library hard-float + CMSE/SPE-NSPE; must match
   the NuttX CP toolchain and FPU context save/restore before linking.
2. **`struct pbuf` layout**: open glue relies on `next/payload/len/tot_len`
   plus a 708-byte private reserve (`108` head + `600` descriptor); the team
   shim must stay layout-compatible with the integration branch's pbuf.
3. **Secure/non-secure SYS base**: `0x44010000` secure vs `+0x10000000`
   non-secure; must be fixed before any clock/power/IRQ write.
4. **STA runtime closure**: WPA source integration is now aligned with the
   Armino non-P2P manifest. Remaining link gaps are queue/time/crypto/driver
   and NuttX capability providers; AP/P2P/WPS sources are excluded.
5. **License**: Armino root carries Apache-2.0; binary relink/redistribution
   and NuttX-buildable delivery must be confirmed with Beken.
