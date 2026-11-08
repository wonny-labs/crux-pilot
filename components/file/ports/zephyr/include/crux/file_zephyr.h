#ifndef CRUX_FILE_ZEPHYR_H
#define CRUX_FILE_ZEPHYR_H

/**
 * @file
 * Root binding of the Zephyr File provider. This is a composition interface, not a portable one.
 *
 * Only the platform composition of a product (the code that also mounts the file system) calls these
 * functions. Application code stays on <crux/file.h>. The names carry the provider on purpose: code that
 * includes this header is not portable to another provider, and a native-free consumer check rejects the
 * target that publishes it (crux::file_zephyr). The header includes no Zephyr, vendor or board header.
 *
 * A product reaches it by linking the CMake target crux::file_zephyr, which adds this include root and
 * crux::file. The File provider itself is selected by Kconfig and is not linked by this target.
 *
 * Shared rules of every function here:
 * - Ownership: the composition owns the mount. These functions never mount, format or select media, and
 *   the binding keeps no reference to the argument after the call returns (the path text is copied).
 * - Lifetime: a binding lasts for the life of the program. There is no rebind and no unbind, and the file
 *   system must stay mounted while File calls can be made.
 * - Execution context: thread context, once, before any concurrent File call. Not for interrupt context.
 * - Thread safety: none during binding. Every File call after the binding is thread safe as documented in
 *   <crux/file.h>.
 */

#include <crux/file.h>

/**
 * Bind root 0, the root that crux_file_open() and crux_file_remove() use.
 *
 * @param root Absolute path of an already mounted directory: 2 to 63 characters, no trailing slash.
 * @return CRUX_FILE_OK; CRUX_FILE_ERR_INVALID_ARGUMENT for a malformed path; CRUX_FILE_ERR_INVALID_STATE
 *         when root 0 is already bound or the path equals, lies inside or contains another bound root;
 *         CRUX_FILE_ERR_NOT_SUPPORTED when the path exists but is not a directory; otherwise the
 *         CRUX_FILE_ERR_* result that the native status of an absent or unreadable path maps to.
 */
crux_file_result_t crux_file_zephyr_bind(const char *root);

/**
 * Bind one root index. The overlap rule compares path text only. crux_file_zephyr_bind() is root 0.
 *
 * @param root Root index below the provider's root limit.
 * @param path Same rules as for crux_file_zephyr_bind().
 * @return As for crux_file_zephyr_bind(); an index at or above the root limit is
 *         CRUX_FILE_ERR_INVALID_ARGUMENT.
 */
crux_file_result_t crux_file_zephyr_bind_root(crux_file_root_t root, const char *path);

/**
 * The result of the last cleanup that failed while a File call was already returning another error, for
 * example closing a file after a failed create-and-replace open. The call still returns its first error.
 * Diagnostic only: read it after the failing call has returned.
 *
 * @return CRUX_FILE_OK when no such cleanup has failed, otherwise the last CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_zephyr_last_cleanup_error(void);

#endif
