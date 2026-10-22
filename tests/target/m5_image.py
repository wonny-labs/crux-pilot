"""Read-only M5 fixture identity. Reuse M3 geometry checks, not M3 profiles or protocol."""
from pathlib import Path
from m3_image import check_image_bytes,require

def check_image(binary_path,elf_path,config_path):
    geometry=check_image_bytes(binary_path,elf_path)
    symbols=geometry['symbols'];config=Path(config_path).read_text().splitlines()
    required={
        'CONFIG_BOARD_REVISION="B"','CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"',
        'CONFIG_FLASH_BASE_ADDRESS=0x60000000','CONFIG_FLASH_SIZE=8192','CONFIG_FLASH_LOAD_OFFSET=0',
        'CONFIG_XIP=y','CONFIG_NXP_IMXRT_BOOT_HEADER=y','CONFIG_HEAP_MEM_POOL_SIZE=0',
        'CONFIG_MAIN_STACK_SIZE=4096','CONFIG_MPU_STACK_GUARD=y','CONFIG_INIT_STACKS=y',
        'CONFIG_THREAD_STACK_INFO=y','CONFIG_PRINTK_SYNC=y','CONFIG_CRUX_SOCKETS=y','CONFIG_CRUX_NETIF=y',
        'CONFIG_CONSOLE_GETCHAR=y','CONFIG_CONSOLE_SUBSYS=y','CONFIG_NET_IPV4=y','CONFIG_NET_TCP=y',
        'CONFIG_NET_UDP=y','CONFIG_NET_SOCKETS=y','CONFIG_NET_BUF_POOL_USAGE=y',
        'CONFIG_NET_MAX_CONTEXTS=4','CONFIG_NET_MAX_CONN=4','CONFIG_NET_PKT_RX_COUNT=16',
        'CONFIG_NET_PKT_TX_COUNT=16','CONFIG_NET_BUF_RX_COUNT=32','CONFIG_NET_BUF_TX_COUNT=32',
    }
    require(required<=set(config),'M5 image configuration differs')
    for feature in ('FLASH','FLASH_MAP','NVS','SETTINGS','FILE_SYSTEM','DISK_ACCESS','WATCHDOG','SHELL','LOG','NET_IPV6','NET_DHCPV4','BOOTLOADER_MCUBOOT'):
        require('CONFIG_'+feature+'=y' not in config,'Unapproved storage/layout/service: '+feature)
    require(symbols.get('z_main_stack',{}).get('bytes')==4160,'Main stack differs')
    require(symbols.get('m5_observer',{}).get('bytes')==2112,'Observer stack differs')
    require('work_stack' not in symbols and 'network_stack' not in symbols,'Unexpected application worker')
    return dict(elf_sections_match_bin=True,gap_fill=255,bin_address=0x60000000,bin_bytes=geometry['binary_bytes'],
                main_stack=symbols['z_main_stack'],observer_stack=symbols['m5_observer'])
