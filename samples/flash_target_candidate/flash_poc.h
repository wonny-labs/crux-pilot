/* Experimental compile-only hooks. Not a public platform API. */
#ifndef FLASH_POC_H
#define FLASH_POC_H
#include <errno.h>
int crux_flash_island_refuse(void);
int crux_flash_island_inspect_and_refuse(void);
_Noreturn void crux_flash_poc_terminal(int reason);
#endif
