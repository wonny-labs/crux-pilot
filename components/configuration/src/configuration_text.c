/* The character rule, decision C2. It is a library apart from the mechanism, so that a service that
 * shows text can use the rule without needing a Configuration provider at link time. */
#include <crux/configuration.h>

/* One row for each lead byte range of well-formed UTF-8 (Unicode table 3-7), except that U+0080 to
 * U+009F are refused: they are the C1 controls, encoded as C2 80 to C2 9F. */
typedef struct {
    unsigned char lead_first, lead_last;
    unsigned char second_first, second_last;
    unsigned char continuations; /* After the second byte. */
} utf8_row_t;

static const utf8_row_t rows[] = {
    {0xC2, 0xC2, 0xA0, 0xBF, 0}, {0xC3, 0xDF, 0x80, 0xBF, 0}, {0xE0, 0xE0, 0xA0, 0xBF, 1},
    {0xE1, 0xEC, 0x80, 0xBF, 1}, {0xED, 0xED, 0x80, 0x9F, 1}, {0xEE, 0xEF, 0x80, 0xBF, 1},
    {0xF0, 0xF0, 0x90, 0xBF, 2}, {0xF1, 0xF3, 0x80, 0xBF, 2}, {0xF4, 0xF4, 0x80, 0x8F, 2},
};

static const utf8_row_t *row_of(unsigned char lead)
{
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        if (lead >= rows[i].lead_first && lead <= rows[i].lead_last) {
            return &rows[i];
        }
    }
    return NULL;
}

bool crux_config_text_valid(const char *text, size_t length)
{
    if (text == NULL) {
        return false;
    }
    for (size_t i = 0; i < length;) {
        unsigned char byte = (unsigned char)text[i];
        if (byte < 0x80) {
            if (byte < 0x20 || byte == 0x7F) {
                return false;
            }
            ++i;
            continue;
        }
        const utf8_row_t *row = row_of(byte);
        if (row == NULL || length - i < 2u + row->continuations) {
            return false;
        }
        unsigned char second = (unsigned char)text[i + 1];
        if (second < row->second_first || second > row->second_last) {
            return false;
        }
        for (size_t j = 0; j < row->continuations; ++j) {
            unsigned char next = (unsigned char)text[i + 2 + j];
            if (next < 0x80 || next > 0xBF) {
                return false;
            }
        }
        i += 2u + row->continuations;
    }
    return true;
}
