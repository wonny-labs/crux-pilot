#include <crux/time.h>

#include "crux_time_command.h"
#include "crux_time_host_test.h"
#include "crux_time_test.h"

#include <gtest/gtest.h>

#include <cstring>

class TimeCommandTest : public ::testing::Test {
  protected:
    void SetUp() override {
        crux_time_test_reset();
        crux_time_host_test_reset();
        ASSERT_EQ(crux_time_init(), CRUX_OK);
    }
};

TEST_F(TimeCommandTest, HelpIdentifiesUtcInput) {
    EXPECT_NE(std::strstr(crux_time_command_help_text(), "Input is UTC"), nullptr);
}

TEST_F(TimeCommandTest, NoDataApiStatusMapsToDistinctCliText) {
    crux_realtime_t instant = {0, 0};
    const crux_time_command_result_t result = crux_time_command_execute("time get");

    EXPECT_EQ(crux_time_get_realtime(&instant), CRUX_ERR_NO_DATA);
    EXPECT_EQ(result.status, CRUX_ERR_NO_DATA);
    EXPECT_STREQ(crux_time_command_cli_error_text(result.status), "ERR NOT_SET");
    EXPECT_STRNE(crux_time_command_cli_error_text(result.status), "ERR NO_DATA");
}

TEST_F(TimeCommandTest, SetAndGetUseTheSamePortableHandler) {
    auto set_result = crux_time_command_execute("time set 2026-10-16T14:30:00");
    auto get_result = crux_time_command_execute("time get");

    EXPECT_EQ(set_result.status, CRUX_OK);
    EXPECT_STREQ(set_result.text, "OK");
    EXPECT_EQ(get_result.status, CRUX_OK);
    EXPECT_STREQ(get_result.text, "2026-10-16T14:30:00 UTC");
}

TEST_F(TimeCommandTest, AllowsWhitespaceBetweenTokensAndCrLf) {
    auto set_result = crux_time_command_execute(" \ttime   set\t2026-10-16T14:30:00\r\n");
    auto get_result = crux_time_command_execute(" time\tget ");

    EXPECT_EQ(set_result.status, CRUX_OK);
    EXPECT_EQ(get_result.status, CRUX_OK);
    EXPECT_STREQ(get_result.text, "2026-10-16T14:30:00 UTC");
}

TEST_F(TimeCommandTest, HelpIsAnAdapterOutcome) {
    auto root_help = crux_time_command_execute("help");
    auto group_help = crux_time_command_execute("time help");

    EXPECT_EQ(root_help.status, CRUX_OK);
    EXPECT_EQ(root_help.kind, CRUX_TIME_COMMAND_HELP);
    EXPECT_EQ(group_help.status, CRUX_OK);
    EXPECT_EQ(group_help.kind, CRUX_TIME_COMMAND_HELP);
}

TEST_F(TimeCommandTest, RejectsMalformedSyntaxAndDates) {
    const char *invalid[] = {
        "",
        "time",
        "time set",
        "time get extra",
        "time set 2026-1-16T14:30:00",
        "time set 2026-10-16T14:30",
        "time set 2026-10-16T14:30:00Z",
        "time set 2026-10-16T14:30:00 extra",
        "time set 2026-10-16T14:30:00 extra extra",
        "time set 2026-02-29T14:30:00",
        "timez get",
        "time get\nextra",
        "help extra",
    };

    for (const char *line : invalid) {
        const auto result = crux_time_command_execute(line);
        EXPECT_EQ(result.status, CRUX_ERR_INVALID_ARGUMENT) << line;
    }
    EXPECT_EQ(crux_time_command_execute(nullptr).status, CRUX_ERR_INVALID_ARGUMENT);
}

TEST_F(TimeCommandTest, MapsOutOfRangeAndIoErrors) {
    auto range_result = crux_time_command_execute("time set 1999-12-31T23:59:59");
    EXPECT_EQ(range_result.status, CRUX_ERR_OUT_OF_RANGE);
    EXPECT_STREQ(crux_time_command_cli_error_text(range_result.status), "ERR OUT_OF_RANGE");

    crux_time_host_test_fail_next_write();
    auto io_result = crux_time_command_execute("time set 2026-10-16T14:30:00");
    EXPECT_EQ(io_result.status, CRUX_ERR_IO);
    EXPECT_STREQ(crux_time_command_cli_error_text(io_result.status), "ERR IO_ERROR");
}

TEST_F(TimeCommandTest, RejectedWriteNeverCallsBackend) {
    auto invalid_result = crux_time_command_execute("time set 2026-02-30T14:30:00");
    auto extra_result = crux_time_command_execute("time set 2026-10-16T14:30:00 Z");
    auto overflow_result = crux_time_command_execute("time set 2026-10-16T14:30:00 extra extra");

    EXPECT_EQ(invalid_result.status, CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(extra_result.status, CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(overflow_result.status, CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(crux_time_host_test_write_calls(), 0U);
}

TEST_F(TimeCommandTest, InvalidCommandDoesNotChangePreviouslySetTime) {
    ASSERT_EQ(crux_time_command_execute("time set 2026-10-16T14:30:00").status, CRUX_OK);
    EXPECT_EQ(crux_time_command_execute("time set 2026-02-30T14:30:00").status,
              CRUX_ERR_INVALID_ARGUMENT);
    const auto result = crux_time_command_execute("time get");
    EXPECT_EQ(result.status, CRUX_OK);
    EXPECT_STREQ(result.text, "2026-10-16T14:30:00 UTC");
    EXPECT_EQ(crux_time_host_test_write_calls(), 1U);
}
