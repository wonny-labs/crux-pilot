/* Row C4: the first account of the RT1060-EVKB bench image, on the real Access core with the fake
 * ports of tests/access.
 *
 * What it shows: the step creates the account with the declared name and the administrator role,
 * from the bytes of the file with or without its newline; a wrong password is refused; a boot with
 * an empty store creates it again, which is every boot of an image on a RAM disk; a boot that kept
 * the store does not; and a password that breaks the file's rules never reaches Access.
 *
 * What it does not show: the real PBKDF2 (the ports are fakes: native_sim, in the report), the
 * embedded bytes (a target build and a scan of its ELF), or anything on a board. The password of
 * each run is made here from the system's random source, lives only in memory, and is never
 * printed: a failing check names a line, not a value. */
#include "access_test.h"
#include "bench_first_account_provision.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER "admin"
#define ROLE "administrator"

static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";

/* A new password in out, length characters and a terminator. */
static void fresh_password(char *out, size_t length)
{
    FILE *source = fopen("/dev/urandom", "rb");
    CHECK(source != NULL);
    uint8_t bytes[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 8u];
    CHECK(length <= sizeof(bytes));
    if (source == NULL || fread(bytes, 1, length, source) != length) {
        puts("FAIL: no random source for the test password");
        exit(2);
    }
    fclose(source);
    for (size_t index = 0; index < length; ++index) {
        out[index] = alphabet[bytes[index] % (sizeof(alphabet) - 1u)];
    }
    out[length] = '\0';
}

/* The bytes of a file: the password and, if asked, its newline. Returns the length. */
static size_t file_bytes(uint8_t *out, const char *password, bool newline)
{
    size_t length = strlen(password);
    memcpy(out, password, length);
    if (newline) {
        out[length++] = '\n';
    }
    return length;
}

static crux_access_result_t login(const char *user, const char *password,
                                  crux_access_session_t *session)
{
    return access_test_login(user, password, &access_test_web, session);
}

static const crux_access_policy_t *policy(void)
{
    return &access_test_profile_reference.policy;
}

static void creates_the_account_with_the_role(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    access_test_boot(policy());
    size_t length = file_bytes(file, password, true);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_OK);

    crux_access_session_t session = {0};
    CHECK(login(USER, password, &session) == CRUX_ACCESS_OK);
    char text[CRUX_ACCESS_NAME_SIZE];
    CHECK(crux_access_session_user(session, text, sizeof(text)) == CRUX_ACCESS_OK &&
          strcmp(text, USER) == 0);
    CHECK(crux_access_session_role(session, text, sizeof(text)) == CRUX_ACCESS_OK &&
          strcmp(text, ROLE) == 0);
    /* An administrator: it holds the permission that manages accounts. */
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    crux_access_account_info_t accounts[CRUX_ACCESS_ACCOUNT_MAX];
    size_t count = 0;
    CHECK(crux_access_account_list(session, accounts, CRUX_ACCESS_ACCOUNT_MAX, &count) ==
          CRUX_ACCESS_OK);
    CHECK(count == 1u);
    CHECK(strcmp(accounts[0].user, USER) == 0 && strcmp(accounts[0].role, ROLE) == 0);
    CHECK(accounts[0].enabled && !accounts[0].must_change_password && accounts[0].key_count == 0u);
    /* The medium holds the derived value, and not the password. */
    size_t record_length = 0;
    const uint8_t *record = access_fake_store_record(&record_length);
    CHECK(record_length > 0u);
    bool found = false;
    for (size_t at = 0; record != NULL && at + strlen(password) <= record_length; ++at) {
        found = found || memcmp(&record[at], password, strlen(password)) == 0;
    }
    CHECK(!found);
    /* The step left the bytes as they were. */
    CHECK(memcmp(file, password, strlen(password)) == 0 && file[length - 1u] == '\n');
}

static void a_file_without_a_newline_gives_the_same_account(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    access_test_boot(policy());
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, file_bytes(file, password, false)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(login(USER, password, &session) == CRUX_ACCESS_OK);
}

static void the_newline_is_not_part_of_the_password(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    char with_newline[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 5u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    snprintf(with_newline, sizeof(with_newline), "%s\n", password);
    access_test_boot(policy());
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, file_bytes(file, password, true)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(login(USER, with_newline, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(session.value == 0u);
}

static void a_wrong_password_is_refused(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    char other[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    fresh_password(other, 24u);
    access_test_boot(policy());
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, file_bytes(file, password, true)) ==
          CRUX_ACCESS_OK);

    crux_access_session_t session = {0};
    CHECK(login(USER, other, &session) == CRUX_ACCESS_ERR_DENIED && session.value == 0u);
    /* One character short and one character long, which a prefix or a padding bug would accept. */
    char shorter[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    char longer[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 5u];
    snprintf(shorter, sizeof(shorter), "%.*s", (int)strlen(password) - 1, password);
    snprintf(longer, sizeof(longer), "%sx", password);
    CHECK(login(USER, shorter, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(login(USER, longer, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(login("nobody", password, &session) == CRUX_ACCESS_ERR_DENIED);
    /* None of that disturbed the account: the right password still works. */
    access_fake_clock_advance(10u * 60u * 1000u);
    CHECK(login(USER, password, &session) == CRUX_ACCESS_OK);
}

/* A RAM disk keeps nothing: every boot is a first boot, and the step creates the account again,
 * with the same password because the image holds the same bytes. */
static void a_boot_with_an_empty_store_creates_it_again(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    size_t length = file_bytes(file, password, true);
    for (unsigned boot = 0; boot < 3u; ++boot) {
        access_test_boot(policy()); /* the medium is empty again */
        crux_access_session_t session = {0};
        CHECK(login(USER, password, &session) ==
              CRUX_ACCESS_ERR_DENIED); /* nothing from the last boot */
        CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_OK);
        CHECK(login(USER, password, &session) == CRUX_ACCESS_OK);
    }
}

/* The persistent case: a store that kept the account closes provisioning, so a second boot does not
 * make a second account or replace the first. main() asks only on an empty store, and Access
 * refuses anyway. */
static void a_boot_that_kept_the_store_does_not_create_it_again(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    char changed[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    fresh_password(changed, 24u);
    access_test_boot(policy());
    size_t length = file_bytes(file, password, true);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(login(USER, password, &session) == CRUX_ACCESS_OK);
    uint32_t retry = 0;
    CHECK(crux_access_account_change_password(session, password, strlen(password), changed,
                                              strlen(changed), &retry) == CRUX_ACCESS_OK);

    access_test_power_cycle(policy()); /* RAM lost, the medium kept */
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_bench_first_account_provision("second", ROLE, file, length) ==
          CRUX_ACCESS_ERR_DENIED);
    /* The account is the one that was changed, and the file's password does not log in. */
    CHECK(login(USER, password, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(login(USER, changed, &session) == CRUX_ACCESS_OK);
}

static void it_is_made_once_per_boot(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    access_test_boot(policy());
    size_t length = file_bytes(file, password, true);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_OK);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_bench_first_account_provision("other", ROLE, file, length) ==
          CRUX_ACCESS_ERR_DENIED);
}

/* The rules of the file are enforced before Access sees the bytes: Access accepts 12 to 128 bytes
 * of anything. */
static void a_password_that_breaks_the_file_rules_never_reaches_access(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 8u];
    access_test_boot(policy());
    access_fake_derive_counters_clear();
    /* Too short by one, and 12 characters, which Access alone would take. */
    for (size_t short_length = 0; short_length < CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MIN;
         short_length += (short_length < 12 ? 6u : 1u)) {
        fresh_password(password, short_length);
        CHECK(crux_bench_first_account_provision(USER, ROLE, file,
                                                 file_bytes(file, password, true)) ==
              CRUX_ACCESS_ERR_PASSWORD_REJECTED);
        CHECK(crux_bench_first_account_provision(USER, ROLE, file,
                                                 file_bytes(file, password, false)) ==
              CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    }
    /* Too long. */
    fresh_password(password, CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u);
    CHECK(crux_bench_first_account_provision(USER, ROLE, (const uint8_t *)password,
                                             strlen(password)) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    /* A space, a tab, a control byte, a NUL and a second newline inside the line. */
    const uint8_t bad[] = {' ', '\t', 0x01u, 0x00u, '\n', '\r', 0x7fu, 0x80u};
    for (size_t index = 0; index < sizeof(bad); ++index) {
        fresh_password(password, 24u);
        password[10] = (char)bad[index];
        CHECK(crux_bench_first_account_provision(USER, ROLE, (const uint8_t *)password, 24u) ==
              CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    }
    /* A newline at the end is the one that is dropped, and a second one is not. */
    fresh_password(password, 24u);
    password[24] = '\n';
    password[25] = '\n';
    CHECK(crux_bench_first_account_provision(USER, ROLE, (const uint8_t *)password, 26u) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    CHECK(access_fake_derive_count() == 0u);
    /* Nothing was created, so provisioning is still open. */
    fresh_password(password, 24u);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, file_bytes(file, password, true)) ==
          CRUX_ACCESS_OK);
    /* The limits themselves are allowed. */
    access_test_boot(policy());
    fresh_password(password, CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MIN);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, file_bytes(file, password, true)) ==
          CRUX_ACCESS_OK);
    access_test_boot(policy());
    fresh_password(password, CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, file_bytes(file, password, true)) ==
          CRUX_ACCESS_OK);
}

static void arguments_and_roles_are_checked(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    size_t length = file_bytes(file, password, true);
    access_test_boot(policy());
    CHECK(crux_bench_first_account_provision(NULL, ROLE, file, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_bench_first_account_provision(USER, NULL, file, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_bench_first_account_provision(USER, ROLE, NULL, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_bench_first_account_provision(USER, "no-such-role", file, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* A role without access.manage cannot be the first account's: nobody could then manage the
     * others. */
    CHECK(crux_bench_first_account_provision(USER, "operator", file, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_bench_first_account_provision("Bad Name", ROLE, file, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* None of those closed provisioning. */
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_OK);
}

static void an_unusable_store_never_opens_provisioning(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    size_t length = file_bytes(file, password, true);
    access_fake_reset();
    crux_access_test_reset();
    access_fake_store_state(CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(crux_access_init(policy()) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_ERR_DENIED);
}

static void a_failed_save_leaves_provisioning_open(void)
{
    char password[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 3u];
    uint8_t file[CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX + 1u];
    fresh_password(password, 24u);
    size_t length = file_bytes(file, password, true);
    access_test_boot(policy());
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_ERR_STORE);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
    CHECK(crux_bench_first_account_provision(USER, ROLE, file, length) == CRUX_ACCESS_OK);
}

int main(void)
{
    creates_the_account_with_the_role();
    a_file_without_a_newline_gives_the_same_account();
    the_newline_is_not_part_of_the_password();
    a_wrong_password_is_refused();
    a_boot_with_an_empty_store_creates_it_again();
    a_boot_that_kept_the_store_does_not_create_it_again();
    it_is_made_once_per_boot();
    a_password_that_breaks_the_file_rules_never_reaches_access();
    arguments_and_roles_are_checked();
    an_unusable_store_never_opens_provisioning();
    a_failed_save_leaves_provisioning_open();
    return access_test_finish("bench_first_account");
}
