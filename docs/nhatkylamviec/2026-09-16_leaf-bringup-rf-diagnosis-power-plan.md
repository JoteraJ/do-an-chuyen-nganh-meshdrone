# Nhật ký buổi làm việc — 16/09/2026: Bring-up Leaf2/Leaf3, chẩn đoán tranh chấp RF, lên kế hoạch nguồn pin độc lập

> File này bổ sung cho `2026-09-14_leaf-bringup-mesh-fix.md` (bản cập nhật gần
> nhất trước đó). Đọc file này để biết tiến độ mới nhất: Leaf2 và Leaf3 đã
> bring-up xong, nguyên nhân gốc gây "THAT BAI" liên tục được xác định là
> tranh chấp RF khi 2 Leaf đặt quá gần nhau (không phải lỗi phần mềm/phần
> cứng), và đã lên kế hoạch chi tiết hàn nguồn pin độc lập.

## Bối cảnh đầu buổi

Leaf #1 đã hoàn tất từ 14/9. Hôm nay tiếp tục bring-up Leaf #2 (COM13) và
Leaf #3 (COM14) — cả 2 board mới cắm lần đầu tiên, chạy song song với Root
(COM7) đã có sẵn.

## Đã hoàn thành

### 1. Nạp firmware cho Leaf2 (COM13) và Leaf3 (COM14)

- Xác nhận đúng Board = AI Thinker ESP32-CAM, Partition Scheme = Huge APP
  (3MB No OTA/1MB SPIFFS), đúng thư viện fork Alteriom/painlessMesh (version
  2.0.3) — khớp cấu hình đã dùng cho Leaf #1.
- Cả 2 con nạp thành công, gửi frame liên tục không lỗi ở mức board đơn lẻ.

### 2. Phát hiện và sửa lỗi cáp/nguồn USB yếu cho COM14

- Lần đầu chạy, COM14 báo 100% "THAT BAI" khi gửi chunk dù firmware đúng,
  kèm heap tụt dần bất thường (154KB → 72KB sau ~190 frame).
- Đổi sang cổng/cáp USB khác cho riêng COM14 → hết lỗi ngay, heap ổn định
  trở lại (~154KB) — xác nhận nguyên nhân là nguồn/cáp không đủ dòng đỉnh
  cho cổng đó, không phải lỗi code.

### 3. Chẩn đoán và loại bỏ nhầm giả thuyết trùng NodeID

- Suốt nhiều lần kiểm tra `/status`, Root luôn chỉ thấy đúng 1 trong 2 Leaf
  (`node_count` dao động 1-2), ban đầu nghi ngờ 2 board trùng NodeID (lỗi
  MAC từ nhà sản xuất board clone giá rẻ).
- Xác nhận bằng cách đọc trực tiếp dòng `[MESH] Node ID: ...` trên Serial
  Monitor mỗi board ngay sau khi RST: Leaf2 = `1207433521`, Leaf3 =
  `1207369673`, Root = `2846999225` (suy ra từ log kết nối) — cả 3 ID đều
  khác nhau, giả thuyết trùng ID bị loại bỏ hoàn toàn.

### 4. Xác định đúng nguyên nhân gốc: tranh chấp RF khi 2 Leaf đặt quá gần nhau

- Rút COM14 ra, chỉ chạy Root + COM13 một mình → COM13 join mesh ổn định
  ngay, gửi liên tục không lỗi, `frames_drop` ngừng tăng.
- Kết luận: khi cả 2 Leaf cùng hoạt động cạnh nhau trên bàn (cách nhau vài
  cm), 2 radio ESP32 tranh chấp kênh WiFi gây hiện tượng "flapping" (đổi
  cha liên tục, rớt kết nối lặp lại) — không phải lỗi phần mềm hay phần
  cứng của board nào.
- Đo được số liệu self-healing thật đầu tiên trong buổi: `last_self_heal_ms`
  ghi nhận các giá trị 885ms, 10461ms, 32528ms tùy điều kiện nhiễu tại thời
  điểm đo — dữ liệu thô đầu tiên cho 1 trong 3 tiêu chí hội đồng chấm
  (self-healing).

### 5. Lên kế hoạch và soạn quy trình hàn nguồn pin 18650 độc lập

- Xác nhận lại linh kiện sẵn có: 5 module boost DC-DC (ngõ ra USB-A), 5 đế
  pin 18650 (2 đã có dây sẵn), 4 pin 18650, 10 tụ 470uF.
- Soạn quy trình 8 bước: xác định cực → hàn dây pin vào IN boost → đo điện
  áp OUT bằng VOM trước khi cắm vào board → nối boost vào board qua cáp
  Micro-USB nguồn (không cần hàn gì ở board vì boost đã có sẵn ngõ ra
  USB-A) → hàn tụ 470uF song song tại IN → bọc cách điện → test thực tế
  với board đang chạy.
- Vẽ sơ đồ minh họa đấu dây: đế pin → chân IN boost (kèm tụ 470uF song
  song) → ngõ ra USB-A → cáp Micro-USB → board ESP32-CAM.

## Vấn đề còn tồn đọng

- Root có dấu hiệu tự reset ít nhất 1 lần trong buổi mà không ai chủ động
  bấm RST (`ts` telemetry nhảy về giá trị nhỏ giữa 2 lần check `/status`
  liên tiếp) — chưa rõ nguyên nhân, cần theo dõi thêm ở buổi sau, có thể
  liên quan tới vấn đề treo nguồn/heap đã gặp trước đây (theo
  `2026-09-12_root-node-bringup.md`).
- Cả 3 Leaf hiện vẫn đang chạy bằng cáp cắm PC, CHƯA có board nào dùng
  nguồn pin độc lập — quy trình hàn đã soạn xong nhưng chưa thực hiện.
- `setDebugMsgTypes(ERROR | CONNECTION)` vẫn đang bật ở `node_camera.ino`
  cho cả Leaf2 và Leaf3 (dùng để debug hôm nay) — cần đổi lại `ERROR` sau
  khi việc hàn nguồn hoàn tất và không còn cần soi log CONNECTION chi tiết
  nữa (đúng lưu ý đã ghi từ `2026-09-14_leaf-bringup-mesh-fix.md`).
- Chưa đo throughput theo số hop (1/2/3 hop) — vẫn là tiêu chí hội đồng
  chấm còn thiếu số liệu, cần cả 3 Leaf hoạt động ổn định đồng thời (bằng
  nguồn pin độc lập, tách xa nhau) mới đo được chính xác.
- `claude_trang-thai-hien-tai.md` và `claude_migration-esp-wifi-mesh.md`
  vẫn chưa được đồng bộ với các phát hiện của buổi 14/9 lẫn buổi hôm nay.

## Việc cần làm tiếp theo (gợi ý thứ tự)

1. Hàn xong bộ nguồn pin độc lập đầu tiên theo quy trình 8 bước đã soạn,
   test với 1 Leaf trước khi làm tiếp 2 bộ còn lại.
2. Tách 3 Leaf ra xa nhau (dùng nguồn pin, không còn bị giới hạn bởi dây
   cắm PC), xác nhận `node_count: 3` ổn định lâu dài, không còn dao động
   1-2 như hôm nay.
3. Đổi lại `setDebugMsgTypes(ERROR)` ở cả `node_camera.ino` (Leaf2, Leaf3)
   và `node_root.ino`.
4. Bắt đầu đo throughput theo 1/2/3 hop và ghi lại thêm nhiều mẫu
   self-healing time trong điều kiện mạng ổn định (không còn nhiễu RF do
   đặt quá gần).
5. Theo dõi xem Root có tiếp tục tự reset bất thường không; nếu lặp lại,
   điều tra nguyên nhân cụ thể.
6. Đồng bộ lại 2 file trạng thái tổng hợp với toàn bộ nội dung từ 14/9 và
   hôm nay.
