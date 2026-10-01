# Bao cao fix on dinh — 2026-10-01

Nhanh `fix/on-dinh-2` (tach tu `feature/on-dinh` @ `547b685`). Khong push, khong sua `main`.
Firmware sau cung: Root `2026-10-01b` (lan 3, Muc 9–12), Leaf `2026-10-01a`.
Muc tieu: ON DINH, KHONG tang tai. Giu nguyen `CAPTURE_INTERVAL_MS=600`, `STAGGER_ENABLE`,
`CHUNK_SIZE`, `MAX_FRAME_SIZE`, so slot, `REASSEMBLY_TIMEOUT_MS`, baud.

Ket qua nhanh:

| Muc | Trang thai |
|---|---|
| 0. Chuyen thu muc (chore) | Xong — 6 commit (4 + 2 archive), moi file la rename R100 |
| 1. Tran so "im lang" | Xong |
| 2. Hai slot cung (node, frame) | Xong (lan 2, theo huong da duyet: bo chunk tre) |
| 3. Tat HTTP tren Root | Xong |
| 4. Telemetry/LOG khong chan | Xong |
| 5. Do dac nguyen nhan reset | Xong (rx_q khong lay duoc → null) |
| 6. Backpressure Leaf | **HOAN** — khong sua Leaf (ly do + huong sau o muc 6) |
| 7. Middleware ghi byte tho | Xong |
| 8. CSV | Xong (+ `lateChunkDrop`; khong co `skip` vi muc 6 hoan) |
| UART_PROTOCOL.md | Da cap nhat cac truong moi (duoc phep sua dac ta) |
| 9. Doi ten truong heap INTERNAL/DEFAULT (lan 3) | Xong |
| 10. Heap guard do DEFAULT, nguong 16 KB | Xong — test17: khong kich hoat |
| 11. `frame_max_bytes_win/boot` | Xong (chua doi `MAX_FRAME_SIZE`/so slot) |
| 12. Giam slot 24576 → 12288 | Chi phan tich, chua sua |

Compile: `arduino-cli` (ban kem Arduino IDE, `esp32:esp32:esp32cam`, core 2.0.17) — ca 2 sketch
compile sach sau MOI commit. Ban cuoi: Root 1 088 369 B flash / 57 296 B RAM tinh;
Leaf 1 145 673 B / 58 036 B. Nhanh `HTTP_ENABLE 1` cung compile duoc (da thu roi tra ve 0).

Luu y chung: so dong o Muc 1, 3, 4, 5, 7, 8 ghi theo commit `589d610` (truoc lan 2); so dong
Muc 2 ghi theo ban cuoi cua nhanh (Muc 2 chen them ~70 dong nen cac muc kia lech tuong ung).

---

## 0. Chuyen thu muc (truoc 8 muc)

Working tree co san viec chuyen `thuattoan/` → `firmware/` + `middleware/`, `nhatkylamviec/`,
`tai lieu/` → `docs/`… chua commit. Tach thanh 4 commit chore rieng, dat truoc moi commit sua.
Lan 2 them 2 commit chore (archive) theo quyet dinh.

- `core.autocrlf=true` tu dua CRLF ve LF khi stage → noi dung trung HEAD, moi file la **R100**
  (`git diff --cached -M --stat` tung commit: 0 dong thay doi, tru `.gitignore`).
- CSV `data mo phong test/` xep theo ngay sua file: `data/2026-09-29/` (test1–3, TEST4-5P, test5),
  `data/2026-09-30/` (test6–16, gom `test15-newfirmware.csv`). CSV chua tung duoc track → la file moi.
- `leaf1_log.txt` → `data/serial-logs/`; `prompt_bringup_3_leaf_camera_node.md` → `docs/nhat-ky/`;
  `mo_phong_reassembly.py` → `firmware/tools/` → lan 2 chuyen tiep vao `archive/` (no mo phong ban
  ESP-IDF `mesh_root_main.c`, timeout 5 s, khong co SUPERSEDE — khong phai `node_root.ino`).
  Deu la rename, lich su giu nguyen.
- `.gitignore`: `firmware/**/secrets.h`, `middleware/node_modules/` (+ `data/raw/` o muc 7).
  - `git check-ignore -v firmware/node_root/secrets.h` →
    `.gitignore:3:firmware/**/secrets.h	firmware/node_root/secrets.h`
  - `git status --porcelain | grep -iE "secrets.h|node_modules"` → rong.
- `code esp32/CameraNode/CameraNode.ino`: KHONG xoa — khoi phuc tu HEAD va chuyen vao
  `archive/camera_node_v0/CameraNode.ino` (`git diff --cached -M --name-status` → `R100`).

---

## Muc 1 — Tran so "im lang" (Root)

**Nguyen nhan xac nhan (dung gia thuyet).** `update_node_tracking()` lay `now = millis()` (ban cu :736),
roi voi node MOI trong `getNodeList()` goi `track_find(id, true)` (:755) gan `seen_ms = millis()`
bang MOT LAN GOI KHAC (:651), co the muon hon `now` 1 ms. :763–765: `ref = seen_ms`,
`now - ref` = 0xFFFFFFFF > `SILENT_NODE_MS` → node vua vao mesh bi danh dau silent ngay,
log in `4294967295 ms` (khop node 1207433521 trong test15), `silent_events` tang sai.

**Soat moi phep tru millis()** (d): `:509, :580, :645, :670, :675, :701, :708, :784, :884, :1410,
:1728, :1913, :1941` (Root, ban cu) va Leaf `:656, :808, :829` — deu la `now` lay SAU `ref` trong
cung task (callback chi chay trong `mesh.update()`), khong am duoc. Chi duy nhat :765/:769 am duoc.

**Thay doi**
- `node_root.ino:332` `age_ms(now, ref)`: tuoi khong am (so sanh co dau → dung ca khi millis() quay
  vong 49,7 ngay; tuong duong "ref > now → 0" nhung khong sai sau quay vong).
- `:699` `track_find(id, create, now)`: nhan `now` cua nguoi goi, `:719 seen_ms = now` (a).
- `:735` `on_valid_chunk()`: goi millis() 1 lan truoc `track_find` (a).
- `:803` `update_node_tracking()`: dung chung `now`; `:837 quiet = age_ms(now, ref)` (b);
  an han `SILENT_NODE_MS` tinh tu `seen_ms` cho node moi (lai) vao mesh — ghi ro trong code (c).
- Dung `age_ms()` them o: `last_chunk_age_ms`, `age_ms` NSTAT, `ap_links_of`, `NSTAT_FORGET_MS`,
  heal ms, eviction track, `cleanup_stale_frames()` (phong xa).
- Leaf `node_camera.ino:659` `check_root_watchdog()`: chan `since` am (hau qua cua am o day la
  `ESP.restart()`), phong xa.

**Kiem tra:** `python firmware/tools/test_millis_age.py` → 30/30 PASS. Test tai hien dung
4294967295 + silent sai o thu tu cu; thu tu moi quiet = 0 voi moi buoc thoi gian 0/1/2/7 ms va
moc gan quay vong (0, 0x7FFFFFFF, U32−5, U32); `age_ms` qua quay vong; an han va het an han.

**Rui ro con lai:** khong. Hanh vi silent thuc (node im > 10 s) khong doi.

---

## Muc 2 — Chunk tre cua frame da dong (hai slot cung node 1207369673 frame 193)

**Lan 1 — dung, bao cao:** nguyen nhan KHAC ca 3 gia thuyet ban dau.
- Khoa day du, dung kieu: `find_assembly_slot()` so `active && node_id && frame_id` (`uint32_t`/
  `uint16_t` ca hai phia), khong so `total_chunks` → loai gia thuyet 1. Frame 193 xa diem quay vong
  `uint16` → loai gia thuyet 3.
- Khong co trang thai "het han nhung chua giai phong": moi duong huy slot (SUPERSEDE, ghep xong,
  TIMEOUT, EVICTION) deu `active = false` / tai dung ngay → loai gia thuyet 2.
- `allocate_assembly_slot()` chi goi khi `find` tra −1, tat ca tren 1 task → 2 slot active cung khoa
  KHONG the ton tai cung luc. Hai dong log la 2 thoi diem: slot 0 giu 193 (4/5) bi dong (SUPERSEDE khi
  194 toi, hoac TIMEOUT); chunk cuoi cua 193 toi TRE → Root khong nho frame da dong → mo slot MOI cho 193
  (1/5) va SUPERSEDE luon 194 dang ghep; chunk 194 ke tiep lai huy 193 ma.

**Lan 2 — sua theo huong da duyet**

Thay doi (`firmware/node_root/node_root.ino`, ban cuoi):
- `:203–214` `ASM_HIST_NODES 8`, `LATE_FRAME_SPAN 16`, `LATE_CHUNK_WINDOW_MS = 2 × REASSEMBLY_TIMEOUT_MS`
  (4 s, KHONG doi `REASSEMBLY_TIMEOUT_MS`), `struct AsmHist asm_hist[8]` — mang tinh, khong malloc.
  Moi node (theo `header.node_id`) giu: frame_id moi nhat da mo slot, da dong chua, moc mo/dong.
- `:1195` `asm_hist_find()` (het o → thay node lau khong mo/dong frame nhat).
- `:1210` `is_late_chunk()`: chunk khong co slot va `d = (int16_t)(frame − newest)`:
  `−16 ≤ d < 0` (cu hon, so sanh co dau → dung qua quay vong 65535 → 0) hoac `d == 0` ma frame do da
  dong, VA con trong cua so 4 s tu lan mo/dong gan nhat → TRE.
- `:1415–1422` trong `receive_and_reassemble()`, TRUOC SUPERSEDE va cap slot: chunk tre → `late_chunk_drop++`,
  `return` (khong mo slot, khong SUPERSEDE). Log chi khi `DEBUG_ASM_VERBOSE`.
- `asm_hist_opened()` `:1451` sau khi cap slot; `asm_hist_closed()` o MOI cho dong slot: EVICTION
  `:1314`, SUPERSEDE `:1440`, ghep xong `:1569`, TIMEOUT `:1613`.
- Telemetry `"late_chunk_drop"` (`:914`), `[STAT] Late:`; dashboard Row "Chunk tre bi bo"
  (`dashboard.html:1617`), CSV cot `lateChunkDrop` (`:196, :1379`).
- KHONG them `dup_slot_events`: da chung minh 2 slot active cung khoa khong the xay ra.

Kiem tra: `python firmware/tools/test_reassembly_late_chunk.py` → **15/15 PASS**. File mo phong DUNG
logic `node_root.ino` hien tai (find, allocate + eviction LRU theo `start_time_ms`, SUPERSEDE, ghep xong,
`cleanup_stale_frames` sau moi chunk), thoi gian tuong minh; `late=False` = ban truoc Muc 2,
`late=True` = ban moi.

| Kich ban | Ban cu | Ban moi |
|---|---|---|
| 1. test15: 193 (4/5) → 194 bat dau → chunk 4 cua 193 tre | tai hien 193 o 2 slot (4/5, 1/5); mat **193 + 194**, frames_dropped 4 | mat **193** (khong cuu duoc — da SUPERSEDE truoc khi chunk cuoi toi), 194/195 ghep xong, frames_dropped 1, late 1 |
| 2. 193 TIMEOUT roi chunk cuoi toi | mo slot ma 193 (1/5) roi huy, frames_dropped 2 | frames_dropped 1 (chi TIMEOUT that), late 1 |
| 3. chunk trung sau khi frame da ghep xong | frame xong van bi dem "huy" (frames_dropped 1) | frames_dropped 0, late 1 |
| 4. quay vong 65535 → 0 | — | frame 0 nhan, chunk tre cua 65535 bi bo |
| 5. Leaf reset (frame_id ve 0 sau 12 s / 9 s) | — | nhan binh thuong, late 0 |
| 6. 3 Leaf xen ke, 50 frame/node | 150/150 | 150/150, late 0, khong hoi quy |
| 7. out-of-order trong 1 frame | — | ghep xong, late 0 |

Ket luan dung chu: chunk tre **khong con gay mat them frame nao** (ban cu: ≥ 1 frame moi bi huy
kem frame goc, tong ≥ 2); frame co chunk tre thi van mat (du lieu toi sau khi da dong).

Rui ro con lai:
- Leaf reset ma frame_id cu ≤ 16 VA frame dau sau reset toi trong 4 s ke tu lan mo/dong cuoi → vai
  chunk dau bi bo nham toi het cua so. Thuc te boot + vao mesh > 4 s nen khong xay ra (kich ban 5).
- Frame tre hon 16 frame (> ~10 s o 600 ms) → coi nhu frame moi (hanh vi cu). Khong the xay ra trong
  cua so 4 s.
- Qua 8 node → node lau khong hoat dong nhat bi thay (chi 3 Leaf).
- Can xac nhan tren board: `late_chunk_drop` > 0 cung luc `frames_drop` giam so voi test15.

---

## Muc 3 — Tat HTTP server va WiFi station tren Root

**Truoc khi tat, thuoc HTTP dang chay:** doi tuong toan cuc `WiFiServer httpServer` + socket lang
nghe lwIP (tao o `httpServer.begin()`); `WiFiClient streamClient`; `handle_http_clients()` goi
`httpServer.available()` (accept khong chan) **moi vong loop()**; khoi in IP goi `WiFi.localIP()` moi
vong toi khi co IP; `latest_frame`/`latest_frame_len`/`new_frame_available` (chi la con tro vao
assembly buffer, khong co buffer rieng). Khong co task/handler rieng. `stationManual()` DA bi comment
tu 14/09. IP 10.255.201.2 trong log test15 la IP station cua Root noi vao AP mesh cua mot Leaf,
khong phai router.

**Thay doi**
- `node_root/mesh_config.h:183` `#define HTTP_ENABLE 0` (co giai thich).
- `node_root.ino`: boc `#if HTTP_ENABLE` (khong xoa): bien HTTP `:149–164`, `ip_printed` `:204`,
  gan `latest_frame` `:1473`, toan bo khoi ham HTTP `:1534`, `httpServer.begin()` `:1984–1994`,
  "Dang cho ket noi WiFi" `:1999`, khoi in IP trong loop `:2027`, `handle_http_clients()` `:2064`.
- Log khoi dong: `[SYS] HTTP: TAT | Heap truoc/sau khoi HTTP: X / Y byte` (`:1991`), in canh dong
  `[SYS] Heap truoc/sau Serial.begin` co san.
- **Bao mat:** ban cu in `STATION_SSID` (tu `secrets.h`) ra log: `[MESH] Ket noi WiFi: <SSID>`.
  Da bo o ca 2 nhanh (nhanh `HTTP_ENABLE 1` in "(SSID trong secrets.h)").

**Uoc luong tiet kiem:** RAM tinh −96 B (57 216 → 57 120), flash −7 KB. Heap luc chay: socket lang
nghe + netconn ~ vai tram byte (idle). Khi co client: tranh them String HTML ~1,5 KB, buffer gui TCP
(~5,7 KB/ket noi) va vong `delay()`/cho doc request toi 2,5 s trong loop. Loi chinh la CPU: khong
con `accept()` moi vong loop.

**Kiem tra:** compile ca `HTTP_ENABLE 0` va `1`. Can xem log khoi dong tren board that.

**Rui ro con lai:** muon xem stream qua trinh duyet phai dat lai `HTTP_ENABLE 1` (va canh bao cu
ve `latest_frame` tro thang vao assembly buffer van con).

---

## Muc 4 — Telemetry/LOG khong chan (Root)

**Nguyen nhan xac nhan:** `send_uart_frame()` (ban cu :425–447) ghi thang `Serial.write()` khong
kiem tra cho trong. Voi TX buffer 8192, `Serial.write()` day → `xRingbufferSend(..., portMAX_DELAY)`
→ chan loop. Chi anh (`send_image_frame`) co duong bo; TELEMETRY/LOG thi khong.

`availableForWrite()` core 2.0.17 = `tx_buf_size − tx_len_tot` (chi dem byte du lieu). Ring buffer TX
cua driver la NOSPLIT: moi `write()` them header item va item khong vat qua cuoi buffer → cho thuc co
the it hon toi 1 item (≤ ~2 KB). Vi vay can du phong.

**Thay doi**
- `:479` `uart_room_for(frame_len)`: can `frame_len + min(frame_len, 2048) + 128` byte trong.
  `uart_tx_buf_size == 0` (setTxBufferSize that bai, `:1758` ban cu) hoac khung lon hon kha nang
  buffer → tra true = **giu hanh vi cu (ghi chan)**, de khung khong bi hoan mai.
- `:488` `send_uart_frame()` tra `bool`; khong du cho → khong ghi byte nao.
- LOG `:522, :535`: bo khi day, `log_drop++`.
- TELEMETRY `:884` `send_telemetry_frame()` tra `bool`: chua du cho → hoan, `uart_defer++` **1 lan
  moi khung**, loop `:2078` thu lai o vong sau voi JSON dung MOI (chi ban moi nhat, khong xep hang).
  Kiem tra truoc bang do dai lan truoc de khong dung lai `asNodeTree()` moi vong khi TX day.
- `heal_n` va mau latency chi xoa SAU khi khung da ghi that → khong mat heal event khi hoan.
- Heap guard `:858`: `Serial.flush()` truoc dong ly do restart.
- Telemetry + `[STAT]`: `uart_defer`, `log_drop`.

**Kiem tra:** compile. Chua chay tren board.

**Rui ro con lai:** (1) du phong 2 KB la uoc luong theo cau truc ring buffer IDF 4.4, chua do tren
board — neu `loop_max_ms` van cao khi `uart_defer` tang, can xem lai. (2) Duong ANH khong doi: frame
anh > cho trong van ghi chan phan con lai (vd anh 10 KB, trong 4 KB → chan ~65 ms o 921600 bps).
(3) Khi TX day lau, dashboard thay telemetry thua hon (dung, khong mat heal).

---

## Muc 5 — Do dac tim nguyen nhan reset (chi them do dac)

**Thay doi**
- a) `loop_max_ms`: do moi vong loop (`:2103`), max trong cua so = giua 2 khung TELEMETRY da ghi
  (~2 s), reset sau moi lan gui thanh cong.
- b) `heap_min_win` = `esp_get_free_heap_size()` thap nhat trong cua so; `heap_min_boot` =
  `ESP.getMinFreeHeap()`.
- c) `rx_q`: **khong lay duoc** → gui `null`. painlessMesh (Alteriom) giu hang doi o
  `BufferedConnection::receiveBuffer/sentBuffer` (`connection.hpp:321–322`, `protected`, khong co
  accessor). `mesh.getQueuedMessageCount()` chi dem `messageQueue` tuy chon, mac dinh `nullptr`
  (`mesh.hpp:3945`) → luon 0, khong dung.
- d) `:192` `__NOINIT_ATTR DiagNoinit diag_noinit` (magic, loop_max_ms, heap_last, heap_min_win,
  up_s, **stage**) ghi MOI vong loop (vai lenh ghi RAM) de con so cua cua so dang do khi WDT cat ngang.
  `stage` = buoc loop() dang chay (1 mesh.update, 2 cleanup, 3 track, 4 telemetry, 5 beacon, 6 stat,
  7 xong) — them de biet WDT xay ra o dau. `:1863` `report_wdt_boot()`: `esp_reset_reason()` 5/6/7 →
  gui ngay khung LOG `[BOOT] WDT reset (ly do N): loop_max_ms=… heap_cuoi=… heap_min_win=… stage=… up_s=…`.
- e) KHONG them `esp_task_wdt_add`, khong doi cau hinh WDT.
- Dashboard: 3 o moi "Loop max" (vang ≥ 200 ms, do ≥ 1000 ms), "Heap min cua so" (do < 70 KB),
  "UART hoan".
- **Sua lai o lan 3 (Muc 9):** `heap_min_win` (DEFAULT) va `heap_min_boot` (INTERNAL) do hai pham vi
  heap khac nhau; nguong 70 KB o tren lay theo thang INTERNAL nen sai voi so DEFAULT. Xem Muc 9–10.

**Kiem tra:** compile; JSX dashboard qua Babel (`@babel/standalone`) OK.

**Rui ro / ghi chu:** Task WDT (ly do 6) cua Arduino 2.0.17 mac dinh chi canh IDLE cua CPU0
(loopTask chay CPU1, khong dang ky WDT). Neu reset la do CPU0 bi chiem (WiFi/lwIP/AsyncTCP),
`loop_max_ms` co the van nho — khi do `stage` + dong "Task watchdog got triggered … tasks: …" ma
Muc 7 bat duoc se chi ra task thu pham. `__NOINIT_ATTR` khong song qua mat nguon (brownout = ly do 9
khong in so).

---

## Muc 6 — Backpressure o Leaf — HOAN (khong sua Leaf)

**Ly do hoan (ket qua buoc 1, chi doc):**
- `capture_and_send()` (`node_camera.ino:256`) gui **het ca frame trong 1 lan goi**: vong `for` moi
  chunk `sendSingle()` (`:416`), giua cac chunk `delay(20)` + `mesh.update()` (`:445`); ham chi tra ve
  khi moi chunk da qua `sendSingle()`. Luc toi lich chup ke tiep khong bao gio con chunk chua
  `sendSingle()` (1 luong, tuan tu) → dieu kien 1 cua buoc 2 luon sai.
- Tai don vao hang doi gui cua painlessMesh: `BufferedConnection::sentBuffer` moi ket noi (toi da
  `PAINLESSMESH_MAX_SENT_BUFFER_MESSAGES` = 64, `buffer.hpp:118`; day → `write()` false →
  `sendSingle()` false → `chunks_send_fail`). `sentBuffer` la **`protected`**
  (`connection.hpp:321–322`), khong co accessor → dieu kien 2 khong do duoc.
- Lam dung mo ta thi `skip_capture` luon 0 → vo tac dung. Khong sua Leaf, khong them `skip`.

**Tra loi: ke thua lop painlessMesh co doc duoc do dai hang doi khong? — KHONG** (chi doc, khong sua):
- `painlessMesh` = `painlessmesh::Mesh<Connection>` (`painlessMesh.h:69`). Hang doi nam o
  `Connection`, lop ke thua `painlessmesh::tcp::BufferedConnection` (`mesh.hpp:4047–4048`), khong nam
  o Mesh. Theo C++, lop dan xuat chi truy cap member `protected` cua CHINH lop co so cua no (qua doi
  tuong kieu dan xuat). `class MyMesh : public painlessMesh` khong dan xuat tu `BufferedConnection`
  → khong doc duoc `sentBuffer`/`receiveBuffer` cua cac `Connection`, du `mesh.subs` (danh sach
  `shared_ptr<Connection>`) la public (`layout.hpp:101`).
- Ke thua `Connection` (`class MyConn : public Connection`) thi doc duoc qua `this`, NHUNG thu vien tu
  tao ket noi bang `std::make_shared<T>(client, &mesh, …)` voi `T = Connection` co dinh
  (`tcp.hpp:159, :359`) — doi tuong khong bao gio la `MyConn`. Dung `Mesh<MyConn>` khong compile duoc
  vi `Connection` giu `Mesh<Connection>* mesh` va ham dung nhan `Mesh<Connection>*`.
- `Connection` co bo dem public `messagesTx/messagesRx/messagesDropped` (`mesh.hpp:4060`…) nhung
  `onMessageSent()`/`onMessageReceived()` **khong duoc goi o dau** trong thu vien → luon 0, vo dung.
- `mesh.getQueuedMessageCount()` chi dem `messageQueue` tuy chon (mac dinh `nullptr`) → luon 0.
- Con duong khong sua thu vien: khong co (tru meo `#define protected public` truoc `#include` — hanh vi
  khong xac dinh ve mat chuan, khong khuyen nghi).

**Huong co the lam sau:**
1. Dung `sendSingle()` tra `false` (sentBuffer day) cua frame truoc lam tin hieu: bo lan chup ke,
   dem `skip`. Don gian, khong can sua thu vien; nhung chi bao khi da day han (64 tin).
2. Theo thoi gian: frame truoc gui lau hon X ms (vd > 1 chu ky) → bo lan chup ke.
3. Patch thu vien (fork Alteriom) them `size_t sentBufferSize() const { return sentBuffer.size(); }`
   public trong `BufferedConnection` + ham tong trong Mesh → do duoc hang doi that.
4. Admission control o Root (da hoan theo yeu cau).

---

## Muc 7 — Middleware ghi byte tho

**Thay doi**
- `middleware/protocol.js:70–127`: `FrameParser` nhan `opts.onSkip(buf, reason)` — chi quan sat,
  goi voi moi byte khong thuoc khung hop le theo dung thu tu tren day: `'skip'` (rac truoc MAGIC,
  LEN vo ly) va `'crc'` (ca khung sai CRC — quan trong: dong WDT do ESP32 in thang vao FIFO co the
  chen GIUA mot khung dang gui, lam khung do sai CRC). Duong tach khung va `stats` khong doi.
- `middleware/raw_logger.js` (moi): `data/raw/uart_raw_YYYYMMDD_HHMMSS.log`, mo luc start; moi dong
  `<ISO time> [skip|crc] <noi dung>`; ASCII in duoc giu nguyen, `\` → `\\`, byte khac → `\xNN`,
  `\n` ket thuc dong (`\r` truoc `\n` bo); dong > 1024 ky tu cat sang dong moi; ghi dia moi 1 s;
  ≥ 20 MB → dong file, mo file moi. Loi dia chi canh bao, khong nem ra ngoai.
- `middleware/server.js:52–53, :158, :173, :179`: noi vao parser, in duong dan file, `/status` co
  `rawLog`, dong file khi SIGINT.
- `.gitignore`: `data/raw/`.

**Kiem tra**
- `node test_protocol.js` → **8/8 PASS** (file hien co 8 test, khong phai 7 — Test 8 them o
  feature/on-dinh).
- `node test_raw_logger.js` (moi) → **5/5 PASS**: dong WDT giua 2 khung; dong WDT chen giua khung
  anh (sai CRC) van vao file, khung sau van doc duoc; dinh dang; xoay file; khong co hook van chay.
- Chay thu `server.js` 4 s voi cong COM khong ton tai: mo file raw, khong loi.

**Rui ro:** khung anh sai CRC ghi ca khoi nhi phan (~4×) vao file — da gioi han 20 MB/file, khong
gioi han so file. ROM bootloader in o 115200 → o 921600 se thanh `\xNN` (van thay duoc moc reset).

---

## Muc 8 — CSV

**Thay doi**
- Leaf `#NSTAT` them `"stg":0|1` (= `STAGGER_ENABLE`, `node_camera.ino:532`); Root luu
  (`node_root.ino:399`) va dua `"stg"` vao `nodes[]` (`:776`), Leaf cu → `null`.
- `dashboard.html:191–199`: cot so `uartDefer, logDrop, loopMaxMs, heapMinWinKB, heapMinBootKB`;
  cot chuoi `n_leaf, fw_root, fw_leaf, stagger` (`:817, :823, :1376–1399`).
  - `n_leaf` = so Leaf CON TRONG CAY dang gui NSTAT (biet fw). Khac cot `leaf` co san
    (= `node_count` cua Root, gom ca node chua gui NSTAT) — de phan biet "vao mesh" va "chay firmware
    camera". Neu muon `n_leaf` = `node_count` thi doi 1 dong.
  - `fw_leaf` = `id:fw;…`; `stagger` = `1`/`0`/`mix`/trong (Leaf fw cu).
- Lan 2: them cot `lateChunkDrop` (Muc 2). KHONG co `dup_slot_events` (khong can, xem Muc 2), `skip`
  (Muc 6 hoan). `rx_q` luon null nen khong dua vao CSV.

**Kiem tra:** JSX qua Babel OK; `test_protocol.js` 8/8. Chua mo dashboard voi du lieu that.

`docs/UART_PROTOCOL.md` (lan 2, duoc phep sua dac ta): bo sung §4.2 cac truong `uart_defer`,
`log_drop`, `late_chunk_drop`, `loop_max_ms`, `heap_min_win`, `heap_min_boot`, `rx_q`, `nodes[].stg`;
sua cau cu "TELEMETRY va LOG khong bao gio bi bo"; `#NSTAT` them `"stg"`; §4.3 dong `[SYS] HTTP: TAT`,
`[BOOT] WDT reset` + y nghia `stage`; §5 file byte tho `data/raw/`.

---

## Muc 9 — Heap INTERNAL va DEFAULT: doi ten truong (lan 3)

**Van de (test17, `data/2026-09-30/test17.csv`, 2 Leaf, 10 phut):** `heapMinWinKB` thuong ~42 KB,
thap nhat 25.2 KB, nhung `heapMinBootKB` dung yen 57.3 KB. Khong mau thuan — hai pham vi khac nhau
(Arduino core 2.0.17 `cores/esp32/Esp.cpp:139–151`):
- `ESP.getFreeHeap()` / `ESP.getMinFreeHeap()` = `heap_caps_*(MALLOC_CAP_INTERNAL)`: DRAM + IRAM con
  trong.
- `esp_get_free_heap_size()` = `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)`: heap `malloc()` dung
  duoc. Root khong co PSRAM kha dung (`psram_free` = 0) nen DEFAULT = DRAM noi.
- test17: `heapKB` (INTERNAL, luc gui) − `heapMinWinKB` (DEFAULT, min cua so) thap nhat 46.9 KB, trung
  vi 50.2 KB → INTERNAL ≈ DEFAULT + ~47 KB (IRAM trong ma malloc khong dung).
- `heap_min_boot` la muc day INTERNAL tu luc khoi dong (khong phai RTC_NOINIT, reset moi lan boot); la
  tong muc day tung vung heap → can duoi. 57.3 KB dung yen = day roi vao luc khoi tao, truoc khi do.

**Thay doi** (commit `31690c1`)
- Telemetry: `heap_free` → `heap_int`; them `heap_def` (`esp_get_free_heap_size()` luc dung JSON);
  `heap_min_win` → `heap_def_min_win`; `heap_min_boot` → `heap_int_min_boot`; them `heap_def_largest`
  = `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)`, min trong cua so (do cuoi moi vong
  `loop()` cung cho voi `heap_def_min_win`). `diag_noinit.heap_min_win` → `heap_def_min_win`, dong
  `[BOOT] WDT reset` doi ten tuong ung. `[STAT]` in `Heap int/def`.
- Dashboard: chi so chinh/sparkline/bieu do so sanh doi sang heap DEFAULT, nhan "Heap DEFAULT trong
  (heap malloc dung duoc)", note kem INTERNAL. O nho "DEFAULT min cua so" va "DEFAULT khoi lon nhat":
  do < 16 KB (= nguong guard), vang < 32 KB. Cot CSV: `heapDefKB` (thay `heapKB`), `heapIntKB`,
  `heapDefMinWinKB`, `heapIntMinBootKB`, `heapDefLargestKB`; doc ten cu cua FW `2026-10-01a`
  (`heap_free`, `heap_min_win`, `heap_min_boot` — cung phep do) nen CSV cu van doi chieu duoc. So lieu
  "So sanh" da luu trong localStorage theo khoa `heapKB` cu khong hien lai.
- `UART_PROTOCOL.md` §4.2: bang 5 truong heap + giai thich DEFAULT/INTERNAL. Root FW `2026-10-01b`.

**Kiem tra:** compile OK sau moi commit (commit 9–10 voi `esp32:esp32:esp32`, ban cuoi voi `esp32cam`: Root 1 088 685 B flash / 57 304 B
RAM tinh; Leaf khong doi); JSX dashboard qua Babel OK; `test_protocol.js` 8/8. Chua nap board.

---

## Muc 10 — Heap guard do DEFAULT (lan 3)

**Thay doi** (commit `d57c8e2`): `check_heap_guard()` doi `ESP.getFreeHeap()` → `esp_get_free_heap_size()`
(cung phep do voi `heap_def_min_win`). `mesh_config.h`: `HEAP_CRITICAL_BYTES 40960` →
`HEAP_DEF_CRITICAL_BYTES 16384`, giu `HEAP_CRITICAL_MS 10000`. Dong LOG:
`[HEAP] RESTART: heap DEFAULT trong X < 16384 byte lien tuc N ms`.

Guard cu: INTERNAL ≈ DEFAULT + 47 KB, nguong 40 KB → DEFAULT phai gan ve 0 guard moi kich hoat.

**test17 voi guard moi:** khong kich hoat lan nao. 304 cua so (~2 s, lien tuc 16:33:47–16:43:53Z,
khong ho), `heap_def_min_win` thap nhat 25.2 KB > 16 KB → moi mau trong moi cua so ≥ 25.2 KB.
(Neu do DEFAULT ma giu nguong 40 KB: 20/304 cua so < 40 KB, toi da 2 cua so lien tiep ~4 s, cua so
ke tiep ≥ 40 KB → van khong du 10 s lien tuc, nhung sat nguong.)

---

## Muc 11 — `frame_max_bytes_win` / `frame_max_bytes_boot` (lan 3)

**Thay doi** (commit `0733400`): Root ghi kich thuoc JPEG lon nhat ghep xong trong cua so (0 = cua so
khong co frame) va tu luc khoi dong, cap nhat ngay sau `frames_completed++`. CSV them
`frameMaxBytesWin`, `frameMaxBytesBoot` (byte). Chua doi `MAX_FRAME_SIZE` hay so slot.

Ly do: cot `frameKB` cu chi la TRUNG BINH moi cua so (test9–17: 2.3–5.4 KB; TEST4-5P 29/09 max 8.4 KB)
— khong biet frame don le lon nhat. Can 1 soak voi FW `2026-10-01b` truoc khi quyet dinh Muc 12.

---

## Muc 12 — Giam slot Root 24576 → 12288 (chi phan tich, CHUA SUA)

**JPEG lon nhat Leaf co the gui hien nay**
- Camera: `CAM_FRAME_SIZE FRAMESIZE_QVGA` (320×240), `CAM_JPEG_QUALITY 12`, `CAM_FB_COUNT 1`, fb trong
  PSRAM (`node_camera/mesh_config.h:94–96`, `node_camera.ino:175–193`). Khong doi quality/framesize luc
  chay.
- Gioi han Leaf: `fb->len > MAX_FRAME_SIZE` (24576) → bo frame, khong gui (`node_camera.ino:307`).
  Chunk 1024 B → toi da 24 chunk (`MAX_CHUNKS_PER_FRAME`).
- Gioi han driver (CHUA kiem tren board): theo ma nguon esp32-camera (`cam_hal.c`), che do JPEG cap
  buffer `width × height / 5` = 15 360 B cho QVGA; JPEG lon hon bi driver loai. Lib trong core 2.0.17
  la file `.a` nen chua xac nhan duoc so nay.
- → Tran ly thuyet hien nay: min(24 576, ~15 360) ≈ 15 KB. Thuc te trung binh 2.3–8.4 KB.
  So that phai lay tu `frame_max_bytes_boot` (Muc 11).

**Neu doi slot 24576 → 12288, phai sua:**
- Root `firmware/node_root/mesh_config.h:98–99`: `MAX_FRAME_SIZE 12288`, `MAX_CHUNKS_PER_FRAME 12`
  (`#error` o `:101` bat sai lech). Comment: `mesh_config.h:86` ("4 slot × 24KB = 96KB"), `:92–94`,
  `node_root.ino:17`, `:99`. Code dung hang so tu dong theo (`:1043`, `:1144`, `:1400`, `:1506`).
- Leaf `firmware/node_camera/mesh_config.h:77–78`: CUNG gia tri (comment ghi "khop Root"). Neu Leaf
  giu 24576, frame 12–24 KB van duoc gui roi Root tu choi tung chunk (`node_root.ino:1400`/`:1506`) →
  ton bang thong mesh vo ich. Can tang FW_VERSION ca hai.
- Leaf co the can giam chat luong (`CAM_JPEG_QUALITY` cao hon) neu `frame_max_bytes_boot` cho thay
  frame > 12 KB; frame lon hon se bi Leaf bo (`node_camera.ino:307`) → tang drop.
- `docs/UART_PROTOCOL.md:47` ("du du cho `MAX_FRAME_SIZE = 24576`") va doan `frame_max_bytes_*`.
- Middleware: KHONG phai sua — `protocol.js:22` `MAX_PAYLOAD = 131072` la tran khung UART, khong phu
  thuoc kich thuoc anh; dashboard khong hard-code 24 KB.

**Tiet kiem:** 4 buffer ghep cap phat 1 lan luc `setup()` bang `alloc_buffer()` → `ps_malloc` that bai
(khong PSRAM) → `malloc` = heap DEFAULT (`node_root.ino:1127–1130`, `:1144`). 4 × (24576 − 12288) =
**49 152 B = 48 KB DEFAULT**. Them 4 × 12 B `chunk_received[]` (.bss, khong phai heap). Heap DEFAULT on
dinh se tu ~45 KB len ~93 KB.

---

## Bien an toan heap (lan 3)

So lieu duy nhat hien co la test17 (**2 Leaf**, 10 phut, Root FW `2026-10-01a`). Chua co so 3 Leaf voi
cach do moi.

| Dai luong (heap DEFAULT, KB) | Gia tri |
|---|---|
| On dinh (uoc: `heapKB` − ~47) | ~45–46 |
| `heap_def_min_win` trung vi / p10 / p5 / p1 | 42.9 / 41.1 / 38.4 / 33.1 |
| `heap_def_min_win` thap nhat (16:39:13Z) | 25.2 |
| So cua so < 35 / < 30 / < 24 | 9 / 2 / 0 (tren 304) |
| Nguong guard `HEAP_DEF_CRITICAL_BYTES` | 16 |

- Bien thuong ngay: 42.9 − 16 ≈ **27 KB**. Bien xau nhat da thay: 25.2 − 16 ≈ **9 KB**.
- Sut ngan han tu ~45 xuong 25 KB (~20 KB trong < 2 s, cua so sau hoi lai) — chua biet do cap phat
  nao (nghi bo dem nhan/gui painlessMesh khi nhieu chunk den don, chua xac minh). `heap_def_largest`
  (Muc 9) se cho biet co phan manh khong — chua co so.
- Khi them Leaf thu 3, bien se nho hon (them 1 luong chunk dong thoi); chua do.
- Giam slot (Muc 12) them ~48 KB vao moi so tren.
- Tieu chi cu "heap_min_win > 70 KB" (checklist) dung thang INTERNAL → khong bao gio dat voi so
  DEFAULT. Doi thanh tieu chi DEFAULT o checklist duoi (de xuat ≥ 24 KB = guard + 8 KB; can duyet).

---

## Bang truong telemetry moi (Root → middleware)

| Truong | Kieu | Y nghia |
|---|---|---|
| `uart_defer` | uint, tich luy | So khung TELEMETRY phai hoan vi TX buffer chua du cho (dem 1 lan/khung) |
| `log_drop` | uint, tich luy | So khung LOG bi bo vi TX buffer chua du cho |
| `late_chunk_drop` | uint, tich luy | So chunk tre (frame da dong) bi bo — khong mo slot, khong SUPERSEDE |
| `loop_max_ms` | uint, cua so | Vong `loop()` lau nhat tu khung telemetry truoc (ms) |
| `heap_int` | uint | `ESP.getFreeHeap()` luc dung JSON — INTERNAL (gom IRAM). Truoc `2026-10-01b`: `heap_free` |
| `heap_def` | uint | `esp_get_free_heap_size()` luc dung JSON — DEFAULT, heap malloc dung duoc (tu `2026-10-01b`) |
| `heap_def_min_win` | uint, cua so | DEFAULT thap nhat tu khung telemetry truoc (byte). Truoc `2026-10-01b`: `heap_min_win` |
| `heap_int_min_boot` | uint | `ESP.getMinFreeHeap()` — day INTERNAL tu luc khoi dong (byte). Truoc `2026-10-01b`: `heap_min_boot` |
| `heap_def_largest` | uint, cua so | Khoi trong lon nhat cua DEFAULT, thap nhat trong cua so (tu `2026-10-01b`) |
| `frame_max_bytes_win` | uint, cua so | JPEG lon nhat Root ghep xong trong cua so, 0 = khong co (tu `2026-10-01b`) |
| `frame_max_bytes_boot` | uint | JPEG lon nhat Root ghep xong tu luc khoi dong (tu `2026-10-01b`) |
| `rx_q` | null | Hang doi nhan painlessMesh — khong doc duoc qua API (luon null) |
| `nodes[].stg` | 0/1/null | `STAGGER_ENABLE` cua Leaf (tu `#NSTAT "stg"`), null = Leaf fw cu |

Khung LOG moi: `[SYS] HTTP: TAT | Heap truoc/sau khoi HTTP: X / Y byte` (moi lan khoi dong);
`[BOOT] WDT reset (ly do N): loop_max_ms=… heap_cuoi=… heap_def_min_win=… stage=… up_s=…` (sau reset 5/6/7;
truoc `2026-10-01b` la `heap_min_win=`); `[HEAP] RESTART: heap DEFAULT trong … < 16384 byte lien tuc … ms`.

## Danh sach commit

`git log --oneline feature/on-dinh..fix/on-dinh-2` (cu → moi):

| Hash | Message |
|---|---|
| `5ca8e2e` | chore: chuyen du lieu do va esp_idf_migration vao data/ |
| `d7f7cf3` | chore: chuyen nhat ky, tai lieu, UART_PROTOCOL vao docs/ |
| `c87ed64` | chore: chuyen thuattoan/ sang firmware/ va middleware/ |
| `b72f068` | chore: chuyen mo_phong_reassembly.py vao firmware/tools/ |
| `1b24934` | fix(root): tuoi "im lang" khong con tran so 4294967295 ms |
| `ade0fc4` | fix(root): tat HTTP server tren Root (HTTP_ENABLE 0) |
| `c88585c` | fix(root): telemetry/LOG khong chan loop khi UART TX day |
| `443f243` | feat(root): do dac tim nguyen nhan reset (loop_max, heap_min, WDT boot log) |
| `a88a13a` | feat(middleware): ghi byte UART tho ngoai khung ra data/raw/ |
| `200b704` | feat(middleware): CSV them fw_root, fw_leaf, stagger, n_leaf va truong do moi |
| `589d610` | chore(fw): tang FW_VERSION Root va Leaf len 2026-10-01a |
| `01f5b31` | chore: luu CameraNode.ino ban dau vao archive/camera_node_v0/ |
| `a17758a` | chore: chuyen mo_phong_reassembly.py vao archive/ |
| `0bb504b` | fix(root): bo chunk tre cua frame da dong, khong mo slot, khong SUPERSEDE |
| `bd3f7f4` | docs(protocol): bo sung truong moi Root/Leaf 2026-10-01a vao UART_PROTOCOL.md |
| `85fa1ad` | docs: bao cao fix on dinh 2026-10-01 |
| `7ec2144` | chore(data): them test17.csv vao data/2026-09-30 (test chay 23:33 gio VN 30/09) |
| `31690c1` | feat(root): tach heap INTERNAL/DEFAULT trong telemetry, them heap_def_largest |
| `d57c8e2` | fix(root): heap guard do heap DEFAULT, nguong HEAP_DEF_CRITICAL_BYTES 16384 |
| `0733400` | feat(root): telemetry frame_max_bytes_win/boot (frame lon nhat Root ghep xong) |
| (commit nay) | docs: bao cao lan 3 — heap INTERNAL/DEFAULT, bien an toan, phan tich giam slot |

Ghi chu: Muc 2 sua SAU commit tang FW_VERSION nhung FW van la `2026-10-01a` (chua nap board nao voi
ban `2026-10-01a` truoc do). Hai commit `0bb504b`, `bd3f7f4` chi sua lai message (so test 15/15, "Muc 7")
bang `--amend` ngay khi vua tao, chua push.

---

## Checklist nap firmware va soak test

Chuan bi
- [ ] `git switch fix/on-dinh-2`; `firmware/node_root/secrets.h` van co (khong nam trong git).
- [ ] Nap Root `firmware/node_root` (Board AI Thinker ESP32-CAM, PSRAM theo board dang dung).
- [ ] Nap CA 3 Leaf `firmware/node_camera` (can `stg` + fw moi cho cot CSV).
- [ ] `cd middleware && node server.js COMx` — log phai co `[RAW] Byte tho ngoai khung -> …data\raw\uart_raw_….log`.
- [ ] Log khoi dong Root co: `[SYS] Firmware: 2026-10-01b`, `[SYS] HTTP: TAT | Heap truoc/sau …`,
      KHONG con dong `[HTTP] Server KHOI DONG`, KHONG in SSID router.
- [ ] Dashboard: o "Loop max", "DEFAULT min cua so", "DEFAULT khoi lon nhat", "UART hoan" co so;
      the "Heap DEFAULT trong" co note INTERNAL; bang Leaf hien fw `2026-10-01a`.
- [ ] Ctrl+F5 dashboard (tranh JS cu), bat ghi so lieu tab "So sanh".

Soak 1 — 2 Leaf, 30 phut
- [ ] Chi bat 2 Leaf. Ghi lai gio bat dau.
- [ ] Het 30 phut: xuat CSV → `data/2026-10-01/soak_2leaf.csv`; chep `data/raw/uart_raw_*.log`.

Soak 2 — 3 Leaf, 30 phut
- [ ] Bat Leaf thu 3, cho `leaf` = 3 on dinh, ghi tiep 30 phut; xuat CSV `soak_3leaf.csv`.

Tieu chi DAT (moi soak)
- [ ] **0 reset Root**: `ts` telemetry khong giam; khong co `[BOOT] WDT reset`; file raw khong co
      `rst:0x`/`Task watchdog`.
- [ ] **drop < 2 %**: `dropPct` trung binh (dong summary CSV) < 2.
- [ ] **heap DEFAULT ≥ 24 KB** (de xuat, can duyet; thay tieu chi cu "heap_min_win > 70 KB" do sai
      thang INTERNAL): `heapDefMinWinKB` nho nhat trong CSV ≥ 24; khong co dong `[HEAP] RESTART`.
- [ ] Ghi `frameMaxBytesBoot` cuoi phien va `heapDefLargestKB` nho nhat (dau vao cho Muc 12).
- [ ] **loop_max_ms < 1000**: `loopMaxMs` lon nhat trong CSV < 1000.
- [ ] Ghi them de doi chieu: `silent_events` khong tang khi khong rut node (kiem Muc 1);
      `late_chunk_drop` (Muc 2 — > 0 nghia la co chunk tre da duoc chan, frames_drop phai thap hon
      test15); `uart_defer`, `log_drop`, `uart_drop` cuoi phien.

Neu Root van reset: lay `[BOOT] WDT reset …` (stage, loop_max, heap) + doan file raw ngay truoc moc
reset (dong `Task watchdog got triggered` va danh sach task) → quyet dinh buoc tiep. Neu drop van
cao du `late_chunk_drop` > 0: xem Muc 6 (backpressure Leaf) huong 1 hoac 3.
