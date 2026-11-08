/* Host run of the pinned FatFs on a RAM disk of a given number of 512 B sectors, with the mkfs
 * options of Zephyr's fs_mkfs (FM_ANY | FM_SFD, one FAT, 512 root entries, automatic cluster size)
 * and the worst-case content of the bench image (docs/rt1060-bench-file-backend.md). Not part of
 * the build: a stand-in for the native_sim test. */
/* ff.h defines the types that diskio.h uses, so it comes first. */
// clang-format off
#include "ff.h"
#include "diskio.h"
// clang-format on
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SS 512
static unsigned char *disk;
static unsigned sectors;
DSTATUS disk_status(BYTE p)
{
    (void)p;
    return 0;
}
DSTATUS disk_initialize(BYTE p)
{
    (void)p;
    return 0;
}
DRESULT disk_read(BYTE p, BYTE *b, LBA_t s, UINT c)
{
    (void)p;
    if (s + c > sectors) {
        return RES_PARERR;
    }
    memcpy(b, disk + s * SS, c * SS);
    return RES_OK;
}
DRESULT disk_write(BYTE p, const BYTE *b, LBA_t s, UINT c)
{
    (void)p;
    if (s + c > sectors) {
        return RES_PARERR;
    }
    memcpy(disk + s * SS, b, c * SS);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE p, BYTE cmd, void *buf)
{
    (void)p;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buf = sectors;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buf = SS;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buf = 1;
        return RES_OK;
    }
    return RES_PARERR;
}
static FATFS fs;
static unsigned char chunk[2048];
static FRESULT put(const char *path, unsigned bytes)
{
    FIL f;
    FRESULT r = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r) {
        return r;
    }
    memset(chunk, (int)bytes, sizeof chunk);
    for (unsigned d = 0; d < bytes;) {
        unsigned n = bytes - d < sizeof chunk ? bytes - d : sizeof chunk;
        UINT w;
        r = f_write(&f, chunk, n, &w);
        if (r || w != n) {
            f_close(&f);
            return r ? r : FR_DENIED;
        }
        d += n;
    }
    return f_close(&f);
}
static void space(const char *what)
{
    DWORD nclst;
    FATFS *p;
    f_getfree("RAM:", &nclst, &p);
    unsigned cb = p->csize * SS;
    printf("  %-60s free %7u B of %7u B data (%u clusters of %u B), fat type %u\n", what,
           (unsigned)(nclst * cb), (unsigned)((p->n_fatent - 2) * cb), (unsigned)(p->n_fatent - 2),
           cb, p->fs_type);
}
static int run(unsigned sec, unsigned area_max, unsigned file_max, unsigned entries, unsigned slot)
{
    sectors = sec;
    disk = calloc(sec, SS);
    MKFS_PARM o = {FM_ANY | FM_SFD, 1, 0, 512, 0};
    BYTE work[512];
    printf("disk %u sectors = %u B; area %u B, file %u B, %u entries, slot %u B\n", sec, sec * SS,
           area_max, file_max, entries, slot);
    if (f_mount(&fs, "RAM:", 1) == FR_OK) {
        printf("  BLANK DISK MOUNTED?\n");
        return 1;
    }
    printf("  blank disk mount: fails (as the NO_FORMAT rule needs)\n");
    if (f_mkfs("RAM:", &o, work, sizeof work)) {
        printf("  mkfs failed\n");
        return 1;
    }
    if (f_mount(&fs, "RAM:", 1)) {
        printf("  mount failed\n");
        return 1;
    }
    space("after mkfs");
    f_mkdir("RAM:/secure-a");
    f_mkdir("RAM:/secure-b");
    f_mkdir("RAM:/files");
    space("after the three directories");
    int bad = 0;
    char p[160];
    for (unsigned i = 0; i < area_max / file_max; i++) {
        snprintf(p, sizeof p, "RAM:/files/%u.dat", i);
        if (put(p, file_max)) {
            printf("  file %u failed\n", i);
            bad = 1;
        }
    }
    if (put("RAM:/secure-a/slot", slot) || put("RAM:/secure-b/slot", slot)) {
        printf("  slot write failed\n");
        bad = 1;
    }
    space("pattern A: area as 4 files of the largest size, then both slots");
    f_mount(NULL, "RAM:", 0);
    memset(disk, 0, sec * SS);
    f_mkfs("RAM:", &o, work, sizeof work);
    f_mount(&fs, "RAM:", 1);
    f_mkdir("RAM:/secure-a");
    f_mkdir("RAM:/secure-b");
    f_mkdir("RAM:/files");
    DWORD nclst;
    FATFS *fp;
    f_getfree("RAM:", &nclst, &fp);
    unsigned cb = fp->csize * SS;
    unsigned each = area_max / entries, whole = (each - 1) / cb, size = whole * cb + 1;
    for (unsigned i = 0; i < entries; i++) {
        char name[64];
        snprintf(name, sizeof name, "%02u", i);
        memset(name + 2, 'a', 61);
        name[63] = 0;
        snprintf(p, sizeof p, "RAM:/files/%s", name);
        if (put(p, size)) {
            printf("  entry %u failed\n", i);
            bad = 1;
        }
    }
    if (put("RAM:/secure-a/slot", slot) || put("RAM:/secure-b/slot", slot)) {
        printf("  slot write failed\n");
        bad = 1;
    }
    space("pattern B: 16 entries of 63-character names at the worst slack, then both slots");
    f_mount(NULL, "RAM:", 0);
    memset(disk, 0, sec * SS);
    f_mkfs("RAM:", &o, work, sizeof work);
    f_mount(&fs, "RAM:", 1);
    f_mkdir("RAM:/files");
    FRESULT r = FR_OK;
    unsigned k = 0;
    for (; k < 2000 && r == FR_OK; k++) {
        snprintf(p, sizeof p, "RAM:/files/fill%u", k);
        r = put(p, 65536);
    }
    printf("  filled to refusal after %u files of 64 KiB, result %d (7 = DENIED, 9 = INT_ERR? see "
           "ff.h)\n",
           k, (int)r);
    free(disk);
    return bad;
}
int main(int argc, char **argv)
{
    unsigned sec = argc > 1 ? atoi(argv[1]) : 1024;
    unsigned area = argc > 2 ? atoi(argv[2]) : 262144, file = argc > 3 ? atoi(argv[3]) : 65536,
             ent = argc > 4 ? atoi(argv[4]) : 16;
    return run(sec, area, file, ent, 16384);
}
