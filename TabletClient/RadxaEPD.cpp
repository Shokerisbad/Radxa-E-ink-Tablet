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
#include "RadxaTouch.h"

// LVGL includes (adjust path as needed depending on your include setup)
#include "lvgl/lvgl.h"

// Hardware configuration
#define SPI_DEVICE "/dev/spidev3.0"
#define SPI_SPEED 2000000
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
    : spi_fd(-1), line_cs(nullptr), line_dc(nullptr),
      line_rst(nullptr), line_busy(nullptr), first_refresh(true) {
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
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
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

  std::cout << "Initializing GDEY075T7 (Matching Latest Python Script)..."
            << std::endl;

  hardware_reset();

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

void RadxaEPD::refresh_full(const uint8_t *buffer) {
  const size_t frame_bytes = (800 * 480) / 8; // 48000 bytes

  // Write white (0x00) to OLD buffer (0x10) — panel polarity: 0x00 = white
  std::vector<uint8_t> old_buf(frame_bytes, 0x00);
  send_command(0x10);
  send_data_array(old_buf.data(), frame_bytes);

  // Write rotated image to NEW buffer (0x13)
  send_command(0x13);
  send_data_array(buffer, frame_bytes);

  // Display Refresh
  send_command(0x12);
  std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Mandatory delay before polling BUSY
  wait_until_idle();
}

void RadxaEPD::refresh_partial(int x_start, int y_start, const uint8_t *buffer, int part_w, int part_h) {
  int x_end = x_start + part_w - 1;
  int y_end = y_start + part_h - 1;
  size_t count = (part_w * part_h) / 8;

  // Partial settings from manufacturer reference
  send_command(0x50);
  send_data(0xA9);
  send_data(0x07);

  send_command(0x91); // Enter partial mode
  send_command(0x90); // Partial resolution setting
  send_data(x_start / 256);
  send_data(x_start % 256);
  send_data(x_end / 256);
  send_data((x_end % 256) - 1);
  
  send_data(y_start / 256);
  send_data(y_start % 256);
  send_data(y_end / 256);
  send_data((y_end % 256) - 1);
  send_data(0x01); // Scan parameter (0x01 = scan only partial area)

  send_command(0x13); // Write data to New SRAM
  send_data_array(buffer, count);

  send_command(0x12); // Display Refresh
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  wait_until_idle();

  send_command(0x92); // Exit partial mode
}

static std::vector<uint8_t> g_last_frame(48000, 0x00);
static int s_min_x = 800, s_min_y = 480, s_max_x = -1, s_max_y = -1;
static bool s_changed = false;

void RadxaEPD::flush_cb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map) {
  if (!g_epd_instance) {
    lv_display_flush_ready(disp);
    return;
  }

  const int log_w = 480;                            // LVGL logical width
  const int log_h = 800;                            // LVGL logical height
  const int phys_w = 800;                           // EPD physical width
  const int phys_h = 480;                           // EPD physical height

  // Calculate logical dirty area dimensions
  int w = area->x2 - area->x1 + 1;
  int h = area->y2 - area->y1 + 1;

  // Decide if we should do a full refresh or partial refresh
  // Full refresh is used for the first draw or large changes (> 40% screen area)
  bool is_full = g_epd_instance->first_refresh || (w * h > 150000);

  if (is_full) {
    g_epd_instance->first_refresh = false;

    // Full Refresh Flow
    const size_t frame_bytes = (phys_w * phys_h) / 8; // 48000 bytes
    std::vector<uint8_t> phys_buffer(frame_bytes, 0x00); // default white

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
          phys_buffer[byte_idx] |= (1 << bit_idx); // SET bit = black
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
          phys_buffer[byte_idx] |= (1 << bit_idx); // SET bit = black
        }
      }
    }
#else
    memcpy(phys_buffer.data(), px_map, frame_bytes);
#endif

    std::cout << "Refreshing display (FULL, rotated)..." << std::endl;
    g_last_frame = phys_buffer;
    g_epd_instance->refresh_full(phys_buffer.data());

  } else {
    // Partial Refresh Flow
    // Calculate raw physical coordinates from logical dirty area
    // Rotation: logical (lx, ly) -> physical (log_h - 1 - ly, lx)
    int px_start = log_h - 1 - area->y2;
    int px_end = log_h - 1 - area->y1;
    int py_start = area->x1;
    int py_end = area->x2;

    // Align physical horizontal coordinates to 8-pixel boundaries for UC8179 controller
    int x_start = px_start & ~7;
    int x_end = ((px_end + 8) & ~7) - 1;
    int y_start = py_start;
    int y_end = py_end;

    // Clamp coordinates
    if (x_start < 0) x_start = 0;
    if (x_end > 799) x_end = 799;
    if (y_start < 0) y_start = 0;
    if (y_end > 479) y_end = 479;

    int part_w = x_end - x_start + 1;
    int part_h = y_end - y_start + 1;
    size_t part_bytes = (part_w * part_h) / 8;

    std::vector<uint8_t> part_buffer(part_bytes, 0x00); // default white

#if LV_COLOR_DEPTH == 32
    lv_color32_t *buf32 = (lv_color32_t *)px_map;
    for (int py_offset = 0; py_offset < part_h; py_offset++) {
      int py = y_start + py_offset;
      int lx = py;
      for (int px_offset = 0; px_offset < part_w; px_offset++) {
        int px = x_start + px_offset;
        int ly = log_h - 1 - px;

        int src_idx = ly * log_w + lx;
        uint8_t brightness =
            (buf32[src_idx].red + buf32[src_idx].green + buf32[src_idx].blue) / 3;
        if (brightness < 128) {
          int phys_idx = py_offset * part_w + px_offset;
          int byte_idx = phys_idx / 8;
          int bit_idx = 7 - (phys_idx % 8);
          part_buffer[byte_idx] |= (1 << bit_idx); // SET bit = black
        }
      }
    }
#elif LV_COLOR_DEPTH == 16
    uint16_t *buf16 = (uint16_t *)px_map;
    for (int py_offset = 0; py_offset < part_h; py_offset++) {
      int py = y_start + py_offset;
      int lx = py;
      for (int px_offset = 0; px_offset < part_w; px_offset++) {
        int px = x_start + px_offset;
        int ly = log_h - 1 - px;

        int src_idx = ly * log_w + lx;
        uint8_t r = (buf16[src_idx] >> 11) & 0x1F;
        uint8_t g = (buf16[src_idx] >> 5) & 0x3F;
        uint8_t b = buf16[src_idx] & 0x1F;
        uint8_t brightness = (r * 8 + g * 4 + b * 8) / 3;
        if (brightness < 128) {
          int phys_idx = py_offset * part_w + px_offset;
          int byte_idx = phys_idx / 8;
          int bit_idx = 7 - (phys_idx % 8);
          part_buffer[byte_idx] |= (1 << bit_idx); // SET bit = black
        }
      }
    }
#endif

    bool local_changed = false;
    int part_stride = part_w / 8;
    int phys_stride = 800 / 8;
    for (int py = 0; py < part_h; py++) {
      int phys_y = y_start + py;
      for (int px_byte = 0; px_byte < part_stride; px_byte++) {
        int phys_x_byte = (x_start / 8) + px_byte;
        int part_idx = py * part_stride + px_byte;
        int phys_idx = phys_y * phys_stride + phys_x_byte;
        if (g_last_frame[phys_idx] != part_buffer[part_idx]) {
           local_changed = true;
           g_last_frame[phys_idx] = part_buffer[part_idx];
        }
      }
    }

    if (local_changed) {
        s_changed = true;
        if (x_start < s_min_x) s_min_x = x_start;
        if (y_start < s_min_y) s_min_y = y_start;
        if (x_end > s_max_x) s_max_x = x_end;
        if (y_end > s_max_y) s_max_y = y_end;
    }

    if (lv_display_flush_is_last(disp)) {
        if (s_changed) {
            s_min_x = s_min_x & ~7;
            s_max_x = ((s_max_x + 8) & ~7) - 1;
            int final_w = s_max_x - s_min_x + 1;
            int final_h = s_max_y - s_min_y + 1;
            
            std::vector<uint8_t> final_buf((final_w * final_h) / 8, 0x00);
            int final_stride = final_w / 8;
            for (int py = 0; py < final_h; py++) {
                int phys_y = s_min_y + py;
                for (int px_byte = 0; px_byte < final_stride; px_byte++) {
                    int phys_x_byte = (s_min_x / 8) + px_byte;
                    final_buf[py * final_stride + px_byte] = g_last_frame[phys_y * phys_stride + phys_x_byte];
                }
            }

            std::cout << "Refreshing display (PARTIAL BATCHED: x=" << s_min_x << ", y=" << s_min_y
                      << ", w=" << final_w << ", h=" << final_h << ")..." << std::endl;
            g_epd_instance->refresh_partial(s_min_x, s_min_y, final_buf.data(), final_w, final_h);
        }
        
        s_min_x = 800; s_min_y = 480; s_max_x = -1; s_max_y = -1;
        s_changed = false;
    }
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
