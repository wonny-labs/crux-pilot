#ifndef CRUX_SOCKETS_ZEPHYR_CLOSE_DECLARATION_H
#define CRUX_SOCKETS_ZEPHYR_CLOSE_DECLARATION_H
/* Pinned Zephyr unistd.h hides this standard declaration behind POSIX_API,
 * although POSIX_DEVICE_IO supplies the actual implementation. Remove when
 * the supported upstream header exposes it for that narrower option. */
#ifdef __cplusplus
extern "C" {
#endif
int close(int descriptor);
#ifdef __cplusplus
}
#endif
#endif
