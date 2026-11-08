/*
 * The first account of the RT1060-EVKB bench image (row C4, D3 of docs/rt1060-management-image-feasibility.md).
 *
 * The password is in a file outside the repository. CMake checks it at configuration and embeds its bytes in this
 * image (../bench_first_account.cmake), because Access takes a plaintext password and derives its own record: there
 * is no pre-derived form to embed instead. So the plaintext IS in the linked image, in read-only data in Flash, and
 * this is a test form: it exists only here, under the bench declaration, and never in a build without it. The bytes
 * are read in place and never copied, printed or logged: the lines below name the account and a result code only.
 *
 * A product does not do this. It creates its first account in a serial setup step, and gives a user a public key
 * (docs/management-in-a-product.md).
 */
#include "bench_first_account.h"

#include "bench_guard.h"

/* Before any other include, so that a build that lacks the form stops here and says why. */
#ifndef CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE
#error "The first account's embedded password is declared only with CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE"
#endif
#if !defined(CRUX_BENCH_FIRST_ACCOUNT_USER) || !defined(CRUX_BENCH_FIRST_ACCOUNT_ROLE)
#error "The bench declaration did not reach the compiler: the first account's name and role"
#endif

#include "bench_first_account_provision.h"

#include <zephyr/sys/printk.h>

/* Defined by the assembler file that CMake writes beside the build, from the password file. */
extern const uint8_t crux_bench_first_account_password[];
extern const uint32_t crux_bench_first_account_password_size;

bool crux_bench_first_account(void)
{
    crux_access_result_t result =
        crux_bench_first_account_provision(CRUX_BENCH_FIRST_ACCOUNT_USER, CRUX_BENCH_FIRST_ACCOUNT_ROLE,
                                           crux_bench_first_account_password,
                                           crux_bench_first_account_password_size);
    if (result != CRUX_ACCESS_OK) {
        printk("BENCH first account failed: result=%d\n", (int)result);
        return false;
    }
    printk("BENCH first account created: user=%s role=%s\n", CRUX_BENCH_FIRST_ACCOUNT_USER,
           CRUX_BENCH_FIRST_ACCOUNT_ROLE);
    return true;
}
