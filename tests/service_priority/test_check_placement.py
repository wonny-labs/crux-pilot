#!/usr/bin/env python3
"""The placement check passes on the tree and catches each defect it is there for."""
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SPEC = importlib.util.spec_from_file_location("check_placement", HERE / "check_placement.py")
check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check)


# The Zephyr CMake files that compile a worker today, which link the placement's interface.
WIRED = [ROOT / name for name in (
    "tests/native/management_composition/composition/composition.cmake",
    "tests/native/management_composition/composition/services.cmake",
    "tests/native/web_session/CMakeLists.txt",
    "tests/zephyr/management_rt1060_compile/management_target.cmake",
    "tests/zephyr/web_compile/CMakeLists.txt",
)]


class PlacementCheck(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        files = [source for source, _, _ in check.WORKERS]
        files += [check.PLACEMENT, check.SSH_PROVIDER, check.SSH_SETTINGS, "zephyr/Kconfig"]
        files += [path.relative_to(ROOT).as_posix() for path in WIRED]
        for name in files:
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, self.root / name)

    def tearDown(self):
        self.scratch.cleanup()

    def edit(self, name, old, new):
        path = self.root / name
        text = path.read_text()
        self.assertIn(old, text, f"{name} no longer has the text this case edits")
        path.write_text(text.replace(old, new, 1))

    def assert_caught(self, fragment):
        found = check.errors(self.root)
        self.assertTrue(any(fragment in error for error in found), found)

    def test_the_tree_passes(self):
        self.assertEqual(check.errors(ROOT), [])

    def test_the_copy_passes(self):
        self.assertEqual(check.errors(self.root), [])

    def test_a_missing_placement(self):
        self.edit("components/reboot/src/reboot.c",
                  "crux_service_priority_place(CRUX_SERVICE_THREAD_REBOOT_WORKER);", "")
        self.assert_caught("reboot.c: worker_main must begin with")

    def test_a_placement_after_other_work(self):
        self.edit("components/network_settings/src/network_settings.c",
                  "    for (;;) {\n        (void)crux_semaphore_take(work_signal, CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS);",
                  "    for (;;) {\n        (void)crux_semaphore_take(work_signal, CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS);\n"
                  "        crux_service_priority_place(CRUX_SERVICE_THREAD_REBOOT_WORKER);")
        self.edit("components/network_settings/src/network_settings.c",
                  "    crux_service_priority_place(CRUX_SERVICE_THREAD_NETWORK_SETTINGS_WORKER);\n", "")
        self.assert_caught("network_settings.c: worker_main must begin with")

    def test_the_wrong_identifier(self):
        self.edit("components/web/session/web_session_attempt.c",
                  "CRUX_SERVICE_THREAD_WEB_SESSION_WORKER", "CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER")
        self.assert_caught("web_session_attempt.c: worker_main must begin with")

    def test_a_changed_default(self):
        text = (self.root / "zephyr/Kconfig").read_text()
        start = text.index("config CRUX_REBOOT_WORKER_PRIORITY")
        end = text.index("default 6", start)
        (self.root / "zephyr/Kconfig").write_text(text[:end] + "default 5" + text[end + len("default 6"):])
        self.assert_caught("CRUX_REBOOT_WORKER_PRIORITY must default to 6")

    def test_a_wrong_map(self):
        self.edit(check.PLACEMENT, "[CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER] = CONFIG_CRUX_WEB_MANAGEMENT_WORKER_PRIORITY",
                  "[CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER] = CONFIG_CRUX_WEB_SESSION_WORKER_PRIORITY")
        self.assert_caught("CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER must map to")

    def test_an_unplaced_worker(self):
        extra = self.root / "components/example/src/example.c"
        extra.parent.mkdir(parents=True)
        extra.write_text("void start(void) { (void)crux_thread_create(&options, run, 0, &thread); }\n")
        self.assert_caught("example.c: creates a Crux Thread worker")

    def test_an_unplaced_native_thread(self):
        extra = self.root / "components/example/ports/zephyr/example.c"
        extra.parent.mkdir(parents=True)
        extra.write_text("K_THREAD_DEFINE(example, 1024, run, NULL, NULL, NULL, 7, 0, 0);\n")
        self.assert_caught("example.c: creates a native thread")

    def test_a_composition_without_the_placement(self):
        self.edit("tests/zephyr/web_compile/CMakeLists.txt",
                  "zephyr_interface crux_service_priority_placement)", "zephyr_interface)")
        self.assert_caught("tests/zephyr/web_compile/CMakeLists.txt: compiles a service worker for Zephyr")

    def test_a_new_zephyr_image_without_the_placement(self):
        extra = self.root / "samples/example/CMakeLists.txt"
        extra.parent.mkdir(parents=True)
        extra.write_text("find_package(Zephyr REQUIRED)\ntarget_sources(app PRIVATE ${CRUX_REBOOT_SOURCES})\n")
        self.assert_caught("samples/example/CMakeLists.txt: compiles a service worker for Zephyr")

    def test_a_host_test_needs_no_placement(self):
        extra = self.root / "tests/example/CMakeLists.txt"
        extra.parent.mkdir(parents=True)
        extra.write_text("add_library(example STATIC ${CRUX_REBOOT_SOURCES})\n")
        self.assertEqual(check.errors(self.root), [])

    def test_a_native_thread_in_the_module(self):
        extra = self.root / "zephyr/example.c"
        extra.write_text("K_THREAD_DEFINE(example, 1024, run, NULL, NULL, NULL, 7, 0, 0);\n")
        self.assert_caught("zephyr/example.c: creates a native thread")

    def test_an_ssh_thread_at_a_fixed_priority(self):
        self.edit(check.SSH_PROVIDER, "CRUX_SSH_WOLF_WORKER_PRIORITY, 0, K_NO_WAIT",
                  "K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT")
        self.assert_caught("worker_stacks must be created at CRUX_SSH_WOLF_WORKER_PRIORITY")

    def test_an_ssh_setting_not_applied(self):
        self.edit(check.SSH_SETTINGS, "CRUX_SSH_WOLF_LISTENER_PRIORITY=${CONFIG_CRUX_SSH_LISTENER_PRIORITY}", "")
        self.assert_caught("CRUX_SSH_WOLF_LISTENER_PRIORITY must come from CONFIG_CRUX_SSH_LISTENER_PRIORITY")


if __name__ == "__main__":
    unittest.main()
