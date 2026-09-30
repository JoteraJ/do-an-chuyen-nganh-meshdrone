/**
 * test_raw_logger.js — Kiểm chứng ghi byte thô ngoài khung (raw_logger.js +
 * FrameParser opts.onSkip) mà không cần board.
 *
 * Chạy: node test_raw_logger.js
 */

'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { FRAME_TYPE, encodeFrame, FrameParser } = require('./protocol');
const { RawLogger } = require('./raw_logger');

function inKetQua(tenTest, ok, chiTiet) {
  console.log(`\n=== ${tenTest}: ${ok ? 'PASS' : 'FAIL'} ===`);
  if (chiTiet) console.log('  ' + chiTiet);
  return ok;
}

function tmpDir() { return fs.mkdtempSync(path.join(os.tmpdir(), 'rawlog-')); }
function readAll(files) { return files.map((f) => fs.readFileSync(f, 'utf8')).join(''); }

const WDT = 'E (123456) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:\r\n';

// Test 1: dòng WDT in thẳng giữa 2 khung hợp lệ -> vào file, khung vẫn tách đúng
function test1_textBetweenFrames() {
  const dir = tmpDir();
  const raw = new RawLogger({ dir, flushMs: 0 });
  const parser = new FrameParser({ onSkip: (b, r) => raw.write(b, r) });
  const f1 = encodeFrame(FRAME_TYPE.LOG, Buffer.from('truoc', 'utf8'));
  const f2 = encodeFrame(FRAME_TYPE.TELEMETRY, Buffer.from('{"ok":1}', 'utf8'));
  const frames = parser.push(Buffer.concat([f1, Buffer.from(WDT, 'ascii'), f2]));
  raw.close();
  const text = readAll(raw.files);
  const ok = frames.length === 2 && frames[1].type === FRAME_TYPE.TELEMETRY &&
    text.includes('[skip] E (123456) task_wdt: Task watchdog got triggered.') &&
    !text.includes('\\x0d') && parser.stats.bytesSkipped === WDT.length;
  return inKetQua('Test 1: dong WDT giua 2 khung -> file raw, 2 khung van dung', ok, text.trim());
}

// Test 2: dòng WDT chen GIỮA một khung ảnh (ESP32 in thẳng vào FIFO) -> khung sai
// CRC; chữ vẫn phải nằm trong file (lý do 'crc'), khung sau vẫn đọc được
function test2_textInsideFrame() {
  const dir = tmpDir();
  const raw = new RawLogger({ dir, flushMs: 0 });
  const parser = new FrameParser({ onSkip: (b, r) => raw.write(b, r) });
  const img = encodeFrame(FRAME_TYPE.IMAGE, Buffer.alloc(300, 0x41));
  const cut = 150;
  const good = encodeFrame(FRAME_TYPE.LOG, Buffer.from('sau reset', 'utf8'));
  // Chữ chen vào giữa, phần khung còn lại vẫn tới đủ -> đủ LEN byte, CRC sai
  const stream = Buffer.concat([img.slice(0, cut), Buffer.from(WDT, 'ascii'), img.slice(cut), good]);
  let frames = [];
  for (let off = 0; off < stream.length; off += 17) frames = frames.concat(parser.push(stream.slice(off, off + 17)));
  raw.close();
  const text = readAll(raw.files);
  const ok = parser.stats.framesBadCrc === 1 &&
    frames.some((f) => f.type === FRAME_TYPE.LOG && f.payload.toString() === 'sau reset') &&
    text.includes('Task watchdog got triggered') && text.includes('[crc]') && text.includes('\\xaa');
  return inKetQua('Test 2: dong WDT chen giua khung anh -> van co trong file (crc)', ok,
    `framesBadCrc=${parser.stats.framesBadCrc}, khung doc duoc=${frames.length}`);
}

// Test 3: định dạng: ASCII giữ nguyên, byte khác \xNN, '\' -> '\\', '\r' lẻ -> \x0d
function test3_format() {
  const dir = tmpDir();
  const raw = new RawLogger({ dir, flushMs: 0, now: () => new Date('2026-10-01T01:02:03.000Z') });
  raw.write(Buffer.from([0x41, 0x00, 0xff, 0x5c, 0x0d, 0x42, 0x0a]), 'skip');
  raw.close();
  const text = readAll(raw.files);
  const ok = text === '2026-10-01T01:02:03.000Z [skip] A\\x00\\xff\\\\\\x0dB\n';
  return inKetQua('Test 3: dinh dang dong', ok, JSON.stringify(text));
}

// Test 4: xoay file khi vượt maxBytes, không mất dòng nào
function test4_rotate() {
  const dir = tmpDir();
  let t = Date.parse('2026-10-01T00:00:00Z');
  const raw = new RawLogger({ dir, flushMs: 0, maxBytes: 2000, now: () => new Date((t += 1000)) });
  const N = 100;
  for (let i = 0; i < N; i++) { raw.write(Buffer.from(`dong ${i}\n`, 'ascii')); raw.flush(); }
  raw.close();
  const lines = readAll(raw.files).split('\n').filter(Boolean);
  const sizesOk = raw.files.slice(0, -1).every((f) => fs.statSync(f).size >= 2000);
  const ok = raw.files.length > 1 && lines.length === N && lines[N - 1].endsWith('dong 99') && sizesOk;
  return inKetQua('Test 4: xoay file khi du maxBytes', ok, `${raw.files.length} file, ${lines.length} dong`);
}

// Test 5: không có onSkip -> parser chạy như cũ (không lỗi)
function test5_noHook() {
  const parser = new FrameParser();
  const frames = parser.push(Buffer.concat([Buffer.from('rac\n'), encodeFrame(FRAME_TYPE.LOG, Buffer.from('x'))]));
  return inKetQua('Test 5: khong co onSkip van chay nhu cu', frames.length === 1 && parser.stats.bytesSkipped === 4);
}

const results = [test1_textBetweenFrames(), test2_textInsideFrame(), test3_format(), test4_rotate(), test5_noHook()];
console.log('\n' + '='.repeat(60));
const pass = results.filter(Boolean).length;
console.log(`TONG KET: ${pass}/${results.length} test PASS`);
console.log('='.repeat(60));
if (pass !== results.length) process.exitCode = 1;
