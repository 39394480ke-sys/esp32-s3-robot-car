#include "stream_protocol.h"

#include <inttypes.h>
#include <stdio.h>

int mjpeg_stream_format_part_header(char *buffer,
                                    size_t buffer_size,
                                    size_t jpeg_size,
                                    uint64_t frame_id,
                                    int64_t capture_timestamp_us)
{
    const int length = snprintf(
        buffer,
        buffer_size,
        "--" MJPEG_STREAM_BOUNDARY "\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %zu\r\n"
        "X-Frame-Id: %" PRIu64 "\r\n"
        "X-Capture-Timestamp-Us: %" PRId64 "\r\n"
        "\r\n",
        jpeg_size,
        frame_id,
        capture_timestamp_us);

    if (length < 0 || (size_t)length >= buffer_size) {
        return -1;
    }
    return length;
}
