#include "hardware_info.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

static const char *TAG = "hardware_info";
static const size_t PSRAM_TEST_SIZE = 256U * 1024U;

static uint8_t psram_test_pattern(size_t index)
{
    return (uint8_t)((index * 33U + 17U) & 0xffU);
}

static bool verify_psram_allocation(void)
{
    uint8_t *buffer = heap_caps_malloc(
        PSRAM_TEST_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        return false;
    }

    for (size_t index = 0U; index < PSRAM_TEST_SIZE; ++index) {
        buffer[index] = psram_test_pattern(index);
    }
    for (size_t index = 0U; index < PSRAM_TEST_SIZE; ++index) {
        if (buffer[index] != psram_test_pattern(index)) {
            free(buffer);
            return false;
        }
    }

    free(buffer);
    return true;
}

bool hardware_info_run_probe(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0U;
    esp_chip_info(&chip_info);
    const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);
    const bool psram_initialized = esp_psram_is_initialized();
    const size_t psram_size = esp_psram_get_size();

    ESP_LOGI(TAG,
             "environment: idf=%s chip_revision=%d.%d flash=%" PRIu32
             " PSRAM=%u",
             esp_get_idf_version(),
             chip_info.revision / 100,
             chip_info.revision % 100,
             flash_size,
             (unsigned)psram_size);

    const bool passed = flash_result == ESP_OK &&
                        flash_size == 16U * 1024U * 1024U &&
                        psram_initialized &&
                        psram_size == 8U * 1024U * 1024U &&
                        verify_psram_allocation();
    ESP_LOGI(TAG, "D2_HARDWARE_RESULT=%s", passed ? "PASS" : "FAIL");
    return passed;
}
