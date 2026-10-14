# Kế hoạch sửa báo cáo – nhánh `sua-bao-cao-2026-10-10`

Lập ngày 09/10/2026 theo Bước 0 của yêu cầu sửa, đối chiếu với `ghichu/ghi-chu-sua-bao-cao_2026-10-10.md` (gọi tắt GHICHU).

## 0. Kiểm tra nhánh cũ

- `git branch main --contains 070a1b0` trả về `main`: commit `070a1b0` (xóa giả thuyết pin yếu, xóa Bảng 4.4, điền MSSV/lớp) **đã nằm trong `main`** qua PR #3 (merge `d8ed534`). Không cần cherry-pick.
- `main` hiện tại (`cc82433`) chỉ thêm GHICHU và một merge `dd91e4e` vào `thongtin.tex`. Merge này **để lại dấu xung đột** (`<<<<<<< HEAD … >>>>>>>`) ở dòng định nghĩa `\mssv` và `\lop`, làm `main.tex` không biên dịch được. Sửa ở nhóm việc 1 (giữ bản không tô vàng).

## 1. Bản đồ repo

| Thành phần | File | Ghi chú |
|---|---|---|
| File chính | `main.tex` | Dòng 1 đã có `% !TeX program = xelatex`; `latexmkrc` đặt `$pdf_mode = 5` (XeLaTeX) |
| Thông tin đề tài | `thongtin.tex` | Tên SV, MSSV, lớp, tên đề tài (3 dạng), GVHD, `\ngayky` (còn `\cansua{...}`) |
| Preamble | `cauhinh.tex` | scrextend 13pt, geometry 3.5/3/3.5/2 cm, fontspec + TeX Gyre Termes, setspace 1.5, titlesec, tocloft, caption, natbib `numbers`, soul (`\cansua` = `\hl`), macro `\hinh`, `\hinhcon`, cột `C{}`/`L{}` |
| Trang đầu (thứ tự include) | `frontmatter/biachinh` → `biaphu` → `loicamon` → `xacnhan` → `camdoan` → `nhiemvu` → `lichtrinh` → `tomtat` → `abstract` → mục lục → DM hình → DM bảng → `viettat` | `xacnhan` là trang Chủ tịch HĐ/Trưởng khoa (cần xóa theo A + B7); `nhiemvu` là trang giữ chỗ, chưa có scan |
| Chương 1 | `chapters/chuong1.tex` | Giới thiệu đề tài, 1.1–1.7 |
| Chương 2 | `chapters/chuong2.tex` | Cơ sở lý thuyết, 2.1–2.7 (2.2.3 FANET, 2.4 FreeRTOS, 2.6 công thức) |
| Chương 3 | `chapters/chuong3.tex` | Thiết kế hệ thống, 3.1–3.5 |
| **Chương 4** | **`chapters/chuong4.tex`** | **ĐÓNG BĂNG** |
| Chương 5 | `chapters/chuong5.tex` | Kết luận và hướng phát triển |
| Phụ lục | `appendix/phuluca.tex` | Phụ lục A, số trang A-1 |
| Tài liệu tham khảo | `references.bib` (12 mục, toàn tiếng Anh) | Đang dùng `\bibliographystyle{IEEEtranN}` + natbib `numbers` → kiểu số [1] |
| Kiểu APA sẵn có | `apalike-vn.bst` | apalike sửa nhãn "và cộng sự"; chưa được dùng |
| Hình | `figures/` | h1–h6.png (chương 3), dashboardtab1/2.png (chương 3), chart_fps.png (**chỉ chương 4**), logo_tdtu.jpg |
| PDF đã commit | `main.pdf` | Bản cũ |

Engine: XeLaTeX + fontspec. Font chính hiện đặt cứng TeX Gyre Termes (không dùng Times New Roman kể cả khi máy có).

Trích dẫn thực tế: `\citep`/`\citet` (natbib) ở chương 2 và 5; **chương 4 không có lệnh `\cite` nào**, nên đổi kiểu trích dẫn không ảnh hưởng câu chữ chương 4.

## 2. Phạm vi chương 4 (đã xác định chắc chắn)

- File: `chapters/chuong4.tex` (137 dòng), được `\input` riêng trong `main.tex`, chứa trọn `\chapter{KẾT QUẢ THỰC NGHIỆM VÀ ĐÁNH GIÁ}` và không chứa chương nào khác.
- Hình chỉ chương 4 dùng: `figures/chart_fps.png`. Repo không có file CSV.
- Nhãn chương 4 định nghĩa: `chap:4`, `tab:4.1`, `tab:4.2`, `tab:4.3`, `fig:4.1`. Chương 4 chỉ tham chiếu các nhãn của chính nó. Không chương nào khác tham chiếu nhãn chương 4.
- Key .bib chương 4 trích: không có.
- Macro tự định nghĩa chương 4 dùng: `\hinh`, `\cansua` (2 chỗ), kiểu cột `L{}`/`C{}`. Không đổi chữ do các macro này sinh ra.
- Số hiệu viết cứng: chỉ có "Chương 4", "Chương 5" (giữ nguyên số chương). Không ghi cứng số hình/bảng/mục của chương khác.

### Thuật ngữ, đơn vị, dấu thập phân chương 4 đang dùng

| Khái niệm | Chương 4 dùng | Quyết định cho toàn báo cáo |
|---|---|---|
| Node gốc / node lá | `Root`, `Leaf`, `node Leaf` (tiêu đề) | Lần đầu "node gốc (Root)", "node lá (Leaf)"; sau đó `Root`/`Leaf` |
| Node chuyển tiếp | `Relay` (1 lần) | Dùng "node chuyển tiếp"; nhắc `Relay` khi cần khớp chương 4 |
| hop | `hop` ("số hop", "1, 2, 3 hop") | Theo chương 4: đếm bằng `hop`; "truyền đa chặng (multi-hop)" chỉ dùng làm tên khái niệm, định nghĩa một lần |
| self-healing | "tự phục hồi" (self-healing chỉ có trong placeholder) | "tự phục hồi"; lần đầu kèm "(self-healing)" |
| chunk | `chunk` | `chunk` (lần đầu "mảnh dữ liệu (chunk)") |
| Vùng đệm ghép | lẫn `slot` và "vùng đệm ghép" | Không nhất quán → "vùng đệm ghép ảnh", lần đầu kèm "(slot)" |
| Khung ảnh | "khung" | "khung ảnh"/"khung" |
| Tốc độ khung | "khung/giây", "FPS tổng" | Giữ cả hai như chương 4 |
| Thông lượng | KB/s | KB/s |
| Tỉ lệ hủy khung | "tỉ lệ hủy" | "tỉ lệ hủy khung" |
| Topology | `topology` | `topology` |
| Giao diện | `dashboard` | Tên riêng "Web Dashboard"; gọi tắt "dashboard" |
| Đưa phần cứng vào hoạt động | `bring-up` | "đưa phần cứng vào hoạt động (bring-up)" |
| Dấu thập phân | dấu phẩy (1,67; 0,5 \%) | Dấu phẩy |
| Phần trăm | có khoảng trắng: `0,5 \%` | Có khoảng trắng |
| Khoảng giá trị | gạch ngang en: `111–113 KB` | Gạch en |
| Đơn vị khác | ms, s, giây, phút, KB, bps | Giữ |

### Số liệu đo xuất hiện trong chương 4 (chỉ những số này được nhắc lại ở chương khác, đúng điều kiện đo)

- Bring-up một Leaf: 1306 khung thành công, 6 khung hủy (khoảng 0,5 \%), không có khung UART sai CRC.
- Một Leaf, chu kỳ 600 ms: 1,67 ± 0,40 khung/giây; 7,8 KB/s; 8,4 chunk/s; hủy 0 \%; khung trung bình khoảng 4,6 KB; heap Root 111–113 KB.
- Hai Leaf, một vùng đệm ghép (17/09): 111 khung thành công, 496 khung hủy, tỉ lệ hủy khoảng 82 \% (so với 0,5 \% khi một Leaf); số slot bị hạ từ 4 xuống 1; sửa thành 4 slot 24 KB.
- Chu kỳ 300 ms sau khi sửa: FPS tổng khoảng 3,5 / 7 / 10 với 1 / 2 / 3 Leaf; hủy 0–5 \%; ba Leaf đứng mạng sau 6–26 giây, FPS tổng về 0 trong hơn 38 giây, số node báo vẫn là 3, heap Root từ khoảng 90 KB xuống 57 KB.
- Phân tích quá tải: khoảng 3,3 khung/giây mỗi Leaf; Root đẩy khoảng 47 KB/s qua UART 921600 bps; giới hạn lý thuyết khoảng 92 KB/s; Root dành gần một nửa thời gian ghi UART chặn.
- Giảm tải khoảng 40 \% bằng chu kỳ 600 ms: ba Leaf không còn đứng; FPS tổng giảm từ khoảng 10 xuống khoảng 5.
- Ba Leaf, chu kỳ 600 ms: chạy liên tục khoảng 2 phút, FPS tổng 4,85 ± 0,96, mỗi Leaf khoảng 1,6 khung/giây, 31 chunk/s, heap Root chủ yếu 95–101 KB; một đợt hủy 7–36 \% trong khoảng 20 giây, heap thấp nhất 73 KB.
- Bảng 4.2: 2 Leaf ≈ 3,3 FPS tổng, chạy ổn khoảng 4 phút; 3 Leaf 4,85 ± 0,96 (≈ 5 ở phiên tối), heap 87–108 KB, chạy liền 3 phút.
- Bảng 4.3 (tối 29/09): 14:07:59–14:10:59 ba Leaf ≈ 5 FPS; ≈ 14:11 Leaf 1207369673 rớt (khung #343); 14:12:01–14:16:15 hai Leaf ≈ 3,3 FPS; 14:16:17–14:16:39 Leaf 1207449329 ngừng gửi, heap 104 → 82 KB.
- Tự phục hồi: 16/09: 885, 10461, 32528 ms; 29/09: 1243, 3944, 7691, 8823, 19919 ms; thời gian chờ painlessMesh khoảng 10 giây.
- Firmware 2026-09-30c (Leaf) / 2026-09-30d (Root); test15-newfirmware.csv: khung bị bỏ ở UART tăng từ 4 lên 20, FPS khoảng 3.

Số liệu GHICHU có nhưng chương 4 **không** có (không đưa vào chương 1–3, 5 và trang đầu): test19, test21 (5,0 FPS; 22,0 / 25,6 KB/s; 280 / 199 ms; uartDrop 137 → 480 / 0; heap 62,4 / 61,5 KB), self-healing 11 220 ms, hop 1 ≈ 96 ms / hop 2 ≈ 228 ms, heap 154 → 72 KB (D1 ví dụ 4).

## 3. Kết quả biên dịch mốc (bản `main`, đã gỡ tạm dấu xung đột trong bản sao)

- TeX Live 2023 (Ubuntu) cài được; `latexmk -xelatex` biên dịch thành công, 54 trang PDF; nội dung chính trang 1–35.
- **Lỗi mất ký tự tiếng Việt**: 175 cảnh báo `Missing character … in font ectt1000`. Nguyên nhân: `\cansua` dùng `\hl` của gói soul; dưới XeLaTeX soul làm rơi các ký tự có dấu (ví dụ tóm tắt in thành "Kết qu đo tr … theo s hop"). Ảnh hưởng mọi chỗ có `\cansua`, kể cả 2 placeholder trong chương 4. Hướng sửa: định nghĩa lại `\cansua` bằng ulem (`\markoverwith` + `\ULon`), đã thử: 0 cảnh báo, giữ highlight vàng và ngắt dòng bình thường; chữ trong chương 4 không đổi.
- Không có "??".
- Times New Roman không có trên Linux → dùng TeX Gyre Termes khi biên dịch ở đây.

## 4. Kế hoạch theo nhóm việc

1. Trang đầu: sửa xung đột `thongtin.tex`; bỏ `\cansua` ở `\ngayky`; xóa include `xacnhan`; thứ tự B7 (bìa → phụ bìa → cảm ơn → cam đoan → nhiệm vụ → lịch trình → tóm tắt → abstract → mục lục → DM hình → DM bảng → viết tắt); trang nhiệm vụ chèn bằng `\includepdf` nếu có `figures/nhiemvu.pdf`, nếu không giữ trang có tiêu đề; thống nhất tên đề tài.
2. Định dạng: fontspec ưu tiên Times New Roman, fallback TeX Gyre Termes; `\cansua` bằng ulem; needspace; giữ các thiết lập lề/giãn dòng/số trang đã đúng; số trang phần đầu ở giữa phía trên.
3. Chương 2: 2.2.3 FANET (C1) + Bảng MANET/VANET/FANET; 2.4 FreeRTOS (C2, 2.4.1–2.4.5) + hình TikZ; 2.6 công thức (B3); bỏ gạch đầu dòng.
4. Chương 1, 3: viết lại theo B4/B5/D6; bỏ gạch đầu dòng; LOI 5; 3.4/3.4.1.
5. Chương 5: ngắn gọn, chỉ dựa trên chương 4; hướng phát triển theo D5.
6. Rà toàn văn trừ chương 4.
7. Tài liệu tham khảo (C3), danh mục viết tắt; commit riêng đổi sang APA bằng `apalike-vn.bst` (chương 4 không có `\cite`).
8. Bố cục trang (làm sau cùng).
9. Kiểm tra cuối, báo cáo `ghichu/BAO_CAO_SUA_2026-10-10.md`.
