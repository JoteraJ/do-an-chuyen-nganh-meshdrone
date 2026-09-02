# Tổng Hợp Cuộc Trò Chuyện: Mạng Mesh Cho Drone & Giao Thức B.A.T.M.A.N

## Câu hỏi 1: Cơ bản về Mạng Mesh cho Drone và Cải thiện hệ thống
**Người dùng:** 
> "Trong hệ thống mạng mesh cho dron nếu một con đứng ở giữa bị chết thì những con ở ngoài sẽ như thế nào có đồng loạt chết theo không và có phương pháp nào để cải thiện vấn đề trên? Có phải mạng mesh tôi đang làm là một dang bust đúng ?, bạn có thể đưa ra vài vấn đề cần cải thiện trong đề tài này cho tôi tham khảo không"

**AI trả lời:**
1. **Mạng Mesh không phải mạng Bus:**
   - Mạng Bus dùng chung 1 trục đường truyền cứng, đứt là chết toàn bộ phía sau.
   - Mạng của bạn là các drone nối tiếp nhau, thực chất là mạng **Linear (Đường thẳng) hoặc Daisy-chain** – một dạng suy biến của mạng Mesh.

2. **Nếu Drone ở giữa bị "chết":**
   - Về mặt vật lý: Các drone ngoài cùng **KHÔNG** chết theo, chúng vẫn hoạt động bình thường.
   - Về mặt mạng: Nếu drone chết là "cây cầu" duy nhất, các drone ngoài sẽ bị cô lập (orphan network) và mất tín hiệu về trạm. Ngược lại, nếu là mạng Mesh thực thụ có mật độ dày, các drone tự tìm đường vòng qua các drone khác (self-healing).

3. **Hướng cải thiện cho đề tài (Đồ án Điện tử - Viễn thông):**
   - **Giao thức định tuyến động:** Ứng dụng AODV, OLSR, B.A.T.M.A.N.
   - **Tăng mật độ Node (Redundancy):** Bay theo cụm (swarm) hoặc zig-zag để tăng liên kết chéo.
   - **Thuật toán Tự điều chỉnh Vị trí (Topology Control):** Lập trình để drone tự bù đắp khoảng trống khi có 1 node rớt.
   - **Quản lý Năng lượng (Energy-Aware Routing):** Ưu tiên node chuyển tiếp còn nhiều pin.
   - **Chất lượng Dịch vụ (QoS):** Ưu tiên băng thông cho dữ liệu điều khiển thay vì video khi mạng suy hao.

---

## Câu hỏi 2: Giao thức B.A.T.M.A.N
**Người dùng:** 
> "Giao thức B.A.T.M.A.N hoạt động như thế nào trong mạng Mesh và cách cài đặt cơ bản?"

**AI trả lời:**
1. **Cách B.A.T.M.A.N hoạt động:**
   - Chạy ở **Tầng 2 (MAC Layer)**, biến mạng thành Switch ảo khổng lồ.
   - Không cần biết toàn bộ đường đi, chỉ quan tâm: *"Next-hop (trạm kế tiếp) tốt nhất là ai?"*
   - Phát sóng **OGM** liên tục để đánh giá chất lượng liên kết (TQ - Transmission Quality) và tự động cập nhật hướng truyền dữ liệu khi có sự cố.

2. **Cài đặt cơ bản (batman-adv trên Linux):**
   - **Bước 1:** Cài đặt công cụ quản lý: `sudo apt update && sudo apt install batctl`
   - **Bước 2:** Kích hoạt module trong nhân: `sudo modprobe batman-adv`
   - **Bước 3:** Chuyển card Wi-Fi (vd: wlan0) sang chế độ Ad-hoc:
     ```bash
     sudo ip link set wlan0 down
     sudo iwconfig wlan0 mode ad-hoc
     sudo iwconfig wlan0 essid my-mesh-network
     sudo iwconfig wlan0 channel 8
     sudo ip link set wlan0 mtu 1532
     sudo ip link set wlan0 up
     ```
   - **Bước 4:** Gắn card vật lý vào mạng B.A.T.M.A.N: `sudo batctl if add wlan0`
   - **Bước 5:** Khởi động và gán IP cho giao diện ảo `bat0`:
     ```bash
     sudo ip link set up dev bat0
     sudo ip addr add 192.168.1.1/24 dev bat0
     ```
