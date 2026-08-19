idf_component_register(
    SRCS
        "app_main.c"
        "benchmark_statistics.c"
        "camera_benchmark.c"
        "camera_init.c"
        "camera_safety_check.c"
        "hardware_info.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_psram esp_system esp_timer heap spi_flash
)
