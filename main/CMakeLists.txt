idf_component_register(SRCS "main.cpp" "gc8_ble.cpp" "device_power.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_timer heap esp_driver_i2c)