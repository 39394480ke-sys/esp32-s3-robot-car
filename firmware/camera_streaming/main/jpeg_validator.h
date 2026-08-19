#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool jpeg_data_is_structurally_valid(const uint8_t *data, size_t length);
bool jpeg_data_get_header_fingerprint(const uint8_t *data,
                                      size_t length,
                                      uint32_t *fingerprint);
