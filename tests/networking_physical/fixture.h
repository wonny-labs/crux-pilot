#ifndef M5_FIXTURE_H
#define M5_FIXTURE_H
#include "control.h"
/* Private native composition only; deliberately outside public include roots. */
void m5_fixture_record(const char *format, ...) __attribute__((format(printf, 1, 2)));
int m5_fixture_init(void);
int m5_fixture_execute(const struct fixture_session *session,
                       const struct fixture_command *command);
#endif
