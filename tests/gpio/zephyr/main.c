#include "consumer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static const struct gpio_dt_spec test_line = GPIO_DT_SPEC_GET(DT_ALIAS(crux_gpio0), gpios);

ZTEST(gpio, test_native_input_is_reported_as_logical_state)
{
    bool active = true;
    zassert_true(gpio_is_ready_dt(&test_line));
    zassert_equal(crux_gpio_configure_input(CRUX_GPIO_LINE_0), CRUX_OK);
    zassert_equal(gpio_emul_input_set_dt(&test_line, 1), 0);
    zassert_equal(read_logical_input(&active), CRUX_OK);
    zassert_false(active);

    zassert_equal(gpio_emul_input_set_dt(&test_line, 0), 0);
    zassert_equal(read_logical_input(&active), CRUX_OK);
    zassert_true(active);
}

ZTEST(gpio, test_argument_errors)
{
    bool active = true;
    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, NULL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_gpio_configure_input(UINT32_MAX), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(crux_gpio_configure_input(CRUX_GPIO_LINE_0), CRUX_OK);
    zassert_equal(gpio_emul_input_set_dt(&test_line, 0), 0);
    zassert_equal(read_logical_input(&active), CRUX_OK);
    zassert_true(active);
}

static K_SEM_DEFINE(isr_done, 0, 1);
static volatile bool isr_ran_in_interrupt;
static volatile crux_status_t isr_configure_status;
static volatile crux_status_t isr_read_status;
static volatile bool isr_read_output;

static void isr_expiry(struct k_timer *timer)
{
    bool active = true;

    ARG_UNUSED(timer);
    isr_ran_in_interrupt = k_is_in_isr();
    isr_configure_status = crux_gpio_configure_input(CRUX_GPIO_LINE_0);
    isr_read_status = crux_gpio_read(CRUX_GPIO_LINE_0, &active);
    isr_read_output = active;
    k_sem_give(&isr_done);
}

static K_TIMER_DEFINE(isr_timer, isr_expiry, NULL);

ZTEST(gpio, test_calls_from_interrupt_context_are_rejected)
{
    k_timer_start(&isr_timer, K_MSEC(1), K_NO_WAIT);
    zassert_equal(k_sem_take(&isr_done, K_MSEC(500)), 0);
    zassert_true(isr_ran_in_interrupt, "the timer callback must run in interrupt context");
    zassert_equal(isr_configure_status, CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(isr_read_status, CRUX_ERR_NOT_SUPPORTED);
    zassert_true(isr_read_output, "the output must be unchanged on error");
}

ZTEST_SUITE(gpio, NULL, NULL, NULL, NULL, NULL);

/*
 * The provider keeps its configured state for the life of the image and Ztest runs suites in
 * name order (prj.conf disables shuffling), so this suite is named to run before "gpio"
 * configures the line. Renaming either suite, or enabling shuffling, breaks this test.
 */
ZTEST(crux_gpio_unconfigured, test_read_before_configure_is_not_ready)
{
    bool active = true;

    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, &active), CRUX_ERR_NOT_READY);
    zassert_true(active, "the output must be unchanged on error");
}

ZTEST_SUITE(crux_gpio_unconfigured, NULL, NULL, NULL, NULL, NULL);
