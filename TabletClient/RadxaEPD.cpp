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
#define PIN_RST_NAME "PIN_18"
#define PIN_BUSY_NAME "PIN_21"

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
  gpiod_line_set_value(line_dc, 0);
  gpiod_line_set_value(line_cs, 0);
  write(spi_fd, &cmd, 1);
  gpiod_line_set_value(line_cs, 1);
}

void RadxaEPD::send_data(uint8_t data) {
  gpiod_line_set_value(line_dc, 1);
  gpiod_line_set_value(line_cs, 0);
  write(spi_fd, &data, 1);
  gpiod_line_set_value(line_cs, 1);
}

void RadxaEPD::send_data_array(const uint8_t *data, size_t len) {
  gpiod_line_set_value(line_dc, 1);
  gpiod_line_set_value(line_cs, 0);

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

  hardware_reset();
  wait_until_idle();

  std::cout << "Initializing GDEY075T7 (Matching Python Script)..." << std::endl;

  // Power Setting - Do this FIRST
  send_command(0x01);
  send_data(0x07); // Internal DC/DC
  send_data(0x07);
  send_data(0x3F); // VDH=15V
  send_data(0x3F); // VDL=-15V

  // Panel Setting
  send_command(0x00);
  send_data(0x8F);

  // Power On Sequence
  send_command(0x03);
  send_data(0x00);

  // Power On
  send_command(0x04);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  wait_until_idle();

  // Booster Soft Start (3 bytes only!)
  send_command(0x06);
  send_data(0x17);
  send_data(0x17);
  send_data(0x27);

  // PLL Control - 50Hz
  send_command(0x30);
  send_data(0x06);

  // Resolution Setting
  send_command(0x61);
  send_data(0x03);
  send_data(0x20);
  send_data(0x01);
  send_data(0xE0);

  // Gate/Source Start Setting
  send_command(0x65);
  send_data(0x00);
  send_data(0x00);

  // VCOM and Data Interval Setting
  send_command(0x50);
  send_data(0x10);
  send_data(0x07);

  // TCON Setting
  send_command(0x60);
  send_data(0x22);

  std::cout << "GDEY075T7 Initialization complete!" << std::endl;
  return true;
}

void RadxaEPD::sleep() {
  send_command(0x02); // POWER OFF
  wait_until_idle();
  send_command(0x07); // DEEP_SLEEP
  send_data(0xA5);    // Data check code
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

  int x1 = area->x1;
  int y1 = area->y1;
  int x2 = area->x2;
  int y2 = area->y2;
  int w = x2 - x1 + 1;
  int h = y2 - y1 + 1;

  // Convert LVGL color buffer to 1-bit monochrome packed buffer
  // EPD expects: 1 = white, 0 = black, MSB first, 8 pixels per byte
  size_t num_bytes = (w * h + 7) / 8;
  std::vector<uint8_t> bw_buffer(num_bytes, 0xFF); // default white

#if LV_COLOR_DEPTH == 32
  lv_color32_t *buf32 = (lv_color32_t *)px_map;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int idx = y * w + x;
      uint8_t brightness =
          (buf32[idx].red + buf32[idx].green + buf32[idx].blue) / 3;
      if (brightness < 128) {
        int byte_idx = idx / 8;
        int bit_idx = 7 - (idx % 8);
        bw_buffer[byte_idx] &= ~(1 << bit_idx);
      }
    }
  }
#elif LV_COLOR_DEPTH == 16
  uint16_t *buf16 = (uint16_t *)px_map;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int idx = y * w + x;
      // RGB565: extract approximate brightness
      uint8_t r = (buf16[idx] >> 11) & 0x1F;
      uint8_t g = (buf16[idx] >> 5) & 0x3F;
      uint8_t b = buf16[idx] & 0x1F;
      uint8_t brightness = (r * 8 + g * 4 + b * 8) / 3;
      if (brightness < 128) {
        int byte_idx = idx / 8;
        int bit_idx = 7 - (idx % 8);
        bw_buffer[byte_idx] &= ~(1 << bit_idx);
      }
    }
  }
#else
  // LV_COLOR_DEPTH 8 or 1:
  memcpy(bw_buffer.data(), px_map, num_bytes);
#endif

  bool is_full = (x1 == 0 && y1 == 0 && w == 800 && h == 480);

  if (is_full) {
    // --- Full Refresh ---
    // Python script logic: send all white (0xFF) to OLD buffer (DTM1)
    std::vector<uint8_t> old_buf(num_bytes, 0xFF);
    g_epd_instance->send_command(0x10);
    g_epd_instance->send_data_array(old_buf.data(), num_bytes);

    // Send new image data to NEW buffer (DTM2)
    g_epd_instance->send_command(0x13);
    g_epd_instance->send_data_array(bw_buffer.data(), num_bytes);

    // Display Refresh (DRF)
    g_epd_instance->send_command(0x12);
    g_epd_instance->wait_until_idle();
  } else {
    // --- Partial Refresh ---
    // Align x to byte boundary (required by UC8179)
    int x1_aligned = x1 & 0xFFF8;
    int x2_aligned = x2 | 0x0007;

    g_epd_instance->send_command(0x91); // PARTIAL_IN
    g_epd_instance->send_command(0x90); // PARTIAL_WINDOW
    g_epd_instance->send_data(x1_aligned / 256);
    g_epd_instance->send_data(x1_aligned % 256);
    g_epd_instance->send_data(x2_aligned / 256);
    g_epd_instance->send_data(x2_aligned % 256);
    g_epd_instance->send_data(y1 / 256);
    g_epd_instance->send_data(y1 % 256);
    g_epd_instance->send_data(y2 / 256);
    g_epd_instance->send_data(y2 % 256);
    g_epd_instance->send_data(0x01);

    g_epd_instance->send_command(0x13); // NEW data
    g_epd_instance->send_data_array(bw_buffer.data(), num_bytes);

    g_epd_instance->send_command(0x12); // DISPLAY_REFRESH
    g_epd_instance->wait_until_idle();

    g_epd_instance->send_command(0x92); // PARTIAL_OUT
  }

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
