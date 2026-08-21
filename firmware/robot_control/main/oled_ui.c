#include "oled_ui.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define OLED_I2C_SDA GPIO_NUM_47
#define OLED_I2C_SCL GPIO_NUM_21
#define OLED_I2C_SPEED_HZ 100000
#define OLED_TIMEOUT_MS 100

static const char *TAG = "oled_ui";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_mutex;
static bool s_bitbang;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGES];
static oled_ui_snapshot_t s_state = {
    .expression = OLED_EXPRESSION_IDLE,
};

static void bitbang_scl(int level)
{
    gpio_set_level(OLED_I2C_SCL, level);
    esp_rom_delay_us(4);
}

static void bitbang_sda(int level)
{
    gpio_set_level(OLED_I2C_SDA, level);
    esp_rom_delay_us(4);
}

static void bitbang_write_byte(uint8_t value)
{
    for (int bit = 0; bit < 8; ++bit) {
        bitbang_scl(0);
        bitbang_sda((value & 0x80U) != 0U);
        value <<= 1U;
        bitbang_scl(1);
    }
    bitbang_scl(0);
    bitbang_sda(1);
    bitbang_scl(1);
    bitbang_scl(0);
}

static void bitbang_start(uint8_t control_byte)
{
    bitbang_sda(1);
    bitbang_scl(1);
    bitbang_sda(0);
    bitbang_scl(0);
    bitbang_write_byte(0x78);
    bitbang_write_byte(control_byte);
}

static void bitbang_stop(void)
{
    bitbang_sda(0);
    bitbang_scl(1);
    bitbang_sda(1);
}

static esp_err_t configure_bitbang(void)
{
    if (s_bus != NULL) {
        const esp_err_t result = i2c_del_master_bus(s_bus);
        if (result != ESP_OK) {
            return result;
        }
        s_bus = NULL;
    }
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << OLED_I2C_SDA) | (1ULL << OLED_I2C_SCL),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&config);
    if (result == ESP_OK) {
        result = gpio_set_level(OLED_I2C_SDA, 1);
    }
    if (result == ESP_OK) {
        result = gpio_set_level(OLED_I2C_SCL, 1);
    }
    return result;
}

static esp_err_t send_command_bytes(const uint8_t *commands, size_t length)
{
    if (s_bitbang) {
        bitbang_start(0x00);
        for (size_t index = 0; index < length; ++index) {
            bitbang_write_byte(commands[index]);
        }
        bitbang_stop();
        return ESP_OK;
    }
    uint8_t buffer[32];
    if (length + 1U > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = 0x00;
    memcpy(&buffer[1], commands, length);
    return i2c_master_transmit(s_device, buffer, length + 1U, OLED_TIMEOUT_MS);
}

static esp_err_t send_framebuffer(void)
{
    if (s_bitbang) {
        for (uint8_t page = 0; page < OLED_PAGES; ++page) {
            const uint8_t page_commands[] = {
                (uint8_t)(0xB0U + page), 0x10, 0x00,
            };
            send_command_bytes(page_commands, sizeof(page_commands));
            bitbang_start(0x40);
            for (size_t column = 0; column < OLED_WIDTH; ++column) {
                bitbang_write_byte(s_framebuffer[(size_t)page * OLED_WIDTH +
                                                  column]);
            }
            bitbang_stop();
        }
        return ESP_OK;
    }
    static const uint8_t address_commands[] = {
        0x21, 0x00, OLED_WIDTH - 1,
        0x22, 0x00, OLED_PAGES - 1,
    };
    esp_err_t result = send_command_bytes(address_commands,
                                          sizeof(address_commands));
    uint8_t buffer[33];
    buffer[0] = 0x40;
    for (size_t offset = 0; result == ESP_OK && offset < sizeof(s_framebuffer);
         offset += sizeof(buffer) - 1U) {
        memcpy(&buffer[1], &s_framebuffer[offset], sizeof(buffer) - 1U);
        result = i2c_master_transmit(s_device,
                                     buffer,
                                     sizeof(buffer),
                                     OLED_TIMEOUT_MS);
    }
    return result;
}

static void pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    const size_t index = (size_t)x + (size_t)(y / 8) * OLED_WIDTH;
    const uint8_t mask = (uint8_t)(1U << (y & 7));
    if (on) {
        s_framebuffer[index] |= mask;
    } else {
        s_framebuffer[index] &= (uint8_t)~mask;
    }
}

static void line(int x0, int y0, int x1, int y1)
{
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void circle(int center_x, int center_y, int radius, bool filled)
{
    const int radius_squared = radius * radius;
    const int inner_squared = (radius - 2) * (radius - 2);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            const int distance = x * x + y * y;
            if (distance <= radius_squared &&
                (filled || distance >= inner_squared)) {
                pixel(center_x + x, center_y + y, true);
            }
        }
    }
}

static void draw_eye(int x, int y, int pupil_offset)
{
    circle(x, y, 9, false);
    circle(x + pupil_offset, y, 3, true);
}

static void draw_smile(bool open)
{
    for (int x = -24; x <= 24; ++x) {
        const int y = 42 + (x * x) / 110;
        pixel(64 + x, y, true);
        if (open) {
            pixel(64 + x, y + 1, true);
        }
    }
}

static void draw_expression(oled_expression_id_t expression)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    switch (expression) {
    case OLED_EXPRESSION_HAPPY:
        line(30, 25, 38, 19);
        line(38, 19, 46, 25);
        line(82, 25, 90, 19);
        line(90, 19, 98, 25);
        draw_smile(true);
        break;
    case OLED_EXPRESSION_CURIOUS:
        circle(39, 24, 11, false);
        circle(39, 24, 3, true);
        circle(89, 24, 6, false);
        circle(89, 24, 2, true);
        circle(64, 48, 7, false);
        break;
    case OLED_EXPRESSION_CONFUSED:
        line(29, 14, 48, 19);
        line(80, 19, 99, 14);
        draw_eye(39, 28, 0);
        draw_eye(89, 28, 0);
        line(40, 50, 52, 45);
        line(52, 45, 64, 50);
        line(64, 50, 76, 45);
        line(76, 45, 88, 50);
        break;
    case OLED_EXPRESSION_SLEEPY:
        line(29, 25, 47, 25);
        line(81, 25, 99, 25);
        line(48, 49, 80, 49);
        line(103, 14, 115, 14);
        line(115, 14, 103, 24);
        line(103, 24, 115, 24);
        break;
    case OLED_EXPRESSION_WATCHING:
        draw_eye(39, 25, 4);
        draw_eye(89, 25, 4);
        line(50, 49, 78, 49);
        break;
    case OLED_EXPRESSION_WARNING:
        line(27, 14, 49, 21);
        line(79, 21, 101, 14);
        draw_eye(39, 29, 0);
        draw_eye(89, 29, 0);
        line(48, 51, 80, 51);
        break;
    case OLED_EXPRESSION_EXCITED:
        line(30, 16, 47, 31);
        line(47, 16, 30, 31);
        line(81, 16, 98, 31);
        line(98, 16, 81, 31);
        circle(64, 47, 12, false);
        break;
    case OLED_EXPRESSION_IDLE:
    default:
        draw_eye(39, 25, 0);
        draw_eye(89, 25, 0);
        line(52, 49, 76, 49);
        break;
    }
}

esp_err_t oled_ui_init(void)
{
    s_bitbang = false;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_I2C_SDA,
        .scl_io_num = OLED_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t result = i2c_new_master_bus(&bus_config, &s_bus);
    if (result != ESP_OK) {
        return result;
    }

    const uint8_t candidate_addresses[] = {0x3C, 0x3D};
    uint8_t address = 0;
    for (size_t index = 0; index < sizeof(candidate_addresses); ++index) {
        if (i2c_master_probe(s_bus,
                             candidate_addresses[index],
                             OLED_TIMEOUT_MS) == ESP_OK) {
            address = candidate_addresses[index];
            break;
        }
    }
    if (address == 0) {
        ESP_LOGI(TAG,
                 "I2C baseline probes: TOF(0x29)=%s MPU6050(0x68)=%s",
                 i2c_master_probe(s_bus, 0x29, OLED_TIMEOUT_MS) == ESP_OK
                     ? "ACK"
                     : "none",
                 i2c_master_probe(s_bus, 0x68, OLED_TIMEOUT_MS) == ESP_OK
                     ? "ACK"
                     : "none");
        ESP_LOGI(TAG,
                 "OLED has no ACK; enabling vendor-compatible bit-bang mode");
        const esp_err_t bitbang_result = configure_bitbang();
        if (bitbang_result != ESP_OK) {
            return bitbang_result;
        }
        s_bitbang = true;
        address = 0x3C;
    }

    if (!s_bitbang) {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = OLED_I2C_SPEED_HZ,
        };
        result = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
        if (result != ESP_OK) {
            return result;
        }
    }

    static const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x10, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xF0, 0xD9, 0x71, 0xDB, 0x00, 0xA4, 0xA6,
        0xAF,
    };
    result = send_command_bytes(init_commands, sizeof(init_commands));
    if (result != ESP_OK) {
        return result;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_state = (oled_ui_snapshot_t){
        .initialized = true,
        .i2c_address = address,
        .expression = OLED_EXPRESSION_IDLE,
    };
    draw_expression(OLED_EXPRESSION_IDLE);
    result = send_framebuffer();
    if (result != ESP_OK) {
        s_state.initialized = false;
        return result;
    }
    ESP_LOGI(TAG,
             "SSD1309 initialized at address 0x%02X mode=%s",
             address,
             s_bitbang ? "vendor-bitbang" : "i2c");
    return ESP_OK;
}

esp_err_t oled_set_expression(const char *expression_name)
{
    oled_expression_id_t expression;
    if (!oled_expression_from_name(expression_name, &expression)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    draw_expression(expression);
    const esp_err_t result = send_framebuffer();
    if (result == ESP_OK) {
        s_state.expression = expression;
    }
    xSemaphoreGive(s_mutex);
    return result;
}

oled_ui_snapshot_t oled_ui_get_snapshot(void)
{
    oled_ui_snapshot_t snapshot = {
        .expression = OLED_EXPRESSION_IDLE,
    };
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    }
    return snapshot;
}
