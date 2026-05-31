idf_component_register(SRCS "main.c" "status_led.c" "led_strip_encoder.c" "board_flash.c" "wifi_time.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_ledc esp_driver_gpio esp_driver_rmt esp_timer spi_flash spiffs
                             esp_wifi esp_netif esp_event nvs_flash)
