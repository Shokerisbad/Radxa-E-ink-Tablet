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

RadxaTouch::RadxaTouch() : i2c_fd(-1), i2c_addr(GT911_I2C_ADDR_28), line_rst(nullptr), line_int(nullptr), last_x(0), last_y(0), is_pressed(false) {
    g_touch_instance = this;
}

RadxaTouch::~RadxaTouch() {
    if (i2c_fd >= 0) close(i2c_fd);
    if (line_rst) gpiod_line_release(line_rst);
    if (line_int) gpiod_line_release(line_int);
}

bool RadxaTouch::init_gpio() {
    line_rst = gpiod_line_find(TOUCH_PIN_RST);
    line_int = gpiod_line_find(TOUCH_PIN_INT);

    if (!line_rst) std::cerr << "Failed to find Touch RST pin: " << TOUCH_PIN_RST << std::endl;
    if (!line_int) std::cerr << "Failed to find Touch INT pin: " << TOUCH_PIN_INT << std::endl;

    if (!line_rst || !line_int) return false;

    gpiod_line_request_output(line_rst, "touch_rst", 1); // Start HIGH (not resetting)
    // INT starts as input — we only read it, never drive it
    gpiod_line_request_input(line_int, "touch_int");

    return true;
}

void RadxaTouch::reset_controller() {
    // Simple reset matching the working Python script:
    // Just toggle RST low then high. Do NOT touch the INT pin.
    // The GT911 will default to address 0x14.
    gpiod_line_set_value(line_rst, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    gpiod_line_set_value(line_rst, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

bool RadxaTouch::init_i2c() {
    // Scan all available I2C buses to find the GT911 (Blinka auto-detects in Python)
    const char* i2c_paths[] = {
        "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", "/dev/i2c-3",
        "/dev/i2c-4", "/dev/i2c-5", "/dev/i2c-6", "/dev/i2c-7",
        nullptr
    };

    for (int i = 0; i2c_paths[i] != nullptr; i++) {
        int fd = open(i2c_paths[i], O_RDWR);
        if (fd < 0) continue;

        // Try to communicate with the GT911 at 0x14
        uint8_t reg_buf[2] = {0x81, 0x40}; // Product ID register
        uint8_t pid[4] = {0};

        struct i2c_msg msgs[2];
        msgs[0].addr = GT911_I2C_ADDR_28;
        msgs[0].flags = 0;
        msgs[0].len = 2;
        msgs[0].buf = reg_buf;
        msgs[1].addr = GT911_I2C_ADDR_28;
        msgs[1].flags = I2C_M_RD;
        msgs[1].len = 4;
        msgs[1].buf = pid;

        struct i2c_rdwr_ioctl_data ioctl_data;
        ioctl_data.msgs = msgs;
        ioctl_data.nmsgs = 2;

        if (ioctl(fd, I2C_RDWR, &ioctl_data) >= 0) {
            std::cout << "GT911 found at address 0x14 on " << i2c_paths[i] << std::endl;
            i2c_fd = fd;
            i2c_addr = GT911_I2C_ADDR_28;
            return true;
        }

        // Try 0x5D
        msgs[0].addr = GT911_I2C_ADDR_BA;
        msgs[1].addr = GT911_I2C_ADDR_BA;

        if (ioctl(fd, I2C_RDWR, &ioctl_data) >= 0) {
            std::cout << "GT911 found at address 0x5D on " << i2c_paths[i] << std::endl;
            i2c_fd = fd;
            i2c_addr = GT911_I2C_ADDR_BA;
            return true;
        }

        close(fd);
    }

    std::cerr << "GT911 not found on any I2C bus!" << std::endl;
    return false;
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
        std::cout << "GT911 Product ID: "
                  << (char)pid[0] << (char)pid[1] << (char)pid[2] << (char)pid[3]
                  << std::endl;
    } else {
        std::cerr << "Failed to read GT911 Product ID." << std::endl;
        return false;
    }

    std::cout << "GT911 Touch initialized successfully!" << std::endl;
    return true;
}

void RadxaTouch::read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    if (!g_touch_instance) return;

    uint8_t status = 0;
    g_touch_instance->read_reg(0x814E, &status, 1);

    if (status & 0x80) { // Buffer status bit (1 = data ready)
        int touch_count = status & 0x0F;
        if (touch_count > 0 && touch_count <= 5) {
            // Read touch point data — 8 bytes per point
            // GT911 format per point at 0x8150:
            //   Byte 0: Track ID
            //   Byte 1: X Low
            //   Byte 2: X High
            //   Byte 3: Y Low
            //   Byte 4: Y High
            //   Byte 5-7: Size + reserved
            uint8_t point_data[8] = {0}; // Initialize to zero to prevent stack garbage
            if (g_touch_instance->read_reg(0x8150, point_data, 8)) {
                // Offset by 1 to skip Track ID (matching Python: data[i*8+1])
                int phys_x = point_data[1] | (point_data[2] << 8);
                int phys_y = point_data[3] | (point_data[4] << 8);

                // --- Coordinate Mapping ---
                // Physical touch panel: 800x480 (Landscape)
                // LVGL Logical screen: 480x800 (Portrait)
                // EPD flush_cb rotates: logical(lx,ly) -> physical(799-ly, lx)
                // Reverse for touch: log_x = phys_y, log_y = 799 - phys_x
                g_touch_instance->last_x = phys_y;
                g_touch_instance->last_y = 799 - phys_x;
                g_touch_instance->is_pressed = true;
            }
        } else {
            g_touch_instance->is_pressed = false;
        }

        // CRITICAL: Clear the status buffer so GT911 registers the next touch
        g_touch_instance->write_reg(0x814E, 0x00);
    } else {
        // No touch event pending
        g_touch_instance->is_pressed = false;
    }

    // Report state to LVGL
    data->point.x = g_touch_instance->last_x;
    data->point.y = g_touch_instance->last_y;
    data->state = g_touch_instance->is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
