/**
 * ================================================================
 *  node_camera — ESP-WIFI-MESH (ESP-IDF) — PHASE 1
 * ================================================================
 *
 *  Muc tieu Phase 1: chung minh mesh join + gui chunk nhi phan
 *  toi Root hoat dong dung, TRUOC KHI ghep camera OV3660 that
 *  (Phase 2). Anh JPEG duoc GIA LAP bang 1 buffer dem so, kich
 *  thuoc thay doi ngau nhien 8-15KB (giong kich thuoc anh QVGA
 *  that) de test dung logic multi-chunk/multi-frame.
 *
 *  DIEM QUAN TRONG so voi ban painlessMesh cu:
 *  ────────────────────────────────────────────
 *  esp_mesh_send(NULL, &data, MESH_DATA_TODS, NULL, 0) gui goi tin
 *  THEO DUONG VE ROOT (unicast, di qua dung cac node cha trung
 *  gian) — KHONG flood ra toan mang nhu mesh.sendBroadcast() cua
 *  painlessMesh. Day chinh la diem ban va minh da ghi trong phan
 *  review truoc: "nen doi sendBroadcast -> sendSingle" — voi
 *  ESP-WIFI-MESH, hanh vi dung (unicast-to-root) la MAC DINH,
 *  khong can tu code lai.
 *
 *  Phase 2 se thay ham fill_fake_jpeg() bang esp_camera_fb_get()
 *  that — phan con lai (chunking loop) giu nguyen khong doi.
 * ================================================================ */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"

#include "mesh_config.h"
#include "chunk_protocol.h"

static const char *TAG = "NODE_CAMERA";

static uint16_t s_frame_id_counter = 0;
static uint32_t s_my_node_id = 0;

/* ------------------------------------------------------------
 *  fill_fake_jpeg() — GIA LAP anh JPEG, se bi THAY THE o Phase 2
 *  bang esp_camera_fb_get()/fb->buf/fb->len that.
 * ------------------------------------------------------------ */
static size_t fill_fake_jpeg(uint8_t *buf, size_t buf_cap) {
    size_t len = 8000 + (esp_random() % 7000);  // 8-15KB, giong QVGA that
    if (len > buf_cap) len = buf_cap;
    buf[0] = 0xFF; buf[1] = 0xD8;  // gia lap SOI marker
    for (size_t i = 2; i < len - 2; i++) buf[i] = (uint8_t)(i & 0xFF);
    buf[len - 2] = 0xFF; buf[len - 1] = 0xD9;  // gia lap EOI marker
    return len;
}

static void capture_and_send_task(void *arg) {
    // fake_jpeg dat trong PSRAM (giong fb->buf that cua camera se dung o
    // Phase 2) de khong chiem SRAM noi bo — vung nay dung chung cho WiFi/mesh stack.
    uint8_t *fake_jpeg = (uint8_t *)heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    static uint8_t packet_buf[CHUNK_PACKET_MAX_SIZE];

    if (fake_jpeg == NULL) {
        ESP_LOGE(TAG, "[CAM] Khong cap phat duoc PSRAM cho fake_jpeg — kiem tra CONFIG_SPIRAM trong sdkconfig.defaults");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CAPTURE_INTERVAL_MS));

        // Chi gui khi da co duong ve Root (co Parent) — tranh gui vao khoang khong
        if (esp_mesh_get_layer() <= 0) {
            continue;
        }

        size_t jpeg_len = fill_fake_jpeg(fake_jpeg, sizeof(fake_jpeg));
        uint16_t total_chunks = calc_total_chunks(jpeg_len);

        int64_t t_start = esp_timer_get_time();

        for (uint16_t i = 0; i < total_chunks; i++) {
            size_t offset = (size_t)i * MAX_CHUNK_PAYLOAD;
            size_t remaining = jpeg_len - offset;
            uint16_t payload_len = (remaining > MAX_CHUNK_PAYLOAD) ? MAX_CHUNK_PAYLOAD : (uint16_t)remaining;

            chunk_header_t header = {
                .node_id      = s_my_node_id,
                .frame_id     = s_frame_id_counter,
                .chunk_index  = i,
                .total_chunks = total_chunks,
                .payload_len  = payload_len,
            };

            size_t packet_len = build_chunk_packet(&header, fake_jpeg + offset, packet_buf, sizeof(packet_buf));
            if (packet_len == 0) {
                ESP_LOGE(TAG, "[CAM] build_chunk_packet that bai (chunk %d)", i);
                continue;
            }

            mesh_data_t data = {
                .data = packet_buf,
                .size = packet_len,
                .proto = MESH_PROTO_BIN,
                .tos = MESH_TOS_P2P,
            };

            // to = NULL + MESH_DATA_TODS => mesh tu dinh tuyen goi tin len Root
            // qua dung duong cay hien tai (unicast, khong flood toan mang).
            esp_err_t err = esp_mesh_send(NULL, &data, MESH_DATA_TODS, NULL, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "[CAM] Gui chunk %d/%d that bai: 0x%x", i + 1, total_chunks, err);
            }

            if (i < total_chunks - 1) {
                vTaskDelay(pdMS_TO_TICKS(CHUNK_SEND_DELAY_MS));
            }
        }

        int64_t elapsed_ms = (esp_timer_get_time() - t_start) / 1000;
        ESP_LOGI(TAG, "[CAM] Frame #%d: %u byte, %d chunks, gui het %lld ms",
                 s_frame_id_counter, (unsigned)jpeg_len, total_chunks, elapsed_ms);

        s_frame_id_counter++;
    }
}

/* ================================================================
 *  MESH EVENT HANDLER — chi log, giong newConnectionCallback() cu
 * ================================================================ */
static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    switch (id) {
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        ESP_LOGI(TAG, "[MESH] Parent connected, layer=%d", connected->self_layer);
        break;
    }
    case MESH_EVENT_PARENT_DISCONNECTED:
        ESP_LOGW(TAG, "[MESH] Mat Parent — dang tu tim duong moi (self-healing)");
        break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *lc = (mesh_event_layer_change_t *)event_data;
        ESP_LOGI(TAG, "[MESH] Layer thay doi -> %d", lc->new_layer);
        break;
    }
    default:
        break;
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif_sta = NULL;
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mesh_init());

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    const uint8_t mesh_id_bytes[6] = MESH_ID;
    memcpy((uint8_t *)&cfg.mesh_id, mesh_id_bytes, 6);
    cfg.channel = MESH_CHANNEL;

    cfg.router.ssid_len = strlen(ROUTER_SSID);
    memcpy((uint8_t *)&cfg.router.ssid, ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *)&cfg.router.password, ROUTER_PASSWORD, strlen(ROUTER_PASSWORD));

    cfg.mesh_ap.max_connection = MESH_AP_MAX_CONNECTION;
    memcpy((uint8_t *)&cfg.mesh_ap.password, MESH_AP_PASSWORD, strlen(MESH_AP_PASSWORD));

    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    ESP_ERROR_CHECK(esp_mesh_start());

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_my_node_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                   ((uint32_t)mac[4] << 8)  |  (uint32_t)mac[5];

    ESP_LOGI(TAG, "[SYS] CAMERA NODE (Phase 1 — anh gia lap, chua ghep OV3660) san sang, node_id=%u",
             (unsigned)s_my_node_id);

    xTaskCreate(capture_and_send_task, "capture_and_send_task", 8192, NULL, 5, NULL);
}
