#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "tts_protocol.h"

typedef struct {
    bool initialized;
    bool busy;
    bool has_last_phrase;
    tts_phrase_id_t last_phrase;
    uint32_t request_count;
} tts_control_snapshot_t;

esp_err_t tts_control_init(void);
esp_err_t tts_speak(const char *text);
esp_err_t tts_play_phrase(tts_phrase_id_t phrase);
tts_control_snapshot_t tts_control_get_snapshot(void);
