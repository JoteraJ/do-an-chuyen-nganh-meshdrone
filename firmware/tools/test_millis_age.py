"""
test_millis_age.py — mô phỏng thứ tự gán thời gian trong update_node_tracking()
(node_root.ino), chứng minh lỗi "im lang ... 4294967295 ms" (test15) và bản sửa.

Mô phỏng uint32 của ESP32: mọi phép trừ & 0xFFFFFFFF.
millis() giả: mỗi lần gọi tăng thêm `step` ms (vd 1 ms trôi qua giữa 2 lần gọi).

Chạy:  python firmware/tools/test_millis_age.py
"""

U32 = 0xFFFFFFFF
SILENT_NODE_MS = 10000


class FakeMillis:
    def __init__(self, start, step):
        self.t = start
        self.step = step

    def __call__(self):
        v = self.t & U32
        self.t += self.step
        return v


def age_ms(now, ref):
    """Bản sửa: tuổi không âm, so sánh có dấu (đúng cả khi millis() quay vòng)."""
    d = (now - ref) & U32
    return 0 if d >= 0x80000000 else d


def tracking_old(millis):
    """Thứ tự CŨ: now lấy trước, seen_ms = millis() khác trong track_find()."""
    now = millis()                      # :736
    seen_ms = millis()                  # :651 (track_find(..., true))
    ref = seen_ms
    quiet = (now - ref) & U32           # :765
    return quiet, quiet > SILENT_NODE_MS


def tracking_new(millis):
    """Thứ tự MỚI: seen_ms = now của lượt cập nhật + age_ms()."""
    now = millis()
    seen_ms = now                       # track_find(..., true, now)
    quiet = age_ms(now, seen_ms)
    return quiet, quiet > SILENT_NODE_MS


def run():
    fails = 0

    def check(name, cond):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + name)
        if not cond:
            fails += 1

    # 1) Tái hiện lỗi: 1 ms trôi qua giữa 2 lần millis()
    q, s = tracking_old(FakeMillis(123456, 1))
    check("cu: tai hien 4294967295 ms, danh dau silent sai", q == 4294967295 and s)

    q, s = tracking_new(FakeMillis(123456, 1))
    check("moi: quiet = 0, khong silent", q == 0 and not s)

    # 2) Quét nhiều bước thời gian & mốc gần quay vòng millis()
    for start in (0, 1000, 0x7FFFFFFF, U32 - 5, U32):
        for step in (0, 1, 2, 7):
            q, s = tracking_new(FakeMillis(start, step))
            check(f"moi: start={start} step={step} -> quiet={q}", q == 0 and not s)

    # 3) age_ms: ref sau now → 0; tuổi thật vẫn đúng; qua mốc quay vòng
    check("age_ms(now, now+1) == 0", age_ms(100, 101) == 0)
    check("age_ms(now, now+5000) == 0", age_ms(100, 5100) == 0)
    check("age_ms(15000, 4000) == 11000", age_ms(15000, 4000) == 11000)
    check("age_ms qua quay vong: now=5, ref=U32-4 -> 10", age_ms(5, U32 - 4) == 10)
    check("age_ms(U32, 0) == 0 (ref sau now 1 ms, qua quay vong)", age_ms(U32, 0) == 0)
    check("age_ms(0, U32) == 1 (ref truoc now 1 ms, qua quay vong)", age_ms(0, U32) == 1)

    # 4) Ân hạn: node vừa vào lại mesh, chunk cuối đã cũ 60 s → chưa silent
    now, seen, last_chunk = 200000, 200000, 140000
    ref = seen if ((last_chunk - seen) & U32) >= 0x80000000 or last_chunk == 0 else last_chunk
    check("an han sau khi vao lai mesh", not (age_ms(now, ref) > SILENT_NODE_MS))
    # ... và sau SILENT_NODE_MS không chunk → silent
    now = seen + SILENT_NODE_MS + 1
    check("het an han -> silent", age_ms(now, ref) > SILENT_NODE_MS)

    print(f"\n{'OK' if fails == 0 else 'LOI'}: {fails} ca that bai")
    return fails


if __name__ == "__main__":
    raise SystemExit(1 if run() else 0)
