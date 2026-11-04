#include "security_event_internal.h"

#include <string.h>

/* Text past this is never recorded, so an emit's work stays bounded on a very long string. */
#define SECURITY_EVENT_SUBJECT_SCAN_MAX 256u
#define SECURITY_EVENT_CHANNEL_MAX 3u

#define SECURITY_EVENT_ROW(row_id, row_name, row_class)                                            \
    _Static_assert(sizeof(row_name) - 1u <= CRUX_SECURITY_EVENT_NAME_MAX,                          \
                   "a name must keep the worst-case line within the Logging limit");
#include "security_event_ids.inc"
#undef SECURITY_EVENT_ROW

typedef struct {
    const char *name;
    crux_security_event_class_t event_class;
    crux_security_event_id_t id;
} security_event_id_row_t;

static const security_event_id_row_t id_table[] = {
#define SECURITY_EVENT_ROW(row_id, row_name, row_class)                                            \
    {.name = (row_name), .event_class = (row_class), .id = (row_id)},
#include "security_event_ids.inc"
#undef SECURITY_EVENT_ROW
};

static const crux_security_event_project_id_t *project_table;
static size_t project_count;

static const security_event_id_row_t *crux_id_find(crux_security_event_id_t id)
{
    for (size_t index = 0; index < sizeof(id_table) / sizeof(id_table[0]); ++index) {
        if (id_table[index].id == id) {
            return &id_table[index];
        }
    }
    return NULL;
}

static const crux_security_event_project_id_t *project_id_find(crux_security_event_id_t id)
{
    for (size_t index = 0; index < project_count; ++index) {
        if (project_table[index].id == id) {
            return &project_table[index];
        }
    }
    return NULL;
}

const char *crux_security_event_id_name(crux_security_event_id_t id, bool *known)
{
    const security_event_id_row_t *row = crux_id_find(id);
    const crux_security_event_project_id_t *project = row == NULL ? project_id_find(id) : NULL;
    *known = row != NULL || project != NULL;
    if (row != NULL) {
        return row->name;
    }
    return project != NULL ? project->name : "event.unknown";
}

const char *crux_security_event_name(crux_security_event_id_t id)
{
    bool known = false;
    return crux_security_event_id_name(id, &known);
}

crux_security_event_class_t crux_security_event_id_class(crux_security_event_id_t id)
{
    const security_event_id_row_t *row = crux_id_find(id);
    if (row != NULL) {
        return row->event_class;
    }
    const crux_security_event_project_id_t *project = project_id_find(id);
    return project != NULL ? project->event_class : CRUX_SECURITY_EVENT_ROUTINE;
}

static bool is_lower_name_character(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

/* The length of a name of 1 to limit characters of [a-z0-9_.-], or zero for anything else. Reads no
 * further than limit + 1. */
static size_t lower_name_length(const char *name, size_t limit)
{
    size_t length = 0;
    for (; length <= limit && name[length] != '\0'; ++length) {
        if (!is_lower_name_character(name[length])) {
            return 0;
        }
    }
    return length <= limit ? length : 0;
}

/* A project name equal to a Crux name would forge that event in a log line. */
static bool name_is_crux(const char *name)
{
    for (size_t index = 0; index < sizeof(id_table) / sizeof(id_table[0]); ++index) {
        if (strcmp(id_table[index].name, name) == 0) {
            return true;
        }
    }
    return false;
}

bool crux_security_event_project_table_valid(const crux_security_event_project_id_t *project_ids,
                                             size_t count)
{
    if (count == 0) {
        return true;
    }
    if (project_ids == NULL) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        const crux_security_event_project_id_t *row = &project_ids[index];
        bool class_valid = row->event_class == CRUX_SECURITY_EVENT_ROUTINE ||
                           row->event_class == CRUX_SECURITY_EVENT_CHANGE;
        if (row->id < CRUX_SECURITY_EVENT_ID_PROJECT_FIRST || row->name == NULL ||
            lower_name_length(row->name, CRUX_SECURITY_EVENT_NAME_MAX) == 0 || !class_valid ||
            name_is_crux(row->name)) {
            return false;
        }
        for (size_t other = 0; other < index; ++other) {
            if (project_ids[other].id == row->id ||
                strcmp(project_ids[other].name, row->name) == 0) {
                return false;
            }
        }
    }
    return true;
}

void crux_security_event_project_table_set(const crux_security_event_project_id_t *project_ids,
                                           size_t count)
{
    project_table = count == 0 ? NULL : project_ids;
    project_count = count;
}

static bool is_subject_character(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.' || c == '/' || c == ':' || c == '-';
}

/* Returns whether the subject passed its rule. A long subject passes, with a mark. */
static bool subject_copy(const char *subject, security_event_stored_t *record)
{
    size_t length = 0;
    for (; length < SECURITY_EVENT_SUBJECT_SCAN_MAX && subject[length] != '\0'; ++length) {
        if (!is_subject_character(subject[length])) {
            return false;
        }
    }
    const size_t capacity = sizeof(record->subject) - 1u;
    if (length > capacity) {
        memcpy(record->subject, subject, capacity - 1u);
        record->subject[capacity - 1u] = '~';
        record->subject[capacity] = '\0';
        record->subject_truncated = true;
    } else {
        memcpy(record->subject, subject, length);
        record->subject[length] = '\0';
    }
    return true;
}

unsigned crux_security_event_fields_copy(const crux_security_event_t *event,
                                         security_event_stored_t *record)
{
    unsigned invalid = 0;

    bool known = false;
    (void)crux_security_event_id_name(event->id, &known);
    if (known && event->id != CRUX_SECURITY_EVENT_ID_EVENT_LOST) {
        record->id = event->id;
    } else {
        record->id = CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN;
        invalid += 1;
    }

    if ((unsigned)event->result <= (unsigned)CRUX_SECURITY_EVENT_FAILED) {
        record->result = (uint8_t)event->result;
    } else {
        record->result = (uint8_t)CRUX_SECURITY_EVENT_FAILED;
        invalid += 1;
    }

    if (event->channel <= SECURITY_EVENT_CHANNEL_MAX) {
        record->channel = event->channel;
    } else {
        record->channel = 0;
        invalid += 1;
    }

    if (event->source_length == 0 || event->source_length == 4 || event->source_length == 8) {
        record->source_length = event->source_length;
        memcpy(record->source, event->source, event->source_length);
    } else {
        record->source_length = 0;
        invalid += 1;
    }

    if (event->user != NULL) {
        size_t length = lower_name_length(event->user, sizeof(record->user) - 1u);
        if (length != 0) {
            memcpy(record->user, event->user, length);
            record->user[length] = '\0';
        } else {
            invalid += 1;
        }
    }

    if (event->subject != NULL && !subject_copy(event->subject, record)) {
        invalid += 1;
    }
    return invalid;
}

void crux_security_event_record_fill(const security_event_stored_t *stored,
                                     crux_security_event_record_t *record)
{
    record->id = stored->id;
    record->result = (crux_security_event_result_t)stored->result;
    record->channel = stored->channel;
    record->source_length = stored->source_length;
    memcpy(record->source, stored->source, sizeof(record->source));
    memcpy(record->user, stored->user, sizeof(record->user));
    memcpy(record->subject, stored->subject, sizeof(record->subject));
    record->sequence = stored->sequence;
    record->uptime_ms = stored->uptime_ms;
    record->no_uptime = stored->no_uptime;
    record->subject_truncated = stored->subject_truncated;
}
