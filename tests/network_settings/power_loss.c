/* A save cut by a power loss between any two of its five writes, from each state the store can be
 * in. After each cut the boot's own decision runs on what the store holds, and a mixed set must
 * never be the one that boots. */
#include "net_test.h"

#include <sys/wait.h>
#include <unistd.h>

typedef enum { EMPTY, SAVED_A, SAVED_A_RECOVERED, ONLY_FLAG_FALSE } scenario_t;

/* The cases of a reversed table set this, so that the order of the table is the opposite. */
static bool reversed_table;

static void prepare(scenario_t scenario)
{
    config_fault_reset();
    switch (scenario) {
    case SAVED_A:
        net_store_set(&net_set_a, true);
        break;
    case SAVED_A_RECOVERED:
        net_store_set(&net_set_a, false);
        break;
    case ONLY_FLAG_FALSE:
        net_store_bool(CRUX_NETWORK_SETTINGS_KEY_USE_SAVED, false);
        break;
    case EMPTY:
    default:
        break;
    }
    net_start_on_store_with((net_options_t){true, 0, reversed_table});
}

/* What the boot must decide when `stored` of the five writes have reached the store. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test helper
static crux_network_boot_reason_t expected_reason(scenario_t scenario, size_t stored,
                                                  crux_network_settings_t *values)
{
    const bool whole = stored >= 5u;

    switch (scenario) {
    case SAVED_A:
        if (stored == 0u) {
            *values = net_set_a;
            return CRUX_NETWORK_BOOT_SAVED;
        }
        /* The check key is the fourth write, and the old flag is still true after it. */
        if (stored >= 4u) {
            *values = net_set_b;
            return CRUX_NETWORK_BOOT_SAVED;
        }
        return CRUX_NETWORK_BOOT_CHECK_MISMATCH;
    case SAVED_A_RECOVERED:
    case ONLY_FLAG_FALSE:
        *values = net_set_b;
        return whole ? CRUX_NETWORK_BOOT_SAVED : CRUX_NETWORK_BOOT_DISABLED;
    case EMPTY:
    default:
        *values = net_set_b;
        if (stored == 0u) {
            return CRUX_NETWORK_BOOT_NOT_STORED;
        }
        return whole ? CRUX_NETWORK_BOOT_SAVED : CRUX_NETWORK_BOOT_INCOMPLETE;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test helper
static void run_cut(scenario_t scenario, size_t cut, bool last_write_lands)
{
    crux_network_settings_t reboot_values = {0};
    crux_network_settings_t expected_values = {0};
    crux_network_state_t state;
    /* A write that lands and is not acknowledged stores one more record than was acknowledged. */
    size_t stored = cut + ((last_write_lands && cut < 5u) ? 1u : 0u);
    crux_network_boot_reason_t expected = expected_reason(scenario, stored, &expected_values);
    crux_network_boot_reason_t reason;

    prepare(scenario);
    net_power_cut_after(cut, last_write_lands);
    CHECK(crux_network_settings_request(s_both, &net_set_b, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());

    reason = net_reboot_decision(&reboot_values);
    /* A mixed set is often not even a usable one, which the boot reports first. */
    if (expected == CRUX_NETWORK_BOOT_CHECK_MISMATCH &&
        reason == CRUX_NETWORK_BOOT_VALUES_INVALID) {
        reason = expected;
    }
    if (reason != expected) {
        printf("  cut %zu lands %d: reason %d, expected %d\n", cut, last_write_lands, (int)reason,
               (int)expected);
    }
    CHECK(reason == expected);
    if (reason == CRUX_NETWORK_BOOT_SAVED) {
        /* Only ever a whole set: the old one or the new one. */
        CHECK(net_same(&reboot_values, &expected_values));
        CHECK(net_same(&reboot_values, &net_set_a) || net_same(&reboot_values, &net_set_b));
    }
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    if (cut >= 5u) {
        CHECK(state.save == CRUX_NETWORK_SAVE_NONE && state.has_saved);
        CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 1u);
    } else {
        CHECK(state.save == CRUX_NETWORK_SAVE_FAILED && !state.has_saved);
        CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED) == 1u);
        CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 0u);
    }
}

#define LANDS_NOLAND false
#define LANDS_LAND true
#define CUT_CASE(scenario, cut, lands)                                                             \
    static void scenario##_cut##cut##_##lands(void)                                                \
    {                                                                                              \
        run_cut(scenario, cut, LANDS_##lands);                                                     \
    }
#define CUTS(scenario, lands)                                                                      \
    CUT_CASE(scenario, 0, lands)                                                                   \
    CUT_CASE(scenario, 1, lands)                                                                   \
    CUT_CASE(scenario, 2, lands)                                                                   \
    CUT_CASE(scenario, 3, lands)                                                                   \
    CUT_CASE(scenario, 4, lands)                                                                   \
    CUT_CASE(scenario, 5, lands)
#define ALL_CUTS(scenario)                                                                         \
    CUTS(scenario, NOLAND)                                                                         \
    CUTS(scenario, LAND)

ALL_CUTS(EMPTY)
ALL_CUTS(SAVED_A)
ALL_CUTS(SAVED_A_RECOVERED)
ALL_CUTS(ONLY_FLAG_FALSE)

#define REGISTER_CUTS(scenario, lands)                                                             \
    net_run_case(#scenario " cut 0 " #lands, scenario##_cut0_##lands);                             \
    net_run_case(#scenario " cut 1 " #lands, scenario##_cut1_##lands);                             \
    net_run_case(#scenario " cut 2 " #lands, scenario##_cut2_##lands);                             \
    net_run_case(#scenario " cut 3 " #lands, scenario##_cut3_##lands);                             \
    net_run_case(#scenario " cut 4 " #lands, scenario##_cut4_##lands);                             \
    net_run_case(#scenario " cut 5 " #lands, scenario##_cut5_##lands)
#define REGISTER_ALL(scenario)                                                                     \
    REGISTER_CUTS(scenario, NOLAND);                                                               \
    REGISTER_CUTS(scenario, LAND)

#define REVERSED_CASE(scenario, cut, lands)                                                        \
    static void reversed_##scenario##_cut##cut##_##lands(void)                                     \
    {                                                                                              \
        reversed_table = true;                                                                     \
        run_cut(scenario, cut, LANDS_##lands);                                                     \
    }
#define REVERSED_CUTS(scenario, lands)                                                             \
    REVERSED_CASE(scenario, 0, lands)                                                              \
    REVERSED_CASE(scenario, 1, lands)                                                              \
    REVERSED_CASE(scenario, 2, lands)                                                              \
    REVERSED_CASE(scenario, 3, lands)                                                              \
    REVERSED_CASE(scenario, 4, lands)                                                              \
    REVERSED_CASE(scenario, 5, lands)
REVERSED_CUTS(SAVED_A, NOLAND)
REVERSED_CUTS(SAVED_A, LAND)
REVERSED_CUTS(SAVED_A_RECOVERED, NOLAND)
REVERSED_CUTS(SAVED_A_RECOVERED, LAND)

#define REGISTER_REVERSED(scenario, lands)                                                         \
    net_run_case("reversed table, " #scenario " cut 0 " #lands,                                    \
                 reversed_##scenario##_cut0_##lands);                                              \
    net_run_case("reversed table, " #scenario " cut 1 " #lands,                                    \
                 reversed_##scenario##_cut1_##lands);                                              \
    net_run_case("reversed table, " #scenario " cut 2 " #lands,                                    \
                 reversed_##scenario##_cut2_##lands);                                              \
    net_run_case("reversed table, " #scenario " cut 3 " #lands,                                    \
                 reversed_##scenario##_cut3_##lands);                                              \
    net_run_case("reversed table, " #scenario " cut 4 " #lands,                                    \
                 reversed_##scenario##_cut4_##lands);                                              \
    net_run_case("reversed table, " #scenario " cut 5 " #lands, reversed_##scenario##_cut5_##lands)

/* A record that a power loss tore in half reads as an invalid one, so no set boots. */
static void a_torn_record_never_boots(void)
{
    static const char *const keys[5] = {
        CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
        CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK,
        CRUX_NETWORK_SETTINGS_KEY_USE_SAVED};
    const uint8_t whole[8] = {1, CRUX_CONFIG_U32, 1, 0, 1, 2, 3, 4};
    crux_network_settings_t values;

    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_start_on_store();
    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_SAVED);
    for (size_t index = 0; index < 5u; ++index) {
        for (size_t length = 0; length < sizeof(whole); ++length) {
            net_store_set(&net_set_a, true);
            config_fault_record(keys[index], whole, length);
            CHECK(net_reboot_decision(&values) != CRUX_NETWORK_BOOT_SAVED);
        }
    }
}

/* Every mix of the records of two stored sets, from the store itself and not from the function. */
static void no_mix_of_two_stored_sets_boots(void)
{
    crux_network_settings_t values;
    unsigned mixes = 0;
    static const char *const keys[4] = {
        CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
        CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK};

    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_start_on_store();
    for (unsigned pick = 0; pick < 16u; ++pick) {
        crux_network_settings_t b = net_stored_form(&net_set_b);
        uint32_t b_values[4] = {b.address, b.prefix_length, b.gateway,
                                crux_network_settings_check_key(&b)};
        crux_network_boot_reason_t reason;

        net_store_set(&net_set_a, true);
        for (unsigned index = 0; index < 4u; ++index) {
            if (pick & (1u << index)) {
                net_store_u32(keys[index], b_values[index]);
            }
        }
        reason = net_reboot_decision(&values);
        if (pick == 0u) {
            CHECK(reason == CRUX_NETWORK_BOOT_SAVED && net_same(&values, &net_set_a));
        } else if (pick == 15u) {
            CHECK(reason == CRUX_NETWORK_BOOT_SAVED && net_same(&values, &net_set_b));
        } else {
            ++mixes;
            CHECK(reason == CRUX_NETWORK_BOOT_CHECK_MISMATCH ||
                  reason == CRUX_NETWORK_BOOT_VALUES_INVALID);
        }
    }
    CHECK(mixes == 14u);
}

/* Runs one configuration in a child of its own, because the services start once. */
static void in_child(void (*body)(size_t, size_t, bool), size_t first, size_t second, bool third)
{
    int status = 0;
    pid_t child;

    fflush(NULL);
    child = fork();
    if (child == 0) {
        body(first, second, third);
        fflush(NULL);
        _exit(access_test_failures == 0 ? 0 : 1);
    }
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("  failed: %zu %zu %d\n", first, second, third);
        CHECK(false);
    }
}

/* The check key is saved after the values, whatever the order of the table: a write of the values
 * that fails at the first of them leaves the old set whole, which a check key saved first would
 * not. In a reversed table the first of them is the gateway. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the signature of in_child()
static void a_value_write_fails(size_t failing, size_t unused, bool unused_flag)
{
    static const char *const values[3] = {CRUX_NETWORK_SETTINGS_KEY_GATEWAY,
                                          CRUX_NETWORK_SETTINGS_KEY_PREFIX,
                                          CRUX_NETWORK_SETTINGS_KEY_ADDRESS};
    crux_network_settings_t reboot_values;
    crux_network_boot_reason_t reason;

    (void)unused;
    (void)unused_flag;
    reversed_table = true;
    prepare(SAVED_A);
    config_fault_write_fail_key(values[failing], CRUX_CONFIG_ERR_IO);
    CHECK(crux_network_settings_request(s_both, &net_set_b, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    reason = net_reboot_decision(&reboot_values);
    if (failing == 0u) {
        CHECK(config_fault_write_log_count() == 1u);
        CHECK(reason == CRUX_NETWORK_BOOT_SAVED && net_same(&reboot_values, &net_set_a));
    } else {
        CHECK(reason != CRUX_NETWORK_BOOT_SAVED);
    }
}

static void check_key_after_the_values_in_a_reversed_table(void)
{
    for (size_t failing = 0; failing < 3u; ++failing) {
        in_child(a_value_write_fails, failing, 0u, false);
    }
}

/* After a failed save, the recovery command: whatever the failed key was, and wherever the power is
 * cut during the recovery, the failed set never boots. */
static void recovery_after_a_failed_save(size_t failing, size_t cut, bool last_write_lands)
{
    static const char *const keys[5] = {
        CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
        CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK,
        CRUX_NETWORK_SETTINGS_KEY_USE_SAVED};
    crux_network_settings_t values = {0};
    crux_network_boot_reason_t reason;

    prepare(SAVED_A);
    config_fault_write_fail_key(keys[failing], CRUX_CONFIG_ERR_IO);
    CHECK(crux_network_settings_request(s_both, &net_set_b, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    net_power_cut_after(cut, last_write_lands);
    (void)crux_network_settings_recover(s_both);
    reason = net_reboot_decision(&values);
    if (reason == CRUX_NETWORK_BOOT_SAVED) {
        /* The old set, which a failure before the first overwrite leaves, or the new set when the
         * failure came at the flag, which is the last write and leaves it whole. Nothing else. */
        bool whole_new = failing == 4u && net_same(&values, &net_set_b);

        if (!net_same(&values, &net_set_a) && !whole_new) {
            printf("  failed %zu, cut %zu, lands %d: a set that failed boots\n", failing, cut,
                   last_write_lands);
            CHECK(false);
        }
    }
}

/* After a failed save, another session saves the configuration. It writes every mark, and the
 * failed save left some, the check key of the failed set among them. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the signature of in_child()
static void a_foreign_save_after_a_failed_save(size_t failing, size_t unused, bool unused_flag)
{
    static const char *const keys[5] = {
        CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
        CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK,
        CRUX_NETWORK_SETTINGS_KEY_USE_SAVED};
    crux_config_service_save_report_t report;
    crux_network_settings_t values = {0};
    crux_network_boot_reason_t reason;

    (void)unused;
    (void)unused_flag;
    prepare(SAVED_A);
    config_fault_write_fail_key(keys[failing], CRUX_CONFIG_ERR_IO);
    CHECK(crux_network_settings_request(s_both, &net_set_b, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_OK);
    reason = net_reboot_decision(&values);
    if (reason == CRUX_NETWORK_BOOT_SAVED) {
        bool whole_new = failing == 4u && net_same(&values, &net_set_b);

        if (!net_same(&values, &net_set_a) && !whole_new) {
            printf("  failed %zu: a foreign save completes the failed set\n", failing);
            CHECK(false);
        }
    }
}

static void a_foreign_save_after_each_failed_save(void)
{
    for (size_t failing = 0; failing < 5u; ++failing) {
        in_child(a_foreign_save_after_a_failed_save, failing, 0u, false);
    }
}

static void recovery_cut_after_a_failed_save(void)
{
    for (size_t failing = 0; failing < 5u; ++failing) {
        for (size_t cut = 0; cut <= 3u; ++cut) {
            in_child(recovery_after_a_failed_save, failing, cut, false);
            in_child(recovery_after_a_failed_save, failing, cut, true);
        }
    }
}

int main(void)
{
    REGISTER_ALL(EMPTY);
    REGISTER_ALL(SAVED_A);
    REGISTER_ALL(SAVED_A_RECOVERED);
    REGISTER_ALL(ONLY_FLAG_FALSE);
    REGISTER_REVERSED(SAVED_A, NOLAND);
    REGISTER_REVERSED(SAVED_A, LAND);
    REGISTER_REVERSED(SAVED_A_RECOVERED, NOLAND);
    REGISTER_REVERSED(SAVED_A_RECOVERED, LAND);
    net_run_case("check key after the values in a reversed table",
                 check_key_after_the_values_in_a_reversed_table);
    net_run_case("recovery cut after a failed save", recovery_cut_after_a_failed_save);
    net_run_case("a foreign save after each failed save", a_foreign_save_after_each_failed_save);
    net_run_case("a torn record never boots", a_torn_record_never_boots);
    net_run_case("no mix of two stored sets boots", no_mix_of_two_stored_sets_boots);
    return net_finish("network_settings_power_loss");
}
