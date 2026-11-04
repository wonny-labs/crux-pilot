"""The shell rule and the serial wiring on real Zephyr builds; enabled by scripts/test-cli.sh --zephyr.

Configuration results only, except for the positive cases, which compile and link. Nothing here runs
an image, flashes a board or resets one. The RT1060 build shows that the sources and the LPUART3
binding compile. It does not show that the wiring works.
"""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]
FIXTURE = REPO / "tests/zephyr/cli_serial_compile"
NATIVE = "native_sim/native/64"
RT1060 = "mimxrt1060_evk@B//qspi"

NETWORK = """
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_NET_TEST=y
CONFIG_NET_DRIVERS=y
CONFIG_NET_SOCKETS=y
"""
# Each refused composition must really hold the symbol, or Kconfig drops it with only a warning and
# the case fails as "unexpectedly accepted".
REFUSED = {
    "management_with_shell": ("CONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_SERIAL=y\n",
                              "must not enable CONFIG_SHELL unless"),
    "marked_with_dummy": ("CONFIG_CRUX_DEVELOPMENT_IMAGE=y\nCONFIG_SHELL=y\n"
                          "CONFIG_SHELL_BACKEND_SERIAL=n\nCONFIG_SHELL_BACKEND_DUMMY=y\n",
                          "may use only the serial shell backend"),
    "telnet": ("CONFIG_CRUX_CLI=n\nCONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_TELNET=y\n" + NETWORK,
               "must not enable CONFIG_SHELL_BACKEND_TELNET"),
    "mqtt": ("CONFIG_CRUX_CLI=n\nCONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_MQTT=y\n" + NETWORK,
             "must not enable CONFIG_SHELL_BACKEND_MQTT"),
    "websocket": ("CONFIG_CRUX_CLI=n\nCONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_WEBSOCKET=y\n"
                  "CONFIG_HTTP_SERVER=y\nCONFIG_HTTP_SERVER_WEBSOCKET=y\n" + NETWORK,
                  "must not enable CONFIG_SHELL_BACKEND_WEBSOCKET"),
    "mcumgr_shell": ("CONFIG_CRUX_CLI=n\nCONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_DUMMY=y\n"
                     "CONFIG_NET_BUF=y\nCONFIG_ZCBOR=y\nCONFIG_CRC=y\nCONFIG_MCUMGR=y\n"
                     "CONFIG_MCUMGR_GRP_SHELL=y\nCONFIG_BASE64=y\n",
                     "must not enable CONFIG_MCUMGR_GRP_SHELL"),
}
# A composition without the rule's symbols must still fail if the check names the wrong thing.
ACCEPTED_NATIVE = {
    "production": ("", True),
    # The shell's own UART is not the console's. A real development image needs its own composition.
    "marked_serial": ("CONFIG_CRUX_DEVELOPMENT_IMAGE=y\nCONFIG_SHELL=y\n"
                      "CONFIG_SHELL_BACKEND_SERIAL=y\n", False),
}


@unittest.skipUnless(os.environ.get("CRUX_CLI_COMPOSITION_TESTS") == "1",
                     "requires pinned Zephyr environment; run scripts/test-cli.sh --zephyr")
class CliCompositionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = REPO / "build/zephyr/cli-composition"
        shutil.rmtree(cls.root, ignore_errors=True)
        cls.root.mkdir(parents=True)
        cls.workspace = Path(os.environ["CRUX_WORKSPACE"])

    def west(self, name, board, config, *flags, modules=str(REPO)):
        conf = self.root / f"{name}.conf"
        conf.write_text(config, encoding="utf-8")
        build = self.root / f"build-{name}"
        command = ["west", "build", *flags, "-o=-j4", "-p", "always", "-b", board, "-d", str(build),
                   str(FIXTURE), "--", f"-DZEPHYR_MODULES={modules}", f"-DEXTRA_CONF_FILE={conf}"]
        result = subprocess.run(command, cwd=self.workspace, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=1800, check=False)
        return build, result

    def check_image(self, build, image):
        command = ["python3", str(REPO / "scripts/check-management-shell.py"),
                   "--config", str(build / "zephyr/.config")]
        if image:
            command += ["--elf", str(build / "zephyr/zephyr.elf")]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False)
        self.assertEqual(result.returncode, 0, result.stdout)
        # The component compiled, including the rule's own file. It is not linked into the image.
        self.assertTrue(list(build.rglob("libcrux_cli_zephyr.a")), "the component was not built")

    def test_refused_compositions(self):
        selected = os.environ.get("CRUX_CLI_COMPOSITION_CASE")
        for name, (config, diagnostic) in REFUSED.items():
            if selected and name != selected:
                continue
            with self.subTest(case=name):
                _, result = self.west(name, NATIVE, config, "--cmake-only")
                self.assertNotEqual(result.returncode, 0,
                                    f"{name}: unexpectedly accepted\n{result.stdout[-3000:]}")
                self.assertIn(diagnostic, result.stdout, f"{name}: unrelated failure")
                print(f"{name}: refused as expected", flush=True)

    def test_accepted_on_native_sim(self):
        selected = os.environ.get("CRUX_CLI_COMPOSITION_CASE")
        for name, (config, image) in ACCEPTED_NATIVE.items():
            if selected and name != selected:
                continue
            with self.subTest(case=name):
                build, result = self.west(name, NATIVE, config)
                self.assertEqual(result.returncode, 0, result.stdout[-6000:])
                self.check_image(build, image)
                print(f"{name}: built and passed the rule", flush=True)

    def test_compiles_for_the_rt1060_with_lpuart3(self):
        selected = os.environ.get("CRUX_CLI_COMPOSITION_CASE")
        if selected and selected != "rt1060":
            self.skipTest("another case was selected")
        listing = subprocess.run(["west", "list", "cmsis", "cmsis_6", "hal_nxp", "-f", "{abspath}"],
                                 cwd=self.workspace, text=True, stdout=subprocess.PIPE, check=True)
        modules = ";".join([*listing.stdout.split(), str(REPO)])
        build, result = self.west("rt1060", RT1060, "", modules=modules)
        self.assertEqual(result.returncode, 0, result.stdout[-6000:])
        self.check_image(build, True)
        # The Serial binding took LPUART3, not the debug UART that belongs to Logging.
        tree = (build / "zephyr/zephyr.dts").read_text(encoding="utf-8")
        self.assertRegex(tree, r"crux-serial0 = &lpuart3;")
        print("rt1060: compiled and linked, not flashed", flush=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
