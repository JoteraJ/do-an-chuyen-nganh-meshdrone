/**
 * protocol.js — Cài đặt UART_PROTOCOL.md (framing + CRC16) cho middleware
 *
 * Xem ../UART_PROTOCOL.md để biết đặc tả đầy đủ. File này thuần logic,
 * không phụ thuộc serialport/socket.io, để có thể unit-test độc lập
 * (xem test_protocol.js) — cùng tinh thần với mo_phong_reassembly.py
 * mà bạn đã dùng để kiểm chứng thuật toán reassembly khi chưa có board thật.
 */

'use strict';

const FRAME_TYPE = Object.freeze({
  IMAGE: 0x01,
  TELEMETRY: 0x02,
  LOG: 0x03,
});

const MAGIC0 = 0xAA;
const MAGIC1 = 0x55;
const HEADER_LEN = 7; // 2 magic + 1 type + 4 len
const CRC_LEN = 2;
const MAX_PAYLOAD = 131072; // phải khớp giới hạn mô tả trong UART_PROTOCOL.md

/** CRC-16/CCITT-FALSE — PHẢI khớp với shared/crc16.h (cùng thuật toán, cùng input -> cùng output) */
function crc16CcittFalse(buf) {
  let crc = 0xFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i] << 8;
    for (let b = 0; b < 8; b++) {
      if (crc & 0x8000) {
        crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
      } else {
        crc = (crc << 1) & 0xFFFF;
      }
    }
  }
  return crc & 0xFFFF;
}

/**
 * Đóng gói 1 khung hoàn chỉnh (dùng khi TEST hoặc khi cần giả lập firmware
 * từ phía Node.js — middleware thật sự không cần gọi hàm này lúc chạy vì
 * chỉ NHẬN khung từ ESP32, nhưng test_protocol.js dùng nó để sinh dữ liệu giả).
 */
function encodeFrame(type, payload) {
  if (!Buffer.isBuffer(payload)) payload = Buffer.from(payload);
  const len = payload.length;
  const header = Buffer.alloc(HEADER_LEN);
  header[0] = MAGIC0;
  header[1] = MAGIC1;
  header[2] = type;
  header.writeUInt32LE(len, 3);

  const crcInput = Buffer.concat([header.slice(2), payload]); // TYPE..PAYLOAD
  const crc = crc16CcittFalse(crcInput);
  const crcBuf = Buffer.alloc(2);
  crcBuf.writeUInt16LE(crc, 0);

  return Buffer.concat([header, payload, crcBuf]);
}

/**
 * FrameParser — bộ tách khung stateful cho luồng byte đến từ serial port.
 * push(chunk) trả về mảng các khung { type, payload } đã tách được (có thể
 * rỗng nếu chưa đủ dữ liệu). An toàn khi gọi liên tục với các Buffer nhỏ
 * (giống cách serialport phát sự kiện 'data').
 */
class FrameParser {
  /**
   * opts.onSkip(buf, reason) — tuỳ chọn: nhận MỌI byte không thuộc khung hợp lệ,
   * đúng thứ tự trên dây ('skip' = rác trước MAGIC / LEN vô lý, 'crc' = cả khung
   * sai CRC). Chỉ quan sát, không đổi cách tách khung hay stats. Dùng để ghi byte
   * thô (vd dòng "Task watchdog got triggered" ESP32 in thẳng ra UART).
   */
  constructor(opts = {}) {
    this._buf = Buffer.alloc(0);
    this.stats = { framesOk: 0, framesBadCrc: 0, bytesSkipped: 0 };
    this._onSkip = typeof opts.onSkip === 'function' ? opts.onSkip : null;
  }

  _skip(n, reason) {
    if (this._onSkip && n > 0) this._onSkip(this._buf.slice(0, n), reason);
  }

  push(chunk) {
    this._buf = this._buf.length ? Buffer.concat([this._buf, chunk]) : Buffer.from(chunk);
    const out = [];

    for (;;) {
      const magicIdx = this._findMagic(this._buf);
      if (magicIdx === -1) {
        // Không thấy MAGIC đầy đủ. Giữ lại tối đa 1 byte (phòng trường hợp
        // byte cuối cùng đúng là MAGIC0, phần MAGIC1 sẽ đến ở chunk kế tiếp).
        if (this._buf.length > 1) {
          this._skip(this._buf.length - 1, 'skip');
          this.stats.bytesSkipped += this._buf.length - 1;
          this._buf = this._buf.slice(this._buf.length - 1);
        }
        break;
      }
      if (magicIdx > 0) {
        this._skip(magicIdx, 'skip');
        this.stats.bytesSkipped += magicIdx;
        this._buf = this._buf.slice(magicIdx);
      }

      if (this._buf.length < HEADER_LEN) break; // chờ thêm byte cho đủ header

      const type = this._buf[2];
      const len = this._buf.readUInt32LE(3);

      if (len > MAX_PAYLOAD) {
        // LEN vô lý -> MAGIC này chắc chắn là rác trùng ngẫu nhiên, bỏ 1 byte rồi quét lại
        this._skip(1, 'skip');
        this._buf = this._buf.slice(1);
        this.stats.bytesSkipped += 1;
        continue;
      }

      const totalLen = HEADER_LEN + len + CRC_LEN;
      if (this._buf.length < totalLen) break; // chưa đủ, chờ thêm dữ liệu

      const payload = this._buf.slice(HEADER_LEN, HEADER_LEN + len);
      const crcReceived = this._buf.readUInt16LE(HEADER_LEN + len);
      const crcCalc = crc16CcittFalse(this._buf.slice(2, HEADER_LEN + len));

      if (crcCalc !== crcReceived) this._skip(totalLen, 'crc');
      this._buf = this._buf.slice(totalLen);

      if (crcCalc !== crcReceived) {
        this.stats.framesBadCrc += 1;
        continue; // bỏ khung hỏng, không dừng toàn bộ luồng
      }

      this.stats.framesOk += 1;
      out.push({ type, payload });
    }

    return out;
  }

  _findMagic(buf) {
    for (let i = 0; i <= buf.length - 2; i++) {
      if (buf[i] === MAGIC0 && buf[i + 1] === MAGIC1) return i;
    }
    return -1;
  }
}

module.exports = { FRAME_TYPE, crc16CcittFalse, encodeFrame, FrameParser, MAX_PAYLOAD };
