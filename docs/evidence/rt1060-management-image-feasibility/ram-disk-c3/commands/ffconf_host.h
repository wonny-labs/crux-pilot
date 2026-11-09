/* Host override of FatFs's ffconf.h with the options of the bench image's target.conf (Zephyr's own
 * defaults for the rest) */
#undef FF_USE_MKFS
#define FF_USE_MKFS 1
#undef FF_CODE_PAGE
#define FF_CODE_PAGE 437
#undef FF_USE_LFN
#define FF_USE_LFN 2
#undef FF_MAX_LFN
#define FF_MAX_LFN 127
#undef FF_FS_NORTC
#define FF_FS_NORTC 1
#undef FF_STR_VOLUME_ID
#define FF_STR_VOLUME_ID 1
#undef FF_VOLUME_STRS
#define FF_VOLUME_STRS "RAM"
#define FS_FATFS_WINDOW_ALIGNMENT 1
