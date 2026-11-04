#include "conformance.h"
#include "file_port.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef __ZEPHYR__
#include <zephyr/ztest.h>
#define CHECK(condition) zassert_true(condition, #condition)
#else
#include <assert.h>
#define CHECK(condition) assert(condition)
#endif

#define UNBOUND_ROOT ((crux_file_root_t)2)
#define MISSING_ROOT ((crux_file_root_t)255)

static void put(crux_file_root_t root, const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    CHECK(crux_file_open_at(root, name, CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    CHECK(crux_file_write(file, text, strlen(text), &count) == CRUX_FILE_OK);
    CHECK(count == strlen(text));
    CHECK(crux_file_sync(file) == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
}

static void expect(crux_file_root_t root, const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[32] = {0};
    CHECK(strlen(text) < sizeof(value));
    CHECK(crux_file_open_at(root, name, CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    CHECK(crux_file_read(file, value, sizeof(value), &count) == CRUX_FILE_OK);
    CHECK(count == strlen(text));
    CHECK(memcmp(value, text, count) == 0);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
}

static void expect_absent(crux_file_root_t root, const char *name)
{
    crux_file_info_t info;
    CHECK(crux_file_stat(root, name, &info) == CRUX_FILE_ERR_NOT_FOUND);
}

void crux_file_conformance_roots(const crux_file_conformance_env_t *env)
{
    (void)env;
    crux_file_t *file = NULL;
    crux_file_t *other = NULL;

    /* The original functions are root 0, and the same name in another root is another file. */
    put(CRUX_FILE_ROOT_DEFAULT, "shared", "zero");
    put(1, "shared", "one");
    expect(CRUX_FILE_ROOT_DEFAULT, "shared", "zero");
    expect(1, "shared", "one");
    CHECK(crux_file_open("shared", CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);

    /* The conflict rule compares the root as well as the name. */
    CHECK(crux_file_open_at(0, "shared", CRUX_FILE_READ_WRITE, &file) == CRUX_FILE_OK);
    CHECK(crux_file_open_at(1, "shared", CRUX_FILE_READ_WRITE, &other) == CRUX_FILE_OK);
    CHECK(crux_file_close(&other) == CRUX_FILE_OK);
    CHECK(crux_file_open("shared", CRUX_FILE_READ, &other) == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(other == NULL);
    CHECK(crux_file_remove_at(0, "shared") == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(crux_file_remove_at(1, "shared") == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
    CHECK(crux_file_remove("shared") == CRUX_FILE_OK);
    CHECK(crux_file_remove_at(1, "shared") == CRUX_FILE_ERR_NOT_FOUND);

    /* Argument checks come first, as for the original functions. */
    CHECK(crux_file_open_at(0, "a/b", CRUX_FILE_READ, &file) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_open_at(0, "x", (crux_file_mode_t)99, &file) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_open_at(0, "x", CRUX_FILE_READ, NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_remove_at(0, "") == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_open_at(MISSING_ROOT, "..", CRUX_FILE_READ, &file) ==
          CRUX_FILE_ERR_INVALID_ARGUMENT);

    /* A root that was never bound looks like a provider that was never bound. */
    crux_file_info_t info;
    crux_file_dir_t *dir = NULL;
    const crux_file_root_t absent[] = {UNBOUND_ROOT, MISSING_ROOT};
    for (size_t i = 0; i < sizeof(absent) / sizeof(absent[0]); i++) {
        CHECK(crux_file_open_at(absent[i], "x", CRUX_FILE_CREATE_REPLACE, &file) ==
              CRUX_FILE_ERR_NOT_READY);
        CHECK(file == NULL);
        CHECK(crux_file_remove_at(absent[i], "x") == CRUX_FILE_ERR_NOT_READY);
        CHECK(crux_file_stat(absent[i], "x", &info) == CRUX_FILE_ERR_NOT_READY);
        CHECK(crux_file_rename(absent[i], "x", "y") == CRUX_FILE_ERR_NOT_READY);
        CHECK(crux_file_dir_open(absent[i], &dir) == CRUX_FILE_ERR_NOT_READY);
        CHECK(dir == NULL);
    }
}

void crux_file_conformance_stat(const crux_file_conformance_env_t *env)
{
    crux_file_info_t info = {99};
    const char *bad[] = {NULL, "", ".", "..", "a/b", "a b", "a\\b", "\x80"};

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(crux_file_stat(0, bad[i], &info) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    }
    CHECK(crux_file_stat(0, "name", NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_stat(0, "name", &info) == CRUX_FILE_ERR_NOT_FOUND);

    put(0, "name", "abcd");
    CHECK(crux_file_stat(0, "name", &info) == CRUX_FILE_OK && info.size == 4);
    put(0, "name", "");
    CHECK(crux_file_stat(0, "name", &info) == CRUX_FILE_OK && info.size == 0);
    expect_absent(1, "name");
    CHECK(crux_file_remove("name") == CRUX_FILE_OK);

    /* Regular files only. */
    env->add_directory(0, "folder");
    CHECK(crux_file_stat(0, "folder", &info) == CRUX_FILE_ERR_NOT_FOUND);
}

void crux_file_conformance_size(void)
{
    crux_file_t *file = NULL;
    uint64_t size = 99;
    size_t count = 0;
    char value[2] = {0};

    CHECK(crux_file_size(NULL, &size) == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(crux_file_open("sized", CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    CHECK(crux_file_size(file, NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_size(file, &size) == CRUX_FILE_OK && size == 0);
    CHECK(crux_file_write(file, "abcdef", 6, &count) == CRUX_FILE_OK);

    /* It sees an unsynced write, and it does not move the position. */
    CHECK(crux_file_size(file, &size) == CRUX_FILE_OK && size == 6);
    CHECK(crux_file_seek(file, 2) == CRUX_FILE_OK);
    CHECK(crux_file_size(file, &size) == CRUX_FILE_OK && size == 6);
    CHECK(crux_file_read(file, value, 2, &count) == CRUX_FILE_OK && count == 2);
    CHECK(memcmp(value, "cd", 2) == 0);

    crux_file_t *stale = file;
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
    CHECK(crux_file_size(stale, &size) == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(crux_file_remove("sized") == CRUX_FILE_OK);
}

void crux_file_conformance_rename(const crux_file_conformance_env_t *env)
{
    crux_file_t *file = NULL;
    crux_file_info_t info;

    /* Arguments are checked first, with both names held to the name rule. */
    const char *bad[] = {NULL, "", ".", "..", "a/b", "a b"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(crux_file_rename(0, bad[i], "to") == CRUX_FILE_ERR_INVALID_ARGUMENT);
        CHECK(crux_file_rename(0, "from", bad[i]) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    }
    CHECK(crux_file_rename(MISSING_ROOT, "..", "to") == CRUX_FILE_ERR_INVALID_ARGUMENT);

    /* Then a live handle on either name, even when the other check would also fail. */
    put(0, "from", "old");
    CHECK(crux_file_open("from", CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    CHECK(crux_file_rename(0, "from", "to") == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(crux_file_rename(0, "missing", "from") == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
    put(0, "held", "keep");
    CHECK(crux_file_open("held", CRUX_FILE_READ_WRITE, &file) == CRUX_FILE_OK);
    CHECK(crux_file_rename(0, "missing", "held") == CRUX_FILE_ERR_INVALID_STATE);
    /* A handle in another root does not conflict. */
    put(1, "other", "x");
    CHECK(crux_file_rename(1, "other", "held") == CRUX_FILE_OK);
    CHECK(crux_file_remove_at(1, "held") == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);

    /* Then a missing source, and only then an existing destination. */
    CHECK(crux_file_rename(0, "missing", "to") == CRUX_FILE_ERR_NOT_FOUND);
    CHECK(crux_file_rename(0, "missing", "held") == CRUX_FILE_ERR_NOT_FOUND);
    CHECK(crux_file_rename(0, "missing", "missing") == CRUX_FILE_ERR_NOT_FOUND);
    CHECK(crux_file_rename(0, "from", "held") == CRUX_FILE_ERR_EXISTS);
    expect(0, "from", "old");
    expect(0, "held", "keep");
    CHECK(crux_file_rename(0, "from", "from") == CRUX_FILE_ERR_EXISTS);
    expect(0, "from", "old");

    /* A directory is never a source, and it is never replaced as a destination. */
    env->add_directory(0, "tree");
    CHECK(crux_file_rename(0, "tree", "to") == CRUX_FILE_ERR_NOT_FOUND);
    CHECK(crux_file_rename(0, "from", "tree") == CRUX_FILE_ERR_EXISTS);
    expect(0, "from", "old");
    /* Refused again, so the directory is still there. */
    CHECK(crux_file_rename(0, "from", "tree") == CRUX_FILE_ERR_EXISTS);
    CHECK(crux_file_stat(0, "tree", &info) == CRUX_FILE_ERR_NOT_FOUND);

    /* Success moves the name and the content, and renames stay inside one root. */
    CHECK(crux_file_rename(0, "from", "to") == CRUX_FILE_OK);
    expect_absent(0, "from");
    expect(0, "to", "old");
    expect_absent(1, "to");

    CHECK(crux_file_remove("to") == CRUX_FILE_OK);
    CHECK(crux_file_remove("held") == CRUX_FILE_OK);
}

static size_t list(crux_file_root_t root, const char **expected, size_t expected_count)
{
    crux_file_dir_t *dir = NULL;
    bool seen[8] = {false};
    size_t found = 0;

    CHECK(expected_count <= sizeof(seen));
    CHECK(crux_file_dir_open(root, &dir) == CRUX_FILE_OK);
    for (;;) {
        char name[64];
        crux_file_info_t info;
        bool end = false;
        CHECK(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK);
        if (end) {
            break;
        }
        bool known = false;
        for (size_t i = 0; i < expected_count; i++) {
            if (strcmp(name, expected[i]) == 0) {
                CHECK(!seen[i]);
                CHECK(info.size == strlen(expected[i]));
                seen[i] = true;
                known = true;
            }
        }
        CHECK(known);
        found++;
    }
    CHECK(crux_file_dir_close(&dir) == CRUX_FILE_OK);
    CHECK(dir == NULL);
    return found;
}

void crux_file_conformance_listing(const crux_file_conformance_env_t *env)
{
    crux_file_dir_t *dir = NULL;
    char name[64];
    crux_file_info_t info;
    bool end = false;

    CHECK(crux_file_dir_open(0, NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(list(1, NULL, 0) == 0);

    /* The names are their own content here, so info.size can be checked against them. */
    const char *names[] = {"a", "Mixed.Case-1_", "aa", "0123456789"};
    char longest[64];
    memset(longest, 'z', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = '\0';
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        put(1, names[i], names[i]);
    }
    CHECK(list(1, names, 4) == 4);

    /* Subdirectories and foreign files are skipped without an error. */
    env->add_directory(1, "folder");
    env->add_foreign_files(1);
    CHECK(list(1, names, 4) == 4);

    /* The longest name fits a 64 byte buffer, and a smaller one loses no entry. */
    crux_file_t *file = NULL;
    CHECK(crux_file_open_at(1, longest, CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
    CHECK(crux_file_dir_open(1, &dir) == CRUX_FILE_OK);
    size_t found = 0;
    bool found_longest = false;
    for (;;) {
        CHECK(crux_file_dir_read(dir, name, 63, &info, &end) == CRUX_FILE_ERR_OUT_OF_RANGE);
        CHECK(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK);
        if (end) {
            break;
        }
        found_longest = found_longest || strcmp(name, longest) == 0;
        found++;
    }
    CHECK(found == 5 && found_longest);

    /* The end repeats, with an empty name. */
    name[0] = 'x';
    CHECK(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK);
    CHECK(end && name[0] == '\0');

    /* Output and handle checks. */
    CHECK(crux_file_dir_read(dir, NULL, sizeof(name), &info, &end) ==
          CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_dir_read(dir, name, 0, &info, &end) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_dir_read(dir, name, sizeof(name), NULL, &end) ==
          CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_dir_read(dir, name, sizeof(name), &info, NULL) ==
          CRUX_FILE_ERR_INVALID_ARGUMENT);
    crux_file_dir_t *stale = dir;
    CHECK(crux_file_dir_open(1, &dir) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_dir_close(NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(crux_file_dir_close(&dir) == CRUX_FILE_OK && dir == NULL);
    CHECK(crux_file_dir_close(&dir) == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(crux_file_dir_close(&stale) == CRUX_FILE_ERR_INVALID_STATE);
    stale = NULL;
    CHECK(crux_file_dir_read(NULL, name, sizeof(name), &info, &end) == CRUX_FILE_ERR_INVALID_STATE);

    /* The pool is bounded, and a closed handle comes back. */
    crux_file_dir_t *pool[CRUX_FILE_DIR_POOL_SIZE] = {0};
    for (size_t i = 0; i < CRUX_FILE_DIR_POOL_SIZE; i++) {
        CHECK(crux_file_dir_open(1, &pool[i]) == CRUX_FILE_OK);
    }
    CHECK(crux_file_dir_open(1, &dir) == CRUX_FILE_ERR_NO_RESOURCES && dir == NULL);
    CHECK(crux_file_dir_close(&pool[0]) == CRUX_FILE_OK);
    CHECK(crux_file_dir_open(1, &dir) == CRUX_FILE_OK);
    CHECK(crux_file_dir_close(&dir) == CRUX_FILE_OK);
    for (size_t i = 1; i < CRUX_FILE_DIR_POOL_SIZE; i++) {
        CHECK(crux_file_dir_close(&pool[i]) == CRUX_FILE_OK);
    }

    /* A listing does not hold the files it passes over. */
    CHECK(crux_file_dir_open(1, &dir) == CRUX_FILE_OK);
    CHECK(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK);
    CHECK(crux_file_remove_at(1, "aa") == CRUX_FILE_OK);
    CHECK(crux_file_dir_close(&dir) == CRUX_FILE_OK);

    for (size_t i = 0; i < 4; i++) {
        if (strcmp(names[i], "aa") != 0) {
            CHECK(crux_file_remove_at(1, names[i]) == CRUX_FILE_OK);
        }
    }
    CHECK(crux_file_remove_at(1, longest) == CRUX_FILE_OK);
}

void crux_file_conformance_binding(const crux_file_conformance_env_t *env)
{
    CHECK(env->bind_root(0, NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    CHECK(env->bind_root(CRUX_FILE_ROOTS_MAX, env->path_free) == CRUX_FILE_ERR_INVALID_ARGUMENT);

    /* Once per index. */
    CHECK(env->bind_root(0, env->path_free) == CRUX_FILE_ERR_INVALID_STATE);

    /* No path may be equal to, inside or a parent of another bound root. */
    CHECK(env->bind_root(UNBOUND_ROOT, env->path_0) == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(env->bind_root(UNBOUND_ROOT, env->path_0_child) == CRUX_FILE_ERR_INVALID_STATE);
    CHECK(env->bind_root(UNBOUND_ROOT, env->path_0_parent) == CRUX_FILE_ERR_INVALID_STATE);

    /* A refused bind leaves the root unbound. */
    crux_file_info_t info;
    CHECK(crux_file_stat(UNBOUND_ROOT, "x", &info) == CRUX_FILE_ERR_NOT_READY);

    /* Text that starts with another root's path is not inside it. */
    CHECK(env->bind_root(UNBOUND_ROOT, env->path_0_prefix) == CRUX_FILE_OK);
    CHECK(env->bind_root(3, env->path_free) == CRUX_FILE_OK);
    put(UNBOUND_ROOT, "bound", "now");
    expect(UNBOUND_ROOT, "bound", "now");
    expect_absent(0, "bound");
    CHECK(crux_file_remove_at(UNBOUND_ROOT, "bound") == CRUX_FILE_OK);
}
