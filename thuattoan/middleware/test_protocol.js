/**
 * test_protocol.js — Kiểm chứng FrameParser (protocol.js) BẰNG PHẦN MỀM,
 * không cần board ESP32 thật, cùng tinh thần với mo_phong_reassembly.py.
 *
 * Chạy: node test_protocol.js
 */

'use strict';

const assert = require('assert');
const { FRAME_TYPE, encodeFrame, FrameParser, crc16CcittFalse } = require('./protocol');

function inKetQua(tenTest, ok, chiTiet) {
  console.log(`\n=== ${tenTest}: ${ok ? 'PASS' : 'FAIL'} ===`);
  if (chiTiet) console.log('  ' + chiTiet);
  return ok;
}

// ---------------------------------------------------------------
// Test 1: CRC khớp giữa 2 lần tính (đối chiếu chéo với giá trị cố định)
// để đảm bảo không lỡ tay đổi thuật toán làm lệch với bản C trong crc16.h
// ---------------------------------------------------------------
function test1_crcKnownVector() {
  // "123456789" -> CRC-16/CCITT-FALSE chuẩn (test vector công khai) = 0x29B1
  const crc = crc16CcittFalse(Buffer.from('123456789', 'ascii'));
  const ok = crc === 0x29b1;
  return inKetQua('Test 1: CRC16/CCITT-FALSE test vector chuẩn', ok, `crc=0x${crc.toString(16)} (kỳ vọng 0x29b1)`);
}

// ---------------------------------------------------------------
// Test 2: Encode rồi decode 1 khung LOG đơn giản, nguyên vẹn trong 1 lần push
// ---------------------------------------------------------------
function test2_singleFrame() {
  const parser = new FrameParser();
  const frame = encodeFrame(FRAME_TYPE.LOG, Buffer.from('[MESH] Parent connected', 'utf8'));
  const frames = parser.push(frame);
  const ok = frames.length === 1 && frames[0].type === FRAME_TYPE.LOG &&
    frames[0].payload.toString('utf8') === '[MESH] Parent connected';
  return inKetQua('Test 2: 1 khung LOG nguyên vẹn', ok);
}

// ---------------------------------------------------------------
// Test 3: Khung TELEMETRY (JSON) + khung IMAGE giả (random bytes) nối liền nhau
// ---------------------------------------------------------------
function test3_multiFrameConcat() {
  const parser = new FrameParser();
  const telemetry = { ts: 123, frames_ok: 5, node_ids: [1, 2, 3] };
  const imagePayload = Buffer.concat([
    (() => { const b = Buffer.alloc(10); b.writeUInt32LE(999, 0); b.writeUInt16LE(7, 4); b.writeUInt32LE(4, 6); return b; })(),
    Buffer.from([0xff, 0xd8, 0xff, 0xd9]),
  ]);
  const stream = Buffer.concat([
    encodeFrame(FRAME_TYPE.TELEMETRY, Buffer.from(JSON.stringify(telemetry), 'utf8')),
    encodeFrame(FRAME_TYPE.IMAGE, imagePayload),
  ]);
  const frames = parser.push(stream);
  const ok = frames.length === 2 &&
    frames[0].type === FRAME_TYPE.TELEMETRY &&
    JSON.parse(frames[0].payload.toString('utf8')).frames_ok === 5 &&
    frames[1].type === FRAME_TYPE.IMAGE &&
    frames[1].payload.readUInt32LE(0) === 999;
  return inKetQua('Test 3: 2 khung khác loại nối liền (TELEMETRY + IMAGE)', ok);
}

// ---------------------------------------------------------------
// Test 4: Mô phỏng serial port chia nhỏ dữ liệu thành từng mảnh vài byte
// (giống thực tế node.on('data') nhận từng đợt nhỏ, không nguyên khung)
// ---------------------------------------------------------------
function test4_fragmentedChunks() {
  const parser = new FrameParser();
  const payload = Buffer.alloc(5000);
  for (let i = 0; i < payload.length; i++) payload[i] = i & 0xff;
  const full = encodeFrame(FRAME_TYPE.IMAGE, payload);

  let received = [];
  const CHUNK_SIZE = 7; // cố tình rất nhỏ để cắt ngang header, CRC, payload
  for (let off = 0; off < full.length; off += CHUNK_SIZE) {
    const piece = full.slice(off, Math.min(off + CHUNK_SIZE, full.length));
    received = received.concat(parser.push(piece));
  }
  const ok = received.length === 1 && received[0].payload.equals(payload);
  return inKetQua('Test 4: Dữ liệu bị cắt vụn từng 7 byte vẫn ghép đúng', ok);
}

// ---------------------------------------------------------------
// Test 5: Rác xen giữa 2 khung (mô phỏng nhiễu đường truyền / lúc mất đồng bộ
// khi middleware mới mở cổng COM giữa chừng lúc ESP32 đang gửi) -> phải bỏ
// qua rác, không rơi cả 2 khung hợp lệ xung quanh
// ---------------------------------------------------------------
function test5_garbageBetweenFrames() {
  const parser = new FrameParser();
  const garbage = Buffer.from([0x00, 0x11, 0xaa, 0x22, 0x55, 0x33]); // có cả chuỗi con giống MAGIC nhưng lệch vị trí
  const f1 = encodeFrame(FRAME_TYPE.LOG, Buffer.from('truoc rac', 'utf8'));
  const f2 = encodeFrame(FRAME_TYPE.LOG, Buffer.from('sau rac', 'utf8'));
  const stream = Buffer.concat([f1, garbage, f2]);
  const frames = parser.push(stream);
  const ok = frames.length === 2 &&
    frames[0].payload.toString('utf8') === 'truoc rac' &&
    frames[1].payload.toString('utf8') === 'sau rac';
  return inKetQua('Test 5: Rác xen giữa 2 khung vẫn khôi phục được cả 2', ok,
    `nhận được ${frames.length} khung, bytesSkipped=${parser.stats.bytesSkipped}`);
}

// ---------------------------------------------------------------
// Test 6: Khung bị hỏng CRC (giả lập nhiễu điện làm sai 1 byte payload)
// -> phải bị loại bỏ, nhưng khung hợp lệ NGAY SAU đó vẫn phải đọc được
// ---------------------------------------------------------------
function test6_corruptedCrcThenRecover() {
  const parser = new FrameParser();
  const bad = encodeFrame(FRAME_TYPE.LOG, Buffer.from('se bi loi', 'utf8'));
  bad[10] ^= 0xff; // phá 1 byte trong payload -> CRC sẽ không khớp
  const good = encodeFrame(FRAME_TYPE.LOG, Buffer.from('van doc duoc', 'utf8'));
  const frames = parser.push(Buffer.concat([bad, good]));
  const ok = frames.length === 1 && frames[0].payload.toString('utf8') === 'van doc duoc' &&
    parser.stats.framesBadCrc === 1;
  return inKetQua('Test 6: Khung sai CRC bị loại, khung sau vẫn đọc được', ok);
}

// ---------------------------------------------------------------
// Test 7: LEN giả mạo cực lớn (mô phỏng nhiễu làm sai 4 byte LEN) không được
// làm middleware "treo" chờ vô hạn hoặc crash — phải tự resync
// ---------------------------------------------------------------
function test7_bogusHugeLen() {
  const parser = new FrameParser();
  const bogus = encodeFrame(FRAME_TYPE.LOG, Buffer.from('x', 'utf8'));
  bogus.writeUInt32LE(0xffffffff, 3); // LEN giả, vượt MAX_PAYLOAD -> phải bị coi là rác
  const good = encodeFrame(FRAME_TYPE.TELEMETRY, Buffer.from('{"ok":true}', 'utf8'));
  const frames = parser.push(Buffer.concat([bogus, good]));
  const ok = frames.length === 1 && frames[0].type === FRAME_TYPE.TELEMETRY;
  return inKetQua('Test 7: LEN giả mạo khổng lồ không làm kẹt parser', ok);
}

// ---------------------------------------------------------------
// Test 8: TELEMETRY có các trường mới (Root 2026-09-30d, nhánh feature/on-dinh):
// silent_events, heal_events, uart_drop, nodes[].last_chunk_age_ms/silent/rst/up_s
// (kể cả giá trị null của node firmware cũ). Khung bị cắt vụn vẫn ghép đúng và
// JSON parse ra đủ trường.
// ---------------------------------------------------------------
function test8_telemetryNewFields() {
  const parser = new FrameParser();
  const telemetry = {
    ts: 123456, role: 'root', layer: 0, fw: '2026-09-30d',
    frames_ok: 42, frames_drop: 3, chunks: 610, chunk_err: 0,
    heap_free: 98304, psram_free: 0, uart_drop: 2,
    node_count: 3, node_ids: [1207433521, 1207449329, 1207369673],
    last_self_heal_ms: 842, silent_events: 1,
    heal_events: [{ id: 1207369673, ms: 4120, start_src: 'list', ts: 123400 },
                  { id: 1207449329, ms: 11020, start_src: 'last_chunk', ts: 123410 }],
    lat_avg_ms: 177.2, lat_min_ms: 90.1, lat_max_ms: 402.5, lat_n: 9, lat_by_node: { 1207433521: 96.5 },
    tree: { nodeId: 2846999225, root: true, subs: [{ nodeId: 1207433521, subs: [{ nodeId: 1207369673 }] }, { nodeId: 1207449329 }] },
    nodes: [
      { id: 1207433521, fw: '2026-09-30c', rssi: -58, heap: 101234, link_rssi: -61, link_rssi_src: 'ap_parent', hop: 1,
        last_chunk_age_ms: 240, silent: false, rst: 1, up_s: 3605, age_ms: 820 },
      { id: 1207369673, fw: '2026-09-30c', rssi: -77, heap: 99876, link_rssi: -79, link_rssi_src: 'ap_parent', hop: 2,
        last_chunk_age_ms: 12450, silent: true, rst: 9, up_s: 41, age_ms: 1430 },
      { id: 1207449329, fw: null, rssi: null, heap: null, link_rssi: -70, link_rssi_src: 'ap_parent', hop: 1,
        last_chunk_age_ms: null, silent: false, rst: null, up_s: null, age_ms: null },
    ],
  };
  const full = encodeFrame(FRAME_TYPE.TELEMETRY, Buffer.from(JSON.stringify(telemetry), 'utf8'));
  let frames = [];
  for (let off = 0; off < full.length; off += 13) frames = frames.concat(parser.push(full.slice(off, off + 13)));
  let t = null;
  try { t = frames.length === 1 ? JSON.parse(frames[0].payload.toString('utf8')) : null; } catch (e) { t = null; }
  const ok = !!t && frames[0].type === FRAME_TYPE.TELEMETRY &&
    t.uart_drop === 2 && t.silent_events === 1 &&
    Array.isArray(t.heal_events) && t.heal_events.length === 2 &&
    t.heal_events[0].start_src === 'list' && t.heal_events[1].ms === 11020 &&
    t.nodes[1].silent === true && t.nodes[1].rst === 9 && t.nodes[1].up_s === 41 &&
    t.nodes[2].rst === null && t.nodes[2].last_chunk_age_ms === null &&
    !t.node_ids.includes(t.tree.nodeId);
  return inKetQua('Test 8: TELEMETRY có trường mới (heal_events, silent, rst, up_s, uart_drop)', ok,
    `payload ${full.length - 9} byte`);
}

const results = [
  test1_crcKnownVector(),
  test2_singleFrame(),
  test3_multiFrameConcat(),
  test4_fragmentedChunks(),
  test5_garbageBetweenFrames(),
  test6_corruptedCrcThenRecover(),
  test7_bogusHugeLen(),
  test8_telemetryNewFields(),
];

console.log('\n' + '='.repeat(60));
const pass = results.filter(Boolean).length;
console.log(`TONG KET: ${pass}/${results.length} test PASS`);
console.log('='.repeat(60));

if (pass !== results.length) {
  process.exitCode = 1;
}
