#ifndef REBOOT_NATIVE_MAIN_COMMON_H
#define REBOOT_NATIVE_MAIN_COMMON_H
/* Fails the ztest case unless exactly the selected case ran, and prints the count when listing. */
void reboot_native_expect_one_case(const char *suite);
#endif
