// WIN32_LEAN_AND_MEAN must be defined BEFORE Windows.h to prevent winsock.h
// from being pulled in (which conflicts with winsock2.h used by httplib).
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <LvglWindowsIconResource.h>

#include "lvgl/demos/lv_demos.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/lvgl.h"

#include "epubHandler.h"
#include "pdfHandler.h"
#include <httplib.h>
#include <json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Include stb_image for cover image decoding (IMPLEMENTATION is in epubHandler.cpp)
#include "../LvglPlatform/lvgl/src/libs/gltf/stb_image/stb_image.h"

#include <mutex>

using json = nlohmann::json;

std::mutex lvgl_mutex;

#include "App.h"

int main() {

  lv_init();

  /*
   * Optional workaround for users who wants UTF-8 console output.
   * If you don't want that behavior can comment them out.
   *
   * Suggested by jinsc123654.
   */
#if LV_TXT_ENC == LV_TXT_ENC_UTF8
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
#endif

  int32_t zoom_level = 100;
  bool allow_dpi_override = false;
  bool simulator_mode = true;
  lv_display_t *display =
      lv_windows_create_display(L"LVGL Windows Simulator Display 1", 480, 800,
                                zoom_level, allow_dpi_override, simulator_mode);
  if (!display) {
    return -1;
  }

  HWND window_handle = lv_windows_get_display_window_handle(display);
  if (!window_handle) {
    return -1;
  }

  HICON icon_handle =
      LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDI_LVGL_WINDOWS));
  if (icon_handle) {
    SendMessageW(window_handle, WM_SETICON, TRUE, (LPARAM)icon_handle);
    SendMessageW(window_handle, WM_SETICON, FALSE, (LPARAM)icon_handle);
  }

  lv_indev_t *pointer_indev = lv_windows_acquire_pointer_indev(display);
  if (!pointer_indev) {
    return -1;
  }

  lv_indev_t *keypad_indev = lv_windows_acquire_keypad_indev(display);
  if (!keypad_indev) {
    return -1;
  }

  lv_indev_t *encoder_indev = lv_windows_acquire_encoder_indev(display);
  if (!encoder_indev) {
    return -1;
  }

  //  lv_demo_widgets();
  // lv_demo_benchmark();

  // Initialize the default theme
  lv_display_t *disp = lv_display_get_default();
  lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
  lv_display_set_theme(disp, th);

  // Windows backend needs at least one timer tick to mature and allocate its draw buffers
  // Wait 50ms so the 30ms timer is definitely due.
  lv_delay_ms(50);
  lv_timer_handler();

  // Build the sub-screens and load main
  build_tablet_ui();
  while (1) {
    extern std::mutex lvgl_mutex;
    lvgl_mutex.lock();
    uint32_t time_till_next = lv_timer_handler();
    lvgl_mutex.unlock();
    lv_delay_ms(time_till_next);
  }

  return 0;
}
