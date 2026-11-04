/* The character rule C2, as vectors. Configuration and the status service both run them, so that
 * the rule cannot differ between the two. */
#ifndef CONFIGURATION_TEXT_VECTORS_H
#define CONFIGURATION_TEXT_VECTORS_H

#include <stddef.h>

typedef struct {
    const char *what;
    const char *bytes;
    size_t length;
} config_text_vector_t;

#define CONFIG_TEXT_VECTOR(what, literal) {what, literal, sizeof(literal) - 1u}

static const config_text_vector_t config_text_valid_vectors[] = {
    CONFIG_TEXT_VECTOR("empty", ""),
    CONFIG_TEXT_VECTOR("ascii", "plain value 123"),
    CONFIG_TEXT_VECTOR("space", " "),
    CONFIG_TEXT_VECTOR("tilde, the last printable ascii", "~"),
    CONFIG_TEXT_VECTOR("quote and backslash", "\"\\"),
    CONFIG_TEXT_VECTOR("U+00A0, the first above C1", "\xc2\xa0"),
    CONFIG_TEXT_VECTOR("U+00E9", "\xc3\xa9"),
    CONFIG_TEXT_VECTOR("U+07FF, the last of two bytes", "\xdf\xbf"),
    CONFIG_TEXT_VECTOR("U+0800, the first of three bytes", "\xe0\xa0\x80"),
    CONFIG_TEXT_VECTOR("U+2028, which C2 lets through", "\xe2\x80\xa8"),
    CONFIG_TEXT_VECTOR("U+D7FF, below the surrogates", "\xed\x9f\xbf"),
    CONFIG_TEXT_VECTOR("U+E000, above the surrogates", "\xee\x80\x80"),
    CONFIG_TEXT_VECTOR("U+FFFF", "\xef\xbf\xbf"),
    CONFIG_TEXT_VECTOR("U+10000, the first of four bytes", "\xf0\x90\x80\x80"),
    CONFIG_TEXT_VECTOR("U+10FFFF, the last code point", "\xf4\x8f\xbf\xbf"),
};

static const config_text_vector_t config_text_invalid_vectors[] = {
    CONFIG_TEXT_VECTOR("NUL at the start", "\0a"),
    CONFIG_TEXT_VECTOR("NUL in the middle", "a\0b"),
    CONFIG_TEXT_VECTOR("NUL at the end", "a\0"),
    CONFIG_TEXT_VECTOR("C0 U+0001", "a\x01"),
    CONFIG_TEXT_VECTOR("tab", "a\tb"),
    CONFIG_TEXT_VECTOR("line feed", "a\nb"),
    CONFIG_TEXT_VECTOR("carriage return", "a\rb"),
    CONFIG_TEXT_VECTOR("escape", "\x1b[2J"),
    CONFIG_TEXT_VECTOR("C0 U+001F", "\x1f"),
    CONFIG_TEXT_VECTOR("DEL", "\x7f"),
    CONFIG_TEXT_VECTOR("C1 U+0080", "\xc2\x80"),
    CONFIG_TEXT_VECTOR("C1 CSI U+009B", "\xc2\x9b"),
    CONFIG_TEXT_VECTOR("C1 U+009F", "\xc2\x9f"),
    CONFIG_TEXT_VECTOR("lone continuation", "\x80"),
    CONFIG_TEXT_VECTOR("lone high continuation", "\xbf"),
    CONFIG_TEXT_VECTOR("truncated two bytes", "a\xc3"),
    CONFIG_TEXT_VECTOR("truncated three bytes", "\xe2\x80"),
    CONFIG_TEXT_VECTOR("truncated four bytes", "\xf0\x90\x80"),
    CONFIG_TEXT_VECTOR("second byte not a continuation", "\xc3("),
    CONFIG_TEXT_VECTOR("third byte not a continuation", "\xe2\x80("),
    CONFIG_TEXT_VECTOR("fourth byte not a continuation", "\xf0\x90\x80("),
    CONFIG_TEXT_VECTOR("third byte above the continuations", "\xe2\x80\xff"),
    CONFIG_TEXT_VECTOR("third of four bytes above the continuations", "\xf0\x90\xc0\x80"),
    CONFIG_TEXT_VECTOR("fourth byte above the continuations", "\xf0\x90\x80\xff"),
    CONFIG_TEXT_VECTOR("overlong NUL", "\xc0\x80"),
    CONFIG_TEXT_VECTOR("overlong two bytes", "\xc1\xbf"),
    CONFIG_TEXT_VECTOR("overlong three bytes", "\xe0\x9f\xbf"),
    CONFIG_TEXT_VECTOR("overlong four bytes", "\xf0\x8f\xbf\xbf"),
    CONFIG_TEXT_VECTOR("first surrogate", "\xed\xa0\x80"),
    CONFIG_TEXT_VECTOR("last surrogate", "\xed\xbf\xbf"),
    CONFIG_TEXT_VECTOR("above U+10FFFF", "\xf4\x90\x80\x80"),
    CONFIG_TEXT_VECTOR("F5 lead byte", "\xf5\x80\x80\x80"),
    CONFIG_TEXT_VECTOR("FE", "\xfe"),
    CONFIG_TEXT_VECTOR("FF", "\xff"),
};

#define CONFIG_TEXT_VECTOR_COUNT(table) (sizeof(table) / sizeof((table)[0]))

#endif
