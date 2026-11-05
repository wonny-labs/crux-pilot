/* Turns on the locks of TF-PSA-Crypto's core, which Zephyr's module offers no option for at this
 * pin. Mbed TLS calls the same core from TLS threads, so a lock in a Crux provider could not cover
 * them. It is included after the module's own configuration. */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
