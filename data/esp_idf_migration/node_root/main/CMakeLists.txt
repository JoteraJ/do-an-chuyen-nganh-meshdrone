idf_component_register(
    SRCS "mesh_root_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash chunk_common esp_timer
    # LUU Y: neu idf.py build bao loi "esp_mesh.h: No such file or directory"
    # hoac thieu ham esp_mesh_xxx, them component mesh vao danh sach tren.
    # Tuy phien ban ESP-IDF, ten component nay co the la "esp-wifi-mesh"
    # hoac da duoc gop san vao esp_wifi — kiem tra thong bao loi cu the.
)
