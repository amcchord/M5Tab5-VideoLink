#include "rtsp/rtp.h"

#include <cstring>
#include <cstdlib>

#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "rtp";

// Largest UDP payload we emit (keeps us under a typical 1500-byte MTU).
#define RTP_MAX_PACKET 1400
#define RTP_PT_JPEG    26

namespace {

// ---- Standard JPEG Huffman tables (JPEG Annex K / RFC 2435 Appendix) --------
const uint8_t lum_dc_codelens[] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t lum_dc_symbols[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t lum_ac_codelens[] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const uint8_t lum_ac_symbols[] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};
const uint8_t chm_dc_codelens[] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t chm_dc_symbols[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t chm_ac_codelens[] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const uint8_t chm_ac_symbols[] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};

uint8_t *put_dht(uint8_t *p, int class_id, int table_id, const uint8_t *codelens, int ncodes,
                 const uint8_t *symbols, int nsymbols)
{
    *p++ = 0xFF;
    *p++ = 0xC4; // DHT
    int len = 3 + ncodes + nsymbols;
    *p++ = (len >> 8) & 0xFF;
    *p++ = len & 0xFF;
    *p++ = (class_id << 4) | table_id;
    memcpy(p, codelens, ncodes);
    p += ncodes;
    memcpy(p, symbols, nsymbols);
    p += nsymbols;
    return p;
}

// Reconstruct a baseline JPEG header (everything up to and including SOS) into
// `out` from the RTP/JPEG parameters and quantization tables. Returns length.
size_t make_jpeg_header(uint8_t *out, int width, int height, int type, const uint8_t *qtables, int qlen)
{
    uint8_t *p = out;
    *p++ = 0xFF; *p++ = 0xD8; // SOI

    // DQT (one segment per 64-byte table present in qtables)
    int ntables = qlen / 64;
    if (ntables < 1) {
        ntables = 1;
    }
    for (int t = 0; t < ntables; t++) {
        *p++ = 0xFF; *p++ = 0xDB;
        *p++ = 0x00; *p++ = 0x43;       // length = 67
        *p++ = (uint8_t) t;             // precision 0, table id t
        memcpy(p, qtables + t * 64, 64);
        p += 64;
    }

    // SOF0
    *p++ = 0xFF; *p++ = 0xC0;
    *p++ = 0x00; *p++ = 0x11;           // length 17
    *p++ = 0x08;                        // precision
    *p++ = (height >> 8) & 0xFF; *p++ = height & 0xFF;
    *p++ = (width >> 8) & 0xFF;  *p++ = width & 0xFF;
    *p++ = 0x03;                        // 3 components
    *p++ = 0x01; *p++ = (type == 1) ? 0x22 : 0x21; *p++ = 0x00; // Y
    *p++ = 0x02; *p++ = 0x11; *p++ = 0x01;                      // Cb
    *p++ = 0x03; *p++ = 0x11; *p++ = 0x01;                      // Cr

    // DHT (4 standard tables)
    p = put_dht(p, 0, 0, lum_dc_codelens, 16, lum_dc_symbols, sizeof(lum_dc_symbols));
    p = put_dht(p, 1, 0, lum_ac_codelens, 16, lum_ac_symbols, sizeof(lum_ac_symbols));
    p = put_dht(p, 0, 1, chm_dc_codelens, 16, chm_dc_symbols, sizeof(chm_dc_symbols));
    p = put_dht(p, 1, 1, chm_ac_codelens, 16, chm_ac_symbols, sizeof(chm_ac_symbols));

    // SOS
    *p++ = 0xFF; *p++ = 0xDA;
    *p++ = 0x00; *p++ = 0x0C;           // length 12
    *p++ = 0x03;                        // 3 components
    *p++ = 0x01; *p++ = 0x00;           // Y  -> DC0/AC0
    *p++ = 0x02; *p++ = 0x11;           // Cb -> DC1/AC1
    *p++ = 0x03; *p++ = 0x11;           // Cr -> DC1/AC1
    *p++ = 0x00; *p++ = 0x3F; *p++ = 0x00; // Ss, Se, AhAl
    return (size_t) (p - out);
}

// Parse a baseline JPEG: extract quant tables, dimensions, sampling type and
// the scan-data span. Returns true on success.
bool parse_jpeg(const uint8_t *jpeg, size_t len, uint8_t *qtables, int *qlen, int *width,
                int *height, int *type, const uint8_t **scan, size_t *scan_len)
{
    const uint8_t *p = jpeg;
    const uint8_t *end = jpeg + len;
    *qlen = 0;
    *width = *height = 0;
    *type = 0;
    *scan = nullptr;
    *scan_len = 0;

    if (len < 4 || p[0] != 0xFF || p[1] != 0xD8) {
        return false;
    }
    p += 2;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) {
            p++;
            continue;
        }
        uint8_t marker = p[1];
        if (marker == 0xD8 || marker == 0xD9) {
            p += 2;
            continue;
        }
        if (marker >= 0xD0 && marker <= 0xD7) {
            p += 2;
            continue;
        }
        uint16_t seglen = (uint16_t) ((p[2] << 8) | p[3]);
        const uint8_t *seg = p + 4;
        const uint8_t *segend = p + 2 + seglen;
        if (segend > end) {
            break;
        }
        if (marker == 0xDB) { // DQT (possibly multiple tables)
            const uint8_t *q = seg;
            while (q + 65 <= segend) {
                uint8_t pqtq = *q++;
                uint8_t tq = pqtq & 0x0F;
                if (tq < 2) {
                    memcpy(qtables + tq * 64, q, 64);
                    if ((tq + 1) * 64 > *qlen) {
                        *qlen = (tq + 1) * 64;
                    }
                }
                q += 64;
            }
        } else if (marker == 0xC0 || marker == 0xC1) { // SOF0/SOF1
            *height = (seg[1] << 8) | seg[2];
            *width = (seg[3] << 8) | seg[4];
            uint8_t ncomp = seg[5];
            if (ncomp >= 1) {
                uint8_t samp = seg[7];
                *type = (samp == 0x22) ? 1 : 0;
            }
        } else if (marker == 0xDA) { // SOS -> scan data follows
            const uint8_t *s = segend;
            const uint8_t *e = s;
            while (e + 1 < end) {
                if (e[0] == 0xFF && e[1] == 0xD9) {
                    break;
                }
                e++;
            }
            *scan = s;
            *scan_len = (size_t) (e - s);
            return (*qlen > 0 && *width > 0 && *scan_len > 0);
        }
        p = segend;
    }
    return false;
}

void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

} // namespace

namespace rtsp {

int rtp_send_jpeg(int sock, const sockaddr_in *dest, const uint8_t *jpeg, size_t jpeg_len,
                  uint16_t *seq, uint32_t ssrc, uint32_t ts)
{
    uint8_t qtables[128];
    int qlen = 0, width = 0, height = 0, type = 0;
    const uint8_t *scan = nullptr;
    size_t scan_len = 0;
    if (!parse_jpeg(jpeg, jpeg_len, qtables, &qlen, &width, &height, &type, &scan, &scan_len)) {
        return -1;
    }
    if (width > 2040 || height > 2040) {
        return -1; // RFC 2435 encodes dimensions as width/8 in one byte
    }

    uint8_t pkt[RTP_MAX_PACKET + 64];
    uint32_t offset = 0;
    size_t remaining = scan_len;
    const uint8_t *sd = scan;

    while (true) {
        bool first = (offset == 0);
        size_t hdr = 12 + 8 + (first ? (4 + (size_t) qlen) : 0);
        size_t maxpay = RTP_MAX_PACKET - hdr;
        size_t chunk = remaining > maxpay ? maxpay : remaining;
        bool last = (chunk == remaining);

        uint8_t *p = pkt;
        // RTP header
        p[0] = 0x80;
        p[1] = (uint8_t) (RTP_PT_JPEG | (last ? 0x80 : 0x00));
        p[2] = (*seq >> 8) & 0xFF;
        p[3] = *seq & 0xFF;
        wr32(p + 4, ts);
        wr32(p + 8, ssrc);
        p += 12;
        // JPEG header
        p[0] = 0;                       // type-specific
        p[1] = (offset >> 16) & 0xFF;
        p[2] = (offset >> 8) & 0xFF;
        p[3] = offset & 0xFF;
        p[4] = (uint8_t) type;
        p[5] = 255;                     // Q dynamic -> tables in-band
        p[6] = (uint8_t) (width / 8);
        p[7] = (uint8_t) (height / 8);
        p += 8;
        if (first) {
            p[0] = 0;                   // MBZ
            p[1] = 0;                   // precision (8-bit)
            p[2] = (qlen >> 8) & 0xFF;
            p[3] = qlen & 0xFF;
            p += 4;
            memcpy(p, qtables, qlen);
            p += qlen;
        }
        memcpy(p, sd, chunk);
        p += chunk;

        int sent = sendto(sock, pkt, (size_t) (p - pkt), 0, (const struct sockaddr *) dest,
                          sizeof(*dest));
        (*seq)++;
        if (sent < 0) {
            return -1;
        }
        offset += chunk;
        sd += chunk;
        remaining -= chunk;
        if (last) {
            break;
        }
    }
    return 0;
}

void RtpJpegReassembler::init(frame_cb_t cb, void *user)
{
    cb_ = cb;
    user_ = user;
    frame_cap_ = 256 * 1024;
    frame_ = (uint8_t *) heap_caps_malloc(frame_cap_, MALLOC_CAP_SPIRAM);
    reset_frame();
}

void RtpJpegReassembler::reset_frame()
{
    header_len_ = 0;
    scan_len_ = 0;
    have_header_ = false;
}

void RtpJpegReassembler::feed(const uint8_t *pkt, size_t len)
{
    if (frame_ == nullptr || len < 12 + 8) {
        return;
    }
    bool marker = (pkt[1] & 0x80) != 0;
    uint32_t ts = rd32(pkt + 4);

    const uint8_t *jh = pkt + 12;
    uint32_t offset = ((uint32_t) jh[1] << 16) | ((uint32_t) jh[2] << 8) | jh[3];
    int type = jh[4];
    int q = jh[5];
    int width = jh[6] * 8;
    int height = jh[7] * 8;
    const uint8_t *payload = jh + 8;
    size_t paylen = len - 12 - 8;

    if (offset == 0) {
        uint8_t qtables[128];
        int qlen = 0;
        if (q >= 128) {
            if (paylen < 4) {
                return;
            }
            int tlen = (payload[2] << 8) | payload[3];
            payload += 4;
            paylen -= 4;
            if (tlen > 128 || (size_t) tlen > paylen) {
                return;
            }
            memcpy(qtables, payload, tlen);
            qlen = tlen;
            payload += tlen;
            paylen -= tlen;
        }
        header_len_ = make_jpeg_header(frame_, width, height, type, qtables, qlen);
        scan_len_ = 0;
        have_header_ = true;
        cur_ts_ = ts;
    }

    if (!have_header_ || ts != cur_ts_) {
        return; // mid-frame join or a different frame; wait for the next start
    }

    size_t pos = header_len_ + offset;
    if (pos + paylen + 2 > frame_cap_) {
        return; // overflow guard
    }
    memcpy(frame_ + pos, payload, paylen);
    size_t end = offset + paylen;
    if (end > scan_len_) {
        scan_len_ = end;
    }

    if (marker) {
        uint8_t *p = frame_ + header_len_ + scan_len_;
        p[0] = 0xFF;
        p[1] = 0xD9; // EOI
        size_t total = header_len_ + scan_len_ + 2;
        if (cb_) {
            cb_(frame_, total, user_);
        }
        reset_frame();
    }
}

} // namespace rtsp
