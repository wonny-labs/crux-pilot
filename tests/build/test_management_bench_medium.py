"""The mount and format rule of the RT1060-EVKB bench image (row C3, decision D2): a card is never formatted.

The bench image keeps its file service on a RAM disk for the first board run. Its mount rule has one decision
core (src/bench_medium.c) and one piece of Zephyr glue (src/bench_medium_fs.c), the only source of the image
that may call fs_mkfs, and only for the RAM disk. This test proves it from four sides, on the host, with fake
devices: the decision core over every kind of medium and every mount error, the glue built once for each
backend over a fake file system layer, the object of the card glue (it has no reference to fs_mkfs at all),
and a scan of every source of the image. Nothing here touches a board, a card or a card reader.
"""
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tests/zephyr/management_rt1060_compile"
HOST = BENCH / "host"
CC = shutil.which("cc") or shutil.which("gcc")
NM = shutil.which("nm")

spec = importlib.util.spec_from_file_location("check_bench_guard", BENCH / "check_bench_guard.py")
guard = importlib.util.module_from_spec(spec)
sys.modules["check_bench_guard"] = guard
spec.loader.exec_module(guard)

spec = importlib.util.spec_from_file_location("check_mkfs_callers", BENCH / "check_mkfs_callers.py")
callers = importlib.util.module_from_spec(spec)
sys.modules["check_mkfs_callers"] = callers
spec.loader.exec_module(callers)


NO_RAM_DISK = {"CONFIG_CRUX_BENCH_FILE_RAM_DISK": None, "CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": None,
               "CONFIG_DISK_DRIVER_RAM": None}


def definitions(backend="ram", **extra):
    """The compile definitions of a bench image, as the guards want them: the RAM disk backend of the declaration,
    or `backend="card"` for the microSD state that a later run takes, or "none"."""
    good = guard.good_c_definitions(guard.Tree(BENCH))
    if backend == "card":
        values = guard.card_definitions(good)
    elif backend == "none":
        values = dict(good, **NO_RAM_DISK)
    else:
        values = dict(good)
    values.update(extra)
    return [f"-D{name}={value}" for name, value in values.items() if value is not None]


def compile_c(sources, output, flags, shim=False, compile_only=False):
    command = [CC, "-std=c17", "-Wall", "-Wextra", "-Werror", f"-I{BENCH / 'src'}"]
    if shim:
        command.append(f"-I{HOST / 'shim'}")
    command += flags
    command += ["-c"] if compile_only else []
    command += ["-o", str(output)] + [str(s) for s in sources]
    return subprocess.run(command, capture_output=True, text=True, check=False)


def comment_free(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


@unittest.skipUnless(CC, "needs a C compiler")
class BenchMedium(unittest.TestCase):
    def build_and_run(self, sources, flags, shim=False):
        with tempfile.TemporaryDirectory() as scratch:
            exe = Path(scratch) / "t"
            built = compile_c(sources, exe, flags, shim)
            self.assertEqual(built.returncode, 0, built.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True, check=False)

    def test_decision_core_never_formats_a_card_and_formats_a_ram_disk_only(self):
        result = self.build_and_run([HOST / "test_bench_medium.c", BENCH / "src/bench_medium.c"], definitions())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all checks passed", result.stdout)

    def test_decision_core_test_fails_when_the_core_formats_a_card(self):
        # Control: the test can see the fault it exists for. A copy of the core that formats on any kind fails it.
        text = (BENCH / "src/bench_medium.c").read_text()
        self.assertIn("BENCH_MEDIUM_RAM_DISK", text)
        weakened = re.sub(r"kind\s*!=\s*BENCH_MEDIUM_RAM_DISK", "0", text)
        weakened = weakened.replace("kind == BENCH_MEDIUM_RAM_DISK", "1")
        self.assertNotEqual(weakened, text, "the control found nothing to weaken")
        with tempfile.TemporaryDirectory() as scratch:
            mutated = Path(scratch) / "bench_medium.c"
            mutated.write_text(weakened)
            result = self.build_and_run([HOST / "test_bench_medium.c", mutated], definitions() + [f"-I{BENCH / 'src'}"])
            self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_glue_on_a_ram_disk_formats_only_the_ram_disk(self):
        result = self.build_and_run([HOST / "test_bench_medium_fs.c", BENCH / "src/bench_medium.c"],
                                    definitions("ram"), shim=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all checks passed", result.stdout)
        self.assertIn("nothing persists", result.stdout)

    def test_glue_on_a_card_never_formats(self):
        result = self.build_and_run([HOST / "test_bench_medium_fs.c", BENCH / "src/bench_medium.c"],
                                    definitions("card"), shim=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all checks passed", result.stdout)

    def test_glue_refuses_no_backend_and_both_backends(self):
        for name, flags in (("none", definitions("none")),
                            ("both", definitions("ram", CONFIG_CRUX_BENCH_FILE_MICROSD="1"))):
            with self.subTest(backends=name), tempfile.TemporaryDirectory() as scratch:
                built = compile_c([BENCH / "src/bench_medium_fs.c"], Path(scratch) / "o.o", flags,
                                  shim=True, compile_only=True)
                self.assertNotEqual(built.returncode, 0)
                self.assertIn("exactly one", built.stderr)

    @unittest.skipUnless(NM, "needs nm")
    def test_the_card_glue_object_has_no_reference_to_fs_mkfs_and_the_ram_glue_has_one(self):
        with tempfile.TemporaryDirectory() as scratch:
            objects = {}
            for name in ("ram", "card"):
                objects[name] = Path(scratch) / f"{name}.o"
                built = compile_c([BENCH / "src/bench_medium_fs.c"], objects[name], definitions(name),
                                  shim=True, compile_only=True)
                self.assertEqual(built.returncode, 0, built.stderr)
            self.assertIn("fs_mkfs", callers.undefined_references(NM, objects["ram"]))
            self.assertNotIn("fs_mkfs", callers.undefined_references(NM, objects["card"]))
            self.assertNotIn("f_mkfs", callers.undefined_references(NM, objects["card"]))

    @unittest.skipUnless(NM, "needs nm")
    def test_the_caller_check_finds_a_second_caller(self):
        # Control for check_mkfs_callers.py: the card glue alone passes, and an extra object that calls fs_mkfs fails.
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)
            glue = directory / "bench_medium_fs.c.obj"
            self.assertEqual(compile_c([BENCH / "src/bench_medium_fs.c"], glue, definitions("ram"), shim=True,
                                       compile_only=True).returncode, 0)
            self.assertEqual(callers.check(NM, directory, "ram"), [])
            stray_source = directory / "stray.c"
            stray_source.write_text("#include <stdint.h>\nint fs_mkfs(int, uintptr_t, void *, int);\n"
                                    "int stray(void) { return fs_mkfs(1, 0, 0, 0); }\n")
            subprocess.run([CC, "-c", "-o", str(directory / "stray.c.obj"), str(stray_source)], check=True)
            problems = callers.check(NM, directory, "ram")
            self.assertTrue(any("stray.c.obj" in p for p in problems), problems)
            # And the card image: any object that calls fs_mkfs, the glue included, is a failure.
            self.assertTrue(callers.check(NM, directory, "card"))


    @unittest.skipUnless(NM, "needs nm")
    def test_each_barrier_stands_alone(self):
        # Two barriers keep a card from a format: the decision core (a card is never passed to a format) and the glue
        # (a card build has no format function and no reference to fs_mkfs). Weaken one at a time: the layer under
        # test must still hold, and the other layer's check must see the weakening.
        glue_text = (BENCH / "src/bench_medium_fs.c").read_text()
        weak_glue = glue_text.replace("#define BENCH_MEDIUM_FORMAT NULL", """static int format_card(void *context)
{
    (void)context;
    return fs_mkfs(FS_FATFS, (uintptr_t)"SD:", NULL, 0);
}
#define BENCH_MEDIUM_FORMAT format_card""")
        self.assertNotEqual(weak_glue, glue_text)
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)
            weak = directory / "bench_medium_fs.c"
            weak.write_text(weak_glue)
            flags = definitions("card") + [f"-I{BENCH / 'src'}"]
            # 1. A card glue that offers a format function: the object check sees it ...
            built = compile_c([weak], directory / "bench_medium_fs.c.obj", flags, shim=True, compile_only=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            self.assertTrue(callers.check(NM, directory, "card"), "the object check missed a card glue that formats")
            # 2. ... and the decision core still refuses to call it: the host test of the glue passes.
            test_source = directory / "test_weak.c"
            test_source.write_text((HOST / "test_bench_medium_fs.c").read_text().replace(
                '#include "../src/bench_medium_fs.c"', f'#include "{weak}"'))
            result = self.build_and_run([test_source, BENCH / "src/bench_medium.c"], flags, shim=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class SourcesOfTheImage(unittest.TestCase):
    def image_sources(self):
        target = (BENCH / "management_target.cmake").read_text()
        listed = re.findall(r'"\$\{component\}/([^"]+\.c)"', target)
        sources = [ROOT / "components" / name for name in listed]
        sources += sorted((BENCH / "src").glob("*.c"))
        sources += [ROOT / "components/file/src/file.c", ROOT / "components/file/ports/zephyr/file_native.c"]
        return sources

    def test_only_the_ram_glue_names_the_format_functions(self):
        names = re.compile(r"\b(fs_mkfs|f_mkfs|fatfs_mkfs|FS_FATFS_MOUNT_MKFS)\b")
        found = {}
        for path in self.image_sources():
            self.assertTrue(path.exists(), path)
            for match in names.finditer(comment_free(path.read_text())):
                found.setdefault(path.relative_to(ROOT).as_posix(), []).append(match.group(1))
        self.assertEqual(found, {"tests/zephyr/management_rt1060_compile/src/bench_medium_fs.c": ["fs_mkfs"]},
                         "a source of the bench image other than the glue names a format function")

    def test_the_format_call_is_for_the_ram_disk_name_and_inside_the_ram_disk_form(self):
        text = comment_free((BENCH / "src/bench_medium_fs.c").read_text())
        call = re.search(r"fs_mkfs\(([^;]*)\);", text)
        self.assertIsNotNone(call)
        self.assertIn("BENCH_MEDIUM_RAM_DISK_NAME", call.group(1))
        self.assertNotIn("SD", call.group(1))
        before = text[:call.start()]
        self.assertGreater(before.rfind("#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK)"),
                           before.rfind("#endif"), "the call is outside the RAM disk form")

    def test_the_copy_of_the_start_source_does_not_format(self):
        text = comment_free((BENCH / "src/mgmt_start.c").read_text())
        self.assertNotIn("fs_mkfs", text)
        self.assertIn("bench_medium_mount()", text)
        self.assertNotIn('"/SD:"', text)


class RamDiskSize(unittest.TestCase):
    def test_the_native_sizing_test_uses_the_limits_of_the_images_file_area(self):
        services = (BENCH / "src/mgmt_services.c").read_text()
        area = services[services.index("#if defined(CRUX_NATIVE_MANAGEMENT_SSH)"):services.index("#else")]
        limits = {k: int(v) for k, v in re.findall(r"\.(max_\w+) = (\d+)u", area)}
        header = (ROOT / "tests/native/management_rt1060_ram_disk/bench_limits.h").read_text()
        declared = {k: int(v) for k, v in re.findall(r"#define (BENCH_AREA_\w+) (\d+)u", header)}
        self.assertEqual(limits, {"max_file_bytes": declared["BENCH_AREA_MAX_FILE_BYTES"],
                                  "max_area_bytes": declared["BENCH_AREA_MAX_BYTES"],
                                  "max_entries": declared["BENCH_AREA_MAX_ENTRIES"]})

    def test_the_disk_holds_the_declared_worst_case_with_a_quarter_to_spare(self):
        # The measured worst case is in docs/rt1060-bench-file-backend.md. This keeps the disk from being lowered, or a
        # limit from being raised, without the sizing being looked at again.
        overlay = (BENCH / "ram_disk.overlay").read_text()
        disk = int(re.search(r"sector-size = <(\d+)>", overlay).group(1)) * int(
            re.search(r"sector-count = <(\d+)>", overlay).group(1))
        port = (ROOT / "components/credential/src/credential_port.h").read_text()
        slot = int(re.search(r"#define CRUX_CREDENTIAL_SLOT_BYTES (\d+)u", port).group(1))
        services = (BENCH / "src/mgmt_services.c").read_text()
        area = int(re.search(r"\.max_area_bytes = (\d+)u", services[services.index("CRUX_NATIVE_MANAGEMENT_SSH"):]).group(1))
        need = area + 2 * slot
        self.assertGreaterEqual(disk, need * 5 // 4, f"the disk of {disk} B is under 125 % of the {need} B that the limits allow")

    def test_the_host_shim_has_the_geometry_of_the_overlay(self):
        overlay = (BENCH / "ram_disk.overlay").read_text()
        shim = (HOST / "shim/zephyr/devicetree.h").read_text()
        self.assertEqual(re.search(r"sector-size = <(\d+)>", overlay).group(1),
                         re.search(r"bench_ram_disk_sector_size (\d+)", shim).group(1))
        self.assertEqual(re.search(r"sector-count = <(\d+)>", overlay).group(1),
                         re.search(r"bench_ram_disk_sector_count (\d+)", shim).group(1))
        self.assertIn('disk-name = "RAM"', overlay)
        self.assertIn("bench_ram_disk:", overlay)


if __name__ == "__main__":
    unittest.main()
