/* SPDX-License-Identifier: Apache-2.0 */
/* OS-level options of the Zephyr port. */
#ifndef CRUX_RELIANCE_EDGE_REDOSCONF_H
#define CRUX_RELIANCE_EDGE_REDOSCONF_H

/* The volume table is fixed at build time. */
#define REDOSCONF_MUTABLE_VOLCONF 0

/* Owners, permissions and symbolic links are off in this profile. */
#define REDOSCONF_FAKE_UID_GID 0
#define REDOSCONF_PERM_OVERRIDE 0
#define REDOSCONF_SYMLINK_FOLLOW 0

#endif /* CRUX_RELIANCE_EDGE_REDOSCONF_H */
