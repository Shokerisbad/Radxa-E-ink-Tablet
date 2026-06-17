#pragma once

#include <cstdint>
#include <chrono>
#include <sstream>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <gpiod.h>
#include <dirent.h>
#endif

#include "lvgl/lvgl.h"
#include <iostream>


// GPIO Configuration (Physical Pin Numbers)
#define TOUCH_PIN_RST "PIN_37"
#define TOUCH_PIN_INT "PIN_35"

// GT911 Addresses
#define GT911_I2C_ADDR_28 0x14 // Address if INT is LOW during reset
#define GT911_I2C_ADDR_BA 0x5D // Address if INT is HIGH during reset

class RadxaTouch {
public:
    RadxaTouch();
    ~RadxaTouch();

    bool init();

    // The callback LVGL uses to poll for touch data
    static void read_cb(lv_indev_t * indev, lv_indev_data_t * data);

    void ignore_touches_for(int ms);

    void prepare_for_sleep();
    void resume_from_sleep();
    
    bool is_hardware_touched();
    void clear_touch_buffer();

private:
    bool init_gpio();
    bool init_i2c();
    void reset_controller();

    bool write_reg(uint16_t reg, uint8_t data);
    bool read_reg(uint16_t reg, uint8_t *data, size_t len);

    int i2c_fd;
    uint8_t i2c_addr;

#ifndef _WIN32
    struct gpiod_line *line_rst;
    struct gpiod_line *line_int;
#endif

    int last_x;
    int last_y;
    bool is_pressed;
    std::chrono::steady_clock::time_point ignore_until;
};

extern RadxaTouch *g_touch_instance;
