/* Another session's save of the configuration lands between two steps of this service's save. The
 * Configuration service saves marks and not names, so it writes whatever is marked at that moment,
 * which may be part of a set. The check key and the order of the phases must make that harmless. */
#include "net_test.h"

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_config_service_result_t
__real_crux_config_service_try_update(const char *name, const crux_config_value_t *candidate);
crux_config_service_result_t
__wrap_crux_config_service_try_update(const char *name, const crux_config_value_t *candidate);
// NOLINTEND(bugprone-reserved-identifier)

static size_t calls;
static size_t inject_after; /* 1 is after the first try_update. 0 is never. */
static bool cut_after_inject;
static unsigned injected;
static crux_network_boot_reason_t reason_after_inject;
static crux_network_settings_t values_after_inject;

crux_config_service_result_t
__wrap_crux_config_service_try_update(const char *name, const crux_config_value_t *candidate)
{
    crux_config_service_result_t result = __real_crux_config_service_try_update(name, candidate);

    if (++calls == inject_after) {
        crux_config_service_save_report_t report;

        CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_OK);
        ++injected;
        reason_after_inject = net_reboot_decision(&values_after_inject);
        if (cut_after_inject) {
            net_power_cut_after(0u, false);
        }
    }
    return result;
}

static void run(bool saved_before, size_t after, bool cut)
{
    crux_network_settings_t values;
    crux_network_state_t state;

    config_fault_reset();
    if (saved_before) {
        net_store_set(&net_set_a, true);
    }
    net_start_on_store();
    calls = 0;
    inject_after = after;
    cut_after_inject = cut;
    CHECK(crux_network_settings_request(s_both, &net_set_b, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    CHECK(injected == 1u);
    /* Right after the foreign save, the store was never a mixed set that boots. */
    CHECK(reason_after_inject != CRUX_NETWORK_BOOT_SAVED ||
          net_same(&values_after_inject, &net_set_a) || net_same(&values_after_inject, &net_set_b));
    crux_network_boot_reason_t reason = net_reboot_decision(&values);
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    /* After the last update nothing is left to write, so the cut finds nothing to cut. */
    if (cut && after < 5u) {
        CHECK(state.save == CRUX_NETWORK_SAVE_FAILED);
        CHECK(reason != CRUX_NETWORK_BOOT_SAVED || net_same(&values, &net_set_a) ||
              net_same(&values, &net_set_b));
    } else {
        /* Everything else was written, so the whole new set is stored and it boots. */
        CHECK(state.save == CRUX_NETWORK_SAVE_NONE && state.has_saved);
        CHECK(reason == CRUX_NETWORK_BOOT_SAVED && net_same(&values, &net_set_b));
    }
}

#define CASES(saved, after)                                                                        \
    static void saved##_after##after(void)                                                         \
    {                                                                                              \
        run(STORE_##saved, after, false);                                                          \
    }                                                                                              \
    static void saved##_after##after##_cut(void)                                                   \
    {                                                                                              \
        run(STORE_##saved, after, true);                                                           \
    }
#define ALL(saved)                                                                                 \
    CASES(saved, 1)                                                                                \
    CASES(saved, 2)                                                                                \
    CASES(saved, 3)                                                                                \
    CASES(saved, 4)                                                                                \
    CASES(saved, 5)
#define STORE_fresh false
#define STORE_old true
ALL(fresh)
ALL(old)

#define REGISTER(saved, after)                                                                     \
    net_run_case(#saved " store, foreign save after update " #after, saved##_after##after);        \
    net_run_case(#saved " store, foreign save after update " #after ", then a power cut",          \
                 saved##_after##after##_cut)
#define REGISTER_ALL(saved)                                                                        \
    REGISTER(saved, 1);                                                                            \
    REGISTER(saved, 2);                                                                            \
    REGISTER(saved, 3);                                                                            \
    REGISTER(saved, 4);                                                                            \
    REGISTER(saved, 5)

int main(void)
{
    REGISTER_ALL(fresh);
    REGISTER_ALL(old);
    return net_finish("network_settings_foreign_save");
}
