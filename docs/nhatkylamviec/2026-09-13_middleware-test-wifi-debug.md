# Nhật ký buổi làm việc — 13/09/2026: Kiểm thử pipeline Root ↔ Middleware ↔ Dashboard

> File này bổ sung cho `2026-09-12_root-node-bringup.md` (bản cập nhật gần nhất
> trước đó). Đọc file này để biết tiến độ mới nhất về việc khép kín vòng dữ
> liệu Root → UART → Middleware → Dashboard, và các phát hiện/quyết định phát
> sinh trong quá trình test.

## Bối cảnh đầu buổi

Board Root đã bring-up xong hôm 12/09 (đã sửa lỗi PSRAM, mesh khởi tạo OK,
telemetry gửi đều mỗi 2s). Middleware Node.js đã viết sẵn từ trước nhưng
**chưa từng chạy thử với board thật**. Mục tiêu buổi này: xác nhận middleware
thực sự đọc và parse đúng dữ liệu UART thật từ board Root, khép kín chuỗi
Root → UART → middleware → Dashboard.

## Đã hoàn thành

### 1. Cài đặt và chạy middleware Node.js lần đầu

- Gặp lỗi PowerShell chặn `npm.ps1` (Execution Policy mặc định của Windows).
  Sửa bằng lệnh (chỉ áp dụng cho user hiện tại, không cần quyền admin):
  ```powershell
  Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
  ```
- `npm install` chạy thành công (118 packages). Riêng gói
  `@serialport/bindings-cpp` (cần cho việc đọc cổng COM) bị chặn install
  script mặc định — phải approve thủ công trước khi build binding native:
  ```powershell
  npm install-scripts approve @serialport/bindings-cpp
  npm install
  ```
- Lần chạy `node server.js COM7` đầu tiên báo `Access denied` — do Serial
  Monitor trong Arduino IDE còn đang giữ cổng COM7. **Bài học quan trọng**:
  Serial Monitor / Upload firmware / middleware Node.js — chỉ được **một**
  trong ba chương trình này giữ COM7 tại một thời điểm, không bao giờ chạy
  đồng thời.

### 2. Xác nhận middleware parse đúng dữ liệu thật từ board Root

Sau khi đóng Serial Monitor, middleware kết nối COM7 thành công:
- Dashboard: `http://localhost:3000`
- Status API: `http://localhost:3000/status`

`/status` xác nhận: `serialConnected: true`, `framesOk: 100`,
`framesBadCrc: 0`, `latestTelemetry` khớp đúng định dạng
`UART_PROTOCOL.md` §4.2. **Đây là điểm khép kín thành công chuỗi
Root → UART → middleware.**

Dashboard HTML tĩnh (route `/`) trả về lỗi `Cannot GET /` — chưa serve được
trang, nhiều khả năng thiếu `public/index.html` hoặc thiếu cấu hình
`express.static` trong `server.js`. **Chưa điều tra tiếp**, không chặn tiến
độ vì `/status` đã đủ xác nhận dữ liệu thật.

### 3. Phát hiện vấn đề `bytesSkipped` cao bất thường

`/status` lần đầu cho thấy `bytesSkipped: 1,308,534 bytes` dù chỉ có ~100
khung telemetry nhỏ (~150 byte/khung) — nghi ngờ log debug thô của
painlessMesh (`CONNECTION: ...`) in lẫn vào cùng đường UART với khung nhị
phân, khiến parser phải bỏ qua rất nhiều byte để tìm lại MAGIC tiếp theo.

Nguyên nhân gốc: `STATION_SSID`/`STATION_PASSWORD` trong `mesh_config.h` vẫn
để giá trị mẫu `YOUR_WIFI_SSID`/`YOUR_WIFI_PASSWORD` — khiến painlessMesh
liên tục lặp vòng scan lỗi và in log dồn dập.

### 4. Sửa `mesh_config.h` — lần đầu sửa nhầm chỗ

- Nhầm lẫn ban đầu: gõ đè WiFi thật (`HFamily_Vmember` /
  `091214Mrs.H`) vào **`MESH_SSID`/`MESH_PASSWORD`** (dòng 30-31 — dùng cho
  mesh nội bộ giữa các ESP32) thay vì **`STATION_SSID`/`STATION_PASSWORD`**
  (dòng 44-45 — dùng để Root nối vào WiFi router). Đã phát hiện và sửa lại
  đúng chỗ.
- `MESH_SSID`/`MESH_PASSWORD` được trả về giá trị dùng cho mesh, nhưng
  **`MESH_PASSWORD` đổi thành `"MeshVideo2026"`** (khác bản gốc dự án
  `"MeshVideo2024!"`).
  ⚠️ **CẦN NHỚ**: phải dùng đúng giá trị `"MeshVideo2026"` này (không phải
  bản gốc) khi cấu hình `mesh_config.h` cho `node_camera/` sau này, nếu
  không các node sẽ không tham gia được cùng một mesh (SSID/password mesh
  phải khớp giữa mọi node).
- Sau khi sửa đúng chỗ và Upload lại: log xác nhận `STATION_SSID` đã đúng
  (`stationScan(): HFamily_Vmember`), nhưng WiFi vẫn không kết nối được —
  lỗi `ERROR: stationScan(): scan could not start, retrying in 15 s` lặp
  vô hạn.

### 5. Thử downgrade gói ESP32 Arduino core (không giải quyết được lỗi WiFi)

- Giả thuyết: painlessMesh chưa tương thích với gói `esp32 by Espressif
  Systems` bản 3.x (theo ghi chú chính thức của RandomNerdTutorials,
  khuyến nghị hạ về 2.0.x).
- Đã hạ gói từ **3.3.11 xuống 2.0.17** qua Boards Manager, build lại toàn
  bộ từ đầu, Upload lại.
- **Kết quả: lỗi `scan could not start` vẫn y hệt sau khi downgrade** —
  loại bỏ giả thuyết core 3.x là nguyên nhân duy nhất/chính.
- Ghi nhận phụ: `heap_free` tăng nhẹ (122496 → ~153668) sau khi đổi core,
  chưa rõ nguyên nhân cụ thể, không ảnh hưởng chức năng.

### 6. Xác định nguyên nhân sâu hơn — quyết định tạm dừng debug WiFi station

Tìm ra log khớp với một issue đã biết, **chưa có bản vá chính thức** của
painlessMesh (GitLab issue #275: "No scans ever after WiFi.disconnect()"):
một sự kiện `STA_DISCONNECTED` giả xảy ra ngay lúc boot (khi ESP32 chuyển
sang chế độ AP+STA) kích hoạt `WiFi.disconnect()` nội bộ trong thư viện,
khiến **mọi lần scan sau đó thất bại vĩnh viễn** cho tới khi board reset —
khớp chính xác với hành vi quan sát được (lỗi lặp lại từ giây đầu tiên,
không bao giờ tự phục hồi).

**Quyết định**: tạm dừng debug tính năng kết nối WiFi router (chỉ phục vụ
xem HTTP MJPEG qua trình duyệt — tính năng phụ). Lý do: 3 tiêu chí hội đồng
chấm (throughput/latency/self-healing) đều đo qua **mesh nội bộ giữa các
ESP32**, hoàn toàn không phụ thuộc vào việc Root có nối được WiFi router
hay không.

### 7. Xử lý đúng nguyên nhân gốc — giảm mức log debug của painlessMesh

Thay vì tiếp tục theo đuổi việc nối WiFi router, xử lý trực tiếp nguyên
nhân gây `bytesSkipped` cao: đổi dòng trong `node_root.ino` (khoảng dòng
1100), ngay trước `mesh.init(...)`:
```cpp
// Trước:
mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
// Sau:
mesh.setDebugMsgTypes(ERROR);
```

Upload lại, xác nhận qua Serial Monitor: hết hẳn log `CONNECTION: ...`
spam, chỉ còn đúng 1 dòng `ERROR: stationScan()...` mỗi ~15 giây.

**Kết quả đo lại qua middleware**: `bytesSkipped` giảm từ **1.308.534 →
1.098 bytes** (giảm hơn 1000 lần), `framesOk: 30`, `framesBadCrc: 0` —
đường UART giờ gần như sạch hoàn toàn cho dữ liệu nhị phân.

## Vấn đề còn tồn đọng

- **WiFi Station (Root ↔ router nhà) vẫn không hoạt động** — lỗi
  `scan could not start` là bug đã biết của painlessMesh, chưa có cách khắc
  phục đã kiểm chứng. Không chặn tiến độ đo các tiêu chí chấm điểm; nếu
  muốn xem MJPEG qua trình duyệt sau này, có thể thử sketch ví dụ gốc
  `bridge.ino` của thư viện để cô lập xem lỗi nằm ở code riêng của dự án
  hay ở chính thư viện/môi trường.
- **Dashboard HTML tĩnh (route `/`) chưa serve được** (`Cannot GET /`) —
  chưa điều tra nguyên nhân cụ thể (thiếu `public/index.html` hoặc thiếu
  cấu hình `express.static` trong `server.js`).
- **Gói board `esp32 by Espressif Systems` đã hạ xuống 2.0.17** (từ
  3.3.11) — nên áp dụng đồng bộ khi bring-up 3 board ESP32-CAM (Leaf) sắp
  tới, dù downgrade này không giải quyết được lỗi WiFi station, vẫn là
  khuyến nghị chính thức chung cho painlessMesh.
- **`MESH_PASSWORD` trong `mesh_config.h` hiện là `"MeshVideo2026"`**
  (khác bản gốc dự án `"MeshVideo2024!"`) — phải dùng đúng giá trị mới
  này khi cấu hình `mesh_config.h` cho `node_camera/`, nếu không các node
  sẽ không tham gia được cùng một mesh.
- File `claude/trang-thai-hien-tai.md` và `claude/migration-esp-wifi-mesh.md`
  vẫn mô tả trạng thái cũ (trước phiên 12/09 và 13/09) — **chưa** được
  đồng bộ với: phát hiện/sửa PSRAM (12/09), và toàn bộ nội dung phiên hôm
  nay (middleware hoạt động thật, bug WiFi station, core đã hạ xuống
  2.0.17, `setDebugMsgTypes` đã đổi).
- Vẫn chỉ có 1 mình board Root — chưa có Leaf/Camera node nào để đo
  throughput/latency/self-healing thật.

## Việc cần làm tiếp theo (gợi ý thứ tự)

1. Cập nhật `claude/trang-thai-hien-tai.md` với toàn bộ phát hiện của phiên
   13/09 (đặc biệt: `MESH_PASSWORD` mới, core đã hạ xuống 2.0.17,
   `setDebugMsgTypes(ERROR)` đã đổi) — tránh nhầm lẫn ở phiên sau.
2. *(Tùy chọn, không cấp thiết)* Thử sketch ví dụ gốc `bridge.ino` của
   painlessMesh để xác định lỗi WiFi station nằm ở thư viện/môi trường hay
   ở code riêng của dự án.
3. *(Tùy chọn)* Điều tra route `/` của dashboard — kiểm tra
   `public/index.html` và cấu hình `express.static` trong `server.js`.
4. Khi 3 board ESP32-CAM (Leaf) về tay: dùng đúng
   `MESH_SSID="ESP32_MESH_VIDEO"` / `MESH_PASSWORD="MeshVideo2026"`, hạ
   core xuống 2.0.17 và đổi `setDebugMsgTypes(ERROR)` ngay từ đầu — tránh
   lặp lại toàn bộ quá trình debug hôm nay.
5. Bắt đầu đo thật throughput/latency/self-healing theo đúng 3 tiêu chí
   hội đồng chấm, một khi có đủ node Leaf.
