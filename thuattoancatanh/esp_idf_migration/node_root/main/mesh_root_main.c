/**
 * ================================================================
 *  node_root — ESP-WIFI-MESH (ESP-IDF) — PHASE 1
 * ================================================================
 *
 *  Muc tieu Phase 1: CHUNG MINH mesh transport + reassembly hoat
 *  dong dung tren ESP-WIFI-MESH that (khong phai painlessMesh),
 *  truoc khi ghep camera + HTTP server (Phase 2).
 *
 *  Nhung gi file nay LAM:
 *   1. Khoi tao ESP-WIFI-MESH, co gang tro thanh Root (node co
 *      duong toi ROUTER_SSID cau hinh trong mesh_config.h).
 *   2. Nhan chunk nhi phan tu cac node khac qua esp_mesh_recv().
 *   3. Ghep (reassemble) chunk thanh "frame" hoan chinh — GIU
 *      NGUYEN logic tu node_root.ino (bitmap, timeout, LRU
 *      eviction) — chi doi tu Arduino/painlessMesh sang ESP-IDF.
 *   4. In thong ke qua ESP_LOG (chua co HTTP server — Phase 2
 *      se them esp_http_server de phat MJPEG).
 *
 *  DIEU CAN BAN TU KIEM TRA KHI BUILD LAN DAU (minh khong the
 *  compile-test tu xa, danh dau ro o day):
 *   - REQUIRES trong main/CMakeLists.txt: neu idf.py build bao
 *     thieu ham esp_mesh_xxx, them component "esp-wifi-mesh" (hoac
 *     ten tuong duong tuy version IDF) vao REQUIRES.
 *   - Co che "fix root": ham lock_self_as_root_once() ben duoi la
 *     each pho bien de node nay giu vai Root on dinh, nhung ban
 *     nen quan sat log MESH_EVENT that ky truoc khi tin tuong.
 * ================================================================ */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_netif.h"

#include "mesh_config.h"
#include "chunk_protocol.h"

static const char *TAG = "NODE_ROOT";

/* ================================================================
 *  FRAME ASSEMBLY — port truc tiep tu node_root.ino, khong doi logic
 * ================================================================ */
typedef struct {
    bool     active;
    uint32_t node_id;
    uint16_t frame_id;
    uint16_t total_chunks;
    uint16_t received_count;
    uint32_t jpeg_total_len;
    bool     chunk_received[MAX_CHUNKS_PER_FRAME];
    uint8_t *jpeg_buffer;    // Tro toi vung PSRAM cap phat 1 lan trong app_main()
                             // (giong ps_malloc() cua ban Arduino cu). 4 slot x 64KB = 256KB,
                             // BAT BUOC phai la PSRAM, SRAM noi bo ESP32 (~320KB) khong du.
    uint32_t start_time_ms;
} frame_assembly_t;

static frame_assembly_t s_assemblies[MAX_CONCURRENT_FRAMES];

static uint32_t s_frames_completed = 0;
static uint32_t s_frames_dropped   = 0;
static uint32_t s_chunks_received  = 0;

static int find_assembly_slot(uint32_t node_id, uint16_t frame_id) {
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (s_assemblies[i].active &&
            s_assemblies[i].node_id  == node_id &&
            s_assemblies[i].frame_id == frame_id) {
            return i;
        }
    }
    return -1;
}

static int allocate_assembly_slot(uint32_t node_id, uint16_t frame_id, uint16_t total_chunks) {
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (!s_assemblies[i].active) {
            s_assemblies[i].active         = true;
            s_assemblies[i].node_id        = node_id;
            s_assemblies[i].frame_id       = frame_id;
            s_assemblies[i].total_chunks   = total_chunks;
            s_assemblies[i].received_count = 0;
            s_assemblies[i].jpeg_total_len = 0;
            s_assemblies[i].start_time_ms  = (uint32_t)(esp_timer_get_time() / 1000);
            memset(s_assemblies[i].chunk_received, 0, sizeof(s_assemblies[i].chunk_received));
            ESP_LOGI(TAG, "[ASM] Slot %d: bat dau ghep frame (node=%u frame=%d chunks=%d)",
                     i, (unsigned)node_id, frame_id, total_chunks);
            return i;
        }
    }

    // Het slot -> evict slot cu nhat (giong LRU trong ban cu)
    uint32_t oldest_time = UINT32_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (s_assemblies[i].start_time_ms < oldest_time) {
            oldest_time = s_assemblies[i].start_time_ms;
            oldest_idx = i;
        }
    }
    ESP_LOGW(TAG, "[ASM] EVICTION slot %d (node=%u frame=%d, nhan %d/%d)",
             oldest_idx, (unsigned)s_assemblies[oldest_idx].node_id,
             s_assemblies[oldest_idx].frame_id,
             s_assemblies[oldest_idx].received_count,
             s_assemblies[oldest_idx].total_chunks);
    s_frames_dropped++;

    s_assemblies[oldest_idx].active         = true;
    s_assemblies[oldest_idx].node_id        = node_id;
    s_assemblies[oldest_idx].frame_id       = frame_id;
    s_assemblies[oldest_idx].total_chunks   = total_chunks;
    s_assemblies[oldest_idx].received_count = 0;
    s_assemblies[oldest_idx].jpeg_total_len = 0;
    s_assemblies[oldest_idx].start_time_ms  = (uint32_t)(esp_timer_get_time() / 1000);
    memset(s_assemblies[oldest_idx].chunk_received, 0, sizeof(s_assemblies[oldest_idx].chunk_received));
    return oldest_idx;
}

static void handle_incoming_packet(const uint8_t *raw, size_t raw_len) {
    chunk_header_t header;
    uint8_t payload_buf[MAX_CHUNK_PAYLOAD];

    if (!parse_chunk_packet(raw, raw_len, &header, payload_buf, sizeof(payload_buf))) {
        ESP_LOGW(TAG, "[ASM] Goi tin khong hop le (len=%u)", (unsigned)raw_len);
        return;
    }
    s_chunks_received++;

    int slot = find_assembly_slot(header.node_id, header.frame_id);
    if (slot < 0) {
        slot = allocate_assembly_slot(header.node_id, header.frame_id, header.total_chunks);
    }
    frame_assembly_t *fa = &s_assemblies[slot];

    if (fa->chunk_received[header.chunk_index]) {
        return;  // Duplicate, bo qua
    }

    size_t offset = (size_t)header.chunk_index * MAX_CHUNK_PAYLOAD;
    if (offset + header.payload_len > MAX_FRAME_SIZE) {
        ESP_LOGE(TAG, "[ASM] TRAN BUFFER offset=%u len=%u", (unsigned)offset, header.payload_len);
        return;
    }

    memcpy(fa->jpeg_buffer + offset, payload_buf, header.payload_len);
    fa->chunk_received[header.chunk_index] = true;
    fa->received_count++;

    size_t chunk_end = offset + header.payload_len;
    if (chunk_end > fa->jpeg_total_len) fa->jpeg_total_len = (uint32_t)chunk_end;

    if (fa->received_count >= fa->total_chunks) {
        ESP_LOGI(TAG, "[ASM] HOAN THANH! node=%u frame=%d size=%u chunks=%d",
                 (unsigned)fa->node_id, fa->frame_id, (unsigned)fa->jpeg_total_len, fa->total_chunks);
        // Phase 2: memcpy sang latest_frame buffer + goi HTTP MJPEG o day.
        fa->active = false;
        s_frames_completed++;
    }
}

static void cleanup_stale_frames(void) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (s_assemblies[i].active) {
            uint32_t elapsed = now - s_assemblies[i].start_time_ms;
            if (elapsed > REASSEMBLY_TIMEOUT_MS) {
                ESP_LOGW(TAG, "[ASM] TIMEOUT slot=%d node=%u frame=%d nhan %d/%d cho %u ms",
                         i, (unsigned)s_assemblies[i].node_id, s_assemblies[i].frame_id,
                         s_assemblies[i].received_count, s_assemblies[i].total_chunks,
                         (unsigned)elapsed);
                s_assemblies[i].active = false;
                s_frames_dropped++;
            }
        }
    }
}

/* ================================================================
 *  MESH EVENT HANDLER
 * ================================================================ */
static int s_mesh_layer = -1;
static bool s_is_root_locked = false;

static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    switch (id) {
    case MESH_EVENT_STARTED:
        ESP_LOGI(TAG, "[MESH] Started, ID=" MACSTR, MAC2STR(esp_mesh_get_mesh_id()->addr));
        break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        s_mesh_layer = connected->self_layer;
        ESP_LOGI(TAG, "[MESH] Parent connected, layer=%d, is_root=%d",
                 s_mesh_layer, esp_mesh_is_root());
        // TODO ban tu kiem tra: neu node nay dung la Root (co duong
        // toi ROUTER_SSID) thi khoa vai tro Root cho on dinh trong
        // luc demo, tranh doi Root giua chung khi RSSI dao dong.
        if (esp_mesh_is_root() && !s_is_root_locked) {
            esp_mesh_fix_root(true);
            s_is_root_locked = true;
            ESP_LOGI(TAG, "[MESH] Da khoa vai tro ROOT (fix_root=true)");
        }
        break;
    }
    case MESH_EVENT_PARENT_DISCONNECTED:
        ESP_LOGW(TAG, "[MESH] Mat ket noi voi Parent — dang tu tim duong moi (self-healing)");
        break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *lc = (mesh_event_layer_change_t *)event_data;
        s_mesh_layer = lc->new_layer;
        ESP_LOGI(TAG, "[MESH] Layer thay doi -> %d", s_mesh_layer);
        break;
    }
    case MESH_EVENT_ROOT_ADDRESS:
        ESP_LOGI(TAG, "[MESH] Root address event nhan duoc");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "[IP] Root nhan IP tu router: " IPSTR, IP2STR(&e->ip_info.ip));
        // Phase 2: khoi dong esp_http_server tai day (sau khi co IP).
    }
}

/* ================================================================
 *  MESH RECEIVE TASK
 *
 *  esp_mesh_recv() la ham BLOCKING — khac han co che callback
 *  onReceive() cua painlessMesh. Phai chay trong 1 task rieng,
 *  vong lap vo han, KHONG duoc goi tu app_main() truc tiep.
 * ================================================================ */
static void mesh_recv_task(void *arg) {
    static uint8_t rx_buf[CHUNK_PACKET_MAX_SIZE + 32];  // du du cho header mesh noi bo

    while (1) {
        mesh_addr_t from;
        mesh_data_t data;
        int flag = 0;
        data.data = rx_buf;
        data.size = sizeof(rx_buf);

        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[MESH] esp_mesh_recv loi: 0x%x", err);
            continue;
        }
        handle_incoming_packet(data.data, data.size);
    }
}

static void stats_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        cleanup_stale_frames();
        ESP_LOGI(TAG, "[STAT] Frames OK: %u | Dropped: %u | Chunks: %u | Layer: %d | Free heap: %u",
                 (unsigned)s_frames_completed, (unsigned)s_frames_dropped,
                 (unsigned)s_chunks_received, s_mesh_layer, (unsigned)esp_get_free_heap_size());
    }
}

static bool init_assembly_buffers(void) {
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        s_assemblies[i].jpeg_buffer = (uint8_t *)heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM);
        if (s_assemblies[i].jpeg_buffer == NULL) {
            ESP_LOGE(TAG, "[MEM] Khong cap phat duoc PSRAM cho slot %d (%d byte) — kiem tra "
                          "sdkconfig.defaults da bat CONFIG_SPIRAM chua", i, MAX_FRAME_SIZE);
            return false;
        }
        s_assemblies[i].active = false;
    }
    ESP_LOGI(TAG, "[MEM] Da cap phat %d slot x %d byte PSRAM (tong %d KB), con lai: %u byte",
             MAX_CONCURRENT_FRAMES, MAX_FRAME_SIZE,
             (MAX_CONCURRENT_FRAMES * MAX_FRAME_SIZE) / 1024,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    if (!init_assembly_buffers()) {
        ESP_LOGE(TAG, "[SYS] LOI NGHIEM TRONG: khong cap phat duoc bo nho PSRAM. Dung lai.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
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
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

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

    ESP_LOGI(TAG, "[SYS] ROOT NODE (Phase 1 — mesh + reassembly, chua co HTTP/camera) san sang");

    xTaskCreate(mesh_recv_task, "mesh_recv_task", 8192, NULL, 5, NULL);
    xTaskCreate(stats_task, "stats_task", 3072, NULL, 3, NULL);
}
