"""
Mo phong thuat toan ghep anh (Frame Reassembly) cua node_root
============================================================
Day KHONG PHAI mo phong mang WiFi Mesh that (khong the lam duoc
tren may tinh vi khong co radio that). Day la mo phong lai DUNG
LOGIC thuat toan trong mesh_root_main.c (bitmap chunk, timeout,
duplicate detection, LRU eviction) bang Python, de kiem tra tinh
dung dan cua thuat toan MA KHONG CAN board ESP32 that.

Chay: python mo_phong_reassembly.py
(dung python co san trong ESP-IDF venv, hoac python 3 bat ky)
"""

import time
import random

MAX_CHUNK_PAYLOAD = 1024
MAX_CONCURRENT_FRAMES = 4
REASSEMBLY_TIMEOUT_S = 5.0  # tuong duong REASSEMBLY_TIMEOUT_MS = 5000


class FrameAssembly:
    def __init__(self):
        self.active = False
        self.node_id = None
        self.frame_id = None
        self.total_chunks = 0
        self.received_count = 0
        self.chunk_received = {}
        self.buffer = {}  # chunk_index -> payload (mo phong, khong dung byte thuc)
        self.start_time = 0.0


class Root:
    """Mo phong dung logic find_assembly_slot / allocate_assembly_slot /
    handle_incoming_packet / cleanup_stale_frames trong mesh_root_main.c"""

    def __init__(self):
        self.slots = [FrameAssembly() for _ in range(MAX_CONCURRENT_FRAMES)]
        self.frames_completed = 0
        self.frames_dropped = 0
        self.chunks_received = 0
        self.log = []

    def _find_slot(self, node_id, frame_id):
        for i, s in enumerate(self.slots):
            if s.active and s.node_id == node_id and s.frame_id == frame_id:
                return i
        return -1

    def _allocate_slot(self, node_id, frame_id, total_chunks):
        for i, s in enumerate(self.slots):
            if not s.active:
                s.__init__()
                s.active = True
                s.node_id = node_id
                s.frame_id = frame_id
                s.total_chunks = total_chunks
                s.start_time = time.monotonic()
                self.log.append(f"[ASM] Slot {i}: bat dau ghep frame (node={node_id} frame={frame_id})")
                return i
        # het slot -> evict slot cu nhat (LRU), giong C code
        oldest_idx = min(range(len(self.slots)), key=lambda i: self.slots[i].start_time)
        old = self.slots[oldest_idx]
        self.log.append(
            f"[ASM] EVICTION slot {oldest_idx} (node={old.node_id} frame={old.frame_id}, "
            f"nhan {old.received_count}/{old.total_chunks})"
        )
        self.frames_dropped += 1
        old.__init__()
        old.active = True
        old.node_id = node_id
        old.frame_id = frame_id
        old.total_chunks = total_chunks
        old.start_time = time.monotonic()
        return oldest_idx

    def handle_packet(self, node_id, frame_id, chunk_index, total_chunks, payload):
        self.chunks_received += 1
        slot_idx = self._find_slot(node_id, frame_id)
        if slot_idx < 0:
            slot_idx = self._allocate_slot(node_id, frame_id, total_chunks)
        s = self.slots[slot_idx]

        if s.chunk_received.get(chunk_index):
            self.log.append(f"[ASM] Chunk {chunk_index} TRUNG LAP (frame={frame_id}) -> bo qua")
            return

        s.buffer[chunk_index] = payload
        s.chunk_received[chunk_index] = True
        s.received_count += 1

        if s.received_count >= s.total_chunks:
            self.log.append(
                f"[ASM] HOAN THANH! node={node_id} frame={frame_id} "
                f"chunks={s.total_chunks} (nhan du, khong thieu khong trung)"
            )
            s.active = False
            self.frames_completed += 1

    def cleanup_stale(self):
        now = time.monotonic()
        for i, s in enumerate(self.slots):
            if s.active and (now - s.start_time) > REASSEMBLY_TIMEOUT_S:
                self.log.append(
                    f"[ASM] TIMEOUT slot={i} node={s.node_id} frame={s.frame_id} "
                    f"nhan {s.received_count}/{s.total_chunks} -> huy"
                )
                s.active = False
                self.frames_dropped += 1


def in_ket_qua(ten_test, root, ky_vong_completed, ky_vong_dropped):
    ok = (root.frames_completed == ky_vong_completed) and (root.frames_dropped == ky_vong_dropped)
    trang_thai = "PASS" if ok else "FAIL"
    print(f"\n=== {ten_test}: {trang_thai} ===")
    for line in root.log:
        print("  " + line)
    print(
        f"  -> frames_completed={root.frames_completed} (ky vong {ky_vong_completed}), "
        f"frames_dropped={root.frames_dropped} (ky vong {ky_vong_dropped})"
    )
    return ok


# ---------------------------------------------------------------
# Test 1: Nhan dung thu tu, day du -> phai HOAN THANH
# ---------------------------------------------------------------
def test_1_dung_thu_tu():
    root = Root()
    total = 5
    for i in range(total):
        root.handle_packet(node_id=1, frame_id=100, chunk_index=i, total_chunks=total, payload=f"data{i}")
    return in_ket_qua("Test 1: Nhan dung thu tu", root, ky_vong_completed=1, ky_vong_dropped=0)


# ---------------------------------------------------------------
# Test 2: Nhan KHONG theo thu tu (out-of-order) -> van phai HOAN THANH
# ---------------------------------------------------------------
def test_2_khong_theo_thu_tu():
    root = Root()
    total = 5
    order = [3, 0, 4, 1, 2]  # co tinh xao tron
    for i in order:
        root.handle_packet(node_id=1, frame_id=200, chunk_index=i, total_chunks=total, payload=f"data{i}")
    return in_ket_qua("Test 2: Nhan khong theo thu tu", root, ky_vong_completed=1, ky_vong_dropped=0)


# ---------------------------------------------------------------
# Test 3: Chunk gui trung lap -> bo qua, khong tang nham received_count
# ---------------------------------------------------------------
def test_3_chunk_trung_lap():
    root = Root()
    total = 3
    root.handle_packet(node_id=1, frame_id=300, chunk_index=0, total_chunks=total, payload="a")
    root.handle_packet(node_id=1, frame_id=300, chunk_index=0, total_chunks=total, payload="a-lai")  # trung
    root.handle_packet(node_id=1, frame_id=300, chunk_index=1, total_chunks=total, payload="b")
    root.handle_packet(node_id=1, frame_id=300, chunk_index=2, total_chunks=total, payload="c")
    return in_ket_qua("Test 3: Chunk trung lap bi bo qua", root, ky_vong_completed=1, ky_vong_dropped=0)


# ---------------------------------------------------------------
# Test 4: Mat 1 chunk giua chung -> cho REASSEMBLY_TIMEOUT_S -> phai TIMEOUT/DROP
# ---------------------------------------------------------------
def test_4_mat_chunk_timeout():
    root = Root()
    total = 3
    root.handle_packet(node_id=1, frame_id=400, chunk_index=0, total_chunks=total, payload="a")
    root.handle_packet(node_id=1, frame_id=400, chunk_index=1, total_chunks=total, payload="b")
    # chunk_index=2 "bi mat", khong bao gio gui toi
    print(f"\n  (dang cho {REASSEMBLY_TIMEOUT_S}s de mo phong timeout that su...)")
    time.sleep(REASSEMBLY_TIMEOUT_S + 0.5)
    root.cleanup_stale()
    return in_ket_qua("Test 4: Mat chunk -> timeout", root, ky_vong_completed=0, ky_vong_dropped=1)


# ---------------------------------------------------------------
# Test 5: Nhieu frame dong thoi vuot qua MAX_CONCURRENT_FRAMES -> LRU eviction
# ---------------------------------------------------------------
def test_5_tran_slot_lru():
    root = Root()
    total = 2
    # Mo 5 frame dong thoi (MAX_CONCURRENT_FRAMES = 4) -> frame dau se bi evict
    for frame_id in range(500, 505):
        root.handle_packet(node_id=1, frame_id=frame_id, chunk_index=0, total_chunks=total, payload="x")
        time.sleep(0.01)  # dam bao thu tu start_time ro rang cho LRU
    # frame 500 (cu nhat) da bi evict truoc khi kip nhan chunk thu 2
    return in_ket_qua("Test 5: Tran slot -> LRU eviction", root, ky_vong_completed=0, ky_vong_dropped=1)


if __name__ == "__main__":
    print("=" * 60)
    print("MO PHONG THUAT TOAN GHEP ANH (node_root reassembly)")
    print("Khong can board ESP32, khong can WiFi that")
    print("=" * 60)

    results = []
    results.append(test_1_dung_thu_tu())
    results.append(test_2_khong_theo_thu_tu())
    results.append(test_3_chunk_trung_lap())
    results.append(test_4_mat_chunk_timeout())
    results.append(test_5_tran_slot_lru())

    print("\n" + "=" * 60)
    so_pass = sum(results)
    print(f"TONG KET: {so_pass}/{len(results)} test PASS")
    print("=" * 60)