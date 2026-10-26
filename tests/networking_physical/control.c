#include "control.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
static int number(const char **cursor, unsigned long *value)
{
    const char *start = *cursor;
    if (*start < '0' || *start > '9')
        return -1;
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(start, &end, 10);
    if (errno || parsed > UINT32_MAX || (*end != ' ' && *end != '\0'))
        return -1;
    *cursor = end;
    *value = parsed;
    return 0;
}
int fixture_control(const char *line, struct fixture_session *session, struct fixture_command *out)
{
    if (line == NULL || session == NULL || out == NULL || strlen(line) < 3 || line[1] != ' ')
        return -1;
    char action = line[0];
    const char *cursor = line + 2;
    unsigned long token, sequence = 0, count = 0;
    if (number(&cursor, &token) || token == 0)
        return -1;
    if (action == 'A') {
        if (*cursor || session->token)
            return -1;
        *session = (struct fixture_session){.token = (uint32_t)token, .next_sequence = 1};
    } else {
        if (!session->token || token != session->token || *cursor++ != ' ' ||
            number(&cursor, &sequence) || sequence != session->next_sequence || sequence > 50)
            return -1;
        if (action == 'P') {
            if (*cursor++ != ' ' || number(&cursor, &count) || count == 0 || count > 10)
                return -1;
        } else if (action == 'L') {
            count = 1;
        } else if (action != 'I') {
            return -1;
        }
        unsigned long ticket;
        if (*cursor++ != ' ' || number(&cursor, &ticket) || !ticket || ticket != session->ticket ||
            *cursor || session->pairs + count > 50)
            return -1;
        session->ticket = 0;
        session->pairs += (unsigned int)count;
        ++session->next_sequence;
    }
    *out = (struct fixture_command){
        .action = action, .sequence = (unsigned int)sequence, .count = (unsigned int)count};
    return 0;
}
