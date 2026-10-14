# Nhật ký đo đạc — 09/10/2026: Topology 3 hop và self-healing (test22)

Oct 9, 2026 · @Danny

## 1. Thông tin chung

Buổi đo ngày 09/10/2026 lần đầu dựng được chuỗi 3 hop có kiểm soát (Root → hop 1 → hop 2 → hop 3) và duy trì ổn định khoảng 16 phút, đồng thời ghi nhận 5 sự kiện self-healing theo cách đo mới.

| Mục | Giá trị |
| --- | --- |
| Mục tiêu | Dựng topology nhiều hop, đo latency, throughput, drop theo số hop; ghi nhận self-healing |
| File dữ liệu | test22.csv (xuất từ dashboard, chu kỳ lấy mẫu 2 s, 602 mẫu) |
| Thời gian | 13:52:23 → 14:12:43 UTC (20:52 → 21:12 giờ Việt Nam), khoảng 20 phút |
| Root | ESP32-WROOM-32D, FW 2026-10-06a (UART TX 16 KB) |
| Leaf | 3 ESP32-CAM, FW 2026-10-01b, bật stagger |
| Node ID | 1207449329, 1207433521, 1207369673 |

Thời gian trong các bảng dưới đây ghi theo UTC để khớp với file CSV.

## 2. Diễn biến topology

Mạng đi từ 1 Leaf (hop 1) lên 2 Leaf, chuyển sang 2 hop lúc 13:55:19, rồi đạt chuỗi 3 hop từ 13:55:47 đến 14:12:19 (có một lần gián đoạn 12 s).

| Thời điểm (UTC) | Số Leaf | hop\_max | Diễn biến |
| --- | --- | --- | --- |
| 13:52:23 | 1 | 1 | Chỉ node 449329, nối trực tiếp Root |
| 13:52:37 | 2 | 1 | Node 433521 gia nhập, cả hai ở hop 1 |
| 13:53:37 → 13:55:01 | 2 | 1 | Node 433521 nhiều lần mất rồi xuất hiện lại trong danh sách |
| 13:55:19 | 2 | 2 | Node 433521 chuyển sang làm con của 449329 (hop 2) |
| 13:55:31 → 13:55:45 | — | — | Không có dữ liệu topology, cây đang dựng lại |
| 13:55:47 | 3 | 3 | Chuỗi 449329 (hop 1) → 433521 (hop 2) → 369673 (hop 3) |
| 14:08:45 | 3 | 2 | Node 369673 (hop 3) vắng mặt |
| 14:08:57 | 3 | 3 | Node 369673 nối lại ở hop 3 |
| 14:12:19 | 3 | 2 | Node 369673 vắng mặt |
| 14:12:23 → 14:12:43 | 3 | 1 | Chỉ còn node 449329, kết thúc phiên đo |

Tại 13:55:19, cột hop\_by\_node ghi node 433521 hai lần (hop 2 và hop 1) trong lúc cây đang chuyển; cần lọc trùng khi xử lý số liệu.

## 3. Kết quả ở cấu hình 3 hop

Trong 490 mẫu 3 hop (khoảng 16 phút), mỗi hop cộng thêm khoảng 200 ms latency, còn fps và drop toàn mạng giữ ổn định.

### 3.1. Latency và RSSI theo hop

| Hop | Node | Số mẫu | Latency trung vị (ms) | Latency trung bình (ms) | Độ lệch chuẩn (ms) | RSSI trung bình (dBm) | RSSI min / max (dBm) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 1207449329 | 485 | 148,3 | 202,1 | 235,6 | −45,8 | −51 / −42 |
| 2 | 1207433521 | 481 | 350,4 | 473,2 | 453,7 | −54,5 | −60 / −50 |
| 3 | 1207369673 | 384 | 575,5 | 685,5 | 440,4 | −67,3 | −72 / −61 |

Xu hướng tăng latency theo hop khớp với test12 cũ (hop 1 khoảng 96 ms, hop 2 khoảng 228 ms). Cả ba liên kết đều nằm trong vùng mạnh −55 đến −70 dBm.

### 3.2. Throughput, drop và tài nguyên Root

| Chỉ số | Trung bình | Trung vị | Min | Max |
| --- | --- | --- | --- | --- |
| FPS tổng | 4,94 | 5,0 | 0 | 9,5 |
| Thông lượng (KB/s) | 20,6 | 20,2 | 0 | 41,0 |
| Drop (%) | 0,56 | 0 | 0 | 100 |
| Latency toàn mạng (ms) | 430 | 344 | 160 | 4 334 |
| Heap DEFAULT Root (KB) | 75,9 | 76,1 | 65,2 | 76,7 |
| loopMaxMs (ms) | 7,6 | 6 | 4 | 70 |

– uartDrop bằng 0 trong toàn bộ thời gian 3 hop. – lateChunkDrop (bộ đếm cộng dồn) tăng từ 7 lên 36. – Khoảng 6,5 % mẫu có latency vượt 1 s; p95 khoảng 1,0–1,1 s. – Cột kbps trong CSV thực chất là KB/s.

So với test21 (3 Leaf đều ở hop 1: 5,0 fps, 25,6 KB/s, latency 199 ms), fps gần như không đổi. Lý do là tải bị giới hạn bởi chu kỳ chụp của Leaf, chưa ép mạng tới bão hòa, nên test22 chưa đo được mức suy giảm thông lượng theo số hop.

## 4. Kết quả self-healing

Ghi nhận 5 sự kiện theo cách đo mới (heal\_ms\_moi), dao động từ 1 530 đến 23 225 ms, cùng 1 sự kiện theo cách đo cũ.

| Thời điểm (UTC) | Số Leaf | Node | heal\_ms\_moi | heal\_ms\_cu | start\_src | Bối cảnh trên CSV |
| --- | --- | --- | --- | --- | --- | --- |
| 13:54:25 | 2 | 1207433521 | 11 721 |  | last\_chunk | Mất rồi nối lại ở hop 1 |
| 13:55:19 | 2 | 1207433521 | 23 225 |  | last\_chunk | Chuyển từ hop 1 sang hop 2 (đổi tuyến) |
| 13:55:45 | 3 | — |  | 7 326 |  | Node thứ 3 gia nhập |
| 13:55:47 | 3 | 1207449329 | 9 420 |  | list | Cây dựng lại thành chuỗi 3 hop |
| 13:55:47 | 3 | 1207433521 | 9 489 |  | list | Cây dựng lại thành chuỗi 3 hop |
| 14:08:55 | 3 | 1207369673 | 1 530 |  | list | Node hop 3 vắng khoảng 12 s rồi nối lại |

Tổng hợp theo số Leaf (heal\_ms\_moi): với 2 Leaf, n = 2, trung bình 17 473 ms (độ lệch chuẩn 8 135 ms); với 3 Leaf, n = 3, trung bình 6 813 ms (độ lệch chuẩn 4 575 ms).

– Khi node thứ 3 gia nhập, toàn mạng không có dữ liệu khoảng 16 s (13:55:31–47). – Giá trị 1 530 ms lúc 14:08:55 thấp hơn thời gian gián đoạn thực tế (khoảng 12 s trên CSV). Với start\_src = list, phép đo bắt đầu từ lúc node bị gỡ khỏi danh sách, nên bỏ qua thời gian chờ phát hiện mất kết nối. – Sau 14:12:19, node hop 3 rồi node hop 2 lần lượt mất; phiên đo kết thúc lúc 14:12:43 nên không có dữ liệu nối lại.

Câu hỏi còn mở: thao tác thực tế tại các mốc 13:54, 13:55, 14:08 và 14:12 (rút nguồn, di chuyển node hay tắt Relay) chưa được ghi lại, cần bổ sung để phân loại kịch bản.

## 5. Đánh giá độ ổn định và hạn chế

Hệ thống vận hành ổn định ở trạng thái tĩnh với topology 3 hop, nhưng chưa ổn định mỗi khi topology thay đổi.

Chia 16 phút 3 hop thành 4 đoạn bằng nhau, các chỉ số không trôi theo thời gian:

| Đoạn (bắt đầu, UTC) | Heap TB (KB) | Heap min (KB) | Latency trung vị (ms) | Latency p95 (ms) | FPS TB | Drop TB (%) | lateChunkDrop | loopMaxMs max (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 13:55:47 | 76,0 | 57,0 | 361 | 1 127 | 5,0 | 0,2 | 10 | 70 |
| 13:59:53 | 75,8 | 63,2 | 364 | 1 022 | 5,0 | 0,3 | 17 | 53 |
| 14:03:57 | 75,9 | 60,5 | 346 | 1 112 | 4,9 | 1,0 | 26 | 58 |
| 14:08:01 | 76,0 | 62,6 | 281 | 1 062 | 4,9 | 0,8 | 36 | 35 |

Hạn chế ghi nhận:

– Thời gian phục hồi 9–23 s, chậm hơn mốc tham chiếu của ESP-WIFI-MESH (dưới 5 s với node con, dưới 10 s với root). – Node mới gia nhập làm toàn mạng gián đoạn khoảng 16 s. – Latency có đuôi dài: khoảng 6,5 % mẫu vượt 1 s, cao nhất 4,3 s. – lateChunkDrop tăng đều; chưa có cơ chế NACK gửi lại chunk. – Phiên đo chỉ khoảng 20 phút, tải chưa bão hòa, chưa chạy bằng pin. – Số mẫu self-healing ít và lẫn nhiều loại sự kiện.

## 6. Việc cần làm tiếp theo

- [ ] Ghi bổ sung thao tác thực tế tại các mốc 13:54, 13:55, 14:08 và 14:12 để phân loại sự kiện self-healing
- [ ] Chụp màn hình cây topology 3 hop trên dashboard để đưa vào báo cáo
- [ ] Đo self-healing ít nhất 5 lần cho mỗi kịch bản, tách riêng "đổi tuyến qua relay" và "nối lại sau mất tín hiệu"
- [ ] Chạy dài 3 hop khoảng 60 phút, không tác động thiết bị
- [ ] Đo thông lượng bão hòa ở 1, 2 và 3 hop (giảm CAPTURE\_INTERVAL\_MS)
- [ ] Chạy 3 hop bằng pin, đo dòng Leaf
- [ ] Lọc trùng node trong hop\_by\_node khi cây đang chuyển
