/*
 * wolfCrypt for the Linux proof server in this directory, with what the design's algorithm table
 * needs and as the Zephyr module builds it where that matters: wolfSSL keeps NO_FILESYSTEM. Only
 * this test uses it.
 */
#ifndef CRUX_SSH_SFTP_PROOF_USER_SETTINGS_H
#define CRUX_SSH_SFTP_PROOF_USER_SETTINGS_H

#define WOLFCRYPT_ONLY
#define NO_FILESYSTEM
#define NO_RSA
#define NO_DSA
#define NO_DH
#define NO_MD4
#define NO_MD5
#define NO_DES3
#define NO_RC4
#define NO_PWDBASED
#define NO_OLD_TLS
#define WOLFSSL_NO_OPTIONS_H

/* wolfCrypt reads no seed device under NO_FILESYSTEM. A test seed from the host: see proof_seed.c.
 */
extern int crux_ssh_sftp_proof_seed(unsigned char *output, unsigned int size);
#define CUSTOM_RAND_GENERATE_SEED crux_ssh_sftp_proof_seed

#define WOLFSSL_WOLFSSH
#define WOLFSSL_KEY_GEN
#define WOLFSSL_ASN_TEMPLATE
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_384
#define WOLFSSL_SP_521
#define WOLFSSL_PUBLIC_MP
#define HAVE_HASHDRBG
#define WC_NO_HARDEN

#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define HAVE_ED25519
#define HAVE_ED25519_KEY_IMPORT
#define HAVE_ED25519_KEY_EXPORT
#define WOLFSSL_ED25519_STREAMING_VERIFY
#define HAVE_CURVE25519
#define HAVE_AESGCM
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_DIRECT
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512

#endif /* CRUX_SSH_SFTP_PROOF_USER_SETTINGS_H */
