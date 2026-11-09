/* Host stand-in for FatFs's ff.h: the bench medium glue needs the FATFS type and nothing else. */
#ifndef HOST_SHIM_FF_H
#define HOST_SHIM_FF_H
typedef struct {
    unsigned char unused[8];
} FATFS;
#endif
