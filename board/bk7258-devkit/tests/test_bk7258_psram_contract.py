#!/usr/bin/env python3
"""Static regression checks for the BK7258 PSRAM heap contract."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
CHIP = ROOT / "chips" / "bk7258"
BOARD = ROOT / "board" / "bk7258-devkit"


class Bk7258PsramContractTest(unittest.TestCase):
    def test_cp_enables_a_second_heap_region(self):
        defconfig = (BOARD / "configs/cp/defconfig").read_text()
        self.assertIn("CONFIG_BK7258_PSRAM=y", defconfig)
        self.assertIn("CONFIG_MM_REGIONS=2", defconfig)

    def test_driver_publishes_only_after_probe(self):
        source = (CHIP / "bk7258_psram.c").read_text()
        heap = (CHIP / "bk7258_allocateheap.c").read_text()
        self.assertIn("BK7258_PSRAM_APS6408L_ID", source)
        self.assertIn("BK7258_PSRAM_APS128XXO_OB9_ID", source)
        self.assertIn("bk7258_psram_probe(detected_size)", source)
        self.assertLess(
            source.index("bk7258_psram_probe(detected_size)"),
            source.index("*size = detected_size"),
        )
        self.assertIn("up_mdelay(1)", source)
        self.assertLess(
            source.index("up_mdelay(1)"),
            source.index("bk7258_psram_clock_120mhz()"),
        )
        self.assertIn("kumm_addregion((void *)BK7258_PSRAM_BASE", heap)
        self.assertIn("PSRAM unavailable", heap)

    def test_contract_does_not_promise_type_aware_allocation(self):
        kconfig = (CHIP / "Kconfig").read_text()
        self.assertIn("allocation placement is not type-aware", kconfig)
        self.assertNotIn("PSRAM is data heap only", kconfig)

    def test_psram_does_not_change_linker_memory(self):
        linker = (BOARD / "scripts/flash.ld").read_text()
        self.assertNotIn("psram", linker.lower())
        self.assertIn("sram  (rwx) : ORIGIN = CONFIG_RAM_START", linker)

    def test_large_allocation_command_requires_psram_address(self):
        source = (BOARD / "src/bk7258_psram_test.c").read_text()
        cmake = (BOARD / "CMakeLists.txt").read_text()
        self.assertIn("NAME bk7258_psram_test", cmake)
        self.assertIn("if(CONFIG_BK7258_PSRAM)", cmake)
        self.assertIn("BK7258_PSRAM_TEST_DEFAULT_KIB  8192ul", source)
        self.assertIn("malloc(size)", source)
        self.assertIn("bk7258_psram_test_is_mapped", source)
        self.assertIn("is outside ", source)
        self.assertIn('"PSRAM [0x%08"', source)
        self.assertIn("free((void *)memory)", source)


if __name__ == "__main__":
    unittest.main()
