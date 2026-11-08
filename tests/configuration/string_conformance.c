/* String parameters, decisions C1 and C2. Runs on Host and on native_sim, as the scalar cases do.
 */
#include "fault_provider.h"
#include "text_vectors.h"

#include <crux/configuration.h>

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

enum { SCHEMA = 0x0102, STORAGE = CRUX_CONFIG_STRING_MAX + 1 };

static crux_config_value_t sized(const char *text, size_t length)
{
    crux_config_value_t value = {.type = CRUX_CONFIG_STRING};
    value.data.string.text = text;
    value.data.string.length = length;
    return value;
}

static crux_config_value_t text(const char *value)
{
    return sized(value, strlen(value));
}

static bool current_is(const crux_config_param_t *parameter, const char *expected)
{
    crux_config_value_t out = {.type = CRUX_CONFIG_BOOL};
    return crux_config_read(parameter, &out) == CRUX_CONFIG_OK && out.type == CRUX_CONFIG_STRING &&
           out.data.string.length == strlen(expected) && out.data.string.text != NULL &&
           strcmp(out.data.string.text, expected) == 0;
}

static crux_config_spec_t spec_of(const char *key, const char *default_text)
{
    crux_config_spec_t spec = {key, SCHEMA, text(default_text), NULL, NULL};
    return spec;
}

/* Rejects a value holding "bad". Counts its calls, and remembers what it was shown. */
typedef struct {
    unsigned calls;
    bool reject_all;
    char seen[STORAGE];
    size_t seen_length;
    char terminator;
} validator_state_t;

static bool validate_text(const crux_config_value_t *candidate, void *context)
{
    validator_state_t *state = context;
    ++state->calls;
    if (candidate->type != CRUX_CONFIG_STRING) {
        return false;
    }
    state->seen_length = candidate->data.string.length;
    state->terminator = candidate->data.string.text[candidate->data.string.length];
    memcpy(state->seen, candidate->data.string.text, candidate->data.string.length);
    return !state->reject_all && strstr(candidate->data.string.text, "bad") == NULL;
}

static void init_cases(void)
{
    config_fault_reset();
    crux_config_spec_t spec = spec_of("host_name", "crux");
    crux_config_spec_t scalar = {
        "scalar", SCHEMA, {.type = CRUX_CONFIG_U32, .data.u32 = 1}, NULL, NULL};
    char storage[STORAGE];
    crux_config_param_t parameter = {0};

    /* Each function refuses the other's spec, and arguments that cannot be right. */
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(parameter.spec == NULL);
    CHECK(crux_config_init_string(&parameter, &scalar, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(parameter.spec == NULL);
    CHECK(crux_config_init_string(NULL, &spec, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_init_string(&parameter, NULL, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_init_string(&parameter, &spec, NULL, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);

    /* Capacity exceeds the default's length, and is at most the maximum and its terminator. */
    CHECK(crux_config_init_string(&parameter, &spec, storage, 0) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_init_string(&parameter, &spec, storage, 4) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    char oversized[STORAGE + 1];
    CHECK(crux_config_init_string(&parameter, &spec, oversized, sizeof(oversized)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(parameter.spec == NULL);
    crux_config_value_t unread = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_read(&parameter, &unread) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(crux_config_key(&parameter) == NULL);
    CHECK(crux_config_key(NULL) == NULL);

    CHECK(crux_config_init_string(&parameter, &spec, storage, 5) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, "crux"));
    CHECK(strcmp(crux_config_key(&parameter), "host_name") == 0);
    CHECK(crux_config_init_string(&parameter, &spec, storage, 5) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(current_is(&parameter, "crux"));

    crux_config_param_t widest = {0};
    CHECK(crux_config_init_string(&widest, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);

    /* The key rule is unchanged. */
    crux_config_spec_t bad_key = spec_of("bad key", "crux");
    crux_config_param_t fresh = {0};
    CHECK(crux_config_init_string(&fresh, &bad_key, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    bad_key.key = NULL;
    CHECK(crux_config_init_string(&fresh, &bad_key, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);

    /* A scalar parameter has a key too. */
    crux_config_param_t counter = {0};
    CHECK(crux_config_init(&counter, &scalar) == CRUX_CONFIG_OK);
    CHECK(strcmp(crux_config_key(&counter), "scalar") == 0);
}

static void default_cases(void)
{
    config_fault_reset();
    char storage[STORAGE];
    crux_config_param_t fresh = {0};

    /* A default fails the same rule as any value, and a failed init changes nothing. */
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_invalid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_invalid_vectors[i];
        char copy[32];
        memcpy(copy, vector->bytes, vector->length);
        copy[vector->length] = '\0';
        crux_config_spec_t spec = {"bad_default", SCHEMA, sized(copy, vector->length), NULL, NULL};
        crux_config_result_t result =
            crux_config_init_string(&fresh, &spec, storage, sizeof(storage));
        if (result != CRUX_CONFIG_ERR_REJECTED) {
            printf("default %s\n", vector->what);
        }
        CHECK(result == CRUX_CONFIG_ERR_REJECTED);
        CHECK(fresh.spec == NULL);
    }

    /* A default with no text, or a length that its terminator contradicts. */
    crux_config_spec_t spec = {"bad_default", SCHEMA, sized(NULL, 0), NULL, NULL};
    CHECK(crux_config_init_string(&fresh, &spec, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    spec.default_value = sized("abc", 2);
    CHECK(crux_config_init_string(&fresh, &spec, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_REJECTED);
    spec.default_value = sized("abc", 8);
    CHECK(crux_config_init_string(&fresh, &spec, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_REJECTED);
    CHECK(fresh.spec == NULL);

    /* A default that aliases the storage would change with the value. */
    memcpy(storage, "abc", 4);
    spec.default_value = sized(storage, 3);
    CHECK(crux_config_init_string(&fresh, &spec, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    spec.default_value = sized(storage + 1, 2);
    CHECK(crux_config_init_string(&fresh, &spec, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    /* It overlaps only at the end, where a terminator would land on the storage's first byte. */
    char shared[16] = "abcdefg";
    spec.default_value = sized(shared, 7);
    CHECK(crux_config_init_string(&fresh, &spec, shared + 7, 8) ==
          CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    /* Next to the storage, with no overlap, it is fine. */
    spec.default_value = sized(shared, 7);
    CHECK(crux_config_init_string(&fresh, &spec, shared + 8, 8) == CRUX_CONFIG_OK);
    fresh = (crux_config_param_t){0};

    /* The application's validator rules on a default too. */
    validator_state_t state = {0};
    crux_config_spec_t validated = {"validated", SCHEMA, text("bad"), validate_text, &state};
    CHECK(crux_config_init_string(&fresh, &validated, storage, sizeof(storage)) ==
          CRUX_CONFIG_ERR_REJECTED);
    CHECK(fresh.spec == NULL);
    CHECK(state.calls == 1);
    validated.default_value = text("good");
    CHECK(crux_config_init_string(&fresh, &validated, storage, sizeof(storage)) == CRUX_CONFIG_OK);
    CHECK(current_is(&fresh, "good"));
}

static void read_and_update_cases(void)
{
    config_fault_reset();
    crux_config_spec_t spec = spec_of("name", "first");
    char storage[8];
    crux_config_param_t parameter = {0};
    CHECK(crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);

    /* A read borrows the parameter's storage, and never the default's. */
    crux_config_value_t out = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_read(&parameter, &out) == CRUX_CONFIG_OK);
    CHECK(out.data.string.text == storage);
    CHECK(out.data.string.length == 5);
    CHECK(strcmp(storage, "first") == 0);

    crux_config_value_t candidate = text("second");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, "second"));
    CHECK(strcmp(spec.default_value.data.string.text, "first") == 0);

    /* Exactly what fits, then one more. */
    candidate = text("1234567");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, "1234567"));
    candidate = text("12345678");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "1234567"));

    /* The empty string is a value. */
    candidate = text("");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, ""));

    /* A rejected update of each kind leaves the old value. */
    candidate = text("kept");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_invalid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_invalid_vectors[i];
        if (vector->length + 1 > sizeof(storage)) {
            continue;
        }
        char copy[8];
        memcpy(copy, vector->bytes, vector->length);
        copy[vector->length] = '\0';
        candidate = sized(copy, vector->length);
        crux_config_result_t result = crux_config_update(&parameter, &candidate);
        if (result != CRUX_CONFIG_ERR_REJECTED) {
            printf("update %s\n", vector->what);
        }
        CHECK(result == CRUX_CONFIG_ERR_REJECTED);
    }
    CHECK(current_is(&parameter, "kept"));
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_valid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_valid_vectors[i];
        if (vector->length + 1 > sizeof(storage)) {
            continue;
        }
        char copy[8];
        memcpy(copy, vector->bytes, vector->length);
        copy[vector->length] = '\0';
        candidate = sized(copy, vector->length);
        crux_config_result_t result = crux_config_update(&parameter, &candidate);
        if (result != CRUX_CONFIG_OK) {
            printf("update %s\n", vector->what);
        }
        CHECK(result == CRUX_CONFIG_OK);
    }

    candidate = text("kept");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    candidate = sized(NULL, 0);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    /* The length must land on the terminator, whichever way it is wrong. */
    candidate = sized("abc", 2);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    candidate = sized("abc", 4);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    /* An enormous length is refused from the length alone. */
    candidate = sized("abc", SIZE_MAX);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "kept"));

    /* Types do not convert, in either direction. */
    crux_config_value_t flag = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    CHECK(crux_config_update(&parameter, &flag) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "kept"));
    crux_config_spec_t scalar = {
        "scalar", SCHEMA, {.type = CRUX_CONFIG_U32, .data.u32 = 1}, NULL, NULL};
    crux_config_param_t counter = {0};
    CHECK(crux_config_init(&counter, &scalar) == CRUX_CONFIG_OK);
    candidate = text("2");
    CHECK(crux_config_update(&counter, &candidate) == CRUX_CONFIG_ERR_REJECTED);

    /* A candidate may alias the storage: the whole of it, or a tail that moves down. */
    candidate = text("abcdef");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_read(&parameter, &out) == CRUX_CONFIG_OK);
    candidate = out;
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, "abcdef"));
    candidate = sized(storage + 2, 4);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, "cdef"));
}

static void validator_cases(void)
{
    config_fault_reset();
    validator_state_t state = {0};
    crux_config_spec_t spec = {"validated", SCHEMA, text("good"), validate_text, &state};
    char storage[STORAGE];
    crux_config_param_t parameter = {0};
    CHECK(crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);

    /* It sees a terminated candidate of the exact bytes, after the character rule. */
    state.calls = 0;
    memset(state.seen, 0, sizeof(state.seen));
    crux_config_value_t candidate = text("sized");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(state.calls == 1);
    CHECK(state.seen_length == 5);
    CHECK(state.terminator == '\0');
    CHECK(memcmp(state.seen, "sized", 5) == 0);

    candidate = text("is bad");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "sized"));

    /* Text the character rule refuses never reaches it, and neither does an embedded NUL. */
    state.calls = 0;
    candidate = text("a\nb");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    candidate = sized("a\0b", 3);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(state.calls == 0);

    /* Save revalidates, and writes nothing if the value no longer passes. */
    state.reject_all = true;
    size_t writes = config_fault_writes();
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(config_fault_writes() == writes);
    state.reject_all = false;
}

static void encoding_cases(void)
{
    config_fault_reset();
    crux_config_spec_t spec = spec_of("wire", "hi");
    char storage[STORAGE];
    crux_config_param_t parameter = {0};
    CHECK(crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_OK);
    uint8_t bytes[STORAGE + 8] = {0};
    const uint8_t two[] = {1, 3, 0x02, 0x01, 'h', 'i'};
    CHECK(config_fault_bytes("wire", bytes, sizeof(bytes)) == sizeof(two));
    CHECK(memcmp(bytes, two, sizeof(two)) == 0);

    crux_config_value_t candidate = text("");
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_OK);
    const uint8_t empty[] = {1, 3, 0x02, 0x01};
    CHECK(config_fault_bytes("wire", bytes, sizeof(bytes)) == sizeof(empty));
    CHECK(memcmp(bytes, empty, sizeof(empty)) == 0);

    /* Scalar records stay byte-identical. */
    crux_config_spec_t number = {
        "number", SCHEMA, {.type = CRUX_CONFIG_U32, .data.u32 = 0x11223344u}, NULL, NULL};
    crux_config_spec_t flag = {
        "flag", SCHEMA, {.type = CRUX_CONFIG_BOOL, .data.boolean = true}, NULL, NULL};
    crux_config_param_t number_parameter = {0}, flag_parameter = {0};
    CHECK(crux_config_init(&number_parameter, &number) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&flag_parameter, &flag) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&number_parameter) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&flag_parameter) == CRUX_CONFIG_OK);
    const uint8_t number_wire[] = {1, 2, 0x02, 0x01, 0x44, 0x33, 0x22, 0x11};
    const uint8_t flag_wire[] = {1, 1, 0x02, 0x01, 1};
    CHECK(config_fault_bytes("number", bytes, sizeof(bytes)) == sizeof(number_wire));
    CHECK(memcmp(bytes, number_wire, sizeof(number_wire)) == 0);
    CHECK(config_fault_bytes("flag", bytes, sizeof(bytes)) == sizeof(flag_wire));
    CHECK(memcmp(bytes, flag_wire, sizeof(flag_wire)) == 0);
}

static void maximum_length_cases(void)
{
    config_fault_reset();
    crux_config_spec_t spec = spec_of("largest", "");
    char storage[STORAGE];
    crux_config_param_t parameter = {0};
    CHECK(crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);

    char longest[CRUX_CONFIG_STRING_MAX + 2];
    memset(longest, 'x', sizeof(longest));
    longest[CRUX_CONFIG_STRING_MAX] = '\0';
    crux_config_value_t candidate = sized(longest, CRUX_CONFIG_STRING_MAX);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_OK);

    /* A value of the maximum length survives a reload into a new object. */
    char restarted_storage[STORAGE];
    crux_config_param_t restarted = {0};
    CHECK(crux_config_init_string(&restarted, &spec, restarted_storage,
                                  sizeof(restarted_storage)) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&restarted) == CRUX_CONFIG_OK);
    CHECK(current_is(&restarted, longest));

    /* One more is refused at the update, whichever object is asked. */
    longest[CRUX_CONFIG_STRING_MAX] = 'x';
    longest[CRUX_CONFIG_STRING_MAX + 1] = '\0';
    candidate = sized(longest, CRUX_CONFIG_STRING_MAX + 1);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    longest[CRUX_CONFIG_STRING_MAX] = '\0';
    CHECK(current_is(&parameter, longest));

    /* A smaller object refuses from the record's length, and falls back to its default. */
    crux_config_spec_t small_spec = spec_of("largest", "small");
    char small_storage[16];
    crux_config_param_t small = {0};
    CHECK(crux_config_init_string(&small, &small_spec, small_storage, sizeof(small_storage)) ==
          CRUX_CONFIG_OK);
    CHECK(crux_config_load(&small) == CRUX_CONFIG_DEFAULT_REJECTED);
    CHECK(current_is(&small, "small"));
}

/* Stores a record whose bytes are header + text, with the length it claims. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test helper
static void store_text(const char *key, const char *value, size_t length)
{
    uint8_t record[4 + CRUX_CONFIG_STRING_MAX + 4] = {1, 3, 0x02, 0x01};
    memcpy(record + 4, value, length);
    config_fault_record(key, record, 4 + length);
}

static void load_cases(void)
{
    config_fault_reset();
    validator_state_t state = {0};
    crux_config_spec_t spec = {"stored", SCHEMA, text("default"), validate_text, &state};
    char storage[16];
    crux_config_param_t parameter = {0};
    CHECK(crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);

    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_MISSING);
    CHECK(current_is(&parameter, "default"));
    crux_config_value_t out = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_read(&parameter, &out) == CRUX_CONFIG_OK && out.data.string.text == storage);

    store_text("stored", "loaded", 6);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, "loaded"));
    store_text("stored", "", 0);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, ""));

    /* A record under the header. */
    const uint8_t record[] = {1, 3, 0x02, 0x01, 'a'};
    for (size_t length = 0; length < 4; ++length) {
        crux_config_value_t active = text("active");
        CHECK(crux_config_update(&parameter, &active) == CRUX_CONFIG_OK);
        config_fault_record("stored", record, length);
        CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
        CHECK(current_is(&parameter, "default"));
    }

    /* A record of exactly what fits, then one longer, which is refused from its length. */
    char fits[17];
    memset(fits, 'y', sizeof(fits));
    fits[15] = '\0';
    store_text("stored", fits, 15);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_OK);
    CHECK(current_is(&parameter, fits));
    fits[15] = 'y';
    fits[16] = '\0';
    store_text("stored", fits, 16);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
    CHECK(current_is(&parameter, "default"));

    /* A short or failed transfer of a valid length is an error that keeps the value. */
    store_text("stored", "loaded", 6);
    crux_config_value_t active = text("active");
    CHECK(crux_config_update(&parameter, &active) == CRUX_CONFIG_OK);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_OK, 5});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_IO);
    CHECK(current_is(&parameter, "active"));
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_ERR_IO, SIZE_MAX});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_IO);
    CHECK(current_is(&parameter, "active"));
    /* A short transfer of a length that is itself invalid is still a rejection, from metadata. */
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_OK, 2});
    store_text("stored", fits, 16);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
    CHECK(current_is(&parameter, "default"));
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_OK, SIZE_MAX});

    /* Malformed headers, and a type mismatch in each direction. */
    for (unsigned field = 0; field < 4; ++field) {
        uint8_t bad[] = {1, 3, 0x02, 0x01, 'o', 'k'};
        bad[field] = 99;
        config_fault_record("stored", bad, sizeof(bad));
        CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
        CHECK(current_is(&parameter, "default"));
    }
    const uint8_t u32_record[] = {1, 2, 0x02, 0x01, 1, 0, 0, 0};
    config_fault_record("stored", u32_record, sizeof(u32_record));
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
    const uint8_t bool_record[] = {1, 1, 0x02, 0x01, 1};
    config_fault_record("stored", bool_record, sizeof(bool_record));
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
    CHECK(current_is(&parameter, "default"));

    crux_config_spec_t number_spec = {
        "number", SCHEMA, {.type = CRUX_CONFIG_U32, .data.u32 = 7}, NULL, NULL};
    crux_config_spec_t flag_spec = {
        "flag", SCHEMA, {.type = CRUX_CONFIG_BOOL, .data.boolean = true}, NULL, NULL};
    crux_config_param_t number = {0}, flag = {0};
    CHECK(crux_config_init(&number, &number_spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&flag, &flag_spec) == CRUX_CONFIG_OK);
    /* A string record whose length is a valid scalar length, and one whose length is not. */
    store_text("number", "abcd", 4);
    CHECK(crux_config_load(&number) == CRUX_CONFIG_DEFAULT_REJECTED);
    store_text("flag", "a", 1);
    CHECK(crux_config_load(&flag) == CRUX_CONFIG_DEFAULT_REJECTED);
    store_text("number", "abcdefghij", 10);
    CHECK(crux_config_load(&number) == CRUX_CONFIG_DEFAULT_REJECTED);

    /* A stored record is held to the character rule, and so to the validator. */
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_invalid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_invalid_vectors[i];
        active = text("active");
        CHECK(crux_config_update(&parameter, &active) == CRUX_CONFIG_OK);
        store_text("stored", vector->bytes, vector->length);
        crux_config_result_t result = crux_config_load(&parameter);
        if (result != CRUX_CONFIG_DEFAULT_REJECTED) {
            printf("record %s\n", vector->what);
        }
        CHECK(result == CRUX_CONFIG_DEFAULT_REJECTED);
        CHECK(current_is(&parameter, "default"));
    }
    store_text("stored", "bad", 3);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
    CHECK(current_is(&parameter, "default"));
}

static void fallback_cases(void)
{
    config_fault_reset();
    validator_state_t state = {0};
    char mutable_default[8] = "default";
    crux_config_spec_t spec = {"stored", SCHEMA, sized(mutable_default, 7), validate_text, &state};
    char storage[16];
    crux_config_param_t parameter = {0};
    CHECK(crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);
    crux_config_value_t active = text("active");
    CHECK(crux_config_update(&parameter, &active) == CRUX_CONFIG_OK);

    /* The default no longer passes the validator: the load refuses, and the value stays. */
    state.reject_all = true;
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "active"));
    state.reject_all = false;

    /* The default's own bytes no longer pass the character rule, or match its length. */
    mutable_default[2] = '\n';
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "active"));
    mutable_default[2] = '\0';
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current_is(&parameter, "active"));
    mutable_default[2] = 'f';
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_MISSING);
    CHECK(current_is(&parameter, "default"));
}

static void text_rule_cases(void)
{
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_valid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_valid_vectors[i];
        bool valid = crux_config_text_valid(vector->bytes, vector->length);
        if (!valid) {
            printf("valid text refused: %s\n", vector->what);
        }
        CHECK(valid);
    }
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_invalid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_invalid_vectors[i];
        bool valid = crux_config_text_valid(vector->bytes, vector->length);
        if (valid) {
            printf("invalid text accepted: %s\n", vector->what);
        }
        CHECK(!valid);
    }
    CHECK(!crux_config_text_valid(NULL, 0));
    /* It reads only the bytes it is given: a sequence cut by the length is not well formed. */
    CHECK(!crux_config_text_valid("\xe2\x80\xa8", 2));
    CHECK(crux_config_text_valid("ab\n", 2));
}

int crux_configuration_string_conformance(void)
{
    checks = failures = 0;
    text_rule_cases();
    init_cases();
    default_cases();
    read_and_update_cases();
    validator_cases();
    encoding_cases();
    maximum_length_cases();
    load_cases();
    fallback_cases();
    printf("CONFIGURATION_STRING_%s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks,
           failures);
    return failures ? 1 : 0;
}
