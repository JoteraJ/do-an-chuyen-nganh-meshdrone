/**
 * ============================================================
 *  mesh_config.h — Cau hinh chung (ban ESP-IDF / ESP-WIFI-MESH)
 * ============================================================
 *
 *  Ban port tu shared/mesh_config.h (painlessMesh) sang ESP-IDF.
 *  Khac biet chinh so voi ban cu:
 *    - Khong con MESH_PORT (painlessMesh dung TCP socket rieng,
 *      ESP-WIFI-MESH la giao thuc L2 khong dung khai niem "port").
 *    - Them MESH_ID (6 byte dinh danh mang, thay cho MESH_SSID)
 *      va MESH_AP_PASSWORD (mat khau AP noi bo cua mesh).
 *    - ROUTER_SSID/PASSWORD la WiFi that ma Root se uplink vao
 *      (giu dung vai tro nhu STATION_SSID/PASSWORD ban cu).
 * ============================================================
 */

#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

#include <stdint.h>

/* ============================================================
 *  1. CAU HINH MANG ESP-WIFI-MESH
 * ============================================================ */
// Dinh danh mang mesh - 6 byte, TAT CA node phai giong nhau
#define MESH_ID                 { 0x77, 0x69, 0x66, 0x69, 0x6D, 0x65 }  // "wifime"
#define MESH_CHANNEL             1        // Kenh WiFi co dinh (1-14), nen giu co dinh de mesh hoi tu nhanh
#define MESH_AP_PASSWORD        "MeshVideo2024!"   // Mat khau AP noi bo giua cac node mesh (>= 8 ky tu)
#define MESH_AP_MAX_CONNECTION   6        // So con toi da 1 node cha co the nhan

/* ============================================================
 *  2. WIFI ROUTER THAT — CHI ROOT DUNG DE UPLINK RA NGOAI
 *
 *  >>> THAY SSID/PASSWORD WIFI ROUTER CUA BAN TAI DAY <<<
 * ============================================================ */
#define ROUTER_SSID              "YOUR_WIFI_SSID"
#define ROUTER_PASSWORD          "YOUR_WIFI_PASSWORD"

/* ============================================================
 *  3. CAU HINH GIAO THUC CHUNK
 *
 *  LUU Y QUAN TRONG: ESP-WIFI-MESH gui du lieu NHI PHAN truc tiep
 *  qua mesh_data_t (khac painlessMesh chi nhan String/JSON).
 *  => KHONG can Base64 encode/decode nua, bo hang base64_util.h.
 *  => Payload = header (12 byte) + JPEG data, gui thang, tiet
 *     kiem ~33% bang thong so voi ban painlessMesh cu.
 * ============================================================ */
#define MAX_CHUNK_PAYLOAD        1024     // Payload toi da moi chunk (byte)
#define CHUNK_SEND_DELAY_MS      20       // Delay giua cac chunk (ms)

/* ============================================================
 *  4. CAU HINH REASSEMBLY O ROOT NODE (giu nguyen logic cu)
 * ============================================================ */
#define REASSEMBLY_TIMEOUT_MS    5000
#define MAX_CONCURRENT_FRAMES    4
#define MAX_FRAME_SIZE           65536
#define MAX_CHUNKS_PER_FRAME     64       // MAX_FRAME_SIZE / MAX_CHUNK_PAYLOAD

/* ============================================================
 *  5. CAMERA OV3660 — Pin mapping ESP32-CAM (AI-Thinker)
 *  (giu nguyen tu ban painlessMesh, dung lai o Phase 2)
 * ============================================================ */
#define CAM_PIN_PWDN      32
#define CAM_PIN_RESET     -1
#define CAM_PIN_XCLK       0
#define CAM_PIN_SIOD      26
#define CAM_PIN_SIOC      27
#define CAM_PIN_D7        35
#define CAM_PIN_D6        34
#define CAM_PIN_D5        39
#define CAM_PIN_D4        36
#define CAM_PIN_D3        21
#define CAM_PIN_D2        19
#define CAM_PIN_D1        18
#define CAM_PIN_D0         5
#define CAM_PIN_VSYNC     25
#define CAM_PIN_HREF      23
#define CAM_PIN_PCLK      22

#define CAM_JPEG_QUALITY  12
#define CAPTURE_INTERVAL_MS 300

/* ============================================================
 *  6. HTTP SERVER (Root Node) — dung o Phase 2
 * ============================================================ */
#define HTTP_SERVER_PORT  80

#endif // MESH_CONFIG_H
