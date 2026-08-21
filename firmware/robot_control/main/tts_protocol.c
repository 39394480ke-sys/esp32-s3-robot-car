#include "tts_protocol.h"

#include <string.h>

typedef struct {
    const char *name;
    const char *text;
} phrase_definition_t;

static const phrase_definition_t PHRASES[TTS_PHRASE_COUNT] = {
    [TTS_PHRASE_HELLO] = {
        .name = "hello",
        .text = "\xE4\xBD\xA0\xE5\xA5\xBD",
    },
    [TTS_PHRASE_HERE] = {
        .name = "here",
        .text = "\xE6\x88\x91\xE5\x9C\xA8\xE8\xBF\x99\xE9\x87\x8C",
    },
    [TTS_PHRASE_RECEIVED] = {
        .name = "received",
        .text = "\xE6\x94\xB6\xE5\x88\xB0",
    },
    [TTS_PHRASE_STOPPED] = {
        .name = "stopped",
        .text = "\xE5\x81\x9C\xE4\xB8\x8B\xE6\x9D\xA5\xE4\xBA\x86",
    },
};

bool tts_phrase_from_name(const char *name, tts_phrase_id_t *phrase)
{
    if (name == NULL || phrase == NULL) {
        return false;
    }
    for (int index = 0; index < TTS_PHRASE_COUNT; ++index) {
        if (strcmp(name, PHRASES[index].name) == 0) {
            *phrase = (tts_phrase_id_t)index;
            return true;
        }
    }
    return false;
}
const char *tts_phrase_name(tts_phrase_id_t phrase)
{
    if (phrase < 0 || phrase >= TTS_PHRASE_COUNT) {
        return "unknown";
    }
    return PHRASES[phrase].name;
}

const char *tts_phrase_text(tts_phrase_id_t phrase)
{
    if (phrase < 0 || phrase >= TTS_PHRASE_COUNT) {
        return NULL;
    }
    return PHRASES[phrase].text;
}

size_t tts_protocol_build_frame(const char *text,
                                uint8_t *frame,
                                size_t frame_capacity)
{
    if (text == NULL || frame == NULL) {
        return 0U;
    }
    const size_t text_length = strlen(text);
    const size_t payload_length = text_length + 2U;
    const size_t frame_length = text_length + 5U;
    if (text_length == 0U || payload_length > UINT16_MAX ||
        frame_length > frame_capacity) {
        return 0U;
    }

    frame[0] = 0xFD;
    frame[1] = (uint8_t)(payload_length >> 8U);
    frame[2] = (uint8_t)(payload_length & 0xFFU);
    frame[3] = 0x01;
    frame[4] = 0x04;
    memcpy(&frame[5], text, text_length);
    return frame_length;
}
