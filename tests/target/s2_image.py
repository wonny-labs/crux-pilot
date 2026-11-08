"""Read-only identity checks of the crux-controller S2 image. Reuse the M3 geometry checks, not M3 profiles or protocol.

The configuration lines and symbol sizes were measured in a local build of the S2 image (see
docs/board-image-profiles.md). Nothing here ran on a board. The checks need only the BIN, the ELF and the resolved
``.config`` that the freeze already holds. The static IPv4 configuration (192.168.77.2/24, no gateway) is a set of
Kconfig values and is checked there. The UART output of the image is unverified and nothing checks it here.
"""
from pathlib import Path
from m3_image import check_image_bytes, check_optimization, require

TARGET = 'mimxrt1060_evk@B//qspi'               # the target string of the S2 firmware identity
MAIN_STACK_BYTES = 4160                         # CONFIG_MAIN_STACK_SIZE 4096 plus the 64-byte guard
WORKER_STACKS_BYTES = 33024                     # 4 Crux threads of 8192 bytes plus a 64-byte guard each

REQUIRED = frozenset({
    'CONFIG_BOARD_REVISION="B"', 'CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"',
    'CONFIG_FLASH_BASE_ADDRESS=0x60000000', 'CONFIG_FLASH_SIZE=8192', 'CONFIG_FLASH_LOAD_OFFSET=0',
    'CONFIG_XIP=y', 'CONFIG_NXP_IMXRT_BOOT_HEADER=y', 'CONFIG_HEAP_MEM_POOL_SIZE=0',
    'CONFIG_MAIN_STACK_SIZE=4096', 'CONFIG_MPU_STACK_GUARD=y', 'CONFIG_THREAD_STACK_INFO=y',
    'CONFIG_CRUX_SOCKETS=y', 'CONFIG_CRUX_NETIF=y', 'CONFIG_NET_IPV4=y', 'CONFIG_NET_TCP=y', 'CONFIG_NET_UDP=y',
    'CONFIG_NET_SOCKETS=y', 'CONFIG_NET_MAX_CONTEXTS=4', 'CONFIG_NET_MAX_CONN=4', 'CONFIG_NET_PKT_RX_COUNT=16',
    'CONFIG_NET_PKT_TX_COUNT=16', 'CONFIG_NET_BUF_RX_COUNT=32', 'CONFIG_NET_BUF_TX_COUNT=32',
    # The static address of the board (no DHCP, no gateway) and the S2 workload with its network peer.
    'CONFIG_NET_CONFIG_SETTINGS=y', 'CONFIG_NET_CONFIG_NEED_IPV4=y', 'CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.77.2"',
    'CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"', 'CONFIG_NET_CONFIG_MY_IPV4_GW=""',
    'CONFIG_CRUX_CONTROLLER_S2=y', 'CONFIG_CRUX_CONTROLLER_PEER_ADDRESS="192.168.77.1"',
    'CONFIG_CRUX_CONTROLLER_PEER_PORT=42420', 'CONFIG_CRUX_CONTROLLER_WORKER_STACK=8192',
    'CONFIG_CRUX_THREAD_COUNT=4', 'CONFIG_CRUX_THREAD_STACK_BYTES=8192',
    # Crux Logging in deferred mode: the console of this image, unlike the M5 image which forbids CONFIG_LOG.
    'CONFIG_LOG=y', 'CONFIG_LOG_MODE_DEFERRED=y', 'CONFIG_CRUX_LOGGING=y',
})
FORBIDDEN = ('FLASH', 'FLASH_MAP', 'NVS', 'SETTINGS', 'FILE_SYSTEM', 'DISK_ACCESS', 'WATCHDOG', 'SHELL',
             'NET_IPV6', 'NET_DHCPV4', 'BOOTLOADER_MCUBOOT')


def check_image(binary_path, elf_path, config_path):
    geometry = check_image_bytes(binary_path, elf_path)
    symbols, config = geometry['symbols'], Path(config_path).read_text().splitlines()
    check_optimization(config)
    require(REQUIRED <= set(config), 'S2 image configuration differs')
    for feature in FORBIDDEN:
        require('CONFIG_' + feature + '=y' not in config, 'Unapproved storage/layout/service: ' + feature)
    require(symbols.get('z_main_stack', {}).get('bytes') == MAIN_STACK_BYTES, 'Main stack differs')
    require(symbols.get('worker_stacks', {}).get('bytes') == WORKER_STACKS_BYTES, 'Worker stacks differ')
    require('m5_observer' not in symbols, 'M5 observer in an S2 image')
    return dict(elf_sections_match_bin=True, gap_fill=255, bin_address=0x60000000, bin_bytes=geometry['binary_bytes'],
                main_stack=symbols['z_main_stack'], worker_stacks=symbols['worker_stacks'])
