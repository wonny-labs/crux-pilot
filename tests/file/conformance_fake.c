#include "conformance.h"
#include "fake_file.h"
#include <assert.h>
#include <stdio.h>

static void add_directory(crux_file_root_t root, const char *name)
{
    crux_file_fake_add_directory(root, name);
}

static void add_foreign_files(crux_file_root_t root)
{
    crux_file_fake_add_raw(root, "has space");
    crux_file_fake_add_raw(root,
                           "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy");
}

int main(void)
{
    crux_file_fake_reset();
    assert(crux_file_fake_bind_root(0, "/volume/a") == CRUX_FILE_OK);
    assert(crux_file_fake_bind_root(1, "/volume/b") == CRUX_FILE_OK);
    const crux_file_conformance_env_t env = {
        .add_directory = add_directory,
        .add_foreign_files = add_foreign_files,
        .bind_root = crux_file_fake_bind_root,
        .path_0 = "/volume/a",
        .path_0_parent = "/volume",
        .path_0_child = "/volume/a/inner",
        .path_free = "/other",
        .path_0_prefix = "/volume/a2",
    };
    crux_file_conformance_roots(&env);
    crux_file_conformance_stat(&env);
    crux_file_conformance_size();
    crux_file_conformance_rename(&env);
    crux_file_conformance_listing(&env);
    crux_file_conformance_binding(&env);
    puts("File conformance on the fake provider passed");
    return 0;
}
