#ifndef FIXTURE_CONTROL_H
#define FIXTURE_CONTROL_H
#include <stdint.h>
struct fixture_session {
    uint32_t token;
    uint32_t ticket;
    unsigned int next_sequence;
    unsigned int pairs;
};
struct fixture_command {
    char action;
    unsigned int sequence;
    unsigned int count;
};
int fixture_control(const char *line, struct fixture_session *session, struct fixture_command *out);
#endif
