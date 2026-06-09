#pragma once

#include <cstdint>
#include <vector>

#include "lvgl/lvgl.h"

class RadxaEPD {
public:
    RadxaEPD();
    ~RadxaEPD();

    // Initializes the SPI and GPIOs, and runs the display init sequence
    bool init();

    // LVGL flush callback
    static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

    // Puts the display to deep sleep
    void sleep();

    // Wakes up / re-initializes the display
    void wake();

    // Full screen refresh
    void refresh_full(const uint8_t *buffer);

    // System Status Checkers
    static int get_battery_percentage();
    static bool is_wifi_connected();
    static bool is_wireguard_connected();

private:
    int spi_fd;
    struct gpiod_line *line_cs;
    struct gpiod_line *line_dc;
    struct gpiod_line *line_rst;
    struct gpiod_line *line_busy;

    // Hardware interactions
    bool init_gpio();
    bool init_spi();
    void hardware_reset();
    void init_display_sequence();

    void send_command(uint8_t cmd);
    void send_data(uint8_t data);
    void send_data_array(const uint8_t* data, size_t len);
    void wait_until_idle();
    // Partial screen refresh
    void refresh_partial(int x_start, int y_start, const uint8_t *buffer, int part_w, int part_h);

    bool first_refresh;
};

extern RadxaEPD *g_epd_instance;
