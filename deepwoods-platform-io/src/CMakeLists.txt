idf_component_register(
    SRCS        "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash       # required by both Wi-Fi & BLE
        freertos
        driver
        log
        esp_wifi
        esp_timer
        bt
)
project(node_mode_dualcore)