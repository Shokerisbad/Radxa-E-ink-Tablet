#pragma once

#include <cstdint>
#include <string>
#include "lvgl/lvgl.h"
#include <chrono>
#include <sstream>
#include <fstream>
#include <memory>
#include <array>
#include <cstdio>

static inline int get_sysfs_gpio_number(const std::string& pin_name) {
    std::string cmd = "gpiofind " + pin_name + " 2>/dev/null";
    std::array<char, 128> buffer;
    std::string res;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return -1;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        res += buffer.data();
    }
    if (res.empty()) return -1;
    
    char chip_name[32];
    int offset;
    if (sscanf(res.c_str(), "%31s %d", chip_name, &offset) != 2) return -1;

    std::string base_path = std::string("/sys/class/gpio/") + chip_name + "/base";
    std::ifstream base_file(base_path);
    if (!base_file.is_open()) return -1;
    
    int base;
    base_file >> base;
    return base + offset;
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
