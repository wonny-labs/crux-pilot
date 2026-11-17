/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * A tiny check harness for the table-driven cases. It counts every check, names the current case in
 * a failure, and never aborts, so that one run shows every failing row.
 */
#ifndef CRUX_IMAGE_STATE_PROTO_HARNESS_H
#define CRUX_IMAGE_STATE_PROTO_HARNESS_H

#include <stdbool.h>

extern unsigned harness_checks;
extern unsigned harness_failures;
extern const char *harness_case;

void harness_check(bool ok, const char *expression, const char *file, int line);
void harness_check_int(long expected, long actual, const char *expression, const char *file,
                       int line);

/* Names the case that the following checks belong to, for the failure message. */
#define CASE(name) (harness_case = (name))
#define CHECK(condition) harness_check((condition), #condition, __FILE__, __LINE__)
#define CHECK_INT(expected, actual)                                                                \
    harness_check_int((long)(expected), (long)(actual), #actual, __FILE__, __LINE__)

void run_pure_tests(void);
void run_provider_tests(void);
void run_scenario_tests(void);
void run_codec_tests(void);

#endif /* CRUX_IMAGE_STATE_PROTO_HARNESS_H */
