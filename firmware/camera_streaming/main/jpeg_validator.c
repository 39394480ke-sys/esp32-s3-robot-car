#include "jpeg_validator.h"

static bool marker_has_segment_length(uint8_t marker)
{
    return marker == 0xc0U ||
           marker == 0xc4U ||
           marker == 0xdbU ||
           marker == 0xddU ||
           marker == 0xfeU ||
           (marker >= 0xe0U && marker <= 0xefU);
}

static bool entropy_data_ends_cleanly(const uint8_t *data,
                                      size_t length,
                                      size_t offset)
{
    while (offset < length) {
        if (data[offset++] != 0xffU) {
            continue;
        }
        while (offset < length && data[offset] == 0xffU) {
            ++offset;
        }
        if (offset >= length) {
            return false;
        }

        const uint8_t marker = data[offset++];
        if (marker == 0x00U || (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        return marker == 0xd9U && offset == length;
    }
    return false;
}

static uint32_t fingerprint_bytes(const uint8_t *data, size_t length)
{
    uint32_t fingerprint = 2166136261U;
    for (size_t index = 0; index < length; ++index) {
        fingerprint ^= data[index];
        fingerprint *= 16777619U;
    }
    return fingerprint;
}

bool jpeg_data_get_header_fingerprint(const uint8_t *data,
                                      size_t length,
                                      uint32_t *fingerprint)
{
    if (data == NULL || length < 4U ||
        data[0] != 0xffU || data[1] != 0xd8U) {
        return false;
    }

    bool has_quantization_table = false;
    bool has_huffman_table = false;
    bool has_baseline_frame = false;
    size_t offset = 2U;

    while (offset < length) {
        if (data[offset++] != 0xffU) {
            return false;
        }
        while (offset < length && data[offset] == 0xffU) {
            ++offset;
        }
        if (offset >= length) {
            return false;
        }

        const uint8_t marker = data[offset++];
        if (marker == 0xdaU) {
            if (!has_quantization_table || !has_huffman_table ||
                !has_baseline_frame || offset + 2U > length) {
                return false;
            }
            const size_t segment_length =
                ((size_t)data[offset] << 8U) | data[offset + 1U];
            if (segment_length < 2U || segment_length > length - offset) {
                return false;
            }
            const size_t header_length = offset + segment_length;
            if (!entropy_data_ends_cleanly(data, length, header_length)) {
                return false;
            }
            if (fingerprint != NULL) {
                *fingerprint = fingerprint_bytes(data, header_length);
            }
            return true;
        }
        if (!marker_has_segment_length(marker) || offset + 2U > length) {
            return false;
        }

        const size_t segment_length =
            ((size_t)data[offset] << 8U) | data[offset + 1U];
        if (segment_length < 2U || segment_length > length - offset) {
            return false;
        }
        if (marker == 0xc0U && segment_length < 8U) {
            return false;
        }

        has_quantization_table |= marker == 0xdbU;
        has_huffman_table |= marker == 0xc4U;
        has_baseline_frame |= marker == 0xc0U;
        offset += segment_length;
    }
    return false;
}

bool jpeg_data_is_structurally_valid(const uint8_t *data, size_t length)
{
    return jpeg_data_get_header_fingerprint(data, length, NULL);
}
