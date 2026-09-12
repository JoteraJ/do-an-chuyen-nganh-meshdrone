/**
 * ============================================================
 *  chunk_protocol.h — Giao thức đóng gói / giải gói Chunk
 * ============================================================
 *
 *  File này định nghĩa cấu trúc Header và các hàm chuyển đổi
 *  giữa dữ liệu binary (header + JPEG payload) và chuỗi Base64
 *  để truyền qua painlessMesh.
 *
 *  Luồng dữ liệu:
 *  ─────────────────────────────────────────────────────────
 *
 *  [Camera Node - GỬI]
 *
 *    JPEG Buffer (binary)
 *         │
 *         ▼
 *    ┌─────────────────────────────────────┐
 *    │  Cắt thành chunks 1024 byte        │
 *    │  Mỗi chunk + Header 12 byte        │
 *    └─────────────────────────────────────┘
 *         │
 *         ▼
 *    serialize_chunk(header, payload)
 *         │
 *         ▼  Base64 encode
 *    String (gửi qua mesh.sendBroadcast)
 *
 *  ─────────────────────────────────────────────────────────
 *
 *  [Root Node - NHẬN]
 *
 *    String (nhận từ mesh callback)
 *         │
 *         ▼  Base64 decode
 *    deserialize_chunk(msg, &header, payload_buf)
 *         │
 *         ▼
 *    ┌─────────────────────────────────────┐
 *    │  Header: biết chunk thuộc frame nào │
 *    │  Payload: copy vào đúng vị trí     │
 *    └─────────────────────────────────────┘
 *         │
 *         ▼
 *    JPEG Buffer hoàn chỉnh
 *
 * ============================================================
 */

#ifndef CHUNK_PROTOCOL_H
#define CHUNK_PROTOCOL_H

#include <Arduino.h>
#include "mesh_config.h"
#include "base64_util.h"

/* ────────────────────────────────────────────────────────────
 *  chunk_log() — hook log dùng chung cho cả node_camera VÀ
 *  node_root, để header này KHÔNG gọi thẳng Serial.println/printf.
 *
 *  Lý do: node_root.ino (sau khi thêm UART_PROTOCOL.md) đóng khung
 *  TOÀN BỘ output qua Serial ở baud 921600 cho middleware — nếu
 *  header này in text thô như bản cũ, sẽ làm hỏng luồng nhị phân
 *  giữa các khung ảnh JPEG. node_camera.ino thì không cần framing
 *  (chỉ debug qua Serial Monitor thường), nên mỗi .ino tự định nghĩa
 *  hàm này theo đúng nhu cầu của nó — chỉ CẦN 1 định nghĩa duy nhất
 *  trong sketch đang biên dịch (không có -> lỗi linker "undefined
 *  reference to chunk_log", nhắc bạn thêm vào .ino đang thiếu):
 *
 *    // node_camera.ino (giữ nguyên debug thô, không cần framing)
 *    void chunk_log(const char* msg) { Serial.println(msg); }
 *
 *    // node_root.ino (đã có sẵn, gọi log_frame_str() -> khung LOG)
 *    void chunk_log(const char* msg) { log_frame_str(msg); }
 * ──────────────────────────────────────────────────────────── */
extern void chunk_log(const char* msg);

/* ────────────────────────────────────────────────────────────
 *  CẤU TRÚC CHUNK HEADER — 12 bytes, packed
 *
 *  Cấu trúc này được đặt NGAY TRƯỚC payload trong mỗi packet
 *  trước khi encode Base64. Sử dụng #pragma pack(push, 1) để
 *  compiler không thêm padding byte.
 *
 *  Layout trong bộ nhớ (12 bytes liên tiếp):
 *
 *  Offset  Kích thước  Trường         Mô tả
 *  ──────  ──────────  ─────────────  ────────────────────────
 *  0       4 bytes     node_id        ID duy nhất của node gửi
 *                                     (lấy từ mesh.getNodeId())
 *  4       2 bytes     frame_id       Số thứ tự bức ảnh (0-65535, wrap)
 *  6       2 bytes     chunk_index    Vị trí chunk trong frame (0-based)
 *  8       2 bytes     total_chunks   Tổng số chunk của frame
 *  10      2 bytes     payload_len    Kích thước payload thực tế (≤1024)
 *
 *  Ví dụ: Ảnh JPEG 10KB = 10240 bytes
 *    total_chunks = ceil(10240 / 1024) = 10
 *    Chunk 0: payload_len = 1024, offset = 0
 *    Chunk 1: payload_len = 1024, offset = 1024
 *    ...
 *    Chunk 9: payload_len = 1024, offset = 9216
 *    (nếu chia đều, chunk cuối có thể < 1024)
 *
 * ──────────────────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint32_t node_id;        // ID node gửi (4 bytes)
    uint16_t frame_id;       // Số thứ tự frame (2 bytes, wrap-around 0-65535)
    uint16_t chunk_index;    // Vị trí chunk trong frame (2 bytes, 0-based)
    uint16_t total_chunks;   // Tổng số chunk của frame (2 bytes)
    uint16_t payload_len;    // Kích thước payload thực tế (2 bytes, ≤ 1024)
} ChunkHeader;
#pragma pack(pop)

/* ────────────────────────────────────────────────────────────
 *  calc_total_chunks()
 *
 *  Tính số chunk cần thiết để truyền một ảnh JPEG.
 *  Sử dụng phép chia ceiling (làm tròn lên).
 *
 *  Ví dụ:
 *    jpeg_len = 10240 → ceil(10240/1024) = 10 chunks
 *    jpeg_len = 10241 → ceil(10241/1024) = 11 chunks (chunk cuối 1 byte)
 *    jpeg_len = 8500  → ceil(8500/1024)  = 9 chunks  (chunk cuối 308 bytes)
 * ──────────────────────────────────────────────────────────── */
static inline uint16_t calc_total_chunks(size_t jpeg_len) {
    return (uint16_t)((jpeg_len + MAX_CHUNK_PAYLOAD - 1) / MAX_CHUNK_PAYLOAD);
}

/* ────────────────────────────────────────────────────────────
 *  serialize_chunk()
 *
 *  Đóng gói Header + Payload thành chuỗi Base64 để gửi qua Mesh.
 *
 *  Quy trình:
 *  1. Tạo buffer tạm: [Header 12 bytes][Payload ≤1024 bytes]
 *  2. Encode Base64 toàn bộ buffer → chuỗi text
 *  3. Trả về Arduino String
 *
 *  Tham số:
 *    header  — Cấu trúc ChunkHeader đã điền đầy đủ
 *    payload — Con trỏ tới dữ liệu JPEG của chunk này
 *              (trỏ vào vùng fb->buf + offset tương ứng)
 *
 *  Trả về:
 *    Arduino String chứa dữ liệu Base64, sẵn sàng gửi qua mesh.
 *    Nếu lỗi, trả về String rỗng "".
 *
 *  Bộ nhớ:
 *    Sử dụng buffer static để tránh malloc/free mỗi lần gọi.
 *    Không thread-safe, nhưng ESP32 Arduino loop chạy đơn luồng.
 * ──────────────────────────────────────────────────────────── */
static String serialize_chunk(const ChunkHeader& header, const uint8_t* payload) {

    /*
     * Buffer tạm chứa binary data trước khi encode:
     *   [12 bytes header][up to 1024 bytes payload]
     * Kích thước tối đa: 12 + 1024 = 1036 bytes
     *
     * Dùng static để:
     * - Tránh cấp phát heap mỗi lần gọi (gọi 10-15 lần/frame × 3 FPS = 30-45 lần/giây)
     * - Tránh phân mảnh heap trên ESP32
     */
    static uint8_t raw_buffer[CHUNK_HEADER_SIZE + MAX_CHUNK_PAYLOAD];

    /* Bước 1: Copy header vào đầu buffer
     *
     * memcpy an toàn vì ChunkHeader được pack(1), kích thước đúng 12 bytes.
     * Đây là serialization đơn giản nhất: copy struct nguyên bản.
     * Cả sender và receiver đều là ESP32 (cùng endianness - Little Endian),
     * nên không cần chuyển đổi byte order.
     */
    memcpy(raw_buffer, &header, CHUNK_HEADER_SIZE);

    /* Bước 2: Copy payload ngay sau header
     *
     * payload_len cho biết kích thước thực tế (có thể < MAX_CHUNK_PAYLOAD
     * ở chunk cuối cùng). Chỉ copy đúng số byte cần thiết.
     */
    if (header.payload_len > 0 && payload != NULL) {
        memcpy(raw_buffer + CHUNK_HEADER_SIZE, payload, header.payload_len);
    }

    /* Bước 3: Tính tổng kích thước binary = header + payload */
    size_t raw_len = CHUNK_HEADER_SIZE + header.payload_len;

    /* Bước 4: Encode Base64
     *
     * Kích thước output Base64:
     *   raw_len = 1036 (worst case)
     *   encoded = ceil(1036/3) * 4 = 346 * 4 = 1384 ký tự
     *
     * Buffer Base64 cũng dùng static:
     *   1384 + 1 (null) = 1385 bytes
     *   Làm tròn lên 1536 để an toàn
     */
    static char b64_buffer[2048];
    size_t b64_len = base64_encode(raw_buffer, raw_len, b64_buffer, sizeof(b64_buffer));

    if (b64_len == 0) {
        chunk_log("[CHUNK] Base64 encode failed!");
        return String("");
    }

    /* Bước 5: Chuyển thành Arduino String và trả về
     *
     * Lưu ý: String sẽ copy dữ liệu từ b64_buffer vào heap.
     * Đây là bản copy duy nhất không thể tránh vì painlessMesh
     * yêu cầu tham số String.
     */
    return String(b64_buffer);
}

/* ────────────────────────────────────────────────────────────
 *  deserialize_chunk()
 *
 *  Giải gói chuỗi Base64 nhận từ Mesh thành Header + Payload.
 *
 *  Quy trình (ngược lại serialize):
 *  1. Decode Base64 → buffer binary
 *  2. Trích xuất 12 bytes đầu → ChunkHeader
 *  3. Phần còn lại → Payload data
 *
 *  Tham số:
 *    encoded        — Chuỗi Base64 nhận từ mesh callback
 *    header         — [OUT] ChunkHeader sẽ được điền
 *    payload_buf    — [OUT] Buffer nhận payload (caller cấp phát)
 *    payload_buf_sz — Kích thước buffer payload
 *
 *  Trả về:
 *    true nếu giải gói thành công
 *    false nếu lỗi (Base64 không hợp lệ, dữ liệu quá ngắn, ...)
 *
 *  Bộ nhớ:
 *    Sử dụng buffer static cho decode, tương tự serialize_chunk().
 * ──────────────────────────────────────────────────────────── */
static bool deserialize_chunk(const String& encoded,
                              ChunkHeader& header,
                              uint8_t* payload_buf,
                              size_t payload_buf_sz) {

    /* Buffer tĩnh cho kết quả decode Base64 */
    static uint8_t decode_buf[CHUNK_HEADER_SIZE + MAX_CHUNK_PAYLOAD + 16];

    size_t encoded_len = encoded.length();
    if (encoded_len < 16) {
        // Base64 của 12 bytes header ≥ 16 ký tự → message quá ngắn
        chunk_log("[CHUNK] Message too short!");
        return false;
    }

    /* Bước 1: Decode Base64 → binary */
    size_t decoded_len = base64_decode(
        encoded.c_str(), encoded_len,
        decode_buf, sizeof(decode_buf)
    );

    if (decoded_len < CHUNK_HEADER_SIZE) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[CHUNK] Decode failed or too short: %d bytes", (int)decoded_len);
        chunk_log(msg);
        return false;
    }

    /* Bước 2: Trích xuất Header (12 bytes đầu)
     *
     * Copy ngược lại: binary → struct ChunkHeader.
     * An toàn vì cùng platform (ESP32 Little Endian).
     */
    memcpy(&header, decode_buf, CHUNK_HEADER_SIZE);

    /* Bước 3: Validate header
     *
     * Kiểm tra các giá trị hợp lý để phòng tránh buffer overflow
     * từ dữ liệu không hợp lệ hoặc bị corrupt trên đường truyền.
     */
    if (header.payload_len > MAX_CHUNK_PAYLOAD) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[CHUNK] Invalid payload_len: %d", header.payload_len);
        chunk_log(msg);
        return false;
    }

    /* Xác minh kích thước decoded khớp với header */
    size_t expected_len = CHUNK_HEADER_SIZE + header.payload_len;
    if (decoded_len < expected_len) {
        char msg[80];
        snprintf(msg, sizeof(msg), "[CHUNK] Size mismatch: decoded=%d, expected=%d",
                 (int)decoded_len, (int)expected_len);
        chunk_log(msg);
        return false;
    }

    /* Bước 4: Copy payload
     *
     * Payload nằm ngay sau header trong decode_buf.
     * Copy vào buffer do caller cung cấp.
     */
    if (header.payload_len > payload_buf_sz) {
        char msg[80];
        snprintf(msg, sizeof(msg), "[CHUNK] Payload buffer too small: %d > %d",
                 header.payload_len, (int)payload_buf_sz);
        chunk_log(msg);
        return false;
    }

    if (header.payload_len > 0) {
        memcpy(payload_buf, decode_buf + CHUNK_HEADER_SIZE, header.payload_len);
    }

    return true;
}

#endif // CHUNK_PROTOCOL_H
