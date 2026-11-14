"""Image checks and profile of the RT1060-EVKB management bench image (row C8 of the feasibility report).

Built, tested, and deliberately NOT registered in ``PROFILES`` of ``prepare_m5_network.py``: the owner's decisions listed
in docs/board-image-profiles.md (B2, B4, B5, B1, D-b to D-f) come first, and a registered profile would let a freeze
record a Zephyr tree that was never compiled. Every fact below is taken from docs/rt1060-bench-image-plan.md, section
"The image profile input", and from the recorded real cross-build (docs/evidence/rt1060-management-image-feasibility/
image-plan-c8). Nothing here ran on a board, and the check reads only the BIN, the ELF, the resolved ``.config`` and the
two frozen records of the patched Zephyr copy (the bench manifest and ``patched-zephyr.json`` with the artefacts it names)
that a freeze holds: it never trusts the build tree and never reads the Zephyr copy (docs/rt1060-bench-freeze-and-capture-design.md,
part 1; the freeze is ``patched_zephyr.py``).
"""
import hashlib
import importlib.util
from pathlib import Path
import struct

from image_profile import ImageProfile, MAX_CAPTURE_SECONDS
import patched_zephyr as zephyr_freeze
from m3_image import check_image_bytes, check_optimization, require

NAME = 'crux-mgmt-bench-rt1060-evkb-b'          # the image id of the declaration and of the banner; the registry key must equal it
MANIFEST_PATH = 'crux-pilot'
TARGET = 'mimxrt1060_evk@B//qspi'               # the target string of the firmware identity

# Blocker B4, decided by the owner on 2026-11-12 as option X of docs/rt1060-bench-freeze-and-capture-design.md, part 2.
# The plan needs about 2,000 s of capture (boot, clients, a reset, a second boot). The program mode keeps its window of
# MAX_CAPTURE_SECONDS (300 s, unchanged) and its 900 s series: it covers the first boot and its first report. The rest is
# the read-only watch mode, which runs no probe command, after program has released the port: 2,400 s (decision D2; the
# design names 2,700 s as the alternative if the 35-minute client figure of the plan is credible). Two evidence files and a
# seam between them (decision D6 of the design, docs/board-image-profiles.md decisions 33 to 40).
BENCH_CAPTURE_SECONDS = MAX_CAPTURE_SECONDS
BENCH_WATCH_SECONDS = 2400

# Section b of the plan: the ten pinned projects and where the build finds them (the build names its modules itself).
PINNED = {
    'zephyr': 'dccb09599635bdff17633fa7e9dab014b91dce90',
    'cmsis': '512cc7e895e8491696b61f7ba8066b4a182569b8',
    'cmsis_6': '30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74',
    'hal_nxp': '7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0',
    'fatfs': 'f4ead3bf4a6dab3a07d7b5f5315795c073db568d',
    'wolfssl': '3c5eead44904df64e6a5a1f4ebdce377d35a849a',
    'wolfssh': '8221e4ee48c93da6cea3325a91de2ecc00ae5f56',
    'wolfpsa': '55ae0ec58cd2ef4c528f44e8466095550c16ca8a',
    'mbedtls': '098e120afb51877ed814bdc04443890ae12e0642',
    'tf-psa-crypto': 'fa92349590a54959ee2efc21b668e5afdb144726',
}
PINNED_PATHS = {
    'zephyr': 'zephyr', 'cmsis': 'modules/hal/cmsis', 'cmsis_6': 'modules/hal/cmsis_6', 'hal_nxp': 'modules/hal/nxp',
    'fatfs': 'modules/fs/fatfs', 'wolfssl': 'modules/crypto/wolfssl', 'wolfssh': 'modules/crypto/wolfssh',
    'wolfpsa': 'modules/crypto/wolfpsa', 'mbedtls': 'modules/crypto/mbedtls',
    'tf-psa-crypto': 'modules/crypto/tf-psa-crypto',
}

# Section a of the plan, one group per row of its table. A line here is required in the resolved configuration.
REQUIRED = {
    'board': frozenset({
        'CONFIG_BOARD_REVISION="B"', 'CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"',
        'CONFIG_FLASH_BASE_ADDRESS=0x60000000', 'CONFIG_FLASH_SIZE=8192', 'CONFIG_FLASH_LOAD_OFFSET=0', 'CONFIG_XIP=y',
        'CONFIG_NXP_IMXRT_BOOT_HEADER=y', 'CONFIG_MPU_STACK_GUARD=y', 'CONFIG_THREAD_STACK_INFO=y'}),
    'declaration and forms': frozenset({
        'CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE=y', 'CONFIG_CRUX_BENCH_TEST_HOST_KEY=y', 'CONFIG_CRUX_BENCH_NO_TIME_SOURCE=y',
        'CONFIG_CRUX_BENCH_FILE_RAM_DISK=y', 'CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE=y'}),
    'entropy': frozenset({'CONFIG_ENTROPY_MCUX_TRNG=y'}),
    'network': frozenset({
        'CONFIG_NET_IPV4=y', 'CONFIG_CRUX_WEB_BIND_ADDRESS="192.168.77.2"', 'CONFIG_CRUX_SSH_BIND_ADDRESS="192.168.77.2"',
        'CONFIG_CRUX_WEB_PORT=443', 'CONFIG_CRUX_SSH_PORT=22', 'CONFIG_CRUX_SSH_MAX_CONNECTIONS=4',
        'CONFIG_HTTP_SERVER_MAX_CLIENTS=3'}),
    'buffers': frozenset({
        'CONFIG_NET_BUF_RX_COUNT=1024', 'CONFIG_NET_BUF_TX_COUNT=768', 'CONFIG_NET_PKT_RX_COUNT=192',
        'CONFIG_NET_PKT_TX_COUNT=32', 'CONFIG_NET_TCP_MAX_RECV_WINDOW_SIZE=5840',
        'CONFIG_NET_TCP_MAX_SEND_WINDOW_SIZE=5840', 'CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=1048576',
        'CONFIG_HTTP_SERVER_STACK_SIZE=8192'}),
    'no card': frozenset({'CONFIG_DISK_DRIVER_RAM=y', 'CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK=y'}),
    'report': frozenset({
        'CONFIG_NET_STATISTICS=y', 'CONFIG_CRUX_SSH_STACK_REPORT=y', 'CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION=y',
        'CONFIG_SYS_HEAP_RUNTIME_STATS=y', 'CONFIG_INIT_STACKS=y', 'CONFIG_THREAD_NAME=y', 'CONFIG_THREAD_MONITOR=y'}),
}
# Features that must not be set to y, with the group of the plan that names them. LOG, FILE_SYSTEM, DISK_ACCESS and
# WATCHDOG are not here on purpose: the bench sets all four (Crux Logging and the RAM disk), unlike M5.
FORBIDDEN = {
    'declaration and forms': ('CRUX_BENCH_FILE_MICROSD', 'WOLFSSL_RSA'),
    'entropy': ('TEST_RANDOM_GENERATOR', 'FAKE_ENTROPY_NATIVE_SIM'),
    'network': ('NET_DHCPV4', 'NET_IPV6'),
    'no card': ('DISK_DRIVER_SDMMC', 'IMX_USDHC', 'FS_FATFS_MOUNT_MKFS'),
    'still refused': ('FLASH', 'FLASH_MAP', 'NVS', 'SETTINGS', 'SHELL', 'BOOTLOADER_MCUBOOT'),
}
# Symbol sizes of the linked image as ``nm -S`` prints them (a stack object and its 64-byte guard). ``worker_stacks``
# is two local symbols of one name: four SSH workers (65,792 B) and four Crux threads (33,024 B).
STACK_OBJECTS = {
    'z_main_stack': [16448],
    '_k_thread_stack_http_server_tid': [8256],
    'worker_stacks': [33024, 65792],
    'listener_stack': [4160],
    'report_stack': [2112],
}

_SPEC = importlib.util.spec_from_file_location(
    'bench_build_provenance', Path(__file__).resolve().parents[2] / 'scripts/record-build-provenance.py')
_PROVENANCE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_PROVENANCE)


def firmware_identity_of(image_sha256, config_bytes, target=TARGET):
    """The firmware identity digest as scripts/record-build-provenance.py computes it, from the recorder's own functions."""
    identity = dict(algorithm=_PROVENANCE.FIRMWARE_IDENTITY_ALGORITHM, target=target, image_sha256=image_sha256,
                    configuration_sha256=hashlib.sha256(_PROVENANCE.normalised_config(config_bytes)).hexdigest())
    return _PROVENANCE.firmware_digest(identity)


def symbol_sizes(elf_path):
    """Every symbol of the ELF as name -> sorted sizes. Unlike the shared check's dict it keeps same-named local symbols."""
    elf = Path(elf_path).read_bytes()
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', elf)
    sections = [struct.unpack_from('<IIIIIIIIII', elf, header[6] + i * header[11]) for i in range(header[12])]
    sizes = {}
    for section in sections:
        if section[1] != 2:
            continue
        table = sections[section[6]]
        strings = elf[table[4]:table[4] + table[5]]
        for offset in range(section[4], section[4] + section[5], 16):
            name, _, size, _, _, _ = struct.unpack_from('<IIIBBH', elf, offset)
            sizes.setdefault(strings[name:].split(b'\0', 1)[0].decode(errors='replace'), []).append(size)
    return {name: sorted(values) for name, values in sizes.items()}


def check_image(binary_path, elf_path, config_path, *, bench_manifest, patched_zephyr):
    """Verify the frozen bytes of the bench image and return the facts. Raises ValueError on any mismatch.

    ``bench_manifest`` and ``patched_zephyr`` (the frozen ``bench-manifest.json`` and ``patched-zephyr.json``) are required
    keyword arguments with no default: there is no way to skip the group, and a missing artefact is a refusal."""
    geometry = check_image_bytes(binary_path, elf_path)
    config = Path(config_path).read_text().splitlines()
    lines = set(config)
    check_optimization(config)
    for group, required in REQUIRED.items():
        require(required <= lines, 'Bench image configuration differs: ' + group)
    for group, features in FORBIDDEN.items():
        for feature in features:
            require('CONFIG_' + feature + '=y' not in lines, 'Unapproved feature in the bench image (%s): %s' % (group, feature))
    sizes = symbol_sizes(elf_path)
    for name, expected in STACK_OBJECTS.items():
        require(sizes.get(name) == expected, 'Stack objects differ: ' + name)
    require('m5_observer' not in sizes, 'M5 observer in a bench image')
    zephyr = zephyr_freeze.check_record(bench_manifest, patched_zephyr, PINNED['zephyr'])
    return dict(elf_sections_match_bin=True, gap_fill=255, bin_address=0x60000000, bin_bytes=geometry['binary_bytes'],
                main_stack=geometry['symbols']['z_main_stack'], stack_objects={n: sizes[n] for n in STACK_OBJECTS},
                firmware_identity=firmware_identity_of(hashlib.sha256(Path(binary_path).read_bytes()).hexdigest(),
                                                       Path(config_path).read_bytes()),
                patched_zephyr=zephyr)


def bench_profile():
    """The bench image profile. Not registered: see the module docstring and docs/board-image-profiles.md."""
    return ImageProfile(name=NAME, manifest_path=MANIFEST_PATH, pinned=PINNED, pinned_paths=PINNED_PATHS,
                        check_image=check_image, commit_tag=False, errno_suffix=None,
                        tool_scripts=('record-build-provenance.py', 'freeze-patched-zephyr.sh'), capture='raw',
                        capture_seconds=BENCH_CAPTURE_SECONDS, extra_roles=('bench_manifest', 'patched_zephyr'),
                        watch_seconds=BENCH_WATCH_SECONDS)
