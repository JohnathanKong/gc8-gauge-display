idf_component_register(SRCS "main.cpp" "gc8_ble.cpp" "device_power.cpp"
                            "garage_button.cpp" "device_orientation.cpp"
                            "audio_reactivity.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_timer esp_system heap esp_driver_i2c esp_driver_gpio)