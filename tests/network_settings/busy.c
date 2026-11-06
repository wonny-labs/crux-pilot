/* The Configuration service is held busy by another session's save while this service saves: the
 * save waits a bounded time, then fails with nothing half done. Also the calls that must not wait.
 */
#include "net_test.h"

#include <pthread.h>
#include <time.h>
#include <unistd.h>

static pthread_t holder;
static pthread_mutex_t hold_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hold_changed = PTHREAD_COND_INITIALIZER;
static bool hold_entered;
static bool hold_released;

static void wait_inside_the_write(void *context)
{
    (void)context;
    config_fault_write_hook(NULL, NULL);
    pthread_mutex_lock(&hold_lock);
    hold_entered = true;
    pthread_cond_broadcast(&hold_changed);
    while (!hold_released) {
        pthread_cond_wait(&hold_changed, &hold_lock);
    }
    pthread_mutex_unlock(&hold_lock);
}

static void *hold_main(void *argument)
{
    crux_config_service_save_report_t report;

    (void)argument;
    (void)crux_config_service_save(s_saver, &report);
    return NULL;
}

/* Holds the Configuration service's lock inside a save, from another thread. */
static void hold_service(void)
{
    crux_config_service_item_t item;

    hold_entered = false;
    hold_released = false;
    CHECK(crux_config_service_update(s_saver, NET_CFG_LIMIT, "6", 1, &item) ==
          CRUX_CONFIG_SERVICE_OK);
    config_fault_write_hook(wait_inside_the_write, NULL);
    CHECK(pthread_create(&holder, NULL, hold_main, NULL) == 0);
    pthread_mutex_lock(&hold_lock);
    while (!hold_entered) {
        pthread_cond_wait(&hold_changed, &hold_lock);
    }
    pthread_mutex_unlock(&hold_lock);
}

static void release_service(void)
{
    pthread_mutex_lock(&hold_lock);
    hold_released = true;
    pthread_cond_broadcast(&hold_changed);
    pthread_mutex_unlock(&hold_lock);
    CHECK(pthread_join(holder, NULL) == 0);
}

static void *release_later(void *argument)
{
    (void)argument;
    usleep(120000);
    pthread_mutex_lock(&hold_lock);
    hold_released = true;
    pthread_cond_broadcast(&hold_changed);
    pthread_mutex_unlock(&hold_lock);
    return NULL;
}

static void a_short_hold_is_waited_out(void)
{
    pthread_t releaser;
    crux_network_settings_t values;
    crux_network_state_t state;

    net_start();
    hold_service();
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_OK);
    CHECK(pthread_create(&releaser, NULL, release_later, NULL) == 0);
    CHECK(crux_network_settings_work_once());
    CHECK(pthread_join(releaser, NULL) == 0);
    CHECK(pthread_join(holder, NULL) == 0);
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE && state.has_saved);
    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_SAVED && net_same(&values, &net_set_a));
}

static void a_long_hold_fails_the_save_with_nothing_half_done(void)
{
    crux_network_settings_t values;
    crux_network_state_t state;

    net_start();
    hold_service();
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    /* The first update never got in, so nothing in memory or in the store changed. */
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    CHECK(state.save == CRUX_NETWORK_SAVE_FAILED && state.failure == CRUX_NETWORK_FAILURE_BUSY);
    CHECK(net_events_count() == 1u);
    CHECK(net_events_get(0)->event == CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED);
    release_service();
    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_NOT_STORED);
    /* The other session's save wrote its own parameter and none of ours. */
    CHECK(config_fault_write_log_count() == 1u);
    CHECK(strcmp(config_fault_write_log_key(0), NET_CFG_LIMIT) == 0);
    /* A new request works once the hold is gone. */
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_SAVED && net_same(&values, &net_set_a));
}

static void *work_main(void *argument)
{
    *(bool *)argument = crux_network_settings_work_once();
    return NULL;
}

/* The calls that a server thread makes do not wait, even while the worker waits on storage. */
static void the_server_thread_never_waits(void)
{
    pthread_t worker;
    bool ran = false;
    struct timespec start, end;

    net_start();
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_OK);
    hold_service();
    CHECK(pthread_create(&worker, NULL, work_main, &ran) == 0);
    usleep(50000);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (unsigned round = 0; round < 50u; ++round) {
        crux_network_state_t state;

        CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
        CHECK(state.save == CRUX_NETWORK_SAVE_PENDING);
        CHECK(crux_network_settings_request(s_both, &net_set_b, NULL) == CRUX_NETWORK_ERR_BUSY);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    CHECK(elapsed_ms < 100);
    release_service();
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(ran);
}

int main(void)
{
    net_run_case("a short hold is waited out", a_short_hold_is_waited_out);
    net_run_case("a long hold fails the save with nothing half done",
                 a_long_hold_fails_the_save_with_nothing_half_done);
    net_run_case("the server thread never waits", the_server_thread_never_waits);
    return net_finish("network_settings_busy");
}
