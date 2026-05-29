// RTP (RFC 3550) transport with an RTP/JPEG (RFC 2435) payloader and
// de-payloader. The sender parses a baseline JPEG (as produced by the P4 HW
// encoder), strips it to scan data + quant tables, and emits RTP/JPEG packets.
// The receiver reassembles packets and reconstructs a complete baseline JPEG
// (using standard Huffman tables) that the HW decoder can consume.
#pragma once

#include <cstdint>
#include <cstddef>

struct sockaddr_in;

namespace rtsp {

// 90 kHz RTP clock for video.
static const uint32_t RTP_CLOCK_HZ = 90000;

// Send one JPEG frame as a sequence of RTP/JPEG packets to `dest`.
// `seq` is advanced; `ts` is the 90kHz timestamp for this frame.
// Returns 0 on success, -1 on error.
int rtp_send_jpeg(int sock, const sockaddr_in *dest, const uint8_t *jpeg, size_t jpeg_len,
                  uint16_t *seq, uint32_t ssrc, uint32_t ts);

// Reassembles RTP/JPEG packets into whole JPEG frames.
class RtpJpegReassembler {
public:
    typedef void (*frame_cb_t)(const uint8_t *jpeg, size_t len, void *user);

    void init(frame_cb_t cb, void *user);
    // Feed one received UDP datagram (a full RTP packet).
    void feed(const uint8_t *pkt, size_t len);

private:
    void reset_frame();

    frame_cb_t cb_ = nullptr;
    void *user_ = nullptr;
    uint8_t *frame_ = nullptr;   // working buffer (headers + scan + EOI)
    size_t frame_cap_ = 0;
    size_t header_len_ = 0;      // bytes of reconstructed JPEG header
    size_t scan_len_ = 0;        // bytes of scan data accumulated
    uint32_t cur_ts_ = 0;
    bool have_header_ = false;
};

} // namespace rtsp
