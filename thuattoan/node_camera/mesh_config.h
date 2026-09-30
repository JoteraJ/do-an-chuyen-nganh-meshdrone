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
 *  >>> THAY ĐỔI SSID VÀ PASSWORD WIFI ROUTER CỦA BẠN TẠI ĐÂY <<<
 * ============================================================ */
#define STATION_SSID       "YOUR_WIFI_SSID"       // SSID của WiFi router
#define STATION_PASSWORD   "YOUR_WIFI_PASSWORD"   // Password WiFi router

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
 *  Camera Node (Leaf) không tự ghép ảnh, nhưng cần các hằng số
 *  này khớp với Root để: (a) tự bỏ frame chụp được nếu > MAX_FRAME_SIZE
 *  trước khi gửi (xem capture_and_send()), tránh Root phải từ chối
 *  từng chunk; (b) REASSEMBLY_TIMEOUT_MS chỉ Root dùng nhưng vẫn
 *  đặt giống nhau để dễ đối chiếu cấu hình giữa 2 node.
 *
 *  MAX_CONCURRENT_FRAMES ở Leaf không được dùng (Leaf không có
 *  khái niệm slot ghép), giữ nguyên giá trị tùy ý.
 * ============================================================ */
#define REASSEMBLY_TIMEOUT_MS   2000    // Timeout hủy frame không hoàn chỉnh (ms) — khớp Root
#define MAX_CONCURRENT_FRAMES   4       // Không dùng ở Leaf, giữ để tham khảo
#define MAX_FRAME_SIZE          24576   // Kích thước tối đa một frame JPEG (bytes) = 24KB — khớp Root
#define MAX_CHUNKS_PER_FRAME    24      // MAX_FRAME_SIZE / MAX_CHUNK_PAYLOAD — khớp Root

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
#define CAPTURE_INTERVAL_MS   600              // Interval chụp ảnh (~1.7 FPS)

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
#define FW_VERSION                "2026-09-30a"
#define NSTAT_PREFIX              "#NSTAT"   // '#' khong thuoc bang ma Base64 -> khong nham voi chunk
#define NSTAT_INTERVAL_MS         2000
#define AP_RSSI_MAX               6         // so station toi da doc tu AP moi node (ap_rssi / link_rssi)

#endif // MESH_CONFIG_H
