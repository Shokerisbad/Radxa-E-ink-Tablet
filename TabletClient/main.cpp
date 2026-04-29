#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "RadxaEPD.h"
#include "src/App.h"

// Define display resolution
#define DISP_HOR_RES 800
#define DISP_VER_RES 480

// Draw buffer size (1/10 screen size is a good default for LVGL, but for EPD we often use full screen buffer or partial)
// Since EPD requires full frame for SPI transfer, we'll allocate a full screen buffer.
#define DISP_BUF_SIZE (DISP_HOR_RES * DISP_VER_RES)

volatile bool g_running = true;

void signal_handler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    g_running = false;
}

// Custom tick interface for LVGL on Linux
static uint32_t custom_tick_get(void) {
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        auto now = std::chrono::steady_clock::now();
        start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }
    auto now = std::chrono::steady_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return (uint32_t)(now_ms - start_ms);
}

// System Status Bar - Example UI Setup
void create_status_bar() {
    lv_obj_t * status_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(status_bar, DISP_HOR_RES, 30);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_border_side(status_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(status_bar, 2, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Wi-Fi Icon
    lv_obj_t * wifi_label = lv_label_create(status_bar);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 10, 0);

    // VPN Icon
    lv_obj_t * vpn_label = lv_label_create(status_bar);
    lv_label_set_text(vpn_label, ""); // Will be populated with LV_SYMBOL_SHIELD if connected
    lv_obj_align_to(vpn_label, wifi_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    // Battery Icon
    lv_obj_t * batt_label = lv_label_create(status_bar);
    lv_label_set_text(batt_label, LV_SYMBOL_BATTERY_FULL " 100%");
    lv_obj_align(batt_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // Time Label
    lv_obj_t * time_label = lv_label_create(status_bar);
    lv_label_set_text(time_label, "12:00");
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

    // Create a timer to update these statuses
    lv_timer_create([](lv_timer_t * timer) {
        lv_obj_t ** labels = (lv_obj_t **)lv_timer_get_user_data(timer);
        lv_obj_t * w_lbl = labels[0];
        lv_obj_t * v_lbl = labels[1];
        lv_obj_t * b_lbl = labels[2];
        lv_obj_t * t_lbl = labels[3];

        // Update Wi-Fi
        if (RadxaEPD::is_wifi_connected()) {
            lv_label_set_text(w_lbl, LV_SYMBOL_WIFI);
        } else {
            lv_label_set_text(w_lbl, "No WiFi");
        }

        // Update VPN
        if (RadxaEPD::is_wireguard_connected()) {
            lv_label_set_text(v_lbl, LV_SYMBOL_DUMMY " VPN"); // LV_SYMBOL_DUMMY or custom icon
        } else {
            lv_label_set_text(v_lbl, "");
        }

        // Update Battery
        int batt = RadxaEPD::get_battery_percentage();
        if (batt >= 0) {
            lv_label_set_text_fmt(b_lbl, LV_SYMBOL_BATTERY_FULL " %d%%", batt);
        }

        // Update Time
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        lv_label_set_text_fmt(t_lbl, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }, 60000, new lv_obj_t*[4]{wifi_label, vpn_label, batt_label, time_label}); // Update every minute
}

int main(void) {
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "Starting Radxa LVGL Tablet Client...\n";

    // 1. Initialize LVGL
    lv_init();
    lv_tick_set_cb(custom_tick_get);

    // 2. Initialize Hardware Display Driver
    RadxaEPD epd;
    if (!epd.init()) {
        std::cerr << "Failed to initialize Radxa EPD. Exiting.\n";
        return -1;
    }

    // 3. Register Display Driver in LVGL (v9 API)
    // We allocate a full screen buffer. LVGL will render into this and pass it to flush_cb.
    static uint8_t * buf1 = (uint8_t *)malloc(DISP_BUF_SIZE * sizeof(lv_color32_t));
    
    lv_display_t * disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_flush_cb(disp, RadxaEPD::flush_cb);
    lv_display_set_buffers(disp, buf1, NULL, DISP_BUF_SIZE * sizeof(lv_color32_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Rotate the UI 90° so portrait layout maps to the landscape panel
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    create_status_bar();

    // Delegate UI creation to the cross-platform App
    build_tablet_ui();

    // 5. Main LVGL Loop
    std::cout << "Entering LVGL Main Loop...\n";
    while (g_running) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Sleep to save CPU
    }

    std::cout << "Putting display to sleep and exiting...\n";
    epd.sleep();

    // Cleanup
    free(buf1);
    return 0;
}
