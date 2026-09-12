/**
 * ================================================================
 *  NODE ROOT — ESP32 Mesh Video Streaming (Receiver / Server)
 * ================================================================
 *
 *  Chức năng chính:
 *  ────────────────
 *  1. Làm Root Node của mạng painlessMesh
 *  2. Nhận các chunk JPEG từ Camera Node qua Mesh callback
 *  3. Ghép (reassemble) các chunk thành ảnh JPEG hoàn chỉnh
 *  4. Phục vụ luồng MJPEG qua HTTP Web Server
 *  5. Xử lý timeout cho frame không hoàn chỉnh
 *
 *  Kiến trúc bộ nhớ:
 *  ─────────────────
 *  - Assembly buffers: Pre-allocated (PSRAM nếu có, ngược lại SRAM)
 *  - MAX_CONCURRENT_FRAMES × 64KB = 1 × 64KB ≈ 64KB (đã giảm từ 320KB)
 *  - Không còn latest_frame riêng: HTTP/UART đọc trực tiếp từ assembly buffer
 *  - Tất cả cấp phát 1 lần trong setup() → KHÔNG phân mảnh heap
 *
 *  Phần cứng yêu cầu:
 *  ──────────────────
 *  - ESP32 có hoặc KHÔNG có PSRAM (tự động fallback về SRAM nội bộ)
 *  - Khuyến nghị board có PSRAM khi cần nhiều slot đồng thời
 *  - KHÔNG cần camera (Root chỉ nhận và phát stream)
 *
 *  Thư viện cần cài (Arduino IDE Library Manager):
 *  ───────────────────────────────────────────────
 *  - painlessMesh
 *  - ArduinoJson (dependency)
 *  - TaskScheduler (dependency)
 *  - AsyncTCP (dependency)
 *
 *  Cách kết nối xem stream:
 *  ────────────────────────
 *  1. Root kết nối vào WiFi router qua mesh.stationManual()
 *  2. Xem Serial Monitor để biết IP được cấp
 *  3. Mở trình duyệt: http://<IP>/stream
 *  4. Hoặc: http://<IP>/ cho trang HTML có player
 *
 * ================================================================
 */

#include <painlessMesh.h>
#include <WiFi.h>
#include <stdarg.h>

/* ────────────────────────────────────────────────────────────
 *  Include shared headers
 *
 *  Copy các file này từ thư mục shared/ vào thư mục node_root/
 *  (cùng cấp với file .ino này). Xem README.md.
 * ──────────────────────────────────────────────────────────── */
#include "mesh_config.h"
#include "base64_util.h"
#include "chunk_protocol.h"
#include "crc16.h"   // CRC16/CCITT-FALSE cho giao thức UART -> middleware, xem UART_PROTOCOL.md


/* ================================================================
 *  CẤU TRÚC DỮ LIỆU — Frame Assembly
 *
 *  Mỗi slot FrameAssembly quản lý quá trình ghép MỘT ảnh JPEG
 *  từ nhiều chunk nhận rải rác qua Mesh.
 *
 *  Tại sao cần nhiều slot đồng thời?
 *  - Nhiều Camera Node gửi cùng lúc
 *  - Frame mới bắt đầu đến trước khi frame cũ ghép xong
 *  - Chunk có thể đến không theo thứ tự (out-of-order)
 *
 *  Chiến lược quản lý slot:
 *  - Mảng cố định MAX_CONCURRENT_FRAMES (4) slot
 *  - Mỗi slot có buffer PSRAM riêng (pre-allocated)
 *  - Khi hết slot → hủy slot cũ nhất (eviction)
 *  - Timeout: hủy frame chờ quá lâu → giải phóng slot
 * ================================================================ */
typedef struct {
    bool     active;          // Slot đang được sử dụng?
    uint32_t node_id;         // ID của Camera Node gửi frame này
    uint16_t frame_id;        // Số thứ tự frame đang ghép
    uint16_t total_chunks;    // Tổng số chunk cần nhận (từ header)
    uint16_t received_count;  // Số chunk đã nhận thành công
    uint32_t jpeg_total_len;  // Kích thước JPEG hoàn chỉnh (byte)

    /* Bitmap theo dõi chunk nào đã nhận
     *
     * chunk_received[i] = true → chunk #i đã nhận và copy vào buffer
     * Dùng để:
     *   - Tránh xử lý chunk trùng lặp (nếu Mesh gửi lại)
     *   - Kiểm tra chunk bị thiếu khi debug
     *   - Biết khi nào nhận ĐỦ (received_count == total_chunks)
     *
     * Kích thước 64: hỗ trợ tối đa 64 chunks/frame
     * = 64 × 1024 = 65536 bytes = MAX_FRAME_SIZE
     */
    bool chunk_received[MAX_CHUNKS_PER_FRAME];

    /* Buffer JPEG trong PSRAM
     *
     * Trỏ đến vùng nhớ pre-allocated trong PSRAM.
     * KHÔNG free/malloc runtime — chỉ tái sử dụng.
     * Các chunk được copy vào đúng offset:
     *   chunk #i → offset = i × MAX_CHUNK_PAYLOAD
     */
    uint8_t* jpeg_buffer;

    /* Timestamp bắt đầu ghép
     *
     * millis() khi nhận chunk ĐẦU TIÊN của frame.
     * Dùng để phát hiện timeout:
     *   if (millis() - start_time_ms > REASSEMBLY_TIMEOUT_MS)
     *     → hủy frame, giải phóng slot
     */
    uint32_t start_time_ms;

} FrameAssembly;


/* ================================================================
 *  BIẾN TOÀN CỤC
 * ================================================================ */

painlessMesh mesh;

/* ── Hệ thống ghép ảnh ──────────────────────────────────── */

// Mảng slot ghép ảnh — kích thước cố định, không dùng dynamic allocation
FrameAssembly assemblies[MAX_CONCURRENT_FRAMES];

// Buffer PSRAM pre-allocated cho mỗi slot
// Cấp phát 1 lần trong setup(), tái sử dụng mãi mãi
uint8_t* assembly_buffers[MAX_CONCURRENT_FRAMES];

/* ── Con trỏ và kích thước frame hoàn chỉnh mới nhất ────── */

/* [ĐÃ ĐỔI] Bỏ buffer latest_frame riêng biệt để tiết kiệm 64KB SRAM.
 * latest_frame bây giờ là con trỏ chỉ vào assembly_buffers[latest_slot]
 * (vùng nhớ pre-allocated dùng chung). HTTP server và send_image_frame()
 * đọc trực tiếp từ đó — an toàn vì slot chỉ bị tái dụng sau khi
 * new_frame_available được reset (single-thread Arduino loop). */
const uint8_t* latest_frame     = NULL;  // Trỏ vào assembly buffer, KHÔNG free
size_t         latest_frame_len = 0;

// Flag báo có frame mới cho HTTP streaming
// volatile vì có thể bị set trong callback (ngữ cảnh khác)
volatile bool new_frame_available = false;

/* ── HTTP Server ─────────────────────────────────────────── */

WiFiServer httpServer(HTTP_SERVER_PORT);

// Client đang streaming MJPEG (chỉ hỗ trợ 1 client tại một thời điểm)
WiFiClient streamClient;
bool client_streaming = false;

/* ── Thống kê ────────────────────────────────────────────── */
uint32_t frames_completed   = 0;    // Tổng frame ghép thành công
uint32_t frames_dropped     = 0;    // Tổng frame bị hủy (timeout/eviction)
uint32_t chunks_received    = 0;    // Tổng chunk đã nhận
uint32_t last_stats_time    = 0;    // Thời điểm in thống kê cuối

// Flag in IP một lần
bool ip_printed = false;

/* ── Self-healing: đo thời gian mesh khôi phục sau khi rớt kết nối ──
 *
 * last_disconnect_ms = 0  -> hiện không có sự cố nào đang chờ khôi phục
 * last_disconnect_ms != 0 -> mesh vừa mất 1 kết nối tại thời điểm này,
 *                            đang chờ changedConnectionCallback() báo
 *                            topology ổn định trở lại để tính elapsed.
 * last_self_heal_ms = -1  -> CHƯA từng có sự kiện rớt/khôi phục nào.
 *
 * LƯU Ý: đây là phép đo XẤP XỈ (lấy mốc theo lần đổi topology KẾ TIẾP
 * sau khi rớt), không phân biệt được 2 sự cố rớt xảy ra dồn dập. Đủ
 * dùng cho kịch bản đồ án (rút nguồn 1 node giữa để đo self-healing)
 * nhưng bạn nên đối chiếu lại với log thời gian thực khi demo.
 */
uint32_t last_disconnect_ms = 0;
int32_t  last_self_heal_ms  = -1;

/* [ĐÃ XÓA] uart_tx_buffer riêng đã bị bỏ để tiết kiệm 64KB+10B SRAM.
 * send_image_frame() bây giờ gửi sub-header 10 byte và JPEG data
 * theo 2 lần write riêng biệt — không cần buffer ghép lại. */


/* ================================================================
 *  UART_PROTOCOL.md — GỬI KHUNG NHỊ PHÂN CHO MIDDLEWARE (PC)
 *
 *  Root Node chỉ có 1 UART thật sự lộ ra ngoài (qua đầu nạp FTDI),
 *  nên TOÀN BỘ output — kể cả log debug — đi qua đây, đóng khung
 *  theo đúng ../UART_PROTOCOL.md để middleware Node.js tách được,
 *  thay vì in text thô sẽ làm hỏng luồng nhị phân của ảnh JPEG.
 *
 *  DBGLN(...) / DBGF(...) bên dưới THAY THẾ Serial.println()/Serial.printf()
 *  gốc ở khắp file này (xem macro cuối khối này).
 * ================================================================ */
#define FRAME_TYPE_IMAGE      0x01
#define FRAME_TYPE_TELEMETRY  0x02
#define FRAME_TYPE_LOG        0x03

static void send_uart_frame(uint8_t type, const uint8_t* payload, size_t len) {
    uint8_t header[7];
    header[0] = 0xAA;
    header[1] = 0x55;
    header[2] = type;
    header[3] = (uint8_t)(len & 0xFF);
    header[4] = (uint8_t)((len >> 8) & 0xFF);
    header[5] = (uint8_t)((len >> 16) & 0xFF);
    header[6] = (uint8_t)((len >> 24) & 0xFF);

    uint16_t crc = crc16_ccitt_false_init();
    crc = crc16_ccitt_false_update(crc, header + 2, 5); // TYPE + LEN, khong tinh 2 byte MAGIC
    if (len > 0 && payload != NULL) {
        crc = crc16_ccitt_false_update(crc, payload, len);
    }

    Serial.write(header, sizeof(header));
    if (len > 0 && payload != NULL) {
        Serial.write(payload, len);
    }
    uint8_t crc_bytes[2] = { (uint8_t)(crc & 0xFF), (uint8_t)((crc >> 8) & 0xFF) };
    Serial.write(crc_bytes, sizeof(crc_bytes));
}

/* log_framef()/log_frame_str(): thay Serial.printf()/Serial.println() cũ.
 * Bỏ ký tự '\n'/'\r' ở cuối vì khung đã tự có ranh giới rõ ràng, không
 * cần dấu xuống dòng để middleware biết hết message. */
static void log_frame_str(const char* msg) {
    size_t len = strlen(msg);
    while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) len--;
    send_uart_frame(FRAME_TYPE_LOG, (const uint8_t*)msg, len);
}

static void log_framef(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return;
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1; // vsnprintf da tu cat, tranh doc rac
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
    send_uart_frame(FRAME_TYPE_LOG, (const uint8_t*)buf, len);
}

#define DBGLN(x) log_frame_str(x)
#define DBGF(...) log_framef(__VA_ARGS__)

/* chunk_log() — hook mà shared/chunk_protocol.h gọi khi có lỗi giải mã
 * chunk (xem khai báo extern trong chunk_protocol.h). Ở node_root, route
 * về log_frame_str() để không in text thô đè lên luồng nhị phân UART. */
void chunk_log(const char* msg) {
    log_frame_str(msg);
}

/* send_telemetry_frame(): xem cấu trúc JSON trong UART_PROTOCOL.md §4.2.
 * Gọi định kỳ mỗi TELEMETRY_SEND_INTERVAL_MS từ loop(). */
static void send_telemetry_frame() {
    auto nodeList = mesh.getNodeList();

    char json[512];
    int pos = snprintf(json, sizeof(json),
        "{\"ts\":%lu,\"role\":\"root\",\"layer\":0,"
        "\"frames_ok\":%u,\"frames_drop\":%u,\"chunks\":%u,"
        "\"heap_free\":%u,\"psram_free\":%u,"
        "\"node_count\":%u,\"node_ids\":[",
        (unsigned long)millis(),
        (unsigned)frames_completed, (unsigned)frames_dropped, (unsigned)chunks_received,
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
        (unsigned)nodeList.size());

    bool first = true;
    for (auto& id : nodeList) {
        if (pos < 0 || pos >= (int)sizeof(json) - 16) break; // an toan, tranh tran buffer khi qua nhieu node
        int written = snprintf(json + pos, sizeof(json) - pos, "%s%u", first ? "" : ",", (unsigned)id);
        if (written < 0) break;
        pos += written;
        first = false;
    }
    if (pos < 0) pos = 0;
    pos += snprintf(json + pos, sizeof(json) - (size_t)pos, "],\"last_self_heal_ms\":%ld}", (long)last_self_heal_ms);

    send_uart_frame(FRAME_TYPE_TELEMETRY, (const uint8_t*)json, (size_t)pos);
}

/* send_image_frame(): 1 frame JPEG Root vừa ghép xong -> UART cho middleware.
 * Sub-header 10 byte (node_id + frame_id + jpeg_len) theo UART_PROTOCOL.md §4.1.
 *
 * [ĐÃ ĐỔI] Bỏ uart_tx_buffer pre-allocated (tiết kiệm 64KB+10B SRAM).
 * Thay bằng sub-header 10 byte trên stack, gửi header trước rồi gửi JPEG
 * riêng. CRC phải bao gồm cả 2 phần → dùng send_uart_frame() mở rộng
 * không thể dùng trực tiếp. Giải pháp: đóng gói thủ công theo đúng
 * UART_PROTOCOL.md — header[7] + sub_hdr[10] + jpeg + crc[2]. */
static void send_image_frame(uint32_t node_id, uint16_t frame_id, const uint8_t* jpeg, size_t jpeg_len) {
    if (jpeg == NULL || jpeg_len == 0) return;
    if (jpeg_len > MAX_FRAME_SIZE) {
        DBGF("[UART] Anh qua lon (%u > %u), bo qua gui UART", (unsigned)jpeg_len, (unsigned)MAX_FRAME_SIZE);
        return;
    }

    /* Sub-header 10 byte: node_id(4) + frame_id(2) + jpeg_len(4) */
    uint8_t sub_hdr[10];
    sub_hdr[0] = (uint8_t)(node_id & 0xFF);
    sub_hdr[1] = (uint8_t)((node_id >> 8) & 0xFF);
    sub_hdr[2] = (uint8_t)((node_id >> 16) & 0xFF);
    sub_hdr[3] = (uint8_t)((node_id >> 24) & 0xFF);
    sub_hdr[4] = (uint8_t)(frame_id & 0xFF);
    sub_hdr[5] = (uint8_t)((frame_id >> 8) & 0xFF);
    sub_hdr[6] = (uint8_t)(jpeg_len & 0xFF);
    sub_hdr[7] = (uint8_t)((jpeg_len >> 8) & 0xFF);
    sub_hdr[8] = (uint8_t)((jpeg_len >> 16) & 0xFF);
    sub_hdr[9] = (uint8_t)((jpeg_len >> 24) & 0xFF);

    /* Đóng gói khung UART thủ công:
     * [MAGIC 2B][TYPE 1B][LEN 4B][sub_hdr 10B][jpeg NB][CRC 2B]
     * LEN = 10 + jpeg_len (tổng payload)
     * CRC tính trên: TYPE + LEN + sub_hdr + jpeg */
    size_t total_payload = 10 + jpeg_len;
    uint8_t frame_hdr[7];
    frame_hdr[0] = 0xAA;
    frame_hdr[1] = 0x55;
    frame_hdr[2] = FRAME_TYPE_IMAGE;
    frame_hdr[3] = (uint8_t)(total_payload & 0xFF);
    frame_hdr[4] = (uint8_t)((total_payload >> 8) & 0xFF);
    frame_hdr[5] = (uint8_t)((total_payload >> 16) & 0xFF);
    frame_hdr[6] = (uint8_t)((total_payload >> 24) & 0xFF);

    uint16_t crc = crc16_ccitt_false_init();
    crc = crc16_ccitt_false_update(crc, frame_hdr + 2, 5); // TYPE + LEN
    crc = crc16_ccitt_false_update(crc, sub_hdr, 10);
    crc = crc16_ccitt_false_update(crc, jpeg, jpeg_len);

    Serial.write(frame_hdr, sizeof(frame_hdr));
    Serial.write(sub_hdr, sizeof(sub_hdr));
    Serial.write(jpeg, jpeg_len);
    uint8_t crc_bytes[2] = { (uint8_t)(crc & 0xFF), (uint8_t)((crc >> 8) & 0xFF) };
    Serial.write(crc_bytes, sizeof(crc_bytes));
}


/* ================================================================
 *  KHỞI TẠO BỘ NHỚ
 *
 *  [ĐÃ ĐỔI] Cấp phát buffer SRAM/PSRAM tự động qua alloc_buffer():
 *  - MAX_CONCURRENT_FRAMES assembly buffers × 64KB
 *    (hiện tại = 1 × 64KB = 64KB)
 *  - KHÔNG còn latest_frame buffer riêng (tiết kiệm 64KB)
 *  - KHÔNG còn uart_tx_buffer riêng (tiết kiệm 64KB+10B)
 *  - Tổng hiện tại: ~64KB — vừa SRAM nội bộ ESP32
 *
 *  Tại sao pre-allocate?
 *  ─────────────────────
 *  Trên MCU, malloc/free liên tục gây phân mảnh heap.
 *  Sau vài giờ chạy, heap bị phân mảnh đến mức không thể
 *  cấp phát block liên tục đủ lớn → crash.
 *
 *  Pre-allocate giải quyết triệt để:
 *  - Cấp phát 1 lần → không phân mảnh
 *  - Tái sử dụng buffer → không cần free
 *  - Nếu không đủ RAM → fail ngay lập tức trong setup()
 *    (thay vì crash random runtime)
 * ================================================================ */

/* alloc_buffer() — wrapper cấp phát bộ nhớ với fallback tự động.
 *
 * Ưu tiên PSRAM (ps_malloc) nếu board có PSRAM gắn sẵn.
 * Nếu không có hoặc ps_malloc thất bại, fallback về malloc()
 * trên SRAM nội bộ. Hành vi này cho phép code chạy được trên cả
 * ESP32-WROOM (không PSRAM) lẫn ESP32-CAM/WROVER (có PSRAM) mà
 * không cần sửa thêm gì sau này khi đổi board. */
static uint8_t* alloc_buffer(size_t size) {
    uint8_t* p = (uint8_t*)ps_malloc(size); // ưu tiên PSRAM nếu có
    if (p == NULL) p = (uint8_t*)malloc(size); // fallback SRAM nội bộ
    return p;
}

bool init_memory() {
    bool using_psram = psramFound();
    DBGF("[MEM] Loai bo nho: %s\n", using_psram ? "PSRAM" : "SRAM noi bo");
    DBGF("[MEM] Heap truoc cap phat: %d bytes\n", ESP.getFreeHeap());
    DBGF("[MEM] Du kien cap phat: %d bytes (%d KB)\n",
         MAX_CONCURRENT_FRAMES * (int)MAX_FRAME_SIZE,
         (MAX_CONCURRENT_FRAMES * (int)MAX_FRAME_SIZE) / 1024);

    /* Cấp phát buffer cho mỗi assembly slot */
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        /* [ĐÃ ĐỔI] ps_malloc() → alloc_buffer() để fallback về SRAM */
        assembly_buffers[i] = alloc_buffer(MAX_FRAME_SIZE);
        if (assembly_buffers[i] == NULL) {
            DBGF("[MEM] THAT BAI: Khong cap phat duoc buffer slot %d (%d bytes)\n",
                          i, MAX_FRAME_SIZE);
            return false;
        }

        // Gán buffer vào slot và đánh dấu trống
        assemblies[i].active      = false;
        assemblies[i].jpeg_buffer = assembly_buffers[i];
    }

    /* [ĐÃ XÓA] Không còn cấp phát latest_frame buffer riêng.
     * latest_frame bây giờ là con trỏ chỉ vào assembly_buffers[slot]
     * của frame vừa ghép xong — xem receive_and_reassemble(). */

    /* [ĐÃ XÓA] Không còn cấp phát uart_tx_buffer riêng.
     * send_image_frame() tự tạo sub-header 10 byte trên stack và
     * gửi thẳng từ assembly buffer mà không cần copy thêm. */

    size_t total_allocated = (size_t)MAX_CONCURRENT_FRAMES * MAX_FRAME_SIZE;
    DBGF("[MEM] Da cap phat %d assembly slot(s), tong: %d bytes (%d KB)\n",
                  MAX_CONCURRENT_FRAMES,
                  (int)total_allocated,
                  (int)(total_allocated / 1024));
    if (using_psram) {
        DBGF("[MEM] PSRAM con lai: %d bytes\n", ESP.getFreePsram());
    } else {
        DBGF("[MEM] Heap con lai sau cap phat: %d bytes\n", ESP.getFreeHeap());
    }

    return true;
}


/* ================================================================
 *  find_assembly_slot()
 *
 *  Tìm slot đang ghép cho cặp (node_id, frame_id).
 *
 *  Mỗi frame từ một camera cụ thể được xác định duy nhất bởi
 *  cặp (node_id, frame_id). Hàm này duyệt tuyến tính qua
 *  mảng assemblies[] để tìm slot khớp.
 *
 *  Tuyến tính O(n) với n = MAX_CONCURRENT_FRAMES (4) → rất nhanh.
 *
 *  Trả về: Index slot (0 đến MAX_CONCURRENT_FRAMES-1)
 *          hoặc -1 nếu không tìm thấy
 * ================================================================ */
int find_assembly_slot(uint32_t node_id, uint16_t frame_id) {
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (assemblies[i].active &&
            assemblies[i].node_id  == node_id &&
            assemblies[i].frame_id == frame_id) {
            return i;
        }
    }
    return -1;
}


/* ================================================================
 *  allocate_assembly_slot()
 *
 *  Cấp phát slot mới cho frame. Nếu không có slot trống,
 *  hủy slot CŨ NHẤT (LRU eviction) để tái sử dụng.
 *
 *  Chiến lược:
 *  1. Tìm slot có active = false → sử dụng
 *  2. Nếu hết slot → tìm slot có start_time_ms nhỏ nhất
 *     (chờ lâu nhất, ít khả năng hoàn thành) → hủy và tái dụng
 *
 *  LƯU Ý: Buffer PSRAM KHÔNG bị free/realloc.
 *  Chỉ reset metadata (active, node_id, ...) và tái sử dụng
 *  vùng nhớ PSRAM hiện có.
 * ================================================================ */
int allocate_assembly_slot(uint32_t node_id, uint16_t frame_id,
                           uint16_t total_chunks) {

    /* Bước 1: Tìm slot trống */
    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (!assemblies[i].active) {
            /* Khởi tạo slot mới */
            assemblies[i].active         = true;
            assemblies[i].node_id        = node_id;
            assemblies[i].frame_id       = frame_id;
            assemblies[i].total_chunks   = total_chunks;
            assemblies[i].received_count = 0;
            assemblies[i].jpeg_total_len = 0;
            assemblies[i].start_time_ms  = millis();

            // Reset bitmap — tất cả chunk chưa nhận
            memset(assemblies[i].chunk_received, 0,
                   sizeof(assemblies[i].chunk_received));

            DBGF("[ASM] Slot %d: bat dau ghep frame (node=%u, frame=%d, chunks=%d)\n",
                          i, node_id, frame_id, total_chunks);
            return i;
        }
    }

    /* Bước 2: Không có slot trống — eviction (hủy slot cũ nhất)
     *
     * Duyệt tìm slot có start_time_ms nhỏ nhất = chờ lâu nhất.
     * Frame chờ quá lâu thường đã mất gói, ít khả năng hoàn thành.
     */
    uint32_t oldest_time = UINT32_MAX;
    int      oldest_idx  = 0;

    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (assemblies[i].start_time_ms < oldest_time) {
            oldest_time = assemblies[i].start_time_ms;
            oldest_idx  = i;
        }
    }

    DBGF("[ASM] EVICTION: Huy slot %d (node=%u, frame=%d, nhan %d/%d chunks)\n",
                  oldest_idx,
                  assemblies[oldest_idx].node_id,
                  assemblies[oldest_idx].frame_id,
                  assemblies[oldest_idx].received_count,
                  assemblies[oldest_idx].total_chunks);

    frames_dropped++;

    /* Tái sử dụng slot — chỉ reset metadata, giữ nguyên buffer PSRAM */
    assemblies[oldest_idx].active         = true;
    assemblies[oldest_idx].node_id        = node_id;
    assemblies[oldest_idx].frame_id       = frame_id;
    assemblies[oldest_idx].total_chunks   = total_chunks;
    assemblies[oldest_idx].received_count = 0;
    assemblies[oldest_idx].jpeg_total_len = 0;
    assemblies[oldest_idx].start_time_ms  = millis();
    memset(assemblies[oldest_idx].chunk_received, 0,
           sizeof(assemblies[oldest_idx].chunk_received));

    return oldest_idx;
}


/* ================================================================
 *  receive_and_reassemble()
 *
 *  HÀM TRUNG TÂM: Nhận message Base64 từ Mesh, giải mã,
 *  trích xuất header, copy payload vào đúng vị trí buffer.
 *
 *  Luồng xử lý:
 *  ─────────────
 *  1. Base64 decode → binary (header 12 bytes + payload ≤1024 bytes)
 *  2. Validate header (chunk_index, total_chunks, payload_len)
 *  3. Tìm slot assembly cho (node_id, frame_id) hoặc tạo mới
 *  4. Kiểm tra chunk trùng lặp → bỏ qua nếu đã nhận
 *  5. Copy payload vào buffer tại offset = chunk_index × 1024
 *  6. Kiểm tra frame hoàn chỉnh → copy sang latest_frame
 *
 *  Xử lý chunk đến KHÔNG theo thứ tự (out-of-order):
 *  ──────────────────────────────────────────────────
 *  Vì mạng Mesh có thể route gói qua các đường khác nhau,
 *  chunk #5 có thể đến trước chunk #3. Thuật toán xử lý được
 *  nhờ dùng chunk_index để tính offset trực tiếp:
 *    offset = chunk_index × MAX_CHUNK_PAYLOAD
 *  → Không cần nhận theo thứ tự!
 * ================================================================ */
void receive_and_reassemble(uint32_t from, String &msg) {

    /* ── Bước 1: Giải mã Base64 → binary ───────────────────
     *
     * deserialize_chunk() trong chunk_protocol.h:
     *   - Decode Base64 string → binary buffer (static)
     *   - Trích xuất 12 bytes đầu → ChunkHeader
     *   - Copy phần còn lại → payload_buf
     *   - Validate kích thước và tính toàn vẹn
     */
    ChunkHeader header;
    uint8_t payload_buf[MAX_CHUNK_PAYLOAD];

    if (!deserialize_chunk(msg, header, payload_buf, MAX_CHUNK_PAYLOAD)) {
        // Lỗi decode: message bị corrupt, quá ngắn, hoặc không phải Base64
        return;
    }

    chunks_received++;

    /* ── Bước 2: Validate header ────────────────────────────
     *
     * Kiểm tra các giá trị hợp lý để phòng chống:
     *   - Buffer overflow (chunk_index >= total_chunks)
     *   - Memory corruption (payload_len > MAX_CHUNK_PAYLOAD)
     *   - Quá nhiều chunk (total_chunks > MAX_CHUNKS_PER_FRAME)
     */
    if (header.chunk_index >= header.total_chunks) {
        DBGF("[ASM] Header KHONG HOP LE: idx=%d >= total=%d\n",
                      header.chunk_index, header.total_chunks);
        return;
    }

    if (header.total_chunks > MAX_CHUNKS_PER_FRAME) {
        DBGF("[ASM] Qua nhieu chunk: %d > %d\n",
                      header.total_chunks, MAX_CHUNKS_PER_FRAME);
        return;
    }

    if (header.payload_len > MAX_CHUNK_PAYLOAD || header.payload_len == 0) {
        DBGF("[ASM] Payload khong hop le: %d bytes\n", header.payload_len);
        return;
    }

    /* ── Bước 3: Tìm hoặc tạo slot assembly ────────────────
     *
     * Tìm slot đang ghép cho cặp (node_id, frame_id):
     *   - Tìm thấy → thêm chunk vào slot đó
     *   - Không thấy → tạo slot mới (hoặc evict slot cũ nhất)
     */
    int slot = find_assembly_slot(header.node_id, header.frame_id);

    if (slot < 0) {
        // Slot mới — frame chưa từng thấy
        slot = allocate_assembly_slot(header.node_id, header.frame_id,
                                      header.total_chunks);
        if (slot < 0) {
            DBGLN("[ASM] Khong the cap phat slot!");
            return;
        }
    }

    FrameAssembly &fa = assemblies[slot];

    /* ── Bước 4: Kiểm tra chunk trùng lặp ──────────────────
     *
     * Trong mạng Mesh, cùng một message có thể bị nhận
     * nhiều lần (do broadcast hoặc retry). Bỏ qua nếu
     * chunk này đã nhận rồi.
     */
    if (fa.chunk_received[header.chunk_index]) {
        // Chunk đã nhận — bỏ qua (duplicate)
        return;
    }

    /* ── Bước 5: Copy payload vào đúng vị trí ──────────────
     *
     * THUẬT TOÁN GHÉP (REASSEMBLY):
     *
     * Buffer JPEG được chia thành các "ô" 1024 bytes:
     *
     *   Buffer:  [  slot 0  ][  slot 1  ][  slot 2  ]...[  slot N  ]
     *   Offset:  0          1024        2048             N×1024
     *   Chunk:   chunk #0    chunk #1    chunk #2         chunk #N
     *
     * Mỗi chunk chỉ cần biết chunk_index để tính offset:
     *   offset = chunk_index × MAX_CHUNK_PAYLOAD
     *
     * Payload được copy nguyên bản vào vị trí tương ứng.
     * Chunk cuối cùng có thể nhỏ hơn 1024 bytes.
     *
     * Ví dụ: JPEG 10500 bytes, 11 chunks
     *   chunk #0:  offset=0,     len=1024  → copy buf[0..1023]
     *   chunk #1:  offset=1024,  len=1024  → copy buf[1024..2047]
     *   ...
     *   chunk #10: offset=10240, len=260   → copy buf[10240..10499]
     *
     * Khi nhận đủ 11 chunk → JPEG hoàn chỉnh: buf[0..10499]
     */
    size_t offset = (size_t)header.chunk_index * MAX_CHUNK_PAYLOAD;

    // Kiểm tra tràn buffer (an toàn)
    if (offset + header.payload_len > MAX_FRAME_SIZE) {
        DBGF("[ASM] TRAN BUFFER: offset=%d + len=%d > %d\n",
                      (int)offset, header.payload_len, MAX_FRAME_SIZE);
        return;
    }

    // === COPY PAYLOAD VÀO BUFFER JPEG ===
    memcpy(fa.jpeg_buffer + offset, payload_buf, header.payload_len);

    // Đánh dấu chunk đã nhận
    fa.chunk_received[header.chunk_index] = true;
    fa.received_count++;

    /* Cập nhật tổng kích thước JPEG
     *
     * jpeg_total_len = vị trí byte cuối cùng đã ghi.
     * Luôn cập nhật bằng max(hiện tại, offset + payload_len)
     * để xử lý đúng khi chunk đến không theo thứ tự.
     */
    size_t chunk_end = offset + header.payload_len;
    if (chunk_end > fa.jpeg_total_len) {
        fa.jpeg_total_len = (uint32_t)chunk_end;
    }

    /* ── Bước 6: Kiểm tra frame hoàn chỉnh ─────────────────
     *
     * Frame hoàn chỉnh khi đã nhận ĐỦ tất cả chunk:
     *   received_count == total_chunks
     *
     * Khi hoàn chỉnh:
     * 1. Copy JPEG data từ assembly buffer → latest_frame
     * 2. Đặt flag new_frame_available = true
     * 3. Giải phóng slot (active = false)
     *
     * HTTP streaming loop sẽ kiểm tra flag và gửi frame.
     */
    if (fa.received_count >= fa.total_chunks) {
        DBGF("[ASM] HOAN THANH! node=%u, frame=%d, size=%d bytes, chunks=%d\n",
                      fa.node_id, fa.frame_id,
                      fa.jpeg_total_len, fa.total_chunks);

        /* [ĐÃ ĐỔI] Bỏ bước memcpy sang latest_frame buffer riêng.
         * Thay bằng: latest_frame trỏ thẳng vào fa.jpeg_buffer (assembly buffer).
         *
         * An toàn vì:
         * - Arduino loop chạy đơn luồng, không có race condition thật sự
         * - Slot (fa.active = false) chỉ bị tái dụng ở lần nhận frame tiếp theo,
         *   tức là sau khi loop() đã xử lý xong new_frame_available
         * - Với MAX_CONCURRENT_FRAMES=1, slot 0 luôn là slot duy nhất
         */
        latest_frame        = fa.jpeg_buffer; // trỏ trực tiếp, không copy
        latest_frame_len    = fa.jpeg_total_len;
        new_frame_available = true;

        // Gửi ngay qua UART cho middleware (song song với HTTP MJPEG cũ,
        // không thay thế — xem UART_PROTOCOL.md §4.1)
        send_image_frame(fa.node_id, fa.frame_id, fa.jpeg_buffer, fa.jpeg_total_len);

        // Giải phóng slot — buffer giữ nguyên cho tái sử dụng
        fa.active = false;

        frames_completed++;
    }
}


/* ================================================================
 *  cleanup_stale_frames()
 *
 *  Dọn dẹp các frame đang ghép dở nhưng đã quá timeout.
 *
 *  TẠI SAO CẦN CLEANUP?
 *  ─────────────────────
 *  Nếu Camera Node bị mất kết nối giữa chừng (hoặc một số
 *  chunk bị mất trên đường truyền), frame sẽ không bao giờ
 *  hoàn chỉnh. Nếu không hủy, slot sẽ bị chiếm mãi mãi.
 *
 *  Cơ chế:
 *  - Mỗi slot có start_time_ms (thời điểm nhận chunk đầu tiên)
 *  - Nếu millis() - start_time_ms > REASSEMBLY_TIMEOUT_MS (5s)
 *    → frame chắc chắn đã mất gói → hủy, giải phóng slot
 *
 *  Gọi hàm này định kỳ từ loop().
 * ================================================================ */
void cleanup_stale_frames() {
    uint32_t now = millis();

    for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        if (assemblies[i].active) {
            uint32_t elapsed = now - assemblies[i].start_time_ms;

            if (elapsed > REASSEMBLY_TIMEOUT_MS) {
                DBGF("[ASM] TIMEOUT: Huy frame (slot=%d, node=%u, frame=%d, nhan %d/%d, cho %d ms)\n",
                              i,
                              assemblies[i].node_id,
                              assemblies[i].frame_id,
                              assemblies[i].received_count,
                              assemblies[i].total_chunks,
                              elapsed);

                // Giải phóng slot — buffer PSRAM giữ nguyên
                assemblies[i].active = false;
                frames_dropped++;
            }
        }
    }
}


/* ================================================================
 *  HTTP MJPEG SERVER
 *
 *  Server HTTP đơn giản phục vụ 2 endpoint:
 *
 *  GET /        → Trang HTML với player MJPEG
 *  GET /stream  → Luồng MJPEG (multipart/x-mixed-replace)
 *
 *  Giao thức MJPEG over HTTP:
 *  ──────────────────────────
 *  Trình duyệt nhận response với Content-Type:
 *    multipart/x-mixed-replace; boundary=frame
 *
 *  Server liên tục gửi các phần (part), mỗi phần là 1 ảnh JPEG:
 *
 *    --frame\r\n
 *    Content-Type: image/jpeg\r\n
 *    Content-Length: 12345\r\n
 *    \r\n
 *    <JPEG binary data>
 *    \r\n
 *    --frame\r\n
 *    Content-Type: image/jpeg\r\n
 *    ...
 *
 *  Trình duyệt hiểu boundary "frame" và thay ảnh liên tục
 *  → tạo hiệu ứng video (Motion JPEG).
 *
 *  Thiết kế NON-BLOCKING:
 *  ──────────────────────
 *  Server KHÔNG block trong while loop chờ client.
 *  Thay vào đó, mỗi vòng loop():
 *  1. Kiểm tra client mới (httpServer.available())
 *  2. Nếu đang streaming: gửi frame mới nếu có
 *  3. Return ngay → mesh.update() được gọi liên tục
 * ================================================================ */

/**
 * Phục vụ trang HTML chính (GET /)
 *
 * Trang HTML đơn giản với:
 * - Tiêu đề "ESP32 Mesh Camera Stream"
 * - Thẻ <img src="/stream"> → trình duyệt tự request /stream
 * - CSS tối giản, nền tối, responsive
 */
void serve_html_page(WiFiClient &client) {
    String html = "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/html; charset=UTF-8\r\n"
                  "Connection: close\r\n\r\n"
                  "<!DOCTYPE html><html><head>"
                  "<meta charset='UTF-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>ESP32 Mesh Camera</title>"
                  "<style>"
                  "body{margin:0;background:#0a0a1a;color:#e0e0e0;"
                  "font-family:'Segoe UI',sans-serif;text-align:center;"
                  "min-height:100vh;display:flex;flex-direction:column;"
                  "align-items:center;justify-content:center}"
                  "h1{color:#4fc3f7;font-size:1.5em;margin-bottom:8px}"
                  ".subtitle{color:#888;font-size:0.85em;margin-bottom:20px}"
                  "img{max-width:95%;max-height:70vh;border:2px solid #1a237e;"
                  "border-radius:12px;box-shadow:0 4px 30px rgba(79,195,247,0.15)}"
                  ".stats{color:#666;font-size:0.75em;margin-top:15px}"
                  "</style></head><body>"
                  "<h1>&#128247; ESP32 Mesh Camera Stream</h1>"
                  "<p class='subtitle'>MJPEG Stream via painlessMesh Network</p>"
                  "<img src='/stream' alt='Live Stream'>"
                  "<p class='stats'>Endpoint: /stream | Target: 2-5 FPS</p>"
                  "</body></html>";
    client.print(html);
}

/**
 * Gửi MỘT frame JPEG qua MJPEG stream
 *
 * Format multipart:
 *   --frame\r\n
 *   Content-Type: image/jpeg\r\n
 *   Content-Length: <size>\r\n
 *   \r\n
 *   <JPEG data>
 *   \r\n
 *
 * Chia nhỏ khi gửi JPEG data (4KB/lần) để:
 * - Tránh timeout WiFi TCP stack
 * - Cho phép WiFi stack flush buffer
 * - Ổn định hơn trên ESP32
 */
void send_mjpeg_frame(WiFiClient &client) {
    if (!new_frame_available || latest_frame == NULL || latest_frame_len == 0) {
        return;  // Chưa có frame mới
    }

    /* Gửi boundary + header cho part này */
    String part_header = "--" MJPEG_BOUNDARY "\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: " + String(latest_frame_len) + "\r\n"
                         "\r\n";
    client.print(part_header);

    /* Gửi dữ liệu JPEG — chia thành block 4KB
     *
     * Tại sao chia nhỏ?
     * - WiFi TCP stack trên ESP32 có send buffer giới hạn
     * - Gửi block lớn (>8KB) có thể timeout hoặc bị drop
     * - 4KB là kích thước tối ưu cho ESP32 WiFi
     */
    const size_t SEND_BLOCK_SIZE = 4096;
    size_t bytes_sent = 0;

    while (bytes_sent < latest_frame_len) {
        size_t remaining = latest_frame_len - bytes_sent;
        size_t to_send   = (remaining > SEND_BLOCK_SIZE) ? SEND_BLOCK_SIZE : remaining;

        size_t written = client.write(latest_frame + bytes_sent, to_send);

        if (written == 0) {
            // Client ngắt kết nối giữa chừng
            DBGLN("[HTTP] Client ngat ket noi khi dang gui frame");
            break;
        }

        bytes_sent += written;
    }

    /* Kết thúc part bằng \r\n */
    client.print("\r\n");

    /* Đánh dấu đã gửi frame này */
    new_frame_available = false;
}

/**
 * handle_http_clients() — Gọi mỗi vòng loop()
 *
 * Xử lý non-blocking:
 * 1. Nếu chưa có client → kiểm tra kết nối mới
 * 2. Nếu đang stream → gửi frame mới nếu có
 * 3. Nếu client mất kết nối → cleanup
 */
void handle_http_clients() {

    /* ── Trường hợp 1: Chưa có client streaming ────────── */
    if (!client_streaming) {
        WiFiClient client = httpServer.available();
        if (!client) return;  // Không có client mới

        DBGLN("[HTTP] Client moi ket noi!");

        /* Đọc request line — timeout 2 giây
         *
         * Request HTTP dạng: "GET /stream HTTP/1.1\r\n..."
         * Chỉ cần đọc dòng đầu tiên để xác định endpoint.
         */
        String request_line = "";
        uint32_t read_timeout = millis() + 2000;

        while (client.connected() && millis() < read_timeout) {
            if (client.available()) {
                char c = client.read();
                request_line += c;
                if (c == '\n') break;  // Hết request line
            }
        }

        /* Đọc bỏ phần header còn lại
         * (trình duyệt gửi thêm User-Agent, Accept, ...)
         */
        uint32_t drain_timeout = millis() + 500;
        while (client.connected() && millis() < drain_timeout) {
            if (client.available()) {
                client.read();
            } else {
                delay(1);
            }
        }

        /* Xử lý request */
        if (request_line.indexOf("GET /stream") >= 0) {
            /* ── Bắt đầu MJPEG stream ──────────────────── */
            String response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY "\r\n"
                              "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                              "Pragma: no-cache\r\n"
                              "Connection: keep-alive\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "\r\n";
            client.print(response);

            streamClient     = client;
            client_streaming = true;

            DBGLN("[HTTP] MJPEG stream BAT DAU");

        } else if (request_line.indexOf("GET /status") >= 0) {
            /* ── Status endpoint (JSON) ─────────────────── */
            String json = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: application/json\r\n"
                          "Connection: close\r\n\r\n"
                          "{\"frames_ok\":" + String(frames_completed) +
                          ",\"frames_drop\":" + String(frames_dropped) +
                          ",\"chunks\":" + String(chunks_received) +
                          ",\"heap\":" + String(ESP.getFreeHeap()) +
                          ",\"psram\":" + String(ESP.getFreePsram()) +
                          ",\"nodes\":" + String(mesh.getNodeList().size()) + "}";
            client.print(json);
            delay(10);
            client.stop();

        } else {
            /* ── Trang HTML mặc định ────────────────────── */
            serve_html_page(client);
            delay(10);
            client.stop();
            DBGLN("[HTTP] Trang HTML da gui");
        }

        return;
    }

    /* ── Trường hợp 2: Đang streaming ──────────────────── */
    if (!streamClient.connected()) {
        // Client ngắt kết nối
        streamClient.stop();
        client_streaming = false;
        DBGLN("[HTTP] Stream client NGAT KET NOI");
        return;
    }

    /* Gửi frame mới nếu có */
    if (new_frame_available) {
        send_mjpeg_frame(streamClient);
    }
}


/* ================================================================
 *  MESH CALLBACKS
 * ================================================================ */

/**
 * Callback nhận message từ Mesh.
 *
 * Được painlessMesh gọi khi có message mới.
 * Chuyển tiếp cho receive_and_reassemble() xử lý.
 *
 * 'from' là Node ID của node gửi (có thể khác node_id trong header
 * nếu message được relay qua node trung gian — nhưng painlessMesh
 * giữ nguyên source ID nên 'from' = node_id gốc).
 */
void receivedCallback(uint32_t from, String &msg) {
    receive_and_reassemble(from, msg);
}

/**
 * Callback khi node mới kết nối vào Mesh.
 */
void newConnectionCallback(uint32_t nodeId) {
    DBGF("[MESH] Node moi ket noi: %u\n", nodeId);
}

/**
 * Callback khi MẤT một kết nối trong mesh (node rớt/di chuyển ra ngoài
 * vùng phủ sóng). Đánh dấu mốc thời gian để đo self-healing — xem
 * changedConnectionCallback() bên dưới, nơi tính khoảng thời gian đã trôi
 * qua khi mesh báo topology ổn định trở lại.
 *
 * LƯU Ý: tên hàm onDroppedConnection() theo API painlessMesh — nếu bản
 * thư viện bạn cài không có hàm này (một số version cũ hơn có thể khác
 * tên/không hỗ trợ), bỏ dòng đăng ký trong setup() và phần đo self-healing
 * sẽ không hoạt động (last_self_heal_ms luôn = -1), các phần khác không
 * bị ảnh hưởng.
 */
void droppedConnectionCallback(uint32_t nodeId) {
    last_disconnect_ms = millis();
    DBGF("[MESH] MAT KET NOI voi node %u — bat dau do thoi gian self-healing\n", nodeId);
}

/**
 * Callback khi topology Mesh thay đổi (bao gồm cả lúc mesh tự định tuyến
 * lại xong sau khi rớt 1 node — đây là điểm dùng để tính self-healing).
 */
void changedConnectionCallback() {
    auto nodes = mesh.getNodeList();
    DBGF("[MESH] Topology thay doi. Tong node: %d\n", nodes.size());
    for (auto &id : nodes) {
        DBGF("  - Node: %u\n", id);
    }

    if (last_disconnect_ms != 0) {
        last_self_heal_ms = (int32_t)(millis() - last_disconnect_ms);
        DBGF("[MESH] SELF-HEALING: mang khoi phuc sau %ld ms\n", (long)last_self_heal_ms);
        last_disconnect_ms = 0; // reset, san sang cho lan rot ket noi tiep theo
    }
}


/* ================================================================
 *  SETUP — Khởi tạo Root Node
 * ================================================================ */
void setup() {
    Serial.begin(PC_UART_BAUD);   // 921600bps — xem UART_PROTOCOL.md, KHONG con 115200 debug
    delay(1000);

    DBGLN("");
    DBGLN("╔══════════════════════════════════════════╗");
    DBGLN("║  ESP32 Mesh Video — ROOT NODE            ║");
    DBGLN("║  Reassembly + MJPEG HTTP Server          ║");
    DBGLN("╚══════════════════════════════════════════╝");
    DBGLN("");

    /* ── Kiểm tra PSRAM & chọn nguồn bộ nhớ ───────────────
     *
     * [ĐÃ ĐỔI] Bỏ khối dừng cứng khi không có PSRAM.
     * Code giờ tự động fallback về SRAM nội bộ qua alloc_buffer()
     * trong init_memory(). Log cảnh báo để dễ phân biệt khi test
     * trên Serial Monitor (baud 921600).
     */
    if (psramFound()) {
        DBGF("[SYS] PSRAM: CO (%d bytes kha dung) — se uu tien dung PSRAM\n",
             ESP.getFreePsram());
    } else {
        DBGLN("[SYS] PSRAM: KHONG CO — chuyen sang dung SRAM noi bo (dung luong nho hon)");
        DBGLN("[SYS] (Binh thuong voi ESP32-WROOM-32D, se chay duoc voi buffer giam)");
    }
    DBGF("[SYS] Heap truoc init_memory(): %d bytes kha dung\n", ESP.getFreeHeap());

    /* ── Cấp phát bộ nhớ ────────────────────────────────── */
    if (!init_memory()) {
        DBGLN("[SYS] LOI NGHIEM TRONG: Khong cap phat duoc bo nho du ca SRAM lan PSRAM!");
        DBGLN("[SYS] Giam MAX_CONCURRENT_FRAMES hoac MAX_FRAME_SIZE trong mesh_config.h");
        while (true) { delay(1000); }
    }

    /* ── Khởi tạo painlessMesh ──────────────────────────── */
    DBGLN("\n[MESH] Dang khoi tao painlessMesh (Root mode)...");

    mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
    mesh.init(MESH_SSID, MESH_PASSWORD, MESH_PORT);

    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onDroppedConnection(&droppedConnectionCallback);  // do self-healing, xem ghi chu tren ham nay

    /* Đặt node này làm ROOT
     *
     * setRoot(true): Node này là root của cây Mesh.
     *   - Không kết nối lên parent nào
     *   - Các node khác sẽ tự route về phía root
     *
     * setContainsRoot(true): Báo cho tất cả node biết
     *   mạng này CÓ root → tổ chức topology tối ưu.
     */
    mesh.setRoot(true);
    mesh.setContainsRoot(true);

    /* Kết nối vào WiFi router bên ngoài
     *
     * stationManual(): Yêu cầu painlessMesh kết nối STA
     * interface vào AP bên ngoài (WiFi router).
     *
     * Vì Root không có parent trong Mesh, STA interface
     * trống → dùng được cho WiFi router.
     *
     * Sau khi kết nối thành công:
     *   - Root có IP trên mạng WiFi router
     *   - HTTP server lắng nghe trên IP này
     *   - Client cùng WiFi có thể truy cập
     *
     * >>> THAY ĐỔI STATION_SSID & STATION_PASSWORD
     * >>> TRONG mesh_config.h <<<
     */
    mesh.stationManual(STATION_SSID, STATION_PASSWORD);

    DBGF("[MESH] Root Node ID: %u\n", mesh.getNodeId());
    DBGF("[MESH] Mesh SSID: %s\n", MESH_SSID);
    DBGF("[MESH] Ket noi WiFi: %s\n", STATION_SSID);

    /* ── Khởi tạo HTTP Server ───────────────────────────── */
    httpServer.begin();
    DBGF("[HTTP] Server KHOI DONG tren port %d\n", HTTP_SERVER_PORT);

    /* ── Thông tin cho người dùng ────────────────────────── */
    DBGLN("");
    DBGLN("========================================");
    DBGLN("[SYS] ROOT NODE da san sang!");
    DBGLN("[SYS] Dang cho ket noi WiFi de lay IP...");
    DBGF("[SYS] Reassembly timeout: %d ms\n", REASSEMBLY_TIMEOUT_MS);
    DBGF("[SYS] Max concurrent frames: %d\n", MAX_CONCURRENT_FRAMES);
    DBGLN("========================================\n");

    last_stats_time = millis();
}


/* ================================================================
 *  LOOP — Vòng lặp chính Root Node
 *
 *  Ba nhiệm vụ song song (non-blocking):
 *  1. mesh.update()         — Duy trì mạng Mesh
 *  2. cleanup_stale_frames() — Hủy frame timeout
 *  3. handle_http_clients() — Phục vụ HTTP MJPEG
 *
 *  Tất cả đều non-blocking để không ảnh hưởng lẫn nhau.
 * ================================================================ */
void loop() {
    /* [1] Cập nhật Mesh — BẮT BUỘC gọi mỗi vòng loop */
    mesh.update();

    /* [2] In IP khi WiFi kết nối thành công (một lần)
     *
     * Sau khi stationManual() thành công, ESP32 nhận IP từ
     * DHCP của router. In ra để user biết địa chỉ truy cập.
     */
    if (!ip_printed) {
        IPAddress ip = WiFi.localIP();
        if (ip != IPAddress(0, 0, 0, 0)) {
            DBGLN("");
            DBGLN("╔══════════════════════════════════════════╗");
            DBGF("║  HTTP Server: http://%s/           \n", ip.toString().c_str());
            DBGF("║  MJPEG Stream: http://%s/stream    \n", ip.toString().c_str());
            DBGF("║  Status JSON:  http://%s/status    \n", ip.toString().c_str());
            DBGLN("╚══════════════════════════════════════════╝");
            DBGLN("");
            ip_printed = true;
        }
    }

    /* [3] Dọn dẹp frame timeout */
    cleanup_stale_frames();

    /* [4] Phục vụ HTTP clients */
    handle_http_clients();

    /* [4b] Gửi khung TELEMETRY cho middleware định kỳ (xem UART_PROTOCOL.md §4.2) */
    static uint32_t last_telemetry_time = 0;
    uint32_t now_telemetry = millis();
    if (now_telemetry - last_telemetry_time >= TELEMETRY_SEND_INTERVAL_MS) {
        last_telemetry_time = now_telemetry;
        send_telemetry_frame();
    }

    /* [5] In thống kê định kỳ (mỗi 15 giây) */
    uint32_t now = millis();
    if (now - last_stats_time >= 15000) {
        last_stats_time = now;
        DBGF("[STAT] Frames OK: %d | Dropped: %d | Chunks: %d | "
                      "Heap: %d | PSRAM: %d | Nodes: %d\n",
                      frames_completed, frames_dropped, chunks_received,
                      ESP.getFreeHeap(), ESP.getFreePsram(),
                      mesh.getNodeList().size());
    }
}
