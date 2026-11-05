#include "tls_x509.h"

#include <string.h>

/* ---- Fixed encodings. */

static const uint8_t oid_ec_public_key[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const uint8_t oid_p256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const uint8_t oid_common_name[] = {0x55, 0x04, 0x03};

/* AlgorithmIdentifier ecdsa-with-SHA256, with no parameters (RFC 5758). */
static const uint8_t ecdsa_sha256[] = {0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86,
                                       0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
/* SubjectPublicKeyInfo up to the point: id-ecPublicKey, prime256v1, a 66-byte BIT STRING. */
static const uint8_t p256_key_prefix[] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
                                          0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                          0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};
static const uint8_t version_3[] = {0xa0, 0x03, 0x02, 0x01, 0x02};
/* Basic constraints, critical, not a CA. */
static const uint8_t ext_basic_constraints[] = {0x30, 0x0c, 0x06, 0x03, 0x55, 0x1d, 0x13,
                                                0x01, 0x01, 0xff, 0x04, 0x02, 0x30, 0x00};
/* Key usage, critical, digital signature. */
static const uint8_t ext_key_usage[] = {0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01,
                                        0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x07, 0x80};
/* Extended key usage, server authentication. */
static const uint8_t ext_server_auth[] = {0x30, 0x13, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04,
                                          0x0c, 0x30, 0x0a, 0x06, 0x08, 0x2b, 0x06, 0x01,
                                          0x05, 0x05, 0x07, 0x03, 0x01};
static const uint8_t oid_subject_alt_name[] = {0x06, 0x03, 0x55, 0x1d, 0x11};
static const uint8_t oid_extension_request[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                                0xf7, 0x0d, 0x01, 0x09, 0x0e};
/* PKCS #8 PrivateKeyInfo and RFC 5915 ECPrivateKey, around the scalar and the point. */
static const uint8_t pkcs8_head[] = {0x30, 0x81, 0x87, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06,
                                     0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
                                     0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,
                                     0x04, 0x6d, 0x30, 0x6b, 0x02, 0x01, 0x01, 0x04, 0x20};
static const uint8_t pkcs8_public_head[] = {0xa1, 0x44, 0x03, 0x42, 0x00};

#define TAG_BOOLEAN 0x01u
#define TAG_INTEGER 0x02u
#define TAG_BIT_STRING 0x03u
#define TAG_OCTET_STRING 0x04u
#define TAG_OID 0x06u
#define TAG_UTF8_STRING 0x0cu
#define TAG_UTC_TIME 0x17u
#define TAG_GENERALIZED_TIME 0x18u
#define TAG_SEQUENCE 0x30u
#define TAG_SET 0x31u
#define TAG_VERSION 0xa0u
#define TAG_ISSUER_UID 0x81u
#define TAG_SUBJECT_UID 0x82u
#define TAG_EXTENSIONS 0xa3u
#define TAG_SAN_DNS 0x82u
#define TAG_SAN_IP 0x87u

#define EXTENSIONS_MAX 32u
#define UTC_TIME_FIRST_YEAR 1950
#define UTC_TIME_LAST_YEAR 2049
#define SECONDS_PER_DAY 86400

/* ---- Time. */

static bool leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && leap(year) ? 29 : days[month - 1];
}

bool tls_x509_time_from_civil(int year, int month, int day, int hour, int minute, int second,
                              int64_t *out)
{
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month(year, month) || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return false;
    }
    /* Days from civil, after H. Hinnant's public domain algorithm, for years from 1. */
    int64_t y = year - (month <= 2 ? 1 : 0);
    int64_t era = y / 400;
    int64_t year_of_era = y - era * 400;
    int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    int64_t days = era * 146097 + day_of_era - 719468;
    *out = days * SECONDS_PER_DAY + hour * 3600 + minute * 60 + second;
    return true;
}

typedef struct {
    int year, month, day, hour, minute, second;
} civil_t;

/* For 0 to 9999-12-31T23:59:59 only, which the callers check. */
static civil_t civil_from_time(int64_t seconds)
{
    int64_t days = seconds / SECONDS_PER_DAY;
    int64_t rest = seconds % SECONDS_PER_DAY;
    int64_t z = days + 719468;
    int64_t era = z / 146097;
    int64_t day_of_era = z - era * 146097;
    int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t month_index = (5 * day_of_year + 2) / 153;
    civil_t civil;
    civil.day = (int)(day_of_year - (153 * month_index + 2) / 5 + 1);
    civil.month = (int)(month_index < 10 ? month_index + 3 : month_index - 9);
    civil.year = (int)(year_of_era + era * 400 + (civil.month <= 2 ? 1 : 0));
    civil.hour = (int)(rest / 3600);
    civil.minute = (int)(rest % 3600 / 60);
    civil.second = (int)(rest % 60);
    return civil;
}

/* ---- Reading. */

typedef struct {
    const uint8_t *p;
    size_t n;
} span_t;

/* One TLV with a low tag number and a definite, minimal length inside what remains. */
static bool der_next(span_t *in, uint8_t *tag, span_t *content, span_t *whole)
{
    if (in->n < 2u || (in->p[0] & 0x1fu) == 0x1fu) {
        return false;
    }
    size_t header = 2;
    size_t length = in->p[1];
    if (length == 0x81u) {
        if (in->n < 3u || in->p[2] < 0x80u) {
            return false;
        }
        length = in->p[2];
        header = 3;
    } else if (length == 0x82u) {
        if (in->n < 4u) {
            return false;
        }
        length = (size_t)in->p[2] << 8 | in->p[3];
        if (length < 0x100u) {
            return false;
        }
        header = 4;
    } else if (length >= 0x80u) {
        return false; /* Indefinite, or more length bytes than any certificate here needs. */
    }
    if (length > in->n - header) {
        return false;
    }
    *tag = in->p[0];
    content->p = in->p + header;
    content->n = length;
    if (whole != NULL) {
        whole->p = in->p;
        whole->n = header + length;
    }
    in->p += header + length;
    in->n -= header + length;
    return true;
}

static bool der_expect(span_t *in, uint8_t tag, span_t *content, span_t *whole)
{
    uint8_t found = 0;
    span_t saved = *in;
    if (!der_next(in, &found, content, whole) || found != tag) {
        *in = saved;
        return false;
    }
    return true;
}

static bool next_is(const span_t *in, uint8_t tag)
{
    return in->n > 0u && in->p[0] == tag;
}

static bool integer_minimal(span_t value)
{
    if (value.n == 0u) {
        return false;
    }
    if (value.n >= 2u && ((value.p[0] == 0x00u && value.p[1] < 0x80u) ||
                          (value.p[0] == 0xffu && value.p[1] >= 0x80u))) {
        return false;
    }
    return true;
}

static bool oid_well_formed(span_t oid)
{
    if (oid.n == 0u || (oid.p[oid.n - 1u] & 0x80u) != 0u) {
        return false;
    }
    bool start = true;
    for (size_t i = 0; i < oid.n; ++i) {
        if (start && oid.p[i] == 0x80u) {
            return false;
        }
        start = (oid.p[i] & 0x80u) == 0u;
    }
    return true;
}

static bool span_equals(span_t a, const uint8_t *b, size_t length)
{
    return a.n == length && memcmp(a.p, b, length) == 0;
}

/* AlgorithmIdentifier: an OID and at most one parameter element. */
static bool algorithm_well_formed(span_t content)
{
    uint8_t tag = 0;
    span_t oid, parameter;
    if (!der_expect(&content, TAG_OID, &oid, NULL) || !oid_well_formed(oid)) {
        return false;
    }
    if (content.n > 0u && !der_next(&content, &tag, &parameter, NULL)) {
        return false;
    }
    return content.n == 0u;
}

static void copy_shown(span_t value, char out[CRUX_TLS_CERTIFICATE_NAME_SIZE])
{
    size_t length = value.n < CRUX_TLS_CERTIFICATE_NAME_SIZE - 1u
                        ? value.n
                        : CRUX_TLS_CERTIFICATE_NAME_SIZE - 1u;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = value.p[i];
        out[i] = c >= 0x20u && c <= 0x7eu ? (char)c : '?';
    }
    out[length] = '\0';
}

/* Name: SETs of attribute type and value. Copies the first common name. */
static bool name_well_formed(span_t content, char out[CRUX_TLS_CERTIFICATE_NAME_SIZE])
{
    bool found = false;
    out[0] = '\0';
    while (content.n > 0u) {
        span_t set;
        if (!der_expect(&content, TAG_SET, &set, NULL) || set.n == 0u) {
            return false;
        }
        while (set.n > 0u) {
            span_t attribute, oid, value;
            uint8_t tag = 0;
            if (!der_expect(&set, TAG_SEQUENCE, &attribute, NULL) ||
                !der_expect(&attribute, TAG_OID, &oid, NULL) || !oid_well_formed(oid) ||
                !der_next(&attribute, &tag, &value, NULL) || attribute.n != 0u) {
                return false;
            }
            if (!found && span_equals(oid, oid_common_name, sizeof(oid_common_name))) {
                copy_shown(value, out);
                found = true;
            }
        }
    }
    return true;
}

static bool digits(const uint8_t *p, size_t count, int *out)
{
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
        if (p[i] < '0' || p[i] > '9') {
            return false;
        }
        value = value * 10 + (p[i] - '0');
    }
    *out = value;
    return true;
}

/* UTCTime YYMMDDHHMMSSZ, or GeneralizedTime YYYYMMDDHHMMSSZ: UTC, seconds, no fraction. */
static bool time_value(span_t *in, int64_t *out)
{
    uint8_t tag = 0;
    span_t value;
    if (!der_next(in, &tag, &value, NULL)) {
        return false;
    }
    size_t year_digits;
    if (tag == TAG_UTC_TIME && value.n == 13u) {
        year_digits = 2;
    } else if (tag == TAG_GENERALIZED_TIME && value.n == 15u) {
        year_digits = 4;
    } else {
        return false;
    }
    int year, month, day, hour, minute, second;
    const uint8_t *p = value.p;
    if (p[value.n - 1u] != 'Z' || !digits(p, year_digits, &year) ||
        !digits(p + year_digits, 2, &month) || !digits(p + year_digits + 2u, 2, &day) ||
        !digits(p + year_digits + 4u, 2, &hour) || !digits(p + year_digits + 6u, 2, &minute) ||
        !digits(p + year_digits + 8u, 2, &second)) {
        return false;
    }
    if (year_digits == 2u) {
        year += year < 50 ? 2000 : 1900;
    }
    return tls_x509_time_from_civil(year, month, day, hour, minute, second, out);
}

/* SubjectPublicKeyInfo. Sets *point when the key is an uncompressed P-256 point. */
static tls_x509_result_t key_info(span_t content, const uint8_t **point)
{
    span_t algorithm, oid, bits;
    if (!der_expect(&content, TAG_SEQUENCE, &algorithm, NULL) ||
        !der_expect(&algorithm, TAG_OID, &oid, NULL) || !oid_well_formed(oid)) {
        return TLS_X509_MALFORMED;
    }
    bool p256 = false;
    span_t curve;
    uint8_t tag = 0;
    if (algorithm.n > 0u && !der_next(&algorithm, &tag, &curve, NULL)) {
        return TLS_X509_MALFORMED;
    }
    if (algorithm.n != 0u) {
        return TLS_X509_MALFORMED;
    }
    if (span_equals(oid, oid_ec_public_key, sizeof(oid_ec_public_key)) && tag == TAG_OID &&
        span_equals(curve, oid_p256, sizeof(oid_p256))) {
        p256 = true;
    }
    if (!der_expect(&content, TAG_BIT_STRING, &bits, NULL) || bits.n == 0u || bits.p[0] != 0u ||
        content.n != 0u) {
        return TLS_X509_MALFORMED;
    }
    if (!p256 || bits.n != 1u + TLS_X509_P256_POINT_BYTES || bits.p[1] != 0x04u) {
        return TLS_X509_INVALID;
    }
    *point = bits.p + 1;
    return TLS_X509_OK;
}

/* Extensions: a non-empty list, each an OID, DER TRUE if critical, and an OCTET STRING. */
static tls_x509_result_t extensions(span_t content)
{
    span_t list;
    if (!der_expect(&content, TAG_SEQUENCE, &list, NULL) || content.n != 0u || list.n == 0u) {
        return TLS_X509_MALFORMED;
    }
    span_t seen[EXTENSIONS_MAX];
    size_t count = 0;
    bool invalid = false;
    while (list.n > 0u) {
        span_t extension, oid, critical, value;
        if (!der_expect(&list, TAG_SEQUENCE, &extension, NULL) ||
            !der_expect(&extension, TAG_OID, &oid, NULL) || !oid_well_formed(oid)) {
            return TLS_X509_MALFORMED;
        }
        if (next_is(&extension, TAG_BOOLEAN)) {
            if (!der_expect(&extension, TAG_BOOLEAN, &critical, NULL) || critical.n != 1u ||
                critical.p[0] != 0xffu) {
                return TLS_X509_MALFORMED;
            }
        }
        if (!der_expect(&extension, TAG_OCTET_STRING, &value, NULL) || extension.n != 0u) {
            return TLS_X509_MALFORMED;
        }
        if (count == EXTENSIONS_MAX) {
            invalid = true;
            continue;
        }
        for (size_t i = 0; i < count; ++i) {
            if (span_equals(seen[i], oid.p, oid.n)) {
                invalid = true;
            }
        }
        seen[count] = oid;
        count += 1u;
    }
    return invalid ? TLS_X509_INVALID : TLS_X509_OK;
}

static tls_x509_result_t worse(tls_x509_result_t a, tls_x509_result_t b)
{
    return a == TLS_X509_MALFORMED || b == TLS_X509_MALFORMED ? TLS_X509_MALFORMED
           : a == TLS_X509_INVALID || b == TLS_X509_INVALID   ? TLS_X509_INVALID
                                                              : TLS_X509_OK;
}

tls_x509_result_t tls_x509_parse(const uint8_t *der, size_t length, tls_x509_view_t *out)
{
    if (der == NULL || out == NULL || length == 0u || length > CRUX_TLS_CERTIFICATE_DER_MAX) {
        return TLS_X509_MALFORMED;
    }
    span_t in = {der, length};
    span_t certificate, tbs, outer_algorithm, outer_whole, signature;
    if (!der_expect(&in, TAG_SEQUENCE, &certificate, NULL) || in.n != 0u ||
        !der_expect(&certificate, TAG_SEQUENCE, &tbs, NULL) ||
        !der_expect(&certificate, TAG_SEQUENCE, &outer_algorithm, &outer_whole) ||
        !algorithm_well_formed(outer_algorithm) ||
        !der_expect(&certificate, TAG_BIT_STRING, &signature, NULL) || signature.n < 2u ||
        signature.p[0] != 0u || certificate.n != 0u) {
        return TLS_X509_MALFORMED;
    }
    /* Policy refusals are collected, so that a malformed certificate always reads as malformed. */
    tls_x509_result_t verdict = TLS_X509_INVALID;
    if (next_is(&tbs, TAG_VERSION)) {
        span_t version, number;
        if (!der_expect(&tbs, TAG_VERSION, &version, NULL) ||
            !der_expect(&version, TAG_INTEGER, &number, NULL) || version.n != 0u ||
            !integer_minimal(number)) {
            return TLS_X509_MALFORMED;
        }
        if (number.n == 1u && number.p[0] == 0u) {
            return TLS_X509_MALFORMED; /* DER leaves a default out. */
        }
        verdict = number.n == 1u && number.p[0] == 2u ? TLS_X509_OK : TLS_X509_INVALID;
    }
    span_t serial;
    if (!der_expect(&tbs, TAG_INTEGER, &serial, NULL) || !integer_minimal(serial)) {
        return TLS_X509_MALFORMED;
    }
    if (serial.n > 21u || (serial.n == 21u && serial.p[0] != 0u)) {
        verdict = TLS_X509_INVALID; /* RFC 5280 allows 20 octets. */
    }
    span_t inner_algorithm, inner_whole, issuer, validity, subject, key;
    tls_x509_view_t view;
    if (!der_expect(&tbs, TAG_SEQUENCE, &inner_algorithm, &inner_whole) ||
        !algorithm_well_formed(inner_algorithm) ||
        !der_expect(&tbs, TAG_SEQUENCE, &issuer, NULL) || !name_well_formed(issuer, view.issuer) ||
        !der_expect(&tbs, TAG_SEQUENCE, &validity, NULL) ||
        !time_value(&validity, &view.not_before_s) || !time_value(&validity, &view.not_after_s) ||
        validity.n != 0u || !der_expect(&tbs, TAG_SEQUENCE, &subject, NULL) ||
        !name_well_formed(subject, view.subject) || !der_expect(&tbs, TAG_SEQUENCE, &key, NULL)) {
        return TLS_X509_MALFORMED;
    }
    if (!span_equals(inner_whole, outer_whole.p, outer_whole.n) ||
        view.not_after_s < view.not_before_s) {
        verdict = TLS_X509_INVALID;
    }
    verdict = worse(verdict, key_info(key, &view.public_key));
    if (verdict == TLS_X509_MALFORMED) {
        return verdict;
    }
    static const uint8_t optional_tags[] = {TAG_ISSUER_UID, TAG_SUBJECT_UID};
    for (size_t i = 0; i < sizeof(optional_tags); ++i) {
        span_t unique;
        if (next_is(&tbs, optional_tags[i]) &&
            (!der_expect(&tbs, optional_tags[i], &unique, NULL) || unique.n == 0u ||
             unique.p[0] > 7u)) {
            return TLS_X509_MALFORMED;
        }
    }
    if (next_is(&tbs, TAG_EXTENSIONS)) {
        span_t list;
        if (!der_expect(&tbs, TAG_EXTENSIONS, &list, NULL)) {
            return TLS_X509_MALFORMED;
        }
        verdict = worse(verdict, extensions(list));
    }
    if (tbs.n != 0u) {
        return TLS_X509_MALFORMED;
    }
    if (verdict != TLS_X509_OK) {
        return verdict;
    }
    view.no_expiry = view.not_after_s == TLS_X509_TIME_MAX_S;
    *out = view;
    return TLS_X509_OK;
}

/* ---- Names. */

static bool common_name_valid(const char *text)
{
    if (text == NULL) {
        return false;
    }
    size_t length = strlen(text);
    if (length == 0u || length > CRUX_TLS_CERTIFICATE_NAME_SIZE - 1u) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == ' ' || c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool dns_name_valid(const char *text)
{
    if (text == NULL) {
        return false;
    }
    size_t length = strlen(text);
    if (length == 0u || length > CRUX_TLS_CERTIFICATE_DNS_NAME_MAX) {
        return false;
    }
    size_t at = 0;
    if (length >= 2u && text[0] == '*' && text[1] == '.') {
        at = 2;
        if (at == length) {
            return false;
        }
    }
    size_t label = 0;
    for (; at <= length; ++at) {
        char c = at < length ? text[at] : '.';
        if (c == '.') {
            if (label == 0u || label > 63u || text[at - 1u] == '-') {
                return false;
            }
            label = 0;
            continue;
        }
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  (c == '-' && label > 0u);
        if (!ok) {
            return false;
        }
        label += 1u;
    }
    return true;
}

static size_t alt_name_count(const crux_tls_certificate_names_t *names)
{
    return names->dns_count + names->ipv4_count + names->ipv6_count;
}

bool tls_x509_names_valid(const crux_tls_certificate_names_t *names)
{
    if (names == NULL || !common_name_valid(names->common_name) ||
        names->dns_count > CRUX_TLS_CERTIFICATE_ALT_NAMES_MAX ||
        names->ipv4_count > CRUX_TLS_CERTIFICATE_ALT_NAMES_MAX ||
        names->ipv6_count > CRUX_TLS_CERTIFICATE_ALT_NAMES_MAX ||
        alt_name_count(names) > CRUX_TLS_CERTIFICATE_ALT_NAMES_MAX ||
        (names->dns_count > 0u && names->dns_names == NULL) ||
        (names->ipv4_count > 0u && names->ipv4 == NULL) ||
        (names->ipv6_count > 0u && names->ipv6 == NULL)) {
        return false;
    }
    for (size_t i = 0; i < names->dns_count; ++i) {
        if (!dns_name_valid(names->dns_names[i])) {
            return false;
        }
    }
    return true;
}

/* ---- Writing. Each element reserves three length bytes and is made minimal when it closes, so
 * a build needs two bytes of working room per open element beyond its result. */

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} writer_t;

static void put(writer_t *w, const void *data, size_t length)
{
    if (w->overflow || length > w->capacity - w->length) {
        w->overflow = true;
        return;
    }
    memcpy(w->buffer + w->length, data, length);
    w->length += length;
}

static void put_byte(writer_t *w, uint8_t value)
{
    put(w, &value, 1);
}

/* Returns where the reserved length starts. */
static size_t w_open(writer_t *w, uint8_t tag)
{
    static const uint8_t reserved[3] = {0};
    put_byte(w, tag);
    size_t mark = w->length;
    put(w, reserved, sizeof(reserved));
    return mark;
}

static void w_close(writer_t *w, size_t mark)
{
    if (w->overflow) {
        return;
    }
    size_t content = w->length - mark - 3u;
    uint8_t header[3];
    size_t header_length;
    if (content < 0x80u) {
        header[0] = (uint8_t)content;
        header_length = 1;
    } else if (content < 0x100u) {
        header[0] = 0x81;
        header[1] = (uint8_t)content;
        header_length = 2;
    } else {
        header[0] = 0x82;
        header[1] = (uint8_t)(content >> 8);
        header[2] = (uint8_t)content;
        header_length = 3;
    }
    memmove(w->buffer + mark + header_length, w->buffer + mark + 3u, content);
    memcpy(w->buffer + mark, header, header_length);
    w->length -= 3u - header_length;
}

static void put_tlv(writer_t *w, uint8_t tag, const void *data, size_t length)
{
    size_t mark = w_open(w, tag);
    put(w, data, length);
    w_close(w, mark);
}

static void put_name(writer_t *w, const char *common_name)
{
    size_t name = w_open(w, TAG_SEQUENCE);
    size_t set = w_open(w, TAG_SET);
    size_t attribute = w_open(w, TAG_SEQUENCE);
    put_tlv(w, TAG_OID, oid_common_name, sizeof(oid_common_name));
    put_tlv(w, TAG_UTF8_STRING, common_name, strlen(common_name));
    w_close(w, attribute);
    w_close(w, set);
    w_close(w, name);
}

static void put_two_digits(char *out, int value)
{
    out[0] = (char)('0' + value / 10);
    out[1] = (char)('0' + value % 10);
}

static void put_time(writer_t *w, int64_t seconds)
{
    civil_t c = civil_from_time(seconds);
    char text[15];
    size_t at = 0;
    bool utc = c.year >= UTC_TIME_FIRST_YEAR && c.year <= UTC_TIME_LAST_YEAR;
    if (!utc) {
        put_two_digits(text, c.year / 100);
        at = 2;
    }
    put_two_digits(text + at, c.year % 100);
    put_two_digits(text + at + 2u, c.month);
    put_two_digits(text + at + 4u, c.day);
    put_two_digits(text + at + 6u, c.hour);
    put_two_digits(text + at + 8u, c.minute);
    put_two_digits(text + at + 10u, c.second);
    text[at + 12u] = 'Z';
    put_tlv(w, utc ? TAG_UTC_TIME : TAG_GENERALIZED_TIME, text, at + 13u);
}

static void put_key(writer_t *w, const uint8_t public_key[TLS_X509_P256_POINT_BYTES])
{
    put(w, p256_key_prefix, sizeof(p256_key_prefix));
    put(w, public_key, TLS_X509_P256_POINT_BYTES);
}

static void put_alt_names(writer_t *w, const crux_tls_certificate_names_t *names)
{
    size_t extension = w_open(w, TAG_SEQUENCE);
    put(w, oid_subject_alt_name, sizeof(oid_subject_alt_name));
    size_t value = w_open(w, TAG_OCTET_STRING);
    size_t list = w_open(w, TAG_SEQUENCE);
    for (size_t i = 0; i < names->dns_count; ++i) {
        put_tlv(w, TAG_SAN_DNS, names->dns_names[i], strlen(names->dns_names[i]));
    }
    for (size_t i = 0; i < names->ipv4_count; ++i) {
        put_tlv(w, TAG_SAN_IP, names->ipv4[i], 4);
    }
    for (size_t i = 0; i < names->ipv6_count; ++i) {
        put_tlv(w, TAG_SAN_IP, names->ipv6[i], 16);
    }
    w_close(w, list);
    w_close(w, value);
    w_close(w, extension);
}

/* An unsigned big-endian value as a minimal DER INTEGER. */
static void put_unsigned(writer_t *w, const uint8_t *value, size_t length)
{
    while (length > 1u && value[0] == 0u) {
        ++value;
        --length;
    }
    size_t mark = w_open(w, TAG_INTEGER);
    if ((value[0] & 0x80u) != 0u) {
        put_byte(w, 0);
    }
    put(w, value, length);
    w_close(w, mark);
}

/* Hashes buffer[from, length), signs it, and appends the algorithm and the signature. */
static tls_x509_build_t put_signature(writer_t *w, size_t from, tls_x509_hash_t hash,
                                      tls_x509_sign_t sign, void *context)
{
    if (w->overflow) {
        return TLS_X509_NO_ROOM;
    }
    uint8_t digest[32];
    uint8_t signature[64];
    bool signed_ok = hash(w->buffer + from, w->length - from, digest) &&
                     sign(context, digest, signature);
    if (!signed_ok) {
        memset(signature, 0, sizeof(signature));
        return TLS_X509_NO_SIGNATURE;
    }
    put(w, ecdsa_sha256, sizeof(ecdsa_sha256));
    size_t bits = w_open(w, TAG_BIT_STRING);
    put_byte(w, 0);
    size_t value = w_open(w, TAG_SEQUENCE);
    put_unsigned(w, signature, 32);
    put_unsigned(w, signature + 32, 32);
    w_close(w, value);
    w_close(w, bits);
    memset(signature, 0, sizeof(signature));
    return w->overflow ? TLS_X509_NO_ROOM : TLS_X509_BUILT;
}

static tls_x509_build_t finish(writer_t *w, tls_x509_build_t result, size_t *length)
{
    if (result == TLS_X509_BUILT && w->overflow) {
        result = TLS_X509_NO_ROOM;
    }
    *length = result == TLS_X509_BUILT ? w->length : 0u;
    return result;
}

tls_x509_build_t tls_x509_build_self_signed(const tls_x509_self_signed_t *input,
                                            const uint8_t public_key[TLS_X509_P256_POINT_BYTES],
                                            tls_x509_hash_t hash, tls_x509_sign_t sign,
                                            void *context, uint8_t *out, size_t capacity,
                                            size_t *length)
{
    *length = 0;
    if (!tls_x509_names_valid(input->names) || input->not_before_s < 0 ||
        input->not_before_s > TLS_X509_TIME_MAX_S ||
        (int64_t)input->validity_days * SECONDS_PER_DAY >
            TLS_X509_TIME_MAX_S - input->not_before_s) {
        return TLS_X509_BAD_INPUT;
    }
    int64_t not_after = input->validity_days == 0u
                            ? TLS_X509_TIME_MAX_S
                            : input->not_before_s + (int64_t)input->validity_days * SECONDS_PER_DAY;
    uint8_t serial[TLS_X509_SERIAL_BYTES];
    memcpy(serial, input->serial, sizeof(serial));
    /* Positive, and always 16 bytes, so that no two regenerations share a short serial. */
    serial[0] = (uint8_t)((serial[0] & 0x7fu) | 0x40u);

    writer_t w = {out, capacity, 0, false};
    size_t certificate = w_open(&w, TAG_SEQUENCE);
    size_t tbs_from = w.length;
    size_t tbs = w_open(&w, TAG_SEQUENCE);
    put(&w, version_3, sizeof(version_3));
    put_tlv(&w, TAG_INTEGER, serial, sizeof(serial));
    put(&w, ecdsa_sha256, sizeof(ecdsa_sha256));
    put_name(&w, input->names->common_name);
    size_t validity = w_open(&w, TAG_SEQUENCE);
    put_time(&w, input->not_before_s);
    put_time(&w, not_after);
    w_close(&w, validity);
    put_name(&w, input->names->common_name);
    put_key(&w, public_key);
    size_t extensions_tag = w_open(&w, TAG_EXTENSIONS);
    size_t list = w_open(&w, TAG_SEQUENCE);
    put(&w, ext_basic_constraints, sizeof(ext_basic_constraints));
    put(&w, ext_key_usage, sizeof(ext_key_usage));
    put(&w, ext_server_auth, sizeof(ext_server_auth));
    if (alt_name_count(input->names) > 0u) {
        put_alt_names(&w, input->names);
    }
    w_close(&w, list);
    w_close(&w, extensions_tag);
    w_close(&w, tbs);
    tls_x509_build_t result = put_signature(&w, tbs_from, hash, sign, context);
    w_close(&w, certificate);
    return finish(&w, result, length);
}

tls_x509_build_t tls_x509_build_request(const crux_tls_certificate_names_t *names,
                                        const uint8_t public_key[TLS_X509_P256_POINT_BYTES],
                                        tls_x509_hash_t hash, tls_x509_sign_t sign, void *context,
                                        uint8_t *out, size_t capacity, size_t *length)
{
    *length = 0;
    if (!tls_x509_names_valid(names)) {
        return TLS_X509_BAD_INPUT;
    }
    writer_t w = {out, capacity, 0, false};
    size_t request = w_open(&w, TAG_SEQUENCE);
    size_t info_from = w.length;
    size_t info = w_open(&w, TAG_SEQUENCE);
    static const uint8_t version_1[] = {0x02, 0x01, 0x00};
    put(&w, version_1, sizeof(version_1));
    put_name(&w, names->common_name);
    put_key(&w, public_key);
    size_t attributes = w_open(&w, TAG_VERSION); /* [0] IMPLICIT SET OF Attribute. */
    if (alt_name_count(names) > 0u) {
        size_t attribute = w_open(&w, TAG_SEQUENCE);
        put(&w, oid_extension_request, sizeof(oid_extension_request));
        size_t values = w_open(&w, TAG_SET);
        size_t list = w_open(&w, TAG_SEQUENCE);
        put_alt_names(&w, names);
        w_close(&w, list);
        w_close(&w, values);
        w_close(&w, attribute);
    }
    w_close(&w, attributes);
    w_close(&w, info);
    tls_x509_build_t result = put_signature(&w, info_from, hash, sign, context);
    w_close(&w, request);
    return finish(&w, result, length);
}

tls_x509_build_t tls_x509_build_pkcs8(const uint8_t private_key[TLS_X509_P256_SCALAR_BYTES],
                                      const uint8_t public_key[TLS_X509_P256_POINT_BYTES],
                                      uint8_t *out, size_t capacity, size_t *length)
{
    writer_t w = {out, capacity, 0, false};
    put(&w, pkcs8_head, sizeof(pkcs8_head));
    put(&w, private_key, TLS_X509_P256_SCALAR_BYTES);
    put(&w, pkcs8_public_head, sizeof(pkcs8_public_head));
    put(&w, public_key, TLS_X509_P256_POINT_BYTES);
    if (w.overflow) {
        memset(out, 0, w.length);
    }
    return finish(&w, TLS_X509_BUILT, length);
}

void tls_x509_fingerprint_text(const uint8_t digest[32],
                               char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE])
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < 32u; ++i) {
        text[i * 3u] = hex[digest[i] >> 4];
        text[i * 3u + 1u] = hex[digest[i] & 0x0fu];
        text[i * 3u + 2u] = i == 31u ? '\0' : ':';
    }
}
