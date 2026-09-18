# Giải pháp cho mất khung hình đa-node ESP32-CAM trên painlessMesh, thiết kế FrameAssembler đa luồng, và đo self-healing time

## TL;DR
- Việc drop tăng từ ~0.5% (1 Leaf) lên ~80% (2 Leaf) **KHÔNG** phải hành vi bình thường của WiFi; đó là dấu hiệu bão frame/nghẽn kênh CSMA/CA cộng với nút thắt ở FrameAssembler một-buffer. painlessMesh broadcast là công cụ **sai** cho video đa-node vì mỗi ảnh bị flood qua nhiều edge TCP có ACK trên cùng một collision domain; giải pháp mạnh nhất là chuyển luồng ảnh sang **ESP-NOW** (đo được ~18.6x lợi airtime so với painlessMesh trong chính issue #418 của Alteriom/painlessMesh).
- Ưu tiên trong vài ngày tới: (1) sửa FrameAssembler thành `Map` theo `node_id` với buffer song song + timeout (bắt buộc, rẻ, có thể là nguyên nhân chính); (2) staggering gửi lệch pha giữa 2 Leaf dùng `mesh.getNodeTime()` để tránh tranh chấp đồng thời; (3) thử nghiệm ESP-NOW cho payload ảnh; (4) thêm ACK/retransmit nhẹ ở tầng chunk.
- Cho mục C (self-healing 3 node): trong cây painlessMesh **không có loop** nên "mất node trung gian" thực chất là mất parent của Leaf; khi tắt nguồn đột ngột, việc phát hiện bị chi phối bởi **NODE_TIMEOUT = 10s**, và log nên dùng `mesh.getNodeTime()` (đồng bộ, µs, cuộn vòng mỗi ~71 phút) để so sánh chéo giữa các node.

## Key Findings

1. **Nhảy vọt 0.5%→80% khi thêm 1 node là bất thường và có 2 nguyên nhân cộng hưởng.** painlessMesh dùng JSON qua TCP, mỗi broadcast bị re-serialize và flood qua từng edge; issue #418 (Alteriom/painlessMesh) đo được overhead envelope 2.70x, ~600 heap alloc/s, và một workload 8-node chiếm 39.2% của collision domain 24 Mbps với 1680 frame/s (một nửa là TCP ACK thuần). Cộng đồng nhất quán khuyến cáo giữ ≤1 message/giây và payload <250 byte cho painlessMesh — bạn đang chạy ngược hoàn toàn khuyến cáo đó (hàng trăm chunk/giây).

2. **ESP-NOW là con đường có bằng chứng mạnh nhất.** Trong chính issue #418, giải pháp ESP-NOW broadcast của người báo cáo đạt 18.6x lợi airtime so với painlessMesh vì "một MAC frame không ACK thay cho bảy edge-traversal có ACK". Thư viện ESPNowCam (github.com/hpsaturn/ESPNowCam) có bảng hiệu năng đo thực: TTGO T-Journal QVGA JPGQ12 ~11 FPS (STABLE), XIAO Sense S3 ~11 FPS, Freenove S3 ~10 FPS, HVGA ~6 FPS (CNX Software 24/02/2025 xác nhận "around 9 to 11 FPS with a QVGA camera and 6 FPS with an HVGA camera"). Đánh đổi: payload tối đa 250 byte (ESP-NOW v1, ESP-IDF <5.4).

3. **Giảm CHUNK_SIZE 1024→512 KHÔNG chắc cải thiện, thậm chí có hại.** Lý thuyết 802.11 cho thấy xác suất va chạm `p` không phụ thuộc độ dài gói; gói nhỏ chỉ giảm thời gian mất khi va chạm nhưng tăng số frame và overhead header/MAC — mà chính số lượng frame mới là thủ phạm. Với contention đa-node, giảm chunk size làm **tăng** số frame → phản tác dụng.

4. **FrameAssembler một-buffer gần như chắc chắn là một nút thắt lớn.** Pattern chuẩn là `Map<node_id:frame_id, PartialFrame>` giữ nhiều buffer song song, mỗi buffer có timeout riêng, giới hạn số buffer đồng thời để chống tràn RAM.

5. **Self-healing painlessMesh: NODE_TIMEOUT=10s, SCAN_INTERVAL=30s, topology sync ~3s.** Khi tắt nguồn đột ngột không có TCP FIN sạch, callback `onDroppedConnection` có thể mất tới ~10s (watchdog NODE_TIMEOUT) mới kích hoạt.

## Details

### A) Giảm mất gói/mất khung khi nhiều ESP32-CAM cùng gửi ảnh (single-hop)

**A.0 — Chẩn đoán: con số 0.5%→80% nói lên điều gì?**
Đây KHÔNG phải suy giảm tuyến tính bình thường của 802.11. Về lý thuyết CSMA/CA (Bianchi và các mô hình saturated throughput), khi tăng từ 1 lên 2 trạm bão hòa, throughput tổng hầu như không đổi và mỗi trạm nhận ~50% băng thông; drop chỉ tăng vừa phải. Một cú nhảy lên 80% drop chỉ ra hiện tượng "sụp đổ" do: (a) hàng đợi gửi/buffer nội bộ tràn khi hai luồng chunk tốc độ cao chồng lên nhau; (b) painlessMesh flood + re-serialize khiến số frame nhân đôi và vượt ngưỡng phi tuyến của backoff (issue #418 lưu ý baseline 39.2% airtime đã "gần nơi hành vi collision/backoff của CSMA/CA trở nên phi tuyến"); và/hoặc (c) FrameAssembler một-buffer bị luồng thứ hai ghi đè liên tục.

Bằng chứng telemetry của bạn (UART sạch, chỉ 5/16069 bad CRC) xác nhận lỗi nằm ở tầng mesh không dây + khâu ghép, không phải UART. Vì middleware ghép OK chỉ 99/444 (~78% hủy) gần khớp với drop tầng telemetry (~82%), rất có thể phần lớn tổn thất là mất chunk trên sóng, **nhưng** FrameAssembler một-buffer đang khuếch đại nó (chỉ cần thiếu 1 chunk là hủy cả frame, và hai luồng đan xen làm mất chunk "ảo"). Đây là lý do phải tách biến số bằng cách sửa FrameAssembler trước (mục B).

**A.1 — Staggering (gửi lệch pha) bằng `mesh.getNodeTime()`, không cần server điều phối.**
painlessMesh duy trì một timebase chung (đồng bộ kiểu SNTP, mục tiêu độ chính xác 5ms — hằng số `TIME_SYNC_ACCURACY 5000` trong `ntp.hpp`; wiki nói "better than 10 ms"). Mỗi Leaf gọi `mesh.getNodeTime()` (microsecond, chung toàn mesh) và tính slot của mình: ví dụ với chu kỳ khung T (ms) và N node, node thứ i chỉ bắt đầu gửi chunk khi `(getNodeTime()/1000) % T` rơi vào cửa sổ `[i*T/N, (i+1)*T/N)`. Đây là TDMA nhẹ ở tầng ứng dụng: hai Leaf tự lệch pha mà không cần PC điều phối. Bằng chứng painlessMesh đã dùng chính kỹ thuật jitter này nội bộ: time-sync thêm "random delay (±35%)" để tránh va chạm đồng bộ (wiki mesh-protocol). Khả thi cao cho đồ án; chỉ cần vài dòng dùng TaskScheduler + `getNodeTime()`. Lưu ý `getNodeTime()` cuộn vòng mỗi ~71 phút → dùng số học modulo an toàn với wraparound.

**A.2 — Tinh chỉnh tham số painlessMesh.**
Các hằng số liên quan: message bị hard-limit ~1400 byte (issue #22 GitLab painlessMesh), `MAX_MESSAGE_QUEUE=50`, `MIN_FREE_MEMORY=4000` (đã bị deprecate thành no-op ở fork Alteriom v1.10). Tinh chỉnh khả thi:
- (i) **MSS batching**: gộp nhiều chunk vào một message tới sát giới hạn TCP_MSS/1400 byte — issue #418 đo được 1.60x airtime (tương đương chuyển sang UDP nhưng không phải đổi transport, chỉ ~10 dòng);
- (ii) **payload nhị phân/base64** thay JSON array: base64 = 219 vs 346 wire byte cho 128 byte data → tổng ~2.90x khi kết hợp batching.

Đánh giá: hữu ích nhưng chỉ đạt ~2.9x, KHÔNG đủ cho video đa-node — chỉ là band-aid. Maintainer #418 kết luận thẳng: painlessMesh vẫn là "công cụ sai cho telemetry đa-node 30Hz" vì "một collision domain chung, flooding theo cây per-edge".

**A.3 — painlessMesh/ESP-WIFI-MESH broadcast vs ESP-NOW.**
ESP-WIFI-MESH có upstream flow control bằng "receiving window" (con phải xin cửa sổ trước khi gửi lên cha) và `esp_mesh_set_xon_qsize` (mặc định 32, min 16) — tức stack native đã có cơ chế chống tràn buffer mà painlessMesh không có. Nhưng cho bài toán truyền ảnh nhiều node cùng hop, **ESP-NOW thắng rõ**:
- Unicast có ACK MAC tự động + callback báo thành/bại; broadcast không ACK (nhanh hơn nhưng phải tự lo mất gói); độ trễ single-hop trong khoảng ~10ms (bit rate mặc định ESP-NOW là 1 Mbps theo ESP-IDF esp_now guide).
- ESP-NOW không đi qua lớp mesh/TCP nên loại bỏ flood và re-serialize.
- **Nhược điểm/ràng buộc** (tài liệu Espressif ESP-FAQ): payload v1.0 = 250 byte (`ESP_NOW_MAX_DATA_LEN`); từ ESP-IDF v5.4 hỗ trợ v2.0 tối đa 1470 byte/packet (bản đầu v5.4/v5.4.1 dùng 1490, sửa còn 1470 từ v5.4.2); unicast tối đa 20 peer đồng thời (`ESP_NOW_MAX_TOTAL_PEER_NUM`), encrypted peer mặc định 7/tối đa 17 trên ESP32; broadcast không mã hóa; multi-hop phải tự xây addressing/relay.

**A.4 — Giảm CHUNK_SIZE có lợi không?**
Bằng chứng lý thuyết (bài "Collision probability in saturated IEEE 802.11 networks"): trong 802.11 bão hòa, `p` "không phụ thuộc độ dài gói" mà phụ thuộc CWmin và số trạm n ("halving W is like doubling n"). Gói nhỏ hơn chỉ giảm thời gian phí khi va chạm, nhưng với per-packet overhead cố định (preamble, DIFS, spacing), throughput tổng **giảm** khi gói nhỏ và số frame **tăng**. Vì thủ phạm của bạn là số frame/airtime, giảm 1024→512 sẽ làm đôi số frame và có khả năng làm tệ hơn. **Khuyến nghị: GIỮ hoặc TĂNG chunk** (nếu chuyển ESP-NOW v2/1470 byte hoặc gộp chunk), không giảm.

**A.5 — ACK/retransmit nhẹ ở tầng chunk.**
Thiết kế đơn giản, nhẹ: mỗi frame gắn bitmap các chunk đã nhận; sau khi gửi hết một frame, Leaf chờ một "NACK gộp" từ Root liệt kê các `chunk_id` thiếu (chỉ 1 message nhỏ/frame), rồi chỉ gửi lại các chunk đó, tối đa 1-2 vòng. Cách này tránh ACK từng chunk (sẽ gây bão ACK — chính PR #383 cảnh báo "N-node broadcast tạo N unicast ACK gần đồng thời hội tụ về sender gây timeout giả", và đề xuất thêm "ACK jitter"). Với ESP-NOW unicast đã có ACK MAC sẵn + callback, chỉ cần retransmit khi callback báo fail. Espressif chính thức khuyến nghị mô hình này (ESP-IDF esp_now): "gán sequence number để loại trùng; nếu chờ ack timeout thì retransmit; gửi gói kế tiếp sau khi callback gửi của gói trước đã trả về".

**A.6 — Benchmark đối chiếu.**
ESP-WIFI-MESH (docs Espressif): per-hop latency 10-30ms, throughput phụ thuộc packet error rate và hop count. Một nghiên cứu mesh ESP32/ESP8266 ("Evaluation Method of Mesh Protocol over ESP32 and ESP8266", ResearchGate) đo throughput trung bình chỉ ~110.5 kbps. Bảng so sánh mesh của Espressif (esp-techpedia) cho ESP-NOW single-hop throughput "< 0.5 Mbps", delay "within 10 ms". Những con số này khẳng định mesh WiFi ESP32 không thiết kế cho nhiều luồng video song song.

### B) FrameAssembler đa luồng ở middleware Node.js

**Pattern chuẩn (khuyến nghị áp dụng ngay):**
- **Map theo khóa kép:** `Map<string, PartialFrame>` với key = `${node_id}:${frame_id}`. Mỗi `PartialFrame` giữ: mảng/Buffer slots theo `chunk_id`, `total_chunks`, số chunk đã nhận, `lastUpdate` timestamp. Việc tách theo `node_id` đảm bảo luồng của Leaf A không bao giờ ghi đè buffer đang ghép dở của Leaf B — đây chính là lỗi của thiết kế một-buffer hiện tại.
- **Hoàn tất:** khi `count == total_chunks`, ghép `Buffer.concat` theo thứ tự `chunk_id`, emit ảnh, xóa entry.
- **Timeout/dọn rác:** một timer định kỳ (ví dụ mỗi 200-500ms) quét Map, xóa các entry có `now - lastUpdate > FRAME_TIMEOUT` (ví dụ 500ms-1s ở 2-5 FPS). Đây là mô hình reassembly/timeout kinh điển của IP fragmentation (giữ fragment trong RAM tới khi đủ hoặc hết hạn rồi loại bỏ).
- **Giới hạn tài nguyên (chống DoS/tràn RAM):** giới hạn số buffer đồng thời (ví dụ tối đa 2×số node hoặc N frame/node); khi vượt, drop frame cũ nhất (LRU). Tài liệu IP reassembly cảnh báo rõ nguy cơ "buffer gigabytes fragment dở → DoS" nếu không giới hạn.
- **Streaming parser cho UART:** vì UART/serialport có thể cắt/gộp gói, cần accumulator buffer + parser tách message hoàn chỉnh theo header/độ dài **trước khi** đưa vào FrameAssembler (giống pattern streaming parser xử lý fragmentation TCP: tích lũy vào buffer, chỉ dispatch message hoàn chỉnh). Bạn đã có tầng parser sạch (bad CRC thấp) nên chỉ cần đảm bảo FrameAssembler phía sau là đa-buffer.

**Đánh giá khả thi:** rất cao, thuần JavaScript, vài chục dòng, không cần thư viện ngoài. Đây là thay đổi rẻ nhất và có khả năng cao nhất giảm tỉ lệ hủy frame — nên làm **đầu tiên** để tách biến số phần cứng khỏi phần mềm.

*Khung mã tham khảo:*
```js
const frames = new Map(); // key = `${nodeId}:${frameId}`
const FRAME_TIMEOUT = 800; // ms
const MAX_FRAMES = 8;      // giới hạn buffer đồng thời

function onChunk({nodeId, frameId, chunkId, totalChunks, payload}) {
  const key = `${nodeId}:${frameId}`;
  let f = frames.get(key);
  if (!f) {
    if (frames.size >= MAX_FRAMES) evictOldest();
    f = { slots: new Array(totalChunks), got: 0, total: totalChunks, lastUpdate: Date.now() };
    frames.set(key, f);
  }
  if (!f.slots[chunkId]) { f.slots[chunkId] = payload; f.got++; }
  f.lastUpdate = Date.now();
  if (f.got === f.total) { emitImage(nodeId, Buffer.concat(f.slots)); frames.delete(key); }
}
setInterval(() => {
  const now = Date.now();
  for (const [k, f] of frames) if (now - f.lastUpdate > FRAME_TIMEOUT) frames.delete(k);
}, 250);
```

### C) Thiết kế test đo self-healing time cho mesh multi-hop painlessMesh (3 node)

**Điểm mấu chốt học thuật cần nêu rõ:** painlessMesh xây dựng topology **cây/sao KHÔNG có vòng lặp**, mỗi node chỉ có 1 đường tới root. Vì vậy trong kịch bản Leaf→Relay→Root, tắt Relay KHÔNG phải "tìm đường thay thế" (không tồn tại) mà là **mất parent → Leaf phải rescan và rejoin** mesh. Cần viết chính xác điều này trong báo cáo để đúng chuẩn học thuật; nếu muốn thí nghiệm "re-route qua đường thay thế" thật sự thì phải dùng 4 node (2 đường lên root) hoặc chuyển sang ESP-WIFI-MESH native.

**Bố trí vật lý reproducible (3 node, đảm bảo đúng 1 đường):**
- Chuẩn vàng học thuật là dùng hộp chắn RF + bộ suy hao (RF matrix switch) như testbed **MeshTest** (Laboratory for Telecommunications Sciences), **Mini-Mesh** (ĐH Rostock, dùng băng dính đồng bọc node + tắt ANI của driver ath9k để có kết quả khoảng cách lặp lại được), hoặc giải pháp thương mại Quintech — kiểm soát chính xác attenuation giữa từng cặp node. Không khả thi cho đồ án sinh viên nhưng nên trích dẫn để biện luận về tính reproducible.
- **Phương án khả thi cho sinh viên:** đặt Root và Leaf đủ xa / có vật cản (tường, tấm kim loại) để RSSI Root↔Leaf dưới ngưỡng kết nối, trong khi Relay đặt giữa sao cho RSSI Relay↔Root và Relay↔Leaf đều mạnh. Ép topology bằng cách bật nguồn theo thứ tự: Root trước, Relay kế (nối Root), Leaf cuối (chỉ thấy Relay). Ghi lại `mesh.subConnectionJson()` để xác nhận Leaf thực sự là con của Relay (2 hop) **trước mỗi lần đo**.
- **Gợi ý RSSI:** giữ liên kết muốn duy trì ở mức mạnh (≈ -55 đến -70 dBm) và liên kết muốn chặn ở mức rất yếu/không thấy (< -85 dBm hoặc ngoài tầm). ESP-WIFI-MESH dùng ngưỡng RSSI beacon để tránh liên kết upstream yếu — nêu cơ chế này để biện luận cho việc chọn ngưỡng.
- **Lặp lại ≥5 lần** mỗi kịch bản và lấy trung bình ± độ lệch chuẩn (chuẩn như bài "Gateway-Free LoRa Mesh on ESP32", MDPI Sensors 2025: "mỗi kịch bản chạy 5 lần độc lập").

**Đo và log timestamp chuẩn xác:**
- Đăng ký cả 3 callback: `onDroppedConnection`, `onNewConnection`, `onChangedConnections`.
- **Timestamp:** dùng `mesh.getNodeTime()` (microsecond, đồng bộ toàn mesh, kiểu SNTP) để so sánh chéo giữa các node; KHÔNG dùng `millis()` để so chéo (đồng hồ local không đồng bộ giữa 2 board). Lưu ý `getNodeTime()` "rolls over 71 minutes from startup of the first node" → phải xử lý wraparound; và `onNodeTimeAdjusted(int32_t offset)` có thể gây bước nhảy clock nên với khoảng thời gian đo **trên cùng một node** (ví dụ khoảng drop→rejoin của chính Leaf) có thể dùng `millis()` (đơn điệu, an toàn) để tránh nhiễu do resync.
- **Định nghĩa:** self-healing time = t(đường mới hoạt động tại Leaf) − t(`onDroppedConnection` tại Leaf). Nên bổ sung xác nhận "đường mới hoạt động thực sự" bằng cách Root gửi bản tin đánh số liên tục và Leaf log timestamp khi nhận lại được gói đầu tiên sau sự cố (tương tự phương pháp bản tin đánh số của HERMES, hoặc GPIO-toggle + logic analyzer dùng trong các bài đo latency mesh 2.4GHz) — vì `onNewConnection` chỉ báo đã có liên kết TCP, chưa chắc route đã thông suốt tới root.

**Con số tham chiếu để đối chiếu (từ source painlessMesh và tài liệu Espressif):**
- Hằng số nội bộ painlessMesh (`configuration.hpp`/`ntp.hpp`): `NODE_TIMEOUT = 10 * TASK_SECOND` (10s), `SCAN_INTERVAL = 30s`, `TIME_SYNC_INTERVAL = 1 phút` (steady-state), nodeSync = `NODE_TIMEOUT/2` (5s phía STA, ×2=20s phía AP), topology sync ~3s (wiki).
- Độ trễ `onDroppedConnection`: nếu TCP đóng sạch (FIN), callback kích qua task ~1s (`TASK_SECOND`); nếu **tắt nguồn đột ngột (không FIN)**, phải chờ tới watchdog `NODE_TIMEOUT` ~10s.
- Healing đo được (nguồn blog journal.hexmos.com, **không peer-review**): sau lỗi node trung gian **3.46s**, sau lỗi root **4.18s**.
- Đối chiếu ESP-WIFI-MESH native (docs Espressif): healing child node break down **<5s**, root **<10s**; được kỹ sư Espressif xác nhận trên ESP32 Forum (t=10233): node layer 3 mất ~10s để phát hiện parent disconnected (chi phối bởi `esp_mesh_set_ap_assoc_expire`), và "không thể giảm healing time dưới 5s — đó là thiết kế".

## Recommendations

**Giai đoạn 1 — trong 1-2 ngày (rẻ, tách biến số):**
1. Viết lại FrameAssembler thành `Map<node_id:frame_id, PartialFrame>` + timeout quét định kỳ + giới hạn buffer (khung mã ở mục B). Đo lại drop 2-Leaf. Nếu tỉ lệ hủy frame giảm mạnh → nút thắt chính là phần mềm, không phải sóng.
2. Thêm staggering bằng `getNodeTime()` để 2 Leaf gửi lệch pha. Đo lại.

**Giai đoạn 2 — 2-3 ngày (nếu vẫn drop cao trên sóng):**
3. Prototype luồng ảnh qua **ESP-NOW** (unicast Leaf→Root, tận dụng ACK MAC + callback), chunk ≤250 byte (v1) hoặc bật ESP-NOW v2 (IDF ≥5.4, tối đa 1470 byte, dùng 1470 từ v5.4.2). Giữ painlessMesh cho telemetry/điều khiển. Đây là thay đổi kiến trúc có tỉ số lợi ích cao nhất (bằng chứng 18.6x trong #418; ESPNowCam đạt ~9-11 FPS QVGA).
4. Thêm NACK gộp + retransmit tối đa 1-2 vòng cho chunk thiếu (tránh ACK từng chunk).

**Giai đoạn 3 — hoàn thiện báo cáo:**
5. Chạy thí nghiệm self-healing 3-node ≥5 lần, log bằng `getNodeTime()` (+ `millis()` cho duration cùng-node), báo cáo trung bình ± độ lệch chuẩn, đối chiếu với `NODE_TIMEOUT=10s` và các mốc Espressif (<5s/<10s).

**Ngưỡng quyết định:**
- Nếu sau Giai đoạn 1, drop 2-Leaf giảm về **<10%** → giữ painlessMesh, tối ưu batching/staggering là đủ.
- Nếu vẫn **>40%** drop trên sóng dù buffer đã sửa → **bắt buộc** chuyển ESP-NOW cho ảnh; painlessMesh không phù hợp cho video đa-node (kết luận thẳng của maintainer trong #418).
- Nếu drop nằm giữa 10-40% → kết hợp staggering + MSS batching + payload nhị phân trước khi quyết định đổi kiến trúc.

## Caveats
- Con số **3.46s/4.18s** healing là từ blog kỹ thuật (journal.hexmos.com), KHÔNG peer-review và không nêu rõ số lần thử hay callback dùng để đo; chỉ nên dùng làm tham chiếu định hướng, không trích như kết quả học thuật.
- Con số **18.6x** và **39.2% airtime** trong issue #418 là từ **mô hình** 802.11g @24Mbps + đo trên host (EPYC), chưa đo trên phần cứng ESP32 thật (chính tác giả issue ghi rõ đây là rủi ro "severity: high": "No on-target measurement exists"). Dùng như định hướng, không như số tuyệt đối — nên tự đo frame count trên 2 board ESP32 để xác nhận.
- ESP-NOW và painlessMesh cùng dùng WiFi 2.4GHz; chạy đồng thời trên cùng chip cần **cùng kênh** và có thể gây nhiễu/mất gói lẫn nhau — cần test kỹ khi kết hợp (ESP32 Forum ghi nhận packet loss chéo Ethernet/UDP khi ESP-NOW đang truyền).
- Fork **Alteriom/painlessMesh** (GitHub) có thêm nhiều tính năng (delivery confirmation, message queue, `setTcpRetryConfig`) và một số hành vi khác bản **gmag11/GitLab gốc**; kiểm tra bản bạn đang dùng trước khi trích hằng số (ví dụ `MIN_FREE_MEMORY` đã thành no-op ở Alteriom v1.10).
- Giới hạn ESP-NOW: unicast tối đa 20 peer (`ESP_NOW_MAX_TOTAL_PEER_NUM`), payload 250 byte (v1)/1470 byte (v2 từ IDF v5.4.2), encrypted peer 7 mặc định/17 tối đa — là ràng buộc thật cần thiết kế quanh khi mở rộng số Leaf.
- Trong cây painlessMesh không có đường thay thế cho 3 node; nếu báo cáo cần đo "re-route qua đường dự phòng" đúng nghĩa, phải thiết kế ≥4 node hoặc dùng ESP-WIFI-MESH native (có cơ chế bầu lại parent theo RSSI beacon).

*Nguồn tham khảo chính: GitHub Alteriom/painlessMesh issue #418 & PR #383; painlessMesh GitLab (`configuration.hpp`, `ntp.hpp`, wiki mesh-protocol & api); tài liệu Espressif ESP-WIFI-MESH & ESP-NOW (ESP-IDF Programming Guide, ESP-FAQ, esp-techpedia mesh-comparison); "Collision probability in saturated IEEE 802.11 networks" (ResearchGate); "Evaluation Method of Mesh Protocol over ESP32 and ESP8266"; "Gateway-Free LoRa Mesh on ESP32" (MDPI Sensors 2025, PMC12526805); MeshTest & Mini-Mesh testbed papers; ESPNowCam (hpsaturn) & CNX Software; ESP32 Forum threads (t=10233, t=11711, t=37757); journal.hexmos.com/painlessmesh (non-peer-reviewed).*