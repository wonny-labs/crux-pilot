/* SPDX-License-Identifier: Apache-2.0 */
/* Actual copied Settings store code; only external locks/stores/handlers are doubles. */
#include "subsys/settings/src/settings_store.c"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int depth, calls, commits, commit_result, stop;
static size_t copied;
struct store {
    struct settings_store base;
    int scan, read_error;
    size_t advertised, read_limit;
    ssize_t size;
};
void settings_lock_take(void) {
    ++depth;
}
void settings_lock_release(void) {
    assert(depth == 1);
    --depth;
}
int settings_commit_subtree(const char *name) {
    (void)name;
    ++commits;
    return commit_result;
}
int settings_name_next(const char *name, const char **next) {
    const char *slash = strchr(name, '/');
    if (next)
        *next = slash ? slash + 1 : NULL;
    return slash ? (int)(slash - name) : (int)strlen(name);
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t n = strlen(key);
    int equal = strncmp(name, key, n) == 0 && (name[n] == '\0' || name[n] == '/');
    if (next)
        *next = equal ? name + n : NULL;
    return equal;
}
static ssize_t read_value(void *context, void *buffer, size_t length) {
    struct store *s = context;
    if (s->read_error)
        return s->read_error;
    size_t n = s->advertised < length ? s->advertised : length;
    if (n > s->read_limit)
        n = s->read_limit;
    if (n)
        memset(buffer, 0x5a, n);
    copied += n;
    return (ssize_t)n;
}
static int load(struct settings_store *base, const struct settings_load_arg *arg) {
    ++calls;
    struct store *s = (void *)base;
    if (s->scan)
        return s->scan;
    if (!arg->cb)
        return 0;
    return arg->cb("", s->advertised, read_value, s, arg->param);
}
static ssize_t get_length(struct settings_store *base, const char *name) {
    (void)name;
    ++calls;
    return ((struct store *)base)->size;
}
static int callback(const char *name, size_t length, settings_read_cb read, void *context,
                    void *parameter) {
    (void)name;
    (void)length;
    (void)read;
    (void)context;
    (void)parameter;
    return stop;
}
static const struct settings_store_itf methods = {.csi_load = load};
static const struct settings_store_itf length_methods = {.csi_get_val_len = get_length};

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    const char *test = argv[1];
    settings_store_init();
    struct store first = {.base.cs_itf = &methods, .advertised = 8, .read_limit = SIZE_MAX};
    struct store second = first;
    settings_src_register(&first.base);
    unsigned char buffer[8] = {0};
    size_t capacity = sizeof(buffer);
    bool two = false;
    if (strstr(test, "two") || strstr(test, "later") || strstr(test, "stop") ||
        strstr(test, "getter"))
        two = true;
    if (strstr(test, "scan_error") || strstr(test, "earlier"))
        first.scan = -EIO;
    if (!strcmp(test, "later_error"))
        second.scan = -EIO;
    if (!strcmp(test, "short_read"))
        first.read_limit = 2;
    if (!strcmp(test, "read_error"))
        first.read_error = -EIO;
    if (!strcmp(test, "small_buffer"))
        capacity = 2;
    if (!strcmp(test, "zero_buffer"))
        capacity = 0;
    if (!strcmp(test, "valid_two"))
        second.advertised = 5;
    if (!strcmp(test, "positive_stop"))
        stop = 7;
    if (!strcmp(test, "negative_stop"))
        stop = -ENOSPC;
    if (!strcmp(test, "commit_error"))
        commit_result = -EAGAIN;
    if (!strcmp(test, "getter_error")) {
        first.base.cs_itf = second.base.cs_itf = &length_methods;
        first.size = -EIO;
        second.size = 8;
    }
    if (two)
        settings_src_register(&second.base);
    ssize_t result;
    if (!strcmp(test, "subtree_scan_error") || !strcmp(test, "commit_error") ||
        !strcmp(test, "valid_subtree"))
        result = settings_load_subtree("probe");
    else if (!strcmp(test, "direct_scan_error") || strstr(test, "stop"))
        result = settings_load_subtree_direct("probe", callback, NULL);
    else if (!strcmp(test, "getter_error"))
        result = settings_get_val_len("probe");
    else
        result = settings_load_one("probe", buffer, capacity);
    printf("%zd %d %d %zu %d\n", result, calls, commits, copied, depth);
    return 0;
}
