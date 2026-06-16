idf_component_register(
    SRCS
        "main.cpp"
        "sensor_manager.cpp"
        "servo_controller.cpp"
        "app_state.cpp"
        "camera_manager.cpp"
        "inference.cpp"
        "display_manager.cpp"
        "audio_player.cpp"
        "wifi_manager.cpp"
        "web_server.cpp"
        "system_fsm.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        "driver"
        "esp_common"
        "esp_timer"
        "esp_hw_support"
        "esp_driver_ledc"
        "esp_driver_gpio"
        "esp_psram"
        "esp_http_server"
        "esp_wifi"
        "nvs_flash"
        "esp_event"
        "esp_netif"
        "json"
        "spiffs"
)

# Tạo image SPIFFS từ thư mục web_data và tự động flash
spiffs_create_partition_image(storage "web_data" FLASH_IN_PROJECT)
