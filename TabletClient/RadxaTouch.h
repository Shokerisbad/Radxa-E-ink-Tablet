#pragma once

#include <cstdint>
#include <chrono>
#include <sstream>
#include <fstream>
#include <string>
#include <gpiod.h>
#include "lvgl/lvgl.h"
#include <iostream>
#include <dirent.h>

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
    
    const char* target_label_cstr = gpiod_chip_label(chip);
    if (!target_label_cstr) {
        std::cerr << "gpiod_chip_label failed for: " << pin_name << std::endl;
        gpiod_chip_close(chip);
        return -1;
    }
    std::string target_label = target_label_cstr;
    
    // Crucial: close the chip to release the libgpiod lock on sysfs!
    gpiod_chip_close(chip);
    
    // Now find the matching gpiochip in sysfs by label
    int base = -1;
    DIR *dir = opendir("/sys/class/gpio");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("gpiochip") == 0) {
                std::string label_path = "/sys/class/gpio/" + name + "/label";
                std::ifstream label_file(label_path);
                if (label_file.is_open()) {
                    std::string label;
                    std::getline(label_file, label);
                    // Sysfs label might have a trailing newline, so we check using find
                    if (label.find(target_label) != std::string::npos || target_label.find(label) != std::string::npos) {
                        std::string base_path = "/sys/class/gpio/" + name + "/base";
                        std::ifstream base_file(base_path);
                        if (base_file.is_open()) {
                            base_file >> base;
                            break;
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
    
    if (base != -1) {
        return base + offset;
    }
    
    std::cerr << "Failed to find sysfs base for label: " << target_label << std::endl;
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
