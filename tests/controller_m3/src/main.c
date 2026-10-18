#include "controller_health.h"
#include "device_input.h"

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

ZTEST(controller_m3, test_device_contract) {
    int value = 42;
    zassert_equal(controller_device_read(NULL), -EINVAL);
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
    zassert_equal(controller_device_init(), 0);
    zassert_equal(controller_device_read(&value), 0);
    zassert_true(value == 0 || value == 1);
#else
    zassert_equal(controller_device_init(), -ENODEV);
    zassert_equal(controller_device_read(&value), -ENODEV);
    zassert_equal(value, 42, "failed read modified caller output");
#endif
}

ZTEST(controller_m3, test_active_low_transitions_or_unavailable) {
    int value = 42;
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
    const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
    zassert_equal(controller_device_init(), 0);
    for (int i = 0; i < 20; ++i) {
        int raw = i % 2;
        zassert_equal(gpio_emul_input_set_dt(&button, raw), 0);
        zassert_equal(controller_device_read(&value), 0);
        zassert_equal(value, 1 - raw, "active-low conversion is wrong");
    }
#else
    zassert_equal(controller_device_init(), -ENODEV);
    zassert_equal(controller_device_read(&value), -ENODEV);
    zassert_equal(value, 42);
#endif
}

ZTEST(controller_m3, test_progress_stall_delayed_observer_and_recovery) {
    controller_health_t health;
    zassert_true(controller_health_init(&health, 500));
    zassert_false(controller_health_observe(&health, 0, -1, 0, false));
    zassert_true(controller_health_observe(&health, 100, 100, 1, true));
    zassert_false(controller_health_observe(&health, 601, 100, 1, true));
    zassert_false(controller_health_observe(&health, 2000, 200, 2, true));
    zassert_false(controller_health_observe(&health, 2100, 2100, 3, false));
    zassert_false(controller_health_observe(&health, 2200, 2100, 3, true));
    zassert_true(controller_health_observe(&health, 2300, 2300, 4, true));
}

ZTEST(controller_m3, test_required_service_failure_is_not_ready) {
    zassert_equal(controller_readiness(true, false, true, true), CONTROLLER_NOT_READY);
    zassert_equal(controller_readiness(true, true, false, true), CONTROLLER_NOT_READY);
    zassert_equal(controller_readiness(true, true, true, false), CONTROLLER_DEGRADED);
    zassert_equal(controller_readiness(true, true, true, true), CONTROLLER_READY);
}

ZTEST_SUITE(controller_m3, NULL, NULL, NULL, NULL, NULL);
