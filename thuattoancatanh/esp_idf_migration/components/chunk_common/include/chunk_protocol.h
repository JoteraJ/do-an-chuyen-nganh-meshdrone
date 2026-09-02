/**
 * ============================================================
 *  chunk_protocol.h — Giao thuc dong goi/giai goi chunk
 *  (ban ESP-IDF — KHONG con Base64, gui nhi phan truc tiep)
 * ============================================================
 *
 *  So voi ban painlessMesh cu (shared/chunk_protocol.h):
 *    - Bo hoan toan buoc Base64 encode/decode. ESP-WIFI-MESH
 *      truyen nhi phan qua mesh_data_t, khong bat buoc phai la
 *      text nhu painlessMesh (String-based).
 *    - build_chunk_packet()/parse_chunk_packet() thay cho
 *      serialize_chunk()/deserialize_chunk() — chi memcpy,
 *      khong encode/decode gi them.
 *    - Loi ich: giam ~33% du lieu truyen di (Base64 tang size),
 *      giam CPU (khong encode/decode), throughput do duoc se
 *      sat voi ly thuyet 802.11 hon.
 *
 *  Cau truc header GIU NGUYEN 12 byte nhu ban cu de bao dam
 *  tinh nhat quan voi phan bao cao/tai lieu da viet.
 * ============================================================ */

#ifndef CHUNK_PROTOCOL_H
#define CHUNK_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "mesh_config.h"

#pragma pack(push, 1)
typedef struct {
    uint32_t node_id;        // ID node gui (mesh_addr_t 6 byte -> lay 4 byte cuoi cua MAC lam id)
    uint16_t frame_id;       // So thu tu frame (wrap-around 0-65535)
    uint16_t chunk_index;    // Vi tri chunk trong frame (0-based)
    uint16_t total_chunks;   // Tong so chunk cua frame
    uint16_t payload_len;    // Kich thuoc payload thuc te (<= MAX_CHUNK_PAYLOAD)
} chunk_header_t;
#pragma pack(pop)

#define CHUNK_HEADER_SIZE   (sizeof(chunk_header_t))   // = 12 byte neu pack dung

/* Buffer toi da can cho 1 goi tin nhi phan hoan chinh (header + payload) */
#define CHUNK_PACKET_MAX_SIZE  (CHUNK_HEADER_SIZE + MAX_CHUNK_PAYLOAD)

/* ------------------------------------------------------------
 *  calc_total_chunks() — giu nguyen logic cu
 * ------------------------------------------------------------ */
static inline uint16_t calc_total_chunks(size_t jpeg_len) {
    return (uint16_t)((jpeg_len + MAX_CHUNK_PAYLOAD - 1) / MAX_CHUNK_PAYLOAD);
}

/* ------------------------------------------------------------
 *  build_chunk_packet()
 *
 *  Dong goi header + payload thanh 1 buffer nhi phan LIEN TUC,
 *  san sang truyen thang cho esp_mesh_send() qua mesh_data_t.data.
 *
 *  Tham so:
 *    header    — ChunkHeader da dien day du
 *    payload   — con tro toi du lieu JPEG cua chunk nay
 *    out_buf   — buffer dich (caller cap phat)
 *    out_buf_sz— kich thuoc buffer dich
 *
 *  Tra ve: so byte da ghi (= CHUNK_HEADER_SIZE + payload_len),
 *          0 neu buffer dich qua nho.
 * ------------------------------------------------------------ */
static inline size_t build_chunk_packet(const chunk_header_t *header,
                                         const uint8_t *payload,
                                         uint8_t *out_buf, size_t out_buf_sz) {
    if (header->payload_len > MAX_CHUNK_PAYLOAD) return 0;

    size_t total = CHUNK_HEADER_SIZE + header->payload_len;
    if (out_buf_sz < total) return 0;

    memcpy(out_buf, header, CHUNK_HEADER_SIZE);
    if (header->payload_len > 0 && payload != NULL) {
        memcpy(out_buf + CHUNK_HEADER_SIZE, payload, header->payload_len);
    }
    return total;
}

/* ------------------------------------------------------------
 *  parse_chunk_packet()
 *
 *  Giai goi buffer nhi phan nhan tu esp_mesh_recv() (mesh_data_t.data)
 *  thanh header + payload. KHONG con buoc Base64 decode.
 *
 *  Tham so:
 *    in_buf         — con tro data nhan tu mesh_data_t
 *    in_len         — mesh_data_t.size
 *    header_out     — [OUT] header giai ma duoc
 *    payload_buf    — [OUT] buffer nhan payload (caller cap phat, toi thieu MAX_CHUNK_PAYLOAD)
 *    payload_buf_sz — kich thuoc payload_buf
 *
 *  Tra ve: true neu hop le, false neu du lieu bi cat/corrupt/vuot gioi han.
 * ------------------------------------------------------------ */
static inline bool parse_chunk_packet(const uint8_t *in_buf, size_t in_len,
                                       chunk_header_t *header_out,
                                       uint8_t *payload_buf, size_t payload_buf_sz) {
    if (in_buf == NULL || header_out == NULL) return false;
    if (in_len < CHUNK_HEADER_SIZE) return false;

    memcpy(header_out, in_buf, CHUNK_HEADER_SIZE);

    if (header_out->payload_len > MAX_CHUNK_PAYLOAD || header_out->payload_len == 0) {
        return false;
    }
    if (header_out->chunk_index >= header_out->total_chunks) {
        return false;
    }
    if (header_out->total_chunks > MAX_CHUNKS_PER_FRAME) {
        return false;
    }

    size_t expected = CHUNK_HEADER_SIZE + header_out->payload_len;
    if (in_len < expected) return false;
    if (header_out->payload_len > payload_buf_sz) return false;

    if (payload_buf != NULL && header_out->payload_len > 0) {
        memcpy(payload_buf, in_buf + CHUNK_HEADER_SIZE, header_out->payload_len);
    }
    return true;
}

#endif // CHUNK_PROTOCOL_H
