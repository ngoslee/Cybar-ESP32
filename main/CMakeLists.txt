set(srcs)

if(CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND)
list(APPEND srcs
     "rf_tesing_configuration_cmd.c")
endif()

idf_component_register(SRCS "web_server.c""main.c" "hardware.c" "diag_port.c" "lin_truck.c" "lin.c" "lin_bar.c" "patterns.c" "egg.c" "system.c" "web_mesh.c" "mesh_node.c" "${srcs}"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart console driver led_strip esp_adc json esp_http_server spiffs
                    INCLUDE_DIRS ".")

                    # Add SPIFFS image generation
spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)