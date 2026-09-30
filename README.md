# ESP32 Mesh Video Streaming
## Truyền phát video MJPEG phân tán qua mạng painlessMesh

Hệ thống truyền ảnh JPEG liên tục từ nhiều Camera Node (ESP32 + OV3660) về Root Node qua mạng Mesh WiFi. Root Node ghép ảnh và phát luồng MJPEG qua HTTP để xem trực tiếp trên trình duyệt web.

```
  ┌─────────────────┐         painlessMesh          ┌─────────────────┐
  │  Camera Node 1  │ ──── JPEG Chunks (Base64) ──→ │                 │
  │  ESP32 + OV3660 │                               │   ROOT NODE     │
  └─────────────────┘                               │   ESP32         │
  ┌─────────────────┐                               │                 │     ┌───────────┐
  │  Camera Node 2  │ ──── JPEG Chunks (Base64) ──→ │  HTTP Server    │ ──→ │  Browser  │
  │  ESP32 + OV3660 │                               │  /stream MJPEG  │     │  Viewer   │
  └─────────────────┘                               └─────────────────┘     └───────────┘
```

---

## 📁 Cấu trúc dự án

```
thuattoancatanh/
├── shared/                     ← Bản gốc các header dùng chung (giống hệt bản trong 2 sketch)
│   ├── base64_util.h           ← Mã hóa/giải mã Base64
│   ├── chunk_protocol.h        ← Giao thức đóng gói chunk
│   └── crc16.h                 ← CRC-16/CCITT-FALSE
├── node_camera/                ← Firmware Camera Node (Leaf)
│   ├── node_camera.ino
│   └── mesh_config.h           ← Cấu hình riêng của Leaf
├── node_root/                  ← Firmware Root Node
│   ├── node_root.ino
│   ├── mesh_config.h           ← Cấu hình riêng của Root
│   ├── secrets.example.h       ← Mẫu SSID/mật khẩu router
│   └── secrets.h               ← Tự tạo, KHÔNG commit
└── README.md
```

---

## 🔧 Phần cứng yêu cầu

### Camera Node (Sender)
| Linh kiện | Mô tả |
|-----------|--------|
| ESP32-CAM (AI-Thinker) | Module ESP32 tích hợp camera, có PSRAM 4MB |
| Camera OV3660 | Sensor camera 3MP, giao diện DVP 8-bit |
| USB-TTL Adapter | Để nạp firmware (ESP32-CAM không có USB onboard) |

### Root Node (Receiver)
| Linh kiện | Mô tả |
|-----------|--------|
| ESP32 có PSRAM | ESP32-CAM, ESP32-WROVER, hoặc module có PSRAM |
| WiFi Router | Để client kết nối xem stream |

> **Lưu ý**: Root Node KHÔNG cần camera. Bất kỳ ESP32 nào có PSRAM đều dùng được.

---

## 📌 Sơ đồ kết nối Pin (ESP32-CAM AI-Thinker)

```
ESP32-CAM              OV3660 (DVP 8-bit)
─────────              ──────────────────
GPIO 5  (Y2)    ────→  D0 (LSB)
GPIO 18 (Y3)    ────→  D1
GPIO 19 (Y4)    ────→  D2
GPIO 21 (Y5)    ────→  D3
GPIO 36 (Y6)    ────→  D4
GPIO 39 (Y7)    ────→  D5
GPIO 34 (Y8)    ────→  D6
GPIO 35 (Y9)    ────→  D7 (MSB)
GPIO 0  (XCLK)  ────→  XCLK (Master Clock)
GPIO 22 (PCLK)  ────→  PCLK (Pixel Clock)
GPIO 25 (VSYNC) ────→  VSYNC
GPIO 23 (HREF)  ────→  HREF
GPIO 26 (SIOD)  ────→  SDA (I2C/SCCB)
GPIO 27 (SIOC)  ────→  SCL (I2C/SCCB)
GPIO 32 (PWDN)  ────→  PWDN (Power Down)
```

> Nếu dùng board khác (ESP-EYE, TTGO T-Camera, M5Stack), thay đổi pin mapping trong `node_camera/mesh_config.h`.

---

## 📦 Cài đặt thư viện

### Arduino IDE (Library Manager)

Cài đặt các thư viện sau qua **Sketch → Include Library → Manage Libraries**:

1. **painlessMesh** — Mesh networking
2. **ArduinoJson** — JSON processing (dependency)
3. **TaskScheduler** — Task scheduling (dependency)
4. **AsyncTCP** — Async TCP cho ESP32 (dependency)

### Board Manager

1. Mở **File → Preferences**
2. Thêm URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. **Tools → Board → Board Manager** → Tìm "esp32" → Cài đặt
4. Chọn board: **AI Thinker ESP32-CAM**

---

## 🚀 Hướng dẫn nạp Firmware

### Bước 1: Chuẩn bị file

Mỗi sketch (`node_camera/`, `node_root/`) đã có sẵn **`mesh_config.h` riêng**. Cấu hình Root và Leaf
khác nhau có chủ đích (Root không có PSRAM, cần slot ghép ảnh; Leaf cần pin camera, lịch chụp), nên
**không** copy `mesh_config.h` giữa hai sketch.

Các header dùng chung (`chunk_protocol.h`, `crc16.h`, `base64_util.h`) đã có sẵn trong mỗi sketch.
Khi sửa một file này, sửa bản ở `shared/` rồi copy sang cả hai sketch để 3 bản luôn giống hệt nhau:

```bash
for f in chunk_protocol.h crc16.h base64_util.h; do
  cp shared/$f node_camera/; cp shared/$f node_root/
done
```

### Bước 2: Cấu hình WiFi (Root Node)

SSID/mật khẩu router **không** nằm trong `mesh_config.h`. Tạo `node_root/secrets.h` từ file mẫu:

```bash
cp node_root/secrets.example.h node_root/secrets.h
# rồi sửa secrets.h:
#define STATION_SSID       "TEN_WIFI_ROUTER"      // ← WiFi router của bạn
#define STATION_PASSWORD   "MAT_KHAU_WIFI"         // ← Mật khẩu WiFi
```

`secrets.h` đã nằm trong `.gitignore` (`thuattoan/**/secrets.h`), không commit file này.
Nếu chưa có `secrets.h`, `mesh_config.h` dùng giá trị mẫu nên build vẫn chạy (chỉ không vào được router).

### Bước 3: Cài đặt Arduino IDE

| Tùy chọn | Giá trị |
|-----------|---------|
| Board | AI Thinker ESP32-CAM |
| CPU Frequency | 240MHz |
| Flash Mode | QIO |
| Flash Size | 4MB |
| Partition Scheme | Huge APP (3MB No OTA) |
| **PSRAM** | **Enabled** ← BẮT BUỘC |
| Upload Speed | 921600 |

### Bước 4: Nạp firmware

1. **Camera Node**: Mở `node_camera/node_camera.ino` → Upload
2. **Root Node**: Mở `node_root/node_root.ino` → Upload
3. Mở Serial Monitor (115200 baud)

### Bước 5: Xem stream

1. Xem Serial Monitor của Root Node → tìm dòng:
   ```
   ║  HTTP Server: http://192.168.1.xxx/
   ║  MJPEG Stream: http://192.168.1.xxx/stream
   ```
2. Kết nối máy tính/điện thoại vào **cùng WiFi router**
3. Mở trình duyệt → nhập URL `/stream` hoặc `/`

---

## 🔬 Nguyên lý hoạt động

### Thuật toán cắt ảnh (Chunking) — Camera Node

```
Ảnh JPEG (ví dụ 10500 bytes)
│
├── Chunk #0:  [Header 16B][Payload 1024B] → offset 0
├── Chunk #1:  [Header 16B][Payload 1024B] → offset 1024
├── Chunk #2:  [Header 16B][Payload 1024B] → offset 2048
├── ...
├── Chunk #9:  [Header 16B][Payload 1024B] → offset 9216
└── Chunk #10: [Header 16B][Payload 260B]  → offset 10240 (chunk cuối)

Mỗi packet = Header (16 bytes) + Payload (≤1024 bytes)
           → Base64 encode → String → mesh.sendBroadcast()
```

### Cấu trúc Header (16 bytes)

```
Offset  Size     Field          Mô tả
──────  ────     ─────          ─────
0       4 bytes  node_id        ID duy nhất camera node
4       2 bytes  frame_id       Số thứ tự bức ảnh
6       2 bytes  chunk_index    Vị trí chunk (0-based)
8       2 bytes  total_chunks   Tổng số chunk của ảnh
10      2 bytes  payload_len    Kích thước payload thực tế
12      4 bytes  capture_us     mesh.getNodeTime() lúc chụp (đo latency)
```

### Thuật toán ghép ảnh (Reassembly) — Root Node

```
Nhận chunk từ Mesh
      │
      ▼
Base64 decode → Header + Payload
      │
      ▼
Tìm FrameAssembly slot cho (node_id, frame_id)
      │
      ├── Chưa có → Tạo slot mới (cấp phát từ pool)
      │
      ▼
Copy payload vào buffer tại offset = chunk_index × 1024
      │
      ▼
Đã nhận đủ tất cả chunk? (received_count == total_chunks)
      │
      ├── Chưa → Chờ chunk tiếp theo
      │
      └── Rồi → Frame hoàn chỉnh!
              → Copy sang latest_frame buffer
              → Gửi cho HTTP MJPEG client
```

### Xử lý lỗi (Error Handling)

| Tình huống | Xử lý |
|------------|--------|
| Chunk bị mất | Timeout 5s → hủy frame, giải phóng slot |
| Chunk trùng lặp | Kiểm tra bitmap → bỏ qua |
| Chunk đến sai thứ tự | OK — dùng chunk_index tính offset trực tiếp |
| Hết slot assembly | Evict slot cũ nhất (LRU) |
| JPEG data corrupt | Validate SOI marker (0xFFD8) trước khi gửi |
| Tràn buffer | Kiểm tra offset + payload_len ≤ MAX_FRAME_SIZE |

---

## ⚙️ Tùy chỉnh thông số

Các thông số nằm trong `mesh_config.h` của từng sketch (Leaf: `node_camera/`, Root: `node_root/`):

| Thông số | Mặc định | Mô tả |
|----------|----------|--------|
| `MAX_CHUNK_PAYLOAD` | 1024 | Kích thước chunk (bytes) |
| `CHUNK_SEND_DELAY_MS` | 20 | Delay giữa các chunk (ms) |
| `CAM_FRAME_SIZE` | QVGA | Độ phân giải (QQVGA, QVGA, VGA) |
| `CAM_JPEG_QUALITY` | 12 | Chất lượng JPEG (0-63, thấp=tốt) |
| `CAPTURE_INTERVAL_MS` | 600 | Interval chụp của Leaf (~1.7 FPS) |
| `REASSEMBLY_TIMEOUT_MS` | 2000 | Timeout ghép ảnh ở Root (ms) |
| `MAX_CONCURRENT_FRAMES` | 4 | Slot ghép đồng thời |

### Điều chỉnh FPS

```
FPS ≈ 1000 / (CAPTURE_INTERVAL_MS + total_chunks × CHUNK_SEND_DELAY_MS)

Ví dụ: QVGA quality 12, ảnh ~10KB, 10 chunks:
  FPS ≈ 1000 / (300 + 10 × 20) = 1000 / 500 = 2 FPS

Muốn tăng FPS:
  - Giảm CAPTURE_INTERVAL_MS (tối thiểu 100)
  - Giảm CHUNK_SEND_DELAY_MS (tối thiểu 10, rủi ro mất gói)
  - Tăng JPEG quality số (giảm chất lượng → file nhỏ hơn)
  - Giảm resolution xuống QQVGA (160×120)
```

---

## 📊 HTTP Endpoints

| Endpoint | Method | Mô tả |
|----------|--------|--------|
| `/` | GET | Trang HTML với player MJPEG |
| `/stream` | GET | Luồng MJPEG (multipart/x-mixed-replace) |
| `/status` | GET | Thống kê JSON (frames, chunks, memory) |

---

## 🐛 Debug / Troubleshooting

### Camera Node không chụp được ảnh
- Kiểm tra PSRAM: `Tools → PSRAM → Enabled`
- Kiểm tra kết nối camera (dây, connector)
- Serial Monitor: tìm dòng `[CAM] Khoi tao THANH CONG`

### Không thấy stream trên trình duyệt
- Kiểm tra Root Node đã kết nối WiFi: tìm dòng `HTTP Server: http://...`
- Đảm bảo client cùng mạng WiFi với Root
- Thử truy cập `/status` trước để kiểm tra server

### FPS thấp hoặc hình bị giật
- Giảm `CAM_JPEG_QUALITY` (tăng số = file nhỏ hơn nhưng xấu hơn)
- Giảm resolution: đổi `FRAMESIZE_QVGA` → `FRAMESIZE_QQVGA`
- Giảm `CHUNK_SEND_DELAY_MS` (cẩn thận mất gói)
- Kiểm tra khoảng cách giữa các node Mesh

### Frame bị drop nhiều (Serial: "TIMEOUT")
- Tăng `REASSEMBLY_TIMEOUT_MS` (ví dụ 8000)
- Giảm tốc độ gửi: tăng `CAPTURE_INTERVAL_MS`
- Kiểm tra tín hiệu WiFi giữa các node

---

## 📄 License

Dự án phục vụ mục đích học tập — Đồ án Chuyên ngành.
