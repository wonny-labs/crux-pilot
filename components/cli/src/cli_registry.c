#include "cli_internal.h"

#include <string.h>

static struct {
    const crux_cli_command_t *const *tables;
    const size_t *counts;
    size_t table_count;
    const char *password_rule;
    bool ready;
} registry;

#ifdef CRUX_CLI_TESTING
void crux_cli_test_reset(void);
void crux_cli_test_reset(void)
{
    memset(&registry, 0, sizeof(registry));
}
#endif

static bool is_word_char(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-';
}

/* The number of words in a well-formed name, or zero. */
static size_t name_words(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    size_t words = 0;
    size_t index = 0;
    for (;;) {
        size_t start = index;
        while (is_word_char(name[index])) {
            index += 1;
        }
        if (index == start) {
            return 0;
        }
        words += 1;
        if (name[index] == '\0') {
            return words;
        }
        if (name[index] != ' ' || words == 2) {
            return 0;
        }
        index += 1;
    }
}

static bool is_printable_line(const char *text)
{
    if (text == NULL) {
        return false;
    }
    for (const char *byte = text; *byte != '\0'; ++byte) {
        if ((unsigned char)*byte < 0x20u || (unsigned char)*byte > 0x7Eu) {
            return false;
        }
    }
    return true;
}

static bool access_class_is_valid(const crux_cli_command_t *command)
{
    switch (command->access_class) {
    case CRUX_CLI_ACCESS_SESSION:
        return command->permission == NULL;
    case CRUX_CLI_ACCESS_PERMISSION:
        return command->permission != NULL && command->permission[0] != '\0' &&
               strlen(command->permission) <= CRUX_ACCESS_PERMISSION_MAX;
    case CRUX_CLI_ACCESS_INVALID:
        break;
    }
    return false;
}

static bool flags_are_valid(uint8_t flags)
{
    const unsigned channels = CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH;
    const unsigned known = channels | CRUX_CLI_FLAG_INTERACTIVE | CRUX_CLI_FLAG_FORCED_CHANGE_OK;
    return (flags & ~known) == 0 && (flags & channels) != 0;
}

static bool command_is_valid(const crux_cli_command_t *command)
{
    size_t words = name_words(command->name);
    return words != 0 && is_printable_line(command->summary) && access_class_is_valid(command) &&
           command->min_args <= command->max_args &&
           words + command->max_args <= CRUX_CLI_TOKENS_MAX && flags_are_valid(command->flags) &&
           command->handler != NULL;
}

/* The same name, or a one-word name that is also the first word of a two-word name. */
static bool names_clash(const char *first, const char *second)
{
    size_t first_length = strlen(first);
    size_t second_length = strlen(second);
    if (first_length > second_length) {
        return names_clash(second, first);
    }
    if (memcmp(first, second, first_length) != 0) {
        return false;
    }
    return first_length == second_length || second[first_length] == ' ';
}

const crux_cli_command_t *crux_cli_registry_at(size_t index)
{
    if (index < crux_cli_builtin_command_count) {
        return &crux_cli_builtin_commands[index];
    }
    index -= crux_cli_builtin_command_count;
    for (size_t table = 0; table < registry.table_count; ++table) {
        if (index < registry.counts[table]) {
            return &registry.tables[table][index];
        }
        index -= registry.counts[table];
    }
    return NULL;
}

static bool commands_are_valid(void)
{
    const crux_cli_command_t *command;
    for (size_t index = 0; (command = crux_cli_registry_at(index)) != NULL; ++index) {
        if (!command_is_valid(command)) {
            return false;
        }
        const crux_cli_command_t *other;
        for (size_t earlier = 0; earlier < index; ++earlier) {
            other = crux_cli_registry_at(earlier);
            if (names_clash(other->name, command->name)) {
                return false;
            }
        }
    }
    return true;
}

crux_cli_result_t crux_cli_init(const crux_cli_command_t *const *tables, const size_t *counts,
                                size_t table_count)
{
    if (registry.ready || (table_count > 0 && (tables == NULL || counts == NULL))) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    for (size_t table = 0; table < table_count; ++table) {
        if (counts[table] > 0 && tables[table] == NULL) {
            return CRUX_CLI_ERR_INVALID_ARGUMENT;
        }
    }
    registry.tables = tables;
    registry.counts = counts;
    registry.table_count = table_count;
    if (!commands_are_valid()) {
        registry.tables = NULL;
        registry.counts = NULL;
        registry.table_count = 0;
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    registry.ready = true;
    return CRUX_CLI_OK;
}

crux_cli_result_t crux_cli_set_password_rule(const char *text)
{
    if (text != NULL && !is_printable_line(text)) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    registry.password_rule = text;
    return CRUX_CLI_OK;
}

const char *crux_cli_registry_password_rule(void)
{
    return registry.password_rule;
}

bool crux_cli_registry_ready(void)
{
    return registry.ready;
}

/* Tokens may hold anything, so a name is compared word by word. */
static bool name_matches(const char *name, const char *const *tokens, size_t count, size_t *words)
{
    const char *space = strchr(name, ' ');
    if (space == NULL) {
        *words = 1;
        return strcmp(name, tokens[0]) == 0;
    }
    *words = 2;
    size_t first_length = (size_t)(space - name);
    return count >= 2 && strlen(tokens[0]) == first_length &&
           memcmp(name, tokens[0], first_length) == 0 && strcmp(space + 1, tokens[1]) == 0;
}

const crux_cli_command_t *crux_cli_registry_find(const char *const *tokens, size_t count,
                                                 size_t *words)
{
    const crux_cli_command_t *command;
    for (size_t index = 0; (command = crux_cli_registry_at(index)) != NULL; ++index) {
        /* Init refused overlaps, so at most one name can match. */
        if (name_matches(command->name, tokens, count, words)) {
            return command;
        }
    }
    return NULL;
}
