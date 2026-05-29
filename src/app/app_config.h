// Compile-time constants shared across modules.
#pragma once

// RTSP control port advertised over mDNS and listened on by the server.
// (We use 8554 rather than the privileged-by-convention 554; ESP-IDF has no
// privilege concept but 8554 is the common RTSP alt-port and avoids surprises.)
#define VIDEOLINK_RTSP_PORT       8554
#define VIDEOLINK_RTSP_PATH       "live"

// mDNS service for peer discovery. Standard Bonjour RTSP service type.
#define VIDEOLINK_SERVICE_TYPE    "_rtsp"
#define VIDEOLINK_SERVICE_PROTO   "_tcp"

// Default media parameters (see plan: 640x360 @ 15fps).
#define VIDEOLINK_VIDEO_WIDTH     640
#define VIDEOLINK_VIDEO_HEIGHT    360
#define VIDEOLINK_VIDEO_FPS       15

// RTP payload types (dynamic range) we use for our media.
#define VIDEOLINK_RTP_PT_JPEG     26   // RFC 2435 (static PT for JPEG)
#define VIDEOLINK_RTP_PT_H264     96   // dynamic
#define VIDEOLINK_RTP_PT_AUDIO    97   // dynamic (Opus / L16)
