#include "ui/ui.h"

#include <cstring>

#include "board/board.h"
#include "app/settings.h"
#include "media/audio.h"
#include "net/wifi_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "ui";

static lv_obj_t *s_scr_main = nullptr;
static lv_obj_t *s_scr_settings = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_video_area = nullptr;
static lv_obj_t *s_video_canvas = nullptr;
static void *s_canvas_buf = nullptr;
static int s_canvas_w = 0;
static int s_canvas_h = 0;

// Settings widgets.
static lv_obj_t *s_mode_dd = nullptr;
static lv_obj_t *s_ssid_ta = nullptr;
static lv_obj_t *s_pass_ta = nullptr;
static lv_obj_t *s_vol_slider = nullptr;
static lv_obj_t *s_gain_slider = nullptr;
static lv_obj_t *s_codec_dd = nullptr;
static lv_obj_t *s_keyboard = nullptr;

// Main-screen mic-mute button.
static lv_obj_t *s_mic_label = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 56, 56);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *make_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xB0B6BC), LV_PART_MAIN);
    return l;
}

// ---------------------------------------------------------------------------
// Settings event handlers
// ---------------------------------------------------------------------------

static void vol_changed_cb(lv_event_t *e)
{
    int v = (int) lv_slider_get_value(s_vol_slider);
    settings::set_volume((uint8_t) v);
    media::audio_set_volume((uint8_t) v);
}

static void gain_changed_cb(lv_event_t *e)
{
    int v = (int) lv_slider_get_value(s_gain_slider);
    settings::set_mic_gain((uint8_t) v);
    media::audio_set_mic_gain((uint8_t) v);
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *) lv_event_get_target(e);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void kb_done_cb(lv_event_t *e)
{
    lv_keyboard_set_textarea(s_keyboard, nullptr);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void back_event_cb(lv_event_t *e)
{
    (void) e;
    lv_screen_load(s_scr_main);
}

static void save_event_cb(lv_event_t *e)
{
    settings::Config c = settings::get();

    c.wifi_mode = (lv_dropdown_get_selected(s_mode_dd) == 1) ? settings::WifiMode::Create
                                                             : settings::WifiMode::Join;
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *pass = lv_textarea_get_text(s_pass_ta);
    if (c.wifi_mode == settings::WifiMode::Create) {
        strncpy(c.ap_ssid, ssid, sizeof(c.ap_ssid) - 1);
        c.ap_ssid[sizeof(c.ap_ssid) - 1] = '\0';
        strncpy(c.ap_password, pass, sizeof(c.ap_password) - 1);
        c.ap_password[sizeof(c.ap_password) - 1] = '\0';
    } else {
        strncpy(c.ssid, ssid, sizeof(c.ssid) - 1);
        c.ssid[sizeof(c.ssid) - 1] = '\0';
        strncpy(c.password, pass, sizeof(c.password) - 1);
        c.password[sizeof(c.password) - 1] = '\0';
    }
    c.codec = (lv_dropdown_get_selected(s_codec_dd) == 1) ? settings::VideoCodec::H264
                                                          : settings::VideoCodec::MJPEG;
    c.volume = (uint8_t) lv_slider_get_value(s_vol_slider);
    c.mic_gain = (uint8_t) lv_slider_get_value(s_gain_slider);
    settings::set(c);

    // Apply what can change live; WiFi is restarted in the new mode.
    media::audio_set_volume(c.volume);
    media::audio_set_mic_gain(c.mic_gain);
    net::wifi_apply();

    ESP_LOGI(TAG, "settings saved (mode=%s codec=%s)",
             c.wifi_mode == settings::WifiMode::Create ? "AP" : "STA",
             c.codec == settings::VideoCodec::H264 ? "H264" : "MJPEG");
    lv_screen_load(s_scr_main);
}

static void populate_settings()
{
    const settings::Config &c = settings::get();
    lv_dropdown_set_selected(s_mode_dd, c.wifi_mode == settings::WifiMode::Create ? 1 : 0);
    if (c.wifi_mode == settings::WifiMode::Create) {
        lv_textarea_set_text(s_ssid_ta, c.ap_ssid);
        lv_textarea_set_text(s_pass_ta, c.ap_password);
    } else {
        lv_textarea_set_text(s_ssid_ta, c.ssid);
        lv_textarea_set_text(s_pass_ta, c.password);
    }
    lv_dropdown_set_selected(s_codec_dd, c.codec == settings::VideoCodec::H264 ? 1 : 0);
    lv_slider_set_value(s_vol_slider, c.volume, LV_ANIM_OFF);
    lv_slider_set_value(s_gain_slider, c.mic_gain, LV_ANIM_OFF);
}

static void gear_event_cb(lv_event_t *e)
{
    (void) e;
    populate_settings();
    lv_screen_load(s_scr_settings);
}

static void mic_event_cb(lv_event_t *e)
{
    (void) e;
    bool muted = !settings::get().mic_muted;
    settings::set_mic_muted(muted);
    media::audio_set_mic_muted(muted);
    if (s_mic_label) {
        lv_label_set_text(s_mic_label, muted ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO);
    }
}

// ---------------------------------------------------------------------------
// Screen builders
// ---------------------------------------------------------------------------

static void build_main_screen()
{
    s_scr_main = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr_main, lv_color_hex(0x000000), LV_PART_MAIN);

    s_video_area = lv_obj_create(s_scr_main);
    lv_obj_set_size(s_video_area, lv_pct(100), lv_pct(100));
    lv_obj_center(s_video_area);
    lv_obj_set_style_bg_color(s_video_area, lv_color_hex(0x101418), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_video_area, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_video_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_video_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *waiting = lv_label_create(s_video_area);
    lv_label_set_text(waiting, "VideoLink\nwaiting for peer...");
    lv_obj_set_style_text_align(waiting, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(waiting, lv_color_hex(0x808890), LV_PART_MAIN);
    lv_obj_center(waiting);

    s_status_label = lv_label_create(s_scr_main);
    lv_label_set_text(s_status_label, "starting");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, 12, 16);

    lv_obj_t *gear = make_icon_button(s_scr_main, LV_SYMBOL_SETTINGS, gear_event_cb);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -12, 12);

    lv_obj_t *mic = make_icon_button(s_scr_main, LV_SYMBOL_AUDIO, mic_event_cb);
    lv_obj_align(mic, LV_ALIGN_BOTTOM_MID, 0, -16);
    s_mic_label = lv_obj_get_child(mic, 0);
}

static void build_settings_screen()
{
    s_scr_settings = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr_settings, lv_color_hex(0x16191d), LV_PART_MAIN);

    lv_obj_t *back = make_icon_button(s_scr_settings, LV_SYMBOL_LEFT, back_event_cb);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 12, 12);

    lv_obj_t *title = lv_label_create(s_scr_settings);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    // Scrollable content column.
    lv_obj_t *col = lv_obj_create(s_scr_settings);
    lv_obj_set_size(col, lv_pct(92), lv_pct(72));
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);

    make_section_label(col, "WiFi mode");
    s_mode_dd = lv_dropdown_create(col);
    lv_dropdown_set_options(s_mode_dd, "Join network (STA)\nCreate network (AP)");
    lv_obj_set_width(s_mode_dd, lv_pct(100));

    make_section_label(col, "Network name (SSID)");
    s_ssid_ta = lv_textarea_create(col);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_obj_set_width(s_ssid_ta, lv_pct(100));
    lv_obj_add_event_cb(s_ssid_ta, ta_focus_cb, LV_EVENT_FOCUSED, nullptr);

    make_section_label(col, "Password");
    s_pass_ta = lv_textarea_create(col);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_obj_set_width(s_pass_ta, lv_pct(100));
    lv_obj_add_event_cb(s_pass_ta, ta_focus_cb, LV_EVENT_FOCUSED, nullptr);

    make_section_label(col, "Speaker volume");
    s_vol_slider = lv_slider_create(col);
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_obj_set_width(s_vol_slider, lv_pct(100));
    lv_obj_add_event_cb(s_vol_slider, vol_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    make_section_label(col, "Microphone gain");
    s_gain_slider = lv_slider_create(col);
    lv_slider_set_range(s_gain_slider, 0, 100);
    lv_obj_set_width(s_gain_slider, lv_pct(100));
    lv_obj_add_event_cb(s_gain_slider, gain_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    make_section_label(col, "Video codec");
    s_codec_dd = lv_dropdown_create(col);
    lv_dropdown_set_options(s_codec_dd, "MJPEG\nH.264");
    lv_obj_set_width(s_codec_dd, lv_pct(100));

    lv_obj_t *save = lv_button_create(col);
    lv_obj_set_width(save, lv_pct(100));
    lv_obj_add_event_cb(save, save_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK "  Save & apply");
    lv_obj_center(save_lbl);

    // On-screen keyboard (hidden until a text field is focused).
    s_keyboard = lv_keyboard_create(s_scr_settings);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, kb_done_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_keyboard, kb_done_cb, LV_EVENT_CANCEL, nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace ui {

void init()
{
    board::lock(0);
    build_main_screen();
    build_settings_screen();
    lv_screen_load(s_scr_main);
    board::unlock();
    ESP_LOGI(TAG, "UI initialized");
}

void set_status(const char *text)
{
    if (s_status_label == nullptr) {
        return;
    }
    board::lock(0);
    lv_label_set_text(s_status_label, text);
    board::unlock();
}

void video_set_frame(const uint8_t *rgb565, int w, int h)
{
    if (s_video_area == nullptr || rgb565 == nullptr || w <= 0 || h <= 0) {
        return;
    }
    board::lock(0);
    if (s_video_canvas == nullptr || w != s_canvas_w || h != s_canvas_h) {
        if (s_video_canvas != nullptr) {
            lv_obj_del(s_video_canvas);
            s_video_canvas = nullptr;
        }
        if (s_canvas_buf != nullptr) {
            heap_caps_free(s_canvas_buf);
            s_canvas_buf = nullptr;
        }
        s_canvas_buf = heap_caps_malloc((size_t) w * h * 2, MALLOC_CAP_SPIRAM);
        if (s_canvas_buf == nullptr) {
            board::unlock();
            return;
        }
        s_video_canvas = lv_canvas_create(s_video_area);
        lv_canvas_set_buffer(s_video_canvas, s_canvas_buf, w, h, LV_COLOR_FORMAT_RGB565);
        lv_obj_center(s_video_canvas);
        s_canvas_w = w;
        s_canvas_h = h;
    }
    memcpy(s_canvas_buf, rgb565, (size_t) w * h * 2);
    lv_obj_invalidate(s_video_canvas);
    board::unlock();
}

} // namespace ui
