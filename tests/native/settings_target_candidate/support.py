# SPDX-License-Identifier: Apache-2.0
"""Private controlled-copy support; no target, mount, Flash or upstream writes."""
import hashlib
from pathlib import Path
import subprocess

PIN = "684c9e8f32e4373a21098559f748f06915f950c9"
CALLBACKS = '\n#ifndef PROBE_SETTINGS_H\n#define PROBE_SETTINGS_H\n#include <stddef.h>\n#include <stdint.h>\n#include <sys/types.h>\n#include <zephyr/sys/slist.h>\ntypedef ssize_t (*settings_read_cb)(void *, void *, size_t);\ntypedef int (*settings_load_direct_cb)(const char *, size_t, settings_read_cb,\n                                       void *, void *);\n'
PROTOTYPES = '\nstruct settings_handler_static {\n    const char *name;\n    int (*h_export)(int (*save)(const char *, const void *, size_t));\n};\nint settings_load_subtree(const char *);\nint settings_commit_subtree(const char *);\nint settings_save_subtree(const char *);\nint settings_load_subtree_direct(const char *, settings_load_direct_cb, void *);\nssize_t settings_load_one(const char *, void *, size_t);\nint settings_save_one(const char *, const void *, size_t);\nvoid settings_store_init(void);\nvoid settings_src_register(struct settings_store *);\nvoid settings_dst_register(struct settings_store *);\nint settings_name_next(const char *, const char **);\nint settings_name_steq(const char *, const char *, const char **);\n#endif\n'
SLIST = '\n#ifndef PROBE_SLIST_H\n#define PROBE_SLIST_H\n#include <stddef.h>\ntypedef struct probe_node { struct probe_node *next; } sys_snode_t;\ntypedef struct { sys_snode_t *head; sys_snode_t *tail; } sys_slist_t;\nstatic inline void sys_slist_init(sys_slist_t *list) {\n    list->head = NULL; list->tail = NULL;\n}\nstatic inline void sys_slist_append(sys_slist_t *list, sys_snode_t *node) {\n    node->next = NULL;\n    if (list->tail != NULL) list->tail->next = node; else list->head = node;\n    list->tail = node;\n}\n#define SYS_SLIST_FOR_EACH_CONTAINER(list, item, field) \\\n    for ((item) = (void *)(list)->head; (item) != NULL; \\\n         (item) = (void *)(item)->field.next)\n#endif\n'

def run(command, **kwargs):
    return subprocess.run(command, text=True, capture_output=True, timeout=30, **kwargs)

def prepare(zephyr, work, candidate=False):
    zephyr = Path(zephyr).resolve()
    revision = run(["git", "-C", str(zephyr), "rev-parse", "HEAD"])
    if revision.returncode or revision.stdout.strip() != PIN:
        raise RuntimeError("Refusing a different pinned source revision")
    relatives = ["subsys/settings/src/settings_store.c", "subsys/settings/src/settings_nvs.c",
                 "subsys/settings/include/settings/settings_nvs.h", "include/zephyr/settings/settings.h"]
    dirty = run(["git", "-C", str(zephyr), "status", "--porcelain", "--", *relatives])
    if dirty.returncode or dirty.stdout.strip():
        raise RuntimeError("Refusing modified pinned source inputs")
    hashes = {}
    for relative in relatives:
        source = zephyr / relative
        target = work / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        hashes[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
    header = (zephyr / "include/zephyr/settings/settings.h").read_text()
    declarations = header[header.index("struct settings_store_itf;\n"):
                          header.index("\n/**\n * Register a backend handler", header.index("struct settings_store_itf;\n"))]
    files = {
        "zephyr/settings/settings.h": CALLBACKS + declarations + PROTOTYPES,
        "zephyr/sys/slist.h": SLIST,
        "zephyr/types.h": "#include <stdint.h>\n#define __maybe_unused __attribute__((__unused__))\n",
        "zephyr/kernel.h": "#define MIN(a,b) ((a)<(b)?(a):(b))\n",
        "zephyr/sys/iterable_sections.h": "#define STRUCT_SECTION_FOREACH(type,item) for (struct type *item = NULL; item != NULL; item = NULL)\n",
        "zephyr/logging/log.h": '#define LOG_MODULE_DECLARE(...) _Static_assert(1, "logging shim")\n#define LOG_DBG(...) ((void)0)\n',
        "settings_priv.h": "void settings_lock_take(void);\nvoid settings_lock_release(void);\n",
    }
    for relative, text in files.items():
        target = work / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
    if candidate:
        for name in ("settings_store", "settings_nvs"):
            patch = Path(__file__).with_name(name + ".patch")
            applied = run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-i", str(patch.resolve())], cwd=work)
            if applied.returncode:
                raise RuntimeError(applied.stdout + applied.stderr)
    return hashes

def nvs_headers(work):
    """Dependency doubles below actual Settings/NVS backend; not the NVS engine."""
    header = work / 'zephyr/settings/settings.h'
    text = header.read_text().replace('#include <stddef.h>', '#include <stddef.h>\n#include <stdbool.h>', 1)
    text = text.replace('#endif\n', '''#define SETTINGS_MAX_DIR_DEPTH 8
#define SETTINGS_MAX_NAME_LEN (8 * SETTINGS_MAX_DIR_DEPTH)
#define SETTINGS_EXTRA_LEN ((SETTINGS_MAX_DIR_DEPTH - 1) + 2)
int settings_call_set_handler(const char *, size_t, settings_read_cb, void *, const struct settings_load_arg *);
#endif
''')
    header.write_text(text)
    files = {
        'zephyr/kvss/nvs.h': '''#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
struct device { int unused; };
struct nvs_fs { const struct device *flash_device; size_t sector_size, sector_count, offset; };
ssize_t nvs_read(struct nvs_fs *, uint16_t, void *, size_t);
ssize_t nvs_write(struct nvs_fs *, uint16_t, const void *, size_t);
int nvs_delete(struct nvs_fs *, uint16_t);
int nvs_mount(struct nvs_fs *);
''',
        'zephyr/storage/flash_map.h': '''#include <stddef.h>
struct flash_area { size_t fa_size, fa_off; const struct device *fa_dev; };
struct flash_sector { size_t fs_size; };
int flash_area_open(int, const struct flash_area **);
int flash_area_get_sectors(int, uint32_t *, struct flash_sector *);
#define DT_HAS_CHOSEN(x) 0
#define PARTITION_ID(x) 0
#define CONFIG_SETTINGS_NVS_SECTOR_SIZE_MULT 1
#define CONFIG_SETTINGS_NVS_SECTOR_COUNT 2
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
''',
        'zephyr/sys/crc.h': '''#include <stddef.h>
#include <stdint.h>
static inline uint16_t crc16_ccitt(uint16_t crc, const void *input, size_t n) {
    const unsigned char *p = input;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (unsigned j = 0; j < 8; ++j) crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
''',
    }
    for relative, text in files.items():
        path = work / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
