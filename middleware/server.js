/**
 * server.js — Middleware Node.js (Lớp 2 trong kiến trúc 3 lớp của đồ án)
 *
 *   Root Node (ESP32) --UART 921600bps--> [server.js] --Socket.IO--> Dashboard
 *
 * Nhiệm vụ:
 *   1. Mở cổng Serial (COM port) tới Root Node, đúng UART_PROTOCOL.md.
 *   2. Tách khung nhị phân (FrameParser trong protocol.js).
 *   3. IMAGE_FRAME  -> phát sự kiện 'frame' (base64 JPEG) qua Socket.IO.
 *      TELEMETRY    -> phát sự kiện 'telemetry' (JSON) qua Socket.IO.
 *      LOG          -> in ra console PC (thay cho Serial Monitor trực tiếp).
 *   4. Serve luôn dashboard tĩnh trong public/ tại http://localhost:3000
 *
 * Chạy:
 *   npm install
 *   node server.js COM5          (Windows — xem tên cổng trong Device Manager)
 *   node server.js /dev/ttyUSB0  (Linux)
 *
 * Có thể set qua biến môi trường thay vì đối số dòng lệnh:
 *   COM_PORT=COM5 BAUD_RATE=921600 HTTP_PORT=3000 node server.js
 */

'use strict';

const path = require('path');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const { SerialPort } = require('serialport');
const { FRAME_TYPE, FrameParser } = require('./protocol');

const PORT_NAME = process.argv[2] || process.env.COM_PORT || 'COM5';
const BAUD_RATE = parseInt(process.env.BAUD_RATE || '921600', 10);
const HTTP_PORT = parseInt(process.env.HTTP_PORT || '3000', 10);
const RECONNECT_DELAY_MS = 3000;

const app = express();
app.use(express.static(path.join(__dirname, 'public')));
// public/ chỉ có dashboard.html (không phải index.html), nên
// express.static không tự động serve nó cho route '/' -> "Cannot GET /".
// Thêm route rõ ràng để trả đúng file dashboard khi vào http://localhost:3000
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'dashboard.html'));
});
const httpServer = http.createServer(app);
const io = new Server(httpServer, { maxHttpBufferSize: 2e6 });

const parser = new FrameParser();

// Trạng thái mới nhất, để client mới kết nối vào có ngay dữ liệu thay vì chờ
// tới lần telemetry/frame kế tiếp.
let latestTelemetry = null;
let latestFrameMeta = null;
let framesRelayed = 0;
let serialConnected = false;
let currentPort = null;

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

function handleFrame(frame) {
  switch (frame.type) {
    case FRAME_TYPE.LOG: {
      console.log('[ESP32]', frame.payload.toString('utf8'));
      break;
    }
    case FRAME_TYPE.TELEMETRY: {
      let telemetry;
      try {
        telemetry = JSON.parse(frame.payload.toString('utf8'));
      } catch (e) {
        log('[TELEMETRY] JSON khong hop le, bo qua:', e.message);
        return;
      }
      latestTelemetry = telemetry;
      io.emit('telemetry', telemetry);
      break;
    }
    case FRAME_TYPE.IMAGE: {
      if (frame.payload.length < 10) {
        log('[IMAGE] Payload qua ngan, bo qua');
        return;
      }
      const nodeId = frame.payload.readUInt32LE(0);
      const frameId = frame.payload.readUInt16LE(4);
      const jpegLen = frame.payload.readUInt32LE(6);
      const jpeg = frame.payload.slice(10, 10 + jpegLen);
      if (jpeg.length !== jpegLen) {
        log(`[IMAGE] jpeg_len khai bao (${jpegLen}) khong khop du lieu thuc (${jpeg.length}), bo qua`);
        return;
      }
      framesRelayed++;
      latestFrameMeta = { nodeId, frameId, bytes: jpeg.length, ts: Date.now() };
      io.emit('frame', { nodeId, frameId, jpegBase64: jpeg.toString('base64') });
      break;
    }
    default:
      log('[PARSER] Loai frame khong ro:', frame.type);
  }
}

function openSerial() {
  log(`[SERIAL] Dang mo ${PORT_NAME} @ ${BAUD_RATE} baud...`);
  const port = new SerialPort({ path: PORT_NAME, baudRate: BAUD_RATE, autoOpen: true }, (err) => {
    if (err) {
      serialConnected = false;
      log(`[SERIAL] Khong mo duoc cong ${PORT_NAME}: ${err.message}`);
      log(`[SERIAL] Kiem tra: dung ten COM (Device Manager) chua, board da cam dung chua,`);
      log(`[SERIAL] co chuong trinh khac (Arduino Serial Monitor...) dang giu cong khong.`);
      log(`[SERIAL] Thu lai sau ${RECONNECT_DELAY_MS / 1000}s...`);
      setTimeout(openSerial, RECONNECT_DELAY_MS);
    }
  });

  port.on('open', () => {
    serialConnected = true;
    currentPort = port;
    log(`[SERIAL] Da ket noi ${PORT_NAME}`);
  });

  port.on('data', (chunk) => {
    let frames;
    try {
      frames = parser.push(chunk);
    } catch (e) {
      log('[PARSER] Loi khong mong doi:', e.message);
      return;
    }
    for (const f of frames) handleFrame(f);
  });

  port.on('close', () => {
    serialConnected = false;
    log('[SERIAL] Cong bi dong, thu ket noi lai...');
    setTimeout(openSerial, RECONNECT_DELAY_MS);
  });

  port.on('error', (err) => {
    log('[SERIAL] Loi:', err.message);
  });
}

app.get('/status', (req, res) => {
  res.json({
    serialPort: PORT_NAME,
    baudRate: BAUD_RATE,
    serialConnected,
    framesRelayed,
    latestFrameMeta,
    latestTelemetry,
    parserStats: parser.stats,
  });
});

io.on('connection', (socket) => {
  log('[WEB] Dashboard client connected:', socket.id);
  socket.emit('status', { serialConnected, portName: PORT_NAME, baudRate: BAUD_RATE });
  if (latestTelemetry) socket.emit('telemetry', latestTelemetry);
});

openSerial();

httpServer.listen(HTTP_PORT, () => {
  log(`[HTTP] Dashboard: http://localhost:${HTTP_PORT}`);
  log(`[HTTP] Status API: http://localhost:${HTTP_PORT}/status`);
});

process.on('SIGINT', () => {
  log('Dang tat middleware...');
  if (currentPort && currentPort.isOpen) currentPort.close();
  process.exit(0);
});
