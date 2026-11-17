/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Fixed-size serialisation of the attempt record. A prototype choice, P1: the proposal gives none.
 */
#include "attempt_codec.h"

#include <stddef.h>

#define BODY_SIZE 12u

uint32_t proto_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t get_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

proto_codec_result_t proto_attempt_encode(const crux_image_attempt_t *attempt, uint8_t *buffer,
                                          size_t capacity)
{
    if (attempt == NULL || buffer == NULL) {
        return PROTO_CODEC_ERR_ARGUMENT;
    }
    if (capacity < PROTO_ATTEMPT_SIZE) {
        return PROTO_CODEC_ERR_SIZE;
    }
    if (attempt->slot >= CRUX_IMAGE_SLOT_COUNT) {
        return PROTO_CODEC_ERR_FIELD;
    }
    buffer[0] = PROTO_ATTEMPT_FORMAT;
    buffer[1] = attempt->slot;
    buffer[2] = attempt->version.major;
    buffer[3] = attempt->version.minor;
    buffer[4] = (uint8_t)(attempt->version.revision & 0xFFu);
    buffer[5] = (uint8_t)(attempt->version.revision >> 8);
    put_u32(&buffer[6], attempt->version.build);
    buffer[10] = attempt->requested ? 1u : 0u;
    buffer[11] = 0;
    put_u32(&buffer[BODY_SIZE], proto_crc32(buffer, BODY_SIZE));
    return PROTO_CODEC_OK;
}

proto_codec_result_t proto_attempt_decode(const uint8_t *buffer, size_t length,
                                          crux_image_attempt_t *attempt)
{
    if (buffer == NULL || attempt == NULL) {
        return PROTO_CODEC_ERR_ARGUMENT;
    }
    if (length != PROTO_ATTEMPT_SIZE) {
        return PROTO_CODEC_ERR_SIZE;
    }
    if (buffer[0] != PROTO_ATTEMPT_FORMAT) {
        return PROTO_CODEC_ERR_FORMAT;
    }
    if (get_u32(&buffer[BODY_SIZE]) != proto_crc32(buffer, BODY_SIZE)) {
        return PROTO_CODEC_ERR_CRC;
    }
    if (buffer[1] >= CRUX_IMAGE_SLOT_COUNT || buffer[10] > 1u || buffer[11] != 0) {
        return PROTO_CODEC_ERR_FIELD;
    }
    attempt->slot = buffer[1];
    attempt->version.major = buffer[2];
    attempt->version.minor = buffer[3];
    attempt->version.revision = (uint16_t)(buffer[4] | (buffer[5] << 8));
    attempt->version.build = get_u32(&buffer[6]);
    attempt->requested = buffer[10] == 1u;
    return PROTO_CODEC_OK;
}
