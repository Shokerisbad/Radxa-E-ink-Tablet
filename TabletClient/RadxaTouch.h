#pragma once

#include <cstdint>
#include <chrono>
#include <sstream>
#include <fstream>
#include <string>
#include <gpiod.h>
#include "lvgl/lvgl.h"
#include <iostream>

static inline int get_sysfs_gpio_number(const std::string& pin_name) {
    struct gpiod_line *line = gpiod_line_find(pin_name.c_str());
    if (!line) {
        std::cerr << "gpiod_line_find failed for: " << pin_name << std::endl;
        return -1;
    }
    
    unsigned int offset = gpiod_line_offset(line);
    struct gpiod_chip *chip = gpiod_line_get_chip(line);
    if (!chip) {
        std::cerr << "gpiod_line_get_chip failed for: " << pin_name << std::endl;
        return -1;
    }
    
    const char* chip_name = gpiod_chip_name(chip);
    if (!chip_name) {
        std::cerr << "gpiod_chip_name failed for: " << pin_name << std::endl;
        gpiod_chip_close(chip);
        return -1;
    }
    
    std::string base_path = std::string("/sys/class/gpio/") + chip_name + "/base";
    std::ifstream base_file(base_path);
    int base = -1;
    if (base_file.is_open()) {
        base_file >> base;
    } else {
        std::cerr << "Failed to open sysfs base file: " << base_path << std::endl;
    }
    
    // Crucial: close the chip to release the libgpiod lock on sysfs!
    gpiod_chip_close(chip);
    
    if (base != -1) {
        return base + offset;
    }
    return -1;
}

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

private:
    bool init_gpio();
    bool init_i2c();
    void reset_controller();

    bool write_reg(uint16_t reg, uint8_t data);
    bool read_reg(uint16_t reg, uint8_t *data, size_t len);

    int i2c_fd;
    uint8_t i2c_addr;

    int last_x;
    int last_y;
    bool is_pressed;
    std::chrono::steady_clock::time_point ignore_until;
};

extern RadxaTouch *g_touch_instance;
