# Host run of the pinned FatFs (modules/fs/fatfs at the west.yml pin f4ead3bf) on a RAM disk, with the options of the
# bench image's target.conf and the mkfs options of Zephyr's fs_mkfs. A stand-in for tests/native/management_rt1060_ram_disk
# while no container ran. Usage: FATFS=<path of the fatfs module> sh fatfs_host_size.sh
set -e
cc -std=c11 -O1 -w -DZEPHYR_CONFIG_OVERRIDE=ffconf_host.h -I. -I"$FATFS/include" -o fatsize fatsize.c \
  "$FATFS/ff.c" "$FATFS/option/ffunicode.c"
./fatsize 1024                      # the bench image: 512 KiB, the limits of src/mgmt_services.c
./fatsize 2048                      # 1 MiB, for comparison
./fatsize 8192 3145728 1048576 16   # 4 MiB, with the design limits of docs/sftp-design.md (1 MiB per file, 3 MiB per area)
