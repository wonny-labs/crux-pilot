/* Compile-only PoC: no Flash write/erase calls or storage activation. */
#include "flash_poc.h"
#include <zephyr/kernel.h>

int main(void) {
    printk("Experimental Flash safety image; NOT authorised for programming\n");
    return 0;
}
