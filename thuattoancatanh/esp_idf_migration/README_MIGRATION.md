# Migration: painlessMesh (Arduino) → ESP-WIFI-MESH (ESP-IDF)

Tài liệu này giải thích: vì sao đổi, đổi cái gì, cách cài môi trường trên
Windows, cấu trúc project mới, và những chỗ **bạn cần tự build-test** vì
mình không có ESP32 thật / ESP-IDF toolchain để compile từ xa.

Nguồn tra cứu API dùng khi viết code này:
- [ESP-WIFI-MESH Programming Guide (stable)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp-wifi-mesh.html)
- [esp-idf/examples/mesh/manual_networking/main/mesh_main.c](https://github.com/espressif/esp-idf/blob/master/examples/mesh/manual_networking/main/mesh_main.c)

## 1. Vì sao đổi

Đề bài giáo viên ghi rõ: *"Cấu hình giao thức ESP-Mesh hoặc BATMAN-adv"*.
`painlessMesh` (thư viện Arduino bên thứ 3 đang dùng) không phải là 1
trong 2 cái đó — nó tự tổ chức cây qua TCP + JSON riêng, không phải API
`esp_mesh` chính thức của Espressif. `BATMAN-adv` thì loại thẳng vì đó là
module kernel Linux, không chạy được trên ESP32 (không có kernel Linux).

=> Lựa chọn đúng: **ESP-WIFI-MESH**, dùng qua ESP-IDF (framework gốc của
Espressif, không phải Arduino).

**Lợi ích phụ (không chỉ để đúng tên đề bài):**
- ESP-WIFI-MESH gửi dữ liệu **nhị phân trực tiếp** qua `mesh_data_t`
  → bỏ hẳn được lớp Base64 (giảm ~33% dữ liệu truyền, giảm CPU encode/decode).
- `esp_mesh_send(NULL, &data, MESH_DATA_TODS, ...)` tự động gửi **unicast
  theo đường về Root** (qua đúng các node cha trung gian) — đây chính là
  điều mình từng khuyên "nên đổi `sendBroadcast` → `sendSingle`" ở bản
  painlessMesh cũ. Với ESP-WIFI-MESH, hành vi đúng là **mặc định**, không
  cần tự code lại.

## 2. Cài ESP-IDF trên Windows

Có 2 cách, chọn 1:

**Cách A — VS Code (khuyên dùng, dễ nhất):**
1. Cài [Visual Studio Code](https://code.visualstudio.com/).
2. Mở Extensions, cài extension **"Espressif IDF"**.
3. Mở Command Palette (`Ctrl+Shift+P`) → gõ `ESP-IDF: Configure ESP-IDF
   Extension` → chọn "Express" → chọn version ESP-IDF (khuyên bản
   **v5.x** ổn định mới nhất) → để tool tự tải về (mất 10-20 phút).
4. Extension tự cấu hình Python venv, toolchain compiler, driver USB.

**Cách B — ESP-IDF Tools Installer (dùng terminal/PowerShell thuần):**
1. Tải "ESP-IDF Tools Installer" cho Windows từ trang chính thức của
   Espressif (tìm "ESP-IDF Windows Installer" trên get-started guide).
2. Cài đặt sẽ tạo sẵn 1 shortcut "ESP-IDF PowerShell" trên Desktop —
   mở nó ra là có sẵn môi trường `idf.py`.

Cả 2 cách đều **không cần Linux/WSL** — ESP-IDF hỗ trợ Windows native.

## 3. Cấu trúc project mới

```
thuattoancatanh/esp_idf_migration/
├── components/
│   └── chunk_common/              ← Header dùng chung (component ESP-IDF)
│       ├── CMakeLists.txt
│       └── include/
│           ├── mesh_config.h      ← Port từ shared/mesh_config.h
│           └── chunk_protocol.h   ← Port từ shared/chunk_protocol.h (BỎ Base64)
├── node_camera/                   ← Firmware Camera Node (project ESP-IDF riêng)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── main/
│       ├── CMakeLists.txt
│       └── mesh_camera_main.c     ← Phase 1: gửi ảnh GIẢ LẬP qua mesh
├── node_root/                     ← Firmware Root Node (project ESP-IDF riêng)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── main/
│       ├── CMakeLists.txt
│       └── mesh_root_main.c       ← Phase 1: nhận + ghép ảnh, in log
└── README_MIGRATION.md            ← File này
```

Lưu ý: bản `thuattoancatanh/node_camera` và `node_root` (painlessMesh) cũ
**vẫn giữ nguyên**, không đụng tới — để có bản dự phòng nếu migrate gặp
vấn đề không kịp deadline.

## 4. Trước khi build — SỬA 2 chỗ bắt buộc

Trong `components/chunk_common/include/mesh_config.h`:
```c
#define ROUTER_SSID   "YOUR_WIFI_SSID"       // ← đổi thành WiFi router thật
#define ROUTER_PASSWORD "YOUR_WIFI_PASSWORD" // ← đổi thành mật khẩu thật
```
Root Node cần thấy được router này để tự bầu làm Root (ESP-WIFI-MESH tự
chọn node nào "nhìn thấy" router cấu hình trong `cfg.router` làm Root).

## 5. Build & nạp (chạy trong "ESP-IDF PowerShell" hoặc VS Code terminal)

```powershell
cd node_root
idf.py set-target esp32
idf.py menuconfig     # kiểm tra lại PSRAM, Partition Table đã đúng theo sdkconfig.defaults chưa
idf.py build
idf.py -p COMx flash monitor    # COMx = cổng COM của board (xem trong Device Manager)
```

Làm tương tự cho `node_camera` (nạp vào board khác). Ctrl+] để thoát
`monitor`.

## 6. Checklist debug — MÌNH CHƯA COMPILE-TEST ĐƯỢC, BẠN CẦN TỰ KIỂM TRA

Vì không có ESP32 thật + ESP-IDF toolchain trong môi trường của mình,
code đã viết dựa trên tài liệu chính thức + ví dụ mesh gốc của Espressif,
nhưng CHƯA build thử. Các điểm rủi ro cao nhất theo thứ tự:

1. **`REQUIRES` trong `main/CMakeLists.txt`.** Nếu `idf.py build` báo lỗi
   `esp_mesh.h: No such file or directory` hoặc thiếu hàm `esp_mesh_xxx`,
   nghĩa là component chứa mesh chưa được liệt kê đúng tên (tùy version
   ESP-IDF, có thể tự gộp sẵn vào `esp_wifi` hoặc cần thêm 1 dòng
   `REQUIRES` khác — thông báo lỗi sẽ chỉ rõ thiếu gì).
2. **`esp_mesh_fix_root(true)` trong `mesh_root_main.c`.** Đây là cách
   phổ biến để khóa 1 node làm Root ổn định, nhưng nên theo dõi log
   `MESH_EVENT_PARENT_CONNECTED` / `esp_mesh_is_root()` thực tế trước khi
   tin tưởng hoàn toàn — nếu Root không tự bầu đúng node bạn muốn, tham
   khảo thêm `esp_mesh_set_type(MESH_ROOT)` trong tài liệu ESP-WIFI-MESH.
3. **Giới hạn kích thước gói `esp_mesh_send()`.** Gói hiện tại tối đa
   `12 + 1024 = 1036 byte`. Tài liệu Espressif không nêu 1 con số cứng
   duy nhất cho mọi version — nếu gặp lỗi gửi (`ESP_ERR_MESH_ARGUMENT`
   hoặc tương tự) khi test, thử giảm `MAX_CHUNK_PAYLOAD` xuống 512 trong
   `mesh_config.h` (đúng thực nghiệm bạn đã định làm ở tuần 8-9 rồi).
4. **Tên hàm `esp_mesh_get_mesh_id()`** dùng trong log Phase 1 có thể
   không tồn tại ở mọi version ESP-IDF — nếu build lỗi dòng đó, xóa dòng
   log debug đó đi (không ảnh hưởng logic chính).

Đừng hoảng nếu build lỗi ngay lần đầu — đây là chuyện bình thường khi
chuyển framework. Báo lại đúng dòng lỗi `idf.py build` in ra, mình sẽ
sửa tiếp theo đúng lỗi thật thay vì đoán.

## 7. Roadmap tiếp theo

- **Phase 1 (đang làm — file trong project này):** mesh join, self-healing
  qua painlessMesh→ESP-WIFI-MESH, gửi/nhận/ghép ảnh GIẢ LẬP, chạy được
  và thấy log `[ASM] HOAN THANH!` trên Root là coi như đạt.
- **Phase 2:** thay `fill_fake_jpeg()` trong `mesh_camera_main.c` bằng
  camera OV3660 thật — dùng component `espressif/esp32-camera` (cài qua
  `idf.py add-dependency "espressif/esp32-camera"`), code `esp_camera_fb_get()`
  gần như giữ nguyên logic so với bản Arduino cũ (cùng driver gốc).
- **Phase 3:** thêm `esp_http_server` (component có sẵn trong ESP-IDF)
  vào `node_root` để phát MJPEG, thay cho `WiFiServer` thô của bản cũ.
- **Phase 4:** thêm timestamp vào `chunk_header_t` (đo latency) và đăng
  ký `MESH_EVENT_PARENT_DISCONNECTED` với timestamp (đo self-healing) —
  đây là 2 điểm còn thiếu đã nói ở lần review trước, migrate sang
  ESP-IDF không tự động giải quyết được, vẫn cần bạn tự thêm.
