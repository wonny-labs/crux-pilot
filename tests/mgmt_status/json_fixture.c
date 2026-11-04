/* Prints status reports, one JSON document on each line, for check_openapi_status.py to hold to the
 * merged schema. Between them they hold every type, every failure word, the longest text and the
 * empty list, all produced by the service through a real session. */
#include "status_test.h"

static crux_status_t read_u32(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_U32;
    out->data.u32 = UINT32_MAX;
    return CRUX_OK;
}

static crux_status_t read_u64(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_U64;
    out->data.u64 = UINT64_MAX;
    return CRUX_OK;
}

static crux_status_t read_bool(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_BOOL;
    out->data.boolean = false;
    return CRUX_OK;
}

static crux_status_t read_text(void *context, crux_mgmt_status_value_t *out)
{
    out->type = CRUX_MGMT_STATUS_TEXT;
    snprintf(out->data.text, sizeof(out->data.text), "%s", (const char *)context);
    return CRUX_OK;
}

static crux_status_t read_failure(void *context, crux_mgmt_status_value_t *out)
{
    (void)out;
    return *(const crux_status_t *)context;
}

static const crux_status_t failures[] = {
    CRUX_ERR_INVALID_ARGUMENT, CRUX_ERR_OUT_OF_RANGE, CRUX_ERR_NOT_READY,
    CRUX_ERR_NO_DATA,          CRUX_ERR_IO,           CRUX_ERR_BUSY,
    CRUX_ERR_TIMEOUT,          CRUX_ERR_NO_RESOURCES, CRUX_ERR_NOT_SUPPORTED,
    CRUX_ERR_INVALID_STATE,
};

/* Every byte needs its escape, the longest a text item can be as JSON. */
static crux_status_t read_longest(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_TEXT;
    memset(out->data.text, '"', CRUX_MGMT_STATUS_TEXT_SIZE - 1u);
    return CRUX_OK;
}

static const crux_mgmt_status_item_t items[] = {
    {"app.a_u32", "app.read", read_u32, NULL},
    {"app.a_u64", "app.read", read_u64, NULL},
    {"app.a_bool", "app.read", read_bool, NULL},
    {"app.a_text", "app.read", read_text,
     (void *)"caf\xc3\xa9 \"quoted\" \\ path/\xe2\x80\xa8 end"},
    {"app.longest_text", "app.read", read_longest, NULL},
    {"app.failure_0", "app.read", read_failure, (void *)&failures[0]},
    {"app.failure_1", "app.read", read_failure, (void *)&failures[1]},
    {"app.failure_2", "app.read", read_failure, (void *)&failures[2]},
    {"app.failure_3", "app.read", read_failure, (void *)&failures[3]},
    {"app.failure_4", "app.read", read_failure, (void *)&failures[4]},
    {"app.failure_5", "app.read", read_failure, (void *)&failures[5]},
    {"app.failure_6", "app.read", read_failure, (void *)&failures[6]},
    {"app.failure_7", "app.read", read_failure, (void *)&failures[7]},
    {"app.failure_8", "app.read", read_failure, (void *)&failures[8]},
    {"app.failure_9", "app.read", read_failure, (void *)&failures[9]},
    /* A bad source, which the service turns into an item with a Base result. */
    {"app.bad_text", "app.read", read_text, (void *)"a\nb"},
};

static void print_report(crux_access_session_t session)
{
    static char out[16384];
    size_t length = 0;
    CHECK(crux_mgmt_status_report_json(session, out, sizeof(out), &length) == CRUX_OK);
    puts(out);
}

int main(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    config.project_items = items;
    config.project_item_count = sizeof(items) / sizeof(items[0]);
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);

    /* Everything a session can see, then each source failing. */
    print_report(s_admin);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
    status_fake_system(CRUX_ERR_IO, CRUX_OK, CRUX_WATCHDOG_ACTIVE);
    status_fake_clock(CRUX_ERR_NOT_READY, CRUX_MGMT_STATUS_CLOCK_UNSET);
    status_fake_uptime(CRUX_ERR_IO, 0);
    crux_mgmt_status_net_t net = {0};
    status_fake_net(CRUX_ERR_NOT_READY, net);
    print_report(s_viewer);

    /* Nothing visible, and a forced password change. */
    print_report(s_none);
    CHECK(crux_access_account_reset_password(s_admin, "viewer", ACCESS_TEST_PASSWORD,
                                             strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    print_report(access_test_session("viewer", ACCESS_TEST_PASSWORD, &access_test_web));

    return access_test_failures == 0 ? 0 : 1;
}
