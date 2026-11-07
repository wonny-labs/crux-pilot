/*
 * Physical M5 fixture for Serial (#27, Group B) and System watchdog/reset (#30, Group D).
 *
 * One image, observed only through raw LPUART1 Crux Logging records (see
 * tests/target/m5_serial_loopback_oracle.py and m5_watchdog_oracle.py).
 *
 * Group B uses LPUART3 in internal loopback mode (devicetree nxp,loopback). Pins, connector and
 * an external peer are not exercised; the PASS record says so explicitly.
 *
 * Group D: the first boot reads the reset cause before any clear, runs Group B, starts the
 * watchdog once, feeds it five times, then deliberately stops feeding. The watchdog resets the
 * CPU; the next boot (marker in no-init RAM) only reports the latched cause. No boot ever starts
 * the watchdog a second time, and there is no software reset.
 */
#include <crux/logging.h>
#include <crux/serial.h>
#include <crux/status.h>
#include <crux/system.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>

#include "phase.h"

#define SERIAL_TAG "m5-serial"
#define WATCHDOG_TAG "m5-watchdog"
#define PAYLOAD_BYTES 16U
#define SERIAL_TIMEOUT_MS 300U
#define READ_BUDGET_MS 1000
#define WATCHDOG_TIMEOUT_MS 2000U
#define FEED_COUNT 5U
#define FEED_INTERVAL_MS 500
#define NO_RESET_WAIT_MS 10000

static struct {
    uint32_t magic;
    uint32_t phase;
} marker __noinit;

/* Store the phase and push it out of the data cache: the marker may live in cached SDRAM. */
static void marker_store(uint32_t phase)
{
    marker.phase = phase;
    (void)sys_cache_data_flush_range(&marker, sizeof(marker));
}

static void emit(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void emit(const char *tag, const char *format, ...)
{
    char text[192];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    (void)crux_log_write(CRUX_LOG_INFO, tag, text);
    log_flush();
}

static void frame_build(uint8_t frame[PAYLOAD_BYTES], uint32_t nonce, uint32_t sequence)
{
    static const uint8_t magic[4] = {'M', '5', 'B', '1'};

    memcpy(frame, magic, sizeof(magic));
    for (unsigned i = 0; i < 4; ++i) {
        frame[4 + i] = (uint8_t)(nonce >> (24 - 8 * i));
        frame[8 + i] = (uint8_t)(sequence >> (24 - 8 * i));
        frame[12 + i] = 0xA5;
    }
}

typedef struct {
    crux_status_t write_status;
    size_t written;
    crux_status_t read_status;
    size_t read;
    bool match;
} transfer_t;

static bool transfer(crux_serial_t port, uint32_t nonce, uint32_t sequence, transfer_t *out)
{
    uint8_t request[PAYLOAD_BYTES], response[PAYLOAD_BYTES] = {0};
    int64_t deadline = k_uptime_get() + READ_BUDGET_MS;

    *out = (transfer_t){0};
    frame_build(request, nonce, sequence);
    out->write_status =
        crux_serial_write(port, request, sizeof(request), SERIAL_TIMEOUT_MS, &out->written);
    out->read_status = out->write_status == CRUX_OK ? CRUX_OK : CRUX_ERR_INVALID_STATE;
    while (out->write_status == CRUX_OK && out->read < sizeof(response) &&
           k_uptime_get() < deadline) {
        size_t count = 0;

        out->read_status =
            crux_serial_read(port, response + out->read, sizeof(response) - out->read, 200, &count);
        if (out->read_status != CRUX_OK && out->read_status != CRUX_ERR_TIMEOUT) {
            break;
        }
        out->read += count;
    }
    if (out->read == sizeof(response)) {
        out->read_status = CRUX_OK;
    }
    out->match = out->read == sizeof(response) && memcmp(request, response, sizeof(request)) == 0;
    emit(SERIAL_TAG,
         "M5B_XFER_V1 seq=%u write_status=%d written=%u read_status=%d read=%u match=%d",
         (unsigned)sequence, (int)out->write_status, (unsigned)out->written, (int)out->read_status,
         (unsigned)out->read, out->match ? 1 : 0);
    return out->write_status == CRUX_OK && out->written == PAYLOAD_BYTES &&
           out->read_status == CRUX_OK && out->read == PAYLOAD_BYTES && out->match;
}

static void run_serial(void)
{
    const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
    crux_serial_t port;
    transfer_t first, second;
    uint8_t scratch[PAYLOAD_BYTES];
    size_t count = 0;
    uint32_t nonce = k_cycle_get_32();
    crux_status_t status = crux_serial_open(0, &config, &port);

    emit(SERIAL_TAG,
         "M5B_BEGIN_V1 target=evkb_b port=lpuart3 mode=internal_loopback baud=115200 "
         "open_status=%d",
         (int)status);
    if (status != CRUX_OK) {
        emit(SERIAL_TAG, "M5B_RESULT_V1 result=FAIL reason=open status=%d", (int)status);
        return;
    }
    bool ok = transfer(port, nonce, 1, &first);

    int64_t started = k_uptime_get();
    crux_status_t timeout_status =
        crux_serial_read(port, scratch, sizeof(scratch), SERIAL_TIMEOUT_MS, &count);
    int64_t elapsed = k_uptime_get() - started;

    emit(SERIAL_TAG, "M5B_TIMEOUT_V1 read_status=%d transferred=%u timeout_ms=%u elapsed_ms=%d",
         (int)timeout_status, (unsigned)count, SERIAL_TIMEOUT_MS, (int)elapsed);
    ok = ok && timeout_status == CRUX_ERR_TIMEOUT && count == 0;
    ok = transfer(port, nonce, 2, &second) && ok;

    crux_status_t closed = crux_serial_close(&port);
    crux_status_t reopened = crux_serial_open(0, &config, &port);
    crux_status_t reclosed =
        reopened == CRUX_OK ? crux_serial_close(&port) : CRUX_ERR_INVALID_STATE;

    emit(SERIAL_TAG, "M5B_CLOSE_V1 close_status=%d reopen_status=%d reclose_status=%d", (int)closed,
         (int)reopened, (int)reclosed);
    ok = ok && closed == CRUX_OK && reopened == CRUX_OK && reclosed == CRUX_OK;
    emit(SERIAL_TAG, ok ? "M5B_RESULT_V1 result=PASS limits=internal_loopback_pins_not_exercised"
                        : "M5B_RESULT_V1 result=FAIL reason=sequence");
}

static const char *marker_name(bool valid, uint32_t phase)
{
    if (!valid) {
        return "none";
    }
    switch (phase) {
    case M5_MARKER_NONE:
        return "none";
    case M5_MARKER_WD_ARMED:
        return "wd_armed";
    case M5_MARKER_DONE:
        return "done";
    default:
        return "invalid";
    }
}

static void status_record(const char *stage)
{
    crux_system_status_t system;
    crux_status_t status = crux_system_status_get(&system);

    if (status != CRUX_OK) {
        emit(WATCHDOG_TAG, "M5D_STATUS_V1 stage=%s wd_status=%d wd_state=-1 timeout_ms=0", stage,
             (int)status);
        return;
    }
    emit(WATCHDOG_TAG, "M5D_STATUS_V1 stage=%s wd_status=%d wd_state=%d timeout_ms=%u", stage,
         (int)system.watchdog_status, (int)system.watchdog_state,
         (unsigned)system.watchdog_timeout_ms);
}

static int watchdog_state(void)
{
    crux_system_status_t system;

    return crux_system_status_get(&system) == CRUX_OK ? (int)system.watchdog_state : -1;
}

static void idle_forever(void)
{
    for (;;) {
        k_sleep(K_FOREVER);
    }
}

static void run_arm(void)
{
    crux_watchdog_t watchdog;
    crux_status_t status;
    bool started;

    run_serial();
    status = crux_watchdog_start(WATCHDOG_TIMEOUT_MS, &watchdog);
    started = status == CRUX_OK;
    /* Record the attempt before anything slow, so a reset can never find a stale marker. */
    marker_store(m5_marker_after_arm(started));
    emit(WATCHDOG_TAG, "M5D_WDT_START_V1 status=%d timeout_ms=%u state=%d", (int)status,
         WATCHDOG_TIMEOUT_MS, watchdog_state());
    if (!started) {
        emit(WATCHDOG_TAG, "M5D_RESULT_V1 result=FAIL reason=start status=%d", (int)status);
        idle_forever();
    }
    for (unsigned index = 1; index <= FEED_COUNT; ++index) {
        k_msleep(FEED_INTERVAL_MS);
        status = crux_watchdog_feed(watchdog);
        emit(WATCHDOG_TAG, "M5D_FEED_V1 index=%u status=%d", index, (int)status);
        if (status != CRUX_OK) {
            emit(WATCHDOG_TAG, "M5D_RESULT_V1 result=FAIL reason=feed status=%d", (int)status);
            idle_forever();
        }
    }
    emit(WATCHDOG_TAG, "M5D_FEED_STOP_V1 fed=%u timeout_ms=%u", FEED_COUNT, WATCHDOG_TIMEOUT_MS);
    /* Intentionally no further feed: the watchdog must reset the CPU. */
    k_msleep(NO_RESET_WAIT_MS);
    emit(WATCHDOG_TAG, "M5D_NO_RESET_V1 waited_ms=%d", NO_RESET_WAIT_MS);
    idle_forever();
}

static void run_report(const crux_reset_cause_t *cause, crux_status_t cause_status)
{
    crux_reset_cause_t after = {0};
    crux_status_t clear_status, readback_status;
    bool observed = cause_status == CRUX_OK &&
                    (cause->supported & CRUX_RESET_CAUSE_WATCHDOG) != 0 &&
                    (cause->causes & CRUX_RESET_CAUSE_WATCHDOG) != 0;

    status_record("post_reset");
    clear_status = crux_system_reset_cause_clear();
    readback_status = crux_system_reset_cause_get(&after);
    emit(WATCHDOG_TAG, "M5D_CLEAR_V1 status=%d readback_status=%d readback_causes=0x%x",
         (int)clear_status, (int)readback_status, (unsigned)after.causes);
    marker_store(M5_MARKER_DONE);
    emit(WATCHDOG_TAG, "M5D_RESULT_V1 result=%s watchdog_bit=%d causes=0x%x",
         observed ? "PASS" : "INCONCLUSIVE", observed ? 1 : 0,
         (unsigned)(cause_status == CRUX_OK ? cause->causes : 0));
    idle_forever();
}

int main(void)
{
    bool valid = marker.magic == M5_MARKER_MAGIC;
    uint32_t stored = valid ? marker.phase : M5_MARKER_NONE;
    crux_reset_cause_t cause = {0};
    /* Read the latched cause before any clear or any other System operation. */
    crux_status_t cause_status = crux_system_reset_cause_get(&cause);
    bool cause_known =
        cause_status == CRUX_OK && (cause.supported & CRUX_RESET_CAUSE_WATCHDOG) != 0;
    bool cause_watchdog = cause_known && (cause.causes & CRUX_RESET_CAUSE_WATCHDOG) != 0;
    m5_phase_t phase = m5_decide_phase(valid, stored, cause_known, cause_watchdog);

    (void)crux_log_set_level(CRUX_LOG_INFO);
    if (phase == M5_PHASE_ARM) {
        marker.magic = M5_MARKER_MAGIC;
        marker_store(M5_MARKER_NONE);
    }
    emit(WATCHDOG_TAG, "M5D_BOOT_V1 phase=%s cause_status=%d supported=0x%x causes=0x%x marker=%s",
         phase == M5_PHASE_ARM ? "arm" : (phase == M5_PHASE_REPORT ? "report" : "observe"),
         (int)cause_status, (unsigned)cause.supported, (unsigned)cause.causes,
         marker_name(valid, stored));
    status_record(phase == M5_PHASE_ARM ? "initial" : "post_reset_observe");
    if (phase == M5_PHASE_ARM) {
        run_arm();
    }
    if (phase == M5_PHASE_REPORT) {
        run_report(&cause, cause_status);
    }
    idle_forever();
    return 0;
}
