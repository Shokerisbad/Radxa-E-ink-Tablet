#ifdef _WIN32
// WIN32_LEAN_AND_MEAN must be defined BEFORE Windows.h to prevent winsock.h
// from being pulled in (which conflicts with winsock2.h used by httplib).
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#endif

#include "App.h"
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

// --- IMAGE UTILITIES ---
static void write_bmp_cover(const char* filename, int w, int h, int comp, const unsigned char* data) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    int pad = (4 - ((w * 3) % 4)) % 4;
    int data_sz = (w * 3 + pad) * h;
    unsigned char header[54] = { 'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
                                40,  0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 1, 0,
                                24,  0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,
                                0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0 };
    int filesize = 54 + data_sz;
    header[2] = (unsigned char)(filesize);
    header[3] = (unsigned char)(filesize >> 8);
    header[4] = (unsigned char)(filesize >> 16);
    header[5] = (unsigned char)(filesize >> 24);
    header[18] = (unsigned char)(w);
    header[19] = (unsigned char)(w >> 8);
    header[20] = (unsigned char)(w >> 16);
    header[21] = (unsigned char)(w >> 24);
    header[22] = (unsigned char)(h);
    header[23] = (unsigned char)(h >> 8);
    header[24] = (unsigned char)(h >> 16);
    header[25] = (unsigned char)(h >> 24);
    header[34] = (unsigned char)(data_sz);
    header[35] = (unsigned char)(data_sz >> 8);
    header[36] = (unsigned char)(data_sz >> 16);
    header[37] = (unsigned char)(data_sz >> 24);
    fwrite(header, 1, 54, f);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            fputc(data[(y * w + x) * comp + 2], f);
            fputc(data[(y * w + x) * comp + 1], f);
            fputc(data[(y * w + x) * comp + 0], f);
        }
        for (int p = 0; p < pad; p++) fputc(0, f);
    }
    fclose(f);
}

// --- READING TRACKER DATA ---

std::vector<FinishedBook> locally_finished_books;

// --- CACHE MANAGEMENT ---
static void checkAndClearCache() {
  std::string cachePath = "books/.cache";
  if (!std::filesystem::exists(cachePath))
    return;

  uintmax_t totalSize = 0;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(cachePath)) {
    if (std::filesystem::is_regular_file(entry)) {
      totalSize += std::filesystem::file_size(entry);
    }
  }

  // 3GB = 3LL * 1024 * 1024 * 1024;
  const uintmax_t MAX_CACHE_SIZE = 3ULL * 1024 * 1024 * 1024;

  if (totalSize > MAX_CACHE_SIZE) {
    std::cout << "Cache exceeded 3GB. Clearing..." << std::endl;
    std::filesystem::remove_all(cachePath);
    std::filesystem::create_directories(cachePath);
  }
}

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
lv_obj_t *ai_content = NULL;   // AI screen results container
lv_obj_t *ai_input_ta = NULL;  // AI screen text input
lv_obj_t *reader_page_label = NULL; // Page counter label (e.g. "3 / 42")
std::vector<std::string> book_filepaths;
lv_obj_t *book_list = NULL;

static EpubHandler *current_epub = nullptr;
static PdfHandler *current_pdf = nullptr;
static bool is_epub_active = false;
static bool is_bottombar_visible = false; // Track toggle state

static void build_library_list(); // Forward declaration
static void request_ai_recommendation(const std::string &user_prompt, bool exact_match = false, bool use_reviews = true);

static void show_rating_popup(const std::string &book_title, int total_pages) {
  lv_obj_t *modal = lv_obj_create(lv_scr_act());
  lv_obj_set_size(modal, 400, 300);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(modal, 2, 0);
  lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *label = lv_label_create(modal);
  lv_label_set_text_fmt(label,
                        "Congratulations!\nYou finished %s.\nRate this book:",
                        book_title.c_str());
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *btn_container = lv_obj_create(modal);
  lv_obj_set_size(btn_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(btn_container, 0, 0);
  lv_obj_set_style_border_width(btn_container, 0, 0);
  lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Storing data for the callbacks
  struct RatingData {
    std::string title;
    int pages;
    int rating;
    lv_obj_t *modal_ptr;
  };

  for (int i = 1; i <= 5; ++i) {
    lv_obj_t *btn = lv_btn_create(btn_container);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text_fmt(btn_lbl, "%d*", i);

    RatingData *rdata = new RatingData{book_title, total_pages, i, modal};

    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e) {
          RatingData *data = (RatingData *)lv_event_get_user_data(e);
          locally_finished_books.push_back(
              {data->title, data->pages, data->rating});
          std::cout << "Stored finished book: " << data->title
                    << " with rating: " << data->rating << std::endl;

          lv_obj_del(data->modal_ptr);

          // Auto prompt AI
          std::string prompt =
              "I just finished " + data->title + " and gave it a " +
              std::to_string(data->rating) +
              "/5 star rating. Please recommend something else.";
          request_ai_recommendation(prompt);

          delete data;
        },
        LV_EVENT_CLICKED, rdata);
  }

  lv_obj_t *close_btn = lv_btn_create(modal);
  lv_obj_t *close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "No Thanks");
  lv_obj_add_event_cb(
      close_btn,
      [](lv_event_t *e) { lv_obj_del((lv_obj_t *)lv_event_get_user_data(e)); },
      LV_EVENT_CLICKED, modal);
}

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

  // Update page counter
  if (reader_page_label) {
    int cur = 0, tot = 0;
    if (is_epub_active && current_epub) {
      cur = current_epub->getCurrentPage() + 1;
      tot = current_epub->getTotalPages();
    } else if (!is_epub_active && current_pdf) {
      cur = current_pdf->getCurrentPage() + 1;
      tot = current_pdf->getTotalPages();
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", cur, tot);
    lv_label_set_text(reader_page_label, buf);
  }
}

static void check_end_of_book() {
  int current = 0;
  int total = 1;
  std::string title;

  if (is_epub_active && current_epub) {
    current = current_epub->getCurrentPage();
    total = current_epub->getTotalPages();
    title = current_epub->getTitle();
  } else if (!is_epub_active && current_pdf) {
    current = current_pdf->getCurrentPage();
    total = current_pdf->getTotalPages();
    title = current_pdf->getTitle();
  }

  // Strip file extension from title (e.g., "book.epub" -> "book")
  size_t dot_pos = title.rfind('.');
  if (dot_pos != std::string::npos) {
    std::string ext = title.substr(dot_pos);
    for (auto &c : ext) c = tolower(c);
    if (ext == ".epub" || ext == ".pdf") {
      title = title.substr(0, dot_pos);
    }
  }

  if (total > 0 && current == total - 1) {
    // Only show if we haven't already rated it
    bool already_rated = false;
    for (const auto &b : locally_finished_books) {
      if (b.title == title) {
        already_rated = true;
        break;
      }
    }
    if (!already_rated) {
      show_rating_popup(title, total);
    }
  }
}

static void reader_next_cb(lv_event_t *e) {
  if (is_epub_active && current_epub) {
    current_epub->nextPage();
  } else if (!is_epub_active && current_pdf) {
    current_pdf->nextPage();
  }
  update_reader_ui();
  check_end_of_book();
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

static void jump_btn_cb(lv_event_t *e); // Forward declaration

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

static void jump_btn_cb(lv_event_t *e) {
  lv_obj_t *modal = lv_obj_create(lv_scr_act());
  lv_obj_set_size(modal, 400, 300);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0xFFFFFF), 0);

  lv_obj_t *label = lv_label_create(modal);
  lv_label_set_text(label, "Enter page to jump to:");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *ta = lv_textarea_create(modal);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789");
  lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *kb = lv_keyboard_create(modal);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

  struct JumpData {
    lv_obj_t *modal;
    lv_obj_t *ta;
  };
  JumpData *jd = new JumpData{modal, ta};

  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        // LV_EVENT_READY fires when OK/Enter is pressed on the keyboard
        JumpData *data = (JumpData *)lv_event_get_user_data(e);
        const char *txt = lv_textarea_get_text(data->ta);
        if (txt && strlen(txt) > 0) {
          int page = std::stoi(txt) - 1;
          if (is_epub_active && current_epub) {
            current_epub->jumpToPage(page);
          } else if (!is_epub_active && current_pdf) {
            current_pdf->jumpToPage(page);
          }
          update_reader_ui();
          check_end_of_book();
        }
        lv_obj_del(data->modal);
        delete data;
      },
      LV_EVENT_READY, jd);

  lv_obj_t *close_btn = lv_btn_create(modal);
  lv_obj_set_size(close_btn, 40, 40);
  lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_t *close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "X");
  lv_obj_center(close_lbl);
  lv_obj_add_event_cb(
      close_btn,
      [](lv_event_t *e) {
        JumpData *data = (JumpData *)lv_event_get_user_data(e);
        lv_obj_del(data->modal);
        delete data;
      },
      LV_EVENT_CLICKED, jd);
}

static void global_gesture_cb(lv_event_t *e) {
  lv_obj_t *screen = lv_event_get_current_target(e);
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

  if (screen == screen_book_reader) {
    if (dir == LV_DIR_RIGHT) {
      // Swipe to the right -> next page
      reader_next_cb(nullptr);
    } else if (dir == LV_DIR_LEFT) {
      // Swipe to the left -> previous page
      reader_prev_cb(nullptr);
    }
  } else if (screen == screen_library) {
    if (dir == LV_DIR_RIGHT) {
      // Swipe to the right -> go back to main screen
      lv_scr_load(screen_main);
    }
  } else if (screen == screen_ai) {
    if (dir == LV_DIR_RIGHT) {
      // Swipe to the right -> go back to main screen
      lv_scr_load(screen_main);
    }
  }
}

void build_tablet_ui() {
  checkAndClearCache();
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

  // Register gesture callbacks on screens
  lv_obj_add_event_cb(screen_book_reader, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(screen_library, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(screen_ai, global_gesture_cb, LV_EVENT_GESTURE, NULL);

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
  lv_obj_set_style_bg_color(btn_lib, lv_color_black(), 0);
  lv_obj_add_event_cb(btn_lib, load_screen_cb, LV_EVENT_CLICKED,
                      screen_library);
  lv_obj_t *lbl_lib = lv_label_create(btn_lib);
  lv_label_set_text(lbl_lib, "My Library");
  lv_obj_set_style_text_color(lbl_lib, lv_color_white(), 0);
  lv_obj_center(lbl_lib);

  lv_obj_t *btn_ai = lv_btn_create(screen_main);
  lv_obj_align(btn_ai, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_size(btn_ai, 200, 50);
  lv_obj_set_style_bg_color(btn_ai, lv_color_black(), 0);
  lv_obj_add_event_cb(btn_ai, load_screen_cb, LV_EVENT_CLICKED, screen_ai);
  lv_obj_t *lbl_ai = lv_label_create(btn_ai);
  lv_label_set_text(lbl_ai, "AI Assistant");
  lv_obj_set_style_text_color(lbl_ai, lv_color_white(), 0);
  lv_obj_center(lbl_ai);

  // --- LIBRARY SCREEN ---
  lv_obj_t *lib_title = lv_label_create(screen_library);
  lv_label_set_text(lib_title, "Library Books");
  lv_obj_align(lib_title, LV_ALIGN_TOP_MID, 0, 45); // Shifted down for status bar

  lv_obj_t *lib_back = lv_btn_create(screen_library);
  lv_obj_align(lib_back, LV_ALIGN_TOP_LEFT, 20, 40); // Shifted down for status bar
  lv_obj_add_event_cb(lib_back, load_screen_cb, LV_EVENT_CLICKED, screen_main);
  lv_obj_t *lbl_lib_back = lv_label_create(lib_back);
  lv_label_set_text(lbl_lib_back, "Home");
  lv_obj_center(lbl_lib_back);

  lv_obj_t *lib_refresh = lv_btn_create(screen_library);
  lv_obj_align(lib_refresh, LV_ALIGN_TOP_RIGHT, -20, 40); // Shifted down for status bar
  lv_obj_add_event_cb(lib_refresh, refresh_lib_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_lib_refresh = lv_label_create(lib_refresh);
  lv_label_set_text(lbl_lib_refresh, "Refresh");
  lv_obj_center(lbl_lib_refresh);

  book_list = lv_obj_create(screen_library);
  lv_obj_set_size(book_list, 440, 660); // Maximized width and height
  lv_obj_align(book_list, LV_ALIGN_TOP_MID, 0, 100); // Placed cleanly below headers
  lv_obj_set_flex_flow(book_list, LV_FLEX_FLOW_COLUMN);

  build_library_list();

  // --- BOOK READER SCREEN ---
  lv_obj_t *reader_topbar = lv_obj_create(screen_book_reader);
  lv_obj_set_size(reader_topbar, LV_PCT(100), 60);
  lv_obj_align(reader_topbar, LV_ALIGN_TOP_MID, 0, 30); // Shifted down for status bar

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
                  650); // 800 (display) - 30 (status_bar) - 60 (topbar) - 60 (bottombar)
  lv_obj_align(reader_body, LV_ALIGN_TOP_MID, 0, 90); // starts at y=90
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

  lv_obj_t *btn_jump = lv_btn_create(reader_bottombar);
  lv_obj_add_event_cb(btn_jump, jump_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_jump = lv_label_create(btn_jump);
  lv_label_set_text(lbl_jump, "Jump");

  // Page counter label
  reader_page_label = lv_label_create(reader_bottombar);
  lv_label_set_text(reader_page_label, "- / -");

  lv_obj_t *btn_next = lv_btn_create(reader_bottombar);
  lv_obj_add_event_cb(btn_next, reader_next_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_next = lv_label_create(btn_next);
  lv_label_set_text(lbl_next, "Next ->");

  // --- AI ASSISTANT SCREEN ---
  lv_obj_t *ai_back = lv_btn_create(screen_ai);
  lv_obj_align(ai_back, LV_ALIGN_TOP_LEFT, 20, 40); // Shifted down for status bar
  lv_obj_add_event_cb(ai_back, load_screen_cb, LV_EVENT_CLICKED, screen_main);
  lv_obj_t *lbl_ai_back = lv_label_create(ai_back);
  lv_label_set_text(lbl_ai_back, "Home");
  lv_obj_center(lbl_ai_back);

  lv_obj_t *ai_title = lv_label_create(screen_ai);
  lv_label_set_text(ai_title, "AI Assistant");
  lv_obj_align(ai_title, LV_ALIGN_TOP_MID, 0, 40); // Shifted down for status bar

  lv_obj_t * history_cb = lv_checkbox_create(screen_ai);
  lv_checkbox_set_text(history_cb, "Use Reading History");
  lv_obj_align(history_cb, LV_ALIGN_TOP_LEFT, 20, 80); // Placed cleanly above input bar
  lv_obj_add_state(history_cb, LV_STATE_CHECKED); // Default to checked

  // Input bar (fixed below the checkbox)
  lv_obj_t *ai_input_bar = lv_obj_create(screen_ai);
  lv_obj_set_size(ai_input_bar, LV_PCT(100), 50);
  lv_obj_align(ai_input_bar, LV_ALIGN_TOP_MID, 0, 110); // Shifted down
  lv_obj_set_flex_flow(ai_input_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ai_input_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(ai_input_bar, 5, 0);

  ai_content = lv_obj_create(screen_ai);
  lv_obj_set_size(ai_content, 460, 600); // Expanded default size
  lv_obj_align(ai_content, LV_ALIGN_TOP_MID, 0, 170); // Shifted down
  lv_obj_set_flex_flow(ai_content, LV_FLEX_FLOW_COLUMN);

  ai_input_ta = lv_textarea_create(ai_input_bar);
  lv_textarea_set_one_line(ai_input_ta, true);
  lv_obj_set_flex_grow(ai_input_ta, 1);
  lv_textarea_set_placeholder_text(ai_input_ta, "Ask for book recommendations...");
  
  // Disable blinking cursor to prevent infinite e-ink refresh loops!
  lv_obj_set_style_anim_duration(ai_input_ta, 0, LV_PART_CURSOR);
  lv_obj_set_style_opa(ai_input_ta, 0, LV_PART_CURSOR);

  lv_obj_t *ai_send_btn = lv_btn_create(ai_input_bar);
  lv_obj_set_size(ai_send_btn, 110, 40);
  lv_obj_add_event_cb(
      ai_send_btn,
      [](lv_event_t *e) {
        lv_obj_t * cb = (lv_obj_t *)lv_event_get_user_data(e);
        const char *txt = lv_textarea_get_text(ai_input_ta);
        if (txt && strlen(txt) > 0) {
          bool use_history = lv_obj_has_state(cb, LV_STATE_CHECKED);
          request_ai_recommendation(txt, false, use_history); // Semantic Match
          lv_textarea_set_text(ai_input_ta, "");
        }
      },
      LV_EVENT_CLICKED, history_cb);
  lv_obj_t *ai_send_lbl = lv_label_create(ai_send_btn);
  lv_label_set_text(ai_send_lbl, "Recommend");
  lv_obj_center(ai_send_lbl);

  lv_obj_t *ai_search_btn = lv_btn_create(ai_input_bar);
  lv_obj_set_size(ai_search_btn, 80, 40);
  lv_obj_add_event_cb(
      ai_search_btn,
      [](lv_event_t *e) {
        lv_obj_t * cb = (lv_obj_t *)lv_event_get_user_data(e);
        const char *txt = lv_textarea_get_text(ai_input_ta);
        if (txt && strlen(txt) > 0) {
          bool use_history = lv_obj_has_state(cb, LV_STATE_CHECKED);
          request_ai_recommendation(txt, true, use_history); // Exact Match
          lv_textarea_set_text(ai_input_ta, "");
        }
      },
      LV_EVENT_CLICKED, history_cb);
  lv_obj_t *ai_search_lbl = lv_label_create(ai_search_btn);
  lv_label_set_text(ai_search_lbl, "Search");
  lv_obj_center(ai_search_lbl);

  // Create keyboard but keep hidden
  lv_obj_t *ai_kb = lv_keyboard_create(screen_ai);
  lv_keyboard_set_textarea(ai_kb, ai_input_ta);
  lv_obj_add_flag(ai_kb, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_size(ai_kb, LV_PCT(100), 300);
  lv_obj_align(ai_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Focus handlers
  struct AiKbCtx {
    lv_obj_t *kb;
    lv_obj_t *content;
  };
  AiKbCtx *kb_ctx = new AiKbCtx{ai_kb, ai_content};

  lv_obj_add_event_cb(
      ai_input_ta,
      [](lv_event_t *e) {
        AiKbCtx *ctx = (AiKbCtx *)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
        
        if (code == LV_EVENT_FOCUSED) {
          lv_obj_clear_flag(ctx->kb, LV_OBJ_FLAG_HIDDEN);
          // Shrink content to make room for keyboard below it
          lv_obj_set_height(ctx->content, 300);
        } else if (code == LV_EVENT_DEFOCUSED) {
          lv_obj_add_flag(ctx->kb, LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_height(ctx->content, 600);
        } else if (code == LV_EVENT_READY) {
          // Checkmark / Enter key pressed on keyboard
          const char *txt = lv_textarea_get_text(ta);
          if (txt && strlen(txt) > 0) {
            request_ai_recommendation(txt, true);
            lv_textarea_set_text(ta, "");
          }
          // Hide keyboard and drop focus
          lv_obj_add_flag(ctx->kb, LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_height(ctx->content, 600);
          lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        } else if (code == LV_EVENT_CANCEL) {
          // Cancel / Close keyboard
          lv_obj_add_flag(ctx->kb, LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_height(ctx->content, 600);
          lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        }
      },
      LV_EVENT_ALL, kb_ctx);

  // Load the home screen by default
  lv_scr_load(screen_main);
}

// --- NETWORK HTTP REQUEST ---

struct AiResponsePayload {
    bool success;
    std::string raw_json_or_error;
};

static void render_ai_async_cb(void* user_data) {
    AiResponsePayload* payload = (AiResponsePayload*)user_data;
    
    lv_obj_clean(ai_content);
    
    // Explicitly make the scrollbar visible so it's obvious there's more content
    lv_obj_set_scrollbar_mode(ai_content, LV_SCROLLBAR_MODE_ON);
    
    if (payload->success) {
        try {
            json response = json::parse(payload->raw_json_or_error);
            if(response.contains("recommendations") && response["recommendations"].is_array()) {
                for (auto &item : response["recommendations"]) {
                    if (!item.is_object()) continue;

                    std::string title = "Unknown Title";
                    if(item.contains("title") && item["title"].is_string()) title = item["title"].get<std::string>();

                    std::string reasoning = "";
                    if(item.contains("reasoning") && item["reasoning"].is_string()) reasoning = item["reasoning"].get<std::string>();

                    std::string book_id = "unknown";
                    if(item.contains("id")) {
                        if(item["id"].is_string()) book_id = item["id"].get<std::string>();
                        else if(item["id"].is_number()) book_id = std::to_string(item["id"].get<long long>());
                    }

                    // Create item container layout (image + text)
                    lv_obj_t* row = lv_obj_create(ai_content);
                    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_style_pad_all(row, 5, 0);
                    lv_obj_set_style_border_width(row, 1, 0);
                    lv_obj_set_style_border_color(row, lv_color_hex(0xCCCCCC), 0);

                    // Container for text
                    lv_obj_t* txt_cont = lv_obj_create(row);
                    lv_obj_set_size(txt_cont, 300, LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(txt_cont, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_style_pad_all(txt_cont, 0, 0);
                    lv_obj_set_style_border_width(txt_cont, 0, 0);
                    lv_obj_set_style_bg_opa(txt_cont, 0, 0);

                    lv_obj_t* title_lbl = lv_label_create(txt_cont);
                    lv_label_set_text(title_lbl, title.c_str());
                    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(title_lbl, LV_PCT(100));

                    if (!reasoning.empty()) {
                      lv_obj_t* reason_lbl = lv_label_create(txt_cont);
                      lv_label_set_text(reason_lbl, reasoning.c_str());
                      lv_label_set_long_mode(reason_lbl, LV_LABEL_LONG_WRAP);
                      lv_obj_set_width(reason_lbl, LV_PCT(100));
                    }

                    if (book_id != "unknown") {
                        std::string bmp_path = "books/.cache/ai_covers/" + book_id + ".bmp";
                        if (std::filesystem::exists(bmp_path)) {
                            lv_obj_t* img_obj = lv_image_create(row);
                            lv_obj_move_to_index(img_obj, 0); // Put image on the left of text
                            lv_img_set_src(img_obj, ("A:" + bmp_path).c_str());
                        }
                    }
                }
            } else {
                lv_obj_t* err_lbl = lv_label_create(ai_content);
                lv_label_set_text(err_lbl, "No recommendations array found in JSON.");
            }
        } catch (json::exception &e) {
            lv_obj_t* err_lbl = lv_label_create(ai_content);
            lv_label_set_text(err_lbl, ("JSON Parse Error: " + std::string(e.what())).c_str());
        }
    } else {
        lv_obj_t* err_lbl = lv_label_create(ai_content);
        lv_label_set_text(err_lbl, payload->raw_json_or_error.c_str());
    }
    
    delete payload;
}

// --- NETWORK HTTP REQUEST ---
static void request_ai_recommendation(const std::string &user_prompt, bool exact_match, bool use_reviews) {
  lv_scr_load(screen_ai);
  lv_obj_clean(ai_content);
  lv_obj_t *loading_lbl = lv_label_create(ai_content);
  lv_label_set_text(loading_lbl, "Thinking...");

  // Run in background thread to not block LVGL UI
  // Copy shared state under lock before entering background thread
  std::vector<FinishedBook> books_snapshot;
  {
    std::lock_guard<std::mutex> lock(lvgl_mutex);
    books_snapshot = locally_finished_books;
  }

  std::thread([user_prompt, exact_match, use_reviews, books_snapshot]() {
    httplib::Client cli("10.8.0.1", 8000); // Connect to laptop over WireGuard
    cli.set_connection_timeout(5, 0);   // 5 seconds to connect
    cli.set_read_timeout(60, 0);        // 60 seconds max for semantic search and downloading covers

    json payload = {{"user_profile", user_prompt},
                    {"session_history", json::array()},
                    {"rating_pref", 4.0},
                    {"use_reviews", use_reviews},
                    {"exact_match", exact_match},
                    {"language", "eng"}};

    json fin_books = json::array();
    for (const auto &b : books_snapshot) {
      fin_books.push_back({{"title", b.title},
                           {"total_pages", b.total_pages},
                           {"user_rating", b.user_rating}});
    }
    payload["finished_books"] = fin_books;

    std::cout << "Sending AI request..." << std::endl;
    if (auto res = cli.Post("/api/recommend", payload.dump(), "application/json")) {
      if (res->status == 200) {
        try {
            json response = json::parse(res->body);
            httplib::Client proxy_cli("10.8.0.1", 8000); // separate client for proxy calls
            
            if (response.contains("recommendations") && response["recommendations"].is_array()) {
                for (auto &item : response["recommendations"]) {
                    if (!item.is_object()) continue;

                    std::string img_url = "";
                    if (item.contains("image_url") && item["image_url"].is_string()) {
                        img_url = item["image_url"].get<std::string>();
                    }

                    std::string book_id = "unknown";
                    if (item.contains("id")) {
                        if (item["id"].is_string()) book_id = item["id"].get<std::string>();
                        else if (item["id"].is_number()) book_id = std::to_string(item["id"].get<long long>());
                    }
                    
                    if (!img_url.empty() && book_id != "unknown") {
                       std::string enc = "";
                       for(char c: img_url) {
                           if(isalnum((unsigned char)c) || c=='-' || c=='_' || c=='.' || c=='~') enc += c;
                           else { char buf[5]; snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c); enc += buf; }
                       }
                       std::string proxy_path = "/api/image?url=" + enc;
                   
                    if(auto img_res = proxy_cli.Get(proxy_path.c_str())) {
                        if(img_res->status == 200) {
                            int w, h, comp;
                            unsigned char* img_data = stbi_load_from_memory(
                                (const unsigned char*)img_res->body.c_str(), 
                                img_res->body.size(), &w, &h, &comp, 3);
                            
                            if(img_data) {
                                std::filesystem::create_directories("books/.cache/ai_covers");
                                std::string bmp_path = "books/.cache/ai_covers/" + book_id + ".bmp";
                                write_bmp_cover(bmp_path.c_str(), w, h, 3, img_data);
                                stbi_image_free(img_data);
                            }
                        }
                    }
                 } // closes if (!img_url.empty() && book_id != "unknown")
                } // closes for loop
            } // closes if (response.contains("recommendations"))
        } catch(...) {}

        // Notify main thread
        lvgl_mutex.lock();
        lv_async_call(render_ai_async_cb, new AiResponsePayload{true, res->body});
        lvgl_mutex.unlock();
      } else {
        lvgl_mutex.lock();
        lv_async_call(render_ai_async_cb, new AiResponsePayload{false, "HTTP Error: " + std::to_string(res->status)});
        lvgl_mutex.unlock();
      }
    } else {
      lvgl_mutex.lock();
      lv_async_call(render_ai_async_cb, new AiResponsePayload{false, "Connection failed. Server running? Err: " + httplib::to_string(res.error())});
      lvgl_mutex.unlock();
    }
  }).detach();
}

// --- END E-INK APP UI PLUMBING ---
