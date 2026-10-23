#!/usr/bin/env python3
"""Run the pinned Settings store wrappers against isolated Host fault stores.

Investigation only: no Zephyr runtime, NVS, Flash or public Crux API is built.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

PIN = "684c9e8f32e4373a21098559f748f06915f950c9"

CALLBACKS = r'''
#ifndef PROBE_SETTINGS_H
#define PROBE_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <zephyr/sys/slist.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
typedef int (*settings_load_direct_cb)(const char *, size_t, settings_read_cb,
                                       void *, void *);
'''

PROTOTYPES = r'''
struct settings_handler_static {
    const char *name;
    int (*h_export)(int (*save)(const char *, const void *, size_t));
};
int settings_load_subtree(const char *);
int settings_commit_subtree(const char *);
int settings_save_subtree(const char *);
int settings_load_subtree_direct(const char *, settings_load_direct_cb, void *);
ssize_t settings_load_one(const char *, void *, size_t);
int settings_save_one(const char *, const void *, size_t);
void settings_store_init(void);
void settings_src_register(struct settings_store *);
void settings_dst_register(struct settings_store *);
int settings_name_next(const char *, const char **);
int settings_name_steq(const char *, const char *, const char **);
#endif
'''

SLIST = r'''
#ifndef PROBE_SLIST_H
#define PROBE_SLIST_H
#include <stddef.h>
typedef struct probe_node { struct probe_node *next; } sys_snode_t;
typedef struct { sys_snode_t *head; sys_snode_t *tail; } sys_slist_t;
static inline void sys_slist_init(sys_slist_t *list) {
    list->head = NULL; list->tail = NULL;
}
static inline void sys_slist_append(sys_slist_t *list, sys_snode_t *node) {
    node->next = NULL;
    if (list->tail != NULL) list->tail->next = node; else list->head = node;
    list->tail = node;
}
#define SYS_SLIST_FOR_EACH_CONTAINER(list, item, field) \
    for ((item) = (void *)(list)->head; (item) != NULL; \
         (item) = (void *)(item)->field.next)
#endif
'''

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/settings/settings.h>
extern struct settings_store *settings_save_dst;
static int lock_depth;
void settings_lock_take(void) { ++lock_depth; }
void settings_lock_release(void) { assert(lock_depth > 0); --lock_depth; }
int settings_commit_subtree(const char *name) { (void)name; return 0; }
int settings_name_next(const char *name, const char **next) {
    const char *slash = strchr(name, '/');
    if (next != NULL) *next = slash == NULL ? NULL : slash + 1;
    return slash == NULL ? (int)strlen(name) : (int)(slash - name);
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t length = strlen(key);
    int equal = strncmp(name, key, length) == 0 &&
                (name[length] == '\0' || name[length] == '/');
    if (next != NULL) *next = equal ? name + length : NULL;
    return equal;
}
struct fault_store {
    struct settings_store base;
    int traversal_result;
    int read_result;
    int save_result;
    int lifecycle_result;
    size_t copied;
    int changed;
    int lifecycle_calls;
};
static ssize_t read_value(void *context, void *buffer, size_t length) {
    struct fault_store *store = context;
    if (store->read_result < 0) return store->read_result;
    size_t copied = (size_t)store->read_result;
    if (copied > length) copied = length;
    memset(buffer, 0x5a, copied);
    store->copied += copied;
    return store->read_result;
}
static int load(struct settings_store *base, const struct settings_load_arg *arg) {
    struct fault_store *store = (void *)base;
    if (store->traversal_result != 0) return store->traversal_result;
    if (arg->cb == NULL) return 0;
    return arg->cb("", 8, read_value, store, arg->param);
}
static int save(struct settings_store *base, const char *name,
                const char *bytes, size_t length) {
    struct fault_store *store = (void *)base;
    (void)name; (void)bytes; (void)length;
    store->changed = 1; /* A backend can change bytes before reporting failure. */
    return store->save_result;
}
static int lifecycle(struct settings_store *base) {
    struct fault_store *store = (void *)base;
    ++store->lifecycle_calls;
    return store->lifecycle_result;
}
static const struct settings_store_itf interface = {
    .csi_load = load, .csi_save = save,
    .csi_save_start = lifecycle, .csi_save_end = lifecycle
};
static void reset(struct fault_store *store) {
    assert(lock_depth == 0);
    settings_store_init(); settings_save_dst = NULL;
    memset(store, 0, sizeof(*store));
    store->base.cs_itf = &interface;
    store->read_result = 8;
    settings_src_register(&store->base);
}
static void observe(const char *label, int actual, int expected) {
    printf("%s observed=%d expected=%d\n", label, actual, expected);
    fflush(stdout);
    assert(actual == expected);
    assert(lock_depth == 0);
}
static int callback(const char *name, size_t length, settings_read_cb read,
                    void *context, void *parameter) {
    (void)name; (void)length; (void)read; (void)context; (void)parameter;
    return 0;
}
int main(void) {
    struct fault_store first, second;
    unsigned char buffer[8] = {0};
    reset(&first); first.traversal_result = -EIO;
    observe("subtree_discards_traversal_error", settings_load_subtree("probe"), 0);
    observe("direct_discards_traversal_error",
            settings_load_subtree_direct("probe", callback, NULL), 0);
    observe("load_one_propagates_final_traversal_error",
            (int)settings_load_one("probe/key", buffer, sizeof(buffer)), -EIO);
    reset(&first); first.read_result = 2;
    observe("load_one_reports_advertised_length_after_short_read",
            (int)settings_load_one("probe/key", buffer, sizeof(buffer)), 8);
    observe("actual_short_read_bytes", (int)first.copied, 2);
    reset(&first); first.read_result = -EIO;
    observe("load_one_propagates_callback_read_error",
            (int)settings_load_one("probe/key", buffer, sizeof(buffer)), -EIO);
    reset(&first); first.traversal_result = -EIO;
    memset(&second, 0, sizeof(second));
    second.base.cs_itf = &interface; second.read_result = 8;
    settings_src_register(&second.base);
    observe("later_source_masks_earlier_error",
            (int)settings_load_one("probe/key", buffer, sizeof(buffer)), 8);
    reset(&first); settings_dst_register(&first.base); first.save_result = -ENOSPC;
    observe("save_one_propagates_no_space",
            settings_save_one("probe/key", buffer, sizeof(buffer)), -ENOSPC);
    first.save_result = -EIO; first.changed = 0;
    observe("save_one_propagates_error_after_change",
            settings_save_one("probe/key", buffer, sizeof(buffer)), -EIO);
    observe("failed_save_may_have_changed_bytes", first.changed, 1);
    first.lifecycle_result = -EIO;
    observe("save_subtree_discards_lifecycle_errors", settings_save_subtree("probe"), 0);
    observe("both_lifecycle_hooks_ran", first.lifecycle_calls, 2);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zephyr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.zephyr.resolve()
    source_path = root / "subsys/settings/src/settings_store.c"
    header_path = root / "include/zephyr/settings/settings.h"
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise SystemExit("Refusing a different Zephyr revision")
    dirty = subprocess.check_output(["git", "-C", str(root), "status", "--porcelain", "--",
                                     str(source_path), str(header_path)], text=True)
    if dirty:
        raise SystemExit("Refusing modified pinned input files")
    compiler = shutil.which("cc")
    if compiler is None:
        raise SystemExit("C compiler unavailable; no result claimed")
    source = source_path.read_bytes()
    header = header_path.read_text()
    declarations = header[header.index("struct settings_store_itf;\n"):
                          header.index("\n/**\n * Register a backend handler", header.index("struct settings_store_itf;\n"))]
    with tempfile.TemporaryDirectory(prefix="crux-settings-probe-") as temporary:
        work = Path(temporary)
        files = {
            "settings_store.c": source.decode(),
            "harness.c": HARNESS,
            "zephyr/settings/settings.h": CALLBACKS + declarations + PROTOTYPES,
            "zephyr/sys/slist.h": SLIST,
            "zephyr/types.h": "#include <stdint.h>\n#define __maybe_unused __attribute__((__unused__))\n",
            "zephyr/kernel.h": "#define MIN(a,b) ((a)<(b)?(a):(b))\n",
            "zephyr/sys/iterable_sections.h": "#define STRUCT_SECTION_FOREACH(type,item) for (struct type *item = NULL; item != NULL; item = NULL)\n",
            "zephyr/logging/log.h": '#define LOG_MODULE_DECLARE(...) _Static_assert(1, "logging shim")\n',
            "settings_priv.h": "void settings_lock_take(void);\nvoid settings_lock_release(void);\n",
        }
        for relative, content in files.items():
            destination = work / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content)
        if (work / "settings_store.c").read_bytes() != source:
            raise SystemExit("Copied source does not match pinned input")
        command = [compiler, "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                   "-fsanitize=address,undefined", "-I", str(work), str(work / "settings_store.c"),
                   str(work / "harness.c"), "-o", str(work / "probe")]
        subprocess.run(command, check=True, text=True)
        result = subprocess.run([str(work / "probe")], check=True, text=True, capture_output=True)
        print(result.stdout, end="")
        # Oracle control: change only the copied direct-load return; original remains intact.
        text = source.decode()
        start = text.index("int settings_load_subtree_direct(")
        end = text.index("\nstruct default_param", start)
        section = text[start:end]
        if section.count("return 0;") != 1:
            raise SystemExit("Unexpected pinned function shape")
        mutated = text[:start] + section.replace("return 0;", "return -EIO;") + text[end:]
        (work / "settings_store.c").write_text(mutated)
        subprocess.run(command, check=True, text=True)
        control = subprocess.run([str(work / "probe")], text=True, capture_output=True, cwd=work)
        if control.returncode == 0 or "direct_discards_traversal_error observed=" not in control.stdout:
            raise SystemExit("Oracle negative control did not reject changed behavior")
        report = {
            "scope": "isolated Host source-wrapper probe; not Zephyr/NVS/Flash qualification",
            "zephyr_revision": revision,
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "settings_header_sha256": hashlib.sha256(header_path.read_bytes()).hexdigest(),
            "harness_sha256": hashlib.sha256(HARNESS.encode()).hexdigest(),
            "observations": result.stdout.splitlines(),
            "oracle_mutation_rejected": True,
            "sanitizers": "AddressSanitizer and UndefinedBehaviorSanitizer",
            "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
            "limits": ["list/lock/handler enumeration are Host shims", "no actual NVS or Zephyr runtime",
                       "no backend correctness, persistence or physical safety claim"],
        }
        output = args.output
        output.write_text(json.dumps(report, indent=2) + "\n")
        print("Pinned source observations reproduced; oracle mutation rejected.")


if __name__ == "__main__":
    main()
