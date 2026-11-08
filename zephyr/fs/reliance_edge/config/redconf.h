/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Crux default profile for Reliance Edge, written against the checks in redconfigchk.h. Every value
 * that a formatted medium records must match between the image that formats it and the image that
 * mounts it. Values that a composition may tune come from Kconfig.
 */
#ifndef CRUX_RELIANCE_EDGE_REDCONF_H
#define CRUX_RELIANCE_EDGE_REDCONF_H

#include <redtransact.h>

#if !defined(CONFIG_CRUX_FS_RELIANCE_EDGE_BUFFER_COUNT)
#error "This configuration is built through CONFIG_CRUX_FS_RELIANCE_EDGE"
#endif

/* API: the POSIX-like one, read-write, without the alternative minimal API. */
#define REDCONF_READ_ONLY 0
#define REDCONF_API_POSIX 1
#define REDCONF_API_FSE 0
#define REDCONF_API_FSE_FORMAT 0
#define REDCONF_API_FSE_TRUNCATE 0
#define REDCONF_API_FSE_TRANSMASKSET 0
#define REDCONF_API_FSE_TRANSMASKGET 0

/* The calls the VFS needs. The formatter is compiled only with its own option. */
#if defined(CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT)
#define REDCONF_API_POSIX_FORMAT 1
#else
#define REDCONF_API_POSIX_FORMAT 0
#endif
#define REDCONF_API_POSIX_UNLINK 1
#define REDCONF_API_POSIX_MKDIR 1
#define REDCONF_API_POSIX_RMDIR 1
#define REDCONF_API_POSIX_RENAME 1
#define REDCONF_RENAME_ATOMIC 1
#define REDCONF_API_POSIX_FTRUNCATE 1
#define REDCONF_API_POSIX_READDIR 1

/* Off: links, symbolic links, space reservation, trim, working directories, owners and
 * permissions, and deleting a file that is still open. */
#define REDCONF_API_POSIX_LINK 0
#define REDCONF_API_POSIX_SYMLINK 0
#define REDCONF_API_POSIX_FRESERVE 0
#define REDCONF_API_POSIX_FSTRIM 0
#define REDCONF_API_POSIX_CWD 0
#define REDCONF_POSIX_OWNER_PERM 0
#define REDCONF_DELETE_OPEN 0

/* Names: the File provider's 63-character limit. */
#define REDCONF_NAME_MAX 63U
#define REDCONF_PATH_SEPARATOR '/'

/* Concurrency and handles. */
/* One entry more than the adapter's thread slots: the spare one is for a thread without a slot
 * while it closes a file or a directory. */
#define REDCONF_TASK_COUNT (CONFIG_CRUX_FS_RELIANCE_EDGE_TASK_COUNT + 1)
#define REDCONF_HANDLE_COUNT CONFIG_CRUX_FS_RELIANCE_EDGE_HANDLE_COUNT

/* CPU. */
#if defined(CONFIG_BIG_ENDIAN)
#define REDCONF_ENDIAN_BIG 1
#define REDCONF_ENDIAN_SWAP
#else
#define REDCONF_ENDIAN_BIG 0
#endif
#define REDCONF_ALIGNMENT_SIZE 8U

/* The table-driven CRC with a 1 KiB table. The name is defined where the CRC is implemented, and
 * this macro is expanded only there. */
#define REDCONF_CRC_ALGORITHM CRC_SARWATE

/* Inodes: no timestamps, so no clock contract is needed. */
#define REDCONF_INODE_TIMESTAMPS 0
#define REDCONF_ATIME 0
#define REDCONF_INODE_BLOCKS 1
#define REDCONF_DIRECT_POINTERS 4U
#define REDCONF_INDIRECT_POINTERS 32U

/* Inode map: inline where it fits, external otherwise. */
#define REDCONF_IMAP_INLINE 1
#define REDCONF_IMAP_EXTERNAL 1

/* No output, and no asserts: a media error must reach the caller, not stop the thread. */
#define REDCONF_OUTPUT 0
#define REDCONF_ASSERTS 0

/* Automatic transactions, without link. Fsync must be one of them, or a sync commits nothing. */
#define REDCONF_TRANSACT_DEFAULT                                                                   \
    (RED_TRANSACT_CREAT | RED_TRANSACT_MKDIR | RED_TRANSACT_RENAME | RED_TRANSACT_UNLINK |         \
     RED_TRANSACT_FSYNC | RED_TRANSACT_CLOSE | RED_TRANSACT_VOLFULL | RED_TRANSACT_UMOUNT |        \
     RED_TRANSACT_SYNC)

/* Buffers: 4 KiB blocks, aligned for a D-cache line and the RT1060 SDHC setting. */
#define REDCONF_BLOCK_SIZE 4096U
#define REDCONF_BUFFER_COUNT CONFIG_CRUX_FS_RELIANCE_EDGE_BUFFER_COUNT
#define REDCONF_BUFFER_ALIGNMENT 32U
#define REDCONF_BUFFER_WRITE_GATHER_SIZE_KB 0U

/* One volume; the rest are tools and features outside this profile. */
#define REDCONF_VOLUME_COUNT 1U
#define REDCONF_DISCARDS 0
#define REDCONF_IMAGE_BUILDER 0
#define REDCONF_CHECKER 0

#endif /* CRUX_RELIANCE_EDGE_REDCONF_H */
