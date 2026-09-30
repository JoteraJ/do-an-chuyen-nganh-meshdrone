/**
 * ============================================================
 *  base64_util.h — Mã hóa / Giải mã Base64 tối ưu cho ESP32
 * ============================================================
 *
 *  Vì painlessMesh truyền dữ liệu dạng String (text), mà
 *  dữ liệu JPEG là binary (có thể chứa byte 0x00, ký tự
 *  đặc biệt), ta cần encode binary → text trước khi gửi.
 *
 *  Base64 Encoding hoạt động như sau:
 *  ─────────────────────────────────
 *  1. Lấy 3 byte input (24 bit)
 *  2. Chia thành 4 nhóm 6 bit
 *  3. Mỗi nhóm 6 bit → 1 ký tự trong bảng 64 ký tự
 *  4. Nếu input không chia hết cho 3, thêm padding '='
 *
 *  Ví dụ:  Input:  [0x4D, 0x61, 0x6E] = "Man"
 *          Binary: 01001101 01100001 01101110
 *          6-bit:  010011 010110 000101 101110
 *          Index:  19     22     5      46
 *          Output: "TWFu"
 *
 *  Tỷ lệ tăng kích thước: ~33% (3 byte → 4 ký tự)
 *
 *  Tối ưu cho ESP32:
 *  - Sử dụng lookup table (O(1) cho mỗi byte)
 *  - Hàm nhận buffer đã cấp phát sẵn (tránh malloc/free)
 *  - Không dùng Arduino String để tránh phân mảnh heap
 *
 * ============================================================
 */

#ifndef BASE64_UTIL_H
#define BASE64_UTIL_H

#include <Arduino.h>

/* ────────────────────────────────────────────────────────────
 *  BẢNG MÃ HÓA BASE64
 *
 *  64 ký tự an toàn cho text: A-Z, a-z, 0-9, +, /
 *  Index 0  → 'A'
 *  Index 25 → 'Z'
 *  Index 26 → 'a'
 *  Index 51 → 'z'
 *  Index 52 → '0'
 *  Index 61 → '9'
 *  Index 62 → '+'
 *  Index 63 → '/'
 * ──────────────────────────────────────────────────────────── */
static const char B64_ENCODE_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* ────────────────────────────────────────────────────────────
 *  BẢNG GIẢI MÃ BASE64 (Inverse Lookup)
 *
 *  Ánh xạ ngược: ký tự ASCII → giá trị 6-bit (0-63)
 *  Ký tự không hợp lệ → 0xFF (dùng để phát hiện lỗi)
 *
 *  Ví dụ:
 *    'A' (ASCII 65) → 0
 *    'B' (ASCII 66) → 1
 *    'a' (ASCII 97) → 26
 *    '0' (ASCII 48) → 52
 *    '+' (ASCII 43) → 62
 *    '/' (ASCII 47) → 63
 *    '=' (ASCII 61) → 0 (padding, xử lý riêng)
 * ──────────────────────────────────────────────────────────── */
static const uint8_t B64_DECODE_TABLE[128] = {
    /*   0-15  */ 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    /*  16-31  */ 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    /*  32-47  */ 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,  62, 0xFF,0xFF,0xFF,  63,
    /*  48-63  */   52,  53,  54,  55,   56,  57,  58,  59,   60,  61,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    /*  64-79  */ 0xFF,   0,   1,   2,    3,   4,   5,   6,    7,   8,   9,  10,   11,  12,  13,  14,
    /*  80-95  */   15,  16,  17,  18,   19,  20,  21,  22,   23,  24,  25,0xFF, 0xFF,0xFF,0xFF,0xFF,
    /*  96-111 */ 0xFF,  26,  27,  28,   29,  30,  31,  32,   33,  34,  35,  36,   37,  38,  39,  40,
    /* 112-127 */   41,  42,  43,  44,   45,  46,  47,  48,   49,  50,  51,0xFF, 0xFF,0xFF,0xFF,0xFF
};

/* ────────────────────────────────────────────────────────────
 *  base64_encoded_size()
 *
 *  Tính kích thước output sau khi encode Base64.
 *  Công thức: ceil(input_len / 3) * 4
 *
 *  Ví dụ:
 *    input 1024 bytes → output 1368 ký tự
 *    input 1036 bytes → output 1384 ký tự (header + payload)
 * ──────────────────────────────────────────────────────────── */
static inline size_t base64_encoded_size(size_t input_len) {
    return ((input_len + 2) / 3) * 4;
}

/* ────────────────────────────────────────────────────────────
 *  base64_decoded_max_size()
 *
 *  Tính kích thước output TỐI ĐA sau khi decode.
 *  Kết quả thực tế có thể nhỏ hơn 1-2 byte do padding '='
 *
 *  Công thức: (input_len / 4) * 3
 * ──────────────────────────────────────────────────────────── */
static inline size_t base64_decoded_max_size(size_t input_len) {
    return (input_len / 4) * 3;
}

/* ────────────────────────────────────────────────────────────
 *  base64_encode()
 *
 *  Mã hóa mảng byte thành chuỗi Base64.
 *
 *  Tham số:
 *    src      — Con trỏ tới dữ liệu binary cần encode
 *    src_len  — Kích thước dữ liệu (bytes)
 *    dst      — Buffer output (đã cấp phát, kích thước >= base64_encoded_size(src_len) + 1)
 *    dst_size — Kích thước buffer output
 *
 *  Trả về:
 *    Số ký tự đã ghi vào dst (không tính null terminator)
 *    0 nếu buffer không đủ lớn
 *
 *  Thuật toán:
 *  ┌──────────────────────────────────────────────┐
 *  │  Lấy 3 byte: [B0] [B1] [B2]                 │
 *  │                                              │
 *  │  Ghép thành 24 bit:                          │
 *  │  B0(8bit) | B1(8bit) | B2(8bit) = 24 bit    │
 *  │                                              │
 *  │  Chia thành 4 nhóm 6 bit:                   │
 *  │  [6bit] [6bit] [6bit] [6bit]                 │
 *  │                                              │
 *  │  Tra bảng B64_ENCODE_TABLE cho mỗi nhóm     │
 *  │  → 4 ký tự output                            │
 *  └──────────────────────────────────────────────┘
 * ──────────────────────────────────────────────────────────── */
static size_t base64_encode(const uint8_t* src, size_t src_len,
                            char* dst, size_t dst_size) {
    size_t needed = base64_encoded_size(src_len) + 1;  // +1 cho null terminator
    if (dst_size < needed) {
        return 0;  // Buffer quá nhỏ
    }

    size_t di = 0;  // Index ghi vào dst
    size_t si = 0;  // Index đọc từ src

    /* Xử lý từng nhóm 3 byte input */
    while (si + 3 <= src_len) {
        // Ghép 3 byte thành một số 24-bit
        // Byte 0 chiếm bit 23-16, Byte 1 chiếm bit 15-8, Byte 2 chiếm bit 7-0
        uint32_t triple = ((uint32_t)src[si] << 16)
                        | ((uint32_t)src[si + 1] << 8)
                        |  (uint32_t)src[si + 2];

        // Trích xuất 4 nhóm 6-bit và tra bảng
        dst[di++] = B64_ENCODE_TABLE[(triple >> 18) & 0x3F];  // Bit 23-18
        dst[di++] = B64_ENCODE_TABLE[(triple >> 12) & 0x3F];  // Bit 17-12
        dst[di++] = B64_ENCODE_TABLE[(triple >>  6) & 0x3F];  // Bit 11-6
        dst[di++] = B64_ENCODE_TABLE[ triple        & 0x3F];  // Bit 5-0

        si += 3;
    }

    /* Xử lý byte dư (nếu src_len không chia hết cho 3) */
    size_t remaining = src_len - si;

    if (remaining == 1) {
        // Còn 1 byte → 8 bit → 2 nhóm 6-bit (pad 4 bit zero) + 2 ký tự '='
        uint32_t val = (uint32_t)src[si] << 16;
        dst[di++] = B64_ENCODE_TABLE[(val >> 18) & 0x3F];
        dst[di++] = B64_ENCODE_TABLE[(val >> 12) & 0x3F];
        dst[di++] = '=';  // Padding
        dst[di++] = '=';  // Padding
    }
    else if (remaining == 2) {
        // Còn 2 byte → 16 bit → 3 nhóm 6-bit (pad 2 bit zero) + 1 ký tự '='
        uint32_t val = ((uint32_t)src[si] << 16)
                     | ((uint32_t)src[si + 1] << 8);
        dst[di++] = B64_ENCODE_TABLE[(val >> 18) & 0x3F];
        dst[di++] = B64_ENCODE_TABLE[(val >> 12) & 0x3F];
        dst[di++] = B64_ENCODE_TABLE[(val >>  6) & 0x3F];
        dst[di++] = '=';  // Padding
    }

    dst[di] = '\0';  // Null terminator
    return di;
}

/* ────────────────────────────────────────────────────────────
 *  base64_decode()
 *
 *  Giải mã chuỗi Base64 thành mảng byte gốc.
 *
 *  Tham số:
 *    src      — Chuỗi Base64 cần decode
 *    src_len  — Độ dài chuỗi (phải chia hết cho 4)
 *    dst      — Buffer output (kích thước >= base64_decoded_max_size(src_len))
 *    dst_size — Kích thước buffer output
 *
 *  Trả về:
 *    Số byte đã ghi vào dst
 *    0 nếu có lỗi (buffer nhỏ, ký tự không hợp lệ, ...)
 *
 *  Thuật toán (ngược lại encode):
 *  ┌──────────────────────────────────────────────┐
 *  │  Lấy 4 ký tự Base64                         │
 *  │  Tra bảng B64_DECODE_TABLE → 4 giá trị 6-bit│
 *  │  Ghép thành 24 bit                           │
 *  │  Tách thành 3 byte output                    │
 *  │  Xử lý padding '=' ở cuối                   │
 *  └──────────────────────────────────────────────┘
 * ──────────────────────────────────────────────────────────── */
static size_t base64_decode(const char* src, size_t src_len,
                            uint8_t* dst, size_t dst_size) {
    // Base64 hợp lệ phải có độ dài chia hết cho 4
    if (src_len == 0 || (src_len % 4) != 0) {
        return 0;
    }

    // Tính kích thước output dự kiến
    size_t out_len = (src_len / 4) * 3;

    // Trừ padding '=' ở cuối
    if (src_len >= 1 && src[src_len - 1] == '=') out_len--;
    if (src_len >= 2 && src[src_len - 2] == '=') out_len--;

    if (dst_size < out_len) {
        return 0;  // Buffer quá nhỏ
    }

    size_t di = 0;  // Index ghi vào dst

    /* Xử lý từng nhóm 4 ký tự */
    for (size_t si = 0; si < src_len; si += 4) {
        // Tra bảng: ký tự Base64 → giá trị 6-bit
        // Kiểm tra ký tự hợp lệ (ASCII 0-127 và không phải 0xFF trong bảng)
        uint8_t a = (src[si]   < 128) ? B64_DECODE_TABLE[(uint8_t)src[si]]   : 0xFF;
        uint8_t b = (src[si+1] < 128) ? B64_DECODE_TABLE[(uint8_t)src[si+1]] : 0xFF;
        uint8_t c = (src[si+2] == '=') ? 0 : ((src[si+2] < 128) ? B64_DECODE_TABLE[(uint8_t)src[si+2]] : 0xFF);
        uint8_t d = (src[si+3] == '=') ? 0 : ((src[si+3] < 128) ? B64_DECODE_TABLE[(uint8_t)src[si+3]] : 0xFF);

        // Kiểm tra ký tự không hợp lệ
        if (a == 0xFF || b == 0xFF || c == 0xFF || d == 0xFF) {
            return 0;  // Ký tự Base64 không hợp lệ
        }

        // Ghép 4 giá trị 6-bit → 24 bit
        uint32_t triple = ((uint32_t)a << 18)
                        | ((uint32_t)b << 12)
                        | ((uint32_t)c <<  6)
                        |  (uint32_t)d;

        // Tách 24 bit → 3 byte (bỏ qua byte padding)
        if (di < out_len) dst[di++] = (triple >> 16) & 0xFF;  // Byte cao
        if (di < out_len) dst[di++] = (triple >>  8) & 0xFF;  // Byte giữa
        if (di < out_len) dst[di++] =  triple        & 0xFF;  // Byte thấp
    }

    return di;
}

#endif // BASE64_UTIL_H
