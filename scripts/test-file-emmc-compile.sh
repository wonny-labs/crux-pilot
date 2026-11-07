#!/usr/bin/env bash
set -euo pipefail

# Link only: the eMMC reference composition (samples/storage_emmc) on the RT1060-EVKB Rev B image,
# without and with the mount at boot, and with Configuration on the Settings File backend.
# The EVKB has no eMMC. The overlay of the sample names an MMC disk so that the stack, the flush
# correction and Reliance Edge link together; nothing is programmed, reset or run, and no image is
# kept or uploaded: the image links GPLv2 source and is a test image.
#
# CRUX_FILE_EMMC_SKIP_BUILD=1 does not run the Twister build and checks the images already under the
# output directory. It exists to show that each check can fail: a deliberately wrong copy of an
# output directory, or a loosened guard, must make the script fail.

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr tests are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
if [[ ! -f "$workspace/.west/config" || ! -d "$workspace/zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before Zephyr tests.\n' >&2
    exit 2
fi
export ZEPHYR_BASE="$workspace/zephyr"
board='mimxrt1060_evk@B/mimxrt1062/qspi'
# The name does not start with twister-, so the CI step that collects Twister evidence does not
# match this directory: it holds the map of an image that links GPLv2 source.
output="${CRUX_FILE_EMMC_OUTPUT:-$repo/build/zephyr/file-emmc-link}"

if [[ "${CRUX_FILE_EMMC_SKIP_BUILD:-0}" != 1 ]]; then
    # All three scenarios must build and link: the mount sequence compiled out, compiled in, and
    # compiled in with Configuration's settings initialised after the layout check.
    (cd "$workspace" && west twister -p "$board" -T "$repo/samples/storage_emmc" \
        --build-only -O "$output" --inline-logs)
fi

python3 - "$output" "$board" "$repo" "$ZEPHYR_BASE" <<'PY'
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

output, board, repo, zephyr = (Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]))
suites = json.load(open(output / 'twister.json'))['testsuites']
names = sorted(s['name'] for s in suites)
expected = ['crux.sample.storage_emmc', 'crux.sample.storage_emmc.settings',
            'crux.sample.storage_emmc.start']
if (names != expected or any(s['platform'] != board or s.get('status') != 'not run' or
                             s.get('runnable') for s in suites)):
    raise RuntimeError(f'RT1060 eMMC link check did not build the three scenarios: '
                       f"{[(s['name'], s['platform'], s.get('status')) for s in suites]}")

# The pinned hashes come from the generator, which refuses any other source.
spec = importlib.util.spec_from_file_location('prepare_mmc', repo / 'scripts/prepare-mmc-cache-flush.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
patch_sha = hashlib.sha256((repo / 'patches/zephyr/mmc-cache-flush.patch').read_bytes()).hexdigest()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def find_nm(build):
    cache = (build / 'CMakeCache.txt').read_text().splitlines()
    for line in cache:
        if line.startswith('CMAKE_NM:'):
            return line.split('=', 1)[1]
    raise RuntimeError(f'no CMAKE_NM in {build}/CMakeCache.txt')


# Each entry is a line that must be in .config; a symbol listed as unset must not be =y or any value.
REQUIRED = ['CONFIG_DISK_DRIVER_MMC=y', 'CONFIG_MMC_STACK=y', 'CONFIG_CRUX_MMC_CACHE_FLUSH=y',
            'CONFIG_SDHC_BUFFER_ALIGNMENT=32', 'CONFIG_CRUX_FS_RELIANCE_EDGE=y',
            'CONFIG_CRUX_FILE_RELIANCE_EDGE=y']
UNSET = ['CONFIG_DISK_DRIVER_SDMMC', 'CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT', 'CONFIG_FAT_FILESYSTEM_ELM',
         'CONFIG_CRUX_FILE_FATFS_RT1060']

# Each scenario is (name, mount at boot, Configuration on the Settings File backend).
SCENARIOS = [('crux.sample.storage_emmc', False, False), ('crux.sample.storage_emmc.start', True, False),
             ('crux.sample.storage_emmc.settings', True, True)]
# The Settings scenario: the target selection and its path under the volume's mount point.
SETTINGS_REQUIRED = ['CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE=y', 'CONFIG_SETTINGS=y',
                     'CONFIG_SETTINGS_FILE=y']
SETTINGS_PATH = 'CONFIG_SETTINGS_FILE_PATH="/MMC:/settings/cfg"'
SETTINGS_SOURCES = ['settings_file.c', 'settings_line.c', 'settings_store.c']

for name, start, settings in SCENARIOS:
    configs = [p for p in output.rglob('.config') if p.parent.parent.name == name]
    if len(configs) != 1:
        raise RuntimeError(f'expected one build configuration for {name}, found {len(configs)}')
    config = configs[0]
    build = config.parent.parent
    lines = config.read_text().splitlines()
    for line in REQUIRED:
        if line not in lines:
            raise RuntimeError(f'{name}: {line} is missing from {config}')
    for symbol in UNSET:
        if any(line.startswith(symbol + '=') for line in lines):
            raise RuntimeError(f'{name}: {symbol} is set in {config}')
    if ('CONFIG_SAMPLE_EMMC_START=y' in lines) != start:
        raise RuntimeError(f'{name}: CONFIG_SAMPLE_EMMC_START enabled={not start}, expected {start}')
    if settings:
        for line in SETTINGS_REQUIRED + [SETTINGS_PATH]:
            if line not in lines:
                raise RuntimeError(f'{name}: {line} is missing from {config}')
    else:
        for symbol in ('CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE', 'CONFIG_SETTINGS'):
            if any(line.startswith(symbol + '=') for line in lines):
                raise RuntimeError(f'{name}: {symbol} is set in {config}')

    # The linked image: no formatter, no FatFs, and the pieces that make the composition.
    elf = build / 'zephyr/zephyr.elf'
    symbols = subprocess.run([find_nm(build), str(elf)], check=True, capture_output=True,
                             text=True).stdout.splitlines()
    present = {line.split()[-1] for line in symbols if len(line.split()) >= 3}
    if 'red_format' in present:
        raise RuntimeError(f'{name}: red_format is linked into {elf}')
    # The VFS table keeps the file system and the MMC stack in both images. Only the mount-at-boot
    # image calls the composition, so only it links the composition and the File binding.
    wanted = ['red_mount2', 'mmc_ioctl']
    if start:
        wanted += ['emmc_storage_start', 'crux_file_zephyr_bind', 'disk_access_init']
    if settings:
        # The sample has no Configuration consumer, so crux_config_save is not referenced and the
        # static library does not bring configuration.c in: the provider's init is what links.
        wanted += ['emmc_configuration_start', 'crux_config_zephyr_init', 'settings_backend_init']
    for symbol in wanted:
        if symbol not in present:
            raise RuntimeError(f'{name}: {symbol} is not linked into {elf}')
    if 'f_mount' in present:
        raise RuntimeError(f'{name}: FatFs is linked into {elf}')
    if not settings and ('crux_config_zephyr_init' in present or 'settings_backend_init' in present):
        raise RuntimeError(f'{name}: the Settings File provider is linked into {elf}')

    # The build used the generated copy of the MMC stack, and not the pinned original.
    commands = json.loads((build / 'compile_commands.json').read_text())
    files = [str(Path(entry['directory'], entry['file']).resolve()) for entry in commands]
    # The Settings File image is built from the corrected copies of the three backend sources, which
    # tests/native/configuration_settings_file stages, and not from the pinned originals.
    staged = sorted(Path(f).name for f in files if '/settings-file-candidate/' in f)
    pinned = [f for f in files if f.startswith(str((zephyr / 'subsys/settings/src').resolve()))]
    if settings and (staged != sorted(SETTINGS_SOURCES) or
                     any(Path(f).name in SETTINGS_SOURCES for f in pinned)):
        raise RuntimeError(f'{name}: the build used staged copies {staged} and pinned sources '
                           f'{[Path(f).name for f in pinned]}, expected the three staged copies and '
                           'none of the three pinned originals')
    if not settings and staged:
        raise RuntimeError(f'{name}: staged Settings File copies {staged} in a build without them')
    generated = [f for f in files if f.endswith('/crux-mmc/mmc.c')]
    original = [f for f in files if f == str((zephyr / 'subsys/sd/mmc.c').resolve())]
    if len(generated) != 1 or original:
        raise RuntimeError(f'{name}: expected the generated mmc.c and not subsys/sd/mmc.c, '
                           f'found {len(generated)} generated and {len(original)} original')
    metadata_path = Path(generated[0]).with_suffix('.json')
    metadata = json.loads(metadata_path.read_text())
    expected_hashes = {'upstream_sha256': prepare.BASE_SHA256, 'generated_sha256': prepare.PATCHED_SHA256,
                       'patch_sha256': patch_sha}
    for key, value in expected_hashes.items():
        if metadata.get(key) != value:
            raise RuntimeError(f'{name}: {metadata_path} has {key} {metadata.get(key)}, expected {value}')
    if sha(generated[0]) != prepare.PATCHED_SHA256 or sha(zephyr / 'subsys/sd/mmc.c') != prepare.BASE_SHA256:
        raise RuntimeError(f'{name}: the generated or the pinned MMC source does not match its hash')
    print(f'RT1060 eMMC link check: {name} linked without red_format, with the generated mmc.c'
          + (', and the staged Settings File backend' if settings else ''))
PY

# Negative builds, configuration only. Without the flush correction, Reliance Edge on an MMC disk
# must fail naming the option, and the fixture-only control must be refused off native_sim.
# CMake wraps a message over lines, so whitespace is folded before matching.
expect_cmake_failure() {
    local directory="$1" message="$2"
    shift 2
    local log="$directory.log"
    if (cd "$workspace" && west build --cmake-only -p always -b "$board" -d "$directory" \
            "$repo/samples/storage_emmc" -- "$@") >"$log" 2>&1; then
        printf 'The eMMC composition configured with %s\n' "$*" >&2
        exit 1
    fi
    local folded
    folded="$(tr -s '[:space:]' ' ' <"$log")"
    [[ "$folded" == *"$message"* ]] || {
        printf 'The eMMC composition with %s failed for another reason; see %s\n' "$*" "$log" >&2
        exit 1
    }
    printf 'RT1060 eMMC composition refused with %s, as intended\n' "$*"
}
expect_cmake_failure "$repo/build/zephyr/file-emmc-noflush" \
    'Reliance Edge on an MMC disk requires CONFIG_CRUX_MMC_CACHE_FLUSH=y' \
    -DCONFIG_CRUX_MMC_CACHE_FLUSH=n
expect_cmake_failure "$repo/build/zephyr/file-emmc-control" \
    'CRUX_EMMC_FLUSH_CONTROL is a native_sim test fixture setting and is refused on any other board' \
    -DCRUX_EMMC_FLUSH_CONTROL=ON
# The settings control keeps the unpatched Settings File backend and is for the native_sim fixture only.
# The same configuration as the Settings scenario, with the control, must fail at CMake.
expect_cmake_failure "$repo/build/zephyr/file-emmc-settings-control" \
    'CRUX_SETTINGS_FILE_CONTROL is a native_sim test fixture setting and is refused on any other board' \
    -DCONFIG_SAMPLE_EMMC_START=y -DCONFIG_SETTINGS=y -DCONFIG_SETTINGS_FILE=y \
    '-DCONFIG_SETTINGS_FILE_PATH="/MMC:/settings/cfg"' -DCONFIG_CRUX_CONFIGURATION_SETTINGS_FILE=y \
    -DCRUX_SETTINGS_FILE_CONTROL=ON
