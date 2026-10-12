# Template LaTeX — Đồ án chuyên ngành (Khoa Điện – Điện tử, TDTU)

1. Overleaf: New Project > Upload Project > chọn file .zip này.
2. Menu > Compiler: chọn **XeLaTeX** (file latexmkrc cũng đã đặt sẵn XeLaTeX).
3. Sửa tên GVHD, lớp, MSSV, tên đề tài trong `thongtin.tex`.
4. Hình: thả file h1.png ... h6.png, dashboard.png vào thư mục `figures/`, hình tự hiện thay cho khung vàng.
5. Phiếu nhiệm vụ có chữ ký: đặt `figures/nhiemvu.pdf`, bỏ dấu % ở dòng \includepdf trong `frontmatter/nhiemvu.tex`.
6. Tài liệu tham khảo: thêm vào `references.bib`, trích dẫn bằng \citep{khoa} hoặc \citet{khoa}.
7. Chỗ tô vàng (\cansua{...}) là phần còn thiếu. Khi xong, trong `cauhinh.tex` đổi thành
   \newcommand{\cansua}[1]{#1} để tắt tô vàng.
