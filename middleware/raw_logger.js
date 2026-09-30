/**
 * raw_logger.js — Ghi byte UART KHÔNG thuộc khung hợp lệ ra file text.
 *
 * Mục đích: bắt được dòng ESP32 in thẳng ra UART (không đóng khung), vd
 * "Task watchdog got triggered ..." ngay trước khi Root reset, hay log
 * bootloader sau reset. FrameParser gọi write() qua opts.onSkip (protocol.js).
 *
 * Định dạng mỗi dòng:  <ISO time> [skip|crc] <nội dung>
 *   - ASCII in được (0x20..0x7E) giữ nguyên, '\' ghi thành '\\'
 *   - '\n' kết thúc dòng ('\r' ngay trước '\n' bỏ đi), byte khác -> \xNN
 *   - 1 dòng dài quá MAX_LINE_CHARS thì cắt sang dòng mới (khối nhị phân lớn)
 * File: <dir>/uart_raw_YYYYMMDD_HHMMSS.log, mở lúc start, ghi đĩa mỗi 1 s,
 * đủ maxBytes (20 MB) thì đóng và mở file mới (xoay file).
 * Không bao giờ ném lỗi ra ngoài: lỗi đĩa chỉ in cảnh báo, không ảnh hưởng
 * đường xử lý khung.
 */

'use strict';

const fs = require('fs');
const path = require('path');

const MAX_LINE_CHARS = 1024;

function pad(n) { return String(n).padStart(2, '0'); }

function fileStamp(d) {
  return `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}_` +
    `${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
}

class RawLogger {
  constructor({ dir, maxBytes = 20 * 1024 * 1024, flushMs = 1000, now = () => new Date() } = {}) {
    this.dir = dir;
    this.maxBytes = maxBytes;
    this.now = now;
    this.fd = null;
    this.file = null;
    this.fileBytes = 0;
    this.pending = [];        // các dòng hoàn chỉnh chờ ghi đĩa
    this.line = '';           // dòng đang dở
    this.lineTag = null;
    this.lineTime = null;
    this.lastCr = false;      // byte trước là '\r' chưa in
    this.totalBytes = 0;      // tổng byte thô đã nhận (thống kê)
    this.files = [];          // các file đã mở (thống kê/test)
    this._open();
    this.timer = flushMs > 0 ? setInterval(() => this.flush(), flushMs) : null;
    if (this.timer && this.timer.unref) this.timer.unref();
  }

  _open() {
    try {
      fs.mkdirSync(this.dir, { recursive: true });
      let name = `uart_raw_${fileStamp(this.now())}.log`;
      // 2 lần xoay trong cùng 1 giây -> thêm hậu tố để không ghi đè
      for (let k = 1; fs.existsSync(path.join(this.dir, name)); k++) {
        name = `uart_raw_${fileStamp(this.now())}_${k}.log`;
      }
      this.file = path.join(this.dir, name);
      this.fd = fs.openSync(this.file, 'a');
      this.fileBytes = 0;
      this.files.push(this.file);
    } catch (e) {
      this.fd = null;
      console.warn('[RAW] Khong mo duoc file log byte tho:', e.message);
    }
  }

  _endLine() {
    if (this.lineTime == null) return;
    this.pending.push(`${this.lineTime.toISOString()} [${this.lineTag}] ${this.line}\n`);
    this.line = '';
    this.lineTime = null;
  }

  _startLineIfNeeded(tag) {
    if (this.lineTime != null && this.lineTag !== tag) this._endLine();
    if (this.lineTime == null) {
      this.lineTime = this.now();
      this.lineTag = tag;
    }
  }

  /** Nhận 1 đoạn byte thô (Buffer), reason = 'skip' | 'crc'. */
  write(buf, reason = 'skip') {
    this.totalBytes += buf.length;
    for (const b of buf) {
      if (b === 0x0a) {                      // '\n' kết thúc dòng
        this.lastCr = false;
        this._startLineIfNeeded(reason);
        this._endLine();
        continue;
      }
      if (this.lastCr) {                     // '\r' lẻ (không đi trước '\n')
        this._startLineIfNeeded(reason);
        this.line += '\\x0d';
        this.lastCr = false;
      }
      if (b === 0x0d) { this.lastCr = true; continue; }
      this._startLineIfNeeded(reason);
      if (b === 0x5c) this.line += '\\\\';
      else if (b >= 0x20 && b <= 0x7e) this.line += String.fromCharCode(b);
      else this.line += '\\x' + b.toString(16).padStart(2, '0');
      if (this.line.length >= MAX_LINE_CHARS) this._endLine();
    }
  }

  /** Ghi đĩa: các dòng hoàn chỉnh + dòng đang dở (dòng dở in ra, dòng sau có mốc mới). */
  flush() {
    if (this.lastCr) { this._startLineIfNeeded(this.lineTag || 'skip'); this.line += '\\x0d'; this.lastCr = false; }
    this._endLine();
    if (!this.pending.length) return;
    const text = this.pending.join('');
    this.pending = [];
    if (this.fd == null) return;
    try {
      const data = Buffer.from(text, 'utf8');
      fs.writeSync(this.fd, data);
      this.fileBytes += data.length;
      if (this.fileBytes >= this.maxBytes) {
        fs.closeSync(this.fd);
        this._open();
      }
    } catch (e) {
      console.warn('[RAW] Loi ghi file log byte tho:', e.message);
    }
  }

  close() {
    if (this.timer) clearInterval(this.timer);
    this.flush();
    if (this.fd != null) {
      try { fs.closeSync(this.fd); } catch (e) { /* bỏ qua */ }
      this.fd = null;
    }
  }
}

module.exports = { RawLogger };
