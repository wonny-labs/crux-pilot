/* The status service on the thin providers and the real System, Time and Netif on Linux, with no
 * Configuration provider linked: it needs only the character rule's small library. The fake sources
 * in the other suites do not show that a provider fits, and source_mapping.c holds every mapping
 * with controlled answers. */
#include "mgmt_status_port.h"
#include "status_test.h"

#include <crux/time.h>
#include <crux/time_authority.h>

/* The shared fixture resets the fakes, which this composition does not have. */
void status_fake_reset(void)
{
}

static const char *const absent_when_not_linked[] = {
    "security.credential_state",
    "security.events_emitted",
};

static void providers_answer(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    /* Host reports reset causes unsupported, and the owner passes that on. */
    crux_reset_cause_t cause = {0};
    config.boot_reset_cause_status = crux_system_reset_cause_get(&cause);
    CHECK(config.boot_reset_cause_status == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);

    crux_mgmt_status_entry_t entry = entry_named(s_viewer, "system.uptime_ms");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_U64);
    /* The memory Time provider is not started, so it is not ready, and the entry says so. */
    expect_failed_item(s_viewer, "system.clock", CRUX_ERR_NOT_READY);
    /* Host has no watchdog running, which is the idle state, and observing it arms nothing. */
    expect_text_item(s_viewer, "system.watchdog_state", "idle");
    expect_failed_item(s_viewer, "reset.causes", CRUX_ERR_NOT_SUPPORTED);
    expect_failed_item(s_viewer, "reset.supported", CRUX_ERR_NOT_SUPPORTED);

    /* The Linux interface this composition selects, which is loopback. */
    entry = entry_named(s_viewer, "net.admin_up");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_BOOL);
    entry = entry_named(s_viewer, "net.ipv4");
    CHECK(entry.status == CRUX_OK || entry.status == CRUX_ERR_NO_DATA);

    expect_text_item(s_viewer, "version.platform", "1.2.3");

    /* A composition with no store and no events has no such items. */
    for (size_t i = 0; i < sizeof(absent_when_not_linked) / sizeof(absent_when_not_linked[0]);
         ++i) {
        CHECK(!listed(s_viewer, absent_when_not_linked[i]));
    }
}

static void clock_words(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    CHECK(crux_realtime_init() == CRUX_OK);
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    expect_text_item(s_viewer, "system.clock", "unset");

    /* The real authority behind the provider, by each source that can set it. The service answers
     * from its snapshot, so this asks the provider, which has no rate limit. */
    const crux_realtime_t now = {CRUX_TIME_MIN_UNIX_SECONDS + 1, 0};
    static const struct {
        crux_realtime_source_t source;
        crux_mgmt_status_clock_t expected;
    } sources[] = {{CRUX_REALTIME_SOURCE_RTC, CRUX_MGMT_STATUS_CLOCK_RTC},
                   {CRUX_REALTIME_SOURCE_MANUAL, CRUX_MGMT_STATUS_CLOCK_MANUAL},
                   {CRUX_REALTIME_SOURCE_EXTERNAL, CRUX_MGMT_STATUS_CLOCK_EXTERNAL}};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        CHECK(crux_realtime_set(&now, sources[i].source, false) == CRUX_OK);
        crux_mgmt_status_clock_t clock = CRUX_MGMT_STATUS_CLOCK_UNSET;
        CHECK(crux_mgmt_status_port_clock(&clock) == CRUX_OK);
        CHECK(clock == sources[i].expected);
    }
}

int main(void)
{
    run_case("providers answer", providers_answer);
    run_case("clock words", clock_words);
    return suite_finish("MgmtStatus.real_sources");
}
