"""
test_reassembly_late_chunk.py — mô phỏng ĐÚNG logic ghép ảnh của node_root.ino
(find_assembly_slot, allocate_assembly_slot + eviction LRU, SUPERSEDE, ghép xong,
cleanup_stale_frames TIMEOUT) để kiểm chứng xử lý chunk trễ (Mục 2, test15:
node 1207369673, frame 193 nằm ở 2 slot: 4/5 rồi 1/5).

  late=False : node_root.ino TRƯỚC Mục 2 (FW 2026-10-01a)
  late=True  : thêm is_late_chunk()/asm_hist_* (bỏ chunk trễ, không SUPERSEDE)

Thời gian là tham số tường minh (ms), không dùng đồng hồ máy.
Chạy:  python firmware/tools/test_reassembly_late_chunk.py
"""

MAX_CONCURRENT_FRAMES = 4
REASSEMBLY_TIMEOUT_MS = 2000
ASM_HIST_NODES = 8
LATE_FRAME_SPAN = 16
LATE_CHUNK_WINDOW_MS = 2 * REASSEMBLY_TIMEOUT_MS
U32 = 0xFFFFFFFF


def age_ms(now, ref):
    d = (now - ref) & U32
    return 0 if d >= 0x80000000 else d


def int16(x):
    x &= 0xFFFF
    return x - 0x10000 if x >= 0x8000 else x


class Slot:
    def __init__(self):
        self.active = False
        self.node = self.frame = self.total = self.count = self.start = 0
        self.got = set()


class Root:
    def __init__(self, late):
        self.late = late
        self.slots = [Slot() for _ in range(MAX_CONCURRENT_FRAMES)]
        self.hist = {}              # node -> [newest, closed, mark_ms]  (mảng tĩnh 8 ô trên ESP32)
        self.completed = []         # (node, frame) theo thứ tự ghép xong
        self.frames_dropped = 0
        self.late_chunk_drop = 0
        self.log = []

    # ── asm_hist_* / is_late_chunk (chỉ khi late=True) ──
    def _is_late(self, node, frame, now):
        h = self.hist.get(node)
        if h is None or age_ms(now, h[2]) > LATE_CHUNK_WINDOW_MS:
            return False
        d = int16(frame - h[0])
        if -LATE_FRAME_SPAN <= d < 0:
            return True
        return d == 0 and h[1]

    def _opened(self, node, frame, now):
        if not self.late:
            return
        if node not in self.hist and len(self.hist) >= ASM_HIST_NODES:
            del self.hist[min(self.hist, key=lambda n: self.hist[n][2])]
        self.hist[node] = [frame, False, now]

    def _closed(self, node, frame, now):
        if not self.late:
            return
        h = self.hist.get(node)
        if h is not None and h[0] == frame:
            h[1] = True
            h[2] = now

    # ── allocate_assembly_slot ──
    def _allocate(self, node, frame, total, now):
        for i, s in enumerate(self.slots):
            if not s.active:
                break
        else:
            i = min(range(MAX_CONCURRENT_FRAMES), key=lambda k: self.slots[k].start)
            old = self.slots[i]
            self.log.append(f"EVICTION slot {i} node={old.node} frame={old.frame} {old.count}/{old.total}")
            self.frames_dropped += 1
            self._closed(old.node, old.frame, now)
        s = self.slots[i]
        s.active, s.node, s.frame, s.total, s.count, s.start, s.got = True, node, frame, total, 0, now, set()
        return i

    # ── receive_and_reassemble (phần sau validate header) ──
    def chunk(self, node, frame, idx, total, now):
        slot = next((i for i, s in enumerate(self.slots)
                     if s.active and s.node == node and s.frame == frame), -1)
        if slot < 0:
            if self.late and self._is_late(node, frame, now):
                self.late_chunk_drop += 1
                self.log.append(f"CHUNK TRE bo node={node} frame={frame} chunk {idx}")
                return
            for i, s in enumerate(self.slots):
                if s.active and s.node == node and s.frame != frame:
                    self.log.append(f"SUPERSEDE slot {i} node={node} frame cu={s.frame} {s.count}/{s.total} frame moi={frame}")
                    s.active = False
                    self.frames_dropped += 1
                    self._closed(node, s.frame, now)
            slot = self._allocate(node, frame, total, now)
            self._opened(node, frame, now)
        s = self.slots[slot]
        if idx in s.got:
            return
        s.got.add(idx)
        s.count += 1
        if s.count >= s.total:
            self.completed.append((node, frame))
            s.active = False
            self._closed(node, frame, now)

    # ── cleanup_stale_frames ──
    def cleanup(self, now):
        for i, s in enumerate(self.slots):
            if s.active and age_ms(now, s.start) > REASSEMBLY_TIMEOUT_MS:
                self.log.append(f"TIMEOUT slot {i} node={s.node} frame={s.frame} {s.count}/{s.total}")
                s.active = False
                self.frames_dropped += 1
                self._closed(s.node, s.frame, now)


def run(events, late):
    """events: ('c', t, node, frame, idx, total) | ('tick', t)"""
    r = Root(late)
    for e in events:
        if e[0] == 'c':
            _, t, node, frame, idx, total = e
            r.chunk(node, frame, idx, total, t)
            r.cleanup(t)          # loop(): cleanup_stale_frames() sau mesh.update()
        else:
            r.cleanup(e[1])
    return r


def frame_chunks(t0, node, frame, idxs, total=5, step=20):
    return [('c', t0 + k * step, node, frame, i, total) for k, i in enumerate(idxs)]


N = 1207369673
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail else ""))
    if not cond:
        fails += 1


def lost(r, frames, node=N):
    return [f for f in frames if (node, f) not in r.completed]


# ── Kịch bản 1: test15 — chunk 4 của frame 193 tới SAU khi frame 194 bắt đầu ──
ev = (frame_chunks(0, N, 193, [0, 1, 2, 3])
      + [('c', 100, N, 194, 0, 5)]
      + [('c', 110, N, 193, 4, 5)]                  # chunk trễ
      + frame_chunks(120, N, 194, [1, 2, 3, 4])
      + frame_chunks(600, N, 195, [0, 1, 2, 3, 4]))
old, new = run(ev, False), run(ev, True)
lo, ln = lost(old, [193, 194, 195]), lost(new, [193, 194, 195])
print("\n[1] test15: 193 (4/5) -> 194 bat dau -> chunk 4 cua 193 tre")
print("  cu :", old.log)
print("  moi:", new.log)
check("cu tai hien 193 o 2 slot (4/5 roi 1/5)",
      any("frame cu=193 4/5" in l for l in old.log) and any("frame cu=193 1/5" in l for l in old.log))
check("cu mat >= 2 frame (193 va 194)", len(lo) >= 2, f"mat {lo}, frames_dropped={old.frames_dropped}")
check("moi: 194, 195 ghep xong; chi mat 193 (da SUPERSEDE truoc khi chunk cuoi toi)",
      ln == [193], f"mat {ln}, frames_dropped={new.frames_dropped}")
check("moi: chunk tre khong gay mat them frame nao", len(ln) - 1 == 0)
check("moi: late_chunk_drop = 1, khong SUPERSEDE frame 194", new.late_chunk_drop == 1
      and not any("frame moi=193" in l for l in new.log))

# ── Kịch bản 2: frame 193 TIMEOUT rồi chunk cuối mới tới ──
ev = (frame_chunks(0, N, 193, [0, 1, 2, 3])
      + [('tick', 2100)]                            # TIMEOUT 193 (4/5)
      + [('c', 2150, N, 193, 4, 5)]                 # chunk trễ sau timeout
      + frame_chunks(2200, N, 194, [0, 1, 2, 3, 4]))
old, new = run(ev, False), run(ev, True)
print("\n[2] 193 TIMEOUT roi chunk cuoi toi tre")
check("cu: mo slot ma cho 193 (1/5) roi huy -> frames_dropped = 2", old.frames_dropped == 2,
      f"{old.log}")
check("moi: frames_dropped = 1 (chi TIMEOUT that), late_chunk_drop = 1",
      new.frames_dropped == 1 and new.late_chunk_drop == 1, f"{new.log}")
check("ca hai: 194 ghep xong", (N, 194) in old.completed and (N, 194) in new.completed)

# ── Kịch bản 3: chunk TRÙNG tới sau khi frame đã ghép xong ──
ev = (frame_chunks(0, N, 193, [0, 1, 2, 3, 4])
      + [('c', 150, N, 193, 4, 5)]                  # bản trùng (mesh gửi lại)
      + frame_chunks(600, N, 194, [0, 1, 2, 3, 4]))
old, new = run(ev, False), run(ev, True)
print("\n[3] chunk trung sau khi frame da ghep xong")
check("cu: frame da xong van bi dem 'huy' (frames_dropped = 1)", old.frames_dropped == 1, f"{old.log}")
check("moi: frames_dropped = 0, late_chunk_drop = 1", new.frames_dropped == 0 and new.late_chunk_drop == 1)

# ── Kịch bản 4: frame_id quay vòng 65535 -> 0 ──
ev = (frame_chunks(0, N, 65535, [0, 1, 2, 3, 4])
      + [('c', 600, N, 0, 0, 5)]
      + [('c', 610, N, 65535, 2, 5)]                # trễ của 65535 sau khi frame 0 mở
      + frame_chunks(620, N, 0, [1, 2, 3, 4]))
new = run(ev, True)
print("\n[4] quay vong frame_id 65535 -> 0")
check("moi: frame 0 sau 65535 duoc nhan, chunk tre cua 65535 bi bo",
      (N, 0) in new.completed and (N, 65535) in new.completed and new.late_chunk_drop == 1
      and new.frames_dropped == 0, f"{new.log}")

# ── Kịch bản 5: Leaf khởi động lại, frame_id về 0 ──
ev = (frame_chunks(0, N, 5000, [0, 1, 2, 3, 4])
      + frame_chunks(12000, N, 0, [0, 1, 2, 3, 4])      # 12 s sau (boot + vào mesh)
      + frame_chunks(12600, N, 1, [0, 1, 2, 3, 4]))
new = run(ev, True)
check("\n[5] Leaf reset (5000 -> 0 sau 12 s): frame 0, 1 duoc nhan",
      (N, 0) in new.completed and (N, 1) in new.completed and new.late_chunk_drop == 0)
ev = (frame_chunks(0, N, 10, [0, 1, 2, 3, 4])
      + frame_chunks(9000, N, 0, [0, 1, 2, 3, 4]))       # frame_id nhỏ, reset sau 9 s
new = run(ev, True)
check("[5b] Leaf reset khi frame_id con nho (10 -> 0 sau 9 s > cua so 4 s): van nhan",
      (N, 0) in new.completed and new.late_chunk_drop == 0)

# ── Kịch bản 6: không hồi quy — 3 Leaf gửi xen kẽ, đúng thứ tự, 50 frame/node ──
nodes = [1207433521, 1207449329, 1207369673]
ev = []
for f in range(50):
    for k, n in enumerate(nodes):
        ev += frame_chunks(f * 600 + k * 200, n, f, [0, 1, 2, 3, 4], step=15)
ev.sort(key=lambda e: e[1])
old, new = run(ev, False), run(ev, True)
check("\n[6] 3 Leaf xen ke, dung thu tu: cu va moi deu ghep du 150 frame, khong bo chunk",
      len(old.completed) == 150 and len(new.completed) == 150 and new.late_chunk_drop == 0
      and new.frames_dropped == old.frames_dropped == 0)

# ── Kịch bản 7: chunk ra khỏi thứ tự TRONG cùng frame vẫn ghép được ──
ev = [('c', 0, N, 7, 3, 5), ('c', 5, N, 7, 0, 5), ('c', 9, N, 7, 4, 5), ('c', 12, N, 7, 1, 5), ('c', 20, N, 7, 2, 5)]
new = run(ev, True)
check("[7] out-of-order trong 1 frame: van ghep xong, khong bo", (N, 7) in new.completed and new.late_chunk_drop == 0)

print(f"\n{'OK' if fails == 0 else 'LOI'}: {fails} ca that bai")
raise SystemExit(1 if fails else 0)
