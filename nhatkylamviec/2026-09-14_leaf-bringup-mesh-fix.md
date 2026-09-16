# Nhật ký buổi làm việc — 14/09/2026: Bring-up Leaf/Camera, sửa lỗi mesh connection

> File này bổ sung cho `2026-09-13_middleware-test-wifi-debug.md` (bản cập nhật
> gần nhất trước đó). Đọc file này để biết tiến độ mới nhất: 3 board
> ESP32-CAM (Leaf) đã về tay, Leaf #1 đã bring-up thành công hoàn toàn, và 2
> lỗi mesh quan trọng đã được xác định + sửa.

## Bối cảnh đầu buổi

3 board ESP32-CAM (Leaf/Camera) về tay — đơn Shopee từ shop SAMIORE.vn
(SKU ESP32-CAM-MB-OV3660, 209.200đ/cái, tổng 473.928đ, giao thành công
14/9/2026), **thay thế** đơn Điện Tử Tuyết Nga cũ (525.000đ) đã bị huỷ. Loại
board này có sẵn đế mạch nạp USB tích hợp (không cần FT232RL rời như dự tính
ban đầu). `node_camera.ino` (firmware Leaf) trước đó chưa từng được nạp vào
board thật.

## Đã hoàn thành

### 1. Nhận hàng và xác nhận đúng linh kiện
- Camera OV3660 ✓, form factor ESP32-CAM chuẩn (khớp pinout code đã viết) ✓,
  anten PCB tích hợp (không phải IPEX rời, đúng quyết định đã chốt) ✓.
- Board đi kèm sẵn đế mạch nạp USB — không cần mạch nạp rời.

### 2. Bring-up Leaf #1 (COM11)
- Board thật dùng chip **CH340** (khác CP2102 của Root) — lần đầu cắm không
  lên COM port do dùng nhầm cáp sạc; đổi sang cáp data thì Windows nhận
  đúng "USB-SERIAL CH340 (COM11)".
- Cấu hình Arduino IDE: Board = **AI Thinker ESP32-CAM**, Partition Scheme =
  **Huge APP (3MB No OTA/1MB SPIFFS)**.
- Serial Monitor phải đặt **115200 baud** (khác Root dùng 921600 — dễ nhầm
  vì baud mặc định IDE là 1000000, gây log tượng hình/mojibake nếu không đổi).

### 3. Phát hiện lỗi: 100% chunk gửi mesh thất bại
Camera chụp ảnh bình thường (PSRAM ~4MB xác nhận, heap ổn định), nhưng
`mesh.sendBroadcast()` trả về `THAT BAI` ở mọi chunk, mọi frame liên tục.

### 4. Chẩn đoán lỗi #1 — bug lõi của painlessMesh (GitLab #275)
Đọc trực tiếp `node_camera.ino`: **không hề gọi** `stationManual()`/
`STATION_SSID` (đã grep, không có kết quả) — vậy mà log vẫn hiện lặp lại
`ERROR: stationScan(): scan could not start`. Tra cứu xác nhận: `stationScan()`
là hàm **lõi** painlessMesh dùng để quét tìm node mesh khác (không riêng
tính năng bridge WiFi router) — nên bug GitLab #275 ("No scans ever after
WiFi.disconnect()") chặn luôn khả năng Leaf tìm thấy Root, không chỉ ảnh
hưởng tính năng bridge của Root như từng ghi nhầm trong
`2026-09-13_middleware-test-wifi-debug.md` mục 6.

**Fix:** chuyển thư viện từ painlessMesh gốc sang fork **Alteriom/painlessMesh**
(github.com/Alteriom/painlessMesh — có các commit sửa đúng nhóm lỗi
scan-kẹt/không-tự-kết-nối-lại). Xác nhận qua log: Leaf tìm thấy Root
(`Found 1 nodes`, `ESP32_MESH_VIDEO`, -57dBm) và bắt đầu `connectToAP()`.

### 5. Chẩn đoán lỗi #2 — tranh chấp radio với STA-bridge của Root
Sau fix #1, Leaf vẫn rớt kết nối liên tục (`connected but unrooted` →
`Found 0 nodes` → lặp lại), dù đã từng thấy Root. Giả thuyết: `node_root.ino`
vẫn chạy `mesh.stationManual(STATION_SSID, STATION_PASSWORD)` cố nối WiFi
nhà (`HFamily_Vmember`), lặp lỗi `scan could not start, retrying in 15 s`
mỗi 15 giây — nghi ngờ việc này tranh chấp cùng 1 radio WiFi với AP mesh
của Root, làm gián đoạn kết nối Leaf đang giữ.

**Fix:** comment out dòng `mesh.stationManual(...)` trong `node_root.ino`
(dòng ~1136, giữ lại comment ghi rõ lý do + cách bật lại, không xoá).

### 6. Xác nhận Leaf #1 hoạt động hoàn toàn ổn định
Sau cả 2 fix, nạp lại Root + reset đồng thời Root và Leaf #1:
- Serial Monitor Leaf: hàng loạt frame gửi thành công liên tục, không còn
  `THAT BAI`.
- `http://localhost:3000/status` (middleware): `serialConnected: true`,
  `node_count: 1`, `node_ids: [1207449329]`, `frames_ok: 1306`,
  `frames_drop: 6` (~0.5%, bình thường), `framesBadCrc: 0`.
- Console middleware: log `HOAN THANH` ghép ảnh liên tục đúng frame_id,
  đúng chunks, đúng size — xác nhận dữ liệu ảnh thật sự tới nơi nguyên vẹn,
  không chỉ "gửi thành công" ở tầng mesh.

**Leaf #1 coi như hoàn tất 100%** cho mục tiêu 1-hop.

### 7. Bắt đầu bring-up Leaf #2 và #3 — CHƯA hoàn tất
Cắm đồng thời 2 board (2 cáp data) nhưng Device Manager chỉ nhận **1** COM
port CH340 mới — nghi ngờ 1 trong 2 cáp là loại sạc, không phải data
(giống lỗi gặp phải với Leaf #1 lúc đầu). Chưa xác định được board nào
join thành công, chưa nạp firmware cho board còn lại.

### 8. Lên kế hoạch nguồn điện độc lập cho các Leaf
Kiểm tra linh kiện đang có: 5x module boost 0.9-5V→5V (ngõ ra là **cổng
USB-A cái**, không phải dây trần) + đế pin 18650 (2 đã có dây, 3 chưa) +
4x pin 18650 + 10x tụ 470uF + dây bẹ đực-cái.

Phát hiện quan trọng: vì boost có ngõ ra USB-A, **không cần hàn gì ở phía
board ESP32-CAM** — chỉ cần cáp Micro-USB thường nối từ boost sang đế mạch
nạp của board. Chỉ còn 1 điểm cần hàn: dây đế pin → chân IN của module
boost, và tụ 470uF hàn song song ngay tại chân IN đó (đơn giản hoá đáng kể
so với kế hoạch hàn trực tiếp vào chân 5V/GND của board ban đầu).

Đã soạn hướng dẫn hàn từng bước (8 bước: chuẩn bị dụng cụ → xác định cực →
hàn đế pin → nối vào IN boost → đo OUT bằng đồng hồ trước khi dùng →
kết nối vào board qua cáp → hàn tụ 470uF // tại IN → bọc cách điện).

### 9. Chốt mua thêm cáp
- **Cáp USB-Micro cho việc nạp firmware** (data cable, cắm PC): tham khảo
  hshop.vn ~30.000đ/cái hoặc tương đương trên Shopee (từ khoá "truyền dữ
  liệu" để tránh mua nhầm loại chỉ sạc).
- **Cáp USB-Micro cho nguồn điện** (boost → board, không cần data): chốt
  mua **Ugreen US289 Quick Charge 3.0, 0.25m, 5V/2.4A, 35.000đ/cái × 3 =
  105.000đ** (Ugreen Việt Nam Store, Shopee Mall) — dòng 2.4A đủ đáp ứng
  dòng đỉnh 400-500mA lúc ESP32 phát WiFi, độ dài 0.25m hợp lý vì các linh
  kiện sẽ gắn gần nhau trên khung Drone/Robot.

## Trạng thái code hiện tại (quan trọng — đọc trước khi sửa tiếp)

- **`node_root.ino`**: đã comment `mesh.stationManual(STATION_SSID,
  STATION_PASSWORD)` (dòng ~1136). `setDebugMsgTypes` đang tạm để
  `ERROR | CONNECTION` (bản debug, CHƯA đổi lại `ERROR`).
- **`node_camera.ino`**: `setDebugMsgTypes` đang tạm để `ERROR | CONNECTION`
  (CHƯA đổi lại `ERROR`). Không có thay đổi nào khác ngoài debug flags.
- **Thư viện painlessMesh**: đã đổi sang fork **Alteriom/painlessMesh** cho
  cả `node_root` và `node_camera` — không dùng bản gốc nữa.

## Vấn đề còn tồn đọng

- `setDebugMsgTypes(ERROR | CONNECTION)` vẫn đang bật ở cả 2 file — cần đổi
  lại `ERROR` sau khi hoàn tất bring-up cả 3 Leaf, tránh log `CONNECTION`
  làm nhiễu UART nhị phân của Root giống sự cố `bytesSkipped` cao hôm 13/9.
- Đã tắt hẳn tính năng bridge WiFi router của Root (`stationManual`) — nghĩa
  là xem HTTP MJPEG qua trình duyệt (qua WiFi nhà) sẽ không hoạt động. Tính
  năng này không nằm trong 3 tiêu chí hội đồng chấm, nhưng cần quyết định
  có bỏ hẳn hay tìm cách bật lại an toàn sau này.
- Leaf #2 và #3 chưa bring-up xong — vướng nghi vấn cáp sạc/data.
- Leaf #1 hiện vẫn đang chạy bằng cáp cắm PC, **chưa có nguồn độc lập**
  (pin 18650 + boost) — cần hoàn tất hàn theo hướng dẫn đã soạn.
- `claude_trang-thai-hien-tai.md` và `claude_migration-esp-wifi-mesh.md`
  chưa được đồng bộ với toàn bộ phát hiện hôm nay (fork Alteriom, tắt
  STA-bridge, kết quả Leaf #1).

## Việc cần làm tiếp theo (gợi ý thứ tự)

1. Xác định rõ cáp nào trong 2 cáp đang có là data thật (test riêng từng
   cáp) để hoàn tất bring-up Leaf #2 và #3, theo đúng quy trình đã dùng
   cho Leaf #1 (không cần sửa code gì thêm, dùng nguyên bản hiện tại).
2. Sau khi cả 3 Leaf bring-up xong: đổi `setDebugMsgTypes` về lại `ERROR`
   ở cả `node_root.ino` và `node_camera.ino`.
3. Mua cáp: 2 cáp data (~30.000đ/cái) cho việc nạp firmware, và 3 cáp
   Ugreen US289 (35.000đ/cái) cho nguồn điện — không nhầm lẫn hai loại.
4. Hàn bộ nguồn độc lập (pin 18650 → đế pin → mạch boost IN, đo OUT bằng
   đồng hồ trước khi dùng, tụ 470uF // tại IN) cho cả 3 Leaf.
5. Xác nhận `/status` có `node_count = 3` khi cả 3 Leaf chạy bằng nguồn
   độc lập song song với Root.
6. Bắt đầu đo **throughput theo số hop** (1/2/3 hop) và **self-healing**
   (tắt nguồn 1 node trung gian, đo thời gian phục hồi qua log
   `onDroppedConnection`/`onChangedConnections`) — đúng 3 tiêu chí hội
   đồng chấm điểm.
7. Đồng bộ lại `claude_trang-thai-hien-tai.md` với toàn bộ nội dung hôm nay.
