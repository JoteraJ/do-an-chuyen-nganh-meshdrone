/*
 * ============================================================================
 *  ESP32-CAM Camera Node - painlessMesh
 * ============================================================================
 *  Board:    ESP32-CAM (AI_THINKER)
 *  Sensor:   OV3660 (VGA/SVGA)
 *  Memory:   PSRAM required
 *  Network:  painlessMesh
 * 
 *  Mô tả:
 *    Node Camera chụp ảnh JPEG, chia nhỏ thành các chunk 1024 bytes,
 *    mã hoá Base64, đóng gói JSON và gửi broadcast qua mạng Mesh.
 * 
 *  JSON format cho mỗi chunk:
 *    {"node":<NodeID>,"chunk":<index>,"total":<totalChunks>,"data":"<base64>"}
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_camera.h"
#include "painlessMesh.h"
#include "mbedtls/base64.h"

// =============================================================================
//  CẤU HÌNH MẠNG MESH
// =============================================================================
#define MESH_PREFIX     "ESP32_MESH_NET"
#define MESH_PASSWORD   "123456789"
#define MESH_PORT       5555

// =============================================================================
//  CẤU HÌNH CAMERA - CAMERA_MODEL_AI_THINKER
// =============================================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// =============================================================================
//  CẤU HÌNH CHUNK
// =============================================================================
// Kích thước tối đa của dữ liệu RAW mỗi chunk (trước khi mã hoá Base64)
// 1024 bytes raw → ~1368 ký tự Base64 + ~80 ký tự JSON header ≈ 1450 bytes/msg
#define RAW_CHUNK_SIZE    1024

// Khoảng thời gian giữa các lần chụp ảnh (ms)
#define CAPTURE_INTERVAL  10000   // 10 giây

// Khoảng delay giữa các chunk gửi liên tiếp (ms) - tránh nghẽn mesh
#define CHUNK_SEND_DELAY  50

// =============================================================================
//  BIẾN TOÀN CỤC
// =============================================================================
painlessMesh mesh;

Scheduler     userScheduler;   // Scheduler cho các task
Task           taskCapture(CAPTURE_INTERVAL, TASK_FOREVER, &captureAndSendImage);

// Prototype
void captureAndSendImage();
bool initCamera();
String encodeBase64(const uint8_t* data, size_t len);
void sendImageChunks(const uint8_t* jpegBuf, size_t jpegLen);

// =============================================================================
//  CALLBACK MESH
// =============================================================================

// Callback khi nhận được message từ mesh (node này chủ yếu gửi, ít nhận)
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("[MESH] Nhận msg từ %u: %s\n", from, msg.c_str());
}

// Callback khi có node mới kết nối
void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("[MESH] Kết nối mới: %u\n", nodeId);
}

// Callback khi topology thay đổi
void changedConnectionCallback() {
  Serial.println("[MESH] Topology mạng thay đổi.");
}

// Callback khi đồng bộ thời gian
void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("[MESH] Đồng bộ thời gian, offset = %d\n", offset);
}

// =============================================================================
//  HÀM KHỞI TẠO CAMERA
// =============================================================================

/**
 * @brief Khởi tạo camera OV3660 trên board AI_THINKER.
 * 
 * Cấu hình:
 *   - Pixel format: JPEG (nén phần cứng)
 *   - Frame size: VGA (640x480) - cân bằng chất lượng/kích thước
 *   - JPEG quality: 12 (0=best, 63=worst)
 *   - Frame buffer: Lưu trong PSRAM (bắt buộc cho ảnh lớn)
 *   - Grab mode: LATEST (luôn lấy frame mới nhất)
 * 
 * @return true  Khởi tạo thành công
 * @return false Khởi tạo thất bại
 */
bool initCamera() {
  Serial.println("[CAM] Bắt đầu khởi tạo camera...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;        // XCLK 20MHz
  config.pixel_format = PIXFORMAT_JPEG;   // Nén JPEG bằng phần cứng

  // Kiểm tra PSRAM
  if (psramFound()) {
    Serial.println("[CAM] PSRAM được phát hiện → dùng PSRAM cho frame buffer.");
    config.frame_size   = FRAMESIZE_VGA;    // 640x480 - tối ưu cho mesh
    config.jpeg_quality = 12;               // Chất lượng JPEG (0-63, nhỏ = tốt hơn)
    config.fb_count     = 2;                // 2 buffer cho pipeline
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("[CAM] CẢNH BÁO: Không tìm thấy PSRAM! Giảm độ phân giải.");
    config.frame_size   = FRAMESIZE_QVGA;   // 320x240 fallback
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  }

  // Khởi tạo driver camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] LỖI khởi tạo camera: 0x%x\n", err);
    return false;
  }

  // Tinh chỉnh cảm biến OV3660
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    // Nhận diện loại cảm biến
    if (s->id.PID == OV3660_PID) {
      Serial.println("[CAM] Cảm biến OV3660 được nhận diện.");
      // Cấu hình riêng cho OV3660
      s->set_vflip(s, 1);        // Lật dọc (tuỳ hướng lắp đặt)
      s->set_brightness(s, 1);   // Tăng sáng nhẹ
      s->set_saturation(s, -1);  // Giảm bão hoà nhẹ cho ảnh tự nhiên hơn
    } else {
      Serial.printf("[CAM] Cảm biến khác (PID: 0x%04X). Dùng cấu hình mặc định.\n", s->id.PID);
    }
  }

  Serial.println("[CAM] Khởi tạo camera thành công!");
  return true;
}

// =============================================================================
//  HÀM MÃ HOÁ BASE64
// =============================================================================

/**
 * @brief Mã hoá một mảng byte sang chuỗi Base64.
 * 
 * Sử dụng thư viện mbedtls có sẵn trên ESP32.
 * Lưu ý: Base64 tăng kích thước dữ liệu ~33%.
 *   1024 bytes raw → ~1368 ký tự Base64.
 * 
 * @param data  Con trỏ đến dữ liệu raw
 * @param len   Kích thước dữ liệu (bytes)
 * @return String Chuỗi Base64 đã mã hoá
 */
String encodeBase64(const uint8_t* data, size_t len) {
  // Tính kích thước output Base64: ceil(len/3)*4 + 1 (null terminator)
  size_t outLen = 0;
  
  // Gọi lần 1 để lấy kích thước cần thiết
  mbedtls_base64_encode(NULL, 0, &outLen, data, len);
  
  // Cấp phát buffer
  uint8_t* outBuf = (uint8_t*)malloc(outLen + 1);
  if (outBuf == NULL) {
    Serial.println("[B64] LỖI: Không đủ bộ nhớ cho Base64 encode!");
    return "";
  }

  // Gọi lần 2 để mã hoá thực sự
  int ret = mbedtls_base64_encode(outBuf, outLen + 1, &outLen, data, len);
  if (ret != 0) {
    Serial.printf("[B64] LỖI mã hoá Base64: %d\n", ret);
    free(outBuf);
    return "";
  }

  outBuf[outLen] = '\0';
  String result = String((char*)outBuf);
  free(outBuf);
  return result;
}

// =============================================================================
//  HÀM GỬI ẢNH QUA MESH (CẮT CHUNK)
// =============================================================================

/**
 * @brief Chia buffer JPEG thành các chunk và gửi qua mesh.
 * 
 * THUẬT TOÁN CẮT ẢNH VÀ TẠO HEADER:
 * ────────────────────────────────────
 * 1. Nhận toàn bộ buffer JPEG (mảng byte liên tục) từ camera.
 * 
 * 2. Tính tổng số chunk:
 *      totalChunks = ceil(jpegLen / RAW_CHUNK_SIZE)
 *    Ví dụ: Ảnh VGA JPEG ≈ 20KB → 20480 / 1024 = 20 chunks.
 * 
 * 3. Vòng lặp cắt: Duyệt từ offset = 0, mỗi bước nhảy RAW_CHUNK_SIZE:
 *      - Chunk cuối cùng có thể ngắn hơn RAW_CHUNK_SIZE.
 *      - Lấy con trỏ: &jpegBuf[offset]
 *      - Kích thước: min(RAW_CHUNK_SIZE, jpegLen - offset)
 * 
 * 4. Mã hoá Base64 từng chunk riêng biệt (không mã hoá toàn bộ ảnh
 *    rồi mới cắt, vì sẽ tốn gấp đôi bộ nhớ).
 * 
 * 5. Đóng gói JSON với header:
 *      {"node": 12345, "chunk": 0, "total": 20, "data": "SGVsbG8=..."}
 *    - node:  ID của node trong mạng mesh (dùng mesh.getNodeId())
 *    - chunk: Số thứ tự chunk (bắt đầu từ 0)
 *    - total: Tổng số chunk của ảnh này
 *    - data:  Chuỗi Base64 của chunk dữ liệu raw
 * 
 * 6. Gửi broadcast qua mesh. Gateway nhận sẽ ghép lại các chunk
 *    theo (node, total) và sắp xếp theo chunk index.
 * 
 * @param jpegBuf  Con trỏ đến buffer JPEG
 * @param jpegLen  Kích thước buffer JPEG (bytes)
 */
void sendImageChunks(const uint8_t* jpegBuf, size_t jpegLen) {
  // Tính tổng số chunk
  uint16_t totalChunks = (jpegLen + RAW_CHUNK_SIZE - 1) / RAW_CHUNK_SIZE;
  uint32_t nodeId = mesh.getNodeId();

  Serial.printf("[SEND] Ảnh %u bytes → %u chunks (mỗi chunk tối đa %d bytes raw)\n",
                jpegLen, totalChunks, RAW_CHUNK_SIZE);

  size_t offset = 0;

  for (uint16_t i = 0; i < totalChunks; i++) {
    // --- Bước 3: Xác định kích thước chunk hiện tại ---
    size_t currentChunkSize = RAW_CHUNK_SIZE;
    if (offset + currentChunkSize > jpegLen) {
      currentChunkSize = jpegLen - offset;  // Chunk cuối có thể ngắn hơn
    }

    // --- Bước 4: Mã hoá Base64 cho chunk này ---
    String base64Data = encodeBase64(&jpegBuf[offset], currentChunkSize);
    if (base64Data.length() == 0) {
      Serial.printf("[SEND] LỖI mã hoá chunk %u, bỏ qua ảnh này.\n", i);
      return;
    }

    // --- Bước 5: Đóng gói JSON ---
    // Xây dựng JSON thủ công để tiết kiệm RAM (không dùng ArduinoJson)
    String jsonMsg = "{\"node\":";
    jsonMsg += String(nodeId);
    jsonMsg += ",\"chunk\":";
    jsonMsg += String(i);
    jsonMsg += ",\"total\":";
    jsonMsg += String(totalChunks);
    jsonMsg += ",\"data\":\"";
    jsonMsg += base64Data;
    jsonMsg += "\"}";

    // --- Bước 6: Gửi broadcast qua mesh ---
    bool sent = mesh.sendBroadcast(jsonMsg);
    if (sent) {
      Serial.printf("[SEND] Chunk %u/%u gửi OK (%u bytes JSON)\n",
                    i + 1, totalChunks, jsonMsg.length());
    } else {
      Serial.printf("[SEND] Chunk %u/%u gửi THẤT BẠI!\n", i + 1, totalChunks);
    }

    // Giải phóng String sớm
    jsonMsg = "";
    base64Data = "";

    // Delay nhỏ giữa các chunk để tránh nghẽn buffer mesh
    delay(CHUNK_SEND_DELAY);

    // Cho mesh xử lý trong lúc chờ
    mesh.update();

    // Cập nhật offset
    offset += currentChunkSize;
  }

  Serial.printf("[SEND] Hoàn tất gửi ảnh: %u chunks.\n", totalChunks);
}

// =============================================================================
//  HÀM CHỤP ẢNH VÀ GỬI
// =============================================================================

/**
 * @brief Chụp một frame JPEG từ camera và gửi qua mesh.
 * 
 * Quy trình:
 *   1. Gọi esp_camera_fb_get() để chụp ảnh
 *   2. Kiểm tra định dạng JPEG
 *   3. Gọi sendImageChunks() để cắt và gửi
 *   4. Giải phóng frame buffer bằng esp_camera_fb_return()
 */
void captureAndSendImage() {
  Serial.println("\n========================================");
  Serial.println("[CAP] Bắt đầu chụp ảnh...");

  // Chụp ảnh
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == NULL) {
    Serial.println("[CAP] LỖI: Không chụp được ảnh!");
    return;
  }

  // Kiểm tra định dạng
  if (fb->format != PIXFORMAT_JPEG) {
    Serial.println("[CAP] CẢNH BÁO: Frame không phải JPEG, bỏ qua.");
    esp_camera_fb_return(fb);
    return;
  }

  Serial.printf("[CAP] Chụp thành công: %ux%u, %u bytes, format=JPEG\n",
                fb->width, fb->height, fb->len);

  // Kiểm tra kích thước hợp lý
  if (fb->len == 0 || fb->len > 200000) {
    Serial.printf("[CAP] CẢNH BÁO: Kích thước ảnh bất thường (%u bytes), bỏ qua.\n", fb->len);
    esp_camera_fb_return(fb);
    return;
  }

  // Gửi ảnh qua mesh
  sendImageChunks(fb->buf, fb->len);

  // Giải phóng frame buffer
  esp_camera_fb_return(fb);
  Serial.println("[CAP] Frame buffer đã được giải phóng.");
  Serial.println("========================================\n");
}

// =============================================================================
//  SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);  // Chờ Serial ổn định

  Serial.println("\n\n");
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   ESP32-CAM Camera Node - Mesh       ║");
  Serial.println("╚══════════════════════════════════════╝");
  Serial.println();

  // --- Bước 1a: Khởi tạo Camera ---
  if (!initCamera()) {
    Serial.println("[SETUP] Camera khởi tạo thất bại! Khởi động lại sau 5 giây...");
    delay(5000);
    ESP.restart();
  }

  // --- Bước 1b: Khởi tạo Mesh ---
  Serial.println("[SETUP] Khởi tạo mạng Mesh...");
  Serial.printf("[SETUP] SSID: %s | Port: %d\n", MESH_PREFIX, MESH_PORT);

  // Tắt debug mặc định của painlessMesh (quá nhiều log)
  // mesh.setDebugMsgTypes(ERROR | STARTUP);

  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);

  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);

  // Đăng ký callback
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  // Thêm task chụp ảnh định kỳ vào scheduler
  userScheduler.addTask(taskCapture);
  taskCapture.enable();

  Serial.printf("[SETUP] Node ID: %u\n", mesh.getNodeId());
  Serial.printf("[SETUP] Chụp ảnh mỗi %d ms\n", CAPTURE_INTERVAL);
  Serial.println("[SETUP] Khởi tạo hoàn tất! Đang chờ kết nối mesh...");
}

// =============================================================================
//  LOOP
// =============================================================================

void loop() {
  // painlessMesh yêu cầu gọi update() liên tục trong loop
  mesh.update();
}
