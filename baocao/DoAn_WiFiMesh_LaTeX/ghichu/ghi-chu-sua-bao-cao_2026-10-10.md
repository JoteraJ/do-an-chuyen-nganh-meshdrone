# Ghi chú sửa báo cáo đồ án – làm ngày 10/10/2026

Ghi lại từ buổi đọc duyệt phần trình bày báo cáo ngày 09/10/2026.

## ⚠ Nguyên tắc quan trọng nhất (áp dụng cho toàn bộ báo cáo)

Nội dung của mỗi tiểu mục phải đi liền với nhau trong một trang để báo cáo liền mạch, mạch lạc, gọn gàng và dễ theo dõi. Cụ thể:

– Tiêu đề tiểu mục không được nằm cuối trang trong khi nội dung bị đẩy sang trang sau.
– Hình/bảng nên nằm cùng trang với đoạn văn dẫn ra nó.
– Nếu một tiểu mục quá dài, ưu tiên ngắt trang ở ranh giới giữa các tiểu mục, không ngắt giữa chừng.
– Gợi ý LaTeX: `\needspace{...}` trước tiêu đề, đặt hình/bảng với `[H]` hoặc `[!htbp]`, tránh dòng mồ côi bằng `\widowpenalty=10000` và `\clubpenalty=10000`.

## Danh sách việc cần sửa

### Phần đầu báo cáo
– [ ] Xóa highlight ở chỗ mã sinh viên và lớp.
– [ ] Trang 4: xóa phần "giảng viên xác nhận danh sách được phê duyệt".

### Chương 1
– [ ] Tiểu mục 1.6: hiện đang bị tách sang trang mới, làm nội dung không liền mạch. Chỉnh lại để tiêu đề và nội dung 1.6 đi cùng nhau.

### Chương 2
– [ ] 2.2.3: bổ sung trích dẫn bài khảo sát (survey). *Cần xác nhận lại tài liệu nào* – trong file `tài_liệu` hiện có các ứng viên:
  – Akyildiz, Wang & Wang (2005), *Wireless mesh networks: a survey*, Computer Networks 47(4), 445–487.
  – *A survey on position-based routing protocols for FANETs*, Ad Hoc Networks (ScienceDirect).
  – *Routing protocols strategies for FANET: Review, taxonomy, and open research issues* (2024).
– [ ] 2.4: kiểm tra lại phần kiến thức chung về FreeRTOS (độ chính xác, thuật ngữ). Theo `claude_2026-10-07_ra-soat-tien-do-repo.md`, mục FreeRTOS còn cần gắn với số liệu thực tế (loop_max_ms, Task WDT, mesh.update) và bổ sung trích dẫn.
– [ ] 2.6: chỉnh lại cách trình bày công thức (căn chỉnh, đánh số, giải thích ký hiệu). Gói nội dung trong một trang nếu được; các tiểu mục phải liền mạch với nhau.

### Chương 3
– [ ] 3.4 và Hình 3.5: đặt chung một trang nếu có thể.
– [ ] 3.4.1 và Bảng 3.5: đặt cùng nội dung của tiểu mục; nếu quá dài thì vẫn ưu tiên để chung một trang cho mạch lạc.

### Chương 4
– [ ] Cải thiện Bảng 4.1 với kết quả tốt nhất có thể.
– [ ] Cập nhật toàn chương với kết quả test mới nhất và viết kết luận dựa trên đó.

Số liệu mới nhất hiện có (theo `claude_2026-10-07_ra-soat-tien-do-repo.md`, đo ngày 06/10, 3 Leaf, hop_max = 1):

| Test | FW Root | Thời lượng | FPS tổng (TB) | KB/s (TB) | Latency TB | uartDrop | Heap min |
|---|---|---|---|---|---|---|---|
| test19 | 2026-10-01c (TX 8 KB) | 15 phút | 5.0 | 22.0 | 280 ms | 137 → 480 | 62.4 KB |
| test21 | 2026-10-06a (TX 16 KB) | 20 phút | 5.0 | 25.6 | 199 ms | 0 | 61.5 KB |

Lưu ý khi viết chương 4 (cùng nguồn trên):
– Cột `kbps` trong CSV thực chất là KB/s.
– test19 có 1 sự kiện self-healing: 11 220 ms.
– Từ test16 đến test21 không có lần nào hop > 1; số liệu hop 2 chỉ có ở test12 (FW cũ): hop 1 ≈ 96 ms, hop 2 ≈ 228 ms.
– Kết luận nên nêu: tăng UART TX lên 16 KB đưa uartDrop về 0 và giảm latency từ 280 ms xuống 199 ms.
– Nếu có test mới hơn test21 thì thay bằng bản mới nhất (không tìm thấy trong tài liệu hiện có).
– Đồng thời sửa lỗi mất ký tự tiếng Việt trong PDF và xóa các placeholder "cần bổ sung" (theo cùng file 07/10).

## Thứ tự làm đề xuất
1. Sửa nhanh phần đầu (highlight, trang 4).
2. Cập nhật nội dung chương 4 (việc nặng nhất).
3. Bổ sung trích dẫn 2.2.3, rà FreeRTOS 2.4, công thức 2.6.
4. Cuối cùng rà toàn bộ bố cục trang theo nguyên tắc "tiểu mục đi liền một trang" – làm sau cùng vì mọi thay đổi nội dung đều làm xê dịch trang.

---

# Phần B – Đối chiếu với tiêu chuẩn trình bày của Khoa/Trường

Nguồn đã đọc (bổ sung ngày 09/10/2026):
– `Huong_dan_trinh_bay_khoa_luan_TN__do_an_TN.pdf` – Hướng dẫn trình bày khóa luận/đồ án của Trường (2020), gọi tắt **[HDTB]**.
– `huong_dan_thuc_hien_DATN.pdf` – Slide hướng dẫn của Bộ môn Điện tử – Viễn thông (26/10/2024), gọi tắt **[SLIDE]**.
– `cac_loi_thuong_gap_DATN.docx` – 16 lỗi thường gặp, gọi tắt **[LOI]**.
– `MauDATN_2021.docm` – Template Word của Khoa, gọi tắt **[MAU]**.
– `TC_42101349_DACN_LÊ_THỊ_THU_THẢO.docx` – Báo cáo đồ án chuyên ngành mẫu đã nộp (2024), gọi tắt **[VIDU]**.

Lưu ý: các tài liệu trên viết cho ĐATN, nhưng template và báo cáo mẫu đồ án chuyên ngành [VIDU] đều áp dụng cùng quy cách, nên dùng làm chuẩn cho đồ án này.

## B1. Căn cứ cho nguyên tắc "tiểu mục đi liền một trang"

Nguyên tắc ở Phần A được các quy định sau hỗ trợ trực tiếp:
– Bảng ngắn và đồ thị nhỏ phải đi liền với đoạn văn nhắc tới chúng lần đầu; bảng dài để trang riêng thì phải nằm ngay sau đoạn nhắc tới [HDTB, mục 2.3].
– Không được để khoảng trống cuối trang, trừ trang cuối của chương [SLIDE].
– Nếu hình lớn bị đẩy sang trang mới thì phải dời đoạn văn phía sau hình lên trước để lấp khoảng trắng [LOI, lỗi 8].

⚠ **Hai yêu cầu này có thể xung đột.** Nếu ép cả tiểu mục vào một trang mới thì trang trước sẽ để trống, và đó là lỗi bị bắt. Cách xử lý đúng:
– Giữ **tiêu đề + ít nhất vài dòng đầu** đi cùng nhau (không để tiêu đề mồ côi cuối trang).
– Giữ **hình/bảng cùng trang với đoạn dẫn**. Nếu không vừa, cho hình sang trang sau và kéo đoạn văn kế tiếp lên lấp chỗ trống.
– Chỉ chấp nhận khoảng trắng ở trang cuối của chương.
– Trong LaTeX: dùng `\needspace{6\baselineskip}` trước `\section`/`\subsection`; với hình/bảng ưu tiên `[!htbp]` thay vì `[H]`, vì `[H]` dễ tạo khoảng trắng lớn. Chỉ dùng `[H]` khi đã kiểm tra trang không bị hở.

## B2. Định dạng chung [HDTB mục 2.1, 2.2; MAU]

– [ ] Font Times New Roman cỡ 13, giãn dòng 1.5, không nén/giãn chữ.
– [ ] Lề: trên 3.5 cm, dưới 3 cm, trái 3.5 cm, phải 2 cm. [VIDU] dùng đúng các lề này.
– [ ] Số trang ở **giữa, phía trên** đầu trang.
– [ ] Dòng đầu mỗi đoạn thụt 1 tab.
– [ ] Nội dung chính từ 30 đến 80 trang (không tính bìa, danh mục, tài liệu tham khảo, phụ lục) [HDTB 2.1].
– [ ] Tiểu mục tối đa 3 cấp. Mỗi nhóm phải có **ít nhất 2 tiểu mục**: có 3.4.1 thì phải có 3.4.2 [HDTB 2.2]. → Kiểm tra lại 3.4.1 khi sửa mục 3.4.

## B3. Hình, bảng, công thức [HDTB 2.3; LOI]

– [ ] Đánh số theo chương: Hình 3.5, Bảng 4.1, công thức (2.1).
– [ ] **Tên bảng đặt phía trên bảng; tên hình đặt phía dưới hình.** → Kiểm tra Bảng 3.5 và Bảng 4.1.
– [ ] Khi nhắc tới hình/bảng phải dùng số ("xem Hình 3.5", "được nêu trong Bảng 4.1"), không dùng "hình dưới", "hình bên" [SLIDE].
– [ ] Mọi hình đều phải được diễn giải bằng lời [SLIDE]. Chương 4 không được trình bày kiểu "album ảnh"; các ảnh gần nhau thì gộp thành một hình với các hình nhỏ (a), (b), (c) [LOI 6].
– [ ] Hình lấy từ nguồn khác phải trích nguồn dưới chú thích; ảnh Internet không rõ nguồn thì ghi "Source: Internet" [LOI 7].
– [ ] Giữ nguyên tỷ lệ ngang:dọc, không làm méo ảnh [LOI 13]. Kích thước hình cân đối: hình linh kiện phụ không được to hơn sơ đồ khối [LOI 9].
– [ ] Chữ trong sơ đồ khối/lưu đồ phải cùng font và cỡ với văn bản, tự vẽ lại nếu chép từ nơi khác [LOI 14].
– [ ] Ảnh chụp mô hình thật phải xử lý nền, không để lẫn bàn tay, dây lộn xộn hay vật lạ [LOI 16].
– [ ] Lưu đồ giải thuật phải chi tiết và đúng ký hiệu chuẩn [LOI 10].

**Áp dụng cho mục 2.6 (công thức):**
– [ ] Tất cả công thức được đánh số trong ngoặc đơn, đặt sát lề phải.
– [ ] Ký hiệu mới phải được giải thích **kèm đơn vị** ngay sau công thức có ký hiệu đó.
– [ ] Cách trình bày công thức (dòng đơn hay dòng kép) phải thống nhất trong toàn báo cáo.
– [ ] Chỉ số dưới/trên viết đúng dạng (x₁, e²), không viết ngang hàng [LOI 12].
– [ ] Không dùng ảnh chụp hoặc ảnh scan cho công thức [SLIDE].

## B4. Văn phong [SLIDE; LOI 1, 3]

– [ ] **Tuyệt đối không viết dạng gạch đầu dòng** trong thân báo cáo, trừ khi trình bày datasheet hoặc hướng dẫn sử dụng phần mềm [SLIDE]. → Rà lại toàn bộ chương 2–4.
– [ ] Không có câu què, câu cụt; mỗi câu phải có đủ chủ ngữ và vị ngữ.
– [ ] Mỗi đoạn văn gồm câu chủ đề, 1–2 câu diễn giải và câu kết/chuyển ý. Không để đoạn chỉ có 1 câu [LOI 1].
– [ ] Không dùng câu văn dịch máy; tự viết lại theo cách hiểu của mình [LOI 3].
– [ ] Viết tắt đúng chuẩn: WiFi, LED, MATLAB, ESP32-CAM, FreeRTOS, UART, JPEG… [LOI 11].

## B5. Nội dung từng chương [SLIDE; LOI 2, 4, 5]

– [ ] Chương 1 phải diễn giải chi tiết yêu cầu đề tài (chức năng, thông số kỹ thuật, phạm vi giới hạn), **không chép nguyên văn phiếu nhiệm vụ** [LOI 2].
– [ ] Tên đề tài phải ghi đúng như phiếu nhiệm vụ ở bìa, phụ bìa, tóm tắt và lịch trình [LOI 4].
– [ ] Chương thiết kế đi từ sơ đồ khối tổng quan xuống từng khối. Linh kiện chỉ giới thiệu khi gắn với khối dùng nó, không dồn hết vào một mục giới thiệu linh kiện ở đầu chương [LOI 5].
– [ ] Chương 4 (kết quả) gồm kết quả và nhận xét, đánh giá. Phần bàn luận phải dựa trên số liệu đo được [SLIDE; HDTB mục 1].
– [ ] Chương Kết luận trình bày ngắn gọn các kết quả mới, **không bàn luận thêm**, và kèm hướng phát triển [HDTB mục 1; MAU].

## B6. Viết tắt và tài liệu tham khảo [HDTB 2.4–2.6; LOI 7, 15]

– [ ] Danh mục chữ viết tắt phải **đầy đủ** (đọc lại toàn bộ báo cáo để liệt kê) và sắp theo thứ tự ABC [LOI 15].
– [ ] Lần đầu dùng một từ viết tắt phải viết đầy đủ, kèm chữ viết tắt trong ngoặc đơn.
– [ ] Trích dẫn theo kiểu **APA, dạng (Tác giả, năm)**, ví dụ (Akyildiz và cộng sự, 2005) [HDTB 2.5]. ⚠ Nếu bản LaTeX đang dùng kiểu số [1], [2] thì cần hỏi GVHD có chấp nhận không, hoặc chuyển sang `biblatex` với `style=apa`.
– [ ] Danh mục tài liệu tham khảo chia theo Tiếng Việt / Tiếng nước ngoài và sắp theo ABC họ tác giả. Tên tài liệu tiếng nước ngoài giữ nguyên văn [HDTB 2.6.2].
– [ ] Không đưa link cửa hàng (Shopee, Tiki…) hay link trần vào danh mục. Nên có giáo trình chính của các môn liên quan [LOI 7].
– [ ] Mọi hình, bảng, công thức hay ý tưởng lấy từ nguồn khác đều phải trích dẫn; thiếu trích dẫn thì báo cáo không được duyệt bảo vệ [HDTB 2.5]. → Áp dụng cho việc bổ sung trích dẫn ở 2.2.3 và 2.4.

## B7. Thứ tự các trang đầu [HDTB mục 3; MAU; VIDU]

Thứ tự đúng: bìa chính → phụ bìa (có MSSV và người hướng dẫn) → lời cảm ơn (có chữ ký) → lời cam đoan → phiếu nhiệm vụ → lịch trình đồ án → tóm tắt tiếng Việt và Abstract (1–2 trang) → mục lục → danh mục hình → danh mục bảng → danh mục chữ viết tắt → nội dung → tài liệu tham khảo → phụ lục.

– [ ] Phiếu nhiệm vụ và tờ lịch trình phải **đóng chung vào cuốn báo cáo**, không để rời. [VIDU] ghi chú rõ yêu cầu này ngay sau trang cam đoan.
– [ ] Liên quan mục "Trang 4" ở Phần A: trang xác nhận của Chủ tịch Hội đồng và Trưởng khoa (Mẫu 4-1 [HDTB]) chỉ ký sau khi bảo vệ xong. Báo cáo mẫu [VIDU] không có trang này, đi thẳng từ lời cảm ơn sang lời cam đoan (Mẫu 4-2), nên việc xóa là phù hợp.
– [ ] Xóa toàn bộ phần hướng dẫn tô vàng và các chú thích mẫu như "(Bold, size 14)" còn sót từ template [MAU; SLIDE]. → Liên quan mục xóa highlight ở MSSV và lớp.
– [ ] Phụ lục đánh số khác với phần thân (ví dụ Phụ lục A) hoặc không đánh số [SLIDE].

## B8. Bước kiểm tra cuối trước khi xuất PDF

– [ ] Tự đọc lại toàn bộ báo cáo để bắt lỗi chính tả và kiểm tra độ dễ hiểu [SLIDE].
– [ ] Cập nhật mục lục và danh mục hình/bảng.
– [ ] Kiểm tra PDF không bị mất ký tự tiếng Việt (lỗi đã ghi ở `claude_2026-10-07_ra-soat-tien-do-repo.md`).
– [ ] Lật từng trang, đối chiếu với B1: không có tiêu đề nằm cuối trang, không có khoảng trắng giữa chương.

---

# Phần C – Mở rộng nội dung FANET và FreeRTOS (bổ sung 09/10/2026)

## C0. Yêu cầu bổ sung cho buổi làm việc

– [ ] Viết lại mục lý thuyết FANET (thuộc chương 2, gắn với 2.2.3) theo dàn ý ở C1. Dùng bài khảo sát của Bekmezci và cộng sự (2013) làm trích dẫn chính.
– [ ] Viết lại mục 2.4 FreeRTOS theo dàn ý ở C2. Mục này phải đi từ khái niệm chung, sang cách FreeRTOS chạy trên ESP32, rồi gắn với hiện tượng đo được trong đồ án. Theo `claude_2026-10-07_ra-soat-tien-do-repo.md`, mục FreeRTOS còn thiếu phần gắn với số liệu (loop_max_ms, Task WDT, mesh.update) và thiếu trích dẫn.
– [ ] Thêm 1 bảng so sánh MANET/VANET/FANET (gợi ý ở C1.3) và 1 hình sơ đồ phân bổ tác vụ trên 2 lõi ESP32 (gợi ý ở C2.5).
– [ ] Viết toàn bộ bằng văn xuôi, không dùng gạch đầu dòng (theo B4).
– [ ] Cập nhật danh mục tài liệu tham khảo theo C3 và danh mục chữ viết tắt: UAV, FANET, MANET, VANET, RTOS, SMP, TWDT, IWDT, GPSR, OLSR, AODV, DSR.

## C1. FANET – Flying Ad-hoc Network

Nguồn chính: Bekmezci, Sahingoz & Temel (2013), *Flying Ad-Hoc Networks (FANETs): A survey*, Ad Hoc Networks 11(3), 1254–1270. Các tác giả nêu đây là bài khảo sát đầu tiên xem FANET là một họ mạng ad-hoc riêng; trang ResearchGate của bài ghi nhận 1.344 lượt trích dẫn (truy cập 09/10/2026). Trong `tài_liệu`, mục 2.4 chỉ nêu FANET ở mức tóm tắt; phần dưới đây mở rộng từ bài gốc.

### C1.1. Định nghĩa (dùng cho đoạn mở đầu mục)
– FANET là mạng ad-hoc mà các node là UAV, được xem như một dạng đặc biệt của MANET và VANET.
– Hai điều kiện để một hệ thống được gọi là FANET: (1) phải có nhiều UAV, vì một UAV đơn lẻ không tạo thành FANET; (2) các UAV phải liên lạc với nhau qua mạng ad-hoc giữa chúng. Nếu mọi UAV đều chỉ nói chuyện qua trạm mặt đất hoặc vệ tinh thì đó không phải FANET.
– Lý do cần FANET: kiến trúc lấy hạ tầng làm trung tâm (trạm mặt đất, vệ tinh) đòi hỏi mỗi UAV mang phần cứng liên lạc đắt và nặng. Kiến trúc này cũng dễ mất kết nối khi UAV ra ngoài vùng phủ hoặc bị địa hình che khuất. Trong FANET, chỉ một số UAV cần nối với hạ tầng, các UAV còn lại chuyển tiếp dữ liệu qua nhau.

### C1.2. Các kịch bản ứng dụng (chọn 2–3 để viết)
– Mở rộng vùng hoạt động: tạo chuỗi liên kết đa chặng giữa các UAV để vươn ra ngoài vùng phủ của trạm mặt đất, kể cả khi bị nhà cao tầng hay núi che. → **Đây là kịch bản gần nhất với đồ án** (Leaf chuyển tiếp qua node trung gian để về Root).
– Tăng độ tin cậy: khi một UAV mất kết nối với hạ tầng, nó vẫn duy trì liên lạc qua các UAV khác.
– Bầy UAV (swarm): phối hợp nhiều UAV nhỏ để làm nhiệm vụ phức tạp.
– Giảm tải trọng và chi phí: phần cứng FANET nhẹ hơn phần cứng liên lạc với vệ tinh.

### C1.3. Khác biệt giữa FANET với MANET và VANET (gợi ý làm Bảng 2.x)

| Tiêu chí | MANET | VANET | FANET |
|---|---|---|---|
| Mức độ di động | Thấp | Cao | Rất cao |
| Mô hình di chuyển | Ngẫu nhiên | Theo đường giao thông | Theo kế hoạch bay hoặc mô hình riêng cho UAV tự hành |
| Mật độ node | Thấp | Cao | Rất thấp |
| Thay đổi tô-pô | Chậm | Nhanh | Nhanh |
| Truyền sóng | Gần mặt đất, thường không có LoS | Gần mặt đất, thường không có LoS | Trên cao, thường có LoS |
| Năng lượng | Cần giao thức tiết kiệm năng lượng | Không phải vấn đề chính | Quan trọng với mini-UAV |
| Năng lực tính toán | Hạn chế | Cao | Cao |
| Định vị | GPS | GPS, AGPS, DGPS | GPS, AGPS, DGPS, IMU |

*Nguồn: tổng hợp theo Bảng 1 của Bekmezci và cộng sự (2013). Khi đưa vào báo cáo, tự kẻ lại bảng và ghi nguồn dưới tên bảng.*

Các ý cần diễn giải thành văn sau bảng:
– Tốc độ UAV có thể từ 30 đến 460 km/h, làm tô-pô thay đổi liên tục.
– Ngoài chuyển động, tô-pô còn thay đổi khi một UAV hỏng, khi bổ sung UAV mới, hoặc khi chất lượng liên kết dao động.
– Khoảng cách giữa các node FANET lớn hơn nhiều so với MANET và VANET, nên tầm phát cần dài hơn.
– Lưu lượng FANET phải hỗ trợ đồng thời 2 kiểu: ngang hàng (UAV ↔ UAV) để phối hợp, và hội tụ (convergecast, nhiều UAV → 1 trạm) để gửi dữ liệu cảm biến về. → **Đồ án là trường hợp convergecast: nhiều Leaf gửi ảnh về Root.**

### C1.4. Các yêu cầu thiết kế FANET (đối chiếu với đồ án)
– **Khả năng thích nghi:** mạng phải tự điều chỉnh khi node di chuyển, hỏng hay được thêm mới. → Gắn với cơ chế tự phục hồi (self-healing) của painlessMesh và số liệu self-healing ở chương 4.
– **Khả năng mở rộng:** hiệu năng không được suy giảm nhiều khi số UAV tăng. → Gắn với phép đo 1/2/3 Leaf.
– **Độ trễ:** yêu cầu tùy ứng dụng; ứng dụng thời gian thực có giới hạn trễ chặt. → Gắn với latency 199 ms ở test21.
– **Ràng buộc nền tảng UAV:** khối lượng và kích thước phần cứng truyền thông bị giới hạn. → Lý do chọn ESP32-CAM nhỏ, nhẹ.
– **Băng thông:** truyền ảnh hoặc video về trạm điều khiển đòi hỏi băng thông cao, trong khi kênh vô tuyến dễ lỗi và tốc độ UAV làm giảm chất lượng kênh. → Gắn với việc phân mảnh ảnh và giới hạn FPS.

### C1.5. Định tuyến trong FANET (đối chiếu với `tài_liệu` mục 2.4)
– Các thử nghiệm FANET đầu tiên dùng lại giao thức MANET: chủ động (proactive, như OLSR, TBRPF) và phản ứng (reactive, như DSR, AODV).
– Duy trì bảng định tuyến kiểu chủ động không tối ưu khi tô-pô thay đổi nhanh. Ngược lại, tìm đường lại trước mỗi gói kiểu phản ứng cũng tốn kém.
– Nhóm định tuyến dựa vị trí (position-based, ví dụ GPSR) được báo cáo cho kết quả tốt hơn hai nhóm trên trong mô phỏng FANET. Tuy nhiên độ tin cậy giảm khi mạng thưa.
– Nhóm phân cấp (cluster, có cluster head) phù hợp khi vùng nhiệm vụ rộng và số UAV lớn.
– **Liên hệ đồ án:** painlessMesh tự tổ chức thành cây, không dùng thông tin vị trí. Đây là điểm cần nêu trung thực ở phần hạn chế và hướng phát triển (bổ sung GPS/UWB để thử định tuyến theo vị trí), khớp với `tài_liệu` mục 2.4.

### C1.6. Lưu ý khi viết để tránh bị phản biện bắt lỗi
– Các node trong đồ án đặt cố định trên mặt đất, không bay. Vì vậy nên viết rằng đồ án **xây dựng và thử nghiệm mô hình truyền thông đa chặng theo hướng FANET** cho drone/robot. Không nên khẳng định hệ thống "là một FANET".
– Bekmezci và cộng sự cũng nêu một hạn chế chung của nhiều nghiên cứu: phần lớn giả định tô-pô 2D, trong khi FANET thực tế là 3D. Có thể dùng ý này ở phần hướng phát triển.

## C2. FreeRTOS trên ESP32

Nguồn chính: tài liệu ESP-IDF Programming Guide của Espressif (các trang *FreeRTOS (IDF)* và *Watchdogs*). Đồ án dùng Arduino-ESP32 core 2.0.17 (theo `2026-09-13_middleware-test-wifi-debug.md`). Theo trang release của Espressif, core 2.0.17 được xây dựng trên ESP-IDF v4.4.7. Vì vậy khi trích dẫn tài liệu ESP-IDF nên ghi đúng phiên bản 4.4, hoặc ghi rõ đang trích bản mới nhất.

### C2.1. Khái niệm (2.4.1)
– RTOS là hệ điều hành thời gian thực cho vi điều khiển. FreeRTOS là một nhân RTOS nhỏ gọn, chạy được trên nhiều dòng MCU và SoC đơn lõi.
– Chương trình được chia thành các **tác vụ (task)**. Mỗi task thường là một vòng lặp vô hạn và không bao giờ return.
– Một task luôn ở một trong 4 trạng thái: Running, Ready, Blocked, Suspended. Task ở trạng thái Blocked (ví dụ đang chờ hàng đợi hay chờ notification) **không tiêu tốn CPU**.
– Các cơ chế giao tiếp giữa task: queue, semaphore, mutex, event group, task notification. Task notification là cách nhẹ và nhanh để báo sự kiện trực tiếp cho task, có thể dùng thay semaphore.

### C2.2. Bộ lập lịch (2.4.2)
– FreeRTOS dùng bộ lập lịch **ưu tiên cố định, có chiếm quyền (preemptive), có chia lát thời gian (time slicing)**:
  – Luôn chạy task Ready có ưu tiên cao nhất.
  – Có thể ngắt task đang chạy mà không cần task đó hợp tác.
  – Các task cùng mức ưu tiên được luân phiên theo kiểu round-robin ở mỗi ngắt tick.
– Ngắt tick tuần hoàn có 3 nhiệm vụ: tăng bộ đếm tick, giải phóng các task hết thời gian chờ, và kích hoạt chuyển ngữ cảnh khi cần.
– Idle task có ưu tiên 0, chạy khi không còn task nào sẵn sàng. Nhiệm vụ của nó là giải phóng bộ nhớ của các task đã bị xóa.
– Dùng `xTaskDelayUntil()` thay `vTaskDelay()` khi cần chu kỳ cố định, vì `vTaskDelay()` tính thời gian chờ tương đối kể từ lúc gọi.

### C2.3. FreeRTOS trên ESP32: bản SMP hai lõi (2.4.3)
– ESP32 có 2 lõi giống nhau và chia sẻ bộ nhớ (SMP). Espressif sửa đổi FreeRTOS gốc thành bản **IDF FreeRTOS** để chạy trên cả hai lõi.
– Core 0 thường được gọi là PRO_CPU và chạy các tác vụ giao thức (Wi-Fi, Bluetooth). Core 1 thường được gọi là APP_CPU và chạy phần ứng dụng.
– Có thể gắn task vào một lõi bằng `xTaskCreatePinnedToCore()` với tham số 0, 1 hoặc `tskNO_AFFINITY`. Lưu ý: kích thước stack trong ESP-IDF tính bằng **byte**, không phải word như FreeRTOS gốc.
– Mỗi lõi tự lập lịch độc lập và chọn task Ready có ưu tiên cao nhất mà nó được phép chạy. Hệ quả: 2 task ưu tiên cao nhất chưa chắc cùng chạy, nếu chúng bị gắn cùng một lõi.
– Chỉ core 0 chịu trách nhiệm đếm thời gian hệ thống. Mỗi lõi có một idle task riêng.
– Tắt ngắt không còn đủ để bảo vệ vùng găng (critical section) vì lõi kia vẫn chạy. IDF dùng spinlock (`portMUX_TYPE`) cho vùng găng, và vùng găng phải ngắn nhất có thể.

### C2.4. Watchdog (2.4.4)
– **IWDT (Interrupt Watchdog):** phát hiện khi ngắt chuyển task của FreeRTOS bị chặn quá lâu. Khi đó không task nào khác được chạy, kể cả task Wi-Fi và idle task.
– **TWDT (Task Watchdog):** phát hiện task chạy quá lâu mà không nhường CPU. Mặc định TWDT theo dõi idle task của mỗi lõi. Task khác có thể đăng ký bằng `esp_task_wdt_add()` và phải gọi `esp_task_wdt_reset()` định kỳ để "cho ăn" watchdog.
– Ý nghĩa với đồ án: nếu vòng lặp chính bị chặn lâu (ví dụ ghi UART dồn dập), idle task của lõi đó không được chạy và TWDT sẽ báo lỗi. Đây là cơ sở lý thuyết cho chỉ số `loop_max_ms` đang ghi trong log.

### C2.5. Áp dụng trong đồ án (2.4.5 – phần quan trọng nhất)
– Với Arduino-ESP32, `setup()` và `loop()` chạy bên trong một task FreeRTOS (loopTask). Mặc định task này được gắn vào core 1, còn Wi-Fi chạy ở core 0. ⚠ Cần kiểm chứng lại trong file `cores/esp32/main.cpp` của core 2.0.17 trước khi viết vào báo cáo.
– painlessMesh xử lý mạng thông qua `mesh.update()` gọi trong `loop()`, dùng bộ lập lịch hợp tác (TaskScheduler) bên trong loopTask. Như vậy, mọi đoạn code chặn lâu trong `loop()` đều làm `mesh.update()` bị gọi thưa đi.
– Ví dụ thực tế nên đưa vào báo cáo (theo `claude_2026-09-29_cap-nhat-trang-thai-quyet-dinh-painlessmesh.md`): ở mức 3 Leaf × 3.3 fps, Root phải đẩy khoảng 47 KB/s qua UART 921600 baud, trong khi giới hạn khoảng 92 KB/s. Root mất gần nửa thời gian để ghi UART, `mesh.update()` bị gọi thưa, dữ liệu dồn lại và cả mạng bị đứng.
– Kết quả khắc phục (theo `claude_2026-10-07_ra-soat-tien-do-repo.md`): tăng bộ đệm UART TX lên 16 KB giúp lệnh ghi không còn chặn vòng lặp. Kết quả là uartDrop giảm về 0 và latency giảm từ 280 ms xuống 199 ms (test19 → test21).
– Gợi ý hình: sơ đồ 2 lõi ESP32, gồm core 0 (Wi-Fi/lwIP) và core 1 (loopTask → mesh.update, ghép ảnh, ghi UART), kèm idle task và TWDT.
– Hướng phát triển (nối sang mục ESP-IDF đã ghi trong `claude_2026-09-29...md`): tách việc ghi UART thành một task riêng, nhận dữ liệu qua queue, để mesh.update không bị chặn.

## C3. Tài liệu tham khảo bổ sung (định dạng APA theo [HDTB])

Tiếng Anh:
– Bekmezci, I., Sahingoz, O. K., & Temel, Ş. (2013). Flying ad-hoc networks (FANETs): A survey. *Ad Hoc Networks, 11*(3), 1254–1270. DOI: 10.1016/j.adhoc.2012.12.004
– Espressif Systems. (n.d.). *FreeRTOS (IDF)*. ESP-IDF Programming Guide. Truy cập 09/10/2026, từ https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html
– Espressif Systems. (n.d.). *Watchdogs*. ESP-IDF Programming Guide. Truy cập 09/10/2026, từ https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/wdts.html
– Espressif Systems. (2024). *Arduino Release v2.0.17 based on ESP-IDF v4.4.7*. Truy cập 09/10/2026, từ https://github.com/espressif/arduino-esp32/releases
– Akyildiz, I. F., Wang, X., & Wang, W. (2005). Wireless mesh networks: A survey. *Computer Networks, 47*(4), 445–487. (đã có trong `tài_liệu`)

Cần bổ sung hoặc kiểm tra thêm:
– Sách chính thức của FreeRTOS: Barry, R. *Mastering the FreeRTOS Real Time Kernel* (Real Time Engineers Ltd.). Nên tải bản PDF từ freertos.org để ghi đúng năm và trích các khái niệm task, queue, scheduler.
– Bài khảo sát định tuyến FANET trên PMC (*Routing Schemes in FANETs: A Survey*, PMC6983172) chưa xác minh tác giả và năm. Cần mở bài để ghi đủ thông tin trước khi đưa vào danh mục.

---

# Phần D – Nguyên tắc viết khi sửa báo cáo (bổ sung 09/10/2026)

Các nguyên tắc dưới đây áp dụng cho mọi đoạn văn được viết lại trong các Phần A–C. Các ví dụ "Trước/Sau" dùng số liệu thật trong tài liệu dự án và có ghi nguồn. Những câu "Trước" là câu minh họa do tôi tự đặt, vì file báo cáo LaTeX không có trong Context. Vì vậy cần đối chiếu với câu thật trong báo cáo khi sửa.

## D1. Thay nhận định chung bằng nội dung cụ thể

Quy tắc:
– Mỗi nhận định về hiệu năng, độ ổn định hay chất lượng phải đi kèm chi tiết kiểm chứng được. Chi tiết đó lấy từ nhiệm vụ đề tài, dữ liệu đo, tài liệu tham khảo, hoặc trải nghiệm có thật trong quá trình làm (đã ghi trong nhật ký).
– Khi chưa có căn cứ, ghi rõ là chưa đo hoặc chưa kiểm chứng. Không thay bằng nhận định chung, không tự thêm số liệu.
– Có thể dùng tình huống giả định để giải thích cơ chế. Câu giả định phải mở đầu rõ ràng ("Giả sử…", "Xét ví dụ…") và không chứa số liệu tự đặt.

Ví dụ 1 – kết quả đo:
– Trước: "Hệ thống hoạt động ổn định với độ trễ thấp."
– Sau: "Trong phép đo test21 kéo dài 20 phút với 3 Leaf, hệ thống đạt tổng 5,0 khung hình/giây, độ trễ trung bình 199 ms và không ghi nhận gói bị mất trên đường UART (uartDrop = 0)."
– Nguồn: `claude_2026-10-07_ra-soat-tien-do-repo.md`.

Ví dụ 2 – nêu đúng giới hạn khi chưa có căn cứ:
– Trước: "Hệ thống hoạt động tốt ở nhiều chặng."
– Sau: "Trong các phép đo từ test16 đến test21, số chặng tối đa luôn bằng 1, nên đồ án chưa có số liệu đánh giá hiệu năng ở cấu hình 2 hoặc 3 chặng trên firmware hiện tại."
– Nguồn: như trên.

Ví dụ 3 – tình huống giả định, có đánh dấu:
"Giả sử một node trung gian bị ngắt nguồn đột ngột. Khi đó kết nối TCP không được đóng theo trình tự thông thường, nên painlessMesh chỉ phát hiện mất node sau khi hết thời gian chờ NODE_TIMEOUT."
– Cơ chế này lấy từ `compass_artifact_...md`. Câu ví dụ không kèm số liệu đo của đồ án.

Ví dụ 4 – trải nghiệm có thật:
"Khi thử nghiệm Leaf, heap giảm dần bất thường từ khoảng 154 KB xuống 72 KB sau khoảng 190 khung hình; sau khi đổi sang cổng và cáp USB khác, heap ổn định trở lại."
– Nguồn: `2026-09-16_leaf-bringup-rf-diagnosis-power-plan.md`.

## D2. Bỏ câu lặp ý; mỗi câu một ý chính

Quy tắc:
– Trước khi viết, xác định ý chính của từng câu.
– Nếu nhiều câu nói cùng một việc, giữ câu rõ nhất hoặc gộp chúng thành một câu.
– Chỉ thêm ví dụ hoặc phân tích khi chúng giúp trả lời trực tiếp câu hỏi của mục đó.
– Cách kiểm tra nhanh: tóm tắt mỗi đoạn trong một câu. Nếu không tóm tắt được, đoạn đó đang chứa nhiều ý và cần tách ra. Nếu có hai câu cho ra cùng một bản tóm tắt, một trong hai câu là thừa.

Ví dụ:
– Trước: "painlessMesh là một thư viện mạng mesh. Thư viện này cho phép tạo mạng mesh giữa các ESP32. Nhờ painlessMesh, các ESP32 có thể kết nối với nhau thành mạng mesh."
– Sau: "painlessMesh là thư viện cho phép các ESP32 tự tổ chức thành mạng mesh mà không cần bộ định tuyến trung tâm."

Lưu ý: rà riêng các đoạn mở đầu và kết thúc của mỗi chương. Đây là chỗ dễ lặp lại nội dung đã nêu ở mục 1.2 (mục tiêu) và chương 5 (kết luận).

## D3. Ngôn ngữ học thuật, thuật ngữ nhất quán

Quy tắc:
– Câu phải đủ chủ ngữ và vị ngữ, đúng ngữ pháp. Không dùng tiếng lóng hay khẩu ngữ. Từ ngữ có thể đơn giản nhưng phải chính xác.
– Mỗi khái niệm chỉ dùng **một** thuật ngữ trong toàn báo cáo. Lần đầu xuất hiện, ghi thuật ngữ tiếng Việt kèm tiếng Anh trong ngoặc.
– Ngôi thứ nhất ("tôi") chỉ dùng ở Lời cảm ơn và Lời cam đoan, đúng như mẫu [MAU]. Thân báo cáo dùng chủ ngữ khách quan: "đồ án", "hệ thống", "kết quả đo". Chỉ thuật lại những sự việc đã thực sự diễn ra và có ghi trong nhật ký.

Thay khẩu ngữ (các ví dụ thường gặp, cần rà trong báo cáo):

| Không dùng | Dùng |
|---|---|
| Root bị đơ, bị treo | Root ngừng phản hồi |
| mạng chết, mạng sập | toàn mạng ngừng truyền dữ liệu |
| bắn, đẩy dữ liệu | gửi, truyền dữ liệu |
| chạy ngon, chạy mượt | hoạt động ổn định (kèm số liệu theo D1) |
| rớt mạng | mất kết nối |

Bảng thuật ngữ đề xuất (chọn một cột rồi áp dụng thống nhất trong toàn báo cáo):

| Khái niệm | Thuật ngữ đề xuất | Ghi chú |
|---|---|---|
| Root / node gốc | node gốc (Root) | Ghi "(Root)" ở lần đầu, sau đó dùng một dạng duy nhất |
| Leaf / node lá | node lá (Leaf) | Như trên |
| hop | chặng | "số chặng", không trộn với "hop" |
| self-healing | tự phục hồi | |
| latency | độ trễ | |
| throughput | thông lượng | |
| chunk | gói phân mảnh | |
| KB/s | KB/s | ⚠ Cột `kbps` trong CSV thực chất là KB/s (theo `claude_2026-10-07...md`). Báo cáo phải ghi đúng đơn vị KB/s |
| dấu thập phân | dấu phẩy (5,0) hoặc dấu chấm (5.0) | Chọn một và áp dụng cho cả bảng, hình và văn bản |

## D4. Phân biệt kết quả đo với nhận xét của người viết

Quy tắc:
– Khi trình bày kết quả, tách rõ hai phần: (1) phép đo hoặc tài liệu cho thấy điều gì, và (2) từ đó rút ra nhận xét nào.
– Nhận xét chỉ được phép bao quát đúng điều kiện đã đo: số node, số chặng, thời lượng, phiên bản firmware, số lần lặp.
– Muốn mở rộng nhận xét ra ngoài các điều kiện đó thì phải có thêm phép đo. Nếu chưa có, ghi vào phần hạn chế hoặc hướng phát triển.
– Mẫu câu gợi ý cho chương 4:
  – "Kết quả đo cho thấy…"
  – "Từ kết quả này có thể nhận xét rằng…, trong điều kiện…"
  – "Kết quả chưa đủ để kết luận…, vì…"

Ví dụ 1 – so sánh test19 và test21 (nguồn: `claude_2026-10-07_ra-soat-tien-do-repo.md`):
– Kết quả: với 3 Leaf và số chặng tối đa bằng 1, firmware Root có bộ đệm UART TX 8 KB (test19, 15 phút) ghi nhận uartDrop tăng từ 137 lên 480 và độ trễ trung bình 280 ms. Firmware có bộ đệm 16 KB (test21, 20 phút) ghi nhận uartDrop = 0 và độ trễ trung bình 199 ms.
– Nhận xét được phép: trong hai lần đo này, tăng bộ đệm UART TX đã loại bỏ hiện tượng mất dữ liệu trên đường UART và đi kèm độ trễ thấp hơn.
– Nhận xét vượt quá dữ liệu (không viết):
  – "Tăng bộ đệm giúp giảm độ trễ 30% trong mọi trường hợp." Mỗi cấu hình mới đo một lần, thời lượng hai lần đo khác nhau, và chưa đo ở 2–3 chặng.
  – "Hệ thống đã sẵn sàng triển khai trên drone." Các node đặt cố định trên mặt đất, chưa thử khi di chuyển.

Ví dụ 2 – tự phục hồi:
– Kết quả: trên firmware mới chỉ có 1 sự kiện tự phục hồi, kéo dài 11 220 ms (test19). Các giá trị ghi trên firmware cũ phân tán từ 1 243 đến 19 919 ms (`claude_2026-09-29_cap-nhat-trang-thai-quyet-dinh-painlessmesh.md`).
– Nhận xét được phép: chưa đủ mẫu để nêu thời gian tự phục hồi trung bình của hệ thống.

Ví dụ 3 – kết quả của tài liệu khác:
– Kết quả: Bekmezci và cộng sự (2013) dẫn một nghiên cứu mô phỏng, trong đó GPSR (định tuyến theo vị trí) cho kết quả tốt hơn các giao thức chủ động và phản ứng trong FANET.
– Không suy ra: "painlessMesh kém hơn GPSR" hay "đồ án nên dùng GPSR". Đó là kết quả mô phỏng của tác giả khác, không đo trên hệ thống của đồ án. Chỉ nên viết: định tuyến theo vị trí là một hướng có thể thử nghiệm khi bổ sung module định vị.

## D5. Đưa ra nhận xét riêng và giải thích lý do

Quy tắc:
– Không cần cụm "tôi nghĩ", "theo em". Thay vào đó, trình bày theo trình tự: **vấn đề → đề xuất → lý do đề xuất phù hợp**. Nếu có thể, thêm cách kiểm chứng.
– Lý do phải dựa trên kết quả đo, cơ chế kỹ thuật hoặc tài liệu đã trích dẫn, không dựa trên cảm nhận.

Ví dụ 1 – hướng phát triển cho Root:
– Vấn đề: khi Root phải ghi UART liên tục, `mesh.update()` trong vòng lặp chính bị gọi thưa, và toàn mạng ngừng truyền dữ liệu (`claude_2026-09-29...md`, mục 6).
– Đề xuất: chuyển việc ghi UART sang một task FreeRTOS riêng, nhận dữ liệu qua queue.
– Lý do: theo cơ chế ở mục C2, task ở trạng thái Blocked khi chờ queue không chiếm CPU. Vì vậy vòng lặp chứa `mesh.update()` không còn bị chặn bởi thao tác ghi UART.
– Kiểm chứng: so sánh `loop_max_ms` và uartDrop trước và sau khi thay đổi, trong cùng kịch bản 3 Leaf.

Ví dụ 2 – phương pháp đo tự phục hồi:
– Vấn đề: các giá trị đã ghi phân tán quá rộng, từ 1 243 đến 19 919 ms.
– Đề xuất: đo lặp từ 5 đến 10 lần và tách riêng hai kịch bản: đổi tuyến qua node trung gian, và mất tín hiệu rồi kết nối lại (`claude_2026-10-07...md`, Việc ưu tiên 3).
– Lý do: hai kịch bản này kích hoạt hai cơ chế phát hiện khác nhau. Gộp chung sẽ làm giá trị trung bình không đại diện cho kịch bản nào.

## D6. Viết lại đoạn chung chung

Quy tắc: đoạn văn chỉ khẳng định một hoạt động "quan trọng" hay "có nhiều lợi ích" phải được viết lại để trả lời bốn câu hỏi:
1. Hoạt động đó là gì?
2. Giúp ai, hoặc giúp thành phần nào của hệ thống?
3. Giải quyết vấn đề gì?
4. Được thực hiện như thế nào?

Có thể thêm một ví dụ để người đọc dễ hình dung. Ví dụ giả định phải được đánh dấu theo D1.

Ví dụ:
– Trước: "Mạng mesh đóng vai trò rất quan trọng và mang lại nhiều lợi ích cho drone và robot."
– Sau: "Trong mạng mesh, mỗi node vừa gửi dữ liệu của chính nó vừa chuyển tiếp dữ liệu cho node lân cận. Nhờ đó, drone hoặc robot nằm ngoài tầm phát trực tiếp của trạm gốc vẫn gửi được dữ liệu qua một chuỗi liên kết đa chặng (Bekmezci và cộng sự, 2013). Trong đồ án, chức năng này do thư viện painlessMesh đảm nhiệm: các node ESP32-CAM tự kết nối thành cấu trúc cây, và ảnh từ node lá (Leaf) được chuyển về node gốc (Root)."
– Ví dụ giả định có thể thêm: "Giả sử một robot hoạt động trong tòa nhà và tường chắn tín hiệu trực tiếp tới node gốc. Robot vẫn gửi được ảnh nếu có một node trung gian đặt ở hành lang nằm trong tầm phát của cả hai."

Các đoạn nên rà theo D6: mục 1.1 (đặt vấn đề), phần mở đầu chương 2, và mục ý nghĩa thực tiễn.

## D7. Bước kiểm tra sau khi viết lại mỗi mục

– [ ] Mỗi nhận định đều có số liệu, nguồn hoặc nhật ký đi kèm; chỗ nào chưa có căn cứ thì đã ghi rõ là chưa đo (D1).
– [ ] Mọi tình huống giả định đều được đánh dấu và không chứa số liệu tự đặt (D1).
– [ ] Không còn câu lặp ý; mỗi đoạn tóm tắt được trong một câu (D2).
– [ ] Không có khẩu ngữ; thuật ngữ khớp với bảng D3; không dùng "tôi" trong thân báo cáo (D3).
– [ ] Mỗi kết quả được tách khỏi nhận xét; mọi nhận xét đều nằm trong điều kiện đã đo (D4).
– [ ] Mọi đề xuất có đủ vấn đề, đề xuất và lý do; không dùng "tôi nghĩ" (D5).
– [ ] Không còn đoạn chỉ khẳng định "quan trọng" hay "nhiều lợi ích" mà không trả lời bốn câu hỏi ở D6.
