/*
 * The bounded DER parser and builders on their own: certificates made by OpenSSL, hand-made
 * variants of each refusal, every truncation, every flipped byte, seeded random mutation, and what
 * each builder writes.
 */
#include "fixtures.h"
#include "tls_test.h"
#include "tls_x509.h"

#include <stdlib.h>
#include <string.h>

/* ---- A small DER composer of the tests' own, so that variants do not reuse the module's writer.
 */

typedef struct {
    uint8_t b[4400];
    size_t n;
} buf_t;

static void put(buf_t *out, const void *data, size_t length)
{
    if (length == 0u) {
        return;
    }
    if (out->n + length > sizeof(out->b)) {
        abort();
    }
    memcpy(out->b + out->n, data, length);
    out->n += length;
}

static void put_byte(buf_t *out, uint8_t value)
{
    put(out, &value, 1);
}

static void put_length(buf_t *out, size_t length)
{
    if (length < 0x80u) {
        put_byte(out, (uint8_t)length);
    } else if (length < 0x100u) {
        put_byte(out, 0x81);
        put_byte(out, (uint8_t)length);
    } else {
        put_byte(out, 0x82);
        put_byte(out, (uint8_t)(length >> 8));
        put_byte(out, (uint8_t)length);
    }
}

static void tlv(buf_t *out, uint8_t tag, const void *content, size_t length)
{
    put_byte(out, tag);
    put_length(out, length);
    put(out, content, length);
}

static void wrap(buf_t *out, uint8_t tag, const buf_t *content)
{
    tlv(out, tag, content->b, content->n);
}

static const uint8_t oid_ecdsa_sha256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
static const uint8_t oid_ecdsa_sha384[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03};
static const uint8_t oid_ec_public_key[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const uint8_t oid_p256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const uint8_t oid_p384[] = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const uint8_t oid_cn[] = {0x55, 0x04, 0x03};
static const uint8_t oid_basic_constraints[] = {0x55, 0x1d, 0x13};

/* Every field of a certificate as a whole TLV, so that a case replaces one. */
typedef struct {
    buf_t version; /* The [0] TLV, or empty to leave it out. */
    buf_t serial;
    buf_t inner_algorithm;
    buf_t issuer;
    buf_t validity;
    buf_t subject;
    buf_t key;
    buf_t extensions; /* The [3] TLV, or empty. */
    buf_t tbs_extra;  /* Bytes after the extensions, inside the TBS. */
    buf_t outer_algorithm;
    buf_t signature;
    buf_t after; /* Bytes after the certificate. */
} parts_t;

static void algorithm(buf_t *out, const uint8_t *oid, size_t oid_length)
{
    buf_t inner = {0};
    tlv(&inner, 0x06, oid, oid_length);
    wrap(out, 0x30, &inner);
}

static void name(buf_t *out, uint8_t string_tag, const char *text)
{
    buf_t atv = {0}, set = {0}, seq = {0};
    tlv(&atv, 0x06, oid_cn, sizeof(oid_cn));
    tlv(&atv, string_tag, text, strlen(text));
    wrap(&set, 0x30, &atv);
    wrap(&seq, 0x31, &set);
    wrap(out, 0x30, &seq);
}

static void validity(buf_t *out, uint8_t tag_a, const char *a, uint8_t tag_b, const char *b)
{
    buf_t inner = {0};
    tlv(&inner, tag_a, a, strlen(a));
    tlv(&inner, tag_b, b, strlen(b));
    wrap(out, 0x30, &inner);
}

static void ec_key(buf_t *out, uint8_t unused_bits, const uint8_t *curve, size_t curve_length,
                   const uint8_t *point, size_t point_length)
{
    buf_t alg = {0}, alg_seq = {0}, bits = {0}, inner = {0};
    tlv(&alg, 0x06, oid_ec_public_key, sizeof(oid_ec_public_key));
    if (curve != NULL) {
        tlv(&alg, 0x06, curve, curve_length);
    }
    wrap(&alg_seq, 0x30, &alg);
    put_byte(&bits, unused_bits);
    put(&bits, point, point_length);
    put(&inner, alg_seq.b, alg_seq.n);
    wrap(&inner, 0x03, &bits);
    wrap(out, 0x30, &inner);
}

static void extension(buf_t *out, const uint8_t *oid, size_t oid_length, const uint8_t *critical,
                      size_t critical_length, const uint8_t *value, size_t value_length)
{
    buf_t inner = {0};
    tlv(&inner, 0x06, oid, oid_length);
    put(&inner, critical, critical_length);
    tlv(&inner, 0x04, value, value_length);
    wrap(out, 0x30, &inner);
}

static void extensions_of(buf_t *out, const buf_t *list)
{
    buf_t seq = {0};
    wrap(&seq, 0x30, list);
    wrap(out, 0xa3, &seq);
}

static const uint8_t critical_true[] = {0x01, 0x01, 0xff};
static const uint8_t basic_constraints_value[] = {0x30, 0x00};

static void parts_default(parts_t *p)
{
    memset(p, 0, sizeof(*p));
    static const uint8_t version[] = {0xa0, 0x03, 0x02, 0x01, 0x02};
    put(&p->version, version, sizeof(version));
    static const uint8_t serial[] = {0x02, 0x01, 0x01};
    put(&p->serial, serial, sizeof(serial));
    algorithm(&p->inner_algorithm, oid_ecdsa_sha256, sizeof(oid_ecdsa_sha256));
    name(&p->issuer, 0x0c, "issuer");
    validity(&p->validity, 0x17, "260101000000Z", 0x17, "270101000000Z");
    name(&p->subject, 0x0c, "subject");
    ec_key(&p->key, 0, oid_p256, sizeof(oid_p256), fixture_device_key, 65);
    buf_t list = {0};
    extension(&list, oid_basic_constraints, sizeof(oid_basic_constraints), critical_true,
              sizeof(critical_true), basic_constraints_value, sizeof(basic_constraints_value));
    extensions_of(&p->extensions, &list);
    algorithm(&p->outer_algorithm, oid_ecdsa_sha256, sizeof(oid_ecdsa_sha256));
    static const uint8_t signature[] = {0x03, 0x09, 0x00, 0x30, 0x06, 0x02,
                                        0x01, 0x01, 0x02, 0x01, 0x01};
    put(&p->signature, signature, sizeof(signature));
}

static size_t assemble(const parts_t *p, uint8_t *out)
{
    buf_t tbs = {0}, tbs_tlv = {0}, cert = {0}, all = {0};
    const buf_t *fields[] = {&p->version, &p->serial,     &p->inner_algorithm,
                             &p->issuer,  &p->validity,   &p->subject,
                             &p->key,     &p->extensions, &p->tbs_extra};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        put(&tbs, fields[i]->b, fields[i]->n);
    }
    wrap(&tbs_tlv, 0x30, &tbs);
    put(&cert, tbs_tlv.b, tbs_tlv.n);
    put(&cert, p->outer_algorithm.b, p->outer_algorithm.n);
    put(&cert, p->signature.b, p->signature.n);
    wrap(&all, 0x30, &cert);
    put(&all, p->after.b, p->after.n);
    memcpy(out, all.b, all.n);
    return all.n;
}

static uint8_t scratch[4400];

static tls_x509_result_t parse_parts(const parts_t *p)
{
    size_t length = assemble(p, scratch);
    tls_x509_view_t view;
    return tls_x509_parse(scratch, length, &view);
}

static void set(buf_t *field, const void *data, size_t length)
{
    field->n = 0;
    put(field, data, length);
}

static bool bytes_equal(const void *a, size_t length, const void *b)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    for (size_t i = 0; i < length; ++i) {
        if (x[i] != y[i]) {
            return false;
        }
    }
    return true;
}

/* ---- Certificates from OpenSSL. */

static void test_openssl_certificates(void)
{
    tls_x509_view_t view;
    CHECK(tls_x509_parse(fixture_ec_self, sizeof(fixture_ec_self), &view) == TLS_X509_OK);
    CHECK(memcmp(view.public_key, fixture_device_key, 65) == 0);
    CHECK(view.not_before_s == FIXTURE_EC_SELF_NOT_BEFORE);
    CHECK(view.not_after_s == FIXTURE_EC_SELF_NOT_AFTER);
    CHECK(!view.no_expiry);
    CHECK(strcmp(view.subject, "device.example") == 0);
    CHECK(strcmp(view.issuer, "device.example") == 0);

    CHECK(tls_x509_parse(fixture_ec_by_rsa, sizeof(fixture_ec_by_rsa), &view) == TLS_X509_OK);
    CHECK(memcmp(view.public_key, fixture_device_key, 65) == 0);
    CHECK(strcmp(view.issuer, "Example Test CA") == 0);
    CHECK(view.not_after_s == FIXTURE_EC_BY_RSA_NOT_AFTER);

    CHECK(tls_x509_parse(fixture_far, sizeof(fixture_far), &view) == TLS_X509_OK);
    CHECK(view.not_after_s == FIXTURE_FAR_NOT_AFTER);

    CHECK(tls_x509_parse(fixture_other, sizeof(fixture_other), &view) == TLS_X509_OK);
    CHECK(memcmp(view.public_key, fixture_device_key, 65) != 0);

    CHECK(tls_x509_parse(fixture_p384, sizeof(fixture_p384), &view) == TLS_X509_INVALID);
    CHECK(tls_x509_parse(fixture_rsa_self, sizeof(fixture_rsa_self), &view) == TLS_X509_INVALID);
    CHECK(tls_x509_parse(fixture_ec_self_pem, sizeof(fixture_ec_self_pem), &view) ==
          TLS_X509_MALFORMED);
    CHECK(tls_x509_parse(fixture_ec_self, 0, &view) == TLS_X509_MALFORMED);
}

static void test_output_untouched_on_refusal(void)
{
    tls_x509_view_t view;
    memset(&view, 0x5a, sizeof(view));
    tls_x509_view_t before = view;
    CHECK(tls_x509_parse(fixture_p384, sizeof(fixture_p384), &view) == TLS_X509_INVALID);
    CHECK(bytes_equal(&view, sizeof(view), &before));
    CHECK(tls_x509_parse(fixture_ec_self, sizeof(fixture_ec_self) - 1u, &view) ==
          TLS_X509_MALFORMED);
    CHECK(bytes_equal(&view, sizeof(view), &before));
}

/* ---- Hand-made variants. */

static void test_composer_baseline(void)
{
    parts_t p;
    parts_default(&p);
    CHECK(parse_parts(&p) == TLS_X509_OK);
    p.extensions.n = 0;
    CHECK(parse_parts(&p) == TLS_X509_OK);
}

static void test_version(void)
{
    parts_t p;
    parts_default(&p);
    p.version.n = 0;
    p.extensions.n = 0;
    CHECK(parse_parts(&p) == TLS_X509_INVALID); /* v1 */
    parts_default(&p);
    static const uint8_t v2[] = {0xa0, 0x03, 0x02, 0x01, 0x01};
    set(&p.version, v2, sizeof(v2));
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
    static const uint8_t v4[] = {0xa0, 0x03, 0x02, 0x01, 0x03};
    set(&p.version, v4, sizeof(v4));
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
    static const uint8_t two_byte[] = {0xa0, 0x04, 0x02, 0x02, 0x00, 0x02};
    set(&p.version, two_byte, sizeof(two_byte));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED); /* Not minimal. */
    static const uint8_t trailing[] = {0xa0, 0x05, 0x02, 0x01, 0x02, 0x05, 0x00};
    set(&p.version, trailing, sizeof(trailing));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t not_integer[] = {0xa0, 0x03, 0x04, 0x01, 0x02};
    set(&p.version, not_integer, sizeof(not_integer));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
}

static void test_serial(void)
{
    parts_t p;
    parts_default(&p);
    static const uint8_t empty[] = {0x02, 0x00};
    set(&p.serial, empty, sizeof(empty));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t padded[] = {0x02, 0x02, 0x00, 0x01};
    set(&p.serial, padded, sizeof(padded));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t negative_padded[] = {0x02, 0x02, 0xff, 0x80};
    set(&p.serial, negative_padded, sizeof(negative_padded));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    uint8_t long_serial[2 + 21] = {0x02, 21, 0x00};
    memset(long_serial + 3, 0x80, 20);
    set(&p.serial, long_serial, sizeof(long_serial));
    CHECK(parse_parts(&p) == TLS_X509_OK); /* 20 octets and a sign byte, as CAs write them. */
    uint8_t too_long[2 + 22] = {0x02, 22, 0x01};
    memset(too_long + 3, 0x80, 21);
    set(&p.serial, too_long, sizeof(too_long));
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
}

static void test_algorithms(void)
{
    parts_t p;
    parts_default(&p);
    p.outer_algorithm.n = 0;
    algorithm(&p.outer_algorithm, oid_ecdsa_sha384, sizeof(oid_ecdsa_sha384));
    CHECK(parse_parts(&p) == TLS_X509_INVALID); /* Inner and outer differ. */
    p.inner_algorithm.n = 0;
    algorithm(&p.inner_algorithm, oid_ecdsa_sha384, sizeof(oid_ecdsa_sha384));
    CHECK(parse_parts(&p) == TLS_X509_OK); /* Any algorithm: the signature is not checked. */
    static const uint8_t with_null[] = {0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                        0xce, 0x3d, 0x04, 0x03, 0x02, 0x05, 0x00};
    set(&p.inner_algorithm, with_null, sizeof(with_null));
    set(&p.outer_algorithm, with_null, sizeof(with_null));
    CHECK(parse_parts(&p) == TLS_X509_OK);
    static const uint8_t two_params[] = {0x30, 0x0e, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
                                         0x3d, 0x04, 0x03, 0x02, 0x05, 0x00, 0x05, 0x00};
    set(&p.inner_algorithm, two_params, sizeof(two_params));
    set(&p.outer_algorithm, two_params, sizeof(two_params));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t no_oid[] = {0x30, 0x02, 0x05, 0x00};
    set(&p.inner_algorithm, no_oid, sizeof(no_oid));
    set(&p.outer_algorithm, no_oid, sizeof(no_oid));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t bad_oid[] = {0x30, 0x04, 0x06, 0x02, 0x2a, 0x86};
    set(&p.inner_algorithm, bad_oid, sizeof(bad_oid));
    set(&p.outer_algorithm, bad_oid, sizeof(bad_oid));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED); /* The last subidentifier is unfinished. */
    static const uint8_t padded_oid[] = {0x30, 0x05, 0x06, 0x03, 0x2a, 0x80, 0x01};
    set(&p.inner_algorithm, padded_oid, sizeof(padded_oid));
    set(&p.outer_algorithm, padded_oid, sizeof(padded_oid));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
}

static void test_names(void)
{
    parts_t p;
    parts_default(&p);
    static const uint8_t empty_name[] = {0x30, 0x00};
    set(&p.subject, empty_name, sizeof(empty_name));
    CHECK(parse_parts(&p) == TLS_X509_OK); /* Allowed when names are in the extension. */
    static const uint8_t empty_set[] = {0x30, 0x02, 0x31, 0x00};
    set(&p.subject, empty_set, sizeof(empty_set));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t no_value[] = {0x30, 0x09, 0x31, 0x07, 0x30, 0x05,
                                       0x06, 0x03, 0x55, 0x04, 0x03};
    set(&p.subject, no_value, sizeof(no_value));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t extra_value[] = {0x30, 0x0e, 0x31, 0x0c, 0x30, 0x0a, 0x06, 0x03,
                                          0x55, 0x04, 0x03, 0x0c, 0x01, 0x61, 0x05, 0x00};
    set(&p.subject, extra_value, sizeof(extra_value));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t not_a_set[] = {0x30, 0x0c, 0x30, 0x0a, 0x30, 0x08, 0x06,
                                        0x03, 0x55, 0x04, 0x03, 0x0c, 0x01, 0x61};
    set(&p.subject, not_a_set, sizeof(not_a_set));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);

    parts_default(&p);
    p.subject.n = 0;
    name(&p.subject, 0x13, "Printable");
    tls_x509_view_t view;
    size_t length = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, length, &view) == TLS_X509_OK);
    CHECK(strcmp(view.subject, "Printable") == 0);
    CHECK(strcmp(view.issuer, "issuer") == 0);

    p.subject.n = 0;
    name(&p.subject, 0x0c, "caf\xc3\xa9\n<b>");
    length = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, length, &view) == TLS_X509_OK);
    CHECK(strcmp(view.subject, "caf??"
                               "?<b>") == 0);

    char long_name[200];
    memset(long_name, 'x', sizeof(long_name) - 1u);
    long_name[sizeof(long_name) - 1u] = '\0';
    p.subject.n = 0;
    name(&p.subject, 0x0c, long_name);
    length = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, length, &view) == TLS_X509_OK);
    CHECK(strlen(view.subject) == CRUX_TLS_CERTIFICATE_NAME_SIZE - 1u);
}

static void test_validity(void)
{
    parts_t p;
    tls_x509_view_t view;
    size_t length;
    parts_default(&p);
    length = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, length, &view) == TLS_X509_OK);
    CHECK(view.not_before_s == 1767225600);
    CHECK(view.not_after_s == 1767225600 + 365 * 86400);

    struct {
        const char *a;
        const char *b;
        tls_x509_result_t expected;
        uint8_t tag_a;
        uint8_t tag_b;
    } cases[] = {
        {"270101000000Z", "260101000000Z", TLS_X509_INVALID, 0x17, 0x17}, /* Reversed. */
        {"260101000000Z", "260101000000Z", TLS_X509_OK, 0x17, 0x17},      /* Equal. */
        {"500101000000Z", "491231235959Z", TLS_X509_OK, 0x17, 0x17},      /* 1950, and 2049. */
        {"20500101000000Z", "99991231235959Z", TLS_X509_OK, 0x18, 0x18},
        /* RFC 5280: a date through 2049 is a UTCTime, so a GeneralizedTime then is malformed. */
        {"20260101000000Z", "99991231235959Z", TLS_X509_MALFORMED, 0x18, 0x18},
        {"260101000000Z", "20491231235959Z", TLS_X509_MALFORMED, 0x17, 0x18},
        {"19490101000000Z", "260101000000Z", TLS_X509_OK, 0x18, 0x17},      /* Before 1950. */
        {"261301000000Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17}, /* Month 13. */
        {"260100000000Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17}, /* Day 0. */
        {"260229000000Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17}, /* Not a leap year. */
        {"280229000000Z", "290101000000Z", TLS_X509_OK, 0x17, 0x17},        /* A leap year. */
        {"260101240000Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17}, /* Hour 24. */
        {"260101006000Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17},
        {"260101000060Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17}, /* Leap second. */
        {"2601010000Z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17},   /* No seconds. */
        {"260101000000+0100", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17},
        {"26010100000OZ", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17},
        {"260101000000z", "270101000000Z", TLS_X509_MALFORMED, 0x17, 0x17},
        {"20260101000000.5Z", "270101000000Z", TLS_X509_MALFORMED, 0x18, 0x17},
        {"2026010100000Z", "270101000000Z", TLS_X509_MALFORMED, 0x18, 0x17},
        {"260101000000Z", "270101000000Z", TLS_X509_MALFORMED, 0x04, 0x17}, /* Not a time. */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        parts_default(&p);
        p.validity.n = 0;
        validity(&p.validity, cases[i].tag_a, cases[i].a, cases[i].tag_b, cases[i].b);
        tls_x509_result_t result = parse_parts(&p);
        if (result != cases[i].expected) {
            printf("validity case %zu: %d\n", i, (int)result);
        }
        CHECK(result == cases[i].expected);
    }

    parts_default(&p);
    p.validity.n = 0;
    validity(&p.validity, 0x17, "260101000000Z", 0x18, "99991231235959Z");
    length = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, length, &view) == TLS_X509_OK);
    CHECK(view.no_expiry);
    CHECK(view.not_after_s == 253402300799);

    parts_default(&p);
    p.validity.n = 0;
    validity(&p.validity, 0x17, "500101000000Z", 0x17, "491231235959Z");
    length = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, length, &view) == TLS_X509_OK);
    CHECK(view.not_before_s == -631152000); /* 1950-01-01. */
    CHECK(view.not_after_s == 2524607999);  /* 2049-12-31T23:59:59. */
}

static void test_keys(void)
{
    parts_t p;
    parts_default(&p);
    p.key.n = 0;
    ec_key(&p.key, 0, oid_p384, sizeof(oid_p384), fixture_device_key, 65);
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
    p.key.n = 0;
    ec_key(&p.key, 0, NULL, 0, fixture_device_key, 65);
    CHECK(parse_parts(&p) == TLS_X509_INVALID); /* No named curve. */
    p.key.n = 0;
    uint8_t compressed[33];
    memcpy(compressed, fixture_device_key, 33);
    compressed[0] = 0x02;
    ec_key(&p.key, 0, oid_p256, sizeof(oid_p256), compressed, sizeof(compressed));
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
    p.key.n = 0;
    uint8_t not_uncompressed[65];
    memcpy(not_uncompressed, fixture_device_key, 65);
    not_uncompressed[0] = 0x06;
    ec_key(&p.key, 0, oid_p256, sizeof(oid_p256), not_uncompressed, 65);
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
    p.key.n = 0;
    ec_key(&p.key, 0, oid_p256, sizeof(oid_p256), fixture_device_key, 64);
    CHECK(parse_parts(&p) == TLS_X509_INVALID);
    p.key.n = 0;
    ec_key(&p.key, 1, oid_p256, sizeof(oid_p256), fixture_device_key, 65);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED); /* Unused bits in a key. */
    p.key.n = 0;
    ec_key(&p.key, 8, oid_p256, sizeof(oid_p256), fixture_device_key, 65);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
}

static void test_extensions(void)
{
    parts_t p;
    buf_t list = {0};
    parts_default(&p);

    p.extensions.n = 0;
    list.n = 0;
    static const uint8_t critical_one[] = {0x01, 0x01, 0x01};
    extension(&list, oid_basic_constraints, 3, critical_one, 3, basic_constraints_value, 2);
    extensions_of(&p.extensions, &list);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED); /* TRUE is 0xff in DER. */

    p.extensions.n = 0;
    list.n = 0;
    static const uint8_t critical_false[] = {0x01, 0x01, 0x00};
    extension(&list, oid_basic_constraints, 3, critical_false, 3, basic_constraints_value, 2);
    extensions_of(&p.extensions, &list);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED); /* A default is left out in DER. */

    p.extensions.n = 0;
    list.n = 0;
    extension(&list, oid_basic_constraints, 3, NULL, 0, basic_constraints_value, 2);
    extension(&list, oid_basic_constraints, 3, NULL, 0, basic_constraints_value, 2);
    extensions_of(&p.extensions, &list);
    CHECK(parse_parts(&p) == TLS_X509_INVALID); /* The same extension twice. */

    p.extensions.n = 0;
    list.n = 0;
    extensions_of(&p.extensions, &list);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED); /* An empty list. */

    p.extensions.n = 0;
    list.n = 0;
    static const uint8_t no_value[] = {0x30, 0x05, 0x06, 0x03, 0x55, 0x1d, 0x13};
    put(&list, no_value, sizeof(no_value));
    extensions_of(&p.extensions, &list);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);

    p.extensions.n = 0;
    list.n = 0;
    static const uint8_t unknown_critical[] = {0x2a, 0x03, 0x04};
    extension(&list, unknown_critical, 3, critical_true, 3, basic_constraints_value, 2);
    extensions_of(&p.extensions, &list);
    CHECK(parse_parts(&p) == TLS_X509_INVALID); /* A client would refuse it, so the device does. */

    /* Unique identifiers before the extensions are well formed and skipped. */
    parts_default(&p);
    static const uint8_t unique_ids[] = {0x81, 0x02, 0x00, 0x01, 0x82, 0x02, 0x00, 0x02};
    buf_t ext = p.extensions;
    set(&p.extensions, unique_ids, sizeof(unique_ids));
    put(&p.extensions, ext.b, ext.n);
    CHECK(parse_parts(&p) == TLS_X509_OK);

    parts_default(&p);
    static const uint8_t extra[] = {0x05, 0x00};
    set(&p.tbs_extra, extra, sizeof(extra));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
}

static void test_signature_and_framing(void)
{
    parts_t p;
    parts_default(&p);
    static const uint8_t empty_bits[] = {0x03, 0x00};
    set(&p.signature, empty_bits, sizeof(empty_bits));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t unused[] = {0x03, 0x02, 0x01, 0x00};
    set(&p.signature, unused, sizeof(unused));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    static const uint8_t octets[] = {0x04, 0x01, 0x00};
    set(&p.signature, octets, sizeof(octets));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);

    parts_default(&p);
    static const uint8_t trailing[] = {0x00};
    set(&p.after, trailing, sizeof(trailing));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    /* Two certificates back to back are not one. */
    parts_default(&p);
    size_t one = assemble(&p, scratch);
    memcpy(scratch + one, scratch, one);
    tls_x509_view_t view;
    CHECK(tls_x509_parse(scratch, one * 2u, &view) == TLS_X509_MALFORMED);
    CHECK(tls_x509_parse(scratch, one, &view) == TLS_X509_OK);
}

static void test_length_encodings(void)
{
    tls_x509_view_t view;
    size_t length = sizeof(fixture_ec_self);
    /* fixture_ec_self starts 30 82 hi lo. */
    CHECK(fixture_ec_self[0] == 0x30 && fixture_ec_self[1] == 0x82);
    uint8_t copy[sizeof(fixture_ec_self) + 2];

    /* Indefinite length. */
    memcpy(copy, fixture_ec_self, length);
    copy[1] = 0x80;
    CHECK(tls_x509_parse(copy, length, &view) == TLS_X509_MALFORMED);
    /* Three length bytes for the same value. */
    copy[0] = 0x30;
    copy[1] = 0x83;
    copy[2] = 0x00;
    memcpy(copy + 3, fixture_ec_self + 2, length - 2u);
    CHECK(tls_x509_parse(copy, length + 1u, &view) == TLS_X509_MALFORMED);
    /* A long form for a value that fits in the short one, on the inner TBS. */
    parts_t p;
    parts_default(&p);
    size_t n = assemble(&p, scratch);
    CHECK(tls_x509_parse(scratch, n, &view) == TLS_X509_OK);
    /* Serial written 02 81 01 01. */
    static const uint8_t serial_long_form[] = {0x02, 0x81, 0x01, 0x01};
    set(&p.serial, serial_long_form, sizeof(serial_long_form));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    /* Two length bytes for a value that fits in one, or in the short form. */
    static const uint8_t serial_two_bytes[] = {0x02, 0x82, 0x00, 0x01, 0x01};
    set(&p.serial, serial_two_bytes, sizeof(serial_two_bytes));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    uint8_t serial_two_bytes_long[4 + 0x90] = {0x02, 0x82, 0x00, 0x90, 0x01};
    set(&p.serial, serial_two_bytes_long, sizeof(serial_two_bytes_long));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    serial_two_bytes_long[1] = 0x81;
    serial_two_bytes_long[2] = 0x90;
    serial_two_bytes_long[3] = 0x01;
    set(&p.serial, serial_two_bytes_long, 3 + 0x90);
    CHECK(parse_parts(&p) == TLS_X509_INVALID); /* The same, minimal: well formed, too long. */
    parts_default(&p);
    /* A high tag number form. */
    parts_default(&p);
    static const uint8_t high_tag[] = {0x1f, 0x02, 0x01, 0x01};
    set(&p.serial, high_tag, sizeof(high_tag));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    /* A length beyond the input, on the outer element. */
    memcpy(copy, fixture_ec_self, length);
    copy[3] = (uint8_t)(copy[3] + 1u);
    CHECK(tls_x509_parse(copy, length, &view) == TLS_X509_MALFORMED);
    /* Larger than the device holds, even when well formed. */
    static uint8_t big[4400];
    parts_default(&p);
    p.subject.n = 0;
    char long_name[CRUX_TLS_CERTIFICATE_DER_MAX];
    memset(long_name, 'n', sizeof(long_name) - 1u);
    long_name[sizeof(long_name) - 1u] = '\0';
    name(&p.subject, 0x0c, long_name);
    n = assemble(&p, big);
    CHECK(n > CRUX_TLS_CERTIFICATE_DER_MAX);
    CHECK(tls_x509_parse(big, n, &view) == TLS_X509_MALFORMED);
}

/* ---- Review of d7e86bd: values the parser took that OpenSSL refuses. */

static void expect_byte_edit(size_t at, uint8_t was, uint8_t now)
{
    uint8_t copy[sizeof(fixture_ec_self)];
    memcpy(copy, fixture_ec_self, sizeof(copy));
    CHECK(copy[at] == was);
    copy[at] = now;
    tls_x509_view_t view;
    tls_x509_result_t result = tls_x509_parse(copy, sizeof(copy), &view);
    if (result != TLS_X509_MALFORMED) {
        printf("byte %zu 0x%02x -> 0x%02x: %d\n", at, was, now, (int)result);
    }
    CHECK(result == TLS_X509_MALFORMED);
}

static void test_review_byte_edits(void)
{
    expect_byte_edit(58, 0x0c, 0x00);  /* Issuer O value with universal tag 0. */
    expect_byte_edit(140, 0x6d, 0xff); /* Not UTF-8, in the subject's UTF8String. */
    expect_byte_edit(135, 0x0c, 0x83); /* A context tag where a DirectoryString belongs. */
}

static tls_x509_result_t subject_value(uint8_t tag, const void *value, size_t length)
{
    parts_t p;
    parts_default(&p);
    buf_t atv = {0}, set = {0}, seq = {0};
    tlv(&atv, 0x06, oid_cn, sizeof(oid_cn));
    tlv(&atv, tag, value, length);
    wrap(&set, 0x30, &atv);
    wrap(&seq, 0x31, &set);
    p.subject.n = 0;
    wrap(&p.subject, 0x30, &seq);
    return parse_parts(&p);
}

static void test_string_values(void)
{
    struct {
        const char *value;
        size_t length;
        tls_x509_result_t expected;
        uint8_t tag;
    } cases[] = {
        {"plain", 5, TLS_X509_OK, 0x0c},
        {"caf\xc3\xa9", 5, TLS_X509_OK, 0x0c},
        {"\xf0\x9f\x94\x92", 4, TLS_X509_OK, 0x0c}, /* U+1F512. */
        {"\xff", 1, TLS_X509_MALFORMED, 0x0c},
        {"\xc0\x80", 2, TLS_X509_MALFORMED, 0x0c},         /* Overlong. */
        {"\xe0\x80\x80", 3, TLS_X509_MALFORMED, 0x0c},     /* Overlong. */
        {"\xed\xa0\x80", 3, TLS_X509_MALFORMED, 0x0c},     /* A surrogate. */
        {"\xf4\x90\x80\x80", 4, TLS_X509_MALFORMED, 0x0c}, /* Above U+10FFFF. */
        {"\xe2\x82", 2, TLS_X509_MALFORMED, 0x0c},         /* Cut short. */
        {"a\x80", 2, TLS_X509_MALFORMED, 0x0c},            /* A lone continuation. */
        {"AZaz09 '()+,-./:=?", 18, TLS_X509_OK, 0x13},
        {"a_b", 3, TLS_X509_MALFORMED, 0x13},
        {"a*b", 3, TLS_X509_MALFORMED, 0x13},
        {"user@example", 12, TLS_X509_OK, 0x16},
        {"\x80", 1, TLS_X509_MALFORMED, 0x16},
        {"0123 45", 7, TLS_X509_OK, 0x12},
        {"12a", 3, TLS_X509_MALFORMED, 0x12},
        {"\xfe\x01", 2, TLS_X509_OK, 0x14}, /* TeletexString is not checked. */
        {"\x00\x41", 2, TLS_X509_OK, 0x1e},
        {"\x00\x41\x00", 3, TLS_X509_MALFORMED, 0x1e},
        {"\xd8\x00", 2, TLS_X509_MALFORMED, 0x1e}, /* A surrogate. */
        {"\x00\x00\x00\x41", 4, TLS_X509_OK, 0x1c},
        {"\x00\x00\x41", 3, TLS_X509_MALFORMED, 0x1c},
        {"\x00\x11\x00\x00", 4, TLS_X509_MALFORMED, 0x1c}, /* Above U+10FFFF. */
        {"abc", 3, TLS_X509_MALFORMED, 0x00},
        {"abc", 3, TLS_X509_MALFORMED, 0x04},
        {"abc", 3, TLS_X509_MALFORMED, 0x83},
        {"", 0, TLS_X509_MALFORMED, 0x30},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        tls_x509_result_t result = subject_value(cases[i].tag, cases[i].value, cases[i].length);
        if (result != cases[i].expected) {
            printf("string case %zu: %d\n", i, (int)result);
        }
        CHECK(result == cases[i].expected);
    }
}

static void test_universal_tag_zero(void)
{
    parts_t p;
    parts_default(&p);
    static const uint8_t zero[] = {0x00, 0x00};
    set(&p.tbs_extra, zero, sizeof(zero));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
    parts_default(&p);
    static const uint8_t null_parameter[] = {0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                             0xce, 0x3d, 0x04, 0x03, 0x02, 0x00, 0x00};
    set(&p.inner_algorithm, null_parameter, sizeof(null_parameter));
    set(&p.outer_algorithm, null_parameter, sizeof(null_parameter));
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
}

static tls_x509_result_t with_extension(const uint8_t *oid, size_t oid_length, bool critical,
                                        const uint8_t *value, size_t value_length)
{
    parts_t p;
    parts_default(&p);
    buf_t list = {0};
    extension(&list, oid, oid_length, critical ? critical_true : NULL, critical ? 3u : 0u, value,
              value_length);
    p.extensions.n = 0;
    extensions_of(&p.extensions, &list);
    return parse_parts(&p);
}

static void test_extension_values(void)
{
    static const uint8_t bc[] = {0x55, 0x1d, 0x13}, ku[] = {0x55, 0x1d, 0x0f},
                         eku[] = {0x55, 0x1d, 0x25}, san[] = {0x55, 0x1d, 0x11},
                         ski[] = {0x55, 0x1d, 0x0e}, other[] = {0x2a, 0x03, 0x04};
    struct {
        const uint8_t *oid;
        const char *value;
        size_t length;
        bool critical;
        tls_x509_result_t expected;
    } cases[] = {
        {bc, "\x30\x00", 2, true, TLS_X509_OK},
        {bc, "\x30\x03\x01\x01\xff", 5, true, TLS_X509_OK},
        {bc, "\x30\x06\x01\x01\xff\x02\x01\x00", 8, true, TLS_X509_OK},
        {bc, "\x30\x03\x01\x01\x00", 5, true, TLS_X509_MALFORMED}, /* An explicit default. */
        {bc, "\x30\x03\x02\x01\xff", 5, true, TLS_X509_MALFORMED}, /* A negative length. */
        {bc, "\x04\x00", 2, true, TLS_X509_MALFORMED},
        {bc, "\x30\x00\x00", 3, true, TLS_X509_MALFORMED}, /* After the value. */
        {ku, "\x03\x02\x07\x80", 4, true, TLS_X509_OK},
        {ku, "\x03\x02\x08\x80", 4, true, TLS_X509_MALFORMED},
        {ku, "\x03\x01\x00", 3, true, TLS_X509_MALFORMED}, /* No bits. */
        {ku, "\x04\x01\x80", 3, true, TLS_X509_MALFORMED},
        {eku, "\x30\x0a\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x01", 12, false, TLS_X509_OK},
        {eku, "\x30\x00", 2, false, TLS_X509_MALFORMED},
        {eku, "\x30\x03\x02\x01\x01", 5, false, TLS_X509_MALFORMED},
        {san, "\x30\x03\x82\x01\x61", 5, false, TLS_X509_OK},
        {san, "\x30\x06\x87\x04\xc0\x00\x02\x0a", 8, false, TLS_X509_OK},
        {san, "\x30\x00", 2, false, TLS_X509_MALFORMED},
        {san, "\x30\x05\x87\x03\xc0\x00\x02", 7, false, TLS_X509_MALFORMED}, /* 3-byte IP. */
        {san, "\x30\x03\x82\x01\x80", 5, false, TLS_X509_MALFORMED},         /* Not IA5. */
        {san, "\x30\x03\x16\x01\x61", 5, false, TLS_X509_MALFORMED}, /* Not a GeneralName. */
        {san, "\x30\x04\xa4\x02\x30\x00", 6, false, TLS_X509_OK},    /* An empty Name. */
        {san, "\x30\x02\xa4\x00", 4, false, TLS_X509_MALFORMED},     /* No Name. */
        {san, "\x30\x04\xa4\x02\x31\x00", 6, false, TLS_X509_MALFORMED},
        {ski, "\x04\x02\x01\x02", 4, false, TLS_X509_OK},
        {ski, "\x30\x00", 2, false, TLS_X509_MALFORMED},
        {other, "\xff\xff", 2, false, TLS_X509_OK}, /* An unknown value is not parsed. */
        {other, "\xff\xff", 2, true, TLS_X509_INVALID},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        tls_x509_result_t result = with_extension(cases[i].oid, 3, cases[i].critical,
                                                  (const uint8_t *)cases[i].value, cases[i].length);
        if (result != cases[i].expected) {
            printf("extension case %zu: %d\n", i, (int)result);
        }
        CHECK(result == cases[i].expected);
    }
}

static tls_x509_result_t with_extension_count(size_t count)
{
    parts_t p;
    parts_default(&p);
    buf_t list = {0};
    for (size_t i = 0; i < count; ++i) {
        const uint8_t oid[] = {0x2a, 0x03, (uint8_t)(i / 100u + 1u), (uint8_t)(i % 100u + 1u)};
        extension(&list, oid, sizeof(oid), NULL, 0, basic_constraints_value, 2);
    }
    p.extensions.n = 0;
    extensions_of(&p.extensions, &list);
    return parse_parts(&p);
}

static void test_extension_count(void)
{
    CHECK(with_extension_count(32) == TLS_X509_OK);
    CHECK(with_extension_count(33) == TLS_X509_INVALID);
    CHECK(with_extension_count(120) == TLS_X509_INVALID);
}

static void test_key_info_and_unique_ids(void)
{
    parts_t p;
    parts_default(&p);
    static const uint8_t extra[] = {0x05, 0x00};
    buf_t inner = {0};
    /* The key's content again, with an element after the BIT STRING. */
    put(&inner, p.key.b + 2, p.key.n - 2u);
    put(&inner, extra, sizeof(extra));
    p.key.n = 0;
    wrap(&p.key, 0x30, &inner);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);

    parts_default(&p);
    static const uint8_t unique_ids[] = {0x81, 0x02, 0x08, 0x01};
    buf_t ext = p.extensions;
    set(&p.extensions, unique_ids, sizeof(unique_ids));
    put(&p.extensions, ext.b, ext.n);
    CHECK(parse_parts(&p) == TLS_X509_MALFORMED);
}

/* ---- Every truncation, every flipped byte, and random mutation. */

static void check_sane(tls_x509_result_t result, const uint8_t *input, size_t length,
                       const tls_x509_view_t *view)
{
    CHECK(result == TLS_X509_OK || result == TLS_X509_MALFORMED || result == TLS_X509_INVALID);
    if (result == TLS_X509_OK) {
        CHECK(view->public_key >= input && view->public_key + 65 <= input + length);
        CHECK(view->public_key[0] == 0x04);
        CHECK(memchr(view->subject, '\0', sizeof(view->subject)) != NULL);
        CHECK(memchr(view->issuer, '\0', sizeof(view->issuer)) != NULL);
        CHECK(view->not_before_s <= view->not_after_s);
    }
}

static void walk_truncations(const uint8_t *der, size_t length)
{
    for (size_t cut = 0; cut < length; ++cut) {
        /* A copy of exactly cut bytes, so that ASan sees a read past the end. */
        uint8_t *exact = malloc(cut == 0u ? 1u : cut);
        memcpy(exact, der, cut);
        tls_x509_view_t view;
        CHECK(tls_x509_parse(exact, cut, &view) == TLS_X509_MALFORMED);
        free(exact);
    }
}

static void test_truncations(void)
{
    walk_truncations(fixture_ec_self, sizeof(fixture_ec_self));
    walk_truncations(fixture_ec_by_rsa, sizeof(fixture_ec_by_rsa));
    walk_truncations(fixture_far, sizeof(fixture_far));
    walk_truncations(fixture_p384, sizeof(fixture_p384));
}

static void test_flipped_bytes(void)
{
    unsigned accepted = 0, accepted_other_key = 0;
    for (size_t at = 0; at < sizeof(fixture_ec_self); ++at) {
        static const uint8_t masks[] = {0x01, 0x80, 0xff};
        for (size_t m = 0; m < sizeof(masks); ++m) {
            uint8_t *copy = malloc(sizeof(fixture_ec_self));
            memcpy(copy, fixture_ec_self, sizeof(fixture_ec_self));
            copy[at] ^= masks[m];
            tls_x509_view_t view;
            tls_x509_result_t result = tls_x509_parse(copy, sizeof(fixture_ec_self), &view);
            check_sane(result, copy, sizeof(fixture_ec_self), &view);
            if (result == TLS_X509_OK) {
                ++accepted;
                if (memcmp(view.public_key, fixture_device_key, 65) != 0) {
                    ++accepted_other_key;
                }
            }
            free(copy);
        }
    }
    /* A flip in a value the parser does not judge, such as a signature byte, still parses. */
    CHECK(accepted > 0u);
    /* Each flip of a point byte past its prefix parses with another key. */
    CHECK(accepted_other_key >= 64u * 3u);
    printf("flipped bytes: %u of %zu accepted, %u of them with another key\n", accepted,
           sizeof(fixture_ec_self) * 3u, accepted_other_key);
}

static uint64_t rng_state = 0x9e3779b97f4a7c15u;

static uint64_t next_random(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void test_random_mutation(void)
{
    const uint8_t *seeds[] = {fixture_ec_self, fixture_ec_by_rsa, fixture_far, fixture_rsa_self};
    const size_t seed_lengths[] = {sizeof(fixture_ec_self), sizeof(fixture_ec_by_rsa),
                                   sizeof(fixture_far), sizeof(fixture_rsa_self)};
    unsigned counts[3] = {0};
    const unsigned rounds = 60000;
    for (unsigned round = 0; round < rounds; ++round) {
        size_t which = (size_t)(next_random() % 4u);
        size_t length = seed_lengths[which];
        uint8_t work[1024];
        memcpy(work, seeds[which], length);
        unsigned edits = 1u + (unsigned)(next_random() % 4u);
        for (unsigned e = 0; e < edits && length > 0u; ++e) {
            size_t at = (size_t)(next_random() % length);
            switch (next_random() % 4u) {
            case 0:
                work[at] = (uint8_t)next_random();
                break;
            case 1: /* Delete a byte. */
                memmove(work + at, work + at + 1u, length - at - 1u);
                length -= 1u;
                break;
            case 2: /* Insert a byte. */
                if (length < sizeof(work)) {
                    memmove(work + at + 1u, work + at, length - at);
                    work[at] = (uint8_t)next_random();
                    length += 1u;
                }
                break;
            default: /* A length byte made large. */
                work[at] = (uint8_t)(0x80u | (next_random() % 4u));
                break;
            }
        }
        uint8_t *exact = malloc(length == 0u ? 1u : length);
        memcpy(exact, work, length);
        tls_x509_view_t view;
        tls_x509_result_t result = tls_x509_parse(exact, length, &view);
        check_sane(result, exact, length, &view);
        counts[result == TLS_X509_OK ? 0 : result == TLS_X509_MALFORMED ? 1 : 2] += 1u;
        free(exact);
    }
    printf("random mutation: %u rounds, %u parsed, %u malformed, %u invalid\n", rounds, counts[0],
           counts[1], counts[2]);
    CHECK(counts[1] > counts[0]);
}

/* ---- Builders. */

static const char *const dns_names[] = {"device.example", "*.lan.example"};
static const uint8_t ipv4[][4] = {{192, 0, 2, 10}};
static const uint8_t ipv6[][16] = {{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
static const crux_tls_certificate_names_t full_names = {"Crux device", dns_names, 2, ipv4, 1,
                                                        ipv6,          1};
static const crux_tls_certificate_names_t cn_only = {"Crux device", NULL, 0, NULL, 0, NULL, 0};

static tls_x509_self_signed_t self_input(const crux_tls_certificate_names_t *names,
                                         int64_t not_before, uint32_t days)
{
    tls_x509_self_signed_t input = {names, {0}, not_before, days};
    for (unsigned i = 0; i < TLS_X509_SERIAL_BYTES; ++i) {
        input.serial[i] = (uint8_t)(0xf0u + i);
    }
    return input;
}

static void test_build_self_signed(void)
{
    static uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    tls_test_signer_t signer = {0};
    tls_x509_self_signed_t input = self_input(&full_names, 1767225600, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign,
                                     &signer, out, sizeof(out), &length) == TLS_X509_BUILT);
    CHECK(signer.calls == 1u);
    tls_x509_view_t view;
    CHECK(tls_x509_parse(out, length, &view) == TLS_X509_OK);
    CHECK(memcmp(view.public_key, fixture_device_key, 65) == 0);
    CHECK(view.not_before_s == 1767225600);
    CHECK(view.no_expiry);
    CHECK(view.not_after_s == 253402300799);
    CHECK(strcmp(view.subject, "Crux device") == 0);
    CHECK(strcmp(view.issuer, "Crux device") == 0);

    /* The signed digest is the TBS: the second element, from its tag to its end. */
    CHECK(out[0] == 0x30 && out[1] == 0x82);
    size_t tbs_length = 4u + ((size_t)out[6] << 8 | out[7]);
    CHECK(out[4] == 0x30 && out[5] == 0x82);
    uint8_t digest[32];
    tls_test_sha256(out + 4, tbs_length, digest);
    CHECK(memcmp(digest, signer.last_digest, 32) == 0);

    /* The serial, positive and 16 bytes, after the version. */
    static const uint8_t version_and_serial[] = {0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x10, 0x70};
    CHECK(memcmp(out + 8, version_and_serial, sizeof(version_and_serial)) == 0);
    /* What clients need to find. */
    static const uint8_t eku_server_auth[] = {0x06, 0x08, 0x2b, 0x06, 0x01,
                                              0x05, 0x05, 0x07, 0x03, 0x01};
    CHECK(tls_test_contains(out, length, eku_server_auth, sizeof(eku_server_auth)));
    static const uint8_t san_dns[] = {0x82, 0x0e, 'd', 'e', 'v', 'i', 'c', 'e',
                                      '.',  'e',  'x', 'a', 'm', 'p', 'l', 'e'};
    CHECK(tls_test_contains(out, length, san_dns, sizeof(san_dns)));
    static const uint8_t san_ip4[] = {0x87, 0x04, 192, 0, 2, 10};
    CHECK(tls_test_contains(out, length, san_ip4, sizeof(san_ip4)));
    static const uint8_t san_ip6_head[] = {0x87, 0x10, 0x20, 0x01, 0x0d, 0xb8};
    CHECK(tls_test_contains(out, length, san_ip6_head, sizeof(san_ip6_head)));
    static const uint8_t no_expiry[] = {0x18, 0x0f, '9', '9', '9', '9', '1', '2', '3',
                                        '1',  '2',  '3', '5', '9', '5', '9', 'Z'};
    CHECK(tls_test_contains(out, length, no_expiry, sizeof(no_expiry)));
    static const uint8_t not_before[] = {0x17, 0x0d, '2', '6', '0', '1', '0', '1',
                                         '0',  '0',  '0', '0', '0', '0', 'Z'};
    CHECK(tls_test_contains(out, length, not_before, sizeof(not_before)));

    /* Without alternative names there is no SAN extension. */
    input = self_input(&cn_only, 1767225600, 825);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BUILT);
    CHECK(tls_x509_parse(out, length, &view) == TLS_X509_OK);
    CHECK(!view.no_expiry);
    CHECK(view.not_after_s == 1767225600 + 825 * 86400);
    static const uint8_t san_oid[] = {0x06, 0x03, 0x55, 0x1d, 0x11};
    CHECK(!tls_test_contains(out, length, san_oid, sizeof(san_oid)));

    /* Dates from 2050 are GeneralizedTime. */
    int64_t in_2060 = 0;
    CHECK(tls_x509_time_from_civil(2060, 6, 1, 12, 0, 0, &in_2060));
    input = self_input(&cn_only, in_2060, 1);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BUILT);
    CHECK(tls_x509_parse(out, length, &view) == TLS_X509_OK);
    CHECK(view.not_before_s == in_2060);
    CHECK(view.not_after_s == in_2060 + 86400);
    static const uint8_t generalized[] = {0x18, 0x0f, '2', '0', '6', '0', '0', '6', '0',
                                          '1',  '1',  '2', '0', '0', '0', '0', 'Z'};
    CHECK(tls_test_contains(out, length, generalized, sizeof(generalized)));
}

static void test_build_serial_forms(void)
{
    static uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    tls_x509_self_signed_t input = self_input(&cn_only, 1767225600, 0);
    memset(input.serial, 0, sizeof(input.serial));
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BUILT);
    tls_x509_view_t view;
    CHECK(tls_x509_parse(out, length, &view) == TLS_X509_OK);
    /* Still 16 bytes and positive, whatever the random bytes: the second bit is set. */
    static const uint8_t serial_head[] = {0x02, 0x10, 0x40, 0x00};
    CHECK(memcmp(out + 13, serial_head, sizeof(serial_head)) == 0);
}

static void signature_integers(const uint8_t *out, size_t length, uint8_t *sig_der,
                               size_t *sig_length)
{
    /* The signature BIT STRING ends the certificate: 03 len 00 then the Ecdsa-Sig-Value. */
    size_t at = length;
    for (size_t i = 0; i + 3u < length; ++i) {
        if (out[i] == 0x03 && out[i + 2u] == 0x00 && out[i + 3u] == 0x30 &&
            i + 2u + out[i + 1u] == length) {
            at = i;
        }
    }
    CHECK(at < length);
    *sig_length = length - at - 3u;
    memcpy(sig_der, out + at + 3u, *sig_length);
}

static void test_build_signature_encoding(void)
{
    static uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    tls_test_signer_t signer = {.fixed = true};
    memset(signer.r, 0x80, 32); /* High bit: needs a zero byte. */
    memset(signer.s, 0x00, 31); /* Leading zeros: stripped. */
    signer.s[31] = 0x05;
    tls_x509_self_signed_t input = self_input(&cn_only, 1767225600, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign,
                                     &signer, out, sizeof(out), &length) == TLS_X509_BUILT);
    uint8_t sig[80];
    size_t sig_length = 0;
    signature_integers(out, length, sig, &sig_length);
    uint8_t expected[2 + 2 + 33 + 3];
    expected[0] = 0x30;
    expected[1] = 2u + 33u + 3u;
    expected[2] = 0x02;
    expected[3] = 33;
    expected[4] = 0x00;
    memset(expected + 5, 0x80, 32);
    expected[37] = 0x02;
    expected[38] = 0x01;
    expected[39] = 0x05;
    CHECK(sig_length == sizeof(expected));
    CHECK(memcmp(sig, expected, sizeof(expected)) == 0);

    memset(signer.r, 0x00, 32); /* Zero is one zero byte. */
    memset(signer.s, 0x7f, 32);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign,
                                     &signer, out, sizeof(out), &length) == TLS_X509_BUILT);
    signature_integers(out, length, sig, &sig_length);
    CHECK(sig_length == 2u + 3u + 34u);
    CHECK(sig[2] == 0x02 && sig[3] == 0x01 && sig[4] == 0x00);
    CHECK(sig[5] == 0x02 && sig[6] == 32 && sig[7] == 0x7f);
}

static void test_build_bounds(void)
{
    static uint8_t full[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t full_length = 0;
    tls_x509_self_signed_t input = self_input(&full_names, 1767225600, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     full, sizeof(full), &full_length) == TLS_X509_BUILT);
    for (size_t capacity = 0; capacity < full_length; ++capacity) {
        uint8_t *out = malloc(capacity + 16u);
        memset(out, 0xa5, capacity + 16u);
        size_t length = 77;
        CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign,
                                         NULL, out, capacity, &length) == TLS_X509_NO_ROOM);
        CHECK(length == 0u);
        for (size_t i = capacity; i < capacity + 16u; ++i) {
            CHECK(out[i] == 0xa5);
        }
        free(out);
    }
    uint8_t request[CRUX_TLS_CERTIFICATE_CSR_MAX];
    size_t request_length = 0;
    CHECK(tls_x509_build_request(&full_names, fixture_device_key, tls_test_hash, tls_test_sign,
                                 NULL, request, sizeof(request),
                                 &request_length) == TLS_X509_BUILT);
    for (size_t capacity = 0; capacity < request_length; ++capacity) {
        size_t length = 77;
        CHECK(tls_x509_build_request(&full_names, fixture_device_key, tls_test_hash, tls_test_sign,
                                     NULL, request, capacity, &length) == TLS_X509_NO_ROOM);
        CHECK(length == 0u);
    }
}

static void test_build_largest_names(void)
{
    static char labels[4][CRUX_TLS_CERTIFICATE_DNS_NAME_MAX + 1];
    const char *names[4];
    for (unsigned n = 0; n < 4u; ++n) {
        /* 63-byte labels joined by dots, to exactly 253. */
        for (unsigned i = 0; i < CRUX_TLS_CERTIFICATE_DNS_NAME_MAX; ++i) {
            if (i % 64u == 63u) {
                labels[n][i] = '.';
            } else {
                labels[n][i] = (char)('a' + (int)n);
            }
        }
        labels[n][CRUX_TLS_CERTIFICATE_DNS_NAME_MAX] = '\0';
        names[n] = labels[n];
    }
    char cn[65];
    memset(cn, 'C', 64);
    cn[64] = '\0';
    crux_tls_certificate_names_t largest = {cn, names, 4, NULL, 0, NULL, 0};
    CHECK(tls_x509_names_valid(&largest));
    static uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    tls_x509_self_signed_t input = self_input(&largest, 1767225600, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BUILT);
    tls_x509_view_t view;
    CHECK(tls_x509_parse(out, length, &view) == TLS_X509_OK);
    uint8_t request[CRUX_TLS_CERTIFICATE_CSR_MAX];
    CHECK(tls_x509_build_request(&largest, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                 request, sizeof(request), &length) == TLS_X509_BUILT);
    printf("largest names: certificate and request fit, request %zu bytes\n", length);
}

static void test_names_rules(void)
{
    const char *const one[] = {"ok.example"};
    crux_tls_certificate_names_t n = {"Crux device", NULL, 0, NULL, 0, NULL, 0};
    CHECK(tls_x509_names_valid(&n));
    n.common_name = NULL;
    CHECK(!tls_x509_names_valid(&n));
    n.common_name = "";
    CHECK(!tls_x509_names_valid(&n));
    char cn[66];
    memset(cn, 'a', 65);
    cn[65] = '\0';
    n.common_name = cn;
    CHECK(!tls_x509_names_valid(&n));
    cn[64] = '\0';
    CHECK(tls_x509_names_valid(&n));
    n.common_name = "a/b";
    CHECK(!tls_x509_names_valid(&n));
    n.common_name = "a\"b";
    CHECK(!tls_x509_names_valid(&n));
    n.common_name = "ok";
    n.dns_count = 1;
    CHECK(!tls_x509_names_valid(&n)); /* A count with no array. */
    n.dns_names = one;
    CHECK(tls_x509_names_valid(&n));

    const char *bad_dns[] = {"",   "a..b",  ".a",   "a.",  "a_b.example",
                             "*.", "a.*.b", "**.a", "a b", "-a.b"};
    for (size_t i = 0; i < sizeof(bad_dns) / sizeof(bad_dns[0]); ++i) {
        const char *list[] = {bad_dns[i]};
        n.dns_names = list;
        if (tls_x509_names_valid(&n)) {
            printf("accepted bad DNS name: '%s'\n", bad_dns[i]);
        }
        CHECK(!tls_x509_names_valid(&n));
    }
    char label64[70];
    memset(label64, 'a', 64);
    memcpy(label64 + 64, ".b", 3);
    const char *long_label[] = {label64};
    n.dns_names = long_label;
    CHECK(!tls_x509_names_valid(&n));
    const char *good_dns[] = {"a", "*.a.example", "x-1.example", "A.EXAMPLE"};
    for (size_t i = 0; i < sizeof(good_dns) / sizeof(good_dns[0]); ++i) {
        const char *list[] = {good_dns[i]};
        n.dns_names = list;
        CHECK(tls_x509_names_valid(&n));
    }
    const char *five[] = {"a", "b", "c", "d", "e"};
    n.dns_names = five;
    n.dns_count = 5;
    CHECK(!tls_x509_names_valid(&n));
    n.dns_count = 3;
    n.ipv4 = ipv4;
    n.ipv4_count = 1;
    CHECK(tls_x509_names_valid(&n));
    n.ipv6 = ipv6;
    n.ipv6_count = 1;
    CHECK(!tls_x509_names_valid(&n)); /* Five in all. */
    n.ipv6_count = 0;
    n.ipv4 = NULL;
    CHECK(!tls_x509_names_valid(&n));
    CHECK(!tls_x509_names_valid(NULL));

    uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 9;
    tls_x509_self_signed_t input = self_input(&n, 1767225600, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BAD_INPUT);
    CHECK(length == 0u);
    CHECK(tls_x509_build_request(&n, fixture_device_key, tls_test_hash, tls_test_sign, NULL, out,
                                 sizeof(out), &length) == TLS_X509_BAD_INPUT);
}

static void test_build_dates_refused(void)
{
    uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    tls_x509_self_signed_t input = self_input(&cn_only, -1, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BAD_INPUT);
    input = self_input(&cn_only, 253402300799 - 86399, 1);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BAD_INPUT);
    input = self_input(&cn_only, 253402300799 - 86400, 1);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BUILT);
    input = self_input(&cn_only, 253402300799, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BUILT);
    input = self_input(&cn_only, 253402300800, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_BAD_INPUT);
}

static bool failing_hash(const uint8_t *data, size_t length, uint8_t digest[32])
{
    (void)data;
    (void)length;
    (void)digest;
    return false;
}

static void test_build_crypto_failures(void)
{
    uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 5;
    tls_test_signer_t signer = {.fail = true};
    tls_x509_self_signed_t input = self_input(&cn_only, 1767225600, 0);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, tls_test_hash, tls_test_sign,
                                     &signer, out, sizeof(out), &length) == TLS_X509_NO_SIGNATURE);
    CHECK(length == 0u);
    CHECK(tls_x509_build_self_signed(&input, fixture_device_key, failing_hash, tls_test_sign, NULL,
                                     out, sizeof(out), &length) == TLS_X509_NO_SIGNATURE);
    CHECK(tls_x509_build_request(&cn_only, fixture_device_key, tls_test_hash, tls_test_sign,
                                 &signer, out, sizeof(out), &length) == TLS_X509_NO_SIGNATURE);
    CHECK(tls_x509_build_request(&cn_only, fixture_device_key, failing_hash, tls_test_sign, NULL,
                                 out, sizeof(out), &length) == TLS_X509_NO_SIGNATURE);
}

static void test_build_request(void)
{
    uint8_t out[CRUX_TLS_CERTIFICATE_CSR_MAX];
    size_t length = 0;
    tls_test_signer_t signer = {0};
    CHECK(tls_x509_build_request(&cn_only, fixture_device_key, tls_test_hash, tls_test_sign,
                                 &signer, out, sizeof(out), &length) == TLS_X509_BUILT);
    /* CertificationRequestInfo: version 0, the subject, the key, and no attributes. */
    CHECK(out[0] == 0x30);
    size_t info_at = out[1] == 0x81 ? 3u : 4u;
    CHECK(out[info_at] == 0x30);
    size_t info_header = out[info_at + 1u] == 0x81 ? 3u : 2u;
    size_t info_length = info_header + out[info_at + info_header - 1u];
    static const uint8_t version0[] = {0x02, 0x01, 0x00, 0x30};
    CHECK(memcmp(out + info_at + info_header, version0, sizeof(version0)) == 0);
    static const uint8_t no_attributes[] = {0xa0, 0x00};
    CHECK(memcmp(out + info_at + info_length - 2u, no_attributes, 2) == 0);
    uint8_t digest[32];
    tls_test_sha256(out + info_at, info_length, digest);
    CHECK(memcmp(digest, signer.last_digest, 32) == 0);
    CHECK(tls_test_contains(out, length, fixture_device_key, 65));

    CHECK(tls_x509_build_request(&full_names, fixture_device_key, tls_test_hash, tls_test_sign,
                                 NULL, out, sizeof(out), &length) == TLS_X509_BUILT);
    static const uint8_t extension_request[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                                0xf7, 0x0d, 0x01, 0x09, 0x0e};
    CHECK(tls_test_contains(out, length, extension_request, sizeof(extension_request)));
    static const uint8_t san_ip4[] = {0x87, 0x04, 192, 0, 2, 10};
    CHECK(tls_test_contains(out, length, san_ip4, sizeof(san_ip4)));
    /* A request is not a certificate. */
    tls_x509_view_t view;
    CHECK(tls_x509_parse(out, length, &view) != TLS_X509_OK);
}

static void test_build_pkcs8(void)
{
    uint8_t scalar[32];
    for (unsigned i = 0; i < 32u; ++i) {
        scalar[i] = (uint8_t)(i + 1u);
    }
    uint8_t out[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    CHECK(tls_x509_build_pkcs8(scalar, fixture_device_key, out, sizeof(out), &length) ==
          TLS_X509_BUILT);
    CHECK(length == 138u);
    static const uint8_t head[] = {0x30, 0x81, 0x87, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06,
                                   0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
                                   0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,
                                   0x04, 0x6d, 0x30, 0x6b, 0x02, 0x01, 0x01, 0x04, 0x20};
    CHECK(memcmp(out, head, sizeof(head)) == 0);
    CHECK(memcmp(out + sizeof(head), scalar, 32) == 0);
    static const uint8_t public_head[] = {0xa1, 0x44, 0x03, 0x42, 0x00};
    CHECK(memcmp(out + sizeof(head) + 32u, public_head, sizeof(public_head)) == 0);
    CHECK(memcmp(out + sizeof(head) + 32u + sizeof(public_head), fixture_device_key, 65) == 0);
    for (size_t capacity = 0; capacity < 138u; ++capacity) {
        CHECK(tls_x509_build_pkcs8(scalar, fixture_device_key, out, capacity, &length) ==
              TLS_X509_NO_ROOM);
    }
}

static void test_time_and_text(void)
{
    int64_t t = -1;
    CHECK(tls_x509_time_from_civil(1970, 1, 1, 0, 0, 0, &t) && t == 0);
    CHECK(tls_x509_time_from_civil(2026, 1, 1, 0, 0, 0, &t) && t == 1767225600);
    CHECK(tls_x509_time_from_civil(2000, 2, 29, 0, 0, 0, &t) && t == 951782400);
    CHECK(tls_x509_time_from_civil(9999, 12, 31, 23, 59, 59, &t) && t == 253402300799);
    CHECK(!tls_x509_time_from_civil(2100, 2, 29, 0, 0, 0, &t));
    CHECK(!tls_x509_time_from_civil(2026, 4, 31, 0, 0, 0, &t));
    CHECK(!tls_x509_time_from_civil(10000, 1, 1, 0, 0, 0, &t));

    uint8_t digest[32];
    for (unsigned i = 0; i < 32u; ++i) {
        digest[i] = (uint8_t)(i * 8u + 7u);
    }
    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    tls_x509_fingerprint_text(digest, text);
    CHECK(strlen(text) == 95u);
    CHECK(strncmp(text, "07:0F:17:1F:", 12) == 0);
    CHECK(strcmp(text + 93, "FF") == 0);
}

int main(void)
{
    tls_test_case("openssl certificates", test_openssl_certificates);
    tls_test_case("output untouched on refusal", test_output_untouched_on_refusal);
    tls_test_case("composer baseline", test_composer_baseline);
    tls_test_case("version", test_version);
    tls_test_case("serial", test_serial);
    tls_test_case("algorithms", test_algorithms);
    tls_test_case("names", test_names);
    tls_test_case("validity", test_validity);
    tls_test_case("keys", test_keys);
    tls_test_case("extensions", test_extensions);
    tls_test_case("signature and framing", test_signature_and_framing);
    tls_test_case("length encodings", test_length_encodings);
    tls_test_case("review byte edits", test_review_byte_edits);
    tls_test_case("string values", test_string_values);
    tls_test_case("universal tag zero", test_universal_tag_zero);
    tls_test_case("extension values", test_extension_values);
    tls_test_case("extension count", test_extension_count);
    tls_test_case("key info and unique identifiers", test_key_info_and_unique_ids);
    tls_test_case("truncations", test_truncations);
    tls_test_case("flipped bytes", test_flipped_bytes);
    tls_test_case("random mutation", test_random_mutation);
    tls_test_case("build self-signed", test_build_self_signed);
    tls_test_case("build serial forms", test_build_serial_forms);
    tls_test_case("build signature encoding", test_build_signature_encoding);
    tls_test_case("build bounds", test_build_bounds);
    tls_test_case("build largest names", test_build_largest_names);
    tls_test_case("names rules", test_names_rules);
    tls_test_case("build dates refused", test_build_dates_refused);
    tls_test_case("build crypto failures", test_build_crypto_failures);
    tls_test_case("build request", test_build_request);
    tls_test_case("build pkcs8", test_build_pkcs8);
    tls_test_case("time and text", test_time_and_text);
    return tls_test_finish("tls_certificate_x509");
}
