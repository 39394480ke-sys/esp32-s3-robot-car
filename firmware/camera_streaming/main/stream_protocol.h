#pragma once

#include <stddef.h>
#include <stdint.h>

#define MJPEG_STREAM_BOUNDARY "d2frame"
#define MJPEG_STREAM_CONTENT_TYPE "multipart/x-mixed-replace; boundary=d2frame"

int mjpeg_stream_format_part_header(char *buffer,
                                    size_t buffer_size,
                                    size_t jpeg_size,
                                    uint64_t frame_id,
                                    int64_t capture_timestamp_us);
