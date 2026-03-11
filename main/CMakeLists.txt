idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES mqtt nvs_flash esp_wifi esp_event driver json)