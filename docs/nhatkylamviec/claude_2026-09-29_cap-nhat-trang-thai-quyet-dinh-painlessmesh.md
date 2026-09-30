# Cập nhật trạng thái 29/09/2026 — Test 3 Leaf chạy pin, chốt dùng painlessMesh

> File này THAY THẾ các phần đã lỗi thời trong `claude_migration-esp-wifi-mesh.md`
> (quyết định 01/09) và `claude_trang-thai-hien-tai.md` (cập nhật 10/09). Khi hai
> nguồn mâu thuẫn, ưu tiên file này.

---

## 1. Quyết định kiến trúc (chốt 29/09)

**Dùng painlessMesh làm bản chính thức. Dừng phát triển bản ESP-IDF. Không chuyển luồng ảnh sang ESP-NOW.**

Căn cứ: danh sách nhiệm vụ của đồ án (mục 2 bên dưới) ghi rõ "giao thức mạng mesh
painlessMesh" ở nhiệm vụ 1, và yêu cầu truyền ảnh "qua mạng mesh" ở nhiệm vụ 3.

- Quyết định ngày 01/09 chuyển sang ESP-WIFI-MESH (ESP-IDF) trong
  `claude_migration-esp-wifi-mesh.md` được **thay thế**. Quyết định cũ dựa trên câu
  "ESP-Mesh hoặc BATMAN-adv" của đề bài gốc.
- Thư mục `thuattoan/esp_idf_migration/`: **giữ nguyên, không xóa, không làm tiếp**
  (bỏ Phase 2 camera thật, Phase 3 HTTP server, và việc nạp lên board). Trong báo cáo,
  nhắc đến ở mục "Hướng phát triển" như phương án dùng API chính thức của Espressif.
  Các ưu điểm đã ghi: bỏ Base64 (giảm ~33% dữ liệu), `MESH_DATA_TODS` tự đi đúng đường
  về Root, có cơ chế chống tràn buffer (receiving window, `esp_mesh_set_xon_qsize`).
- Hai ý tưởng lấy lại từ bản ESP-IDF để áp dụng vào painlessMesh:
  1. Mốc thời gian lúc chụp trong header chunk (`capture_ts_ms`) để đo latency.
  2. Đo self-healing bằng sự kiện mất và có lại kết nối.
- ESP-NOW: gác lại. Truyền ảnh qua ESP-NOW (một chặng, không relay) lệch khỏi yêu cầu
  "qua mạng mesh". Cách tăng FPS phù hợp hơn là làm ngay trong painlessMesh: Root ghi
  UART không chặn, và staggering bằng `mesh.getNodeTime()`.
- Việc còn lại: xác nhận nhanh với giảng viên rằng danh sách nhiệm vụ này đã được duyệt.

---

## 2. Danh sách nhiệm vụ của đồ án

1. Nghiên cứu FreeRTOS và giao thức mạng mesh painlessMesh; thiết kế mô hình giao tiếp đa node cho hệ thống drone.
2. Thiết kế và thi công phần cứng các node, hệ thống nguồn; đo dòng tiêu thụ và đánh giá độ ổn định khi truyền dữ liệu.
3. Xây dựng cơ chế phân mảnh, truyền, nhận và ghép dữ liệu ảnh qua mạng mesh; xử lý mất gói và mất chunk.
4. Xây dựng hệ thống thu nhận dữ liệu trên máy tính qua UART tốc độ cao và truyền dữ liệu thời gian thực đến giao diện giám sát bằng WebSocket.
5. Phát triển Web Dashboard hiển thị luồng ảnh, topology mạng, RSSI, latency, throughput và FPS.
6. Xây dựng kịch bản thử nghiệm theo số node, số hop và tải dữ liệu; đánh giá throughput, latency, packet loss, khả năng phục hồi và khả năng mở rộng của mạng.

---

## 3. Phần cứng và node ID đang dùng

| Vai trò | Node ID | Ghi chú |
|---|---|---|
| Root | 2846999225 | ESP32-WROOM-32D DevKitC, không PSRAM, CP210x (COM7), UART 921600 |
| Leaf | 1207369673 | ESP32-CAM-MB OV3660, pin 18650 + boost. Là "Leaf3/COM14" hôm 16/9, từng lỗi nguồn |
| Leaf | 1207433521 | ESP32-CAM-MB OV3660, pin 18650 + boost. Chạy ổn định nhất |
| Leaf | 1207449329 | ESP32-CAM-MB OV3660, pin 18650 + boost |

- Cả 3 Leaf đã được hàn bộ nguồn pin (18650 + boost DC-DC 0.9–5V). Tụ 470uF mới chỉ đặt ở chân IN của boost.
- Leaf nạp qua chip CH340, số COM thay đổi theo từng lần cắm (đã gặp COM13/14/15/16).
  Board "AI Thinker ESP32-CAM", partition "Huge APP", thư viện Alteriom/painlessMesh 2.0.3.

---

## 4. Thay đổi code trong ngày

| File | Thay đổi | Trạng thái |
|---|---|---|
| `thuattoan/node_camera/mesh_config.h` (dòng ~97) | `CAPTURE_INTERVAL_MS` 300 → 600 (~3.3 fps → ~1.7 fps mỗi Leaf) | Đã sửa bằng Claude Code, đã nạp cả 3 Leaf, **chưa commit** |

- Không có thay đổi nào khác. `node_camera.ino` đã dùng sẵn `CAPTURE_INTERVAL_MS` trong
  `loop()` và in dòng `[SYS] San sang! Chup moi 600 ms (~1.7 FPS)` lúc khởi động. Dùng
  dòng này để kiểm tra Leaf đã chạy bản mới.
- `node_camera.ino` đã có `mesh.setDebugMsgTypes(ERROR)` (dòng 573). Phía Root chưa kiểm tra lại.
- Sự cố: lượt sửa đầu tiên lúc 10:43 không chạy vì Claude Code báo "Login expired". Do
  đó test1.csv lúc 10:49 vẫn đo trên firmware cũ (300 ms).

---

## 5. Kết quả thử nghiệm (dashboard tab "So sánh 1/2/3 leaf", xuất CSV)

### 5.1 CSV ngày 28/09 — firmware 27/09, chu kỳ 300 ms, 3 Leaf chạy pin

- Khi mạng còn chạy: FPS tổng tăng tuyến tính 3.5 → 7 → 10 theo 1/2/3 Leaf, drop 0–5%.
  So với 82% drop hôm 17/09, bản sửa 4 slot ghép và `sendSingle` đã có tác dụng.
- **Lỗi chính: cả mạng đứng cùng lúc.** Với 3 Leaf, mạng đứng sau 6–26 giây: FPS tổng về 0
  hơn 38 giây, `node_count` vẫn là 3, heap Root tụt từ ~90KB xuống 57KB và tiếp tục giảm
  trong lúc không nhận được gì. Với 2 Leaf thỉnh thoảng cũng đứng.
- Có hiện tượng "node ma": Root vẫn đếm node nhưng node đó không gửi được ảnh.

### 5.2 test1.csv (29/09, ~10:49 giờ VN) — vẫn firmware cũ 300 ms

- Mỗi Leaf vẫn 3.2–3.5 fps. 2 Leaf đứng một lần (heap Root 104 → 66KB).
- Lần chạy "3 Leaf" thực chất chỉ có 1 Leaf gửi (FPS tổng ~3.2).
- Leaf 1207369673 rớt sớm ("mất tín hiệu", dừng ở frame #95).

### 5.3 test2.csv — 1 Leaf, chu kỳ 600 ms

- 1.67 ± 0.40 fps, 7.8 KB/s, 8.4 chunk/s, drop 0%, heap Root 111–113KB, frame ~4.6KB.
- **Đây là mốc 1 Leaf chuẩn.** Không dùng giá trị "1 leaf = 0.86 fps" trong bảng so sánh
  của test3, vì phần đó bị lẫn các đoạn trống lúc rút Leaf ra nạp firmware.

### 5.4 test3.csv — 3 Leaf, chu kỳ 600 ms

- 3 Leaf chạy liên tục ~2 phút (04:20:45 → 04:22:50 UTC), không đứng.
- FPS tổng 4.85 ± 0.96, mỗi Leaf ~1.6, 31 chunk/s (so với ~50 chunk/s hôm 28/09),
  heap Root chủ yếu 95–101KB.
- Có một đợt drop 7–36% (04:21:49 → 04:22:09), heap thấp nhất 73KB. Mạng tự hồi lại.

### 5.5 TEST4-5P.csv — phiên tối (14:07 → 14:16 UTC, khoảng 21:07 → 21:16 giờ VN)

(File còn lẫn dữ liệu buổi sáng vì chưa bấm "Xoá dữ liệu". Số liệu dưới đây chỉ lấy phiên tối.)

| Thời gian (UTC) | Số Leaf | Diễn biến |
|---|---|---|
| 14:07:59 → 14:10:59 | 3 | **Chạy liền 3 phút**, ~5 fps tổng, drop gần 0%, heap Root 87–108KB, không đứng |
| ~14:11 | 3 → 1 | Leaf 1207369673 rớt, không quay lại (dừng ở frame #343) |
| 14:12:01 → 14:16:15 | 2 | Chạy ổn ~4 phút, ~3.3 fps tổng, một đợt drop ngắn 14:13:05 → 14:13:25 |
| 14:16:17 → 14:16:39 | 2 (thực tế 1) | Leaf 1207449329 ngừng gửi (frame #758) nhưng vẫn được đếm; heap Root 104 → 82KB |

Giá trị self-healing đã ghi được (ms): 1243, 3944, 7691, 8823, 19919. Mẫu cũ ghi ở file
18/09 là 885, 10461, 32528. **Phân tán quá lớn, chưa dùng được.**

---

## 6. Chẩn đoán

1. **Root quá tải → cả mạng đứng: ĐÃ XÁC NHẬN.** Giảm tải ~40% (300 → 600 ms) thì 3 Leaf hết
   đứng trong suốt thời gian đo. Cơ chế nghi ngờ: ở mức 3 Leaf × 3.3 fps, Root phải đẩy
   ~47KB/s ảnh qua UART 921600 baud (tối đa ~92KB/s). Như vậy Root dành gần nửa thời gian
   để ghi UART, `mesh.update()` bị gọi thưa, và dữ liệu dồn lại trong các kết nối TCP của
   painlessMesh. Cái giá của cách sửa hiện tại là FPS tổng giảm từ ~10 xuống ~5.
2. **Leaf rớt từng con: nghi do pin (CHƯA xác nhận).** Leaf rớt lần lượt chứ không đồng
   loạt, nên nguyên nhân không nằm ở Root. Các Leaf đã chạy pin từ sáng đến tối, và
   1207369673 là con có lịch sử lỗi nguồn.
3. **Node ma: CHƯA xử lý.** Khi Leaf ngừng gửi, Root vẫn giữ kết nối đó và heap Root giảm dần.
4. Mạng vẫn còn sát ngưỡng: vẫn có các đợt drop ngắn và heap có lúc sụt xuống ~73KB.

---

## 7. Đối chiếu nhiệm vụ với tiến độ

| # | Tiến độ | Đã có | Còn thiếu |
|---|---|---|---|
| 1 | Gần xong | Mô hình Root/Leaf, beacon `#ROOT` + `sendSingle`; nghiên cứu painlessMesh (`compass_artifact_...md`) | Mục FreeRTOS trong báo cáo (task, heap, vòng `mesh.update()`); lấy lỗi Root quá tải làm ví dụ |
| 2 | Đã thi công | 3 bộ nguồn pin, số liệu ổn định qua CSV | **Đo dòng tiêu thụ**, ước tính thời gian chạy pin, tụ sát chân 5V của board |
| 3 | Phần lớn xong | Chunk 1024B, bitmap, timeout, 4 slot ghép, `mo_phong_reassembly.py` 5/5 | Gửi lại chunk bị mất (NACK gộp); hiện mất chunk là hủy cả frame |
| 4 | Xong | UART_PROTOCOL (MAGIC+TYPE+LEN+CRC16) 921600, middleware Socket.IO, test parser 7/7 | Root ghi UART không chặn `loop()` |
| 5 | Một nửa | Luồng ảnh, FPS, throughput, tỉ lệ hủy, heap, self-healing | **Topology dạng cây** (cha/con), **RSSI**, **latency** |
| 6 | Một phần | Theo số node 1/2/3; theo tải (300 ms và 600 ms là hai mức tải) | **Theo số hop**, latency, phục hồi đo lặp lại, mở rộng trên 3 node |

---

## 8. Việc cần làm (theo thứ tự ưu tiên)

### A. Ổn định hệ thống
- [ ] Đo điện áp từng pin 18650 (nhất là của 1207369673 và 1207449329), rồi sạc đầy cả 3.
- [ ] Cắm USB vào Leaf vừa rớt, mở Serial xem có dòng "Brownout detector was triggered" không.
- [ ] Hàn thêm tụ 470uF song song vào chân 5V–GND sát board ESP32-CAM; đo áp OUT của boost khi có tải.
- [ ] Watchdog phía Root: có node trong mesh mà không nhận được chunk nào trong 10 giây → khởi động lại.
- [ ] Watchdog phía Leaf: mất beacon `#ROOT` 8 giây → đặt `root_id = 0` để học lại; mất 30 giây → `ESP.restart()`.
- [ ] Sạc đầy pin, bấm "Xoá dữ liệu", chạy lại 3 Leaf liên tục 5–10 phút.

### B. Mở khóa nhiệm vụ 5 và 6
- [ ] Thêm mốc thời gian (`mesh.getNodeTime()`) vào header chunk. Root lấy thời gian nhận trừ đi để ra latency; đưa lên dashboard.
- [ ] Cho Root báo quan hệ cha/con và RSSI; dashboard vẽ topology dạng cây.
- [ ] Bố trí chuỗi Root → Relay → Leaf xa, đo throughput và latency ở 1/2/3 hop.
- [ ] Đo self-healing ít nhất 5 lần theo quy trình 6 bước (file 17/09); tách riêng hai loại "đổi tuyến qua relay" và "nối lại sau khi mất tín hiệu"; ghi min/max/trung bình.

### C. Nâng chất lượng
- [ ] Đo dòng tiêu thụ của 1 Leaf khi đang truyền ảnh.
- [ ] Root ghi UART không chặn: `Serial.setTxBufferSize(8192)` trước `Serial.begin`; bỏ frame nếu `availableForWrite()` không đủ chỗ; không cấp phát thêm khi heap < 40KB.
- [ ] Kiểm tra slot ghép ảnh ở Root được cấp phát tĩnh hay bằng `malloc`; nếu là `malloc` thì đổi thành mảng tĩnh.
- [ ] Staggering bằng `mesh.getNodeTime()`, rồi thử đưa chu kỳ chụp về gần 300 ms.
- [ ] Cơ chế NACK gộp để gửi lại chunk bị mất.
- [ ] Kiểm tra `setDebugMsgTypes` ở `node_root.ino` đã về `ERROR` chưa.

### D. Repo và tài liệu
- [ ] Commit thay đổi `CAPTURE_INTERVAL_MS = 600` cùng các file CSV hôm nay (test1, test2, test3, TEST4-5P).
- [ ] Commit `.gitignore` đã sửa từ 27/09, và chạy `git rm -r --cached thuattoan/middleware/node_modules`.
- [ ] Thêm nhật ký 29/09 vào `nhatkylamviec/`. File gần nhất ở đó vẫn là 17/09.
- [ ] Ghi chú vào đầu `claude_migration-esp-wifi-mesh.md` và `claude_trang-thai-hien-tai.md`: "Đã được thay thế bởi file 29/09 — giữ painlessMesh".
- [ ] Viết mục FreeRTOS và mục "Hướng phát triển" (ESP-IDF, ESP-NOW) trong báo cáo.

---

## 9. Nguồn thông tin

- Số liệu đo: `test1.csv`, `test2.csv`, `test3.csv`, `TEST4-5P.csv` (29/09), CSV ngày 28/09, ảnh chụp dashboard.
- `nhat-ky_2026-09-26_den_2026-09-27.md`: firmware 4 slot, `sendSingle`, beacon, UART 921600, việc repo còn dở.
- `claude_2026-09-18_bao-cao-tien-do-va-kiem-tra-source-code.md`: thứ tự ưu tiên cũ, latency, trạng thái ESP-IDF.
- `claude_2026-09-17_multi-leaf-throughput-diagnosis-selfhealing-plan.md`: 82% drop, quy trình đo self-healing.
- `2026-09-16_leaf-bringup-rf-diagnosis-power-plan.md`: node ID, lỗi nguồn COM14, quy trình hàn nguồn.
- `compass_artifact_...md`: giới hạn của painlessMesh, staggering, ESP-NOW, NACK gộp.
- `claude_migration-esp-wifi-mesh.md`, `claude_trang-thai-hien-tai.md`: quyết định cũ (đã thay thế).
- Danh sách nhiệm vụ: do Danny cung cấp trong chat ngày 29/09.
