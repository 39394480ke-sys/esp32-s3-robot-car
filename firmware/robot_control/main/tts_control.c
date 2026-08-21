#include "tts_control.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TTS_UART UART_NUM_1
#define TTS_TX_GPIO GPIO_NUM_38
#define TTS_RX_GPIO GPIO_NUM_39
#define TTS_FRAME_CAPACITY 128U
#define TTS_TX_TIMEOUT_MS 500U

static SemaphoreHandle_t s_mutex;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static tts_control_snapshot_t s_state;

static void set_busy(bool busy)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.busy = busy;
    portEXIT_CRITICAL(&s_state_mux);
}

static esp_err_t speak_locked(const char *text)
{
    uint8_t frame[TTS_FRAME_CAPACITY];
    const size_t frame_length =
        tts_protocol_build_frame(text, frame, sizeof(frame));
    if (frame_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const int written = uart_write_bytes(TTS_UART,
                                         (const char *)frame,
                                         frame_length);
    if (written != (int)frame_length) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(TTS_UART, pdMS_TO_TICKS(TTS_TX_TIMEOUT_MS));
}

esp_err_t tts_control_init(void)
{
    const uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t result = uart_driver_install(TTS_UART, 256, 0, 0, NULL, 0);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_param_config(TTS_UART, &config);
    if (result == ESP_OK) {
        result = uart_set_pin(TTS_UART,
                              TTS_TX_GPIO,
                              TTS_RX_GPIO,
                              UART_PIN_NO_CHANGE,
                              UART_PIN_NO_CHANGE);
    }
    if (result != ESP_OK) {
        uart_driver_delete(TTS_UART);
        return result;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        uart_driver_delete(TTS_UART);
        return ESP_ERR_NO_MEM;
    }
    s_state = (tts_control_snapshot_t){
        .initialized = true,
    };
    return ESP_OK;
}

esp_err_t tts_speak(const char *text)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    set_busy(true);
    const esp_err_t result = speak_locked(text);
    set_busy(false);
    xSemaphoreGive(s_mutex);
    return result;
}

esp_err_t tts_play_phrase(tts_phrase_id_t phrase)
{
    const char *text = tts_phrase_text(phrase);
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    set_busy(true);
    const esp_err_t result = speak_locked(text);
    set_busy(false);
    if (result == ESP_OK) {
        portENTER_CRITICAL(&s_state_mux);
        s_state.has_last_phrase = true;
        s_state.last_phrase = phrase;
        ++s_state.request_count;
        portEXIT_CRITICAL(&s_state_mux);
    }
    xSemaphoreGive(s_mutex);
    return result;
}

tts_control_snapshot_t tts_control_get_snapshot(void)
{
    tts_control_snapshot_t snapshot;
    portENTER_CRITICAL(&s_state_mux);
    snapshot = s_state;
    portEXIT_CRITICAL(&s_state_mux);
    return snapshot;
}
