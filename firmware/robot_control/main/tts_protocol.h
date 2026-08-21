#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TTS_PHRASE_HELLO = 0,
    TTS_PHRASE_HERE,
    TTS_PHRASE_RECEIVED,
    TTS_PHRASE_STOPPED,
    TTS_PHRASE_COUNT,
} tts_phrase_id_t;

bool tts_phrase_from_name(const char *name, tts_phrase_id_t *phrase);
const char *tts_phrase_name(tts_phrase_id_t phrase);
const char *tts_phrase_text(tts_phrase_id_t phrase);
size_t tts_protocol_build_frame(const char *text,
                                uint8_t *frame,
                                size_t frame_capacity);
