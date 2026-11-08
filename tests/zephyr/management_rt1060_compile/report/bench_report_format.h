/*
 * Lines of the bench report, as text in a caller's buffer (row C2 of
 * docs/rt1060-management-image-feasibility.md). Pure C: no Zephyr, no libc formatting, no allocation,
 * so that a host test builds it and the image links it for a few hundred bytes.
 *
 * Every function returns the length written, without the terminator, or 0 when the line does not
 * fit the buffer, an argument is not valid, or the numbers do not add up. On 0 the buffer holds an
 * empty string: a line is whole or absent, never cut. A line is never longer than
 * BENCH_REPORT_LINE_MAX - 1 characters. The grammar is in docs/rt1060-management-image-feasibility.md
 * and is read by check_bench_uart.py.
 */
#ifndef CRUX_BENCH_REPORT_FORMAT_H
#define CRUX_BENCH_REPORT_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The buffer for one line, with its terminator. The longest line is the certificate line, 134 characters. */
#define BENCH_REPORT_LINE_MAX 160u
/** A thread name as printed, with its terminator. Longer names are cut, never wrapped. */
#define BENCH_REPORT_NAME_MAX 24u
/** The text of the module's certificate fingerprint: 32 hexadecimal pairs joined by colons. */
#define BENCH_REPORT_FINGERPRINT_CHARS 95u

typedef enum {
    BENCH_REPORT_BOOT = 0,  /**< Once, when the report starts: the idle baseline before any client. */
    BENCH_REPORT_QUIET = 1, /**< The traffic of a client has stopped for the quiet period. */
    BENCH_REPORT_IDLE = 2,  /**< Nothing happened for the idle period, once per burst. */
} bench_report_reason_t;

/** "BENCH-REPORT v=1 begin seq=.. reason=.. uptime_ms=.. bursts=..". */
size_t bench_report_format_begin(char *out, size_t capacity, uint32_t seq,
                                 bench_report_reason_t reason, uint32_t uptime_ms,
                                 uint32_t bursts);
/**
 * "BENCH-STACK seq=.. thread=.. size=.. used=.. unused=.. state=live|ended". The name is as
 * bench_report_sanitize_name() makes it. 0 for a NULL or empty name, or unused above size.
 */
size_t bench_report_format_stack(char *out, size_t capacity, uint32_t seq, const char *name,
                                 uint32_t size, uint32_t unused, bool ended);
/** "BENCH-HEAP seq=.. arena=.. in_use=.. peak=.. free=..". */
size_t bench_report_format_heap(char *out, size_t capacity, uint32_t seq, uint32_t arena,
                                uint32_t in_use, uint32_t peak, uint32_t free_bytes);
/** "BENCH-REPORT v=1 end seq=.. lines=.. missed=..": lines are the STACK and HEAP lines before it. */
size_t bench_report_format_end(char *out, size_t capacity, uint32_t seq, uint32_t lines,
                               uint32_t missed);
/**
 * "BENCH-CERT v=1 sha256=<fingerprint> source=generated|stored". The fingerprint must be exactly
 * 32 uppercase hexadecimal pairs joined by colons, which is what `openssl x509 -fingerprint -sha256`
 * prints after its label; anything else gives 0.
 */
size_t bench_report_format_cert(char *out, size_t capacity, const char *fingerprint,
                                bool generated);

/**
 * Copies a thread name for printing: only A-Z a-z 0-9 _ . [ ] - are kept, any other byte becomes
 * '_', and at most BENCH_REPORT_NAME_MAX - 1 characters are copied. A NULL or empty name becomes
 * "thread@<fallback_id in lowercase hexadecimal>". Returns the length, or 0 when the buffer holds
 * fewer than 2 bytes or is NULL.
 */
size_t bench_report_sanitize_name(char *out, size_t capacity, const char *name,
                                  uintptr_t fallback_id);

#endif
