# Nhật ký buổi làm việc — 12/09/2026: Bring-up board Root

> File này bổ sung cho `claude/trang-thai-hien-tai.md` (bản cập nhật gần nhất
> trước đó là 2026-09-10). Đọc file này để biết tiến độ mới nhất về việc đưa
> board Root vật lý vào hoạt động thật, sau khi hàng đã về tay.

## Bối cảnh đầu buổi

Board Root (ESP32-32D DevKitC TypeC, chip CP2102) đã về tay, nhưng **chưa
từng cắm điện lần nào, chưa cài driver, chưa build-test bất kỳ firmware nào**
— cả bản painlessMesh lẫn bản ESP-IDF đều mới chỉ nằm trên code, chưa chạm
vào phần cứng thật.

## Đã hoàn thành trong buổi này

### 1. Xác nhận danh tính phần cứng board Root
- Board thực tế: **ESP32-32D DevKitC TypeC** (khác tên gọi "Wroom-32D
  DevKitC" ghi trong ghi chú BOM trước đó, nhưng cùng lõi WROOM-32, không
  ảnh hưởng gì tới code).
- Chip USB-Serial: **CP2102** (xác nhận qua thông số sản phẩm của shop).
- **Board này KHÔNG có PSRAM vật lý** — phát hiện quan trọng, ảnh hưởng
  trực tiếp tới firmware Root (xem mục 4).

### 2. Cài driver + toolchain trên PC (từ số 0)
- Driver CP210x (Silicon Labs) không tự nhận — phải cài thủ công qua
  Device Manager (Update driver > Browse my computer > trỏ vào thư mục
  driver đã giải nén, tick "Include subfolders"). Nguyên nhân ban đầu còn
  có thêm lỗi "Device Descriptor Request Failed" do cáp USB không phải
  loại truyền dữ liệu — đổi cáp mới giải quyết được bước này trước khi
  cài driver.
- Kết quả: board hiện ra đúng **"Silicon Labs CP210x USB to UART Bridge
  (COM7)"** trong Device Manager.
- Cài mới hoàn toàn **Arduino IDE 2.3.10** (máy chưa từng có) + gói board
  **esp32 by Espressif Systems v3.3.11** qua Boards Manager.

### 3. Xác nhận toàn bộ chuỗi phần cứng + toolchain hoạt động
- Nạp thành công code mẫu **Blink** (sau khi sửa `LED_BUILTIN` không được
  định nghĩa sẵn trên board này → dùng số chân GPIO trực tiếp).
- Xác nhận: USB → cáp → driver CP2102 → Arduino IDE → board đều thông
  suốt, không còn nghi ngờ gì về phần cứng cơ bản.

### 4. Phát hiện và sửa lỗi PSRAM trong firmware `node_root.ino`
- Nạp thử bản chính thức `node_root.ino` (painlessMesh) → board **treo
  vĩnh viễn ngay từ `setup()`**, log lặp lại:
  `[SYS] LOI NGHIEM TRONG: Khong co PSRAM!`
- Nguyên nhân gốc: code gốc thiết kế cho board **có PSRAM** (ESP32-CAM,
  ESP32-WROVER...) — cấp phát ~384KB buffer ghép ảnh (`ps_malloc()`, 3 chỗ
  trong `init_memory()`) bắt buộc nằm trong PSRAM, cộng thêm 1 khối kiểm
  tra chặn cứng (`while(true)`) ở đầu `setup()` nếu không thấy PSRAM.
  Board Root thực tế đã mua **không có PSRAM** — sai lệch giữa code và
  BOM đã chốt (BOM vốn chọn board này *không* cần PSRAM vì Root không xử
  lý camera).
- **Đã sửa xong** (qua Antigravity, theo prompt chi tiết đã soạn): đổi
  cấp phát bộ nhớ từ PSRAM bắt buộc sang **SRAM nội bộ có fallback**, bỏ
  khối chặn cứng, giảm kích thước buffer để vừa heap thực tế.
- **Kết quả xác nhận qua Serial Monitor (921600 baud) sau khi sửa:**
  - Không còn treo ở PSRAM check.
  - Mesh khởi tạo thành công, telemetry JSON gửi đều mỗi 2 giây, đúng
    format `UART_PROTOCOL.md` §4.2:
    ```json
    {"ts":16000,"role":"root","layer":0,"frames_ok":0,"frames_drop":0,
     "chunks":0,"heap_free":122636,"psram_free":0,"node_count":0,
     "node_ids":[],"last_self_heal_ms":-1}
    ```
  - `heap_free: 122636` (~120KB) ổn định, không giảm dần → không có dấu
    hiệu memory leak ở bước test ngắn này.

## Vấn đề còn tồn đọng, chưa xử lý

- **`STATION_SSID`/`STATION_PASSWORD` trong `mesh_config.h` vẫn để giá
  trị mẫu `"YOUR_WIFI_SSID"`** — log liên tục báo
  `stationScan(): YOUR_WIFI_SSID` / `scan could not start`. Đây chỉ là
  thiếu cấu hình (chưa điền WiFi thật), không phải lỗi code. Không ảnh
  hưởng tới mesh hay UART, chỉ ảnh hưởng tính năng xem HTTP MJPEG qua
  WiFi router — chưa cấp thiết phải sửa ngay.
- **CHƯA chạy thử middleware Node.js** (`node server.js COM7`) để xác
  nhận middleware parse đúng khung UART thật từ board — đây là bước tiếp
  theo cần làm để khép kín vòng xác nhận Root ↔ middleware ↔ dashboard.
- Node_root.ino bản đã sửa **chưa được đồng bộ lại vào tài liệu chính**
  (`claude/migration-esp-wifi-mesh.md`, `claude/trang-thai-hien-tai.md`)
  — 2 file đó vẫn mô tả trạng thái cũ (chưa build-test, giả định có
  PSRAM). Cần cập nhật để tránh nhầm lẫn ở phiên làm việc sau.
- Bản ESP-IDF (`esp_idf_migration/`) **vẫn đang dở dang từ phiên trước**
  — dừng ở bước `idf.py build` lần đầu, chưa quay lại debug tiếp trong
  buổi này (ưu tiên bản painlessMesh trước vì rủi ro thấp hơn).
- **Vẫn chỉ có 1 mình board Root** — chưa có Leaf/Camera node nào để test
  luồng ghép ảnh thật, đo throughput/latency/self-healing theo đúng 3
  tiêu chí hội đồng chấm. Buffer ghép ảnh trong code (dù đã sửa PSRAM)
  vẫn hoàn toàn chưa được exercise bằng dữ liệu thật.

## Việc cần làm tiếp theo (gợi ý thứ tự)

1. Chạy `node server.js COM7` trong `thuattoancatanh/middleware/`, xác
   nhận middleware nhận và parse đúng CRC/JSON từ board thật.
2. Cập nhật lại `claude/trang-thai-hien-tai.md` với phát hiện PSRAM +
   cách đã sửa, để không lặp lại nhầm lẫn này khi nạp cho Leaf/Camera
   node sau này (Leaf **cần** camera nên bắt buộc phải là board có
   PSRAM thật như ESP32-CAM — khác với Root).
3. Điền `STATION_SSID`/`STATION_PASSWORD` thật vào `mesh_config.h` nếu
   muốn test luôn phần HTTP MJPEG server.
4. Khi 3 board ESP32-CAM (Leaf) về tay — lặp lại quy trình bring-up
   tương tự (đã có kinh nghiệm từ buổi này: cáp data, driver CP210x,
   kiểm tra PSRAM — nhưng ESP32-CAM vốn có PSRAM sẵn nên khả năng cao
   không gặp lại vấn đề này).
