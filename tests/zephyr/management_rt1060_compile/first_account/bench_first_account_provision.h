/*
 * The step that creates the bench image's first account (row C4, D3 of
 * docs/rt1060-management-image-feasibility.md), from the bytes of the untracked password file.
 *
 * This file holds no test form and no secret: it takes the bytes from its caller and passes them to
 * Access, so the host test suite (tests/bench_first_account) can run it on the real Access core.
 * The bench-only caller is ../src/bench_first_account.c, which includes the bench guard and is the
 * only place that names the embedded bytes.
 */
#ifndef CRUX_BENCH_FIRST_ACCOUNT_PROVISION_H
#define CRUX_BENCH_FIRST_ACCOUNT_PROVISION_H

#include <crux/access.h>

#include <stddef.h>
#include <stdint.h>

/* The limits of bench_declaration.json ("first_account") and of bench_first_account_file.py, which
 * refuses a file outside them at configuration. They are checked here too, so a build that skipped
 * that check still cannot make an account from a password the rules refuse. tests/build keeps the
 * three equal. */
#define CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MIN 16u
#define CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX 128u
_Static_assert(CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX == CRUX_ACCESS_PASSWORD_MAX,
               "the file's longest password is the longest one Access holds");

/*
 * Create the first account, on the serial origin that Access requires, with the password in file.
 *
 * file is the file as it was embedded, with or without its one trailing newline, which is removed.
 * It is borrowed and read in place: the password is not copied, and nothing here prints or logs it.
 * The password must be 16 to 128 printable ASCII characters with no space.
 *
 * @param user Account name, from the bench declaration.
 * @param role Role name of the image's policy. It must hold access.manage.
 * @retval CRUX_ACCESS_OK Created.
 * @retval CRUX_ACCESS_ERR_PASSWORD_REJECTED The bytes break the file's rules, or Access's rule.
 * Access is not called for a password that breaks the file's rules.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT A missing argument, or a role that is unknown or not an
 * administrator's.
 * @retval CRUX_ACCESS_ERR_DENIED Provisioning is closed: the store held an account, or is unusable.
 * @retval others As crux_access_provision_first_account().
 */
crux_access_result_t crux_bench_first_account_provision(const char *user, const char *role,
                                                        const uint8_t *file, size_t file_length);

#endif
