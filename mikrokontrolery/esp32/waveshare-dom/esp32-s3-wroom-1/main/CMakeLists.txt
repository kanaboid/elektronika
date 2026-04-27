idf_component_register(SRCS "main.c" "led_status.c" "ds18b20_app.c" "a01nyub_app.c" "wifi_app.c" "web_ui.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        "esp_driver_gpio"
                        "esp_driver_uart"
                        "esp_event"
                        "esp_http_server"
                        "esp_netif"
                        "esp_timer"
                        "esp_wifi"
                        "nvs_flash"
                        "espressif__led_strip"
                        "espressif__ds18b20"
                        "espressif__onewire_bus")