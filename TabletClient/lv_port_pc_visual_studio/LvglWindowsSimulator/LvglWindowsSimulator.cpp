#include <Windows.h>

#include <LvglWindowsIconResource.h>

#include "lvgl/demos/lv_demos.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/lvgl.h"

#include "epubHandler.h"
#include "pdfHandler.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// --- E-INK APP UI PLUMBING ---

// Global screens
lv_obj_t *screen_main;
lv_obj_t *screen_library;
lv_obj_t *screen_book_reader;
lv_obj_t *screen_ai;

static void load_screen_cb(lv_event_t *e) {
  lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(e);
  lv_scr_load(target);
}

// Globals for reader updates
lv_obj_t *reader_title_label;
lv_obj_t *reader_content_label;
lv_obj_t *reader_img;       // Added for EPUB images
lv_obj_t *reader_bottombar; // Made global to toggle hidden state
std::vector<std::string> book_filepaths;
lv_obj_t *book_list = NULL;

static EpubHandler *current_epub = nullptr;
static PdfHandler *current_pdf = nullptr;
static bool is_epub_active = false;
static bool is_bottombar_visible = false; // Track toggle state

static void build_library_list(); // Forward declaration

static void update_reader_ui() {
  std::string text;
  if (is_epub_active && current_epub) {
    lv_label_set_text(reader_title_label, current_epub->getTitle().c_str());
    text = current_epub->getContent();
  } else if (!is_epub_active && current_pdf) {
    lv_label_set_text(reader_title_label, current_pdf->getTitle().c_str());
    text = current_pdf->getContent();
  }

  // Parse for [IMG:...] marker
  size_t img_start = text.find("[IMG:");
  if (img_start != std::string::npos) {
    size_t img_end = text.find("]", img_start);
    if (img_end != std::string::npos) {
      std::string img_path =
          text.substr(img_start + 5, img_end - (img_start + 5));
      text.erase(img_start, img_end - img_start + 1);

      if (reader_img) {
        lv_image_set_src(reader_img, img_path.c_str());
        lv_obj_clear_flag(reader_img, LV_OBJ_FLAG_HIDDEN);
      }
    }
  } else {
    // Hide image if not present on this page
    if (reader_img) {
      lv_obj_add_flag(reader_img, LV_OBJ_FLAG_HIDDEN);
    }
  }

  lv_label_set_text(reader_content_label, text.c_str());
}

static void reader_next_cb(lv_event_t *e) {
  if (is_epub_active && current_epub) {
    current_epub->nextPage();
  } else if (!is_epub_active && current_pdf) {
    current_pdf->nextPage();
  }
  update_reader_ui();
}

static void reader_prev_cb(lv_event_t *e) {
  if (is_epub_active && current_epub) {
    current_epub->prevPage();
  } else if (!is_epub_active && current_pdf) {
    current_pdf->prevPage();
  }
  update_reader_ui();
}

static void toggle_bottombar_cb(lv_event_t *e) {
  if (is_bottombar_visible) {
    lv_obj_add_flag(reader_bottombar, LV_OBJ_FLAG_HIDDEN);
    is_bottombar_visible = false;
  } else {
    lv_obj_clear_flag(reader_bottombar, LV_OBJ_FLAG_HIDDEN);
    is_bottombar_visible = true;
  }
}

static void book_clicked_cb(lv_event_t *e) {
  const char *filepath = (const char *)lv_event_get_user_data(e);
  std::string ext = std::filesystem::path(filepath).extension().string();

  // Convert to lowercase
  for (auto &c : ext)
    c = tolower(c);

  if (ext == ".epub") {
    if (current_epub)
      delete current_epub;
    current_epub = new EpubHandler();
    current_epub->loadEpub(filepath);
    is_epub_active = true;
  } else if (ext == ".pdf") {
    if (current_pdf)
      delete current_pdf;
    current_pdf = new PdfHandler();
    current_pdf->loadPdf(filepath);
    is_epub_active = false;
  } else {
    if (reader_title_label)
      lv_label_set_text(reader_title_label, "Unknown Book Format");
    if (reader_content_label)
      lv_label_set_text(reader_content_label, "Unsupported file.");
    lv_scr_load(screen_book_reader);
    return;
  }

  is_bottombar_visible = true;

  update_reader_ui();
  lv_scr_load(screen_book_reader);
}

static void refresh_lib_cb(lv_event_t *e) { build_library_list(); }

static void build_library_list() {
  if (book_list == NULL)
    return;

  // Clear existing children
  lv_obj_clean(book_list);
  book_filepaths.clear();

  if (!std::filesystem::exists("books")) {
    std::filesystem::create_directory("books");
  }

  for (const auto &entry : std::filesystem::directory_iterator("books")) {
    if (entry.is_regular_file()) {
      std::string ext = entry.path().extension().string();
      for (auto &c : ext)
        c = tolower(c);

      if (ext == ".epub" || ext == ".pdf") {
        book_filepaths.push_back(entry.path().string());

        lv_obj_t *btn = lv_btn_create(book_list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_add_event_cb(btn, book_clicked_cb, LV_EVENT_CLICKED,
                            (void *)book_filepaths.back().c_str());
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, entry.path().filename().string().c_str());
        lv_obj_center(lbl);
      }
    }
  }
}

void build_screens() {
  screen_main = lv_obj_create(NULL);
  screen_library = lv_obj_create(NULL);
  screen_book_reader = lv_obj_create(NULL);
  screen_ai = lv_obj_create(NULL);

  // Apply white background to all screens
  lv_obj_set_style_bg_color(screen_main, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_color(screen_library, lv_color_hex(0xFFFFFF),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(screen_book_reader, lv_color_hex(0xFFFFFF),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(screen_ai, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  // Toggle bottom bar when clicking anywhere on the background of the read
  // screen
  lv_obj_add_event_cb(screen_book_reader, toggle_bottombar_cb, LV_EVENT_CLICKED,
                      NULL);

  // --- MAIN SCREEN ---
  lv_obj_t *main_title = lv_label_create(screen_main);
  lv_label_set_text(main_title, "E-Ink Reader Home");
  lv_obj_align(main_title, LV_ALIGN_TOP_MID, 0, 50);

  lv_obj_t *btn_lib = lv_btn_create(screen_main);
  lv_obj_align(btn_lib, LV_ALIGN_CENTER, 0, -50);
  lv_obj_set_size(btn_lib, 200, 50);
  lv_obj_add_event_cb(btn_lib, load_screen_cb, LV_EVENT_CLICKED,
                      screen_library);
  lv_obj_t *lbl_lib = lv_label_create(btn_lib);
  lv_label_set_text(lbl_lib, "My Library");
  lv_obj_center(lbl_lib);

  lv_obj_t *btn_ai = lv_btn_create(screen_main);
  lv_obj_align(btn_ai, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_size(btn_ai, 200, 50);
  lv_obj_add_event_cb(btn_ai, load_screen_cb, LV_EVENT_CLICKED, screen_ai);
  lv_obj_t *lbl_ai = lv_label_create(btn_ai);
  lv_label_set_text(lbl_ai, "AI Assistant");
  lv_obj_center(lbl_ai);

  // --- LIBRARY SCREEN ---
  lv_obj_t *lib_title = lv_label_create(screen_library);
  lv_label_set_text(lib_title, "Library Books");
  lv_obj_align(lib_title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *lib_back = lv_btn_create(screen_library);
  lv_obj_align(lib_back, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_add_event_cb(lib_back, load_screen_cb, LV_EVENT_CLICKED, screen_main);
  lv_obj_t *lbl_lib_back = lv_label_create(lib_back);
  lv_label_set_text(lbl_lib_back, "Home");
  lv_obj_center(lbl_lib_back);

  lv_obj_t *lib_refresh = lv_btn_create(screen_library);
  lv_obj_align(lib_refresh, LV_ALIGN_TOP_RIGHT, -20, 20);
  lv_obj_add_event_cb(lib_refresh, refresh_lib_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_lib_refresh = lv_label_create(lib_refresh);
  lv_label_set_text(lbl_lib_refresh, "Refresh");
  lv_obj_center(lbl_lib_refresh);

  book_list = lv_obj_create(screen_library);
  lv_obj_set_size(book_list, 400, 600);
  lv_obj_align(book_list, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_flex_flow(book_list, LV_FLEX_FLOW_COLUMN);

  // Initial population of the library list
  build_library_list();

  // --- BOOK READER SCREEN ---
  lv_obj_t *reader_topbar = lv_obj_create(screen_book_reader);
  lv_obj_set_size(reader_topbar, LV_PCT(100), 60);
  lv_obj_align(reader_topbar, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *reader_back = lv_btn_create(reader_topbar);
  lv_obj_align(reader_back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(reader_back, load_screen_cb, LV_EVENT_CLICKED,
                      screen_library);
  lv_obj_t *lbl_reader_back = lv_label_create(reader_back);
  lv_label_set_text(lbl_reader_back, "Library");
  lv_obj_center(lbl_reader_back);

  reader_title_label = lv_label_create(reader_topbar);
  lv_label_set_text(reader_title_label, "Reading Book...");
  lv_obj_align(reader_title_label, LV_ALIGN_CENTER, 0, 0);

  // Create a scrollable container for the body of the reader
  lv_obj_t *reader_body = lv_obj_create(screen_book_reader);
  lv_obj_set_size(reader_body, 480,
                  680); // 800 (display) - 60 (topbar) - 60 (bottombar)
  lv_obj_align(reader_body, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_set_style_bg_opa(reader_body, 0, 0);
  lv_obj_set_style_border_width(reader_body, 0, 0);
  lv_obj_remove_flag(reader_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(reader_body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(reader_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(reader_body, 0, 0);

  // Dedicated image object, hidden by default
  reader_img = lv_image_create(reader_body);
  lv_obj_set_width(reader_img, LV_SIZE_CONTENT);
  lv_obj_set_height(reader_img, LV_SIZE_CONTENT);
  lv_obj_add_flag(reader_img, LV_OBJ_FLAG_HIDDEN);

  reader_content_label = lv_label_create(reader_body);
  lv_label_set_long_mode(reader_content_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(reader_content_label, 460);

  lv_label_set_text(reader_content_label,
                    "Select a book from the library to begin reading.");

  // Bottom Toolbar for Pagination (Always Visible)
  reader_bottombar = lv_obj_create(screen_book_reader);
  lv_obj_set_size(reader_bottombar, LV_PCT(100), 60);
  lv_obj_align(reader_bottombar, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_set_flex_flow(reader_bottombar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(reader_bottombar, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_prev = lv_btn_create(reader_bottombar);
  lv_obj_add_event_cb(btn_prev, reader_prev_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_prev = lv_label_create(btn_prev);
  lv_label_set_text(lbl_prev, "<- Prev");

  lv_obj_t *btn_next = lv_btn_create(reader_bottombar);
  lv_obj_add_event_cb(btn_next, reader_next_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_next = lv_label_create(btn_next);
  lv_label_set_text(lbl_next, "Next ->");

  // --- AI ASSISTANT SCREEN ---
  lv_obj_t *ai_back = lv_btn_create(screen_ai);
  lv_obj_align(ai_back, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_add_event_cb(ai_back, load_screen_cb, LV_EVENT_CLICKED, screen_main);
  lv_obj_t *lbl_ai_back = lv_label_create(ai_back);
  lv_label_set_text(lbl_ai_back, "Home");
  lv_obj_center(lbl_ai_back);

  lv_obj_t *ai_title = lv_label_create(screen_ai);
  lv_label_set_text(ai_title, "AI Assistant");
  lv_obj_align(ai_title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *ai_content = lv_label_create(screen_ai);
  lv_label_set_text(ai_content,
                    "AI:\nHello! I am your AI reading assistant.\nSince you "
                    "liked '1984', I highly recommend\n'Brave New World' by "
                    "Aldous Huxley.\nWould you like to read it?");
  lv_obj_align(ai_content, LV_ALIGN_CENTER, 0, 0);
}

// --- END E-INK APP UI PLUMBING ---

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

  // Initialize the monochrome theme.
  // 1. Get the default display
  lv_display_t *disp = lv_display_get_default();

  // 2. Initialize the monochrome theme (light background)
  lv_theme_t *th = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);

  // 3. Apply it to the display
  lv_display_set_theme(disp, th);

  // Build the sub-screens and load main
  build_screens();
  lv_scr_load(screen_main);
  while (1) {
    uint32_t time_till_next = lv_timer_handler();
    lv_delay_ms(time_till_next);
  }

  return 0;
}
