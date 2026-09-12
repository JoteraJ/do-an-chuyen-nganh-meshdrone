# UART_PROTOCOL.md — Giao thức Root Node (ESP32) ↔ Middleware (PC)

> Bổ sung cho kiến trúc đã chốt trong tài liệu đồ án (mục "MIDDLEWARE & DASHBOARD"):
> Root Node ──UART 921600bps──▶ Node.js middleware ──Socket.IO──▶ React dashboard.
>
> **Lưu ý quan trọng:** code node_root hiện tại (cả bản painlessMesh và bản ESP-IDF
> Phase 1) mới chỉ tự host HTTP MJPEG server trên chính ESP32, CHƯA gửi gì qua UART
> cho middleware. File này định nghĩa giao thức để việc đó thực hiện được, và
> `node_root.ino` đã được vá theo đúng giao thức này (xem mục "Đã áp dụng" bên dưới).

## 1. Vì sao cần đóng khung (framing) thay vì in text thô

ESP32-CAM (AI-Thinker) chỉ có một UART thực sự lộ ra ngoài qua đầu nạp FTDI (UART0 —
GPIO1/GPIO3). Không có UART2 dự phòng đáng tin cậy trên mọi board (một số bản
AI-Thinker dùng GPIO16 cho PSRAM). Vì vậy **debug log, dữ liệu ảnh JPEG, và số liệu
đo (telemetry) đều phải đi chung một đường UART duy nhất**. Nếu chỉ in text thô như
code cũ (`Serial.printf(...)`) xen giữa dữ liệu nhị phân ảnh JPEG, middleware sẽ
không thể phân biệt đâu là log, đâu là byte ảnh → hỏng frame.

Giải pháp: đóng gói TẤT CẢ output của Root (kể cả log debug) vào một khung
(frame) nhị phân có độ dài rõ ràng + CRC kiểm lỗi, middleware tách khung rồi mới
diễn giải theo `type`.

## 2. Cấu hình đường truyền

| Thông số | Giá trị |
|---|---|
| Baud rate | **921600 bps** (đúng quyết định đã chốt — code cũ trước khi vá vẫn để 115200, đã sửa) |
| Data bits / Parity / Stop | 8N1 |
| Chiều | Root → PC (một chiều; PC không cần gửi lệnh ngược lại ở Phase này) |

## 3. Cấu trúc khung (frame)

```
Offset  Size    Field     Mô tả
──────  ─────   ───────   ──────────────────────────────────────────
0       2 byte  MAGIC     0xAA 0x55 (cố định, dùng để resync khi mất đồng bộ)
2       1 byte  TYPE      0x01 = IMAGE_FRAME, 0x02 = TELEMETRY, 0x03 = LOG
3       4 byte  LEN       Độ dài PAYLOAD, little-endian uint32
7       LEN     PAYLOAD   Nội dung tuỳ theo TYPE (xem mục 4)
7+LEN   2 byte  CRC16     CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, không
                          reflect, không xorout), tính trên vùng [TYPE..PAYLOAD]
                          (tức byte offset 2 .. 7+LEN-1), little-endian
```

Tổng độ dài 1 khung = `7 + LEN + 2` byte. `LEN` tối đa cho phép ở middleware:
131072 byte (đủ dư cho `MAX_FRAME_SIZE = 65536` + 10 byte sub-header ảnh); khung
báo LEN lớn hơn mức này bị coi là hỏng, middleware bỏ 1 byte MAGIC rồi quét lại.

CRC dùng thuật toán **CRC-16/CCITT-FALSE** — cài đặt tham chiếu:

```c
// C (dùng trong firmware) — xem shared/crc16.h
static inline uint16_t crc16_ccitt_false(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
```

```js
// JavaScript (dùng trong middleware) — xem middleware/protocol.js, PHẢI cho
// cùng kết quả với hàm C ở trên với cùng input.
function crc16CcittFalse(buf) {
  let crc = 0xFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i] << 8;
    for (let b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
  }
  return crc & 0xFFFF;
}
```

## 4. Nội dung PAYLOAD theo từng TYPE

### 4.1 `0x01 IMAGE_FRAME` — một ảnh JPEG đã ghép xong ở Root

```
Offset  Size    Field
──────  ─────   ─────────────
0       4 byte  node_id       (LE uint32 — node camera gốc gửi ảnh này)
4       2 byte  frame_id      (LE uint16)
6       4 byte  jpeg_len      (LE uint32 — số byte JPEG thực tế)
10      jpeg_len  jpeg_data   (dữ liệu JPEG thô, bắt đầu bằng 0xFFD8, kết bằng 0xFFD9)
```

### 4.2 `0x02 TELEMETRY` — số liệu mạng, gửi định kỳ (khuyến nghị mỗi 1-2s)

PAYLOAD là chuỗi JSON UTF-8 (không byte NUL kết thúc, LEN đã cho biết độ dài),
ví dụ:

```json
{
  "ts": 123456,
  "role": "root",
  "layer": 0,
  "frames_ok": 42,
  "frames_drop": 3,
  "chunks": 610,
  "heap_free": 123456,
  "psram_free": 234567,
  "node_count": 3,
  "node_ids": [111, 222, 333],
  "last_self_heal_ms": 842
}
```

`last_self_heal_ms`: thời gian (ms) giữa lúc mất kết nối tới một node con
(`onDroppedConnection`) và lúc mạng có kết nối trở lại
(`onNewConnection`/`changedConnection`) gần nhất — đây chính là số liệu
"self-healing" hội đồng sẽ hỏi (câu hỏi dự kiến #3 trong tài liệu đồ án).
`-1` nghĩa là chưa từng xảy ra sự kiện rớt kết nối nào.

> **Giới hạn hiện tại (ghi rõ để không "làm giả" số liệu):** `node_ids` là danh
> sách phẳng các node đang kết nối vào mesh nhìn từ phía Root
> (`mesh.getNodeList()`), CHƯA có thông tin ai là cha của ai (cây phân cấp) hay
> RSSI từng liên kết — painlessMesh không lộ RSSI theo node qua API public một
> cách chắc chắn giữa các bản. Dashboard hiện hiển thị đúng những gì có thật
> (danh sách phẳng), không vẽ cây giả. Đây là việc còn lại nếu muốn có sơ đồ
> cây topology đầy đủ như tài liệu mô tả — xem TODO trong `node_root.ino`.

### 4.3 `0x03 LOG` — dòng log debug (thay thế Serial.printf cũ)

PAYLOAD là text UTF-8 thuần (1 dòng, không cần `\n` ở cuối). Middleware in ra
console của PC, giữ nguyên trải nghiệm xem log như khi cắm Serial Monitor
trực tiếp, nhưng không làm hỏng luồng nhị phân.

## 5. Cách middleware tách khung khỏi luồng byte

1. Gom byte đến từ cổng serial vào một buffer.
2. Quét tìm `0xAA 0x55` (MAGIC). Byte rác trước MAGIC bị bỏ (đứt kết nối lúc
   ESP32 đang giữa chừng một khung khi middleware khởi động, ví dụ).
3. Khi đủ 7 byte header, đọc `TYPE`, `LEN`. Nếu `LEN` vượt giới hạn cho phép
   → coi MAGIC này là rác, dịch buffer 1 byte, quét lại (resync).
4. Chờ đủ `7 + LEN + 2` byte mới xử lý (dữ liệu ảnh JPEG ~10-15KB có thể đến
   qua nhiều lần `data` event của serial port).
5. So CRC16 tính được với CRC nhận. Sai → bỏ khung, tiếp tục quét (không dừng
   toàn bộ luồng vì 1 khung lỗi).
6. Đúng → trả về `{ type, payload }` cho phần xử lý nghiệp vụ.

Cài đặt tham chiếu: `middleware/protocol.js` (`FrameParser`), có test tự động
mô phỏng nhiễu/đứt gói tại `middleware/test_protocol.js` (cùng tinh thần với
`mo_phong_reassembly.py` — kiểm chứng thuật toán bằng phần mềm khi chưa có
board thật để test).

## 6. Đã áp dụng ở đâu (checklist)

- [x] `thuattoancatanh/shared/crc16.h` — CRC dùng chung cho Arduino
- [x] `thuattoancatanh/node_root/node_root.ino` — gửi IMAGE_FRAME + TELEMETRY +
      LOG qua UART 921600bps, song song với HTTP MJPEG cũ (giữ cả hai, không
      mất tính năng nào)
- [x] `thuattoancatanh/middleware/` — Node.js đọc UART, giải khung, phát lại
      qua Socket.IO cho dashboard
- [x] `thuattoancatanh/tài liệu/dashboard` (bản mới trong `middleware/public/`)
      — bỏ dữ liệu giả lập (`Math.random`), nối Socket.IO thật
- [ ] `esp_idf_migration` (bản ESP-Mesh chính thức) — CHƯA áp dụng UART_PROTOCOL
      này (Phase 1 vẫn chỉ log qua `ESP_LOGI`/console mặc định). Đã bổ sung
      riêng phần đo latency + self-healing timestamp (yêu cầu Phase 4 trong
      README_MIGRATION.md) nhưng việc chuyển console sang khung nhị phân này
      cần tắt `esp_log` mặc định trên UART0 trước — xem ghi chú trong
      `mesh_root_main.c`.
