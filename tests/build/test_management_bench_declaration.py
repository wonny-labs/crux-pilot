"""The bench declaration of the RT1060-EVKB bench image agrees with the files that carry it (C1).

tests/zephyr/management_rt1060_compile/bench_declaration.json is the one declaration: the board, the bench
address, the test forms and the build-local Zephyr copy. This test fails when another file says something else.
The guards themselves are broken one at a time by check_bench_guard.py, which this test also runs when cmake and a C
compiler are present. Nothing here touches a board.
"""
import ipaddress
import json
import os
import re
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tests/zephyr/management_rt1060_compile"
DECLARATION = json.loads((BENCH / "bench_declaration.json").read_text())
sys.path.insert(0, str(ROOT / "tests/target"))
import patched_zephyr  # noqa: E402  (the freeze and the image check that read the block this declaration becomes)
import zephyr_tree  # noqa: E402


def settings(path):
    values = {}
    for line in path.read_text().splitlines():
        match = re.match(r'^CONFIG_([A-Z0-9_]+)=(.*)$', line)
        if match:
            values[match.group(1)] = match.group(2).strip('"')
    return values


class BenchDeclaration(unittest.TestCase):
    def test_network_is_one_consistent_bench(self):
        net = DECLARATION["network"]
        interface = ipaddress.ip_interface(f"{net['address']}/{net['prefix_length']}")
        self.assertEqual(str(interface.network.netmask), net["netmask"])
        self.assertIn(ipaddress.ip_address(net["peer"]), interface.network)
        self.assertNotEqual(net["peer"], net["address"])
        # The M5 image's bench network, which stage 1 proved: the board is .2 and the host is .1.
        self.assertEqual((net["address"], net["peer"], net["prefix_length"]), ("192.168.77.2", "192.168.77.1", 24))

    def test_board_names_agree(self):
        board = DECLARATION["board"]
        self.assertEqual(board["target"], "mimxrt1060_evk@B//qspi")
        self.assertEqual(board["kconfig_target"], "mimxrt1060_evk@B/mimxrt1062/qspi")
        self.assertEqual(board["target"].split("@")[1][0], board["revision"])

    def test_patched_zephyr_matches_the_prepare_script_and_the_manifest(self):
        patched = DECLARATION["patched_zephyr"]
        script = (ROOT / "scripts/prepare-wolf-tls-zephyr.sh").read_text()
        tls = patched["patches"][0]
        tls_file = next(f for f in patched["patched_files"] if f["patches"] == [tls["name"]])
        self.assertIn(f"patch_name={tls['name']}", script)
        self.assertIn(f"patch_sha256={tls['sha256']}", script)
        self.assertIn(f"result_sha256={tls_file['sha256']}", script)
        self.assertIn(patched["marker_file"], script)
        self.assertIn(tls_file["path"], script)
        west = (ROOT / "west.yml").read_text()
        self.assertIn(f"revision: {patched['zephyr_pin']}", west)

    def test_the_fail_closed_initial_sequence_number_patch_matches_its_script_and_its_file(self):
        """Row C11, D-c: the second patch of the build-local copy. The declaration, the script, the patch file and the
        three guard levels name the same patch, hashes and setting."""
        import hashlib
        block = DECLARATION["patched_zephyr"]
        isn, first = block["patches"][1], block["patches"][0]
        script = (ROOT / "scripts/prepare-isn-fail-closed.py").read_text()
        patch = ROOT / "patches/zephyr" / isn["name"]
        entry = next(f for f in block["patched_files"] if f["patches"] == [isn["name"]])
        self.assertEqual(isn["sha256"], hashlib.sha256(patch.read_bytes()).hexdigest())
        self.assertIn(f'PATCH_SHA256 = "{isn["sha256"]}"', script)
        self.assertIn(f'TLS_PATCH_SHA256 = "{first["sha256"]}"', script)
        self.assertIn(f'BASE_SHA256 = "{block["stock_sha256"][entry["path"]]}"', script)
        self.assertIn(f'PATCHED_SHA256 = "{entry["sha256"]}"', script)
        self.assertIn(f'TLS_MARKER = "{block["marker_file"]}"', script)
        self.assertIn(f'RELATIVE_SOURCE = Path("{entry["path"]}")', script)
        self.assertEqual(entry["path"], "subsys/net/ip/tcp.c")
        requirement = next(r for r in block["requires_settings"] if r["patch"] == isn["name"])
        self.assertEqual((requirement["symbol"], requirement["row"], requirement["decision"]), ("NET_TCP_ISN_RFC6528", "C11", "D-c"))
        # The patch changes this one file, and it is not the first patch's file. The parser of the freeze reads the same set.
        self.assertEqual(re.findall(r"^\+\+\+ b/(.*)$", patch.read_text(), re.MULTILINE), [entry["path"]])
        self.assertEqual(zephyr_tree.patch_touched_files(patch.read_bytes()), [(entry["path"], "modified")])
        self.assertNotEqual(entry["path"], next(f["path"] for f in block["patched_files"] if f["patches"] == [first["name"]]))
        # The three levels require the setting, and the image sets it.
        self.assertIn(f"CONFIG_{requirement['symbol']}=y", (BENCH / "target.conf").read_text())
        self.assertIn(f"depends on {requirement['symbol']}", (BENCH / "Kconfig.bench").read_text())
        self.assertIn(f"CONFIG_{requirement['symbol']}", (BENCH / "src/bench_guard.h").read_text())
        self.assertIn("BENCH_REQUIRED_SETTINGS", (BENCH / "bench_guard.cmake").read_text())

    def test_the_patched_zephyr_declaration_is_a_schema_2_list_of_the_design(self):
        """T11 of docs/rt1060-bench-freeze-and-capture-design.md. The block that bench_guard.cmake writes into the manifest
        (the keys of BLOCK_KEYS) is checked by the freeze's own validator, with a stand-in for the one value that is not derived."""
        block = DECLARATION["patched_zephyr"]
        self.assertEqual(block["schema"], 2)
        for old in patched_zephyr.OLD_KEYS + ("isn_fail_closed",):
            self.assertNotIn(old, block)
        self.assertEqual([p["name"] for p in block["patches"]], ["zephyr-tls-4.4.1.patch", "zephyr-isn-fail-closed.patch"])   # application order
        # The declaration holds the keys of the manifest block (zephyr_pin is the block's pin) and the keys only the guard and the tests read.
        declaration_only = {"zephyr_pin", "stock_sha256", "requires_settings", "open_items"}
        self.assertEqual(set(block) - declaration_only, patched_zephyr.BLOCK_KEYS - {"pin"})
        mirrored = {key: block[key] for key in patched_zephyr.BLOCK_KEYS - {"pin"}}
        mirrored["pin"] = block["zephyr_pin"]
        # The tree digest is not derived yet, on purpose: the validator refuses the block as it stands, and for that reason alone.
        self.assertEqual(block["tree"], {"algorithm": zephyr_tree.ALGORITHM, "sha256": None, "file_count": None})
        with self.assertRaisesRegex(ValueError, "tree must be"):
            patched_zephyr.validate_block(mirrored)
        mirrored["tree"] = dict(mirrored["tree"], sha256="a" * 64, file_count=1)
        self.assertIs(patched_zephyr.validate_block(mirrored), mirrored)
        # What is open is said in the declaration, and the image check cannot pass it by accident.
        text = " ".join(block["open_items"])
        self.assertIn("null on purpose", text)
        self.assertIn("not in this repository", text)

    def test_the_marker_of_the_declaration_is_the_one_marker_the_two_scripts_write(self):
        block = DECLARATION["patched_zephyr"]
        hashes = [p["sha256"] for p in block["patches"]]
        self.assertEqual(zephyr_tree.marker_text(block["zephyr_pin"], hashes[:1]).rstrip("\n"), f"{block['zephyr_pin']} {hashes[0]}")   # the TLS script
        self.assertEqual(zephyr_tree.marker_text(block["zephyr_pin"], hashes), f"{block['zephyr_pin']} {hashes[0]} {hashes[1]}\n")        # after this script
        guard = (BENCH / "bench_guard.cmake").read_text()
        self.assertIn("expected_marker", guard)

    def test_every_patched_file_names_declared_patches_in_order_and_the_list_is_sorted(self):
        block = DECLARATION["patched_zephyr"]
        names = [p["name"] for p in block["patches"]]
        paths = [f["path"] for f in block["patched_files"]]
        self.assertEqual(paths, sorted(set(paths), key=lambda path: path.encode()))
        for entry in block["patched_files"]:
            self.assertTrue(entry["patches"] and all(n in names for n in entry["patches"]))
            self.assertEqual(entry["patches"], [n for n in names if n in entry["patches"]])
            self.assertNotEqual(entry["path"], block["marker_file"])

    def test_target_conf_carries_the_declaration(self):
        values = settings(BENCH / "target.conf")
        net = DECLARATION["network"]
        self.assertEqual(values["CRUX_MANAGEMENT_BENCH_IMAGE"], "y")
        self.assertEqual(values["CRUX_WEB_BIND_ADDRESS"], net["address"])
        self.assertEqual(values["CRUX_SSH_BIND_ADDRESS"], net["address"])
        self.assertEqual(int(values["CRUX_SSH_PORT"]), DECLARATION["listeners"]["ssh_port"])
        for name, form in DECLARATION["test_forms"].items():
            if form["state"] == "selected":
                self.assertEqual(values.get(form["symbol"]), "y", name)
            else:
                self.assertNotIn(form["symbol"], values, f"{name} is reserved for row {form.get('row')}")
        for forbidden in ("TEST_RANDOM_GENERATOR", "FAKE_ENTROPY_NATIVE_SIM", "NET_DHCPV4", "NET_CONFIG_SETTINGS",
                          "NET_IPV6", "RTC", "FS_FATFS_MOUNT_MKFS", "CRUX_WEB_PLAIN_HTTP_FOR_TESTS"):
            self.assertIn(values.get(forbidden, "n"), ("n", ""), forbidden)

    def test_wolfcrypt_is_built_without_rsa_and_the_declaration_says_so(self):
        # Row C9, D11: the declared profile (TLS 1.3 with P-256, SSH with Ed25519 and ecdsa-sha2-nistp256, ECDSA and
        # Ed25519 stored keys) uses no RSA. One module option drops it from wolfCrypt, wolfSSL, the single-precision
        # math and wolfPSA together, and the three guard levels refuse the option and anything that would hide it.
        rsa = DECLARATION["profile"]["wolfssl_rsa"]
        self.assertEqual((rsa["symbol"], rsa["decision"], rsa["row"], rsa["state"]), ("WOLFSSL_RSA", "D11", "C9", "off"))
        values = settings(BENCH / "target.conf")
        self.assertEqual(values.get(rsa["symbol"]), "n")
        kconfig = (BENCH / "Kconfig.bench").read_text()
        head = kconfig[kconfig.index("config CRUX_MANAGEMENT_BENCH_IMAGE"):kconfig.index("if CRUX_MANAGEMENT_BENCH_IMAGE")]
        self.assertIn(f"depends on !{rsa['symbol']}", head)
        self.assertIn("!WOLFSSL_HAS_SETTINGS_FILE", head)
        guard_h = (BENCH / "src/bench_guard.h").read_text()
        self.assertIn(f"defined(CONFIG_{rsa['symbol']})", guard_h)
        guard_cmake = (BENCH / "bench_guard.cmake").read_text()
        self.assertIn("profile wolfssl_rsa symbol", guard_cmake)

    def test_the_linked_image_is_checked_for_rsa_symbols(self):
        # The knob is checked at three levels. The linked image is checked too, because a module update or another
        # definition could bring RSA back with the knob still off.
        cmake = (BENCH / "CMakeLists.txt").read_text()
        self.assertIn("check_bench_rsa_symbols.py", cmake)
        self.assertIn("add_dependencies(bench_rsa_symbols zephyr_final)", cmake)
        self.assertTrue((BENCH / "check_bench_rsa_symbols.py").is_file())

    def test_http_server_stack_starts_at_8192_and_the_declaration_does_not_pin_it(self):
        # The owner asked for 8 KB from the start (2026-11-08). Row C2 may raise it, so nothing may cap it.
        values = settings(BENCH / "target.conf")
        self.assertGreaterEqual(int(values["HTTP_SERVER_STACK_SIZE"]), 8192)
        self.assertNotIn("STACK", (BENCH / "bench_declaration.json").read_text().upper())
        self.assertNotIn("HTTP_SERVER_STACK_SIZE", (BENCH / "bench_guard.cmake").read_text().split("function(crux_bench_manifest")[0])

    def test_network_buffers_and_arena_meet_the_declared_load_of_row_c6(self):
        # The rule is in bench_guard.cmake and again in check_bench_guard.py. These are the figures worked by hand in
        # docs/rt1060-bench-network-buffers.md for D9's sessions, so a change to either copy of the rule shows here.
        sys.path.insert(0, str(BENCH))
        try:
            import check_bench_guard  # noqa: PLC0415
        finally:
            sys.path.remove(str(BENCH))
        values = settings(BENCH / "target.conf")
        needs = check_bench_guard.buffer_needs(DECLARATION, values)
        self.assertEqual(needs, {"window": 5840, "ring_min": 6, "rx_bufs": 915, "tx_bufs": 517, "rx_pkts": 173,
                                 "tx_pkts": 18, "paper_peak": 347104, "arena": 694208})
        self.assertEqual(int(values["NET_TCP_MAX_RECV_WINDOW_SIZE"]), needs["window"])
        self.assertEqual(int(values["NET_TCP_MAX_SEND_WINDOW_SIZE"]), needs["window"])
        self.assertGreaterEqual(int(values["ETH_NXP_ENET_RX_BUFFERS"]), needs["ring_min"])
        for symbol, key in (("NET_BUF_RX_COUNT", "rx_bufs"), ("NET_BUF_TX_COUNT", "tx_bufs"),
                            ("NET_PKT_RX_COUNT", "rx_pkts"), ("NET_PKT_TX_COUNT", "tx_pkts"),
                            ("COMMON_LIBC_MALLOC_ARENA_SIZE", "arena")):
            self.assertGreaterEqual(int(values[symbol]), needs[key], symbol)
        # D9 is not changed by this row: 4 SSH connections, 3 HTTPS clients, the SSH window at the provider's 16 KiB.
        self.assertEqual((int(values["CRUX_SSH_MAX_CONNECTIONS"]), int(values["HTTP_SERVER_MAX_CLIENTS"]),
                          int(values["CRUX_SSH_WINDOW_BYTES"]), int(values["CRUX_WEB_BACKLOG"])), (4, 3, 16384, 4))
        self.assertEqual(int(values["NET_BUF_DATA_SIZE"]), 128)
        # The window stays under what the driver's ring can take, which is the reason for it.
        self.assertLessEqual(DECLARATION["network_buffers"]["window_segments"] + DECLARATION["network_buffers"]["ring_spare_frames"],
                             int(values["ETH_NXP_ENET_RX_BUFFERS"]))

    def test_kconfig_names_the_declared_test_forms_and_each_depends_on_the_declaration(self):
        text = (BENCH / "Kconfig.bench").read_text()
        symbols = set(re.findall(r"^config (CRUX_BENCH_[A-Z0-9_]+)", text, re.MULTILINE))
        self.assertEqual(symbols, {form["symbol"] for form in DECLARATION["test_forms"].values()})
        declared_block = text[text.index("if CRUX_MANAGEMENT_BENCH_IMAGE"):text.index("endif")]
        for symbol in symbols:
            self.assertIn(f"config {symbol}", declared_block, f"{symbol} must sit under the declaration")

    def test_the_certificate_names_the_bench_address(self):
        text = (BENCH / "src/mgmt_services.c").read_text()
        octets = ", ".join(DECLARATION["network"]["address"].split("."))
        self.assertIn("{{" + octets + "}}", text)

    def test_the_public_test_password_and_the_native_declaration_are_not_in_the_bench_image(self):
        for path in sorted(BENCH.rglob("*")):
            if not path.is_file() or path.name in ("check_bench_guard.py",) or "__pycache__" in path.parts:
                continue
            text = path.read_text()
            self.assertNotIn("CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD", text, path.name)
            self.assertNotIn("provision_test_account", text, path.name)
            self.assertNotIn("composition test password", text, path.name)
            self.assertNotIn("CRUX_NATIVE_MANAGEMENT_TEST_IMAGE", text, path.name)

    def test_the_refusal_in_main_stays_until_a_reviewed_change_removes_it(self):
        # Rows C2 to C9 and the approval of the board stage are not done. The switch is one line in CMakeLists.txt,
        # and changing it must change this test in the same review.
        self.assertIn("set(CRUX_BENCH_RUN_REFUSED ON)", (BENCH / "CMakeLists.txt").read_text())
        main = (BENCH / "src/main.c").read_text()
        self.assertIn("if (run_refused)", main)
        # The refusal stops everything but the banner and the entropy line (row C10): no address, store, listener.
        body = main[main.index("static int bench_main(void)"):]
        refusal = body.index("if (run_refused)")
        for later in ("static_address_up()", "crux_native_management_start()", "crux_bench_first_account()",
                      "crux_native_management_services_start()", "crux_mgmt_rt1060_ssh_start()", "crux_bench_report_start()"):
            self.assertLess(refusal, body.index(later), later)
        refused = body[refusal:body.index("}", refusal)]
        self.assertIn("return 1;", refused)

    def test_both_listeners_are_bound_by_the_product_code_only_when_asked(self):
        web = (ROOT / "components/web/ports/zephyr/web_http_server.c").read_text()
        self.assertIn("CRUX_WEB_HOST", web)
        self.assertIn("sizeof(CONFIG_CRUX_WEB_BIND_ADDRESS) > 1u ? CONFIG_CRUX_WEB_BIND_ADDRESS : NULL", web)
        kconfig = (ROOT / "zephyr/Kconfig").read_text()
        for symbol in ("CRUX_WEB_BIND_ADDRESS", "CRUX_SSH_BIND_ADDRESS"):
            block = kconfig[kconfig.index(f"config {symbol}"):]
            block = block[:block.index("\nconfig ", 5)]
            self.assertIn('default ""', block, f"{symbol} must keep every address by default")


@unittest.skipUnless(shutil.which("cmake") and shutil.which("cc"), "needs cmake and a C compiler")
class BenchGuardBreaks(unittest.TestCase):
    def test_every_guard_refuses_and_every_mutant_is_killed(self):
        result = subprocess.run([sys.executable, str(BENCH / "check_bench_guard.py"), "--skip-kconfig"],
                                capture_output=True, text=True, check=False, env=dict(os.environ))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("control ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
