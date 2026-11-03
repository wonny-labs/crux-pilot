#ifndef CRUX_FILE_FAKE_H
#define CRUX_FILE_FAKE_H
#include <crux/file.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * An in-memory File provider for Host tests of code built on File. It sits behind the private port,
 * so the real common layer checks every call, and the crux::file functions are the only interface a
 * test's code under test uses. Storage lives in a shared mapping so that it survives the process
 * dying: a test forks a child, cuts it at a chosen mutation, and starts again in a new child.
 *
 * Power loss model, which is simpler than FatFs and proves nothing about a real card:
 *  - Create, truncate and write reach storage at a sync or a close of that handle.
 *  - Remove and rename reach storage when they return.
 *  - A cut stops at the start of a mutation, so that mutation and all later ones do not happen.
 */
typedef enum {
    CRUX_FILE_FAKE_OPEN,
    CRUX_FILE_FAKE_TRUNCATE,
    CRUX_FILE_FAKE_READ,
    CRUX_FILE_FAKE_WRITE,
    CRUX_FILE_FAKE_SEEK,
    CRUX_FILE_FAKE_SIZE,
    CRUX_FILE_FAKE_SYNC,
    CRUX_FILE_FAKE_CLOSE,
    CRUX_FILE_FAKE_REMOVE,
    CRUX_FILE_FAKE_STAT,
    CRUX_FILE_FAKE_RENAME,
    CRUX_FILE_FAKE_DIR_OPEN,
    CRUX_FILE_FAKE_DIR_READ,
    CRUX_FILE_FAKE_DIR_CLOSE,
    CRUX_FILE_FAKE_OP_COUNT
} crux_file_fake_op_t;

/* Largest file the fake can hold. A write past it gives CRUX_FILE_ERR_NO_SPACE. */
#define CRUX_FILE_FAKE_FILE_BYTES (64u * 1024u)

/* Empty the storage, unbind every root and clear faults. No File handle may be open. */
void crux_file_fake_reset(void);

/* The same checks as the real providers, against path text only. */
crux_file_result_t crux_file_fake_bind_root(crux_file_root_t root, const char *path);

/* Entries that File did not create. A raw name is stored as given, and it need not pass the rule.
 */
void crux_file_fake_add_directory(crux_file_root_t root, const char *name);
void crux_file_fake_add_raw(crux_file_root_t root, const char *raw_name);

/* The call of op that comes after skip more calls returns result, once. Only one per op is held. */
void crux_file_fake_fail(crux_file_fake_op_t op, unsigned skip, crux_file_result_t result);
/* How many calls of op reached the provider since the last reset. */
unsigned crux_file_fake_calls(crux_file_fake_op_t op);
/* Accept at most this many bytes per write, which gives CRUX_FILE_PARTIAL. SIZE_MAX turns it off.
 */
void crux_file_fake_limit_write(size_t bytes);
/* Call this once from inside the next write, as if another caller ran while the medium was busy. */
void crux_file_fake_on_write(void (*hook)(void));
/* Total bytes of file content the volume holds. UINT64_MAX is the default, no limit. */
void crux_file_fake_set_capacity(uint64_t bytes);

/*
 * Call cut, which normally calls _exit, at the start of the mutation that comes after this many
 * more mutations. If cut returns or is NULL, every later provider call returns CRUX_FILE_ERR_IO.
 */
void crux_file_fake_cut_after(uint32_t mutations, void (*cut)(void));
/* Mutations made since the last reset, which gives the range of cut points for a test to walk. */
uint32_t crux_file_fake_mutations(void);
/*
 * What is on the medium when power returns: whatever had not reached storage is gone, and provider
 * handles and the cut are cleared. Call it from a process that holds no File handles.
 */
void crux_file_fake_power_on(void);

/* The stored content of a file, without touching File. False if it does not exist. */
bool crux_file_fake_exists(crux_file_root_t root, const char *name);
#endif
