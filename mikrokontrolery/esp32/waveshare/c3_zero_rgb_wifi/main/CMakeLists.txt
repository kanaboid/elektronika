idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_http_server esp_driver_gpio led_strip
    INCLUDE_DIRS ".")
