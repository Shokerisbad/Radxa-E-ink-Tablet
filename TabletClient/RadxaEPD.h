#pragma once

#include <cstdint>
#include <vector>

// Forward declaration for lv_disp_drv_t
struct _lv_disp_drv_t;
struct _lv_color_t;

class RadxaEPD {
public:
    RadxaEPD();
    ~RadxaEPD();

    // Initializes the SPI and GPIOs, and runs the display init sequence
    bool init();

    // LVGL flush callback
    static void flush_cb(struct _lv_disp_drv_t * disp_drv, const struct _lv_area_t * area, struct _lv_color_t * color_p);

    // Puts the display to deep sleep
    void sleep();

    // Wakes up / re-initializes the display
    void wake();

    // System Status Checkers
    static int get_battery_percentage();
    static bool is_wifi_connected();
    static bool is_wireguard_connected();

private:
    int spi_fd;
    struct gpiod_chip *gpio_chip;
    struct gpiod_line *line_cs;
    struct gpiod_line *line_dc;
    struct gpiod_line *line_rst;
    struct gpiod_line *line_busy;

    // Hardware interactions
    bool init_gpio();
    bool init_spi();
    void hardware_reset();

    void send_command(uint8_t cmd);
    void send_data(uint8_t data);
    void send_data_array(const uint8_t* data, size_t len);
    void wait_until_idle();

    // Full screen refresh
    void refresh_full();
    // Partial screen refresh
    void refresh_partial(int x, int y, int w, int h);
};
