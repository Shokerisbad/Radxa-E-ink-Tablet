#pragma once

#include <cstdint>
#include <string>
#include "lvgl/lvgl.h"

// I2C Device Configuration
#ifndef TOUCH_I2C_DEVICE
#define TOUCH_I2C_DEVICE "/dev/i2c-3" // Adjust if your SDA/SCL (pins 3/5) map to a different I2C bus
#endif

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

private:
    bool init_gpio();
    bool init_i2c();
    void reset_controller();

    bool write_reg(uint16_t reg, uint8_t data);
    bool read_reg(uint16_t reg, uint8_t *data, size_t len);

    int i2c_fd;
    uint8_t i2c_addr;

    // libgpiod resources
    struct gpiod_chip *gpio_chip;
    struct gpiod_line *line_rst;
    struct gpiod_line *line_int;

    int last_x;
    int last_y;
    bool is_pressed;
};
