#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "stream_protocol.h"

static void formats_complete_multipart_header(void)
{
    char header[256];
    const char expected[] =
        "--d2frame\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 5232\r\n"
        "X-Frame-Id: 42\r\n"
        "X-Capture-Timestamp-Us: 123456789\r\n"
        "\r\n";

    const int length = mjpeg_stream_format_part_header(
        header, sizeof(header), 5232U, 42U, 123456789);

    assert(length == (int)strlen(expected));
    assert(strcmp(header, expected) == 0);
}

static void rejects_truncated_multipart_header(void)
{
    char header[24];
    assert(mjpeg_stream_format_part_header(
               header, sizeof(header), 5232U, UINT64_MAX, INT64_MAX) == -1);
}

int main(void)
{
    assert(strcmp(MJPEG_STREAM_CONTENT_TYPE,
                  "multipart/x-mixed-replace; boundary=d2frame") == 0);
    formats_complete_multipart_header();
    rejects_truncated_multipart_header();
    return 0;
}
