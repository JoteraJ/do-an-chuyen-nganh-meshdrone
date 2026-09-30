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
 *  - MAX_CONCURRENT_FRAMES × MAX_FRAME_SIZE = 4 × 24KB = 96KB
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
#include <esp_wifi.h>
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

// 1 = in log moi frame hoan thanh/bat dau ghep (debug), 0 = tat khi do dac hieu nang
#define DEBUG_ASM_VERBOSE 0


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
     * Kích thước 24: hỗ trợ tối đa 24 chunks/frame
     * = 24 × 1024 = 24576 bytes = MAX_FRAME_SIZE
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

    /* capture_us của frame (từ header chunk đầu tiên — Leaf gán cùng
     * giá trị cho mọi chunk). Dùng tính latency khi ghép xong. */
    uint32_t capture_us;

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

/* ── Latency chụp (Leaf) → ghép xong (Root) ─────────────────
 *
 * latency_us = mesh.getNodeTime() lúc ghép xong − capture_us trong header.
 * Cả hai đều là đồng hồ mesh đã đồng bộ (KHÔNG dùng millis() của 2 board).
 * Gom mẫu trong 1 chu kỳ telemetry, gửi đi rồi reset (xem
 * send_telemetry_frame()). Mẫu > LATENCY_MAX_VALID_US bị bỏ — coi là
 * đồng hồ mesh chưa đồng bộ xong (vd Leaf vừa join), kể cả trường hợp
 * đồng hồ Leaf chạy trước Root khiến phép trừ không dấu ra số rất lớn.
 */
#define LATENCY_MAX_VALID_US  10000000UL   // 10 giây
#define LAT_MAX_NODES         8            // số Leaf tối đa theo dõi riêng mỗi chu kỳ

uint64_t lat_sum_us = 0;
uint32_t lat_min_us = 0;
uint32_t lat_max_us = 0;
uint32_t lat_n      = 0;

struct NodeLatency { uint32_t node_id; uint64_t sum_us; uint32_t n; };
NodeLatency lat_by_node[LAT_MAX_NODES];
uint8_t     lat_node_count = 0;

/* ── Trạng thái từng Leaf (NSTAT) ──────────────────────────
 *
 * Leaf gửi "#NSTAT{...}" mỗi NSTAT_INTERVAL_MS (xem node_camera.ino
 * send_nstat()). Root chỉ giữ bản MỚI NHẤT của mỗi node + millis() lúc
 * nhận, rồi đưa vào telemetry ("nodes"). Không log mỗi lần nhận.
 *
 * Node đã rời mesh vẫn được liệt kê (hop = null) thêm NSTAT_FORGET_MS
 * để dashboard kịp vẽ mờ "mất tín hiệu", sau đó bỏ khỏi telemetry.
 */
#define NSTAT_MAX_NODES   8
#define NSTAT_FORGET_MS   60000UL
#define LINK_AP_MAX_AGE_MS 6000UL   // ap_rssi trong NSTAT cũ hơn mức này → coi như không biết AP của node đó

/* RSSI đo phía AP: các station đang nối vào AP của một node, khóa = nodeId
 * (xem sta_mac_to_node_id()). Root tự đọc của mình; Leaf gửi trong "ap_rssi". */
struct ApLinks {
    uint8_t  n;
    uint32_t ids[AP_RSSI_MAX];
    int8_t   rssi[AP_RSSI_MAX];
};

struct NodeStat {
    uint32_t id;         // 0 = ô trống
    char     fw[16];
    bool     has_rssi;
    int16_t  rssi;       // WiFi.RSSI() phía station của chính node này (lên node cha)
    uint32_t heap;
    uint32_t rx_ms;      // millis() lúc nhận NSTAT gần nhất
    ApLinks  ap;         // "ap_rssi": các node con nối vào AP của node này
    bool     ap_known;   // NSTAT có trường "ap_rssi" (kể cả {}); false = Leaf firmware cũ
    bool     has_rst;    // Leaf firmware cũ không gửi "rst"/"up_s" → null trong telemetry
    int16_t  rst;        // esp_reset_reason() lúc Leaf khởi động
    bool     has_up;
    uint32_t up_s;       // uptime Leaf (giây) trong NSTAT gần nhất
};
NodeStat node_stats[NSTAT_MAX_NODES];

/* ── Theo dõi chunk từng node: phát hiện node "im lặng" ─────
 *
 * Mảng tĩnh, cập nhật ở MỖI chunk hợp lệ (on_valid_chunk()) nên không dùng
 * String/malloc. Tập node trong mesh cập nhật ở update_node_tracking()
 * (changedConnectionCallback + mỗi TRACK_UPDATE_MS trong loop()).
 * silent = còn trong getNodeList() nhưng quá SILENT_NODE_MS không có chunk,
 * tính từ chunk cuối hoặc từ lúc node (vào lại) mesh nếu sau đó chưa có chunk. */
#define TRACK_MAX_NODES   8
#define TRACK_UPDATE_MS   500

struct NodeTrack {
    uint32_t id;             // 0 = ô trống
    uint32_t last_chunk_ms;  // millis() chunk hợp lệ cuối, 0 = chưa có
    uint32_t seen_ms;        // millis() lúc node (lại) xuất hiện trong getNodeList()
    bool     in_list;        // có trong getNodeList() ở lần cập nhật gần nhất
    bool     silent;
};
NodeTrack node_track[TRACK_MAX_NODES];
uint32_t  silent_events = 0;    // số lần một node chuyển sang im lặng (từ lúc khởi động)

static void record_latency(uint32_t node_id, uint32_t capture_us) {
    // Phép trừ không dấu 32 bit tự xử lý quay vòng của getNodeTime() (~71 phút)
    uint32_t latency_us = (uint32_t)(mesh.getNodeTime() - capture_us);
    if (latency_us > LATENCY_MAX_VALID_US) return;

    if (lat_n == 0 || latency_us < lat_min_us) lat_min_us = latency_us;
    if (lat_n == 0 || latency_us > lat_max_us) lat_max_us = latency_us;
    lat_sum_us += latency_us;
    lat_n++;

    for (uint8_t i = 0; i < lat_node_count; i++) {
        if (lat_by_node[i].node_id == node_id) {
            lat_by_node[i].sum_us += latency_us;
            lat_by_node[i].n++;
            return;
        }
    }
    if (lat_node_count < LAT_MAX_NODES) {
        lat_by_node[lat_node_count++] = { node_id, latency_us, 1 };
    }
}

static NodeStat* find_node_stat(uint32_t id) {
    for (uint8_t i = 0; i < NSTAT_MAX_NODES; i++) {
        if (node_stats[i].id == id) return &node_stats[i];
    }
    return NULL;
}

static void store_nstat(uint32_t from, const String& msg) {
    JsonDocument doc;
    if (deserializeJson(doc, msg.c_str() + strlen(NSTAT_PREFIX))) return; // JSON hỏng → bỏ

    NodeStat* ns = find_node_stat(from);
    if (ns == NULL) {
        // Ô trống, nếu hết thì ghi đè ô nhận lâu nhất
        uint8_t oldest = 0;
        for (uint8_t i = 0; i < NSTAT_MAX_NODES; i++) {
            if (node_stats[i].id == 0) { oldest = i; break; }
            if (node_stats[i].rx_ms < node_stats[oldest].rx_ms) oldest = i;
        }
        ns = &node_stats[oldest];
    }

    ns->id = from;   // tin 'from' của painlessMesh hơn trường "id" trong JSON

    // Chỉ giữ ký tự an toàn để chèn thẳng vào JSON telemetry
    const char* fw = doc["fw"] | "";
    uint8_t k = 0;
    for (; *fw && k < sizeof(ns->fw) - 1; fw++) {
        char c = *fw;
        if (isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_') ns->fw[k++] = c;
    }
    ns->fw[k] = '\0';

    ns->has_rssi = doc["rssi"].is<int>();
    ns->rssi     = ns->has_rssi ? (int16_t)doc["rssi"].as<int>() : 0;
    ns->heap     = doc["heap"] | 0u;
    ns->rx_ms    = millis();
    ns->has_rst  = doc["rst"].is<int>();
    ns->rst      = ns->has_rst ? (int16_t)doc["rst"].as<int>() : 0;
    ns->has_up   = doc["up_s"].is<uint32_t>();
    ns->up_s     = ns->has_up ? doc["up_s"].as<uint32_t>() : 0;

    // "ap_rssi": {"<nodeId>": dBm} — Leaf firmware cũ không gửi → ap.n = 0
    ns->ap.n = 0;
    ns->ap_known = doc["ap_rssi"].is<JsonObject>();
    for (JsonPair kv : doc["ap_rssi"].as<JsonObject>()) {
        if (ns->ap.n >= AP_RSSI_MAX) break;
        if (!kv.value().is<int>()) continue;
        uint32_t id = strtoul(kv.key().c_str(), NULL, 10);
        if (id == 0) continue;
        ns->ap.ids[ns->ap.n]  = id;
        ns->ap.rssi[ns->ap.n] = (int8_t)kv.value().as<int>();
        ns->ap.n++;
    }
}

/* sta_mac_to_node_id(): MAC station (trong danh sách station của AP) → nodeId.
 *
 * painlessMesh tính nodeId = tcp::encodeNodeId(MAC SoftAP) — 4 byte cuối,
 * big-endian (painlessmesh/tcp.hpp, arduino/wifi.hpp init()). Station nối vào
 * AP bằng MAC STA; với ESP32, core 2.0.17 (CONFIG_ESP32_UNIVERSAL_MAC_ADDRESSES=4)
 * MAC SoftAP = MAC STA với byte cuối +1 (tràn 8 bit, không nhớ). setup() tự
 * kiểm tra công thức trên chính board (ap_map_self_check()). */
static uint32_t sta_mac_to_node_id(const uint8_t* sta_mac) {
    uint8_t ap_mac[6];
    memcpy(ap_mac, sta_mac, 6);
    ap_mac[5] = (uint8_t)(ap_mac[5] + 1);
    return painlessmesh::tcp::encodeNodeId(ap_mac);
}

/* Đọc RSSI từng station đang nối vào AP của Root (node con hop 1). */
static void read_own_ap_links(ApLinks& a) {
    a.n = 0;
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) != ESP_OK) return;
    for (int i = 0; i < sta_list.num && a.n < AP_RSSI_MAX; i++) {
        a.ids[a.n]  = sta_mac_to_node_id(sta_list.sta[i].mac);
        a.rssi[a.n] = sta_list.sta[i].rssi;
        a.n++;
    }
}

static bool ap_links_find(const ApLinks& a, uint32_t id, int& rssi) {
    for (uint8_t i = 0; i < a.n; i++) {
        if (a.ids[i] == id) { rssi = a.rssi[i]; return true; }
    }
    return false;
}

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

/* json_appendf(): ghi tiếp vào buf tại pos, không bao giờ để pos vượt buf
 * (bị cắt -> JSON hỏng, middleware tự bỏ qua khung đó thay vì tràn bộ nhớ). */
static void json_appendf(char* buf, size_t sz, int& pos, const char* fmt, ...) {
    if (pos >= (int)sz - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + pos, sz - (size_t)pos, fmt, args);
    va_end(args);
    if (n > 0) pos = (pos + n > (int)sz - 1) ? (int)sz - 1 : pos + n;
}

/* chunk_log() — hook mà shared/chunk_protocol.h gọi khi có lỗi giải mã
 * chunk (xem khai báo extern trong chunk_protocol.h). Ở node_root, route
 * về log_frame_str() để không in text thô đè lên luồng nhị phân UART.
 *
 * Giới hạn tối đa 1 dòng / CHUNK_LOG_INTERVAL_MS: gom các lỗi cùng loại
 * (phần trước ": " của message) thành 1 dòng
 *   "[CHUNK] Size mismatch x<N> (decoded=..., expected=...)"
 * với chi tiết của lỗi gần nhất. Mọi lỗi vẫn được đếm vào chunk_errors
 * (gửi trong telemetry "chunk_err"). Lỗi khác loại trong cùng cửa sổ
 * được đếm vào "+M loi khac". chunk_log_flush() gọi thêm từ loop() để
 * dòng tổng kết cuối đợt lỗi vẫn được in ra. */
#define CHUNK_LOG_INTERVAL_MS 1000

uint32_t chunk_errors = 0;               // tổng lỗi chunk từ lúc khởi động

static char     chunk_err_kind[48];
static char     chunk_err_detail[64];
static uint32_t chunk_err_kind_n     = 0; // số lỗi loại chunk_err_kind trong cửa sổ
static uint32_t chunk_err_other_n    = 0; // số lỗi loại khác trong cửa sổ
static uint32_t chunk_err_last_print = 0;

static void chunk_log_flush() {
    if (chunk_err_kind_n == 0) return;
    uint32_t now = millis();
    if (now - chunk_err_last_print < CHUNK_LOG_INTERVAL_MS) return;

    char line[160];
    int pos = 0;
    json_appendf(line, sizeof(line), pos, "%s x%u", chunk_err_kind, (unsigned)chunk_err_kind_n);
    if (chunk_err_detail[0]) json_appendf(line, sizeof(line), pos, " (%s)", chunk_err_detail);
    if (chunk_err_other_n)   json_appendf(line, sizeof(line), pos, " +%u loi khac", (unsigned)chunk_err_other_n);
    log_frame_str(line);

    chunk_err_kind_n = 0;
    chunk_err_other_n = 0;
    chunk_err_last_print = now;
}

/* chunk_logf(): như chunk_log() nhưng nhận format — lỗi validate header
 * trong receive_and_reassemble() cũng đi qua bộ giới hạn 1 dòng/giây.
 * (chunk_log() đã khai báo extern trong chunk_protocol.h.) */
static void chunk_logf(const char* fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    chunk_log(buf);
}

void chunk_log(const char* msg) {
    chunk_errors++;

    const char* sep  = strstr(msg, ": ");
    size_t      klen = sep ? (size_t)(sep - msg) : strlen(msg);
    if (klen > sizeof(chunk_err_kind) - 1) klen = sizeof(chunk_err_kind) - 1;

    if (chunk_err_kind_n == 0) {
        memcpy(chunk_err_kind, msg, klen);
        chunk_err_kind[klen] = '\0';
    } else if (strlen(chunk_err_kind) != klen || strncmp(chunk_err_kind, msg, klen) != 0) {
        chunk_err_other_n++;
        chunk_log_flush();
        return;
    }
    chunk_err_kind_n++;
    strlcpy(chunk_err_detail, sep ? sep + 2 : "", sizeof(chunk_err_detail));
    chunk_log_flush();
}

/* collect_hops(): duyệt cây từ mesh.asNodeTree() (Root ở gốc, depth 0),
 * ghi (node_id, hop, node cha) của mọi node khác vào ids[]/hops[]/parents[].
 * Nối thẳng Root = hop 1. Dừng ghi khi đầy mảng. */
#define TREE_MAX_NODES 16

static void collect_hops(const painlessmesh::protocol::NodeTree& t, uint8_t depth,
                         uint32_t* ids, uint8_t* hops, uint32_t* parents, uint8_t& n) {
    for (auto& s : t.subs) {
        if (n >= TREE_MAX_NODES) return;
        // Root vọng lại trong nhánh con (vòng liên kết, test12) → bỏ cả nhánh đó
        if (s.nodeId == mesh.getNodeId()) continue;
        ids[n] = s.nodeId;
        hops[n] = depth + 1;
        parents[n] = t.nodeId;
        n++;
        collect_hops(s, depth + 1, ids, hops, parents, n);
    }
}

/* ap_links_of(): danh sách station đang nối vào AP của node x.
 * Root: đọc trực tiếp (root_ap). Leaf: "ap_rssi" trong NSTAT, chỉ khi Leaf
 * có gửi trường này và NSTAT chưa quá LINK_AP_MAX_AGE_MS. NULL = không biết. */
static const ApLinks* ap_links_of(uint32_t x, const ApLinks& root_ap, uint32_t now) {
    if (x == mesh.getNodeId()) return &root_ap;
    const NodeStat* s = find_node_stat(x);
    if (s && s->ap_known && now - s->rx_ms <= LINK_AP_MAX_AGE_MS) return &s->ap;
    return NULL;
}

/* root_sta_peer(): nodeId của node mà station của Root đang nối vào AP của
 * nó (0 = không nối). BSSID là MAC SoftAP của node kia → encodeNodeId() trực
 * tiếp, giống painlessMeshSTA.cpp (encodeNodeId(ap->bssid)). */
static uint32_t root_sta_peer() {
    if (!WiFi.isConnected()) return 0;
    uint8_t* bssid = WiFi.BSSID();
    return bssid ? painlessmesh::tcp::encodeNodeId(bssid) : 0;
}

/* link_rssi_for(): RSSI của liên kết (cha, con) trong cây. Mỗi liên kết
 * painlessMesh là station của một bên nối vào AP của bên kia, nên chỉ một
 * trong hai AP thấy bên kia. Thứ tự ưu tiên:
 *   1. "ap_parent": AP của cha thấy con.
 *   2. "ap_child":  AP của con thấy cha (liên kết chiều ngược: station của
 *                   cha nối vào AP của con).
 *   3. "sta":       "rssi" phía station của con — chỉ khi chắc station con
 *                   đang nối vào cha: AP của con đã biết mà không có cha, và
 *                   AP của cha chưa biết (nếu AP cha đã biết mà không có con
 *                   thì station con không nối vào cha).
 *   4. "root_sta":  Root là một đầu và station của Root đang nối vào AP của
 *                   đầu kia → WiFi.RSSI() của Root.
 * Trả nguồn, hoặc NULL nếu không có số đo nào đáng tin. */
static const char* link_rssi_for(uint32_t child, uint32_t parent, const ApLinks& root_ap,
                                 uint32_t root_sta_id, int root_sta_rssi,
                                 uint32_t now, int& rssi) {
    const ApLinks* pa = ap_links_of(parent, root_ap, now);
    if (pa && ap_links_find(*pa, child, rssi)) return "ap_parent";

    const ApLinks* ca = ap_links_of(child, root_ap, now);
    if (ca && ap_links_find(*ca, parent, rssi)) return "ap_child";

    const NodeStat* c = find_node_stat(child);
    if (!pa && ca && c && c->has_rssi) { rssi = c->rssi; return "sta"; }

    uint32_t self = mesh.getNodeId();
    if (root_sta_id != 0 &&
        ((parent == self && root_sta_id == child) || (child == self && root_sta_id == parent))) {
        rssi = root_sta_rssi;
        return "root_sta";
    }
    return NULL;
}

/* track_find(): ô theo dõi của node id; create = true thì cấp ô mới nếu chưa có.
 * Hết ô → lấy lại ô của node không còn trong mesh (ưu tiên), rồi ô lâu không
 * có chunk nhất. Trả NULL khi không tìm thấy và create = false. */
static NodeTrack* track_find(uint32_t id, bool create) {
    NodeTrack* empty = NULL;
    for (uint8_t i = 0; i < TRACK_MAX_NODES; i++) {
        if (node_track[i].id == id) return &node_track[i];
        if (empty == NULL && node_track[i].id == 0) empty = &node_track[i];
    }
    if (!create) return NULL;
    NodeTrack* t = empty;
    if (t == NULL) {
        uint32_t now = millis();
        uint32_t oldest_age = 0;
        for (uint8_t i = 0; i < TRACK_MAX_NODES; i++) {
            NodeTrack& c = node_track[i];
            uint32_t ref = (c.last_chunk_ms != 0) ? c.last_chunk_ms : c.seen_ms;
            // Node ngoài mesh coi như "cũ hơn" mọi node trong mesh
            uint32_t age = (now - ref) / 2 + (c.in_list ? 0 : 0x80000000UL);
            if (t == NULL || age > oldest_age) { t = &c; oldest_age = age; }
        }
    }
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->seen_ms = millis();
    return t;
}

/* on_valid_chunk(): gọi cho mỗi chunk đã qua validate header. */
static void on_valid_chunk(uint32_t id) {
    NodeTrack* t = track_find(id, true);
    uint32_t now = millis();
    t->last_chunk_ms = now ? now : 1;
    t->silent = false;
}

/* append_node_json(): 1 phần tử của mảng "nodes". Trường chưa có → null.
 * link_src = NULL → "link_rssi"/"link_rssi_src" = null. */
static void append_node_json(char* json, size_t sz, int& pos, bool first,
                             uint32_t id, int hop, const NodeStat* ns, uint32_t now,
                             const char* link_src, int link_rssi) {
    const NodeTrack* tr = track_find(id, false);
    json_appendf(json, sz, pos, "%s{\"id\":%u,", first ? "" : ",", (unsigned)id);
    if (ns) {
        json_appendf(json, sz, pos, "\"fw\":\"%s\",", ns->fw);
        if (ns->has_rssi) json_appendf(json, sz, pos, "\"rssi\":%d,", (int)ns->rssi);
        else              json_appendf(json, sz, pos, "\"rssi\":null,");
        json_appendf(json, sz, pos, "\"heap\":%u,", (unsigned)ns->heap);
    } else {
        json_appendf(json, sz, pos, "\"fw\":null,\"rssi\":null,\"heap\":null,");
    }
    if (link_src) json_appendf(json, sz, pos, "\"link_rssi\":%d,\"link_rssi_src\":\"%s\",", link_rssi, link_src);
    else          json_appendf(json, sz, pos, "\"link_rssi\":null,\"link_rssi_src\":null,");
    if (hop > 0) json_appendf(json, sz, pos, "\"hop\":%d,", hop);
    else         json_appendf(json, sz, pos, "\"hop\":null,");
    if (tr && tr->last_chunk_ms) json_appendf(json, sz, pos, "\"last_chunk_age_ms\":%u,", (unsigned)(now - tr->last_chunk_ms));
    else                         json_appendf(json, sz, pos, "\"last_chunk_age_ms\":null,");
    json_appendf(json, sz, pos, "\"silent\":%s,", (tr && tr->silent) ? "true" : "false");
    if (ns && ns->has_rst) json_appendf(json, sz, pos, "\"rst\":%d,", (int)ns->rst);
    else                   json_appendf(json, sz, pos, "\"rst\":null,");
    if (ns && ns->has_up)  json_appendf(json, sz, pos, "\"up_s\":%u,", (unsigned)ns->up_s);
    else                   json_appendf(json, sz, pos, "\"up_s\":null,");
    if (ns) json_appendf(json, sz, pos, "\"age_ms\":%u}", (unsigned)(now - ns->rx_ms));
    else    json_appendf(json, sz, pos, "\"age_ms\":null}");
}

/* collect_node_ids(): mesh.getNodeList() bỏ Root và ID trùng.
 *
 * getNodeList() chỉ bỏ Root ở gốc cây; nếu một Leaf báo lại cây con có chứa
 * Root (vòng liên kết lúc topology đang đổi — test12: Root hiện ở hop 3) thì
 * Root bị đếm thêm 1 và node ở nhánh vọng lại bị đếm 2 lần. */
static uint8_t collect_node_ids(uint32_t* ids, uint8_t max) {
    uint32_t self = mesh.getNodeId();
    uint8_t n = 0;
    for (auto& id : mesh.getNodeList()) {
        if (id == self || n >= max) continue;
        bool dup = false;
        for (uint8_t i = 0; i < n; i++) {
            if (ids[i] == id) { dup = true; break; }
        }
        if (!dup) ids[n++] = id;
    }
    return n;
}

/* update_node_tracking(): đối chiếu node_track[] với getNodeList() (đã bỏ Root),
 * cập nhật in_list/seen_ms và trạng thái silent. */
static void update_node_tracking() {
    uint32_t ids[TREE_MAX_NODES];
    uint8_t  n = collect_node_ids(ids, TREE_MAX_NODES);
    uint32_t now = millis();

    for (uint8_t i = 0; i < TRACK_MAX_NODES; i++) {
        NodeTrack& t = node_track[i];
        if (t.id == 0) continue;
        bool in_now = false;
        for (uint8_t j = 0; j < n; j++) {
            if (ids[j] == t.id) { in_now = true; break; }
        }
        if (in_now && !t.in_list) t.seen_ms = now;   // vào (lại) mesh
        t.in_list = in_now;
    }
    for (uint8_t j = 0; j < n; j++) {
        NodeTrack* t = track_find(ids[j], false);
        if (t == NULL) {                              // node mới, chưa gửi chunk nào
            t = track_find(ids[j], true);
            t->in_list = true;
        }
    }

    for (uint8_t i = 0; i < TRACK_MAX_NODES; i++) {
        NodeTrack& t = node_track[i];
        if (t.id == 0) continue;
        uint32_t ref = t.seen_ms;
        if (t.last_chunk_ms != 0 && (int32_t)(t.last_chunk_ms - ref) > 0) ref = t.last_chunk_ms;
        bool s = t.in_list && now - ref > SILENT_NODE_MS;
        if (s && !t.silent) {
            silent_events++;
            DBGF("[NODE] %u im lang: con trong mesh nhung %u ms khong co chunk",
                 (unsigned)t.id, (unsigned)(now - ref));
        }
        t.silent = s;
    }
}

/* check_heap_guard(): heap trống < HEAP_CRITICAL_BYTES liên tục quá
 * HEAP_CRITICAL_MS → khung LOG nêu lý do rồi ESP.restart(). Biện pháp cuối,
 * không phải watchdog node. */
static void check_heap_guard() {
    static uint32_t low_since = 0;   // 0 = heap đang trên ngưỡng
    uint32_t heap = ESP.getFreeHeap();
    uint32_t now = millis();
    if (heap >= HEAP_CRITICAL_BYTES) { low_since = 0; return; }
    if (low_since == 0) { low_since = now ? now : 1; return; }
    if (now - low_since > HEAP_CRITICAL_MS) {
        DBGF("[HEAP] RESTART: heap trong %u < %u byte lien tuc %u ms",
             (unsigned)heap, (unsigned)HEAP_CRITICAL_BYTES, (unsigned)(now - low_since));
        Serial.flush();
        delay(100);
        ESP.restart();
    }
}

/* send_telemetry_frame(): xem cấu trúc JSON trong UART_PROTOCOL.md §4.2.
 * Gọi định kỳ mỗi TELEMETRY_SEND_INTERVAL_MS từ loop().
 *
 * Kích thước: ~550 byte trường cũ + ~40 byte/node trong "tree" + ~125
 * byte/node trong "nodes". 16 node (TREE_MAX_NODES) ≈ 3,2KB < 4096 —
 * vẫn rất xa giới hạn khung UART (131072, UART_PROTOCOL.md §3). Buffer
 * static để không chiếm stack của loopTask. Nếu vẫn bị cắt, bỏ khung
 * thay vì gửi JSON hỏng. */
static void send_telemetry_frame() {
    uint32_t node_ids[TREE_MAX_NODES];
    uint8_t  node_n = collect_node_ids(node_ids, TREE_MAX_NODES);

    static char json[4096];
    int pos = snprintf(json, sizeof(json),
        "{\"ts\":%lu,\"role\":\"root\",\"layer\":0,\"fw\":\"" FW_VERSION "\","
        "\"frames_ok\":%u,\"frames_drop\":%u,\"chunks\":%u,\"chunk_err\":%u,"
        "\"heap_free\":%u,\"psram_free\":%u,"
        "\"node_count\":%u,\"node_ids\":[",
        (unsigned long)millis(),
        (unsigned)frames_completed, (unsigned)frames_dropped, (unsigned)chunks_received,
        (unsigned)chunk_errors,
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
        (unsigned)node_n);

    if (pos < 0) pos = 0;
    if (pos > (int)sizeof(json) - 1) pos = (int)sizeof(json) - 1;
    for (uint8_t i = 0; i < node_n; i++) {
        json_appendf(json, sizeof(json), pos, "%s%u", i ? "," : "", (unsigned)node_ids[i]);
    }
    json_appendf(json, sizeof(json), pos, "],\"last_self_heal_ms\":%ld", (long)last_self_heal_ms);
    json_appendf(json, sizeof(json), pos, ",\"silent_events\":%u", (unsigned)silent_events);

    /* Latency của chu kỳ vừa qua. Không có mẫu → lat_n = 0, các giá trị là
     * null (không gửi số giả). */
    if (lat_n > 0) {
        json_appendf(json, sizeof(json), pos,
            ",\"lat_avg_ms\":%.1f,\"lat_min_ms\":%.1f,\"lat_max_ms\":%.1f,\"lat_n\":%u,\"lat_by_node\":{",
            (double)lat_sum_us / lat_n / 1000.0, lat_min_us / 1000.0, lat_max_us / 1000.0, (unsigned)lat_n);
        for (uint8_t i = 0; i < lat_node_count; i++) {
            json_appendf(json, sizeof(json), pos, "%s\"%u\":%.1f",
                i ? "," : "", (unsigned)lat_by_node[i].node_id,
                (double)lat_by_node[i].sum_us / lat_by_node[i].n / 1000.0);
        }
        json_appendf(json, sizeof(json), pos, "}");
    } else {
        json_appendf(json, sizeof(json), pos,
            ",\"lat_avg_ms\":null,\"lat_min_ms\":null,\"lat_max_ms\":null,\"lat_n\":0,\"lat_by_node\":{}");
    }

    lat_sum_us = 0; lat_n = 0; lat_node_count = 0;

    /* "tree": nguyên văn mesh.asNodeTree().toString() (= subConnectionJson()),
     * dạng {"nodeId":N,"root":true,"subs":[{"nodeId":M,"subs":[...]}]} */
    auto tree = mesh.asNodeTree();
    json_appendf(json, sizeof(json), pos, ",\"tree\":%s", tree.toString().c_str());

    /* "nodes": mọi node trong cây (kể cả chưa từng gửi NSTAT → fw = null),
     * cộng node đã rời mesh nhưng NSTAT còn mới hơn NSTAT_FORGET_MS (hop = null). */
    uint32_t tree_ids[TREE_MAX_NODES];
    uint8_t  tree_hops[TREE_MAX_NODES];
    uint32_t tree_parents[TREE_MAX_NODES];
    uint8_t  tree_n = 0;
    collect_hops(tree, 0, tree_ids, tree_hops, tree_parents, tree_n);

    ApLinks root_ap;
    read_own_ap_links(root_ap);
    uint32_t root_sta_id   = root_sta_peer();
    int      root_sta_rssi = root_sta_id ? WiFi.RSSI() : 0;

    uint32_t now = millis();
    bool first_node = true;
    json_appendf(json, sizeof(json), pos, ",\"nodes\":[");
    for (uint8_t i = 0; i < tree_n; i++) {
        int link_rssi = 0;
        const char* link_src = link_rssi_for(tree_ids[i], tree_parents[i], root_ap,
                                             root_sta_id, root_sta_rssi, now, link_rssi);
        append_node_json(json, sizeof(json), pos, first_node, tree_ids[i], tree_hops[i],
                         find_node_stat(tree_ids[i]), now, link_src, link_rssi);
        first_node = false;
    }
    for (uint8_t i = 0; i < NSTAT_MAX_NODES; i++) {
        const NodeStat& ns = node_stats[i];
        if (ns.id == 0 || now - ns.rx_ms > NSTAT_FORGET_MS) continue;
        bool in_tree = false;
        for (uint8_t j = 0; j < tree_n; j++) {
            if (tree_ids[j] == ns.id) { in_tree = true; break; }
        }
        if (in_tree) continue;
        append_node_json(json, sizeof(json), pos, first_node, ns.id, 0, &ns, now, NULL, 0);
        first_node = false;
    }
    json_appendf(json, sizeof(json), pos, "]}");

    if (pos >= (int)sizeof(json) - 1) {
        DBGF("[TELE] JSON telemetry vuot %u byte, bo khung nay", (unsigned)sizeof(json));
        return;
    }

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
 *  - MAX_CONCURRENT_FRAMES assembly buffers × MAX_FRAME_SIZE
 *    (hiện tại = 4 × 24KB = 96KB)
 *  - KHÔNG còn latest_frame buffer riêng (tiết kiệm 24KB)
 *  - KHÔNG còn uart_tx_buffer riêng (tiết kiệm 24KB+10B)
 *  - Tổng hiện tại: ~96KB — vừa SRAM nội bộ ESP32-WROOM-32D
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
                           uint16_t total_chunks, uint32_t capture_us) {

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
            assemblies[i].capture_us     = capture_us;

            // Reset bitmap — tất cả chunk chưa nhận
            memset(assemblies[i].chunk_received, 0,
                   sizeof(assemblies[i].chunk_received));

#if DEBUG_ASM_VERBOSE
            DBGF("[ASM] Slot %d: bat dau ghep frame (node=%u, frame=%d, chunks=%d)\n",
                          i, node_id, frame_id, total_chunks);
#endif
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
        // assemblies[i].active luôn true ở đây (bước 1 đã loại slot trống),
        // nhưng vẫn kiểm tra để an toàn nếu logic phía trên thay đổi sau này
        if (assemblies[i].active && assemblies[i].start_time_ms < oldest_time) {
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
    assemblies[oldest_idx].capture_us     = capture_us;
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
 *  1. Base64 decode → binary (header 16 bytes + payload ≤1024 bytes)
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
     *   - Trích xuất 16 bytes đầu → ChunkHeader
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
        chunk_logf("[ASM] Header KHONG HOP LE: idx=%d >= total=%d",
                   header.chunk_index, header.total_chunks);
        return;
    }

    if (header.total_chunks > MAX_CHUNKS_PER_FRAME) {
        chunk_logf("[ASM] Qua nhieu chunk: %d > %d",
                   header.total_chunks, MAX_CHUNKS_PER_FRAME);
        return;
    }

    if (header.payload_len > MAX_CHUNK_PAYLOAD || header.payload_len == 0) {
        chunk_logf("[ASM] Payload khong hop le: %d bytes", header.payload_len);
        return;
    }

    // Chunk hợp lệ → node này còn gửi dữ liệu (theo dõi "silent", mảng tĩnh)
    on_valid_chunk(from);

    /* ── Bước 3: Tìm hoặc tạo slot assembly ────────────────
     *
     * Tìm slot đang ghép cho cặp (node_id, frame_id):
     *   - Tìm thấy → thêm chunk vào slot đó
     *   - Không thấy → tạo slot mới (hoặc evict slot cũ nhất)
     */
    int slot = find_assembly_slot(header.node_id, header.frame_id);

    if (slot < 0) {
        /* Supersede: Leaf gửi tuần tự từng frame qua kết nối TCP có thứ
         * tự, nên nếu chunk của MỘT frame mới từ cùng node đã tới, mọi
         * slot đang ghép dở của CÙNG node nhưng frame_id khác chắc chắn
         * không bao giờ hoàn thành nữa (frame cũ đã bị Leaf bỏ qua).
         * Giải phóng ngay thay vì chiếm slot tới hết REASSEMBLY_TIMEOUT_MS,
         * để nhường chỗ cho Leaf khác thay vì bị eviction chéo. */
        for (int i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
            if (assemblies[i].active &&
                assemblies[i].node_id == header.node_id &&
                assemblies[i].frame_id != header.frame_id) {
                DBGF("[ASM] SUPERSEDE: Huy slot %d (node=%u, frame cu=%d, nhan %d/%d chunks, frame moi=%d)\n",
                              i, assemblies[i].node_id, assemblies[i].frame_id,
                              assemblies[i].received_count, assemblies[i].total_chunks,
                              header.frame_id);
                assemblies[i].active = false;
                frames_dropped++;
            }
        }

        // Slot mới — frame chưa từng thấy
        slot = allocate_assembly_slot(header.node_id, header.frame_id,
                                      header.total_chunks, header.capture_us);
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
        chunk_logf("[ASM] TRAN BUFFER: offset=%d + len=%d > %d",
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
#if DEBUG_ASM_VERBOSE
        DBGF("[ASM] HOAN THANH! node=%u, frame=%d, size=%d bytes, chunks=%d\n",
                      fa.node_id, fa.frame_id,
                      fa.jpeg_total_len, fa.total_chunks);
#endif

        /* [ĐÃ ĐỔI] Bỏ bước memcpy sang latest_frame buffer riêng.
         * Thay bằng: latest_frame trỏ thẳng vào fa.jpeg_buffer (assembly buffer).
         *
         * An toàn cho luồng UART: send_image_frame() bên dưới gửi đồng bộ
         * ngay lúc frame hoàn thành, đọc buffer trước khi slot có thể bị
         * tái dụng.
         *
         * CẢNH BÁO (chỉ ảnh hưởng luồng HTTP MJPEG, không dùng hiện tại vì
         * mesh.stationManual() đang tắt): với MAX_CONCURRENT_FRAMES > 1,
         * slot này có thể bị allocate_assembly_slot()/eviction tái dụng
         * và ghi đè trước khi handle_http_clients() kịp gửi xong
         * latest_frame trong send_mjpeg_frame() (vòng loop() kế tiếp),
         * vì nhiều Leaf gửi xen kẽ khiến slot khác được cấp trước khi
         * HTTP gửi xong. Chấp nhận được vì HTTP không dùng lúc này;
         * nếu bật lại HTTP với nhiều Leaf, cần copy latest_frame sang
         * buffer riêng thay vì trỏ thẳng vào assembly buffer.
         */
        // Latency chụp → ghép xong, đo NGAY khi frame hoàn chỉnh (trước khi gửi UART)
        record_latency(fa.node_id, fa.capture_us);

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
    // Beacon do chính Root phát ra (xem loop()) — phòng xa, Root
    // không nên tự nhận broadcast của mình, nhưng bỏ qua sớm cho chắc.
    if (msg.startsWith(ROOT_BEACON_PREFIX)) return;
    // Trạng thái Leaf — tách TRƯỚC khi coi message là chunk Base64
    if (msg.startsWith(NSTAT_PREFIX)) {
        store_nstat(from, msg);
        return;
    }
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

    update_node_tracking();   // bắt đúng lúc node vào/rời getNodeList()
}


/* Áp sta_mac_to_node_id() lên MAC STA của chính board, phải ra đúng
 * mesh.getNodeId(). Sai → link_rssi phía AP sẽ gán nhầm node. */
static void ap_map_self_check() {
    uint8_t sta_mac[6];
    esp_read_mac(sta_mac, ESP_MAC_WIFI_STA);
    uint32_t derived = sta_mac_to_node_id(sta_mac);
    if (derived == mesh.getNodeId()) {
        DBGLN("[MESH] MAC->nodeId (link_rssi phia AP): OK");
    } else {
        DBGF("[MESH] CANH BAO: MAC->nodeId sai (%u != %u), link_rssi phia AP se khong khop node",
             (unsigned)derived, (unsigned)mesh.getNodeId());
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

    // painlessMesh in log nay thang ra Serial (khong qua khung DBGF/UART_PROTOCOL.md),
    // chen vao luong nhi phan 921600bps va lam middleware phai bo byte (bytesSkipped).
    // Muc ERROR hiem khi in nen chap nhan duoc.
    mesh.setDebugMsgTypes(ERROR);
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
    /* [TAM TAT - debug 14/09/2026] Nghi ngo vong lap retry cua stationManual()
     * ("scan could not start, retrying in 15 s") dang gianh dung chung radio
     * WiFi voi mesh AP/channel cua Root, khien Leaf tim thay Root roi lai rot
     * ("Found 1 nodes" -> "Found 0 nodes" lap lai). Comment tam de cach ly:
     * neu Leaf giu ket noi on dinh khi khong co dong nay, day la root cause
     * that su (khac bug #275 da sua o fork Alteriom).
     * BAT LAI: bo comment dong duoi day (chi can WiFi router that su can thiet
     * cho tinh nang xem HTTP MJPEG qua trinh duyet, khong anh huong 3 tieu chi
     * cham diem qua mesh noi bo). */
    // mesh.stationManual(STATION_SSID, STATION_PASSWORD);

    DBGF("[MESH] Root Node ID: %u\n", mesh.getNodeId());
    DBGF("[SYS] Firmware: %s\n", FW_VERSION);
    DBGF("[SYS] Ly do reset: %d", (int)esp_reset_reason());
    ap_map_self_check();
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

    /* [3b] In dòng tổng kết lỗi chunk còn treo (giới hạn 1 dòng/giây) */
    chunk_log_flush();

    /* [3c] Theo dõi node im lặng + bảo vệ heap */
    static uint32_t last_track_time = 0;
    if (millis() - last_track_time >= TRACK_UPDATE_MS) {
        last_track_time = millis();
        update_node_tracking();
    }
    check_heap_guard();

    /* [4] Phục vụ HTTP clients */
    handle_http_clients();

    /* [4b] Gửi khung TELEMETRY cho middleware định kỳ (xem UART_PROTOCOL.md §4.2) */
    static uint32_t last_telemetry_time = 0;
    uint32_t now_telemetry = millis();
    if (now_telemetry - last_telemetry_time >= TELEMETRY_SEND_INTERVAL_MS) {
        last_telemetry_time = now_telemetry;
        send_telemetry_frame();
    }

    /* [4c] Phát beacon Root định kỳ để Leaf tự học root_id, dùng
     * sendSingle() thay vì sendBroadcast() cho từng chunk (xem Sửa 2) */
    static uint32_t last_beacon_time = 0;
    uint32_t now_beacon = millis();
    if (now_beacon - last_beacon_time >= ROOT_BEACON_INTERVAL_MS) {
        last_beacon_time = now_beacon;
        mesh.sendBroadcast(ROOT_BEACON_PREFIX);
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
