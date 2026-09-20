#!/usr/bin/env python3
"""Static contracts for the BK7258 Armino mailbox IPC RPMsg boundary.

The imported Armino CP/AP state machines remain unchanged. This test checks
that Vela contributes only the RPMsg transport adaptation and keeps incomplete
PHY/SARADC authority services disabled until their full client/server ABI is
integrated. It is host evidence only, not a hardware ACK claim.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
CHIP = ROOT / "chips" / "bk7258"
SYS_CONFIG = CHIP / "wifi/hal_port/include/common/sys_config.h"
SARADC_PORT = CHIP / "saradc/nuttx_port/nuttx_port.c"
BACKEND = CHIP / "mb_ipc/bk_mailbox_rpmsg.c"
AP_RTOS = CHIP / "mb_ipc/bk_mailbox_rpmsg_ap_rtos.c"
CP_AUTHORITY = CHIP / "mb_ipc/armino/middleware/driver/mailbox"
AP_AUTHORITY = CHIP / "mb_ipc/armino_ap/middleware/driver/mailbox"
CP_SOURCE = Path("/home/czp/armino/bk_avdk_smp/cp/middleware/driver/mailbox")
AP_SOURCE = Path("/home/czp/armino/bk_avdk_smp/ap/middleware/driver/mailbox")
PHY_ADAPTER = CHIP / "wifi/armino/glue/bk_phy/src/bk_phy_adapter.c"
PHY_ADAPTER_AUTHORITY = Path(
    "/home/czp/armino/bk_avdk_smp/cp/components/bk_phy/src/bk_phy_adapter.c"
)


class Bk7258MailboxRpmsgContractTest(unittest.TestCase):
    def test_wifi_profile_keeps_unintegrated_authority_services_off(self):
        config = SYS_CONFIG.read_text()
        self.assertIn("#define CONFIG_PHY_MB               0", config)
        self.assertIn("#define CONFIG_SARADC_MB            0", config)
        self.assertIn("#define CONFIG_BLUETOOTH            0", config)
        self.assertNotIn("#define CONFIG_CPU_CNT", config)

    def test_handwritten_pm_token_state_machine_is_absent(self):
        self.assertFalse((CHIP / "bk7258_pm_ipc.c").exists())
        self.assertFalse((CHIP / "include/bk7258_pm_ipc.h").exists())

    def test_backend_uses_rpmsg_and_preserves_authority_four_words(self):
        source = BACKEND.read_text()
        self.assertIn("rpmsg_register_callback", source)
        self.assertIn("int bk7258_mb_ipc_initialize(void)", source)
        self.assertIn("return mb_ipc_init();", source)
        self.assertIn("rpmsg_create_ept", source)
        self.assertIn("rpmsg_send_offchannel_raw", source)
        self.assertIn("uint32_t word[4]", source)
        self.assertIn("sizeof(mailbox_data_t) == 4 * sizeof(uint32_t)", source)
        self.assertIn("BK_MB_HDR_ACK_BOX", source)
        self.assertIn("BK_MB_IPC_RESPONSE", source)
        self.assertIn("visible.param3 = (uint32_t)(uintptr_t)slot->payload", source)
        self.assertIn("bk_mb_release_response_slot", source)
        self.assertNotIn("bk7258_mbox_notify_magic", source)

    def test_backend_bounds_and_reassembles_copied_payloads(self):
        source = BACKEND.read_text()
        self.assertIn("BK_MB_RPMSG_CHUNK       384", source)
        self.assertIn("BK_MB_RPMSG_MAX         4096", source)
        self.assertIn("wire.chunk_len > BK_MB_RPMSG_CHUNK", source)
        self.assertIn("slot->received != wire.offset", source)
        self.assertIn("offset += chunk_len", source)
        self.assertIn("while (offset < total_len)", source)

    def test_role_config_keeps_cpu_count_private_and_selects_ap_smp_route(self):
        source = (CHIP / "mb_ipc/role_config.h").read_text()
        self.assertIn("#include <nuttx/config.h>", source)
        self.assertIn("#define CONFIG_CPU_CNT 2", source)
        self.assertIn("#ifdef CONFIG_BK7258_COMPONENT_AP", source)
        self.assertIn("#define CONFIG_SOC_SMP 1", source)
        self.assertIn("#define SPINLOCK_SECTION", source)

    def test_ap_has_a_nuttx_rtos_provider_without_wifi_runtime(self):
        source = AP_RTOS.read_text()
        self.assertIn("defined(CONFIG_BK7258_COMPONENT_AP)", source)
        self.assertIn("nxsem_tickwait_uninterruptible", source)
        self.assertIn("nxsem_wait_uninterruptible", source)
        self.assertIn("spin_lock_irqsave", source)
        self.assertIn("return up_irq_save();", source)
        self.assertIn("return TICK2MSEC(clock_systime_ticks());", source)

    def test_imported_state_machines_match_role_authority(self):
        for filename in ("mailbox_channel.c", "mb_chnl_buff.c", "mb_ipc.c"):
            self.assertEqual(
                (CP_AUTHORITY / filename).read_bytes().rstrip(b"\r\n"),
                (CP_SOURCE / filename).read_bytes().rstrip(b"\r\n"),
                f"CP authority source drifted: {filename}",
            )
            self.assertEqual(
                (AP_AUTHORITY / filename).read_bytes().rstrip(b"\r\n"),
                (AP_SOURCE / filename).read_bytes().rstrip(b"\r\n"),
                f"AP authority source drifted: {filename}",
            )

    def test_phy_adapter_source_matches_cp_authority(self):
        self.assertEqual(
            PHY_ADAPTER.read_bytes().rstrip(b"\r\n"),
            PHY_ADAPTER_AUTHORITY.read_bytes().rstrip(b"\r\n"),
            "bk_phy_adapter.c must remain the CP authority source",
        )

    def test_saradc_stays_in_authority_local_callback_fallback(self):
        source = SARADC_PORT.read_text()
        self.assertIn("bk_err_t mb_saradc_ipc_init(void)", source)
        self.assertIn("bk_err_t mb_saradc_register_op_notify", source)
        self.assertIn("g_saradc_op_notify(0);", source)
        self.assertIn("g_saradc_op_notify(1);", source)
        self.assertIn("BK_ERR_SARADC_WAIT_CB_NOT_REGISTER (BK_ERR_ADC_BASE - 11)", source)
        self.assertNotIn("bk7258_pm_ipc", source)


if __name__ == "__main__":
    unittest.main()
