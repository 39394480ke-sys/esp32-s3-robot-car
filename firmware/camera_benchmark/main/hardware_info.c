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

static void log_heap(const char *label, uint32_t capabilities)
{
    ESP_LOGI(TAG,
             "%s heap: free=%u min_free=%u largest=%u",
             label,
             (unsigned)heap_caps_get_free_size(capabilities),
             (unsigned)heap_caps_get_minimum_free_size(capabilities),
             (unsigned)heap_caps_get_largest_free_block(capabilities));
}

static uint8_t psram_test_pattern(size_t index)
{
    return (uint8_t)((index * 33U + 17U) & 0xffU);
}

static bool verify_psram_allocation(void)
{
    uint8_t *buffer = heap_caps_malloc(
        PSRAM_TEST_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "PSRAM allocation test failed: could not allocate %u bytes",
                 (unsigned)PSRAM_TEST_SIZE);
        return false;
    }

    for (size_t index = 0; index < PSRAM_TEST_SIZE; ++index) {
        buffer[index] = psram_test_pattern(index);
    }

    for (size_t index = 0; index < PSRAM_TEST_SIZE; ++index) {
        const uint8_t expected = psram_test_pattern(index);
        if (buffer[index] != expected) {
            ESP_LOGE(TAG,
                     "PSRAM read/write mismatch at offset=%u expected=0x%02x actual=0x%02x",
                     (unsigned)index,
                     expected,
                     buffer[index]);
            free(buffer);
            return false;
        }
    }

    free(buffer);
    ESP_LOGI(TAG, "PSRAM allocation/read/write test: PASS (%u bytes)",
             (unsigned)PSRAM_TEST_SIZE);
    return true;
}

bool hardware_info_run_probe(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;
    const esp_app_desc_t *app = esp_app_get_description();

    esp_chip_info(&chip_info);
    const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "firmware: project=%s version=%s build=%s %s",
             app->project_name, app->version, app->date, app->time);
    ESP_LOGI(TAG, "environment: idf=%s target=%s", esp_get_idf_version(), CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "chip: model=%d revision=%d.%d cores=%u features=0x%08" PRIx32,
             chip_info.model,
             chip_info.revision / 100,
             chip_info.revision % 100,
             chip_info.cores,
             chip_info.features);

    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "flash: detected=%" PRIu32 " bytes (%" PRIu32 " MB)",
                 flash_size, flash_size / (1024U * 1024U));
    } else {
        ESP_LOGE(TAG, "flash size detection failed: %s", esp_err_to_name(flash_result));
    }

    log_heap("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    const bool psram_initialized = esp_psram_is_initialized();
    const size_t psram_size = esp_psram_get_size();
    ESP_LOGI(TAG, "psram: initialized=%s size=%u bytes (%u MB)",
             psram_initialized ? "YES" : "NO",
             (unsigned)psram_size,
             (unsigned)(psram_size / (1024U * 1024U)));

    if (!psram_initialized || psram_size == 0U) {
        ESP_LOGE(TAG, "PSRAM is not initialized and usable");
        return false;
    }

    log_heap("psram before test", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool psram_test_passed = verify_psram_allocation();
    log_heap("psram after test", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    log_heap("internal after test", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    return flash_result == ESP_OK && flash_size == 16U * 1024U * 1024U &&
           psram_size == 8U * 1024U * 1024U && psram_test_passed;
}
