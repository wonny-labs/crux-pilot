#ifndef CRUX_FILE_CONFORMANCE_H
#define CRUX_FILE_CONFORMANCE_H
#include <crux/file.h>

/*
 * File additions that every provider must behave the same way on: roots, stat, handle size,
 * rename and listing. The same cases run on the Linux provider, the fake and Zephyr's VFS.
 * Roots 0 and 1 must be bound to empty directories, and roots 2 and 3 must be unbound.
 */
typedef struct {
    /* Add a subdirectory that File cannot open, under a name that passes the name rule. */
    void (*add_directory)(crux_file_root_t root, const char *name);
    /* Add files in this root that the listing must not report, under the provider's native names.
     */
    void (*add_foreign_files)(crux_file_root_t root);
    crux_file_result_t (*bind_root)(crux_file_root_t root, const char *path);
    const char *path_0;        /* What root 0 is bound to. */
    const char *path_0_parent; /* An existing parent of path_0. */
    const char *path_0_child;  /* An existing directory inside path_0. */
    const char *path_free;     /* An existing directory, sharing no path with a bound root. */
    const char *path_0_prefix; /* An existing directory whose path text starts with path_0. */
} crux_file_conformance_env_t;

void crux_file_conformance_roots(const crux_file_conformance_env_t *env);
void crux_file_conformance_stat(const crux_file_conformance_env_t *env);
void crux_file_conformance_size(void);
void crux_file_conformance_rename(const crux_file_conformance_env_t *env);
void crux_file_conformance_listing(const crux_file_conformance_env_t *env);
/* Binds roots 2 and 3, so it must run last. */
void crux_file_conformance_binding(const crux_file_conformance_env_t *env);
#endif
