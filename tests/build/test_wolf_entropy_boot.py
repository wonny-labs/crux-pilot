"""The boot check of the random source is wired into the RT1060-EVKB bench image where it must be (Row C5).

tests/zephyr/entropy_boot makes the first random read and prints one line in a fixed format. The bench image
cannot run, so the order in its main() is pinned here: the banner, then the refusal that stands in the image, then the
check, then everything that makes a key or opens a listener. The check's own behaviour is tested on native_sim by
scripts/test-wolf-entropy.sh. Nothing here touches a board.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tests/zephyr/management_rt1060_compile"
BOOT = ROOT / "tests/zephyr/entropy_boot"
MAIN = (BENCH / "src/main.c").read_text()
TARGET = (BENCH / "management_target.cmake").read_text()
SOURCE = (BOOT / "entropy_boot.c").read_text()
HEADER = (BOOT / "entropy_boot.h").read_text()


def main_body():
    return MAIN[MAIN.index("int main(void)"):]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


class BenchWiring(unittest.TestCase):
    def test_the_check_follows_the_refusal_and_precedes_every_key_and_listener(self):
        body = strip_comments(main_body())
        order = ["banner();", "if (run_refused)", "crux_entropy_boot_check()", "static_address_up()",
                 "crux_native_management_start()", "crux_native_management_services_start()",
                 "crux_mgmt_rt1060_ssh_start()"]
        positions = [body.find(item) for item in order]
        self.assertTrue(all(position >= 0 for position in positions), dict(zip(order, positions)))
        self.assertEqual(positions, sorted(positions), "the boot check must come before anything that makes a key")
        self.assertEqual(body.count("crux_entropy_boot_check()"), 1, "printed once at boot")

    def test_a_failed_read_prints_a_line_and_stops(self):
        body = strip_comments(main_body())
        match = re.search(r"if \(!crux_entropy_boot_check\(\)\) \{(.*?)\}", body, flags=re.S)
        self.assertIsNotNone(match, "no refusal after the boot check")
        self.assertIn('printk("BENCH refusing to start:', match.group(1))
        self.assertIn("return 1;", match.group(1))

    def test_the_bench_image_compiles_the_check(self):
        self.assertIn("${CRUX_ROOT}/tests/zephyr/entropy_boot/entropy_boot.c", TARGET)
        self.assertIn("${CRUX_ROOT}/tests/zephyr/entropy_boot", TARGET)
        self.assertIn('#include "entropy_boot.h"', MAIN)


class BootCheck(unittest.TestCase):
    def test_a_composition_where_wolfcrypt_would_call_sys_rand_get_does_not_compile(self):
        code = strip_comments(SOURCE)
        self.assertRegex(code, r"#if !\(defined\(CONFIG_ENTROPY_HAS_DRIVER\) && DT_HAS_CHOSEN\(zephyr_entropy\)\)\s*#error")
        self.assertRegex(code, r"BUILD_ASSERT\(!IS_ENABLED\(CONFIG_TEST_RANDOM_GENERATOR\)")
        self.assertEqual(SOURCE.count("wolfCrypt would seed from sys_rand_get"), 2)

    def test_the_check_reads_through_psa_only(self):
        code = re.sub(r'"(?:[^"\\]|\\.)*"', '""', strip_comments(SOURCE))
        self.assertIn("psa_generate_random(", code)
        for forbidden in ("sys_rand_get", "sys_csrand_get", "entropy_get_entropy", "sys_rand32_get"):
            self.assertNotIn(forbidden, code, "the check must use the function that makes keys, not a side door")

    def test_the_line_has_the_fixed_format_of_the_header_and_the_script(self):
        fields = ["first-read=", "source=", "compatible=", "bytes=", "status=", "device-ready="]
        # The format is a run of adjacent string literals: join them.
        format_line = "".join(re.findall(r'"([^"]*)"', SOURCE[SOURCE.index('"CRUX-ENTROPY first-read=%s'):].split(",")[0]))
        positions = [format_line.find(field) for field in fields]
        self.assertTrue(all(position >= 0 for position in positions), format_line)
        self.assertEqual(positions, sorted(positions))
        # The fallback line, for a line that cannot be formatted, has the same fields in the same order.
        fallback = SOURCE[SOURCE.index('"CRUX-ENTROPY first-read=failed'):]
        fallback = "".join(re.findall(r'"([^"]*)"', fallback.split(");")[0]))
        self.assertEqual([fallback.find(field) for field in fields], sorted(fallback.find(field) for field in fields))
        self.assertTrue(all(fallback.find(field) >= 0 for field in fields), fallback)
        script = (ROOT / "scripts/test-wolf-entropy.sh").read_text()
        for field in fields:
            self.assertIn(field, script)
            self.assertIn(field, HEADER)


if __name__ == "__main__":
    unittest.main()
