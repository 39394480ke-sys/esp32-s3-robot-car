#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "jpeg_validator.h"

static const uint8_t valid_baseline_jpeg[] = {
    0xff, 0xd8,
    0xff, 0xe0, 0x00, 0x02,
    0xff, 0xdb, 0x00, 0x02,
    0xff, 0xc4, 0x00, 0x02,
    0xff, 0xc0, 0x00, 0x08, 0x08, 0x00, 0xf0, 0x01, 0x40, 0x00,
    0xff, 0xda, 0x00, 0x02,
    0x12, 0x34, 0xff, 0x00, 0x56, 0xff, 0xd0, 0x78,
    0xff, 0xd9,
};

static void accepts_structurally_valid_baseline_jpeg(void)
{
    assert(jpeg_data_is_structurally_valid(
        valid_baseline_jpeg, sizeof(valid_baseline_jpeg)));
}

static void rejects_corrupted_dqt_marker_seen_on_hardware(void)
{
    uint8_t jpeg[sizeof(valid_baseline_jpeg)];
    for (size_t index = 0; index < sizeof(jpeg); ++index) {
        jpeg[index] = valid_baseline_jpeg[index];
    }
    jpeg[7] = 0x5b;
    assert(!jpeg_data_is_structurally_valid(jpeg, sizeof(jpeg)));
}

static void rejects_invalid_marker_inside_entropy_data(void)
{
    uint8_t jpeg[sizeof(valid_baseline_jpeg)];
    for (size_t index = 0; index < sizeof(jpeg); ++index) {
        jpeg[index] = valid_baseline_jpeg[index];
    }
    jpeg[31] = 0x42;
    assert(!jpeg_data_is_structurally_valid(jpeg, sizeof(jpeg)));
}

static void rejects_truncated_segment(void)
{
    assert(!jpeg_data_is_structurally_valid(
        valid_baseline_jpeg, sizeof(valid_baseline_jpeg) - 3U));
}

static void fingerprints_header_content_but_not_entropy_data(void)
{
    uint32_t original = 0U;
    uint32_t changed_header = 0U;
    uint32_t changed_entropy = 0U;
    uint8_t jpeg[sizeof(valid_baseline_jpeg)];

    for (size_t index = 0; index < sizeof(jpeg); ++index) {
        jpeg[index] = valid_baseline_jpeg[index];
    }
    assert(jpeg_data_get_header_fingerprint(
        jpeg, sizeof(jpeg), &original));

    jpeg[20] ^= 0x01U;
    assert(jpeg_data_get_header_fingerprint(
        jpeg, sizeof(jpeg), &changed_header));
    assert(changed_header != original);

    jpeg[20] ^= 0x01U;
    jpeg[28] ^= 0x01U;
    assert(jpeg_data_get_header_fingerprint(
        jpeg, sizeof(jpeg), &changed_entropy));
    assert(changed_entropy == original);
}

int main(void)
{
    accepts_structurally_valid_baseline_jpeg();
    rejects_corrupted_dqt_marker_seen_on_hardware();
    rejects_invalid_marker_inside_entropy_data();
    rejects_truncated_segment();
    fingerprints_header_content_but_not_entropy_data();
    return 0;
}
