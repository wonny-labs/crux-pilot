/*
 * Threads sharing the store on the Linux File provider. The store lock serialises every operation,
 * so each save succeeds and each read finds a whole value. A status read takes only the short lock.
 * The sanitizer preset does not detect data races: see the pull request for a ThreadSanitizer run.
 */
#include "credential_test.h"
#include "medium_linux.h"

#include <pthread.h>
#include <stdlib.h>

#define WRITES_PER_THREAD 40u
#define VALUE_BYTES 256u

typedef struct {
    crux_credential_record_t record;
    uint8_t fill;
    unsigned failures;
} writer_t;

static volatile int stop_readers;
static unsigned reader_failures;
static unsigned status_reads;

/* A value is all one byte, so a read that mixed two saves would show. */
static void *writer_main(void *argument)
{
    writer_t *writer = argument;
    uint8_t value[VALUE_BYTES];
    for (unsigned index = 0; index < WRITES_PER_THREAD; ++index) {
        memset(value, writer->fill + (int)(index % 8u), sizeof(value));
        if (crux_credential_write(writer->record, value, sizeof(value)) != CRUX_CREDENTIAL_OK) {
            writer->failures += 1;
        }
    }
    return NULL;
}

static bool value_whole(const uint8_t *value, size_t length)
{
    for (size_t index = 1; index < length; ++index) {
        if (value[index] != value[0]) {
            return false;
        }
    }
    return length == VALUE_BYTES;
}

static void *reader_main(void *argument)
{
    (void)argument;
    uint8_t value[VALUE_BYTES];
    size_t length = 0;
    while (!__atomic_load_n(&stop_readers, __ATOMIC_ACQUIRE)) {
        crux_credential_result_t result =
            crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, value, sizeof(value), &length);
        if (result != CRUX_CREDENTIAL_OK ||
            (result == CRUX_CREDENTIAL_OK && !value_whole(value, length))) {
            reader_failures += 1;
        }
    }
    return NULL;
}

static void *status_main(void *argument)
{
    (void)argument;
    while (!__atomic_load_n(&stop_readers, __ATOMIC_ACQUIRE)) {
        crux_credential_store_info_t info;
        if (crux_credential_store_get_info(&info) != CRUX_OK ||
            info.state != CRUX_CREDENTIAL_STATE_VALID) {
            reader_failures += 1;
        }
        status_reads += 1;
    }
    return NULL;
}

static void test_threads_share_the_store(void)
{
    uint8_t first[VALUE_BYTES];
    memset(first, 0x10, sizeof(first));
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, first, sizeof(first)) ==
          CRUX_CREDENTIAL_OK);

    writer_t writers[] = {{CRUX_CREDENTIAL_RECORD_ACCOUNTS, 0x20, 0},
                          {CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, 0x40, 0}};
    pthread_t writer_threads[2];
    pthread_t reader_thread;
    pthread_t status_thread;
    CHECK(pthread_create(&reader_thread, NULL, reader_main, NULL) == 0);
    CHECK(pthread_create(&status_thread, NULL, status_main, NULL) == 0);
    for (size_t index = 0; index < 2u; ++index) {
        CHECK(pthread_create(&writer_threads[index], NULL, writer_main, &writers[index]) == 0);
    }
    for (size_t index = 0; index < 2u; ++index) {
        CHECK(pthread_join(writer_threads[index], NULL) == 0);
        CHECK(writers[index].failures == 0);
    }
    __atomic_store_n(&stop_readers, 1, __ATOMIC_RELEASE);
    CHECK(pthread_join(reader_thread, NULL) == 0);
    CHECK(pthread_join(status_thread, NULL) == 0);
    CHECK(reader_failures == 0);
    CHECK(status_reads > 0);
    CHECK(crux_credential_test_buffers_clear());

    /* The last save of each writer is what a reset finds. */
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    memset(first, 0x20 + (int)((WRITES_PER_THREAD - 1u) % 8u), sizeof(first));
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, first, sizeof(first));
    memset(first, 0x40 + (int)((WRITES_PER_THREAD - 1u) % 8u), sizeof(first));
    test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, first, sizeof(first));
}

int main(void)
{
    credential_medium_linux_open();
    test_threads_share_the_store();
    int status = credential_test_finish("credential concurrency");
    credential_medium_linux_close();
    return status;
}
