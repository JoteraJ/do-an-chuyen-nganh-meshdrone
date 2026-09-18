# Nhật ký buổi làm việc — 17/09/2026: Test 2 Leaf đồng thời, phát hiện drop rate cao, nghiên cứu giải pháp và thiết kế test self-healing

> File này bổ sung cho `claude_2026-09-17_middleware-dashboard-route-fix.md`
> (sửa lỗi `Cannot GET /` cùng ngày) và `2026-09-16_leaf-bringup-rf-diagnosis-power-plan.md`
> (bản cập nhật gần nhất trước đó). Đọc file này để biết tiến độ mới nhất:
> đã chạy được 2 Leaf gửi ảnh liên tục đồng thời, phát hiện tỉ lệ mất khung
> hình tăng vọt so với chạy 1 Leaf, và đã có phương án kỹ thuật cụ thể để
> xử lý cho buổi sau.

## Bối cảnh đầu buổi

Sau khi xác nhận fix route `/` hoạt động (xem
`claude_2026-09-17_middleware-dashboard-route-fix.md`), tiếp tục thử kết nối
đồng thời nhiều Leaf với Root (COM7) — mục tiêu là kiểm tra hệ thống với
>1 Leaf hoạt động cùng lúc, việc chưa làm được ổn định ở buổi 16/9 do tranh
chấp RF khi đặt quá gần nhau.

## Đã hoàn thành

### 1. Xác định đúng cổng COM mới qua Device Manager

- Trong danh sách cổng, chỉ có 2 cổng USB-serial thật liên quan tới board:
  `COM7` (Silicon Labs CP210x — đúng chip của Root, đã xác nhận từ
  `2026-09-12_root-node-bringup.md`) và `COM15` (USB-SERIAL CH340 — đúng
  chip của Leaf). Các cổng "Standard Serial over Bluetooth link" (COM3-9)
  là cổng ảo của Windows, không liên quan.
- Xác nhận: mesh giữa các Leaf hoạt động hoàn toàn qua sóng WiFi, không cần
  cáp USB của Leaf phải là cáp data — chỉ cần cấp nguồn là Leaf tham gia
  được mesh và gửi ảnh. Cáp USB có data (như của COM15) chỉ cần thiết nếu
  muốn mở Serial Monitor debug trực tiếp con đó qua PC.

### 2. Test 2 Leaf + 1 Root đồng thời — ban đầu không ổn định

- Bố trí: Root ở giữa, Leaf #1 bên trái cách Root ~30cm (cắm trực tiếp cổng
  PC), Leaf #2 (COM15) bên phải cách Root ~40-50cm (cắm qua 1 hub USB nối
  vào 1 cổng PC khác).
- Lần đầu: `node_count` bắt đầu ở 2 nhưng tụt dần về 1 rồi 0 — quan sát qua
  dashboard cho thấy Media Stream vẫn hiển thị khung hình cuối cùng của mỗi
  node kèm đồng hồ đếm "X giây trước" (không tự xoá khi node im lặng), dễ
  gây hiểu nhầm là vẫn đang có dữ liệu trong khi thực tế node đã rớt hẳn.
- Nghi vấn chính được đặt ra: Leaf #2 cắm qua hub USB (có thể là hub thụ
  động) có thể không đủ dòng đỉnh khi ESP32 burst gửi ảnh — đúng loại lỗi
  đã gặp với COM14 hôm 16/9 (nguồn/cáp yếu, không phải lỗi mesh). **Chưa
  xác nhận dứt điểm** giả thuyết này trong buổi — cần test lại bằng cách
  cắm cả 2 Leaf trực tiếp vào cổng PC (không qua hub) ở buổi sau.

### 3. Đạt được trạng thái 2 Leaf gửi ảnh liên tục ổn định

- Sau đó, `node_count: 2` giữ ổn định, cả 2 node (`1207433521`, `1207449329`)
  cùng gửi ảnh liên tục, dashboard hiển thị cả 2 đều "vừa xong".
- Đính chính 1 giả thuyết đã nêu sai ở buổi trước: từng lo ngại cấu trúc
  `ImageChunkPacket` thực tế thiếu trường `node_id` (dựa theo tài liệu tham
  khảo `gemini-code-1787323211193.md`), có thể gây lẫn frame giữa 2 Leaf.
  Dữ liệu thực tế từ `/status` (`latestFrameMeta.nodeId: 1207433521`) xác
  nhận bản triển khai thật **đã có** trường phân biệt node gửi — không có
  rủi ro lẫn frame giữa các node như đã lo ngại.

### 4. Thu thập số liệu quan trọng: drop rate tăng vọt khi có 2 Leaf

So sánh với baseline chỉ 1 Leaf (Leaf #1 một mình, đo được hôm 14/9,
`2026-09-14_leaf-bringup-mesh-fix.md`: `frames_ok: 1306`, `frames_drop: 6`,
~0.5% drop), số liệu đo được hôm nay khi 2 Leaf cùng gửi liên tục:

- Tầng telemetry Root (`/status`): `frames_ok: 111`, `frames_drop: 496` →
  ~82% drop.
- Tầng ghép ảnh middleware (dashboard): Frame ghép OK = 99, Frame bị hủy =
  345 → ~78% hủy.
- Tầng UART/parser vẫn sạch: `framesBadCrc: 5` / `framesOk: 16069` (~0.03%),
  `bytesSkipped` nhỏ so với tổng — xác nhận UART/middleware không phải nơi
  gây mất dữ liệu, vấn đề nằm ở tầng sóng mesh và/hoặc khâu ghép frame.

Đây là số liệu định lượng đầu tiên cho tiêu chí throughput/packet loss theo
số node gửi đồng thời — cần lưu lại nguyên văn cho báo cáo.

### 5. Nghiên cứu và lên phương án kỹ thuật xử lý drop rate cao

Đã chạy 1 tác vụ nghiên cứu sâu (Advanced Research) để tìm giải pháp cụ thể
cho ESP32/painlessMesh, kết quả chính (xem đầy đủ trong artifact
"ESP32-CAM Multi-Node Frame Loss on painlessMesh..." tạo trong phiên chat
17/9):

- **Nghi vấn cần kiểm chứng trước tiên**: `FrameAssembler` ở middleware có
  khả năng đang dùng 1 buffer ghép dở duy nhất — nếu đúng, đây có thể là
  nguyên nhân khuếch đại chính (chỉ cần thiếu 1 chunk là huỷ cả frame, 2
  luồng đan xen dễ "chen ngang" nhau). Cần sửa thành `Map` theo khoá
  `(node_id, frame_id)` giữ nhiều buffer song song + timeout dọn rác, rồi
  đo lại drop rate để tách biến số phần mềm khỏi phần cứng/sóng.
- Nếu sau khi sửa buffer vẫn drop cao trên sóng: thử kỹ thuật **staggering**
  (2 Leaf tự lệch pha thời điểm gửi bằng `mesh.getNodeTime()`, không cần
  điều phối từ server) để giảm tranh chấp CSMA/CA khi cả 2 gửi cùng lúc.
- Nếu vẫn >40% drop sau 2 bước trên: cân nhắc chuyển hẳn luồng ảnh sang
  **ESP-NOW** (unicast, có ACK MAC + callback, bằng chứng cộng đồng đo được
  lợi airtime đáng kể so với painlessMesh cho use-case nhiều node cùng gửi)
  — giữ painlessMesh cho phần telemetry/điều khiển.
- Xác nhận: **không nên giảm `CHUNK_SIZE`** xuống dưới 1024 — theo lý
  thuyết 802.11 bão hoà, gói nhỏ hơn làm tăng số frame và có thể phản tác
  dụng với đúng loại nghẽn đang gặp (số frame/airtime, không phải độ dài
  gói).
- Thêm cơ chế NACK gộp + retransmit tối đa 1-2 vòng cho chunk thiếu (tránh
  ACK từng chunk gây bão ACK ngược).

### 6. Xác định phạm vi thực sự đo được cho self-healing với setup hiện tại

- Theo đúng ghi chú trên dashboard ("Cây hiện chỉ có 1 tầng Root → node"),
  **cả 2 Leaf hiện đang nối trực tiếp 1 hop vào Root**, không qua nhau. Với
  cấu hình này, chỉ đo được self-healing kiểu "tái kết nối cơ bản" (rejoin
  sau khi mất tín hiệu), **chưa đo được** self-healing kiểu "đổi tuyến qua
  node trung gian khác" đúng nghĩa mesh.
- Đã thiết kế quy trình 6 bước để đo self-healing cơ bản với đúng 2 Leaf
  hiện có (đánh dấu mốc t0 khi rút nguồn 1 Leaf, theo dõi `node_count`
  giảm rồi tăng lại, đọc `last_self_heal_ms` trong telemetry, lặp lại 3-5
  lần lấy min/max/trung bình) — **chưa thực hiện đo thật trong buổi này**.
- Từ kết quả nghiên cứu: để đo self-healing "đổi tuyến" đúng nghĩa học
  thuật, cần bố trí vật lý theo chuỗi Root → Leaf trung gian (Relay) → Leaf
  xa (chỉ thấy được Relay, không thấy trực tiếp Root) — cần đủ 3 node và
  Leaf #3 chưa tham gia hôm nay.
- Lưu ý kỹ thuật quan trọng từ nghiên cứu: nên dùng `mesh.getNodeTime()`
  (đồng bộ toàn mesh, đơn vị µs) để so sánh timestamp **giữa các node khác
  nhau**, còn `millis()` (đồng hồ local, đơn điệu) phù hợp hơn khi đo
  khoảng thời gian **trên cùng 1 node** để tránh nhiễu do resync giữa
  chừng.

## Vấn đề còn tồn đọng

- **Chưa xác nhận nguyên nhân chính xác** của ~80% drop khi 2 Leaf cùng
  gửi — chưa tách được biến số giữa (a) tranh chấp sóng CSMA/CA thuần tuý
  và (b) `FrameAssembler` một-buffer khuếch đại vấn đề. Cần sửa buffer
  trước rồi đo lại mới kết luận được.
- Giả thuyết "hub USB yếu dòng gây rớt Leaf #2" **chưa được xác nhận dứt
  điểm** — mới chỉ là nghi vấn dựa trên tương đồng với sự cố COM14 hôm
  16/9, chưa có phép so sánh trực tiếp (cắm không qua hub) trong buổi này.
- `node_root.ino` vẫn chưa báo cáo quan hệ cha/con đầy đủ (TODO cũ từ các
  buổi trước) — dashboard không thể xác nhận trực quan 2 Leaf có đang nối
  trực tiếp Root hay qua nhau; hiện chỉ suy luận qua khoảng cách vật lý.
- `setDebugMsgTypes(ERROR | CONNECTION)` — tình trạng hiện tại **chưa được
  xác nhận lại** trong buổi này liệu đã đổi về `ERROR` như khuyến nghị từ
  16/9 hay chưa.
- Leaf #3 vẫn chưa tham gia lần test nào — mới dừng ở 2/3 Leaf, nên chưa
  đo được kịch bản 3-node (cả throughput 3-node lẫn self-healing multi-hop
  đúng nghĩa).
- Chưa thực hiện đo self-healing thật (chỉ mới có quy trình 6 bước và
  phương án kỹ thuật đo chuẩn 3-node từ nghiên cứu, chưa chạy thử nghiệm
  thực tế).
- `claude_trang-thai-hien-tai.md` và `claude_migration-esp-wifi-mesh.md`
  vẫn chưa được đồng bộ với các phát hiện từ 16/9 lẫn hôm nay.

## Việc cần làm tiếp theo (gợi ý thứ tự)

1. Sửa `FrameAssembler` ở middleware thành `Map` theo `(node_id, frame_id)`
   + timeout dọn rác + giới hạn số buffer đồng thời. Đo lại drop rate 2-Leaf
   ngay sau khi sửa để xác nhận đây có phải nguyên nhân chính không.
2. Test lại 2 Leaf cắm trực tiếp cả 2 vào cổng PC (không qua hub) để xác
   nhận/loại trừ dứt điểm giả thuyết hub USB yếu dòng.
3. Nếu sau bước 1 drop trên sóng vẫn cao (>40%): thử staggering bằng
   `getNodeTime()`, sau đó cân nhắc chuyển luồng ảnh sang ESP-NOW.
4. Chạy đúng quy trình 6 bước đo self-healing cơ bản với 2 Leaf hiện có,
   lấy ít nhất 3-5 mẫu `last_self_heal_ms`.
5. Đưa Leaf #3 vào tham gia, bố trí theo chuỗi Root → Relay → Leaf xa để
   chuẩn bị đo self-healing "đổi tuyến" đúng nghĩa và throughput theo
   1/2/3 hop.
6. Xác nhận lại `setDebugMsgTypes` đã về `ERROR` ở cả `node_root.ino` và
   `node_camera.ino`.
7. Đồng bộ 2 file trạng thái tổng hợp với toàn bộ phát hiện từ 16/9 và
   hôm nay.
