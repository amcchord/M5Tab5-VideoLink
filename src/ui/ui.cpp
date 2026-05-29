#include "ui/ui.h"

#include "board/board.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_scr_main = nullptr;
static lv_obj_t *s_scr_settings = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_video_area = nullptr;

static void gear_event_cb(lv_event_t *e)
{
    (void) e;
    lv_screen_load(s_scr_settings);
}

static void back_event_cb(lv_event_t *e)
{
    (void) e;
    lv_screen_load(s_scr_main);
}

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

static void build_main_screen()
{
    s_scr_main = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr_main, lv_color_hex(0x000000), LV_PART_MAIN);

    // Full-screen area where the decoded remote video will be drawn.
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
}

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

} // namespace ui
