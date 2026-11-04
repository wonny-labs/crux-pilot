/*
 * wolfSSH build settings for a Crux composition.
 *
 * wolfssh/settings.h includes this file through CONFIG_WOLFSSH_SETTINGS_FILE, after the wolfSSL
 * user_settings.h, so it applies to wolfSSH translation units only. wolfSSL and the TLS socket
 * backend keep NO_FILESYSTEM and never see these definitions. See docs/ssh-provider-contract.md.
 */
#ifndef CRUX_WOLFSSH_SETTINGS_H
#define CRUX_WOLFSSH_SETTINGS_H

/* SFTP reaches files only through the port header myFilesystem.h, owned by Issue #194. */
#undef NO_FILESYSTEM
#undef WOLFSSH_SFTP
#define WOLFSSH_SFTP
#undef WOLFSSH_USER_FILESYSTEM
#define WOLFSSH_USER_FILESYSTEM

/*
 * The wolfSSH module always defines WOLFSSH_ZEPHYR. With NO_FILESYSTEM undefined that compiles its
 * direct Zephyr file code in port.c and ssh.c even under WOLFSSH_USER_FILESYSTEM, and it removes
 * the SETSTAT and FSETSTAT handlers from wolfsftp.c. Nothing else in wolfSSH needs it once the
 * provider supplies the socket callbacks, which WOLFSSH_USER_IO requires.
 */
#undef WOLFSSH_ZEPHYR
#undef WOLFSSH_USER_IO
#define WOLFSSH_USER_IO

/* Owner decision: SSH keys are Ed25519 and ECDSA only. wolfSSL keeps RSA for TLS. */
#undef WOLFSSH_NO_RSA
#define WOLFSSH_NO_RSA

/* The design offers no SHA-1 and no CBC. wolfSSL may still hold them for TLS. */
#undef WOLFSSH_NO_SHA1
#define WOLFSSH_NO_SHA1
#undef WOLFSSH_NO_AES_CBC
#define WOLFSSH_NO_AES_CBC

#endif /* CRUX_WOLFSSH_SETTINGS_H */
