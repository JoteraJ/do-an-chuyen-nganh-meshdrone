# Báo cáo đồ án chuyên ngành (Khoa Điện – Điện tử, TDTU)

1. Biên dịch bằng **XeLaTeX**. Dòng đầu `main.tex` đã có `% !TeX program = xelatex`; file `latexmkrc` cũng đặt sẵn XeLaTeX.
   - TeXstudio: Options > Configure TeXstudio > Build > Default Compiler = XeLaTeX, Default Bibliography Tool = BibTeX.
   - Overleaf: Menu > Compiler > XeLaTeX.
2. Font: Times New Roman nếu máy có; nếu không có, `cauhinh.tex` tự dùng TeX Gyre Termes (bản tương đương, đủ dấu tiếng Việt).
3. Sửa tên GVHD, lớp, MSSV, tên đề tài trong `thongtin.tex`.
4. Hình đặt trong thư mục `figures/`.
5. Phiếu nhiệm vụ và lịch trình có chữ ký: lưu bản scan thành `figures/nhiemvu.pdf` và `figures/lichtrinh.pdf`, trang scan tự thay cho trang tạm.
6. Tài liệu tham khảo: thêm vào `references.bib`, trích dẫn kiểu APA bằng `\citep{khoa}` → (Tác giả, năm) hoặc `\citet{khoa}` → Tác giả (năm). Kiểu danh mục: `apalike-vn.bst`.
7. Chỗ tô vàng (`\cansua{...}`) là phần còn thiếu, cần bổ sung trước khi nộp.
