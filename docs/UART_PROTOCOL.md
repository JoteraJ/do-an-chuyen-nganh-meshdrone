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
131072 byte (đủ dư cho `MAX_FRAME_SIZE = 24576` + 10 byte sub-header ảnh); khung
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
  "uart_drop": 0,
  "uart_defer": 0,
  "log_drop": 0,
  "late_chunk_drop": 1,
  "loop_max_ms": 38,
  "heap_min_win": 90112,
  "heap_min_boot": 71680,
  "rx_q": null,
  "node_count": 3,
  "node_ids": [111, 222, 333],
  "last_self_heal_ms": 842,
  "silent_events": 1,
  "heal_events": [ { "id": 333, "ms": 4120, "start_src": "list", "ts": 123400 } ],
  "lat_avg_ms": 412.3,
  "lat_min_ms": 305.0,
  "lat_max_ms": 580.2,
  "lat_n": 9,
  "lat_by_node": { "111": 398.1, "222": 430.7 },
  "fw": "2026-09-29b",
  "chunk_err": 0,
  "tree": { "nodeId": 999, "root": true, "subs": [
    { "nodeId": 111, "subs": [ { "nodeId": 333 } ] },
    { "nodeId": 222 } ] },
  "nodes": [
    { "id": 111, "fw": "2026-09-30c", "rssi": -58, "heap": 101234, "link_rssi": -61, "link_rssi_src": "ap_parent",  "hop": 1,
      "last_chunk_age_ms": 240, "silent": false, "rst": 1, "up_s": 3605, "stg": 1, "age_ms": 820 },
    { "id": 333, "fw": "2026-09-30c", "rssi": -77, "heap": 99876,  "link_rssi": -79, "link_rssi_src": "ap_parent",  "hop": 2,
      "last_chunk_age_ms": 12450, "silent": true, "rst": 9, "up_s": 41, "stg": null, "age_ms": 1430 },
    { "id": 222, "fw": null, "rssi": null, "heap": null, "link_rssi": -70, "link_rssi_src": "ap_parent", "hop": 1,
      "last_chunk_age_ms": null, "silent": false, "rst": null, "up_s": null, "stg": null, "age_ms": null }
  ]
}
```

Mọi trường thêm sau bản 2026-09-29 đều **tùy chọn**: bên nhận phải chịu được khi
thiếu trường (Root/Leaf firmware cũ) hoặc giá trị `null`.

`node_count` / `node_ids`: các node trong mesh nhìn từ Root, **không gồm Root** và
không trùng lặp (Root firmware từ 2026-09-30d lọc cả trường hợp Root vọng lại trong
nhánh con do vòng liên kết — lỗi test12).

`last_self_heal_ms` (cách đo cũ, giữ để so sánh): thời gian (ms) giữa lúc mất kết
nối tới một node con (`onDroppedConnection`) và lần `changedConnection` kế tiếp
trên Root. Không phân biệt node, không biết node đã gửi lại dữ liệu hay chưa.
`-1` nghĩa là chưa từng xảy ra sự kiện rớt kết nối nào.

`heal_events` (cách đo mới, theo luồng dữ liệu, từng node): mảng tối đa 8 sự kiện
gần nhất **chưa gửi**; mỗi sự kiện chỉ xuất hiện trong đúng một khung telemetry rồi
Root xóa, nên dashboard cộng dồn được mà không đếm trùng (vẫn nên lọc trùng theo
`id` + `ts` vì middleware gửi lại telemetry cuối cho client mới nối). Mỗi phần tử:
- `id`: nodeId của node vừa hồi phục.
- `ms`: từ lúc bắt đầu sự cố tới **chunk hợp lệ đầu tiên** Root nhận từ node đó sau sự cố.
- `start_src`: nguồn mốc bắt đầu. `"list"` = lúc node biến khỏi `getNodeList()` của
  Root (Root đối chiếu trong `changedConnectionCallback()` và mỗi 500 ms).
  `"last_chunk"` = không thấy node rời mesh (rời rồi vào lại giữa 2 lần đối chiếu,
  hoặc node im lặng mà vẫn trong mesh) nhưng khoảng trống giữa 2 chunk > 10 s → mốc
  là chunk cuối trước khoảng trống.
- `ts`: `millis()` của Root lúc đo xong (cùng thang với `ts` của telemetry).
Không có sự kiện mới: `[]`.

`silent_events`: số lần (từ lúc Root khởi động) một node chuyển sang trạng thái
`silent` (xem `nodes`).

`lat_*`: latency từ lúc Leaf chụp xong (`capture_us` trong ChunkHeader, lấy
`mesh.getNodeTime()` ngay sau `esp_camera_fb_get()`) tới lúc Root ghép xong frame
(`mesh.getNodeTime()` tại Root), gom trong MỘT chu kỳ telemetry rồi reset.
Mẫu > 10 s bị bỏ (đồng hồ mesh chưa đồng bộ). Chu kỳ không có mẫu:
`lat_n = 0`, `lat_avg_ms`/`lat_min_ms`/`lat_max_ms` = `null`, `lat_by_node` = `{}`.
Khóa của `lat_by_node` là node_id dạng chuỗi (JSON bắt buộc khóa là chuỗi).

`uart_drop`: số frame ảnh Root đã ghép xong nhưng **không** ghi ra UART vì TX
buffer (`Serial.setTxBufferSize(8192)`, `UART_TX_BUFFER_SIZE`) còn trống dưới một
nửa — frame trước chưa ra hết dây. Root bỏ frame ảnh thay vì chặn `loop()` (chặn
lâu làm `mesh.update()` trễ). Frame bị bỏ ở đây vẫn được tính trong `frames_ok`
(đã ghép xong).

Từ Root firmware 2026-10-01a, TELEMETRY và LOG cũng không chặn `loop()`: chỉ ghi
khi TX buffer đủ chỗ cho **cả khung** (kèm dự phòng cho header item của ring buffer
UART: cần `khung + min(khung, 2048) + 128` byte trống); không đủ thì không ghi byte
nào. Không có TX buffer (`setTxBufferSize` thất bại) hoặc khung lớn hơn khả năng
buffer → ghi chặn như cũ.
- `uart_defer` (tích luỹ): số khung TELEMETRY phải hoãn (đếm 1 lần mỗi khung). Khung
  hoãn được dựng lại với số liệu MỚI NHẤT ở vòng `loop()` sau (không xếp hàng);
  `heal_events` và mẫu `lat_*` chỉ bị xóa sau khi khung đã thật sự ghi → không mất.
- `log_drop` (tích luỹ): số khung LOG bị bỏ vì TX buffer chưa đủ chỗ.

`late_chunk_drop` (tích luỹ, Root từ 2026-10-01a): số chunk **trễ** bị bỏ — chunk tới
khi frame của nó đã đóng (ghép xong, SUPERSEDE, TIMEOUT hoặc EVICTION). Root nhớ cho
mỗi node frame_id mới nhất đã mở slot; chunk không có slot mà frame_id cũ hơn frame
đó 1..16 (so sánh `int16_t`, đúng qua quay vòng 65535 → 0) hoặc đúng frame đó nhưng
frame đã đóng, trong 2 × `REASSEMBLY_TIMEOUT_MS` (4 s) → bỏ, không mở slot mới,
không SUPERSEDE frame đang ghép. (Trước đây chunk trễ mở lại slot cho frame cũ và
hủy luôn frame mới đang ghép — test15: 1 chunk trễ làm mất thêm frame kế tiếp.)

Đo đạc tìm nguyên nhân reset (Root từ 2026-10-01a, chỉ đo): "cửa sổ" = khoảng giữa 2
khung TELEMETRY đã ghi thành công (~2 s), reset sau mỗi lần gửi.
- `loop_max_ms`: vòng `loop()` lâu nhất trong cửa sổ (ms).
- `heap_min_win`: `esp_get_free_heap_size()` thấp nhất trong cửa sổ (byte).
- `heap_min_boot`: `ESP.getMinFreeHeap()` — heap thấp nhất từ lúc khởi động (byte).
- `rx_q`: luôn `null` — hàng đợi nhận/gửi của painlessMesh (`BufferedConnection`)
  là `protected`, không có API đọc độ dài.

`fw`: `FW_VERSION` của Root. `chunk_err`: tổng lỗi giải mã/validate chunk tại Root
từ lúc khởi động (log text của các lỗi này bị giới hạn 1 dòng/giây, bộ đếm thì không).

`tree`: nguyên văn `mesh.asNodeTree().toString()` (= `mesh.subConnectionJson()`,
painlessMesh Alteriom 2.0.3) nhìn từ Root. `root`/`hasTimeAuthority` chỉ có khi
`true`, `subs` chỉ có khi khác rỗng.

`nodes`: một phần tử cho mỗi node trong `tree` (trừ Root), cộng node đã rời mesh
nhưng Root nhận NSTAT chưa quá 60 s (khi đó `hop` = `null`). `hop` = độ sâu trong
cây (nối thẳng Root = 1). `fw`/`rssi`/`heap`/`age_ms` lấy từ message
`#NSTAT{...}` mà Leaf gửi `sendSingle()` tới Root mỗi 2 s; `age_ms` = thời gian từ
lần nhận NSTAT cuối. Node chưa từng gửi NSTAT (firmware cũ): `fw`, `rssi`, `heap`,
`age_ms` = `null`. `rssi` là `WiFi.RSSI()` của liên kết STA từ Leaf lên node cha
(`null` nếu Leaf không có liên kết STA).

`last_chunk_age_ms`: thời gian từ chunk hợp lệ cuối Root nhận của node đó (`null` =
chưa nhận chunk nào từ lúc Root khởi động). `silent`: `true` nếu node còn trong
`getNodeList()` nhưng quá `SILENT_NODE_MS` (10 s) không có chunk, tính từ chunk cuối
hoặc từ lúc node (vào lại) mesh. Root chỉ báo, **không** restart vì node im lặng
(Leaf có watchdog riêng: mất beacon `#ROOT` 8 s → học lại Root, 30 s → `ESP.restart()`).

`rst`, `up_s`: lấy từ `#NSTAT` gần nhất của Leaf (Leaf firmware từ 2026-09-30c).
`rst` = `esp_reset_reason()` lúc Leaf khởi động (ESP-IDF 4.4, core 2.0.17): 1
`POWERON`, 3 `SW` (`ESP.restart()`, vd watchdog mất Root), 4 `PANIC`, 5 `INT_WDT`,
6 `TASK_WDT`, 7 `WDT`, 9 `BROWNOUT`. `up_s` = uptime của Leaf (giây) lúc gửi NSTAT
đó. Leaf firmware cũ: cả hai là `null`.

`stg` (Leaf từ 2026-10-01a): `STAGGER_ENABLE` của Leaf — `1` = chụp theo lịch lệch
pha trên đồng hồ mesh, `0` = lịch cũ theo `millis()` riêng từng board; `null` = Leaf
firmware cũ không gửi.

Message `#NSTAT{...}` Leaf gửi (Leaf → Root qua mesh, không đi qua UART):
`{"id":..,"fw":"..","rssi":..,"heap":..,"frames":..,"chunks_fail":..,"rst":..,"up_s":..,"stg":0|1,"ap_rssi":{..}}`.

`link_rssi`: RSSI (dBm) của liên kết nối node đó với node cha trong `tree`;
`link_rssi_src` cho biết số đo lấy từ đâu. Mỗi liên kết painlessMesh là station
của một bên nối vào AP của bên kia, nên chỉ một trong hai AP thấy bên kia.
Root thử theo thứ tự:
1. `ap_parent` — AP của node cha thấy node con (Root tự đọc
   `esp_wifi_ap_get_sta_list()`; Leaf gửi `"ap_rssi": {"<nodeId>": dBm}` trong `#NSTAT`).
2. `ap_child` — AP của node con thấy node cha (liên kết chiều ngược: station của
   cha nối vào AP của con).
3. `sta` — `rssi` phía station của node con, chỉ khi xác nhận station đó nối vào
   cha: AP của con đã biết mà không có cha, và AP của cha chưa biết.
4. `root_sta` — Root là một đầu và station của Root đang nối vào AP đầu kia
   (`WiFi.BSSID()` → nodeId) → `WiFi.RSSI()` của Root.

`ap_rssi` của một Leaf chỉ được dùng khi NSTAT của Leaf đó chưa quá 6 s. Không có
số đo nào thì `link_rssi` và `link_rssi_src` là `null`. MAC station → nodeId:
painlessMesh tính nodeId = `tcp::encodeNodeId(MAC SoftAP)` (4 byte cuối, big-endian),
mà station nối vào AP bằng MAC STA; với ESP32 (core 2.0.17, 4 universal MAC) MAC
SoftAP = MAC STA có byte cuối +1. Mỗi board tự kiểm tra công thức lúc khởi động
(log `MAC->nodeId ... OK`).

### 4.3 `0x03 LOG` — dòng log debug (thay thế Serial.printf cũ)

PAYLOAD là text UTF-8 thuần (1 dòng, không cần `\n` ở cuối). Middleware in ra
console của PC, giữ nguyên trải nghiệm xem log như khi cắm Serial Monitor
trực tiếp, nhưng không làm hỏng luồng nhị phân.

Từ Root 2026-10-01a: khung LOG bị bỏ (đếm `log_drop`) khi TX buffer chưa đủ chỗ,
trừ dòng lý do restart của heap guard (Root `Serial.flush()` trước khi gửi). Dòng
khởi động đáng chú ý:
- `[SYS] HTTP: TAT | Heap truoc/sau khoi HTTP: X / Y byte` (`HTTP_ENABLE 0`).
- `[BOOT] WDT reset (ly do N): loop_max_ms=.. heap_cuoi=.. heap_min_win=.. stage=.. up_s=..`
  — chỉ sau reset do watchdog (N = 5 `INT_WDT`, 6 `TASK_WDT`, 7 `WDT`); số liệu lấy từ
  vùng `.noinit` Root ghi mỗi vòng `loop()`. `stage` = bước `loop()` đang chạy lúc bị
  cắt: 1 `mesh.update()`, 2 dọn frame timeout, 3 theo dõi node, 4 telemetry,
  5 beacon, 6 `[STAT]`, 7 xong vòng.

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

Byte không thuộc khung hợp lệ (rác ở bước 2–3 và cả khung sai CRC ở bước 5) được
middleware ghi vào `data/raw/uart_raw_YYYYMMDD_HHMMSS.log` (`middleware/raw_logger.js`,
qua `FrameParser` `opts.onSkip`): mỗi dòng có mốc thời gian, ASCII in được giữ
nguyên, byte khác ghi `\xNN`, ghi đĩa mỗi 1 s, xoay file ở 20 MB. Mục đích: giữ lại
các dòng ESP32 in thẳng ra UART0 không qua khung (vd `Task watchdog got triggered`,
log bootloader sau reset) — có thể chen giữa một khung đang gửi làm khung đó sai CRC.

Cài đặt tham chiếu: `middleware/protocol.js` (`FrameParser`), có test tự động
mô phỏng nhiễu/đứt gói tại `middleware/test_protocol.js` (cùng tinh thần với
`archive/mo_phong_reassembly.py` — kiểm chứng thuật toán bằng phần mềm khi chưa có
board thật để test). Ghép ảnh của `node_root.ino` hiện tại được mô phỏng ở
`firmware/tools/test_reassembly_late_chunk.py`.

## 6. Đã áp dụng ở đâu (checklist)

- [x] `thuattoancatanh/shared/crc16.h` — CRC dùng chung cho Arduino
- [x] `thuattoancatanh/node_root/node_root.ino` (nay `firmware/node_root/`) — gửi
      IMAGE_FRAME + TELEMETRY + LOG qua UART 921600bps. HTTP MJPEG cũ tắt từ
      2026-10-01a (`HTTP_ENABLE 0` trong `mesh_config.h`, code giữ trong `#if`)
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
