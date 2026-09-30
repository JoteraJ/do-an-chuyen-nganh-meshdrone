/**
 * ================================================================
 *  NODE CAMERA — ESP32 Mesh Video Streaming (Sender / Leaf Node)
 * ================================================================
 *
 *  Chức năng chính:
 *  ────────────────
 *  1. Khởi tạo camera OV3660 ở chế độ JPEG, độ phân giải QVGA
 *  2. Kết nối vào mạng painlessMesh
 *  3. Định kỳ chụp ảnh JPEG → cắt thành các chunk 1024 byte
 *  4. Đóng gói mỗi chunk với Header 16 byte → encode Base64
 *  5. Gửi từng chunk thẳng tới Root qua sendSingle() (học root_id
 *     qua beacon "#ROOT..." Root phát định kỳ). Chưa biết root_id thì
 *     KHÔNG chụp/gửi (không broadcast ảnh — làm nghẽn mesh)
 *  6. Watchdog: mất beacon Root quá ROOT_LOST_RESET_MS → học lại root_id;
 *     quá ROOT_LOST_RESTART_MS → ESP.restart()
 *
 *  Phần cứng yêu cầu:
 *  ──────────────────
 *  - ESP32-CAM (AI-Thinker) hoặc board ESP32 có camera OV3660
 *  - PSRAM (4MB) — bắt buộc cho frame buffer camera
 *
 *  Thư viện cần cài (Arduino IDE Library Manager):
 *  ───────────────────────────────────────────────
 *  - painlessMesh (by painlessMesh team)
 *  - ArduinoJson (dependency của painlessMesh)
 *  - TaskScheduler (dependency của painlessMesh)
 *  - AsyncTCP (dependency của painlessMesh trên ESP32)
 *
 *  Cách sử dụng:
 *  ─────────────
 *  1. Copy các file mesh_config.h, base64_util.h, chunk_protocol.h
 *     từ thư mục shared/ vào thư mục node_camera/ (cùng cấp .ino)
 *  2. Mở node_camera.ino trong Arduino IDE
 *  3. Chọn Board: "AI Thinker ESP32-CAM"
 *  4. Bật PSRAM: Tools → PSRAM → "Enabled"
 *  5. Upload và mở Serial Monitor (115200 baud)
 *
 * ================================================================
 */

#include <painlessMesh.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "esp_camera.h"

/* ────────────────────────────────────────────────────────────
 *  Include shared headers
 *
 *  Các file này cần nằm cùng thư mục với file .ino này.
 *  Xem README.md để biết cách cài đặt.
 * ──────────────────────────────────────────────────────────── */
#include "mesh_config.h"
#include "base64_util.h"
#include "chunk_protocol.h"

// 1 = in log moi frame chup/gui (debug), 0 = tat khi do dac hieu nang
#define DEBUG_CAM_VERBOSE 0

/* chunk_log() — hook mà chunk_protocol.h gọi khi có lỗi encode/decode
 * chunk (xem khai báo extern trong chunk_protocol.h). node_camera KHÔNG
 * đóng khung UART cho middleware (chỉ Root mới làm việc đó), nên ở đây
 * giữ nguyên hành vi debug cũ: in thẳng ra Serial Monitor. */
void chunk_log(const char* msg) {
    Serial.println(msg);
}

/* ================================================================
 *  BIẾN TOÀN CỤC
 * ================================================================ */

painlessMesh mesh;                   // Đối tượng quản lý mạng Mesh

uint16_t frame_id_counter = 0;      // Bộ đếm frame, tự tăng sau mỗi lần chụp
                                     // Wrap-around: 0 → 65535 → 0 (uint16)
                                     // Root dùng giá trị này để phân biệt frame

uint32_t last_capture_time = 0;     // millis() của lần chụp cuối cùng
                                     // Dùng để giữ tốc độ chụp ~3 FPS

uint32_t frames_sent = 0;           // Tổng số frame đã gửi (thống kê)
uint32_t chunks_sent = 0;           // Tổng số chunk đã gửi (thống kê)
uint32_t chunks_send_fail = 0;      // Tổng số chunk gửi thất bại (thống kê)

/* root_id: Node ID của Root, học qua beacon ROOT_BEACON_PREFIX do Root
 * phát định kỳ (xem receivedCallback()). 0 = chưa biết → capture_and_send()
 * không chụp/gửi gì. Nếu ROOT_NODE_ID_OVERRIDE khác 0 trong mesh_config.h,
 * ép dùng ID đó và không tự học qua beacon nữa. */
uint32_t root_id = ROOT_NODE_ID_OVERRIDE;

/* millis() lúc nhận beacon Root cuối; setup() đặt = lúc khởi động để watchdog
 * vẫn restart được nếu chưa từng nhận beacon nào (xem check_root_watchdog()). */
uint32_t last_root_beacon_ms = 0;

/* esp_reset_reason() lúc khởi động — gửi trong NSTAT ("rst") để nhìn từ xa
 * biết Leaf vừa reset vì brownout, watchdog hay ESP.restart(). */
esp_reset_reason_t boot_reset_reason = ESP_RST_UNKNOWN;

uint32_t last_nstat_time = 0;       // millis() lần gửi NSTAT cuối (xem send_nstat())

#if STAGGER_ENABLE
/* Staggering (mesh_config.h mục 11): lịch chụp theo đồng hồ mesh. */
struct StaggerSlot { uint32_t node_id; uint8_t slot; };
static const StaggerSlot STAGGER_TABLE[] = STAGGER_SLOT_TABLE;
static const uint32_t    STAGGER_PERIOD_US = (uint32_t)CAPTURE_INTERVAL_MS * 1000UL;

uint8_t  stagger_slot      = 0;
uint32_t stagger_offset_us = 0;     // slot × P / STAGGER_SLOTS
uint32_t next_capture_us   = 0;     // mốc đồng hồ mesh (µs) của lần chụp kế tiếp
#endif

/* ================================================================
 *  KHỞI TẠO CAMERA OV3660
 *
 *  Cấu hình tất cả các pin GPIO kết nối giữa ESP32 và sensor
 *  camera, thiết lập chế độ xuất JPEG ở độ phân giải QVGA.
 *
 *  Lưu ý quan trọng về PSRAM:
 *  - Frame buffer camera PHẢI nằm trong PSRAM
 *  - Một ảnh QVGA JPEG ~8-15KB
 *  - Nếu không có PSRAM, camera init sẽ thất bại ở các
 *    resolution cao hoặc khi fb_count > 1
 * ================================================================ */
bool init_camera() {
    camera_config_t config;

    /* Cấu hình LEDC (LED Controller) cho XCLK
     * XCLK là clock master cấp cho sensor camera.
     * ESP32 dùng module LEDC để tạo tín hiệu clock này. */
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    /* Pin mapping — DVP 8-bit Parallel Interface
     *
     * Sensor camera giao tiếp với ESP32 qua 8 đường dữ liệu
     * song song (D0-D7), truyền 8 bit mỗi chu kỳ PCLK.
     *
     * Các pin được định nghĩa trong mesh_config.h cho board
     * ESP32-CAM (AI-Thinker). Nếu dùng board khác, thay đổi
     * pin mapping tại mesh_config.h.
     */
    config.pin_d0     = Y2_GPIO_NUM;     // Data bit 0 (LSB)
    config.pin_d1     = Y3_GPIO_NUM;     // Data bit 1
    config.pin_d2     = Y4_GPIO_NUM;     // Data bit 2
    config.pin_d3     = Y5_GPIO_NUM;     // Data bit 3
    config.pin_d4     = Y6_GPIO_NUM;     // Data bit 4
    config.pin_d5     = Y7_GPIO_NUM;     // Data bit 5
    config.pin_d6     = Y8_GPIO_NUM;     // Data bit 6
    config.pin_d7     = Y9_GPIO_NUM;     // Data bit 7 (MSB)
    config.pin_xclk   = XCLK_GPIO_NUM;  // Master clock output → sensor
    config.pin_pclk   = PCLK_GPIO_NUM;  // Pixel clock input ← sensor
    config.pin_vsync  = VSYNC_GPIO_NUM;  // Vertical sync ← sensor
    config.pin_href   = HREF_GPIO_NUM;   // Horizontal reference ← sensor

    /* I2C (SCCB) — Serial Camera Control Bus
     * Dùng để ghi/đọc register cấu hình của sensor OV3660.
     * SCCB tương thích I2C nhưng không dùng ACK/NACK. */
    config.pin_sccb_sda = SIOD_GPIO_NUM; // I2C Data
    config.pin_sccb_scl = SIOC_GPIO_NUM; // I2C Clock

    /* Power control */
    config.pin_pwdn  = PWDN_GPIO_NUM;   // Power down (active HIGH)
    config.pin_reset = RESET_GPIO_NUM;   // Hardware reset (-1 = không dùng)

    /* Thông số hoạt động */
    config.xclk_freq_hz = 20000000;      // XCLK 20MHz (tiêu chuẩn cho OV3660)

    /* Cấu hình output:
     *
     * PIXFORMAT_JPEG: Sensor OV3660 có bộ mã hóa JPEG phần cứng
     * tích hợp, xuất trực tiếp dữ liệu JPEG compressed.
     * Điều này tiết kiệm đáng kể RAM và băng thông so với
     * chế độ RAW (RGB565, YUV422).
     *
     * FRAMESIZE_QVGA: 320×240 pixels
     *   - Ảnh JPEG quality 12: ~8-15KB
     *   - Chia thành ~8-15 chunks × 1024 bytes
     *   - Phù hợp băng thông Mesh (~100-200 KB/s effective)
     */
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = CAM_FRAME_SIZE;       // QVGA 320×240
    config.jpeg_quality = CAM_JPEG_QUALITY;     // Quality 12 (0=tốt nhất, 63=tệ nhất)
    config.fb_count     = CAM_FB_COUNT;         // 1 frame buffer (tiết kiệm RAM)

    /* Vị trí frame buffer:
     * CAMERA_FB_IN_PSRAM: Đặt buffer trong PSRAM (4MB)
     * Bắt buộc vì internal SRAM chỉ có ~520KB, không đủ. */
    config.fb_location = CAMERA_FB_IN_PSRAM;

    /* CAMERA_GRAB_LATEST: Khi gọi esp_camera_fb_get(), lấy
     * frame MỚI NHẤT thay vì frame trong queue. Giảm latency
     * cho ứng dụng streaming. */
    config.grab_mode = CAMERA_GRAB_LATEST;

    /* Khởi tạo camera driver */
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Khoi tao THAT BAI! Error: 0x%x\n", err);
        Serial.println("[CAM] Kiem tra:");
        Serial.println("  - Ket noi phan cung camera");
        Serial.println("  - PSRAM da bat trong Arduino IDE");
        Serial.println("  - Chon dung board ESP32-CAM");
        return false;
    }

    /* Tùy chỉnh thêm cho sensor OV3660
     *
     * esp_camera_sensor_get() trả về con trỏ đến cấu trúc
     * sensor, cho phép điều chỉnh runtime các thông số hình ảnh.
     */
    sensor_t* s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_brightness(s, 0);     // Độ sáng: -2 đến +2
        s->set_contrast(s, 0);       // Độ tương phản: -2 đến +2
        s->set_saturation(s, 0);     // Độ bão hòa: -2 đến +2

        /* Nếu hình bị ngược hoặc lật, bỏ comment các dòng sau: */
        // s->set_vflip(s, 1);       // Lật dọc
        // s->set_hmirror(s, 1);     // Lật ngang

        /* OV3660 hỗ trợ thêm:
         * s->set_whitebal(s, 1);    // Auto white balance
         * s->set_awb_gain(s, 1);    // AWB gain
         * s->set_exposure_ctrl(s, 1); // Auto exposure
         */
    }

    Serial.println("[CAM] Khoi tao THANH CONG");
    Serial.printf("[CAM] Resolution: %dx%d, JPEG Quality: %d\n",
                  (CAM_FRAME_SIZE == FRAMESIZE_QVGA) ? 320 : 0,
                  (CAM_FRAME_SIZE == FRAMESIZE_QVGA) ? 240 : 0,
                  CAM_JPEG_QUALITY);
    return true;
}


/* ================================================================
 *  capture_and_send()
 *
 *  Hàm trung tâm thực hiện toàn bộ quy trình:
 *  1. Chụp ảnh JPEG từ camera
 *  2. Validate dữ liệu JPEG
 *  3. Tính số chunk cần thiết
 *  4. Cắt ảnh thành chunks, đóng header, encode Base64
 *  5. Gửi từng chunk qua Mesh network
 *  6. Giải phóng frame buffer
 *
 *  Gọi hàm này định kỳ từ loop() với interval CAPTURE_INTERVAL_MS
 *
 *  QUẢN LÝ BỘ NHỚ:
 *  - Frame buffer camera: tự động quản lý bởi esp_camera driver
 *    Gọi esp_camera_fb_get() → nhận buffer → xử lý → esp_camera_fb_return()
 *  - Serialize buffers: static trong serialize_chunk() → không phân mảnh heap
 *  - Chỉ có Arduino String tạm thời bị cấp phát trên heap
 * ================================================================ */
void capture_and_send() {
    /* ── Bước 0: Chưa biết Root → không chụp, không gửi ──
     *
     * root_id == 0: chưa nhận beacon Root, hoặc watchdog vừa quên Root.
     * KHÔNG gửi ảnh bằng sendBroadcast(): mỗi chunk broadcast bị mọi node
     * nhận và chuyển tiếp, làm nghẽn mesh đúng lúc mesh đang hồi phục.
     * Bỏ qua SỚM, TRƯỚC esp_camera_fb_get(), để không giữ frame buffer.
     */
    if (root_id == 0) {
        return;
    }

    /* ── Bước 1: Chụp ảnh JPEG ──────────────────────────────
     *
     * esp_camera_fb_get() block cho đến khi sensor hoàn thành
     * một frame. Trả về con trỏ đến camera_fb_t:
     *   - fb->buf: Con trỏ đến dữ liệu JPEG (trong PSRAM)
     *   - fb->len: Kích thước dữ liệu (bytes)
     *   - fb->width, fb->height: Kích thước ảnh
     *   - fb->format: Định dạng pixel (JPEG)
     *
     * QUAN TRỌNG: Phải gọi esp_camera_fb_return(fb) sau khi
     * xử lý xong để trả buffer về cho driver tái sử dụng.
     */
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == NULL) {
        Serial.println("[CAM] Chup anh THAT BAI! (fb == NULL)");
        return;
    }

    /* Mốc thời gian chụp theo đồng hồ mesh (µs, painlessMesh tự đồng bộ
     * giữa các node) — lấy ĐÚNG MỘT LẦN, gán cho mọi chunk của frame để
     * Root tính latency chụp → ghép xong. Không dùng millis(): mỗi board
     * có millis() riêng, không so sánh được giữa hai board. */
    uint32_t capture_us = mesh.getNodeTime();

    /* ── Bước 2: Validate dữ liệu JPEG ─────────────────────
     *
     * Mọi file JPEG hợp lệ đều bắt đầu bằng marker SOI
     * (Start Of Image): 0xFF 0xD8
     *
     * Nếu 2 byte đầu không phải 0xFFD8, dữ liệu bị corrupt
     * hoặc camera chưa sẵn sàng. Bỏ qua frame này.
     */
    if (fb->len < 2 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
        Serial.println("[CAM] Du lieu JPEG khong hop le! (Missing SOI marker)");
        esp_camera_fb_return(fb);
        return;
    }

    /* Kiểm tra kích thước hợp lý */
    if (fb->len > MAX_FRAME_SIZE) {
        Serial.printf("[CAM] Frame qua lon: %d bytes (max %d)\n",
                      fb->len, MAX_FRAME_SIZE);
        esp_camera_fb_return(fb);
        return;
    }

#if DEBUG_CAM_VERBOSE
    Serial.printf("[CAM] Frame #%d: %d bytes\n", frame_id_counter, fb->len);
#endif

    /* ── Bước 3: Tính số chunk cần thiết ────────────────────
     *
     * Chia kích thước ảnh cho MAX_CHUNK_PAYLOAD (1024 bytes),
     * làm tròn lên.
     *
     * Ví dụ: fb->len = 10500 bytes
     *   total_chunks = ceil(10500 / 1024) = 11
     *   Chunks 0-9: mỗi chunk 1024 bytes
     *   Chunk 10: 10500 - 10×1024 = 260 bytes (chunk cuối)
     */
    uint16_t total_chunks = calc_total_chunks(fb->len);
    uint32_t my_node_id   = mesh.getNodeId();

    /* ── Bước 4: Vòng lặp cắt và gửi từng chunk ────────────
     *
     * Duyệt từ chunk 0 đến chunk (total_chunks - 1).
     * Mỗi chunk:
     *   a) Tính offset trong JPEG buffer
     *   b) Tính payload_len (chunk cuối có thể < 1024)
     *   c) Tạo ChunkHeader
     *   d) Serialize (header + payload) → Base64 String
     *   e) Gửi String qua mesh.sendSingle() tới Root
     *   f) Delay ngắn để tránh tràn TCP buffer của Mesh
     */
    bool aborted = false;
    for (uint16_t i = 0; i < total_chunks; i++) {

        /* Root có thể vừa bị quên trong mesh.update() giữa 2 chunk
         * (changedConnectionCallback) → bỏ phần còn lại của frame. */
        if (root_id == 0) {
            aborted = true;
            break;
        }

        /* a) Tính vị trí bắt đầu đọc trong JPEG buffer
         *    chunk 0: offset = 0
         *    chunk 1: offset = 1024
         *    chunk 2: offset = 2048
         *    ...
         */
        size_t offset = (size_t)i * MAX_CHUNK_PAYLOAD;

        /* b) Tính kích thước payload thực tế
         *    Tất cả chunk trừ chunk cuối: payload = 1024
         *    Chunk cuối: payload = fb->len - offset (phần còn lại)
         *
         *    Ví dụ: fb->len = 10500, chunk 10 (cuối):
         *      offset    = 10 × 1024 = 10240
         *      remaining = 10500 - 10240 = 260 bytes
         */
        size_t remaining = fb->len - offset;
        uint16_t payload_len = (remaining > MAX_CHUNK_PAYLOAD)
                                ? MAX_CHUNK_PAYLOAD
                                : (uint16_t)remaining;

        /* c) Đóng gói Header
         *
         * ChunkHeader (16 bytes):
         *   node_id:      Để Root phân biệt dữ liệu từ camera nào
         *   frame_id:     Để Root ghép đúng các chunk cùng frame
         *   chunk_index:  Vị trí chunk → Root biết copy vào offset nào
         *   total_chunks: Để Root biết khi nào nhận đủ
         *   payload_len:  Kích thước payload (chunk cuối < 1024)
         *   capture_us:   Thời điểm chụp (đồng hồ mesh), chung cả frame
         */
        ChunkHeader header;
        header.node_id      = my_node_id;
        header.frame_id     = frame_id_counter;
        header.chunk_index  = i;
        header.total_chunks = total_chunks;
        header.payload_len  = payload_len;
        header.capture_us   = capture_us;

        /* d) Serialize: Header + Payload → Base64 String
         *
         * serialize_chunk() trong chunk_protocol.h:
         *   1. Copy header (16 bytes) vào static buffer
         *   2. Copy payload (≤1024 bytes) ngay sau header
         *   3. Base64 encode toàn bộ → trả về String
         *
         * Pointer fb->buf + offset trỏ vào đúng vùng JPEG
         * data của chunk hiện tại trong PSRAM.
         */
        String encoded = serialize_chunk(header, fb->buf + offset);

        if (encoded.length() == 0) {
            Serial.printf("[CAM] Serialize chunk %d THAT BAI!\n", i);
            continue;  // Bỏ qua chunk lỗi, tiếp tục chunk tiếp theo
        }

        /* e) Gửi qua Mesh Network
         *
         * sendSingle(root_id, encoded): Gửi thẳng tới Root — root_id
         * học được qua beacon ROOT_BEACON_PREFIX (xem receivedCallback()).
         * Tránh việc mọi chunk (broadcast) bị các Leaf khác cũng nhận,
         * cấp phát String rồi bỏ, nhân traffic không khí lên vô ích khi
         * có nhiều Leaf. Không còn fallback sendBroadcast() (xem Bước 0).
         */
        bool sent_ok = mesh.sendSingle(root_id, encoded);

        if (!sent_ok) {
            chunks_send_fail++;
            Serial.printf("[CAM] Gui chunk %d/%d THAT BAI!\n",
                          i + 1, total_chunks);
        }

        chunks_sent++;

        /* f) Delay giữa các chunk
         *
         * TẠI SAO CẦN DELAY?
         * painlessMesh sử dụng TCP nội bộ giữa các node.
         * Gửi quá nhanh (burst) sẽ:
         *   - Tràn TCP send buffer → gói bị drop
         *   - Tràn receive buffer ở node trung gian → mất gói
         *   - CPU không kịp xử lý JSON wrapping
         *
         * 20ms delay giữa các chunk:
         *   - 15 chunks × 20ms = 300ms overhead/frame
         *   - Vẫn cho phép đạt ~2-3 FPS
         *   - Giảm đáng kể tỷ lệ mất gói
         *
         * Gọi mesh.update() trong delay để:
         *   - Mesh tiếp tục xử lý routing, heartbeat
         *   - Tránh timeout connection
         */
        if (i < total_chunks - 1) {
            delay(CHUNK_SEND_DELAY_MS);
            mesh.update();   // Duy trì kết nối Mesh trong khi chờ
        }
    }

#if DEBUG_CAM_VERBOSE
    Serial.printf("[CAM] Frame #%d: da gui %d chunks\n",
                  frame_id_counter, total_chunks);
#endif

    /* ── Bước 5: Giải phóng frame buffer ────────────────────
     *
     * TRẢ LẠI buffer cho camera driver. Nếu không gọi hàm này,
     * driver sẽ hết buffer và esp_camera_fb_get() trả về NULL.
     *
     * Với fb_count = 1: chỉ có 1 buffer, phải return trước khi
     * chụp ảnh tiếp.
     */
    esp_camera_fb_return(fb);

    /* ── Bước 6: Tăng frame counter ────────────────────────
     *
     * uint16_t tự wrap-around: 65535 + 1 = 0
     * Root Node xử lý wrap-around: nếu nhận frame_id mới khác
     * frame_id cũ → tạo assembly slot mới.
     */
    frame_id_counter++;
    if (!aborted) frames_sent++;
}


/* ================================================================
 *  sta_mac_to_node_id()
 *
 *  Đổi MAC của một station (thấy trong danh sách station của AP)
 *  sang nodeId painlessMesh của node đó.
 *
 *  painlessMesh tính nodeId = tcp::encodeNodeId(MAC SoftAP) — 4 byte
 *  cuối, big-endian (painlessmesh/tcp.hpp, arduino/wifi.hpp init()).
 *  Nhưng station nối vào AP bằng MAC STA. Với ESP32, core 2.0.17
 *  (CONFIG_ESP32_UNIVERSAL_MAC_ADDRESSES=4): MAC SoftAP = MAC STA với
 *  byte cuối +1 (tràn 8 bit, không nhớ). setup() tự kiểm tra công
 *  thức trên chính board này (xem ap_map_self_check()).
 * ================================================================ */
static uint32_t sta_mac_to_node_id(const uint8_t* sta_mac) {
    uint8_t ap_mac[6];
    memcpy(ap_mac, sta_mac, 6);
    ap_mac[5] = (uint8_t)(ap_mac[5] + 1);
    return painlessmesh::tcp::encodeNodeId(ap_mac);
}

/* Áp sta_mac_to_node_id() lên MAC STA của chính board, phải ra đúng
 * mesh.getNodeId(). Sai → ap_rssi của node này sẽ gán nhầm node. */
static void ap_map_self_check() {
    uint8_t sta_mac[6];
    esp_read_mac(sta_mac, ESP_MAC_WIFI_STA);
    uint32_t derived = sta_mac_to_node_id(sta_mac);
    if (derived == mesh.getNodeId()) {
        Serial.println("[MESH] MAC->nodeId (ap_rssi): OK");
    } else {
        Serial.printf("[MESH] CANH BAO: MAC->nodeId sai (%u != %u), ap_rssi se khong khop node\n",
                      derived, mesh.getNodeId());
    }
}

/* ================================================================
 *  send_nstat()
 *
 *  Gửi trạng thái node cho Root: "#NSTAT" + JSON gọn. Root gom lại
 *  đưa vào telemetry để dashboard vẽ topology, RSSI, phiên bản fw.
 *
 *  rssi = WiFi.RSSI() của liên kết STA lên node cha. Nếu Leaf không
 *  có liên kết STA (WiFi.RSSI() trả 0) → gửi null, không gửi số giả.
 *
 *  ap_rssi = {"<nodeId>": dBm} của từng station đang nối vào AP của
 *  Leaf này (node con trong cây), đo phía AP. Không có station → {}.
 *  Chỉ gọi khi root_id != 0 (xem loop()).
 * ================================================================ */
void send_nstat() {
    char buf[384];
    char rssi_str[8];
    int8_t rssi = WiFi.RSSI();
    if (rssi != 0) snprintf(rssi_str, sizeof(rssi_str), "%d", (int)rssi);
    else           strcpy(rssi_str, "null");

    int pos = snprintf(buf, sizeof(buf),
             NSTAT_PREFIX "{\"id\":%u,\"fw\":\"" FW_VERSION "\",\"rssi\":%s,"
             "\"heap\":%u,\"frames\":%u,\"chunks_fail\":%u,\"rst\":%d,\"up_s\":%u,\"ap_rssi\":{",
             (unsigned)mesh.getNodeId(), rssi_str, (unsigned)ESP.getFreeHeap(),
             (unsigned)frames_sent, (unsigned)chunks_send_fail,
             (int)boot_reset_reason, (unsigned)(millis() / 1000));

    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        int n = sta_list.num < AP_RSSI_MAX ? sta_list.num : AP_RSSI_MAX;
        for (int i = 0; i < n && pos < (int)sizeof(buf) - 32; i++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s\"%u\":%d", i ? "," : "",
                            (unsigned)sta_mac_to_node_id(sta_list.sta[i].mac),
                            (int)sta_list.sta[i].rssi);
        }
    }
    snprintf(buf + pos, sizeof(buf) - pos, "}}");
    mesh.sendSingle(root_id, String(buf));
}


/* ================================================================
 *  MESH CALLBACKS
 *
 *  painlessMesh gọi các callback này khi có sự kiện mạng.
 *  Camera Node chỉ cần log để debug, không xử lý message nhận.
 * ================================================================ */

/**
 * Được gọi khi nhận message từ node khác trong Mesh.
 * Camera Node không xử lý chunk ảnh (chỉ Root mới reassemble),
 * nhưng lắng nghe riêng beacon Root để học root_id.
 */
void receivedCallback(uint32_t from, String &msg) {
    if (msg.startsWith(ROOT_BEACON_PREFIX)) {
        last_root_beacon_ms = millis();
        if (ROOT_NODE_ID_OVERRIDE == 0 && root_id != from) {
            root_id = from;
            Serial.printf("[MESH] Root ID = %u\n", root_id);
        }
        return;
    }
    // Bỏ qua — Camera Node không xử lý message nhận được khác
    // (vd chunk broadcast từ Leaf firmware cũ còn fallback broadcast).
}

/**
 * Được gọi khi một node mới kết nối vào Mesh.
 * nodeId là ID duy nhất của node mới (dựa trên chip ID ESP32).
 */
void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("[MESH] Ket noi moi: Node %u\n", nodeId);
}

/**
 * Được gọi khi topology mạng thay đổi (node join/leave).
 * In ra danh sách các node hiện có trong mạng.
 */
void changedConnectionCallback() {
    Serial.println("[MESH] Topology mang thay doi");
    auto nodes = mesh.getNodeList();
    Serial.printf("[MESH] Tong so node: %d\n", nodes.size());
    for (auto &id : nodes) {
        Serial.printf("  - Node: %u\n", id);
    }

    // Root vua roi khoi mesh (vd rut nguon) — quen root_id da hoc,
    // se hoc lai qua beacon khi Root (hoac Root khac) xuat hien lai.
    if (root_id != 0) {
        bool root_still_present = false;
        for (auto &id : nodes) {
            if (id == root_id) { root_still_present = true; break; }
        }
        if (!root_still_present) {
            root_id = ROOT_NODE_ID_OVERRIDE;
        }
    }
}


#if STAGGER_ENABLE
static uint8_t stagger_slot_of(uint32_t id) {
    for (const StaggerSlot& e : STAGGER_TABLE) {
        if (e.node_id == id) return e.slot % STAGGER_SLOTS;
    }
    return (uint8_t)(id % STAGGER_SLOTS);
}

/* stagger_next_mark(): mốc chụp đầu tiên SAU t (t' > t, t' ≡ offset mod P).
 * Tính trên uint32 như getNodeTime(): đồng hồ mesh quay vòng sau 2^32 µs
 * (~71,6 phút). 2^32 không chia hết cho P nên lưới mốc lệch một lần mỗi vòng,
 * nhưng mọi node quay vòng cùng lúc nên vẫn cùng một lưới. */
static uint32_t stagger_next_mark(uint32_t t) {
    uint32_t ph = (uint32_t)(((uint64_t)t + STAGGER_PERIOD_US - stagger_offset_us) % STAGGER_PERIOD_US);
    return t + (STAGGER_PERIOD_US - ph);
}

/* stagger_due(): đã tới lúc chụp chưa. d = t − next_capture_us (có dấu, nên
 * quay vòng uint32 không làm sai phép so sánh):
 *   0 ≤ d        → tới mốc (d > P: đồng hồ mesh nhảy tới / loop bị chặn lâu)
 *   d < −P       → mốc ở xa hơn 1 chu kỳ phía trước: đồng hồ mesh vừa LÙI
 *                  (mesh chỉnh giờ, vd lúc mới vào mesh) → chụp ngay.
 * Sau mỗi lần chụp, loop() đặt lại next_capture_us = mốc kế tiếp tính từ
 * THỜI ĐIỂM HIỆN TẠI → nhảy giờ chỉ gây 1 lần chụp lệch lịch, không chụp dồn. */
static bool stagger_due() {
    int32_t d = (int32_t)(mesh.getNodeTime() - next_capture_us);
    if (d >= 0 && d <= (int32_t)STAGGER_PERIOD_US) return true;
    if (d > (int32_t)STAGGER_PERIOD_US || d < -(int32_t)STAGGER_PERIOD_US) {
        Serial.printf("[STAG] dong ho mesh lech lich %ld us, chup ngay 1 lan roi bam lich moi\n", (long)d);
        return true;
    }
    return false;
}
#endif

/* ================================================================
 *  check_root_watchdog()
 *
 *  Gọi mỗi vòng loop(). Tính từ beacon Root cuối (hoặc lúc khởi động):
 *   - quá ROOT_LOST_RESET_MS: quên root_id → capture_and_send() ngừng
 *     chụp/gửi, send_nstat() ngừng; học lại khi beacon tới.
 *   - quá ROOT_LOST_RESTART_MS: ESP.restart() — thoát trạng thái kẹt
 *     (vd station treo, không vào lại được mesh).
 *  ROOT_NODE_ID_OVERRIDE khác 0 → giữ root_id ép cứng, chỉ còn restart.
 * ================================================================ */
void check_root_watchdog() {
    uint32_t now = millis();
    // Mốc "sau" now (không xảy ra với thứ tự gọi hiện tại) → 0, tránh 4294967295 ms → restart nhầm
    uint32_t since = ((int32_t)(now - last_root_beacon_ms) < 0) ? 0 : now - last_root_beacon_ms;

    if (ROOT_NODE_ID_OVERRIDE == 0 && root_id != 0 && since > ROOT_LOST_RESET_MS) {
        root_id = 0;
        Serial.printf("[WDT] mat beacon Root, hoc lai (%u ms khong co beacon)\n", (unsigned)since);
    }

    if (since > ROOT_LOST_RESTART_MS) {
        Serial.printf("[WDT] restart (%u ms khong co beacon Root)\n", (unsigned)since);
        delay(100);   // cho Serial kịp in
        ESP.restart();
    }
}


/* ================================================================
 *  SETUP — Khởi tạo một lần khi ESP32 khởi động
 * ================================================================ */
void setup() {
    boot_reset_reason = esp_reset_reason();

    /* Khởi tạo Serial để debug */
    Serial.begin(115200);
    delay(1000);  // Chờ Serial ổn định

    Serial.println();
    Serial.println("╔══════════════════════════════════════════╗");
    Serial.println("║  ESP32 Mesh Video — CAMERA NODE         ║");
    Serial.println("║  OV3660 JPEG → Chunk → painlessMesh     ║");
    Serial.println("╚══════════════════════════════════════════╝");
    Serial.println();

    /* ── Kiểm tra PSRAM ─────────────────────────────────────
     *
     * PSRAM (Pseudo-Static RAM) là bộ nhớ ngoài 4MB gắn trên
     * module ESP32-CAM. Bắt buộc có vì:
     *   - Frame buffer camera cần ~150KB (QVGA RGB565)
     *   - JPEG compressed buffer cũng nằm trong PSRAM
     *   - Internal SRAM chỉ có ~520KB (dùng cho stack, heap)
     *
     * Nếu không có PSRAM: kiểm tra:
     *   1. Arduino IDE → Tools → PSRAM → "Enabled"
     *   2. Board có module PSRAM gắn kèm (ESP32-CAM đều có)
     */
    if (psramFound()) {
        Serial.printf("[SYS] PSRAM: %d bytes kha dung\n", ESP.getFreePsram());
    } else {
        Serial.println("[SYS] CANH BAO: Khong tim thay PSRAM!");
        Serial.println("[SYS] Camera co the khong hoat dong.");
        Serial.println("[SYS] Kiem tra: Tools > PSRAM > Enabled");
    }

    Serial.printf("[SYS] Heap con lai: %d bytes\n", ESP.getFreeHeap());

    /* ── Khởi tạo Camera ────────────────────────────────────── */
    Serial.println("\n[CAM] Dang khoi tao camera OV3660...");
    if (!init_camera()) {
        Serial.println("[SYS] LOI NGHIEM TRONG: Camera khong khoi tao duoc!");
        Serial.println("[SYS] Dung hoat dong. Khoi dong lai ESP32.");
        while (true) { delay(1000); }  // Halt
    }

    /* Chụp thử 1 ảnh để warm up pipeline camera */
    camera_fb_t* test_fb = esp_camera_fb_get();
    if (test_fb) {
        Serial.printf("[CAM] Test frame: %d bytes\n", test_fb->len);
        esp_camera_fb_return(test_fb);
    }

    /* ── Khởi tạo painlessMesh ──────────────────────────────
     *
     * setDebugMsgTypes(): Chọn loại message debug hiển thị
     *   ERROR:   Lỗi nghiêm trọng
     *   STARTUP: Thông tin khởi tạo
     *
     * init(): Tạo Mesh network với SSID, password, port
     *   - ESP32 tạo AP riêng cho Mesh
     *   - Tự động tìm và kết nối các node khác
     *   - Tự xử lý routing multi-hop
     *
     * setContainsRoot(true): Báo cho Mesh biết mạng có Root
     *   Node. Mesh sẽ tổ chức topology hình cây với Root
     *   ở gốc, giúp routing hiệu quả hơn.
     */
    Serial.println("\n[MESH] Dang khoi tao painlessMesh...");

    // painlessMesh in log nay thang ra Serial Monitor cua Leaf (khong
    // qua khung UART nhi phan nhu Root, nhung van giam nhieu khi debug).
    // Muc ERROR hiem khi in nen chap nhan duoc.
    mesh.setDebugMsgTypes(ERROR);
    mesh.init(MESH_SSID, MESH_PASSWORD, MESH_PORT);

    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);

    mesh.setContainsRoot(true);

    Serial.printf("[MESH] Node ID: %u\n", mesh.getNodeId());
    Serial.printf("[MESH] Mesh SSID: %s\n", MESH_SSID);
    Serial.printf("[SYS] Firmware: %s\n", FW_VERSION);
    Serial.printf("[SYS] Ly do reset: %d\n", (int)boot_reset_reason);
    ap_map_self_check();
#if STAGGER_ENABLE
    stagger_slot      = stagger_slot_of(mesh.getNodeId());
    stagger_offset_us = (uint32_t)stagger_slot * (STAGGER_PERIOD_US / STAGGER_SLOTS);
    Serial.printf("[STAG] BAT: slot %u/%u, lech %u ms trong chu ky %u ms (dong ho mesh)\n",
                  stagger_slot, STAGGER_SLOTS, (unsigned)(stagger_offset_us / 1000), CAPTURE_INTERVAL_MS);
#else
    Serial.println("[STAG] TAT: chup theo millis() rieng tung board");
#endif

    /* ── Sẵn sàng ──────────────────────────────────────────── */
    Serial.println();
    Serial.println("========================================");
    Serial.printf("[SYS] San sang! Chup moi %d ms (~%.1f FPS)\n",
                  CAPTURE_INTERVAL_MS,
                  1000.0f / CAPTURE_INTERVAL_MS);
    Serial.printf("[SYS] Chunk size: %d bytes, delay: %d ms\n",
                  MAX_CHUNK_PAYLOAD, CHUNK_SEND_DELAY_MS);
    Serial.println("========================================\n");

    last_capture_time = millis();
    last_root_beacon_ms = millis();   // watchdog tính từ lúc khởi động nếu chưa có beacon
#if STAGGER_ENABLE
    next_capture_us = stagger_next_mark(mesh.getNodeTime());
#endif
}


/* ================================================================
 *  LOOP — Vòng lặp chính, chạy liên tục
 *
 *  Hai nhiệm vụ:
 *  1. mesh.update(): BẮT BUỘC gọi mỗi vòng loop để painlessMesh
 *     duy trì kết nối, xử lý routing, heartbeat.
 *  2. capture_and_send(): Chụp + gửi ảnh khi đến interval.
 *
 *  Không dùng delay() trong loop chính vì sẽ block mesh.update()
 *  và gây mất kết nối Mesh.
 * ================================================================ */
void loop() {
    /* Cập nhật Mesh — phải gọi THƯỜNG XUYÊN */
    mesh.update();

    check_root_watchdog();

    /* Kiểm tra lịch chụp ảnh */
    uint32_t now = millis();
#if STAGGER_ENABLE
    bool capture_due = stagger_due();                                  // đồng hồ mesh + slot
#else
    bool capture_due = (now - last_capture_time >= CAPTURE_INTERVAL_MS); // millis() riêng board
#endif
    if (capture_due) {
        last_capture_time = now;
        capture_and_send();
#if STAGGER_ENABLE
        // Mốc kế tiếp tính từ SAU khi gửi xong: gửi lâu quá 1 chu kỳ thì bỏ mốc, không chụp dồn
        next_capture_us = stagger_next_mark(mesh.getNodeTime());
#endif

        /* In thống kê định kỳ (mỗi 10 frame) */
        if (frames_sent > 0 && frames_sent % 10 == 0) {
            Serial.printf("[STAT] Frames: %d, Chunks: %d, ChunksFail: %d, Heap: %d, PSRAM: %d, RootID: %u\n",
                          frames_sent, chunks_sent, chunks_send_fail,
                          ESP.getFreeHeap(), ESP.getFreePsram(), root_id);
        }
    }

    /* Gửi NSTAT cho Root mỗi NSTAT_INTERVAL_MS — chỉ khi đã biết Root.
     * Lấy millis() mới vì capture_and_send() ở trên có thể mất vài trăm ms. */
    uint32_t now_nstat = millis();
    if (root_id != 0 && now_nstat - last_nstat_time >= NSTAT_INTERVAL_MS) {
        last_nstat_time = now_nstat;
        send_nstat();
    }
}
