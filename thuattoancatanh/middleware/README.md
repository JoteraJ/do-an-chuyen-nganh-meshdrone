# Middleware (Lớp 2) — Node.js

Đọc dữ liệu từ Root Node qua UART (theo `../UART_PROTOCOL.md`), phát lại qua
Socket.IO cho dashboard, kèm sẵn dashboard chạy được (`public/dashboard.html`).

```
Root Node (ESP32) --UART 921600bps--> server.js --Socket.IO--> dashboard.html
```

## Cài đặt

Cần Node.js ≥ 18 (khuyến nghị LTS). Cài driver CH340/CP2102 cho USB-TTL của
board trước nếu Windows chưa nhận cổng COM.

```bash
cd thuattoancatanh/middleware
npm install
```

## Kiểm tra logic KHÔNG cần board (khuyến nghị chạy trước)

```bash
npm test
```

Chạy 7 kịch bản mô phỏng (khung hợp lệ, dữ liệu bị cắt vụn, rác xen giữa,
CRC sai, LEN giả mạo...) — phải ra `7/7 test PASS` trước khi cắm board thật.

## Chạy với board thật

1. Cắm Root Node vào PC qua USB-TTL/FTDI.
2. Mở **Device Manager** (Windows) → mục **Ports (COM & LPT)** → tìm tên cổng
   (ví dụ `COM5`).
3. Chạy:

```bash
node server.js COM5
```

hoặc trên Linux/macOS (ví dụ `/dev/ttyUSB0`):

```bash
node server.js /dev/ttyUSB0
```

4. Mở trình duyệt: `http://localhost:3000/dashboard.html`

Nếu chưa cắm board hoặc sai tên cổng, middleware KHÔNG crash — nó in cảnh báo
và tự thử kết nối lại mỗi 3 giây, dashboard vẫn mở được (chỉ chưa có dữ liệu).

## Endpoint

| Endpoint | Mô tả |
|---|---|
| `GET /dashboard.html` | Dashboard (React qua Babel-standalone, không cần build) |
| `GET /status` | JSON: trạng thái cổng serial, telemetry mới nhất, thống kê parser |
| Socket.IO event `frame` | `{ nodeId, frameId, jpegBase64 }` — 1 ảnh JPEG đã ghép xong |
| Socket.IO event `telemetry` | Xem cấu trúc trong `../UART_PROTOCOL.md` §4.2 |
| Socket.IO event `status` | `{ serialConnected, portName, baudRate }` |

## Đổi baud rate / cổng qua biến môi trường (thay vì đối số dòng lệnh)

```bash
COM_PORT=COM7 BAUD_RATE=921600 HTTP_PORT=3000 node server.js
```

## Cấu trúc file

- `protocol.js` — CRC16 + `FrameParser` (tách khung nhị phân), không phụ
  thuộc serialport/socket.io nên test được độc lập.
- `test_protocol.js` — bộ test tự động cho `protocol.js`.
- `server.js` — mở serial, dùng `FrameParser`, phát Socket.IO, serve
  `public/`.
- `public/dashboard.html` — dashboard, dùng Socket.IO client tự động serve
  bởi `socket.io` package tại `/socket.io/socket.io.js` (không cần Internet).

## Việc còn thiếu / cần bạn tự làm tiếp

- Root Node (firmware) hiện mới gửi danh sách node dạng **phẳng**
  (`node_ids`), chưa có quan hệ cha/con → cây topology trên dashboard chỉ vẽ
  1 tầng. Muốn có cây đầy đủ như tài liệu mô tả, cần Root tự thu thập thêm
  thông tin này từ mesh (xem TODO trong `node_root.ino`).
- Chưa có RSSI theo từng liên kết (painlessMesh không lộ chắc chắn qua API
  public) → dashboard không hiển thị số này thay vì bịa ra số giả.
- Throughput (Kbps) tổng thể chưa được tính — có thể suy ra từ
  `chunks_received × MAX_CHUNK_PAYLOAD / thời gian` phía middleware nếu cần
  cho báo cáo, hiện chưa làm.
