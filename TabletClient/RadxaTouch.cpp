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

RadxaTouch::RadxaTouch() : i2c_fd(-1), i2c_addr(GT911_I2C_ADDR_28), last_x(0), last_y(0), is_pressed(false) {
#ifndef _WIN32
    line_rst = nullptr;
    line_int = nullptr;
#endif
    ignore_until = std::chrono::steady_clock::now();
    g_touch_instance = this;
}

RadxaTouch::~RadxaTouch() {
    if (i2c_fd >= 0) close(i2c_fd);
#ifndef _WIN32
    if (line_rst) gpiod_line_release(line_rst);
    if (line_int) gpiod_line_release(line_int);
#endif
}

bool RadxaTouch::init_gpio() {
#ifndef _WIN32
    line_rst = gpiod_line_find(TOUCH_PIN_RST);
    line_int = gpiod_line_find(TOUCH_PIN_INT);

    if (!line_rst) std::cerr << "Failed to find Touch RST pin: " << TOUCH_PIN_RST << std::endl;
    if (!line_int) std::cerr << "Failed to find Touch INT pin: " << TOUCH_PIN_INT << std::endl;

    if (!line_rst || !line_int) return false;

    // Setup RST as output, HIGH (not resetting)
    gpiod_line_request_output(line_rst, "touch_rst", 1);

    // INT starts as input — we only read it, never drive it initially
    gpiod_line_request_input(line_int, "touch_int");
#endif
    return true;
}

void RadxaTouch::reset_controller() {
#ifndef _WIN32
    if (!line_rst || !line_int) return;

    // To select address 0x5D (0xBA) per GT911 spec, the INT pin must be held LOW during the reset sequence.
    gpiod_line_release(line_int);
    gpiod_line_request_output(line_int, "touch_int_out", 0);

    gpiod_line_set_value(line_rst, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    gpiod_line_set_value(line_rst, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Release INT line back to input mode for GT911 interrupt monitoring
    gpiod_line_release(line_int);
    gpiod_line_request_input(line_int, "touch_int_in");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
}

bool RadxaTouch::init_i2c() {
    // User specifically requested to only use /dev/i2c-3 at address 0x5D
    const char* i2c_path = "/dev/i2c-3";
    int fd = open(i2c_path, O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open I2C bus: " << i2c_path << std::endl;
        return false;
    }

    // Attempt to read Product ID from 0x5D to verify
    uint8_t reg_buf[2] = {0x81, 0x40}; // Product ID register
    uint8_t pid[4] = {0};

    struct i2c_msg msgs[2];
    msgs[0].addr = GT911_I2C_ADDR_BA; // 0x5D
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = reg_buf;
    msgs[1].addr = GT911_I2C_ADDR_BA;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 4;
    msgs[1].buf = pid;

    struct i2c_rdwr_ioctl_data ioctl_data;
    ioctl_data.msgs = msgs;
    ioctl_data.nmsgs = 2;

    if (ioctl(fd, I2C_RDWR, &ioctl_data) >= 0) {
        std::cout << "GT911 successfully connected at address 0x5D on " << i2c_path << std::endl;
        i2c_fd = fd;
        i2c_addr = GT911_I2C_ADDR_BA;
        return true;
    }

    close(fd);
    std::cerr << "GT911 not responding at address 0x5D on " << i2c_path << std::endl;
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
static auto last_press_time = std::chrono::steady_clock::now();

void RadxaTouch::read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    if (!g_touch_instance) return;

    uint8_t point_data[10] = {0};
    auto now = std::chrono::steady_clock::now();
    bool hardware_reports_press = false;

    // Read 10 bytes starting from 0x814E (Buffer Status) in a single transaction.
    if (g_touch_instance->read_reg(0x814E, point_data, 10)) {
        uint8_t status = point_data[0];
        if (status & 0x80) { // Buffer status bit (1 = data ready)
            int touch_count = status & 0x0F;
            if (touch_count > 0 && touch_count <= 5) {
                if (now < g_touch_instance->ignore_until) {
                    // EPD is refreshing, GT911 might send spurious noise. 
                    // Ignore new coordinate updates to prevent ghost clicks.
                    hardware_reports_press = g_touch_instance->is_pressed; // Maintain current state
                } else {
                    int raw_x = point_data[2] | (point_data[3] << 8);
                    int raw_y = point_data[4] | (point_data[5] << 8);

                    // Map to LVGL Logical Portrait (480x800).
                    int log_x = raw_y;
                    int log_y = 799 - raw_x;

                    // Clamp to prevent LVGL warnings
                    if (log_x < 0) log_x = 0;
                    if (log_x > 479) log_x = 479;
                    if (log_y < 0) log_y = 0;
                    if (log_y > 799) log_y = 799;

                    g_touch_instance->last_x = log_x;
                    g_touch_instance->last_y = log_y;
                    g_touch_instance->is_pressed = true;
                    
                    hardware_reports_press = true;
                }
            }

            // CRITICAL: Clear the status buffer so GT911 registers the next touch
            g_touch_instance->write_reg(0x814E, 0x00);
        }
    }

    if (!hardware_reports_press) {
        g_touch_instance->is_pressed = false;
    }

    // Report state to LVGL
    data->point.x = g_touch_instance->last_x;
    data->point.y = g_touch_instance->last_y;
    data->state = g_touch_instance->is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void RadxaTouch::ignore_touches_for(int ms) {
    ignore_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}
