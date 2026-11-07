/*
 * The wire checks of Issue #192, on the Host: known answers from OpenSSH 9.6, the refused key
 * types, every truncation and flipped byte of each accepted form, and seeded random mutations held
 * to an oracle written apart from the code under test. Inputs live in buffers of their exact
 * length, so a sanitizer build catches a read past the end.
 */
#include "crux_ssh_wire.h"
#include "ssh_check.h"

#include <stdlib.h>
#include <string.h>

#ifndef WIRE_TEST_ROUNDS
#define WIRE_TEST_ROUNDS 200000u
#endif

/* ---- Known answers: keys made by ssh-keygen in OpenSSH 9.6p1, and what ssh-keygen -l printed.
 * ---- */

static const uint8_t ed25519_blob[51] = {
    0x00, 0x00, 0x00, 0x0b, 0x73, 0x73, 0x68, 0x2d, 0x65, 0x64, 0x32, 0x35, 0x35,
    0x31, 0x39, 0x00, 0x00, 0x00, 0x20, 0xd3, 0x0d, 0x94, 0x79, 0x7c, 0x72, 0x97,
    0xc2, 0x27, 0x5b, 0x95, 0x0c, 0x25, 0x77, 0x2e, 0x58, 0xec, 0x97, 0xfe, 0x1a,
    0x5f, 0x72, 0x52, 0x9d, 0x7e, 0x1f, 0x79, 0xbd, 0xe8, 0x09, 0xfc, 0x75};
static const uint8_t ed25519_digest[32] = {
    0x7d, 0xc7, 0x5b, 0x23, 0x4f, 0xaa, 0xe5, 0x79, 0x8f, 0xbb, 0x78, 0x41, 0x44, 0x37, 0x8c, 0x86,
    0x87, 0x19, 0x19, 0x86, 0xc2, 0x4b, 0x94, 0x87, 0x6b, 0xe2, 0x11, 0x94, 0xdb, 0x1b, 0x3d, 0xd7};
static const char ed25519_fingerprint[] = "SHA256:fcdbI0+q5XmPu3hBRDeMhocZGYbCS5SHa+IRlNsbPdc";

static const uint8_t p256_blob[104] = {
    0x00, 0x00, 0x00, 0x13, 0x65, 0x63, 0x64, 0x73, 0x61, 0x2d, 0x73, 0x68, 0x61, 0x32, 0x2d,
    0x6e, 0x69, 0x73, 0x74, 0x70, 0x32, 0x35, 0x36, 0x00, 0x00, 0x00, 0x08, 0x6e, 0x69, 0x73,
    0x74, 0x70, 0x32, 0x35, 0x36, 0x00, 0x00, 0x00, 0x41, 0x04, 0x50, 0x1a, 0x3a, 0xdf, 0x70,
    0xa4, 0x5e, 0x77, 0xf3, 0x03, 0xe7, 0xe0, 0x18, 0x87, 0x57, 0xa5, 0x6c, 0x8b, 0x70, 0xb5,
    0x2e, 0x1b, 0xe5, 0xd5, 0xfd, 0xbc, 0x1e, 0x81, 0xf3, 0xb9, 0xb7, 0x5a, 0x18, 0x89, 0xd0,
    0x85, 0xa4, 0x7f, 0x7c, 0x4b, 0x5a, 0xb3, 0x68, 0xe2, 0x1c, 0xa0, 0xf9, 0x0f, 0x75, 0x32,
    0x80, 0x92, 0x43, 0xf0, 0x39, 0xa2, 0x0e, 0x6e, 0xaa, 0xd6, 0x52, 0xdb, 0xbe, 0x8b};
static const uint8_t p256_digest[32] = {
    0x48, 0x2f, 0xec, 0xee, 0x83, 0xe7, 0x06, 0x43, 0x22, 0xbe, 0x74, 0x18, 0xb0, 0x35, 0x2e, 0x21,
    0xb7, 0xde, 0x58, 0x05, 0x8c, 0xd9, 0xd9, 0x1f, 0x6c, 0x40, 0x2e, 0x5e, 0x2c, 0x49, 0xfc, 0x99};
static const char p256_fingerprint[] = "SHA256:SC/s7oPnBkMivnQYsDUuIbfeWAWM2dkfbEAuXixJ/Jk";

/* Where the key material starts in each blob. Everything before it is fixed for the type. */
#define ED25519_HEADER 19u
#define P256_HEADER 40u /* Including the 0x04 of the uncompressed point. */

/* Values that are no key type: a refusal must leave the output alone, and an unknown type is
 * refused. File scope keeps them out of the analyser's path checks, which would flag each cast. */
static const crux_ssh_key_type_t no_type = (crux_ssh_key_type_t)0;
static const crux_ssh_key_type_t next_type = (crux_ssh_key_type_t)3;
static const crux_ssh_key_type_t negative_type = (crux_ssh_key_type_t)-1;

static const char name_ed25519[] = "ssh-ed25519";
static const char name_p256[] = "ecdsa-sha2-nistp256";

/* ---- Helpers. ---- */

static uint64_t rng_state = 0x192u;

static uint32_t next_random(void)
{
    rng_state ^= rng_state << 13u;
    rng_state ^= rng_state >> 7u;
    rng_state ^= rng_state << 17u;
    return (uint32_t)(rng_state >> 16u);
}

/* A heap copy of exactly this length, so that a sanitizer sees any read past it. */
static uint8_t *exact_copy(const uint8_t *bytes, size_t length)
{
    uint8_t *copy = malloc(length == 0u ? 1u : length);

    if (copy == NULL) {
        abort();
    }
    if (length > 0u) {
        memcpy(copy, bytes, length);
    }
    return copy;
}

static size_t put_u32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24u);
    at[1] = (uint8_t)(value >> 16u);
    at[2] = (uint8_t)(value >> 8u);
    at[3] = (uint8_t)value;
    return 4u;
}

static size_t put_string(uint8_t *at, const void *text, size_t length)
{
    size_t used = put_u32(at, (uint32_t)length);

    memcpy(at + used, text, length);
    return used + length;
}

static bool user_key(const char *name, const uint8_t *blob, size_t blob_length,
                     crux_ssh_key_type_t *type)
{
    uint8_t *name_copy = exact_copy((const uint8_t *)name, strlen(name));
    uint8_t *blob_copy = exact_copy(blob, blob_length);
    bool accepted = crux_ssh_wire_user_key(name_copy, strlen(name), blob_copy, blob_length, type);

    free(name_copy);
    free(blob_copy);
    return accepted;
}

/* The oracle: the only accepted blobs are the two fixed headers followed by key material. */
static int oracle_user_key(const uint8_t *name, size_t name_length, const uint8_t *blob,
                           size_t blob_length)
{
    if (name_length == strlen(name_ed25519) && memcmp(name, name_ed25519, name_length) == 0 &&
        blob_length == sizeof(ed25519_blob) && memcmp(blob, ed25519_blob, ED25519_HEADER) == 0) {
        return CRUX_SSH_KEY_ED25519;
    }
    if (name_length == strlen(name_p256) && memcmp(name, name_p256, name_length) == 0 &&
        blob_length == sizeof(p256_blob) && memcmp(blob, p256_blob, P256_HEADER) == 0) {
        return CRUX_SSH_KEY_ECDSA_P256;
    }
    return 0;
}

static void check_user_key_against_oracle(const uint8_t *name, size_t name_length,
                                          const uint8_t *blob, size_t blob_length)
{
    uint8_t *name_copy = exact_copy(name, name_length);
    uint8_t *blob_copy = exact_copy(blob, blob_length);
    crux_ssh_key_type_t type = no_type;
    bool accepted = crux_ssh_wire_user_key(name_copy, name_length, blob_copy, blob_length, &type);
    int expected = oracle_user_key(name, name_length, blob, blob_length);

    SSH_CHECK(accepted == (expected != 0));
    SSH_CHECK(!accepted || (int)type == expected);
    SSH_CHECK(accepted || type == no_type);
    free(name_copy);
    free(blob_copy);
}

/* ---- User names. ---- */

static void test_user_names(void)
{
    char user[CRUX_SSH_USER_NAME_MAX + 1u];
    uint8_t name[CRUX_SSH_USER_NAME_MAX + 2u];

    SSH_CHECK(crux_ssh_wire_user_name((const uint8_t *)"admin", 5u, user));
    SSH_CHECK(strcmp(user, "admin") == 0);

    memset(name, 'a', sizeof(name));
    SSH_CHECK(crux_ssh_wire_user_name(name, CRUX_SSH_USER_NAME_MAX, user));
    SSH_CHECK(strlen(user) == CRUX_SSH_USER_NAME_MAX);
    SSH_CHECK(!crux_ssh_wire_user_name(name, CRUX_SSH_USER_NAME_MAX + 1u, user));
    SSH_CHECK(user[0] == '\0');

    SSH_CHECK(!crux_ssh_wire_user_name(name, 0u, user));
    SSH_CHECK(!crux_ssh_wire_user_name((const uint8_t *)"ad\0min", 6u, user));
    SSH_CHECK(!crux_ssh_wire_user_name((const uint8_t *)"admin\0", 6u, user));
    SSH_CHECK(!crux_ssh_wire_user_name((const uint8_t *)"\0", 1u, user));
    SSH_CHECK(!crux_ssh_wire_user_name(NULL, 5u, user));
    SSH_CHECK(!crux_ssh_wire_user_name(name, 5u, NULL));
}

static void test_user_names_random(void)
{
    for (unsigned round = 0; round < WIRE_TEST_ROUNDS / 4u; ++round) {
        uint8_t name[40];
        size_t length = next_random() % sizeof(name);
        char user[CRUX_SSH_USER_NAME_MAX + 1u];
        bool expected = length >= 1u && length <= CRUX_SSH_USER_NAME_MAX;

        for (size_t i = 0; i < length; ++i) {
            /* Mostly printable, with a NUL now and then. */
            name[i] = (next_random() % 16u) == 0u ? 0u : (uint8_t)(0x20u + next_random() % 0x5fu);
            expected = expected && name[i] != 0u;
        }
        uint8_t *copy = exact_copy(name, length);
        bool accepted = crux_ssh_wire_user_name(copy, length, user);

        SSH_CHECK(accepted == expected);
        SSH_CHECK(!accepted || (strlen(user) == length && memcmp(user, name, length) == 0));
        SSH_CHECK(accepted || user[0] == '\0');
        free(copy);
    }
}

/* ---- User keys. ---- */

static void test_accepted_keys(void)
{
    crux_ssh_key_type_t type = no_type;

    SSH_CHECK(user_key(name_ed25519, ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(type == CRUX_SSH_KEY_ED25519);
    type = no_type;
    SSH_CHECK(user_key(name_p256, p256_blob, sizeof(p256_blob), &type));
    SSH_CHECK(type == CRUX_SSH_KEY_ECDSA_P256);
    SSH_CHECK(!crux_ssh_wire_user_key(NULL, 11u, ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(!crux_ssh_wire_user_key((const uint8_t *)name_ed25519, 11u, NULL,
                                      sizeof(ed25519_blob), &type));
    SSH_CHECK(!crux_ssh_wire_user_key((const uint8_t *)name_ed25519, 11u, ed25519_blob,
                                      sizeof(ed25519_blob), NULL));
}

/* Each refused type, with a well-formed blob of its own, under its own name and under each accepted
 * name. RSA comes first: the owner's decision is that SSH has none. */
static void test_refused_key_types(void)
{
    static const uint8_t rsa_e[] = {0x01, 0x00, 0x01};
    static const uint8_t ssh_prefix[] = "ssh:";
    uint8_t material[160];
    uint8_t blob[400];
    size_t used;
    crux_ssh_key_type_t type = no_type;

    memset(material, 0x5a, sizeof(material));
    material[0] = 0x04; /* An uncompressed point, where a point is expected. */

    /* ssh-rsa: e, then a 2048-bit n. */
    used = put_string(blob, "ssh-rsa", 7u);
    used += put_string(blob + used, rsa_e, sizeof(rsa_e));
    used += put_u32(blob + used, 257u);
    blob[used++] = 0x00;
    for (size_t i = 0; i < 256u; ++i) {
        blob[used++] = 0xc3u;
    }
    const size_t rsa_length = used;
    const char *rsa_names[] = {"ssh-rsa",    "rsa-sha2-256", "rsa-sha2-512",
                               name_ed25519, name_p256,      "ssh-rsa-cert-v01@openssh.com"};
    for (size_t i = 0; i < sizeof(rsa_names) / sizeof(rsa_names[0]); ++i) {
        SSH_CHECK(!user_key(rsa_names[i], blob, rsa_length, &type));
    }
    /* An accepted key's blob under an RSA name is refused too. */
    SSH_CHECK(!user_key("ssh-rsa", ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(!user_key("rsa-sha2-256", p256_blob, sizeof(p256_blob), &type));

    /* ssh-dss: p, q, g, y. */
    used = put_string(blob, "ssh-dss", 7u);
    for (int i = 0; i < 4; ++i) {
        used += put_string(blob + used, material + 1, 20u);
    }
    SSH_CHECK(!user_key("ssh-dss", blob, used, &type));
    SSH_CHECK(!user_key(name_ed25519, blob, used, &type));

    /* Other curves, which crux_access stores but the provider does not offer. */
    used = put_string(blob, "ecdsa-sha2-nistp384", 19u);
    used += put_string(blob + used, "nistp384", 8u);
    used += put_string(blob + used, material, 97u);
    SSH_CHECK(!user_key("ecdsa-sha2-nistp384", blob, used, &type));
    SSH_CHECK(!user_key(name_p256, blob, used, &type));
    used = put_string(blob, "ecdsa-sha2-nistp521", 19u);
    used += put_string(blob + used, "nistp521", 8u);
    used += put_string(blob + used, material, 133u);
    SSH_CHECK(!user_key("ecdsa-sha2-nistp521", blob, used, &type));
    SSH_CHECK(!user_key(name_p256, blob, used, &type));

    /* A P-256 blob that names another curve, and one that names P-256 with a P-384 point. */
    used = put_string(blob, name_p256, 19u);
    used += put_string(blob + used, "nistp384", 8u);
    used += put_string(blob + used, material, 65u);
    SSH_CHECK(!user_key(name_p256, blob, used, &type));
    used = put_string(blob, name_p256, 19u);
    used += put_string(blob + used, "nistp256", 8u);
    used += put_string(blob + used, material, 97u);
    SSH_CHECK(!user_key(name_p256, blob, used, &type));

    /* Security keys: an accepted key followed by an application string. */
    used = put_string(blob, "sk-ssh-ed25519@openssh.com", 26u);
    used += put_string(blob + used, material + 1, 32u);
    used += put_string(blob + used, ssh_prefix, 4u);
    SSH_CHECK(!user_key("sk-ssh-ed25519@openssh.com", blob, used, &type));
    SSH_CHECK(!user_key(name_ed25519, blob, used, &type));
    used = put_string(blob, "sk-ecdsa-sha2-nistp256@openssh.com", 34u);
    used += put_string(blob + used, "nistp256", 8u);
    used += put_string(blob + used, material, 65u);
    used += put_string(blob + used, ssh_prefix, 4u);
    SSH_CHECK(!user_key("sk-ecdsa-sha2-nistp256@openssh.com", blob, used, &type));
    SSH_CHECK(!user_key(name_p256, blob, used, &type));

    /* Certificates, and Ed448. */
    SSH_CHECK(
        !user_key("ssh-ed25519-cert-v01@openssh.com", ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(
        !user_key("ecdsa-sha2-nistp256-cert-v01@openssh.com", p256_blob, sizeof(p256_blob), &type));
    used = put_string(blob, "ssh-ed448", 9u);
    used += put_string(blob + used, material + 1, 57u);
    SSH_CHECK(!user_key("ssh-ed448", blob, used, &type));

    /* Each accepted key under the other's name, and names that differ only in case or length. */
    SSH_CHECK(!user_key(name_p256, ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(!user_key(name_ed25519, p256_blob, sizeof(p256_blob), &type));
    SSH_CHECK(!user_key("SSH-ED25519", ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(!user_key("ssh-ed2551", ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(!user_key("ssh-ed255190", ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(!user_key("", ed25519_blob, sizeof(ed25519_blob), &type));
    SSH_CHECK(type == no_type);
}

static void test_malformed_accepted_types(void)
{
    uint8_t blob[200];
    size_t used;
    crux_ssh_key_type_t type = no_type;

    /* A compressed point, and an uncompressed point marked as compressed. */
    used = put_string(blob, name_p256, 19u);
    used += put_string(blob + used, "nistp256", 8u);
    memcpy(blob + used, p256_blob + 35, 4u + 65u);
    blob[used + 4u] = 0x02;
    SSH_CHECK(!user_key(name_p256, blob, used + 4u + 65u, &type));
    put_u32(blob + used, 33u);
    SSH_CHECK(!user_key(name_p256, blob, used + 4u + 33u, &type));

    /* Ed25519 keys of 31 and 33 bytes. */
    for (size_t length = 31u; length <= 33u; length += 2u) {
        used = put_string(blob, name_ed25519, 11u);
        used += put_u32(blob + used, (uint32_t)length);
        memset(blob + used, 0x42, length);
        used += length;
        SSH_CHECK(!user_key(name_ed25519, blob, used, &type));
    }

    /* A byte after the key, and lengths that point past the end or wrap. */
    memcpy(blob, ed25519_blob, sizeof(ed25519_blob));
    blob[sizeof(ed25519_blob)] = 0x00;
    SSH_CHECK(!user_key(name_ed25519, blob, sizeof(ed25519_blob) + 1u, &type));
    static const uint32_t lengths[] = {0u,          10u,         12u,        0x7fffffffu,
                                       0x80000000u, 0xffffffffu, 0xfffffff5u};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        memcpy(blob, ed25519_blob, sizeof(ed25519_blob));
        put_u32(blob, lengths[i]);
        SSH_CHECK(!user_key(name_ed25519, blob, sizeof(ed25519_blob), &type));
        memcpy(blob, ed25519_blob, sizeof(ed25519_blob));
        put_u32(blob + 15, lengths[i]);
        SSH_CHECK(!user_key(name_ed25519, blob, sizeof(ed25519_blob), &type));
        memcpy(blob, p256_blob, sizeof(p256_blob));
        put_u32(blob + 23, lengths[i]);
        SSH_CHECK(!user_key(name_p256, blob, sizeof(p256_blob), &type));
        memcpy(blob, p256_blob, sizeof(p256_blob));
        put_u32(blob + 35, lengths[i]);
        SSH_CHECK(!user_key(name_p256, blob, sizeof(p256_blob), &type));
    }
    SSH_CHECK(type == no_type);
}

static void test_every_truncation_and_flip(void)
{
    const struct {
        const char *name;
        const uint8_t *blob;
        size_t length;
    } keys[] = {{name_ed25519, ed25519_blob, sizeof(ed25519_blob)},
                {name_p256, p256_blob, sizeof(p256_blob)}};

    for (size_t k = 0; k < 2u; ++k) {
        uint8_t blob[CRUX_SSH_WIRE_KEY_BLOB_MAX + 1u];
        const uint8_t *name = (const uint8_t *)keys[k].name;
        const size_t name_length = strlen(keys[k].name);

        for (size_t length = 0; length < keys[k].length; ++length) {
            check_user_key_against_oracle(name, name_length, keys[k].blob, length);
        }
        for (size_t at = 0; at < keys[k].length; ++at) {
            static const uint8_t flips[] = {0x01, 0x80, 0xff};
            for (size_t f = 0; f < sizeof(flips); ++f) {
                memcpy(blob, keys[k].blob, keys[k].length);
                blob[at] ^= flips[f];
                check_user_key_against_oracle(name, name_length, blob, keys[k].length);
            }
        }
        for (size_t at = 0; at < name_length; ++at) {
            uint8_t flipped[32];
            memcpy(flipped, name, name_length);
            flipped[at] ^= 0x20u;
            check_user_key_against_oracle(flipped, name_length, keys[k].blob, keys[k].length);
            check_user_key_against_oracle(name, at, keys[k].blob, keys[k].length);
        }
    }
}

static size_t mutate(uint8_t *bytes, size_t length, size_t capacity)
{
    unsigned operations = 1u + next_random() % 4u;

    for (unsigned op = 0; op < operations; ++op) {
        size_t at = length == 0u ? 0u : next_random() % length;
        switch (next_random() % 7u) {
        case 0: /* Flip a bit. */
            if (length > 0u) {
                bytes[at] ^= (uint8_t)(1u << (next_random() % 8u));
            }
            break;
        case 1: /* Replace a byte. */
            if (length > 0u) {
                bytes[at] = (uint8_t)next_random();
            }
            break;
        case 2: /* Insert a byte. */
            if (length < capacity) {
                memmove(bytes + at + 1u, bytes + at, length - at);
                bytes[at] = (uint8_t)next_random();
                ++length;
            }
            break;
        case 3: /* Delete a byte. */
            if (length > 0u) {
                memmove(bytes + at, bytes + at + 1u, length - at - 1u);
                --length;
            }
            break;
        case 4: /* Truncate. */
            length = length == 0u ? 0u : next_random() % length;
            break;
        case 5: /* Append. */
            while (length < capacity && next_random() % 4u != 0u) {
                bytes[length++] = (uint8_t)next_random();
            }
            break;
        default: { /* Overwrite a length field with a value near a boundary. */
            static const uint32_t values[] = {0u,  1u,  4u,  8u,          11u,
                                              19u, 32u, 65u, 0x7fffffffu, 0xffffffffu};
            uint32_t value = values[next_random() % (sizeof(values) / sizeof(values[0]))];
            if (length >= 4u && at <= length - 4u) {
                put_u32(bytes + at, value + (next_random() % 3u) - 1u);
            }
            break;
        }
        }
    }
    return length;
}

static void test_user_keys_random(void)
{
    static const char *names[] = {name_ed25519, name_p256, "ssh-rsa", "rsa-sha2-256"};
    unsigned accepted = 0;

    for (unsigned round = 0; round < WIRE_TEST_ROUNDS; ++round) {
        uint8_t blob[256];
        uint8_t name[40];
        size_t length;
        size_t name_length;
        const bool p256 = next_random() % 2u != 0u;

        length = p256 ? sizeof(p256_blob) : sizeof(ed25519_blob);
        memcpy(blob, p256 ? p256_blob : ed25519_blob, length);
        length = mutate(blob, length, sizeof(blob));
        const char *chosen = (next_random() % 8u) == 0u ? names[next_random() % 4u]
                                                        : (p256 ? name_p256 : name_ed25519);
        name_length = strlen(chosen);
        memcpy(name, chosen, name_length);
        if (next_random() % 16u == 0u) {
            name_length = mutate(name, name_length, sizeof(name));
        }
        check_user_key_against_oracle(name, name_length, blob, length);
        accepted += oracle_user_key(name, name_length, blob, length) != 0;
    }
    /* The mutations must leave some keys valid, or they test the refusals alone. */
    SSH_CHECK(accepted > WIRE_TEST_ROUNDS / 100u);
    SSH_CHECK(accepted < WIRE_TEST_ROUNDS);
}

/* ---- Requests. ---- */

static bool exec_command(const uint8_t *data, size_t length, size_t max, const uint8_t **command,
                         size_t *command_length)
{
    uint8_t *copy = exact_copy(data, length);
    bool accepted = crux_ssh_wire_exec_command(copy, length, max, command, command_length);

    /* The command must point into the copy, so translate it back before the copy goes. */
    if (accepted) {
        *command = data + (*command - copy);
    }
    free(copy);
    return accepted;
}

static bool oracle_exec(const uint8_t *data, size_t length, size_t max)
{
    if (length < 4u) {
        return false;
    }
    uint32_t claimed = ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
                       ((uint32_t)data[2] << 8u) | data[3];
    return claimed == length - 4u && claimed <= max && memchr(data + 4, 0, claimed) == NULL;
}

static void test_exec_requests(void)
{
    uint8_t data[1100];
    const uint8_t *command = NULL;
    size_t command_length = 0;
    size_t used = put_string(data, "status", 6u);

    SSH_CHECK(exec_command(data, used, 1023u, &command, &command_length));
    SSH_CHECK(command == data + 4 && command_length == 6u);

    /* Empty, which the provider passes on, and the limit. */
    used = put_u32(data, 0u);
    SSH_CHECK(exec_command(data, used, 1023u, &command, &command_length));
    SSH_CHECK(command_length == 0u);
    memset(data + 4, 'x', 1024u);
    put_u32(data, 1023u);
    SSH_CHECK(exec_command(data, 4u + 1023u, 1023u, &command, &command_length));
    put_u32(data, 1024u);
    SSH_CHECK(!exec_command(data, 4u + 1024u, 1023u, &command, &command_length));

    /* A NUL, a string that does not fill the data, or one that runs past it. */
    used = put_string(data, "sta\0tus", 7u);
    SSH_CHECK(!exec_command(data, used, 1023u, &command, &command_length));
    used = put_string(data, "status", 6u);
    data[used] = 'x';
    SSH_CHECK(!exec_command(data, used + 1u, 1023u, &command, &command_length));
    SSH_CHECK(!exec_command(data, used - 1u, 1023u, &command, &command_length));
    for (size_t length = 0; length < 4u; ++length) {
        SSH_CHECK(!exec_command(data, length, 1023u, &command, &command_length));
    }
    put_u32(data, 0xffffffffu);
    SSH_CHECK(!exec_command(data, used, 1023u, &command, &command_length));
    put_u32(data, 0xfffffffcu);
    SSH_CHECK(!exec_command(data, used, 0xffffffffu, &command, &command_length));

    SSH_CHECK(!crux_ssh_wire_exec_command(NULL, used, 1023u, &command, &command_length));
    SSH_CHECK(!crux_ssh_wire_exec_command(data, used, 1023u, NULL, &command_length));
    SSH_CHECK(!crux_ssh_wire_exec_command(data, used, 1023u, &command, NULL));
}

static void test_exec_requests_random(void)
{
    for (unsigned round = 0; round < WIRE_TEST_ROUNDS / 2u; ++round) {
        uint8_t data[96];
        const uint8_t *command = NULL;
        size_t command_length = 0;
        size_t max = next_random() % 64u;
        size_t length = put_string(data, "account list", 12u);

        length = mutate(data, length, sizeof(data));
        bool accepted = exec_command(data, length, max, &command, &command_length);
        SSH_CHECK(accepted == oracle_exec(data, length, max));
        SSH_CHECK(!accepted || (command == data + 4 && command_length == length - 4u));
    }
}

static bool subsystem(const uint8_t *data, size_t length)
{
    uint8_t *copy = exact_copy(data, length);
    bool accepted = crux_ssh_wire_subsystem_is_sftp(copy, length);

    free(copy);
    return accepted;
}

static void test_subsystem_requests(void)
{
    static const uint8_t sftp[] = {0, 0, 0, 4, 's', 'f', 't', 'p'};
    uint8_t data[32];
    size_t used;

    SSH_CHECK(subsystem(sftp, sizeof(sftp)));
    SSH_CHECK(!crux_ssh_wire_subsystem_is_sftp(NULL, sizeof(sftp)));
    for (size_t length = 0; length < sizeof(sftp); ++length) {
        SSH_CHECK(!subsystem(sftp, length));
    }
    for (size_t at = 0; at < sizeof(sftp); ++at) {
        memcpy(data, sftp, sizeof(sftp));
        data[at] ^= 0x20u;
        SSH_CHECK(!subsystem(data, sizeof(sftp)));
    }
    static const char *const others[] = {"SFTP", "sftp2", "sft", "scp", "shell", "sftp\0"};
    static const size_t lengths[] = {4u, 5u, 3u, 3u, 5u, 5u};
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i) {
        used = put_string(data, others[i], lengths[i]);
        SSH_CHECK(!subsystem(data, used));
    }
    memcpy(data, sftp, sizeof(sftp));
    data[sizeof(sftp)] = 0;
    SSH_CHECK(!subsystem(data, sizeof(sftp) + 1u));

    for (unsigned round = 0; round < WIRE_TEST_ROUNDS / 4u; ++round) {
        size_t length;

        memcpy(data, sftp, sizeof(sftp));
        length = mutate(data, sizeof(sftp), sizeof(data));
        SSH_CHECK(subsystem(data, length) ==
                  (length == sizeof(sftp) && memcmp(data, sftp, sizeof(sftp)) == 0));
    }
}

/* ---- Host keys. ---- */

static void host_key_of(uint8_t *key, const uint8_t *blob, size_t header, size_t public_length)
{
    memset(key, 0x11, CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES);
    memcpy(key + CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES, blob + header - (public_length == 65u),
           public_length);
}

static void test_host_key_blob_matches_openssh(void)
{
    uint8_t ed_key[64];
    uint8_t p256_key[97];
    uint8_t blob[CRUX_SSH_WIRE_KEY_BLOB_MAX];
    size_t length = 1u;

    host_key_of(ed_key, ed25519_blob, ED25519_HEADER, 32u);
    host_key_of(p256_key, p256_blob, P256_HEADER, 65u);
    SSH_CHECK(crux_ssh_wire_host_key_form(CRUX_SSH_KEY_ED25519, ed_key, sizeof(ed_key)));
    SSH_CHECK(crux_ssh_wire_host_key_form(CRUX_SSH_KEY_ECDSA_P256, p256_key, sizeof(p256_key)));

    SSH_CHECK(crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ED25519, ed_key, sizeof(ed_key), blob,
                                          sizeof(blob), &length));
    SSH_CHECK(length == sizeof(ed25519_blob) && memcmp(blob, ed25519_blob, length) == 0);
    SSH_CHECK(crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ECDSA_P256, p256_key, sizeof(p256_key), blob,
                                          sizeof(blob), &length));
    SSH_CHECK(length == sizeof(p256_blob) && memcmp(blob, p256_blob, length) == 0);

    /* The private part never reaches the blob. */
    memset(ed_key, 0xee, CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES);
    SSH_CHECK(crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ED25519, ed_key, sizeof(ed_key), blob,
                                          sizeof(blob), &length));
    SSH_CHECK(memcmp(blob, ed25519_blob, length) == 0);

    /* Too small a buffer, by one byte. */
    SSH_CHECK(!crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ED25519, ed_key, sizeof(ed_key), blob,
                                           sizeof(ed25519_blob) - 1u, &length));
    SSH_CHECK(length == 0u);
    SSH_CHECK(!crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ECDSA_P256, p256_key, sizeof(p256_key),
                                           blob, sizeof(p256_blob) - 1u, &length));
    SSH_CHECK(!crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ED25519, ed_key, sizeof(ed_key), NULL,
                                           sizeof(blob), &length));
    SSH_CHECK(!crux_ssh_wire_host_key_blob(CRUX_SSH_KEY_ED25519, ed_key, sizeof(ed_key), blob,
                                           sizeof(blob), NULL));
}

static void test_host_key_forms(void)
{
    uint8_t key[200];
    const crux_ssh_key_type_t types[] = {no_type, CRUX_SSH_KEY_ED25519, CRUX_SSH_KEY_ECDSA_P256,
                                         next_type, negative_type};
    uint8_t blob[CRUX_SSH_WIRE_KEY_BLOB_MAX];
    size_t blob_length = 0;

    memset(key, 0x04, sizeof(key));
    for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); ++t) {
        for (size_t length = 0; length < sizeof(key); ++length) {
            const bool expected = (types[t] == CRUX_SSH_KEY_ED25519 && length == 64u) ||
                                  (types[t] == CRUX_SSH_KEY_ECDSA_P256 && length == 97u);
            uint8_t *copy = exact_copy(key, length);

            SSH_CHECK(crux_ssh_wire_host_key_form(types[t], copy, length) == expected);
            SSH_CHECK(crux_ssh_wire_host_key_blob(types[t], copy, length, blob, sizeof(blob),
                                                  &blob_length) == expected);
            free(copy);
        }
    }
    /* A P-256 public part that is not an uncompressed point. */
    for (unsigned prefix = 0; prefix < 256u; ++prefix) {
        key[CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES] = (uint8_t)prefix;
        SSH_CHECK(crux_ssh_wire_host_key_form(CRUX_SSH_KEY_ECDSA_P256, key, 97u) ==
                  (prefix == 0x04u));
    }
    SSH_CHECK(!crux_ssh_wire_host_key_form(CRUX_SSH_KEY_ED25519, NULL, 64u));
}

/* ---- Fingerprints. ---- */

static void oracle_base64(const uint8_t *data, size_t length, char *out)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;

    for (size_t i = 0; i < length; i += 3u) {
        uint32_t group = (uint32_t)data[i] << 16u;
        size_t present = length - i < 3u ? length - i : 3u;

        if (present > 1u) {
            group |= (uint32_t)data[i + 1u] << 8u;
        }
        if (present > 2u) {
            group |= data[i + 2u];
        }
        for (size_t c = 0; c < present + 1u; ++c) {
            out[o++] = alphabet[(group >> (18u - 6u * c)) & 0x3fu];
        }
    }
    out[o] = '\0';
}

static void test_fingerprints(void)
{
    char text[CRUX_SSH_WIRE_FINGERPRINT_SIZE + 8u];
    char expected[CRUX_SSH_WIRE_FINGERPRINT_SIZE + 8u];

    SSH_CHECK(crux_ssh_wire_fingerprint_text(ed25519_digest, text, sizeof(text)));
    SSH_CHECK(strcmp(text, ed25519_fingerprint) == 0);
    SSH_CHECK(crux_ssh_wire_fingerprint_text(p256_digest, text, CRUX_SSH_WIRE_FINGERPRINT_SIZE));
    SSH_CHECK(strcmp(text, p256_fingerprint) == 0);
    SSH_CHECK(strlen(text) == CRUX_SSH_WIRE_FINGERPRINT_SIZE - 1u);

    memset(text, 'z', sizeof(text));
    SSH_CHECK(
        !crux_ssh_wire_fingerprint_text(p256_digest, text, CRUX_SSH_WIRE_FINGERPRINT_SIZE - 1u));
    SSH_CHECK(text[0] == '\0');
    SSH_CHECK(!crux_ssh_wire_fingerprint_text(NULL, text, sizeof(text)));
    SSH_CHECK(!crux_ssh_wire_fingerprint_text(p256_digest, NULL, sizeof(text)));

    for (unsigned round = 0; round < 2000u; ++round) {
        uint8_t digest[CRUX_SSH_WIRE_DIGEST_BYTES];

        for (size_t i = 0; i < sizeof(digest); ++i) {
            digest[i] = (uint8_t)next_random();
        }
        memcpy(expected, "SHA256:", 7u);
        oracle_base64(digest, sizeof(digest), expected + 7);
        SSH_CHECK(crux_ssh_wire_fingerprint_text(digest, text, CRUX_SSH_WIRE_FINGERPRINT_SIZE));
        SSH_CHECK(strcmp(text, expected) == 0);
    }
}

int main(void)
{
    test_user_names();
    test_user_names_random();
    test_accepted_keys();
    test_refused_key_types();
    test_malformed_accepted_types();
    test_every_truncation_and_flip();
    test_user_keys_random();
    test_exec_requests();
    test_exec_requests_random();
    test_subsystem_requests();
    test_host_key_blob_matches_openssh();
    test_host_key_forms();
    test_fingerprints();
    if (ssh_check_failures != 0u) {
        printf("%u failures\n", ssh_check_failures);
        return 1;
    }
    printf("SSH wire checks: all pass, with %u random rounds per parser.\n", WIRE_TEST_ROUNDS);
    return 0;
}
