# Prompt bàn giao — Bring-up firmware cho 3 board Leaf/Camera (ESP32-CAM-MB-OV3660)

> Dùng prompt này để giao việc cho Antigravity / Claude Code trên máy local
> (nơi có project `thuattoancatanh/`, board thật, Arduino IDE). Đây KHÔNG
> phải code — đây là bản mô tả công việc theo đúng cấu trúc 5 phần bạn hay
> dùng.

---

## 1. Vai trò

Bạn là kỹ sư firmware embedded, đang hỗ trợ bring-up 3 board ESP32-CAM
(camera OV3660, có sẵn đế mạch nạp USB tích hợp) làm **Leaf/Camera node**
cho đồ án mạng WiFi Mesh (painlessMesh) kết nối Drone/Robot. Board Root đã
bring-up xong và đang chạy ổn định.

## 2. Bối cảnh (đã xác nhận trong các phiên làm việc trước)

- Firmware Leaf nằm tại `thuattoancatanh/node_camera/` (painlessMesh,
  chưa từng nạp vào board thật — theo `2026-09-12_root-node-bringup.md`,
  mục "Việc còn thiếu").
- Board Root đã bring-up xong, mesh init OK, telemetry gửi đều mỗi 2s qua
  UART 921600 baud (theo `2026-09-12_root-node-bringup.md`).
- **Cấu hình mesh phải khớp chính xác với Root** (theo
  `2026-09-13_middleware-test-wifi-debug.md`, mục 4 và "Việc cần làm tiếp
  theo" #4):
  - `MESH_SSID = "ESP32_MESH_VIDEO"`
  - `MESH_PASSWORD = "MeshVideo2026"` (⚠️ khác bản gốc dự án
    `"MeshVideo2024!"` — phải dùng đúng giá trị mới này)
- Gói board `esp32 by Espressif Systems` trên máy nạp Root đã hạ xuống
  **2.0.17** (từ 3.3.11) do khuyến nghị chính thức cho painlessMesh —
  nên dùng cùng version khi nạp cho Leaf để tránh lệch môi trường (theo
  `2026-09-13_middleware-test-wifi-debug.md`, mục 5 + "Việc còn tồn đọng").
- `mesh.setDebugMsgTypes(ERROR)` (KHÔNG dùng
  `ERROR | STARTUP | CONNECTION`) — bản đầy đủ gây log spam làm nghẽn/nhiễu
  dữ liệu binary trên UART (bài học từ Root, theo
  `2026-09-13_middleware-test-wifi-debug.md`, mục 7). Với Leaf, UART không
  phải đường truyền ảnh chính (ảnh đi qua mesh) nhưng vẫn nên đặt log mức
  ERROR ngay từ đầu để tránh thói quen sai.
- Trước khi mở Arduino IDE: phải copy `shared/*.h` (đã có `crc16.h`) vào
  `node_camera/` (theo `claude_trang-thai-hien-tai.md`, mục "Việc còn
  thiếu / cần làm tiếp").
- Leaf **bắt buộc cần PSRAM thật** (khác Root không cần) vì phải cấp phát
  buffer ảnh JPEG — ESP32-CAM tiêu chuẩn thường có sẵn PSRAM, nhưng Root
  từng bị "bất ngờ" vì board thực tế không có PSRAM dù code giả định có
  (theo `2026-09-12_root-node-bringup.md`, mục 4) — nên **kiểm tra log
  PSRAM ngay ở bước đầu**, đừng giả định.
- Cấu trúc gói ảnh đã chốt: `CHUNK_SIZE = 1024`,
  `ImageChunkPacket { frame_id, chunk_id, total_chunks, data_len,
  payload[1024] }` (theo `gemini-code-1787323211193.md` /
  `Tong_Hop_Do_An_WiFi_Mesh_Drone.pdf`) — không đổi trừ khi có lý do rõ
  ràng và đã bàn trước.
- 3 board vừa nhận là loại "ESP32-CAM-MB-OV3660" (đế mạch nạp tích hợp
  sẵn) — mỗi board tự có cổng Micro USB riêng để nạp, không cần tráo qua
  tráo lại 1 mạch nạp như dự tính ban đầu với FT232RL.

## 3. Yêu cầu chi tiết (làm tuần tự, từng board một — KHÔNG nạp cả 3 cùng lúc)

1. Xác nhận version gói `esp32 by Espressif Systems` trong Arduino IDE là
   `2.0.17`. Nếu chưa, hạ xuống qua Boards Manager.
2. Copy `shared/*.h` (gồm `crc16.h`) vào `thuattoancatanh/node_camera/`.
3. Mở `node_camera/mesh_config.h`, xác nhận/sửa:
   - `MESH_SSID = "ESP32_MESH_VIDEO"`
   - `MESH_PASSWORD = "MeshVideo2026"`
   - Không đụng vào `STATION_SSID`/`STATION_PASSWORD` nếu file có (Leaf
     không cần nối WiFi router, chỉ cần tham gia mesh nội bộ).
4. Tìm dòng gọi `mesh.setDebugMsgTypes(...)` trong `node_camera.ino`
   (hoặc tương đương), đặt thành `mesh.setDebugMsgTypes(ERROR)`.
5. Cắm **board Leaf #1**, chọn đúng COM port mới (khác COM7 của Root nếu
   Root đang cắm song song), nạp firmware.
6. Mở Serial Monitor (baud khớp với `Serial.begin()` trong code — kiểm
   tra giá trị thật trong file, không giả định 921600 vì đây không phải
   đường UART đi middleware), xác nhận theo thứ tự:
   - Camera OV3660 init thành công (không lỗi `esp_camera_init failed`).
   - **Không có lỗi thiếu PSRAM** (nếu có, đây là vấn đề cần dừng lại xử
     lý trước khi qua board tiếp theo — xem cách Root đã sửa trong
     `2026-09-12_root-node-bringup.md` mục 4 làm tham khảo, nhưng lưu ý
     hướng sửa của Root là "bỏ PSRAM" — với Leaf thì ngược lại, nếu thiếu
     PSRAM thật thì đây là vấn đề phần cứng, không phải bug code, cần
     báo lại chứ không tự ý đổi sang SRAM nội bộ).
   - Node tham gia mesh thành công, thấy được Root trong danh sách kết
     nối (log kiểu `New Connection` hoặc tương đương từ painlessMesh).
7. Nếu board Leaf #1 ổn, lặp lại bước 5-6 cho Leaf #2 rồi Leaf #3.
8. Sau khi cả 3 board đều join mesh: bật `middleware/` (`node server.js
   COMx` — COM port của Root), mở `/status`, xác nhận `node_count` và
   `node_ids` tăng lên đúng 3 khi cả 3 Leaf đang bật, và `framesOk` bắt
   đầu tăng khi Leaf gửi ảnh (không chỉ dừng ở telemetry như log trước).

## 4. Định dạng kết quả mong muốn

Báo cáo lại dạng checklist ngắn, theo đúng thứ tự 3 board, mỗi board ghi
rõ: có nạp được không, log camera/PSRAM/mesh-join ra sao (copy nguyên
dòng log lỗi nếu có), và bước nào đang bị chặn (nếu có) — **không tự sửa
những gì chưa hiểu rõ nguyên nhân, dừng lại báo cáo cụ thể** (đúng tinh
thần đã làm với lỗi PSRAM và lỗi `scan could not start` của Root — ghi
log, xác định nguyên nhân, rồi mới quyết định hướng sửa).

## 5. Ràng buộc quan trọng (nhắc lại)

- **Không đổi `CHUNK_SIZE`, cấu trúc `ImageChunkPacket`, hay
  `MESH_PASSWORD`** khỏi giá trị đã chốt ở trên nếu chưa bàn trước.
- **Không sửa code/cấu hình của Root** trong lúc làm việc này.
- Nạp và xác nhận **từng board một**, không nạp đồng thời 3 board rồi mới
  kiểm tra — nếu có lỗi sẽ khó biết lỗi ở board nào.
- Nếu gặp lỗi thiếu PSRAM trên board Leaf: đây là tín hiệu **dừng lại**,
  không tự ý áp dụng cách sửa "bỏ PSRAM" đã dùng cho Root, vì Leaf cần
  PSRAM thật để hoạt động đúng.
