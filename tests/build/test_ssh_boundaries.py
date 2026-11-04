"""The SSH boundary check must reject each violation, on a copy of the files it reads."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / "scripts/check-ssh-boundaries.py"
COPIED = (
    "CMakeLists.txt",
    "west.yml",
    "components/ssh",
    "tests/ssh",
    ".github/workflows",
)


class SshBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for name in COPIED:
            source = ROOT / name
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, target)
            else:
                shutil.copy2(source, target)

    def run_check(self):
        return subprocess.run([sys.executable, str(CHECK), "--root", str(self.root)],
                              capture_output=True, text=True, check=False)

    def assert_rejected(self, expected):
        result = self.run_check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(expected, result.stderr)

    def edit(self, name, old, new):
        path = self.root / name
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text)
        path.write_text(text.replace(old, new), encoding="utf-8")

    def add_to_interface(self, code):
        self.edit("components/ssh/src/crux_ssh_provider.h", "#endif /* CRUX_SSH_PROVIDER_H */",
                  code + "\n#endif /* CRUX_SSH_PROVIDER_H */")

    def test_copy_of_the_repository_passes(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_library_type_in_the_interface(self):
        self.add_to_interface("typedef struct WOLFSSH crux_ssh_native_t;")
        self.assert_rejected("forbidden type or name WOLFSSH")

    def test_library_word_type_in_the_interface(self):
        self.add_to_interface("void crux_ssh_size(word32 size);")
        self.assert_rejected("forbidden type or name word32")

    def test_access_type_in_the_interface(self):
        self.add_to_interface("typedef crux_access_session_t crux_ssh_session_t;")
        self.assert_rejected("forbidden type or name crux_access_session_t")

    def test_socket_address_in_the_interface(self):
        self.add_to_interface("struct sockaddr *crux_ssh_address(void);")
        self.assert_rejected("forbidden type or name sockaddr")

    def test_zephyr_socket_address_in_the_interface(self):
        self.add_to_interface("struct net_sockaddr *crux_ssh_address(void);")
        self.assert_rejected("forbidden type or name net_sockaddr")

    def test_access_type_in_the_table(self):
        with (self.root / "components/ssh/src/crux_ssh_callbacks.h").open("a", encoding="utf-8") as stream:
            stream.write("crux_access_session_t crux_ssh_session(void);\n")
        self.assert_rejected("crux_ssh_callbacks.h: forbidden type or name crux_access_session_t")

    def test_other_header_in_the_accessor(self):
        with (self.root / "components/ssh/adapter/crux_ssh_attachment.h").open("a", encoding="utf-8") as stream:
            stream.write("#include <crux/file.h>\n")
        self.assert_rejected("unexpected include crux/file.h")

    def test_sample_includes_the_accessor(self):
        sample = self.root / "samples/controller/main.c"
        sample.parent.mkdir(parents=True)
        sample.write_text('#include "crux_ssh_attachment.h"\n', encoding="utf-8")
        self.assert_rejected("a consumer must not include the interface")

    def test_zephyr_header_in_the_interface(self):
        self.add_to_interface("#include <zephyr/kernel.h>")
        self.assert_rejected("zephyr/kernel.h")

    def test_other_header_in_the_interface(self):
        self.add_to_interface("#include <string.h>")
        self.assert_rejected("unexpected include string.h")

    def test_library_type_in_the_fake(self):
        with (self.root / "tests/ssh/fake_provider.c").open("a", encoding="utf-8") as stream:
            stream.write("static WOLFSSH *session;\n")
        self.assert_rejected("tests/ssh/fake_provider.c: forbidden type or name WOLFSSH")

    def test_public_header_directory(self):
        (self.root / "components/ssh/include/crux").mkdir(parents=True)
        self.assert_rejected("SSH must have no public header")

    def test_public_target(self):
        with (self.root / "components/ssh/CMakeLists.txt").open("a", encoding="utf-8") as stream:
            stream.write("add_library(crux::ssh ALIAS crux_ssh_provider)\n")
        self.assert_rejected("SSH must have no crux:: target")

    def test_public_component_list(self):
        self.edit("CMakeLists.txt", "i2c spi access", "i2c spi ssh access")
        self.assert_rejected("ssh must not join the public component list")

    def test_fake_outside_the_test_build(self):
        self.edit("CMakeLists.txt", "        add_subdirectory(tests/ssh)\n", "")
        self.assert_rejected("tests/ssh must be added only under CRUX_BUILD_TESTS")

    def test_sample_includes_the_interface(self):
        sample = self.root / "samples/controller/main.c"
        sample.parent.mkdir(parents=True)
        sample.write_text('#include "crux_ssh_provider.h"\n', encoding="utf-8")
        self.assert_rejected("a consumer must not include the interface")

    def test_sample_links_an_ssh_target(self):
        sample = self.root / "samples/controller/CMakeLists.txt"
        sample.parent.mkdir(parents=True)
        sample.write_text("target_link_libraries(app PRIVATE crux_ssh_provider)\n", encoding="utf-8")
        self.assert_rejected("a consumer must not link an SSH target")

    def test_component_passes_the_interface_on(self):
        build = self.root / "components/web/CMakeLists.txt"
        build.parent.mkdir(parents=True)
        build.write_text("target_link_libraries(crux_web PUBLIC crux_ssh_provider)\n", encoding="utf-8")
        self.assert_rejected("link SSH targets PRIVATE")

    def test_settings_file_option(self):
        with (self.root / "components/ssh/ports/zephyr/wolf-ssh.conf").open("a", encoding="utf-8") as stream:
            stream.write('CONFIG_WOLFSSL_SETTINGS_FILE="mine.h"\n')
        self.assert_rejected("rejects a wolfSSL settings file")

    def test_group_on_by_default(self):
        self.edit("west.yml", "group-filter: [-crux-secure-management]", "group-filter: []")
        self.assert_rejected("must stay off by default")

    def test_default_workflow_selects_the_stacks(self):
        workflow = self.root / ".github/workflows/added.yml"
        workflow.write_text("run: west config manifest.group-filter +crux-secure-management\n",
                            encoding="utf-8")
        self.assert_rejected("default workflow names crux-secure-management")


if __name__ == "__main__":
    unittest.main()
