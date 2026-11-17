/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Fixed-size serialisation of the attempt record. THE PROPOSAL DOES NOT SPECIFY ONE: it says the
 * update agent persists the record in its own storage and that where and how is the update agent's
 * design (proposal, Q2). This codec is a prototype choice made so that a torn or corrupted record
 * can be tested. See prototype-notes.md, P1. It carries no contract name on purpose.
 *
 * Layout, 16 bytes, little endian:
 *   0 format (1)   1 slot   2 major   3 minor   4-5 revision   6-9 build   10 requested
 *   11 reserved (0)   12-15 CRC-32 (IEEE, reflected) of bytes 0 to 11
 */
#ifndef CRUX_IMAGE_STATE_PROTO_ATTEMPT_CODEC_H
#define CRUX_IMAGE_STATE_PROTO_ATTEMPT_CODEC_H

#include "image_state_proto.h"

#define PROTO_ATTEMPT_SIZE 16u
#define PROTO_ATTEMPT_FORMAT 1u

typedef enum {
    PROTO_CODEC_OK = 0,
    PROTO_CODEC_ERR_ARGUMENT = -1,
    PROTO_CODEC_ERR_SIZE = -2,   /* encode: buffer too small. decode: not exactly 16 bytes */
    PROTO_CODEC_ERR_FORMAT = -3, /* unknown format byte */
    PROTO_CODEC_ERR_CRC = -4,
    PROTO_CODEC_ERR_FIELD = -5 /* a field is outside its range, with a valid CRC */
} proto_codec_result_t;

uint32_t proto_crc32(const uint8_t *data, size_t length);
proto_codec_result_t proto_attempt_encode(const crux_image_attempt_t *attempt, uint8_t *buffer,
                                          size_t capacity);
proto_codec_result_t proto_attempt_decode(const uint8_t *buffer, size_t length,
                                          crux_image_attempt_t *attempt);

#endif /* CRUX_IMAGE_STATE_PROTO_ATTEMPT_CODEC_H */
