/**
 * ============================================================
 *  crc16.h — CRC-16/CCITT-FALSE dùng chung cho UART_PROTOCOL.md
 * ============================================================
 *
 *  Dùng để kiểm lỗi các khung nhị phân gửi từ Root Node sang
 *  middleware Node.js qua UART (921600 bps). Middleware có bản
 *  JavaScript tương đương trong middleware/protocol.js — PHẢI
 *  cho ra cùng kết quả với cùng input, đã kiểm chứng bằng
 *  middleware/test_protocol.js.
 *
 *  Tham số thuật toán: poly=0x1021, init=0xFFFF, không reflect
 *  input/output, không xorout (CRC-16/CCITT-FALSE chuẩn).
 * ============================================================ */

#ifndef CRC16_CCITT_H
#define CRC16_CCITT_H

#include <stdint.h>
#include <stddef.h>

/* Giá trị khởi tạo CRC — dùng khi cần tính CRC theo kiểu "nạp dần"
 * (streaming) qua nhiều vùng bộ nhớ rời rạc, ví dụ header + payload
 * không nằm liền nhau, để KHÔNG phải memcpy gộp lại (tốn RAM/PSRAM
 * gấp đôi với ảnh JPEG ~15-65KB). Toán học CRC là một phép chạy
 * tuần tự nên gọi update() nhiều lần liên tiếp cho kết quả giống hệt
 * gọi 1 lần trên buffer đã nối liền. */
static inline uint16_t crc16_ccitt_false_init(void) {
    return 0xFFFF;
}

static inline uint16_t crc16_ccitt_false_update(uint16_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* Tiện ích: tính CRC trên 1 buffer liền mạch duy nhất (trường hợp đơn giản,
 * dùng trong test_protocol.js làm test vector đối chiếu). Tương đương
 * crc16_ccitt_false_update(crc16_ccitt_false_init(), data, len). */
static inline uint16_t crc16_ccitt_false(const uint8_t *data, size_t len) {
    return crc16_ccitt_false_update(crc16_ccitt_false_init(), data, len);
}

#endif // CRC16_CCITT_H
