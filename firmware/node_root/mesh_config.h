/**
 * ============================================================
 *  mesh_config.h — Cấu hình chung cho hệ thống Mesh Video
 * ============================================================
 *
 *  File này chứa TẤT CẢ các hằng số cấu hình dùng chung
 *  giữa Node Camera và Root Node:
 *    - Thông số mạng Mesh (SSID, password, port)
 *    - Thông số WiFi Station cho Root (để phục vụ HTTP)
 *    - Kích thước chunk, timeout, giới hạn bộ nhớ
 *    - Pin mapping camera OV3660 cho board ESP32-CAM AI-Thinker
 *    - Cấu hình HTTP MJPEG Server
 *
 *  *** Hướng dẫn sử dụng với Arduino IDE ***
 *  Copy file này vào thư mục sketch của cả node_camera/ và
 *  node_root/ (cùng cấp với file .ino), sau đó #include "mesh_config.h"
 *
 * ============================================================
 */

#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

/* ============================================================
 *  1. CẤU HÌNH MẠNG MESH (painlessMesh)
 *
 *  Tất cả các node trong mạng phải dùng chung SSID, password
 *  và port để tham gia cùng một topology Mesh.
 * ============================================================ */
#define MESH_SSID          "ESP32_MESH_VIDEO"     // Tên mạng Mesh nội bộ
#define MESH_PASSWORD      "MeshVideo2026"       // Mật khẩu Mesh (ít nhất 8 ký tự)
#define MESH_PORT          5555                   // TCP port cho giao tiếp Mesh

/* ============================================================
 *  2. CẤU HÌNH WIFI STATION CHO ROOT NODE
 *
 *  Root Node sẽ vừa chạy Mesh, vừa kết nối vào WiFi router
 *  bên ngoài thông qua mesh.stationManual(). Điều này cho phép
 *  trình duyệt web trên cùng mạng WiFi truy cập vào luồng
 *  MJPEG của Root.
 *
 *  >>> ĐIỀN SSID VÀ PASSWORD WIFI ROUTER TRONG secrets.h <<<
 * ============================================================ */
// SSID/mat khau that nam trong secrets.h (khong commit). Chua co secrets.h
// -> dung gia tri mau ben duoi, build van chay (chi khong vao duoc router).
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef STATION_SSID
#define STATION_SSID       "YOUR_WIFI_SSID"       // SSID của WiFi router
#endif
#ifndef STATION_PASSWORD
#define STATION_PASSWORD   "YOUR_WIFI_PASSWORD"   // Password WiFi router
#endif

/* ============================================================
 *  3. CẤU HÌNH GIAO THỨC TRUYỀN CHUNK
 *
 *  Mỗi ảnh JPEG được chia thành các chunk có kích thước cố định.
 *  Mỗi chunk đi kèm một header 16 byte chứa metadata (gồm capture_us).
 *
 *  Lưu ý: painlessMesh truyền dữ liệu dạng String, nên chunk
 *  binary sẽ được encode Base64 trước khi gửi. Base64 tăng
 *  kích thước ~33%, nên message tổng cộng mỗi chunk:
 *    = ceil((16 + 1024) / 3) * 4 = 1388 ký tự
 *  painlessMesh có thể xử lý message đến vài KB, nên vừa đủ.
 * ============================================================ */
#define MAX_CHUNK_PAYLOAD     1024    // Payload tối đa mỗi chunk (bytes)
#define CHUNK_HEADER_SIZE     16      // Kích thước header cố định (bytes) — phải = sizeof(ChunkHeader)
#define CHUNK_SEND_DELAY_MS   20      // Delay giữa các chunk (ms) — tránh tràn buffer

/* ============================================================
 *  4. CẤU HÌNH REASSEMBLY — GHÉP ẢNH TẠI ROOT NODE
 *
 *  Root cần quản lý nhiều frame đang ghép đồng thời (từ nhiều
 *  camera node hoặc frame mới chồng lên frame cũ).
 *
 *  REASSEMBLY_TIMEOUT_MS: Nếu sau khoảng thời gian này mà
 *  frame chưa nhận đủ chunk → hủy frame, giải phóng slot.
 *  Giảm 5000 → 2000: 1 frame gửi xong trong ~0,5s, chờ 5s chỉ
 *  làm giữ slot vô ích khi có nhiều Leaf tranh chấp slot.
 *
 *  MAX_CONCURRENT_FRAMES: Số slot ghép đồng thời. Mỗi slot
 *  chiếm MAX_FRAME_SIZE byte SRAM (Root KHÔNG có PSRAM).
 *
 *  [ĐÃ ĐỔI] 4 slot × 12KB = 48KB heap DEFAULT (2026-10-01c; trước đó
 *  4 × 24KB = 96KB làm heap DEFAULT lúc chạy chỉ còn ~45–48 KB, đáy 21 KB).
 *  Số slot phải ≥ số Leaf gửi đồng thời + 1 (cho frame chồng lấn),
 *  nếu không chunk của Leaf này sẽ hất frame đang ghép dở của Leaf
 *  kia ra ngoài (eviction chéo, drop rate cao). 4 slot đủ cho 3 Leaf.
 *
 *  MAX_FRAME_SIZE: Kích thước tối đa một ảnh JPEG. QVGA quality
 *  12: frame lớn nhất đo được 5887 B (test18, frame_max_bytes_boot),
 *  trung bình 2,3–8,4 KB → 12KB còn dư ~2×. Phải KHỚP Leaf
 *  (node_camera/mesh_config.h): Leaf bỏ frame lớn hơn trước khi gửi.
 * ============================================================ */
#define REASSEMBLY_TIMEOUT_MS   2000    // Timeout hủy frame không hoàn chỉnh (ms)
#define MAX_CONCURRENT_FRAMES   4       // Số frame đang ghép đồng thời tối đa
#define MAX_FRAME_SIZE          12288   // Kích thước tối đa một frame JPEG (bytes) = 12KB — khớp Leaf
#define MAX_CHUNKS_PER_FRAME    12      // MAX_FRAME_SIZE / MAX_CHUNK_PAYLOAD — khớp Leaf

#if (MAX_CHUNKS_PER_FRAME * MAX_CHUNK_PAYLOAD) < MAX_FRAME_SIZE
#error "MAX_CHUNKS_PER_FRAME qua nho so voi MAX_FRAME_SIZE"
#endif

/* ============================================================
 *  5. CẤU HÌNH CAMERA OV3660
 *
 *  FRAMESIZE_QVGA (320×240) là lựa chọn tối ưu cho Mesh:
 *    - File JPEG nhỏ (~8-15KB ở quality 12)
 *    - Chia thành ~10-15 chunks
 *    - Đủ để đạt 2-5 FPS trên băng thông Mesh
 *
 *  Nếu muốn giảm thêm: dùng FRAMESIZE_QQVGA (160×120)
 *  Nếu muốn tăng chất lượng: dùng FRAMESIZE_VGA nhưng FPS giảm
 *
 *  CAM_JPEG_QUALITY: 0-63, SỐ THẤP = CHẤT LƯỢNG CAO HƠN
 *  nhưng file lớn hơn. Khuyến nghị 10-15 cho Mesh.
 * ============================================================ */
#define CAM_FRAME_SIZE        FRAMESIZE_QVGA   // 320×240 pixels
#define CAM_JPEG_QUALITY      12               // Chất lượng JPEG (0-63)
#define CAM_FB_COUNT          1                // Số frame buffer camera
// CAPTURE_INTERVAL_MS: Root không chụp ảnh nên không dùng. Giá trị thật ở node_camera/mesh_config.h (600).

/* ============================================================
 *  6. PIN MAPPING — ESP32-CAM (AI-Thinker Module)
 *
 *  Đây là cấu hình pin cho board ESP32-CAM phổ biến nhất
 *  (module AI-Thinker). Board này tương thích cả OV2640 và
 *  OV3660 vì cùng giao diện DVP (Digital Video Port).
 *
 *  Nếu bạn dùng board khác, thay đổi pin tại đây:
 *    - ESP-EYE: khác pin PWDN, RESET
 *    - TTGO T-Camera: khác toàn bộ
 *    - M5Stack Camera: khác pin data
 *
 *  Sơ đồ kết nối DVP 8-bit:
 *    D0(Y2) → D7(Y9): 8 đường dữ liệu song song
 *    XCLK: Clock cấp cho sensor
 *    PCLK: Pixel clock từ sensor
 *    VSYNC, HREF: Tín hiệu đồng bộ frame/line
 *    SIOD, SIOC: I2C (SCCB) để cấu hình register sensor
 *    PWDN: Power down (LOW = active)
 * ============================================================ */
#define PWDN_GPIO_NUM      32
#define RESET_GPIO_NUM     -1     // -1 = không sử dụng (reset bằng phần mềm)
#define XCLK_GPIO_NUM       0
#define SIOD_GPIO_NUM      26     // I2C SDA (SCCB)
#define SIOC_GPIO_NUM      27     // I2C SCL (SCCB)

// Data pins — DVP 8-bit parallel interface
#define Y9_GPIO_NUM        35     // D7 (MSB)
#define Y8_GPIO_NUM        34     // D6
#define Y7_GPIO_NUM        39     // D5
#define Y6_GPIO_NUM        36     // D4
#define Y5_GPIO_NUM        21     // D3
#define Y4_GPIO_NUM        19     // D2
#define Y3_GPIO_NUM        18     // D1
#define Y2_GPIO_NUM         5     // D0 (LSB)

// Sync & Clock
#define VSYNC_GPIO_NUM     25     // Vertical sync
#define HREF_GPIO_NUM      23     // Horizontal reference
#define PCLK_GPIO_NUM      22     // Pixel clock

/* ============================================================
 *  7. CẤU HÌNH HTTP SERVER (ROOT NODE)
 *
 *  Root Node chạy một HTTP server đơn giản phục vụ:
 *    GET /       → Trang HTML có thẻ <img> trỏ tới /stream
 *    GET /stream → Luồng MJPEG multipart/x-mixed-replace
 *
 *  MJPEG_BOUNDARY: Chuỗi phân tách giữa các frame JPEG
 *  trong giao thức multipart. Trình duyệt dùng boundary này
 *  để biết khi nào một frame kết thúc và frame mới bắt đầu.
 * ============================================================ */
#define HTTP_SERVER_PORT    80          // Port HTTP server
#define MJPEG_BOUNDARY      "frame"    // Boundary cho multipart MJPEG

/* HTTP_ENABLE: 0 = Root KHÔNG khởi tạo web server, không poll client HTTP
 * mỗi vòng loop(), không chờ IP router. Ảnh/telemetry chỉ đi qua UART sang
 * middleware (đường dùng thật). 1 = bật lại code HTTP MJPEG cũ (giữ nguyên,
 * bọc #if trong node_root.ino); stationManual() vẫn đang comment. */
#define HTTP_ENABLE         0

/* ============================================================
 *  8. UART SANG MIDDLEWARE PC (xem ../UART_PROTOCOL.md)
 *
 *  Root Node gửi TOÀN BỘ output (log debug, ảnh JPEG đã ghép,
 *  telemetry) qua UART này dưới dạng khung nhị phân có CRC —
 *  KHÔNG in text thô như bản debug cũ, vì ESP32-CAM chỉ có một
 *  UART thật sự lộ ra ngoài (qua đầu nạp FTDI), phải dùng chung
 *  cho cả log lẫn dữ liệu nhị phân.
 *
 *  921600bps là quyết định đã chốt trong tài liệu đồ án (mục
 *  MIDDLEWARE & DASHBOARD) — bản debug cũ của file này vẫn để
 *  115200, đã sửa lại cho khớp.
 * ============================================================ */
#define PC_UART_BAUD               921600
#define TELEMETRY_SEND_INTERVAL_MS 2000    // Chu kỳ gửi khung TELEMETRY

/* TX ring buffer của UART0 (Serial.setTxBufferSize(), gọi TRƯỚC Serial.begin(),
 * có từ ESP32 Arduino core 2.0.x — đang dùng 2.0.17). Mặc định core = 0: chỉ có
 * FIFO phần cứng 128 B nên mọi Serial.write() chặn tới khi byte ra dây. Buffer
 * lấy từ heap: nếu heap Root lúc khởi động (log "[SYS] Heap ... Serial.begin")
 * xuống dưới ~80 KB thì giảm về 4096. send_image_frame() bỏ frame ảnh khi buffer
 * còn trống < cả khung ảnh (7 + 10 + JPEG + 2 byte) → đếm "uart_drop"; JPEG lớn hơn
 * UART_TX_BUFFER_SIZE − 19 vì vậy luôn bị bỏ (8192 → JPEG > 8173 B). */
#define UART_TX_BUFFER_SIZE        8192

/* ============================================================
 *  9. BEACON ROOT — Leaf tự học Root ID để dùng sendSingle()
 *
 *  Root phát định kỳ một message ngắn bắt đầu bằng
 *  ROOT_BEACON_PREFIX. Leaf lắng nghe, gán root_id = from khi
 *  thấy message này, rồi gửi chunk thẳng tới Root bằng
 *  sendSingle() thay vì sendBroadcast() tới toàn mạng.
 *
 *  '#' không thuộc bảng mã Base64 nên không thể nhầm với một
 *  chunk hợp lệ (mọi chunk đều là chuỗi Base64 thuần).
 * ============================================================ */
#define ROOT_BEACON_PREFIX        "#ROOT"   // '#' khong thuoc bang ma Base64 -> khong nham voi chunk
#define ROOT_BEACON_INTERVAL_MS   2000
#define ROOT_NODE_ID_OVERRIDE     0         // 0 = tu hoc qua beacon; khac 0 = ep dung ID nay
/* ============================================================
 *  10. PHIÊN BẢN FIRMWARE & TRẠNG THÁI NODE (NSTAT)
 *
 *  FW_VERSION: đổi mỗi lần nạp bản mới để dashboard biết board
 *  nào còn chạy firmware cũ. Leaf gửi kèm trong NSTAT; Root in
 *  ra lúc khởi động.
 *
 *  Leaf gửi mỗi NSTAT_INTERVAL_MS (chỉ khi đã biết root_id) một
 *  message "#NSTAT{...}" tới Root: id, fw, rssi, heap, frames,
 *  chunks_fail, ap_rssi. '#' không thuộc Base64 → Root tách được
 *  trước khi coi message là chunk.
 *
 *  ap_rssi: RSSI từng station đang nối vào AP của node này (đo
 *  phía AP, esp_wifi_ap_get_sta_list), khóa = nodeId của station.
 * ============================================================ */
#define FW_VERSION                "2026-10-01c"
#define NSTAT_PREFIX              "#NSTAT"   // '#' khong thuoc bang ma Base64 -> khong nham voi chunk
#define NSTAT_INTERVAL_MS         2000
#define AP_RSSI_MAX               6         // so station toi da doc tu AP moi node (ap_rssi / link_rssi)

/* ============================================================
 *  11. THEO DÕI NODE IM LẶNG & BẢO VỆ HEAP (chỉ Root)
 *
 *  SILENT_NODE_MS: node còn trong getNodeList() nhưng quá mức này
 *  không có chunk hợp lệ (tính từ chunk cuối, hoặc từ lúc node vào
 *  lại mesh) → "silent" trong telemetry. Root KHÔNG restart vì việc
 *  này — Leaf tự có watchdog riêng.
 *
 *  Heap guard: heap DEFAULT trống (esp_get_free_heap_size(), heap
 *  malloc dùng được) < HEAP_DEF_CRITICAL_BYTES liên tục quá
 *  HEAP_CRITICAL_MS → gửi khung LOG rồi ESP.restart(). Biện pháp
 *  cuối cho Root. Test17 (3 Leaf): DEFAULT ổn định ~45 KB, đáy
 *  trong cửa sổ thường ~42 KB, thấp nhất 25 KB. (Trước 2026-10-01b
 *  guard đo ESP.getFreeHeap() = INTERNAL, cao hơn DEFAULT ~47 KB.)
 * ============================================================ */
#define SILENT_NODE_MS            10000
#define HEAP_DEF_CRITICAL_BYTES   16384
#define HEAP_CRITICAL_MS          10000

#endif // MESH_CONFIG_H
