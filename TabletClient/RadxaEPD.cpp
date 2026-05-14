#include "RadxaEPD.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <gpiod.h>
#include <iostream>
#include <linux/spi/spidev.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

// LVGL includes (adjust path as needed depending on your include setup)
#include "lvgl/lvgl.h"

// Hardware configuration
#define SPI_DEVICE "/dev/spidev3.0"
#define SPI_SPEED 500000
#define SPI_MODE SPI_MODE_0
#define SPI_BITS 8

// GPIO Configuration (Radxa Zero 3W physical pin mapping)
// Names from `sudo gpioinfo` output — kernel uses "PIN_XX" format
#define PIN_CS_NAME "PIN_24"
#define PIN_DC_NAME "PIN_22"
#define PIN_RST_NAME "PIN_11"
#define PIN_BUSY_NAME "PIN_18"

RadxaEPD *g_epd_instance = nullptr;

RadxaEPD::RadxaEPD()
    : spi_fd(-1), gpio_chip(nullptr), line_cs(nullptr), line_dc(nullptr),
      line_rst(nullptr), line_busy(nullptr) {
  g_epd_instance = this;
}

RadxaEPD::~RadxaEPD() {
  if (spi_fd >= 0)
    close(spi_fd);
  if (line_cs)
    gpiod_line_release(line_cs);
  if (line_dc)
    gpiod_line_release(line_dc);
  if (line_rst)
    gpiod_line_release(line_rst);
  if (line_busy)
    gpiod_line_release(line_busy);
  if (gpio_chip)
    gpiod_chip_close(gpio_chip);
}

bool RadxaEPD::init_gpio() {
  line_cs = gpiod_line_find(PIN_CS_NAME);
  line_dc = gpiod_line_find(PIN_DC_NAME);
  line_rst = gpiod_line_find(PIN_RST_NAME);
  line_busy = gpiod_line_find(PIN_BUSY_NAME);

  if (!line_cs)
    std::cerr << "Failed to find GPIO line: " << PIN_CS_NAME << std::endl;
  if (!line_dc)
    std::cerr << "Failed to find GPIO line: " << PIN_DC_NAME << std::endl;
  if (!line_rst)
    std::cerr << "Failed to find GPIO line: " << PIN_RST_NAME << std::endl;
  if (!line_busy)
    std::cerr << "Failed to find GPIO line: " << PIN_BUSY_NAME << std::endl;

  if (!line_cs || !line_dc || !line_rst || !line_busy) {
    std::cerr << "Could not resolve all GPIO lines by name. Exiting."
              << std::endl;
    return false;
  }

  gpiod_line_request_output(line_cs, "radxa_epd_cs", 1);
  gpiod_line_request_output(line_dc, "radxa_epd_dc", 0);
  gpiod_line_request_output(line_rst, "radxa_epd_rst", 1);
  gpiod_line_request_input(line_busy,
                           "radxa_epd_busy"); // Note: pull-up might be needed
                                              // depending on hardware/dtb

  return true;
}

bool RadxaEPD::init_spi() {
  spi_fd = open(SPI_DEVICE, O_RDWR);
  if (spi_fd < 0) {
    std::cerr << "Failed to open SPI device " << SPI_DEVICE << std::endl;
    return false;
  }

  uint8_t mode = SPI_MODE;
  uint8_t bits = SPI_BITS;
  uint32_t speed = SPI_SPEED;

  if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) == -1)
    return false;
  if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) == -1)
    return false;
  if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1)
    return false;

  return true;
}

void RadxaEPD::hardware_reset() {
  gpiod_line_set_value(line_rst, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  gpiod_line_set_value(line_rst, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void RadxaEPD::send_command(uint8_t cmd) {
  gpiod_line_set_value(line_cs, 0);  // CS low FIRST
  gpiod_line_set_value(line_dc, 0);  // DC low = command
  write(spi_fd, &cmd, 1);
  gpiod_line_set_value(line_cs, 1);
}

void RadxaEPD::send_data(uint8_t data) {
  gpiod_line_set_value(line_cs, 0);  // CS low FIRST
  gpiod_line_set_value(line_dc, 1);  // DC high = data
  write(spi_fd, &data, 1);
  gpiod_line_set_value(line_cs, 1);
}

void RadxaEPD::send_data_array(const uint8_t *data, size_t len) {
  gpiod_line_set_value(line_cs, 0);  // CS low FIRST
  gpiod_line_set_value(line_dc, 1);  // DC high = data

  // SPI transfers might have a max length limit depending on the OS (e.g. 4096
  // bytes). We should loop through the data in chunks if needed.
  const size_t CHUNK_SIZE = 4096;
  for (size_t i = 0; i < len; i += CHUNK_SIZE) {
    size_t chunk = std::min(CHUNK_SIZE, len - i);
    write(spi_fd, data + i, chunk);
  }

  gpiod_line_set_value(line_cs, 1);
}

void RadxaEPD::wait_until_idle() {
  while (gpiod_line_get_value(line_busy) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

bool RadxaEPD::init() {
  if (!init_gpio())
    return false;
  if (!init_spi())
    return false;

  std::cout << "Initializing GDEY075T7 (Matching Latest Python Script)..."
            << std::endl;

  hardware_reset();
  std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Post-reset stabilization

  // Panel Setting
  send_command(0x00);
  send_data(0x1F);

  // VCOM and Data Interval Setting (before Power On)
  send_command(0x50);
  send_data(0x10);
  send_data(0x07);

  // Power On
  send_command(0x04);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  wait_until_idle();

  // Booster Soft Start (4 bytes!)
  send_command(0x06);
  send_data(0x27);
  send_data(0x27);
  send_data(0x18);
  send_data(0x17);

  // Cascade Setting
  send_command(0xE0);
  send_data(0x02);

  // Force Temperature
  send_command(0xE5);
  send_data(0x5A);

  // Resolution Setting
  send_command(0x61);
  send_data(0x03);
  send_data(0x20);
  send_data(0x01);
  send_data(0xE0);

  std::cout << "GDEY075T7 Initialization complete!" << std::endl;
  return true;
}

void RadxaEPD::sleep() {
  // Set VCOM border waveform to safe state before power-down
  send_command(0x50); // VCOM AND DATA INTERVAL SETTING
  send_data(0xF7);    // WBRmode safe value for sleep

  send_command(0x02); // POWER OFF
  wait_until_idle();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  send_command(0x07); // DEEP_SLEEP
  send_data(0xA5);    // Data check code
}

void RadxaEPD::wake() {
  hardware_reset();
  init(); // Re-run initialization
}

void RadxaEPD::flush_cb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map) {
  if (!g_epd_instance) {
    lv_display_flush_ready(disp);
    return;
  }

  // LVGL renders in portrait (480x800). We must rotate 90° to physical
  // (800x480). In FULL render mode, area should always be the full logical
  // screen.
  const int log_w = 480;                            // LVGL logical width
  const int log_h = 800;                            // LVGL logical height
  const int phys_w = 800;                           // EPD physical width
  const int phys_h = 480;                           // EPD physical height
  const size_t frame_bytes = (phys_w * phys_h) / 8; // 48000 bytes

  // Step 1: Build the physical 800x480 1-bit buffer by rotating the LVGL
  // pixels. Rotation: logical (lx, ly) -> physical (phys_w - 1 - ly, lx)
  //   i.e. rotate 90° clockwise
  std::vector<uint8_t> phys_buffer(frame_bytes, 0xFF); // default white

#if LV_COLOR_DEPTH == 32
  lv_color32_t *buf32 = (lv_color32_t *)px_map;
  for (int ly = 0; ly < log_h; ly++) {
    for (int lx = 0; lx < log_w; lx++) {
      int src_idx = ly * log_w + lx;
      uint8_t brightness =
          (buf32[src_idx].red + buf32[src_idx].green + buf32[src_idx].blue) / 3;
      if (brightness < 128) {
        // Map logical portrait -> physical landscape (90° CW rotation)
        int px = log_h - 1 - ly;
        int py = lx;
        int phys_idx = py * phys_w + px;
        int byte_idx = phys_idx / 8;
        int bit_idx = 7 - (phys_idx % 8);
        phys_buffer[byte_idx] &= ~(1 << bit_idx); // CLEAR bit = black
      }
    }
  }
#elif LV_COLOR_DEPTH == 16
  uint16_t *buf16 = (uint16_t *)px_map;
  for (int ly = 0; ly < log_h; ly++) {
    for (int lx = 0; lx < log_w; lx++) {
      int src_idx = ly * log_w + lx;
      uint8_t r = (buf16[src_idx] >> 11) & 0x1F;
      uint8_t g = (buf16[src_idx] >> 5) & 0x3F;
      uint8_t b = buf16[src_idx] & 0x1F;
      uint8_t brightness = (r * 8 + g * 4 + b * 8) / 3;
      if (brightness < 128) {
        int px = log_h - 1 - ly;
        int py = lx;
        int phys_idx = py * phys_w + px;
        int byte_idx = phys_idx / 8;
        int bit_idx = 7 - (phys_idx % 8);
        phys_buffer[byte_idx] &= ~(1 << bit_idx); // CLEAR bit = black
      }
    }
  }
#else
  // For 1-bit depth, we'd need a bit-level rotation; skip for now
  memcpy(phys_buffer.data(), px_map, frame_bytes);
#endif

  std::cout << "Refreshing display (FULL, rotated)..." << std::endl;

  // Step 2: Send to EPD — always full frame
  // Write white (0xFF) to OLD buffer (0x10)
  std::vector<uint8_t> old_buf(frame_bytes, 0xFF);
  g_epd_instance->send_command(0x10);
  g_epd_instance->send_data_array(old_buf.data(), frame_bytes);

  // Write rotated image to NEW buffer (0x13)
  g_epd_instance->send_command(0x13);
  g_epd_instance->send_data_array(phys_buffer.data(), frame_bytes);

  // Display Refresh
  g_epd_instance->send_command(0x12);
  std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Mandatory delay before polling BUSY
  g_epd_instance->wait_until_idle();

  std::cout << "Display updated!" << std::endl;

  lv_display_flush_ready(disp);
}

// System Status Checkers

int RadxaEPD::get_battery_percentage() {
  // Basic polling of Linux sysfs for battery
  std::ifstream file(
      "/sys/class/power_supply/axp20x-battery/capacity"); // Adjust to radxa's
                                                          // actual PMIC battery
                                                          // path
  if (file.is_open()) {
    int capacity = 100;
    file >> capacity;
    return capacity;
  }
  return -1; // Unknown
}

bool RadxaEPD::is_wifi_connected() {
  std::ifstream file("/sys/class/net/wlan0/operstate");
  if (file.is_open()) {
    std::string state;
    file >> state;
    return state == "up";
  }
  return false;
}

bool RadxaEPD::is_wireguard_connected() {
  std::ifstream file("/sys/class/net/wg0/operstate");
  if (file.is_open()) {
    std::string state;
    file >> state;
    return state == "up" ||
           state == "unknown"; // wireguard often reports unknown when up
  }
  return false;
}
