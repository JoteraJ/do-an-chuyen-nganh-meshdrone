idf_component_register(
    SRCS "mesh_camera_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash chunk_common
    # Phase 2: them "esp32-camera" vao day sau khi chay
    #   idf.py add-dependency "espressif/esp32-camera"
)
