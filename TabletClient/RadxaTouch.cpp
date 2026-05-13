#include "RadxaTouch.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <gpiod.h>
#include <thread>
#include <chrono>
#include <cstring>

RadxaTouch *g_touch_instance = nullptr;

RadxaTouch::RadxaTouch() : i2c_fd(-1), i2c_addr(GT911_I2C_ADDR_BA), gpio_chip(nullptr), line_rst(nullptr), line_int(nullptr), last_x(0), last_y(0), is_pressed(false) {
    g_touch_instance = this;
}

RadxaTouch::~RadxaTouch() {
    if (i2c_fd >= 0) close(i2c_fd);
    if (line_rst) gpiod_line_release(line_rst);
    if (line_int) gpiod_line_release(line_int);
    if (gpio_chip) gpiod_chip_close(gpio_chip);
}

bool RadxaTouch::init_gpio() {
    line_rst = gpiod_line_find(TOUCH_PIN_RST);
    line_int = gpiod_line_find(TOUCH_PIN_INT);

    if (!line_rst) std::cerr << "Failed to find Touch RST pin: " << TOUCH_PIN_RST << std::endl;
    if (!line_int) std::cerr << "Failed to find Touch INT pin: " << TOUCH_PIN_INT << std::endl;

    if (!line_rst || !line_int) return false;

    gpiod_line_request_output(line_rst, "touch_rst", 0);
    gpiod_line_request_output(line_int, "touch_int", 0);

    return true;
}

void RadxaTouch::reset_controller() {
    // GT911 Address Selection sequence
    // Hold RST low, pull INT high for 0x5D, then pull RST high.
    gpiod_line_set_value(line_rst, 0);
    gpiod_line_set_value(line_int, 1); 
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    gpiod_line_set_value(line_rst, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Release INT pin and set to input so we can read it (or poll it)
    gpiod_line_release(line_int);
    gpiod_line_request_input(line_int, "touch_int");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

bool RadxaTouch::init_i2c() {
    i2c_fd = open(TOUCH_I2C_DEVICE, O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "Failed to open I2C device " << TOUCH_I2C_DEVICE << std::endl;
        return false;
    }
    return true;
}

bool RadxaTouch::write_reg(uint16_t reg, uint8_t data) {
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), data };
    
    struct i2c_msg msg;
    msg.addr = i2c_addr;
    msg.flags = 0;
    msg.len = 3;
    msg.buf = buf;

    struct i2c_rdwr_ioctl_data ioctl_data;
    ioctl_data.msgs = &msg;
    ioctl_data.nmsgs = 1;

    return ioctl(i2c_fd, I2C_RDWR, &ioctl_data) >= 0;
}

bool RadxaTouch::read_reg(uint16_t reg, uint8_t *data, size_t len) {
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };

    struct i2c_msg msgs[2];
    msgs[0].addr = i2c_addr;
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = reg_buf;

    msgs[1].addr = i2c_addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = len;
    msgs[1].buf = data;

    struct i2c_rdwr_ioctl_data ioctl_data;
    ioctl_data.msgs = msgs;
    ioctl_data.nmsgs = 2;

    return ioctl(i2c_fd, I2C_RDWR, &ioctl_data) >= 0;
}

bool RadxaTouch::init() {
    std::cout << "Initializing GT911 Touch..." << std::endl;
    if (!init_gpio()) return false;
    reset_controller();
    if (!init_i2c()) return false;

    // Verify communication by reading product ID (Reg 0x8140 - 0x8143)
    uint8_t pid[4] = {0};
    if (read_reg(0x8140, pid, 4)) {
        std::cout << "GT911 Product ID: " << pid[0] << pid[1] << pid[2] << pid[3] << std::endl;
    } else {
        std::cerr << "Failed to read GT911 Product ID. Check I2C connection." << std::endl;
        return false;
    }

    return true;
}

void RadxaTouch::read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    if (!g_touch_instance) return;

    // Check INT pin if a touch is pending
    // GT911 pulls INT low when data is ready
    if (gpiod_line_get_value(g_touch_instance->line_int) == 0) {
        uint8_t status = 0;
        g_touch_instance->read_reg(0x814E, &status, 1);

        if (status & 0x80) { // Buffer status bit (1 = data ready)
            int touch_count = status & 0x0F;
            if (touch_count > 0) {
                uint8_t point_data[4];
                // Read Point 1 data (Reg 0x8150: X_L, X_H, Y_L, Y_H)
                if (g_touch_instance->read_reg(0x8150, point_data, 4)) {
                    int phys_x = point_data[0] | (point_data[1] << 8);
                    int phys_y = point_data[2] | (point_data[3] << 8);

                    // --- Coordinate Mapping ---
                    // Physical touch panel: 800x480 (Landscape)
                    // LVGL Logical screen: 480x800 (Portrait)
                    // The EPD driver rotates pixels: phys(799-Y, X) = log(X, Y)
                    // So we must reverse this for touch:
                    // log_x = phys_y
                    // log_y = 799 - phys_x
                    g_touch_instance->last_x = phys_y;
                    g_touch_instance->last_y = 799 - phys_x;
                    g_touch_instance->is_pressed = true;
                }
            } else {
                g_touch_instance->is_pressed = false;
            }

            // Important: Clear the buffer status bit so GT911 can send the next interrupt
            g_touch_instance->write_reg(0x814E, 0x00);
        }
    }

    // Report state to LVGL
    data->point.x = g_touch_instance->last_x;
    data->point.y = g_touch_instance->last_y;
    data->state = g_touch_instance->is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
