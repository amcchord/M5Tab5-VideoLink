# The PlatformIO "src" directory is the ESP-IDF "main" component.
# All application sources live here (recursively) and third-party libraries are
# pulled in as managed components (see src/idf_component.yml).

file(GLOB_RECURSE APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_timer
        esp_event
        esp_hw_support
        esp_netif
        lwip
        esp_wifi
        esp_wifi_remote
        esp_hosted
        mdns
        m5stack_tab5
        esp_codec_dev
        esp_video
        esp_h264
        esp_driver_jpeg
        esp_driver_ppa
        esp_lvgl_port
        lvgl
        driver
)