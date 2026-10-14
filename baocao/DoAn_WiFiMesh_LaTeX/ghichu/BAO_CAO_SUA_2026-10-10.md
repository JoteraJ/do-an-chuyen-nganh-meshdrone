# Báo cáo sửa báo cáo đồ án – 10/10/2026

Nhánh: `sua-bao-cao-2026-10-10` (tạo từ `main` @ `cc82433`). Căn cứ: `ghichu/ghi-chu-sua-bao-cao_2026-10-10.md` (GHICHU). Kế hoạch và khảo sát ban đầu: `ghichu/KE_HOACH_SUA.md`.

## 1. Tóm tắt kết quả

| Hạng mục | Kết quả |
|---|---|
| Biên dịch | `latexmk -xelatex main.tex` (TeX Live 2023, Linux): 0 lỗi, 0 cảnh báo `Missing character`, 0 `Overfull`, không còn "??" |
| Mất ký tự tiếng Việt | Đã sửa (175 cảnh báo trước khi sửa → 0) |
| Số trang | PDF 62 trang; nội dung chính (Chương 1–5) 43 trang, nằm trong khoảng 30–80 |
| Chương 4 | `git diff --stat main -- chapters/chuong4.tex figures/chart_fps.png` trả về **rỗng** |
| Commit `070a1b0` (nhánh cũ) | Đã có trong `main` qua PR #3, không cần cherry-pick |

Các commit trên nhánh (cũ → mới): khảo sát; trang đầu; định dạng chung; chương 2; chương 1 và 3; chương 5; rà toàn văn; tài liệu tham khảo và viết tắt; **commit riêng** đổi kiểu trích dẫn sang APA; bố cục trang; thu gọn Hình 3.5 và cập nhật `main.pdf`.

## 2. Ràng buộc chương 4

- File chương 4: `chapters/chuong4.tex` (xác định chắc chắn, chứa trọn `\chapter{KẾT QUẢ THỰC NGHIỆM VÀ ĐÁNH GIÁ}`). Hình riêng: `figures/chart_fps.png`. Không có CSV trong repo.
- Không đổi ký tự nào trong file chương 4, không đổi nhãn `chap:4`, `tab:4.1`–`tab:4.3`, `fig:4.1`. Chương 4 không có lệnh `\cite`.
- Chương 4 không ghi cứng số hình/bảng/mục của chương khác; chỉ ghi "Chương 4", "Chương 5" (số chương giữ nguyên).
- Thay đổi toàn cục có ảnh hưởng tới cách hiển thị chương 4 (được phép):
  - `\cansua` (cauhinh.tex:94–96) chuyển từ `\hl` của soul sang ulem. Chữ trong 2 placeholder của chương 4 không đổi, vẫn tô vàng, nhưng nay **hiện đủ dấu** (trước đây bị rơi ký tự có dấu).
  - Font chính Times New Roman/TeX Gyre Termes, font toán TeX Gyre Termes Math (ký hiệu `±`, `≈`, `→` trong chương 4 đổi kiểu chữ).
  - `\Needspace{6\baselineskip}` trước `\section`/`\subsection`, `\topfraction` 0.9: trang của chương 4 xê dịch (chương 4 nay ở trang 34–39).
  - Macro `\hinh` không đổi.

## 3. Bảng đối chiếu từng mục GHICHU

Vị trí ghi theo `file:dòng` ở trạng thái cuối của nhánh.

### Phần A – Danh sách việc

| Mục GHICHU | Trạng thái | Vị trí / ghi chú |
|---|---|---|
| Nguyên tắc "tiểu mục đi liền một trang" | Đã sửa | cauhinh.tex:139–144 (`\Needspace`), cauhinh.tex:123; kiểm bằng `pdftotext` từng trang: không trang nào có dòng cuối là tiêu đề mục. Xem mục 5 về khoảng trống còn lại |
| Phần đầu: xóa highlight MSSV, lớp | Đã sửa | thongtin.tex:6–7. `main` còn để lại dấu xung đột merge `<<<<<<<` ở hai dòng này (làm hỏng biên dịch); đã gỡ, giữ bản không tô vàng. Bỏ `\cansua` ở dòng ngày ký thongtin.tex:24 |
| Trang 4: xóa "giảng viên xác nhận danh sách được phê duyệt" | Đã sửa | Xóa trang xác nhận Chủ tịch HĐ/Trưởng khoa (`frontmatter/xacnhan.tex`, đúng như B7 diễn giải). Đồng thời xóa câu tô vàng "Cần xác nhận với giảng viên hướng dẫn rằng danh sách nhiệm vụ này đã được duyệt" ở mục 1.5 (trang 4 của phần nội dung cũ), vì câu này khớp nguyên văn với ghi chú |
| Ch1 – 1.6 tách trang | Đã sửa | chuong1.tex:84–89; 1.6 viết thành văn xuôi, nằm trọn trang 5 |
| Ch2 – 2.2.3 trích dẫn survey | Đã sửa | chuong2.tex:47–93: dùng Bekmezci và cộng sự (2013) làm nguồn chính, Akyildiz và cộng sự (2005) cho phần mesh. Hai ứng viên còn lại (position-based routing FANETs; Routing strategies 2024) không thêm vì không có đủ thông tin xác minh |
| Ch2 – 2.4 FreeRTOS | Đã sửa | chuong2.tex:117–175 (2.4.1–2.4.5), có trích dẫn ESP-IDF và Arduino-ESP32; nhắc `loop_max_ms`, TWDT, `mesh.update()` (không kèm số đo) |
| Ch2 – 2.6 công thức | Đã sửa | chuong2.tex:192–241. 2.6 dài khoảng 1,5 trang ở giãn dòng 1.5 nên không gói được trong một trang; đã cho 2.6 bắt đầu ở đầu trang 18 và chạy liền sang trang 19 cùng 2.7 |
| Ch3 – 3.4 và Hình 3.5 | Đã xử lý theo tinh thần ghi chú | Hình 3.5 thực chất thuộc mục 3.3.3 (lưu đồ Root), không thuộc 3.4. Đã đặt Hình 3.5 ngay sau đoạn đầu của 3.3.3 (chuong3.tex:159–169, trang 27–28); tiêu đề 3.4 nằm đầu trang 30 cùng nội dung |
| Ch3 – 3.4.1 và Bảng 3.5 | Đã sửa | chuong3.tex:220–246: 3.4, 3.4.1, Bảng 3.5 và đoạn sau bảng cùng trang 30. 3.4 có 3 tiểu mục (3.4.1–3.4.3) |
| Ch4 – Bảng 4.1, cập nhật test mới, kết luận | Bỏ qua vì thuộc chương 4 | Xem mục 4 |
| Sửa lỗi mất ký tự tiếng Việt | Đã sửa | cauhinh.tex:94–96. Nguyên nhân: `\hl` (soul) dưới XeLaTeX |
| Xóa placeholder "cần bổ sung" | Đã sửa ngoài chương 4 | Tóm tắt, abstract, chương 1, chương 2 (2 chỗ), phụ lục. Chương 4 còn 2 placeholder, không sửa |

### Phần B – Chuẩn trình bày

| Mục | Trạng thái | Vị trí / ghi chú |
|---|---|---|
| B1 tiêu đề + vài dòng đầu đi cùng nhau | Đã sửa | cauhinh.tex:139–144 |
| B1 hình/bảng cùng trang với đoạn dẫn | Đã sửa | Hình 3.4/3.5 đặt sau đoạn đầu 3.3.2/3.3.3; Hình 3.6/3.7 cùng trang 3.4.2/3.4.3 (trước đây bị đẩy ra sau mục Kết luận chương 3). Hình 3.1, 3.2 và Bảng 3.1, 3.2 nằm ở trang ngay sau đoạn dẫn vì quá lớn để chung trang |
| B1 ưu tiên `[!htbp]`, không `[H]` | Đã sửa | Mọi hình/bảng dùng `[!htbp]` |
| B2 Times New Roman 13, giãn dòng 1.5 | Đã sửa | cauhinh.tex:6, 14–36, 40 |
| B2 lề 3.5/3/3.5/2 cm | Đã có sẵn | cauhinh.tex:7 |
| B2 số trang giữa, phía trên | Đã có sẵn | Phần đầu: số La Mã giữa đầu trang; nội dung: "Trang N" giữa đầu trang |
| B2 thụt dòng đầu đoạn | Đã có sẵn | `indentfirst`, `\parindent` 1,27 cm |
| B2 nội dung chính 30–80 trang | Đạt | 43 trang |
| B2 tối đa 3 cấp, mỗi nhóm ≥ 2 tiểu mục | Đạt | Mọi nhóm tiểu mục ở chương 1–3, 5 có ≥ 2 tiểu mục; 2.4 tách thành 2.4.1–2.4.5 |
| B3 đánh số theo chương | Đã có sẵn | cauhinh.tex (`\thefigure`, `\thetable`, `\theequation`) |
| B3 tên bảng trên, tên hình dưới | Đạt | Chương 1–3, 5 đã đúng; kiểm lại sau khi viết |
| B3 nhắc hình/bảng bằng số | Đạt | Mọi hình/bảng ở chương 1–3 được nhắc bằng `\ref` và diễn giải bằng lời |
| B3 hình lấy từ nguồn khác ghi nguồn | Đã sửa | Bảng 2.1 ghi "Nguồn: tổng hợp theo Bảng 1 của Bekmezci và cộng sự (2013)" dưới tên bảng (chuong2.tex:61) |
| B3 chữ trong sơ đồ khối/lưu đồ cùng font, cỡ | Đã sửa | Vẽ lại Hình 2.1, 3.1–3.6 bằng TikZ, chữ cỡ 13 cùng font (figures/tikz/). Hình PNG cũ h1–h6 có tiêu đề "HÌNH x —" nhúng trong ảnh, ghi chú lỗi thời và số liệu đo không có ở chương 4 (vd. framesOk = 16069 ở h6, "chưa đo được throughput theo số hop" ở h1/h2). File PNG cũ vẫn để trong `figures/` nhưng không còn dùng |
| B3 ảnh mô hình thật | Không áp dụng | Báo cáo không có ảnh chụp mô hình thật |
| B3 lưu đồ đúng ký hiệu | Đã sửa | Hình 3.4–3.6: khối bắt đầu/kết thúc bo tròn, xử lý chữ nhật, quyết định hình thoi, vào/ra hình bình hành |
| B3 áp dụng cho 2.6 | Đã sửa | Đánh số (2.1)–(2.5) sát lề phải; ký hiệu giải thích kèm đơn vị ngay sau từng công thức (môi trường `kyhieu`, cauhinh.tex:262); chỉ số dưới dạng `t_{ghép}`; không dùng ảnh |
| B4 không gạch đầu dòng | Đã sửa | Không còn `itemize`/`enumerate` ở chương 1–3, 5 và trang đầu |
| B4 mỗi đoạn ≥ 3 câu | Đã sửa | Kiểm bằng script đếm câu; các đoạn dẫn công thức 2.6 nay có 3 câu |
| B4 viết tắt đúng chuẩn | Đã sửa | WiFi, JPEG, UART, FreeRTOS, ESP32-CAM… |
| B5 chương 1 diễn giải yêu cầu, không chép phiếu nhiệm vụ | Đã sửa | Bảng 1.1 đổi thành "Diễn giải các nhóm nhiệm vụ" với chức năng, thông số, giới hạn (chuong1.tex:33–59) |
| B5 tên đề tài thống nhất | Đạt | Bìa, phụ bìa, tóm tắt, lịch trình đều lấy từ macro trong thongtin.tex. Chưa đối chiếu được với phiếu nhiệm vụ gốc (repo không có scan) |
| B5 chương thiết kế từ tổng quát xuống từng khối | Đã sửa | Chương 3: sơ đồ ba lớp → phần cứng (Bảng 3.1 lên đầu 3.2, linh kiện giới thiệu cùng khối, 3.2.3 chỉ còn là bảng tổng hợp) → firmware → phần mềm |
| B5 chương 4 | Bỏ qua vì thuộc chương 4 | |
| B5 chương 5 ngắn gọn, không bàn luận thêm | Đã sửa | chuong5.tex |
| B6 danh mục viết tắt đầy đủ, ABC | Đã sửa | frontmatter/viettat.tex: 62 mục, gồm danh sách C0 và chữ viết tắt quét được ở cả chương 4 (UTC, ID, CSV…) |
| B6 viết đầy đủ ở lần dùng đầu | Đã sửa | Chương 1–3, 5 |
| B6 trích dẫn APA | Đã sửa (commit riêng `e421bc5`) | main.tex dùng `apalike-vn.bst`, natbib `round,authoryear`. Chương 4 không có `\cite` nên không bị ảnh hưởng |
| B6 chia Tiếng Việt / Tiếng nước ngoài, ABC | Đã sửa một phần | Toàn bộ 16 tài liệu là tiếng nước ngoài nên chỉ có mục "Tiếng nước ngoài" (cauhinh.tex:210–213), sắp ABC theo họ tác giả |
| B6 không link cửa hàng/link trần | Đạt | Không có link cửa hàng; đường dẫn đi kèm tác giả, tên, ngày truy cập |
| B6 nên có giáo trình môn học | Chưa làm được | Không có thông tin giáo trình trong repo/GHICHU; không tự thêm |
| B7 thứ tự trang đầu | Đã sửa | main.tex:18–36 |
| B7 phiếu nhiệm vụ, lịch trình đóng chung | Đã sửa một phần | Tự chèn scan nếu có `figures/nhiemvu.pdf`, `figures/lichtrinh.pdf` (frontmatter/nhiemvu.tex, lichtrinh.tex). Repo chưa có scan → trang nhiệm vụ chỉ có tiêu đề; lịch trình giữ bảng gõ sẵn |
| B7 trang xác nhận Hội đồng | Đã xóa | |
| B7 xóa hướng dẫn tô vàng, chú thích mẫu | Đã sửa | Không còn "(Bold, size …)"; bỏ câu "(Trang này dùng để đính kèm …)"; README cập nhật |
| B7 phụ lục đánh số riêng | Đã có sẵn | Phụ lục A, trang A-1 |
| B8 cập nhật mục lục, danh mục hình/bảng | Đạt | Danh mục hình có Hình 2.1, 3.1–3.7, 4.1; danh mục bảng có Bảng 1.1, 2.1, 3.1–3.5, 4.1–4.3 |
| B8 không mất ký tự tiếng Việt | Đạt | |
| B8 lật từng trang | Đã làm bằng script + ảnh trang | Xem mục 5 |

### Phần C – FANET, FreeRTOS

| Mục | Trạng thái | Vị trí / ghi chú |
|---|---|---|
| C0 viết lại FANET theo C1 | Đã sửa | chuong2.tex:47–93 |
| C0 viết lại 2.4 theo C2 | Đã sửa | chuong2.tex:117–175 |
| C0 bảng MANET/VANET/FANET | Đã sửa | Bảng 2.1 (chuong2.tex:58–85) |
| C0 hình phân bổ tác vụ 2 lõi | Đã sửa | Hình 2.1 (figures/tikz/hinh2_1.tex) |
| C0 văn xuôi | Đã sửa | |
| C0 tài liệu tham khảo, viết tắt (UAV, FANET, MANET, VANET, RTOS, SMP, TWDT, IWDT, GPSR, OLSR, AODV, DSR) | Đã sửa | references.bib, frontmatter/viettat.tex |
| C1.1–C1.5 | Đã sửa | Định nghĩa, hai điều kiện, lý do; chọn 3 kịch bản (mở rộng vùng hoạt động, tăng độ tin cậy, bầy UAV); bảng và các ý sau bảng (30–460 km/h, convergecast); 5 yêu cầu thiết kế đối chiếu với đồ án; định tuyến (OLSR, TBRPF, DSR, AODV, GPSR, phân cụm). Ý "latency 199 ms ở test21" ở C1.4 **không đưa vào** vì chương 4 không có số này; chỉ nêu độ trễ là chỉ số được giám sát |
| C1.6 không khẳng định "là một FANET" | Đã sửa | chuong2.tex:93: "xây dựng và thử nghiệm một mô hình truyền thông đa chặng theo hướng FANET"; ý tô-pô 3D dùng ở hướng phát triển (chuong5.tex:46) |
| C2.1–C2.4 | Đã sửa | 2.4.1–2.4.4 |
| C2.5 loopTask gắn core 1 | Đã sửa, **đã kiểm chứng** | Đọc `cores/esp32/main.cpp`, `Kconfig.projbuild` và `tools/sdk/esp32/sdkconfig` của arduino-esp32 tag 2.0.17 trên GitHub: `xTaskCreateUniversal(loopTask, …, 1, …, ARDUINO_RUNNING_CORE)`, mặc định `CONFIG_ARDUINO_RUNNING_CORE=1`; tác vụ WiFi và lwIP gắn core 0. Kiểm thêm: `loopTaskWDTEnabled = false` mặc định và sdkconfig 2.0.17 chỉ bật `TASK_WDT_CHECK_IDLE_TASK_CPU0`, nên đã viết dè dặt ở 2.4.4 rằng TWDT mặc định không theo dõi loop() trên core 1 |
| C2.5 ví dụ 47 KB/s, 92 KB/s | Đã sửa | chuong2.tex:172; các số này có ở chương 4, cùng điều kiện (3 Leaf, chu kỳ 300 ms) |
| C2.5 kết quả 16 KB, uartDrop 0, 280 → 199 ms | **Chỉ trình bày cơ chế, không kèm số** | chuong2.tex:174: chương 4 không có số liệu test19/test21 nên 2.4.5 chỉ mô tả cơ chế ghi UART không chặn |
| C2.5 hướng phát triển: tách task ghi UART | Đã sửa | chuong5.tex:34 |
| C3 tài liệu bổ sung | Đã sửa | Bekmezci 2013 (DOI), ESP-IDF FreeRTOS (IDF), Watchdogs, Arduino Release v2.0.17 (2024), Akyildiz 2005 |
| C3 sách Barry | Không thêm | Không xác minh được năm và nhà xuất bản (repo FreeRTOS-Kernel-Book trên GitHub không ghi) |
| C3 bài khảo sát FANET trên PMC | Không thêm | Chưa xác minh tác giả, năm |

### Phần D – Nguyên tắc viết

| Mục | Trạng thái | Câu thật đã sửa (ví dụ) |
|---|---|---|
| D1 nhận định kèm căn cứ | Đã áp dụng | Chương 2 cũ: "Các nghiên cứu mô phỏng thường cho thấy OLSR có thông lượng và độ trễ tốt hơn AODV…" (không trích dẫn) → thay bằng phân tích cơ chế đánh đổi (chuong2.tex:37). Chương 5 cũ: "ba Leaf hoạt động liên tục trong nhiều phút" → "khoảng 2 phút với FPS tổng 4,85 ± 0,96 khung/giây" đúng như chương 4 |
| D1 ví dụ 2 (2–3 chặng) | Áp dụng theo yêu cầu | Không viết "đồ án chưa có số liệu 2–3 chặng"; chỉ viết "Chương 4 chưa trình bày kết quả đo độ trễ và thông lượng theo số hop" (chuong5.tex:23) |
| D1 tình huống giả định có đánh dấu | Đã áp dụng | chuong1.tex:11 ("Giả sử một robot hoạt động trong tòa nhà…"), không kèm số |
| D2 bỏ câu lặp | Đã áp dụng | Câu painlessMesh ở 2.3.2 viết lại theo mẫu D2; rút gọn các đoạn mở đầu/kết luận chương 1, 2, 3 |
| D3 thuật ngữ, khẩu ngữ, không "tôi" | Đã áp dụng | "đẩy … dữ liệu" → "gửi"; "đứng mạng" → "toàn mạng ngừng truyền dữ liệu" (ch2, lịch trình); "tôi" chỉ còn ở Lời cảm ơn, Lời cam đoan |
| D3 thuật ngữ thống nhất với chương 4 | Đã áp dụng | Theo chương 4: Root/Leaf (lần đầu "node gốc (Root)", "node lá (Leaf)"), "hop" để đếm số chặng (chương 4 dùng "hop"; "đa chặng" chỉ dùng làm tên khái niệm multi-hop), chunk, topology, tự phục hồi, KB/s, dấu phẩy thập phân, "0,5 %" có khoảng trắng. Chương 4 dùng lẫn "slot"/"vùng đệm ghép" nên chọn "vùng đệm ghép ảnh (slot)" |
| D4 tách kết quả và nhận xét | Đã áp dụng | Chương 5: hạn chế nêu trong phạm vi chương 4; "các nhận xét ở Chương 4 chỉ áp dụng cho một Root và tối đa ba Leaf đặt trên mặt đất". GPSR chỉ nêu là kết quả mô phỏng của tác giả khác |
| D5 vấn đề → đề xuất → lý do → kiểm chứng | Đã áp dụng | chuong5.tex:32–50 (tách task UART, đo tự phục hồi, đo theo số hop, NACK gộp, ESP-WIFI-MESH, định tuyến theo vị trí và 3D, ESP-NOW) |
| D6 đoạn chung chung | Đã áp dụng | Mục 1.1 viết lại theo bốn câu hỏi, thêm đoạn ý nghĩa thực tiễn ở cuối 1.1 (chuong1.tex:17) vì báo cáo chưa có mục riêng; đoạn mở đầu chương 2 viết lại |
| D7 checklist | Đã rà | Từng mục viết lại ở chương 1–3, 5, tóm tắt, abstract |

## 4. Lỗi trong chương 4 chưa sửa (chỉ ghi lại)

1. Mục 4.6 dùng gạch đầu dòng (`itemize`, chuong4.tex:123–130), vi phạm B4.
2. Còn 2 placeholder tô vàng "cần bổ sung": cuối 4.5 (đo self-healing ≥ 5 lần) và cuối 4.6 (độ trễ, thông lượng theo 1, 2, 3 hop).
3. Mâu thuẫn số liệu ba Leaf ở chu kỳ 600 ms: văn bản 4.4.3 ghi "chạy liên tục khoảng 2 phút … heap Root chủ yếu ở mức 95–101 KB", còn Bảng 4.2 ghi "chạy liền 3 phút … heap Root 87–108 KB" (số của phiên tối 29/09 ở Bảng 4.3).
4. Khẩu ngữ, thuật ngữ chưa thống nhất: "đứng mạng", "bị đứng", "rớt", "node ma", "Cái giá phải trả", "bring-up" chưa giải thích, "self-healing" (trong placeholder), lẫn "slot" và "vùng đệm ghép", "Relay" (chương khác dùng "node chuyển tiếp"), "dashboard" viết thường.
5. Các đoạn dưới 3 câu: đoạn mở đầu chương, đoạn 4.3, đoạn mở đầu 4.4, đoạn "Khi giảm tải khoảng 40 %…", đoạn "Trong phiên đo buổi tối ngày 29/09…".
6. Kết luận chương 4 nhắc "dòng tiêu thụ vẫn cần được bổ sung" nhưng thân chương không có nội dung về đo dòng.
7. Bảng 4.1 nhắc fork Alteriom/painlessMesh nhưng chương 4 không có trích dẫn nào.
8. Hình 4.1 (`chart_fps.png`) là ảnh xuất từ công cụ vẽ biểu đồ; chữ trong hình nhiều khả năng không cùng font với văn bản (LOI 14). Chưa đổi vì là tệp chỉ chương 4 dùng.
9. Các việc GHICHU Phần A dành cho chương 4 chưa làm: cải thiện Bảng 4.1; cập nhật test19/test21 (5,0 FPS; 280 → 199 ms; uartDrop 137 → 480 → 0; heap 62,4/61,5 KB; self-healing 11 220 ms; hop 1 ≈ 96 ms, hop 2 ≈ 228 ms ở test12); viết lại kết luận chương; lưu ý cột `kbps` trong CSV là KB/s.
10. Bố cục: vì không sửa chương 4, trang đầu chương 4 còn khoảng trống khoảng 5 dòng (Bảng 4.1 không vừa, tiêu đề 4.4 sang trang sau) và trang 38 còn khoảng trống khoảng 5 dòng.

## 5. Bố cục trang sau khi sửa

- Kiểm bằng `pdftotext -bbox-layout` từng trang: không còn trang nào có dòng cuối là tiêu đề mục.
- Đã xem ảnh trang (`pdftoppm`) chứa 1.6 (trang 5), 2.6 (trang 18–19), 3.4/3.4.1/Bảng 3.5 (trang 30), Hình 3.5 (trang 28).
- Lỗi lớn đã sửa: `\needspace` chèn `\penalty -100` làm TeX ngắt trang quá sớm (trang 1 chương 1 chỉ còn đoạn mở đầu, rất nhiều trang hở nửa trang). Đổi sang `\Needspace` (cauhinh.tex:142, 144).
- Còn lại: một số trang (13, 17, 20, 22, 27, 29, 30) hở khoảng 4–6 dòng ở cuối vì tiêu đề mục kế tiếp không đủ chỗ cho 6 dòng nên được chuyển sang trang sau. Đây là đánh đổi B1 đã nêu (giữ tiêu đề cùng vài dòng đầu). Nếu GVHD thấy các khoảng hở này không chấp nhận được, có thể giảm `6\baselineskip` xuống `4\baselineskip` ở cauhinh.tex:142, 144.

## 6. Thông tin chưa xác minh hoặc tự quyết định

1. **Phiên bản gói board esp32**: chương 1 cũ ghi 3.3.11; GHICHU (C2, theo nhật ký 13/09) và chú thích trong hình h2.png cũ ghi 2.0.17 "hạ từ 3.3.11 theo khuyến nghị painlessMesh". Đã sửa chương 1 thành 2.0.17 (chuong1.tex:79). Cần Danny xác nhận.
2. **Lưu đồ Hình 3.4–3.6** được vẽ lại từ mô tả ở chương 3 và lưu đồ PNG cũ (vốn vẽ theo firmware cũ: header 12 byte, sendBroadcast, timeout 5000 ms). Các chi tiết cần đối chiếu với `node_camera.ino`, `node_root.ino`, `protocol.js`: thứ tự kiểm tra hủy khung cũ (supersede) và loại LRU; vị trí kiểm tra khe gửi lệch pha; việc Leaf kiểm tra marker JPEG; Leaf xử lý thế nào khi chưa biết Root (không đưa vào lưu đồ); bước cập nhật độ trễ. Nhánh hủy khung quá 2 giây chỉ nêu trong văn bản, không vẽ.
3. `loop_max_ms` được hiểu là thời gian dài nhất của một vòng `loop()` (theo GHICHU C2.4), chưa đối chiếu với mã nguồn; báo cáo chỉ nêu tên chỉ số, không nêu số.
4. painlessMesh dùng TaskScheduler bên trong `mesh.update()`: lấy theo GHICHU C2.5, chưa đọc lại mã nguồn thư viện.
5. Tài liệu ESP-IDF: trang docs.espressif.com bị proxy chặn; nội dung C2 được đối chiếu với mã nguồn tài liệu `docs/en/api-reference/system/freertos_idf.rst` và `wdts.rst` (nhánh master của esp-idf trên GitHub), khớp với C2. Trích dẫn ghi URL bản latest, ngày truy cập 09/10/2026.
6. Cấu hình TWDT của gói 3.3.11 chưa kiểm (chỉ kiểm 2.0.17).
7. Các ý của Bekmezci và cộng sự (2013) (Bảng 1, 30–460 km/h, nghiên cứu mô phỏng GPSR, giả định 2D) lấy theo tóm tắt ở GHICHU C1, chưa đọc lại bài gốc.
8. Tác giả trong references.bib giữ dạng viết tắt tên như bản cũ, không bổ sung tên đầy đủ.
9. Câu "ESP-NOW không cần kết nối TCP giữa các node" (chuong5.tex:48) là kiến thức chung, kèm trích dẫn tài liệu ESP-NOW của Espressif.
10. Giữ Bảng 3.4 với `UART_TX_BUFFER_SIZE = 8192 byte` cho khớp với firmware 2026-09-30d mà chương 4 dùng; theo GHICHU firmware 2026-10-06a đã tăng lên 16 KB.
11. Không tách riêng mục "Ý nghĩa thực tiễn" để giữ số mục 1.6 như GHICHU nhắc; nội dung này là đoạn cuối của 1.1.
12. APA: tài liệu trực tuyến không ghi năm dùng "n.d.", trùng tác giả dùng "n.d.-a", "n.d.-b"…; tên tổ chức chưa có dấu chấm trước năm (giới hạn của apalike).

## 7. Việc Danny cần tự làm

1. **Chèn bản scan** phiếu nhiệm vụ và lịch trình có chữ ký: lưu thành `figures/nhiemvu.pdf` và `figures/lichtrinh.pdf`, biên dịch lại là tự thay cho trang tạm. Đối chiếu tên đề tài trên phiếu với `thongtin.tex`.
2. **Cấu hình TeXstudio**: Options > Configure TeXstudio > Build: Default Compiler = **XeLaTeX**, Default Bibliography Tool = **BibTeX**. Vì đã đổi kiểu trích dẫn, lần đầu cần Tools > Clean Auxiliary Files, sau đó chạy XeLaTeX → BibTeX → XeLaTeX → XeLaTeX. Times New Roman được dùng tự động nếu Windows có font này.
3. **Hỏi GVHD**: có chấp nhận kiểu trích dẫn APA (Tác giả, năm) không (nếu cần quay lại kiểu số, đổi `main.tex` về `IEEEtranN` và `cauhinh.tex` natbib về `numbers,square,sort&compress`); có cần thêm giáo trình môn học vào danh mục tài liệu không.
4. Điền ngày ký ở `thongtin.tex:24` (đang là dấu chấm để ký tay).
5. Xác nhận phiên bản gói board esp32 (mục 6.1) và kiểm lại 3 lưu đồ với mã nguồn (mục 6.2).
6. Khi cập nhật chương 4 với test19/test21: cập nhật đồng bộ Bảng 3.4 (bộ đệm UART 16 KB), đoạn cuối 2.4.5 (có thể thêm số 280 → 199 ms, uartDrop = 0), chương 5, tóm tắt và abstract; sau đó chạy lại kiểm tra bố cục vì số trang sẽ xê dịch.
7. Nếu thêm tài liệu tiếng Việt, cần thêm mục "Tiếng Việt" trước "Tiếng nước ngoài" (hiện BibTeX chỉ in một danh sách, xem chú thích ở cauhinh.tex:210–213).
8. Có thể xóa các ảnh cũ không còn dùng `figures/h1.png` … `figures/h6.png`.
9. `main.pdf` trong nhánh được biên dịch trên Linux với TeX Gyre Termes; biên dịch lại trên Windows để có bản dùng Times New Roman.
