#ifdef _WIN32
// WIN32_LEAN_AND_MEAN must be defined BEFORE Windows.h to prevent winsock.h
// from being pulled in (which conflicts with winsock2.h used by httplib).
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#endif
#ifndef _WIN32
#include <unistd.h>
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
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <algorithm>

// Include stb_image for cover image decoding (IMPLEMENTATION is in epubHandler.cpp)
#include "../LvglPlatform/lvgl/src/libs/gltf/stb_image/stb_image.h"

#include <mutex>

using json = nlohmann::json;

std::recursive_mutex lvgl_mutex;

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

struct ReadingState {
    std::string last_book_path;
    std::map<std::string, int> book_pages;
};
ReadingState g_reading_state;

static void load_reading_state() {
    std::string path = "books/.cache/reading_state.json";
    if (std::filesystem::exists(path)) {
        try {
            std::ifstream f(path);
            json j;
            f >> j;
            if (j.contains("last_book_path")) g_reading_state.last_book_path = j["last_book_path"];
            if (j.contains("book_pages")) {
                for (auto& [key, val] : j["book_pages"].items()) {
                    g_reading_state.book_pages[key] = val;
                }
            }
        } catch (...) {}
    }
}

static void save_reading_state() {
    std::filesystem::create_directories("books/.cache");
    std::string path = "books/.cache/reading_state.json";
    try {
        json j;
        j["last_book_path"] = g_reading_state.last_book_path;
        j["book_pages"] = g_reading_state.book_pages;
        std::ofstream f(path);
        f << j.dump(4);
    } catch (...) {}
}

struct BookMetadata {
    std::string title;
    std::string author;
    uint64_t mtime;
};
std::map<std::string, BookMetadata> g_book_metadata;

static void load_metadata_cache() {
    std::string path = "books/.cache/metadata.json";
    if (std::filesystem::exists(path)) {
        try {
            std::ifstream f(path);
            json j;
            f >> j;
            for (auto& [key, val] : j.items()) {
                g_book_metadata[key] = {
                    val.value("title", ""),
                    val.value("author", ""),
                    val.value("mtime", 0ULL)
                };
            }
        } catch (...) {}
    }
}

static void save_metadata_cache() {
    std::filesystem::create_directories("books/.cache");
    std::string path = "books/.cache/metadata.json";
    try {
        json j;
        for (const auto& [key, val] : g_book_metadata) {
            j[key] = {
                {"title", val.title},
                {"author", val.author},
                {"mtime", val.mtime}
            };
        }
        std::ofstream f(path);
        f << j.dump(4);
    } catch (...) {}
}

std::vector<FinishedBook> locally_finished_books;

enum SortMode {
    SORT_BY_TITLE,
    SORT_BY_AUTHOR
};

// --- STYLED BUTTON HELPER ---
static lv_obj_t * create_styled_btn(lv_obj_t * parent) {
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_anim_duration(btn, 0, 0); // Disable state transition animations
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    return btn;
}


static lv_obj_t * create_white_container(lv_obj_t * parent) {
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF); // Disable scrollbar fade animations
    return cont;
}

// --- PRS-505 MENU ROW HELPER ---
static lv_obj_t* create_menu_row(lv_obj_t* parent, const char* icon, const char* title, const char* subtitle) {
  lv_obj_t* row = lv_btn_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 65); // Full width
  lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(row, lv_color_hex(0x000000), 0); // Force black text
  
  // Only bottom border to act as a separator
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 2, 0); 
  lv_obj_set_style_border_color(row, lv_color_hex(0x000000), 0);
  lv_obj_set_style_radius(row, 0, 0);
  
  // Remove padding so elements can align properly
  lv_obj_set_style_pad_all(row, 10, 0);

  // Left Icon
  lv_obj_t* lbl_icon = lv_label_create(row);
  lv_label_set_text(lbl_icon, icon);
  lv_obj_align(lbl_icon, LV_ALIGN_LEFT_MID, 10, 0);

  // Title
  lv_obj_t* lbl_title = lv_label_create(row);
  lv_label_set_text(lbl_title, title);
  lv_obj_align_to(lbl_title, lbl_icon, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

  // Subtitle
  if (subtitle && strlen(subtitle) > 0) {
    lv_obj_t* lbl_sub = lv_label_create(row);
    lv_label_set_text(lbl_sub, subtitle);
    lv_obj_align(lbl_sub, LV_ALIGN_RIGHT_MID, -10, 0);
  }

  // Disable pressed animations to prevent e-ink ghosting/double refresh
  lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(row, 0, LV_STATE_PRESSED);
  lv_obj_set_style_anim_duration(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_PRESS_LOCK);

  return row;
}

// --- CACHE MANAGEMENT ---
static inline void trim_string(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

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

static void build_library_list(SortMode mode = SORT_BY_TITLE); // Forward declaration
static void request_ai_recommendation(const std::string &user_prompt, bool exact_match = false, bool use_reviews = true);

static void show_rating_popup(const std::string &book_title, int total_pages) {
  lv_obj_t *modal = create_white_container(lv_scr_act());
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

  lv_obj_t *btn_container = create_white_container(modal);
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
    lv_obj_t *btn = create_styled_btn(btn_container);
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

  lv_obj_t *close_btn = create_styled_btn(modal);
  lv_obj_t *close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "No Thanks");
  lv_obj_add_event_cb(
      close_btn,
      [](lv_event_t *e) { lv_obj_del((lv_obj_t *)lv_event_get_user_data(e)); },
      LV_EVENT_CLICKED, modal);
}

static void update_reader_ui() {
  lv_obj_invalidate(screen_book_reader); // Force single unified redraw for page and counter
  std::string text;
  if (is_epub_active && current_epub) {
    std::string display_title = current_epub->getTitle();
    if (g_book_metadata.count(g_reading_state.last_book_path)) {
        display_title = g_book_metadata[g_reading_state.last_book_path].title;
    }
    lv_label_set_text(reader_title_label, display_title.c_str());
    text = current_epub->getContent();
  } else if (!is_epub_active && current_pdf) {
    std::string display_title = current_pdf->getTitle();
    if (g_book_metadata.count(g_reading_state.last_book_path)) {
        display_title = g_book_metadata[g_reading_state.last_book_path].title;
    }
    lv_label_set_text(reader_title_label, display_title.c_str());
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
    g_reading_state.book_pages[g_reading_state.last_book_path] = current_epub->getCurrentPage();
  } else if (!is_epub_active && current_pdf) {
    current_pdf->nextPage();
    g_reading_state.book_pages[g_reading_state.last_book_path] = current_pdf->getCurrentPage();
  }
  save_reading_state();
  update_reader_ui();
  check_end_of_book();
}

static void reader_prev_cb(lv_event_t *e) {
  if (is_epub_active && current_epub) {
    current_epub->prevPage();
    g_reading_state.book_pages[g_reading_state.last_book_path] = current_epub->getCurrentPage();
  } else if (!is_epub_active && current_pdf) {
    current_pdf->prevPage();
    g_reading_state.book_pages[g_reading_state.last_book_path] = current_pdf->getCurrentPage();
  }
  save_reading_state();
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

  // Track state
  g_reading_state.last_book_path = std::string(filepath);
  if (g_reading_state.book_pages.count(g_reading_state.last_book_path)) {
      int saved_page = g_reading_state.book_pages[g_reading_state.last_book_path];
      if (is_epub_active && current_epub) current_epub->jumpToPage(saved_page);
      else if (!is_epub_active && current_pdf) current_pdf->jumpToPage(saved_page);
  } else {
      g_reading_state.book_pages[g_reading_state.last_book_path] = 0;
  }
  save_reading_state();

  update_reader_ui();
  lv_scr_load(screen_book_reader);
}

static SortMode g_current_sort = SORT_BY_TITLE;

static void refresh_lib_cb(lv_event_t *e) { build_library_list(g_current_sort); }

static void build_library_list(SortMode mode) {
  g_current_sort = mode;
  if (book_list == NULL)
    return;

  // Clear existing children
  lv_obj_clean(book_list);
  book_filepaths.clear();

  if (!std::filesystem::exists("books")) {
    std::filesystem::create_directory("books");
  }

  static bool cache_loaded = false;
  if (!cache_loaded) {
      load_metadata_cache();
      cache_loaded = true;
  }
  
  bool cache_changed = false;

  std::vector<std::string> temp_files;
  std::vector<std::pair<std::string, std::string>> pending_renames;

  for (const auto &entry : std::filesystem::directory_iterator("books")) {
    if (entry.is_regular_file()) {
      std::string ext = entry.path().extension().string();
      for (auto &c : ext) c = tolower(c);
      if (ext == ".epub" || ext == ".pdf") {
        std::string p = entry.path().string();
        temp_files.push_back(p);
        
        auto mtime = std::chrono::duration_cast<std::chrono::seconds>(entry.last_write_time().time_since_epoch()).count();
        if (g_book_metadata.find(p) == g_book_metadata.end() || g_book_metadata[p].mtime != (uint64_t)mtime) {
            std::string t, a;
            bool ok = false;
            if (ext == ".epub") ok = EpubHandler::getMetadata(p, t, a);
            else ok = PdfHandler::getMetadata(p, t, a);
            
            trim_string(t);
            trim_string(a);
            
            if (t.empty()) {
                std::string fn = entry.path().filename().string();
                size_t dot_pos = fn.rfind('.');
                if (dot_pos != std::string::npos) fn = fn.substr(0, dot_pos);
                t = fn;
            }
            if (a.empty()) a = "Unknown";
            
            g_book_metadata[p] = {t, a, (uint64_t)mtime};
            cache_changed = true;

            std::string sanitized_t = t;
            std::string sanitized_a = a;
            const char* invalid_chars = "\\/:*?\"<>|";
            for (char& c : sanitized_t) { if (strchr(invalid_chars, c)) c = '_'; }
            for (char& c : sanitized_a) { if (strchr(invalid_chars, c)) c = '_'; }
            
            std::string desired_fn = sanitized_t + " - " + sanitized_a + ext;
            std::string new_path = entry.path().parent_path().string() + "/" + desired_fn;
            if (p != new_path) pending_renames.push_back({p, new_path});
        }
      }
    }
  }

  for (auto& rename_pair : pending_renames) {
      std::string old_p = rename_pair.first;
      std::string new_path = rename_pair.second;
      
      int counter = 1;
      std::string ext = std::filesystem::path(old_p).extension().string();
      std::string base_name = std::filesystem::path(new_path).stem().string();
      std::string parent_dir = std::filesystem::path(old_p).parent_path().string();
      
      while (std::filesystem::exists(new_path) && old_p != new_path) {
          new_path = parent_dir + "/" + base_name + " (" + std::to_string(counter++) + ")" + ext;
      }
      
      if (old_p != new_path) {
          try {
              std::filesystem::rename(old_p, new_path);
              
              for (auto& f : temp_files) {
                  if (f == old_p) { f = new_path; break; }
              }
              
              if (g_reading_state.last_book_path == old_p) g_reading_state.last_book_path = new_path;
              if (g_reading_state.book_pages.count(old_p)) {
                  g_reading_state.book_pages[new_path] = g_reading_state.book_pages[old_p];
                  g_reading_state.book_pages.erase(old_p);
              }
              
              g_book_metadata[new_path] = g_book_metadata[old_p];
              g_book_metadata.erase(old_p);
              
              cache_changed = true;
          } catch(...) {}
      }
  }

  if (cache_changed) {
      save_metadata_cache();
      save_reading_state();
  }

  if (mode == SORT_BY_TITLE) {
      std::sort(temp_files.begin(), temp_files.end(), [](const std::string& a, const std::string& b) {
          return g_book_metadata.at(a).title < g_book_metadata.at(b).title;
      });
  } else if (mode == SORT_BY_AUTHOR) {
      std::sort(temp_files.begin(), temp_files.end(), [](const std::string& a, const std::string& b) {
          if (g_book_metadata.at(a).author == g_book_metadata.at(b).author)
              return g_book_metadata.at(a).title < g_book_metadata.at(b).title;
          return g_book_metadata.at(a).author < g_book_metadata.at(b).author;
      });
  }

  for (const auto &path_str : temp_files) {
      book_filepaths.push_back(path_str);
      
      lv_obj_t *row = create_white_container(book_list);
      lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_all(row, 0, 0);

      lv_obj_t *btn = create_styled_btn(row);
      lv_obj_set_size(btn, 320, LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_add_event_cb(btn, book_clicked_cb, LV_EVENT_CLICKED, (void *)book_filepaths.back().c_str());
      
      lv_obj_t *lbl_title = lv_label_create(btn);
      lv_label_set_text(lbl_title, g_book_metadata[path_str].title.c_str());
      lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_CLIP);
      lv_obj_set_width(lbl_title, 280);

      lv_obj_t *lbl_author = lv_label_create(btn);
      lv_label_set_text(lbl_author, g_book_metadata[path_str].author.c_str());
      lv_label_set_long_mode(lbl_author, LV_LABEL_LONG_CLIP);
      lv_obj_set_width(lbl_author, 280);
      lv_obj_set_style_text_color(lbl_author, lv_color_hex(0x666666), 0); // Gray text for author

      lv_obj_t *rate_btn = create_styled_btn(row);
      lv_obj_set_size(rate_btn, 80, LV_SIZE_CONTENT);
      lv_obj_t *rate_lbl = lv_label_create(rate_btn);
      lv_label_set_text(rate_lbl, "Rate");
      lv_obj_center(rate_lbl);
      
      lv_obj_add_event_cb(rate_btn, [](lv_event_t* e) {
          const char* p = (const char*)lv_event_get_user_data(e);
          std::string title = g_book_metadata[p].title;
          show_rating_popup(title, 1); 
      }, LV_EVENT_CLICKED, (void *)book_filepaths.back().c_str());
  }
}

static void jump_btn_cb(lv_event_t *e) {
  lv_obj_t *modal = create_white_container(lv_scr_act());
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
  lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR);
  lv_obj_set_style_opa(ta, 0, LV_PART_CURSOR);

  lv_obj_t *kb = lv_keyboard_create(modal);
  lv_keyboard_set_popovers(kb, false);
  lv_obj_set_style_anim_duration(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(kb, lv_color_hex(0x000000), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(kb, 0, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(kb, 0, LV_PART_ITEMS | LV_STATE_PRESSED);
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
            g_reading_state.book_pages[g_reading_state.last_book_path] = current_epub->getCurrentPage();
          } else if (!is_epub_active && current_pdf) {
            current_pdf->jumpToPage(page);
            g_reading_state.book_pages[g_reading_state.last_book_path] = current_pdf->getCurrentPage();
          }
          save_reading_state();
          update_reader_ui();
          check_end_of_book();
        }
        lv_obj_del(data->modal);
        delete data;
      },
      LV_EVENT_READY, jd);

  lv_obj_t *close_btn = create_styled_btn(modal);
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
  lv_obj_t *target = (lv_obj_t *)lv_event_get_current_target(e);
  lv_obj_t *screen = lv_obj_get_screen(target);
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
  load_reading_state();
  checkAndClearCache();
  screen_main = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_main, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scrollbar_mode(screen_main, LV_SCROLLBAR_MODE_OFF);

  screen_library = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_library, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scrollbar_mode(screen_library, LV_SCROLLBAR_MODE_OFF);

  screen_book_reader = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_book_reader, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scrollbar_mode(screen_book_reader, LV_SCROLLBAR_MODE_OFF);

  screen_ai = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_ai, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scrollbar_mode(screen_ai, LV_SCROLLBAR_MODE_OFF);

  // Apply white background to all screens
  lv_obj_set_style_bg_color(screen_main, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_main, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen_main, lv_color_black(), LV_PART_MAIN);
  
  lv_obj_set_style_bg_color(screen_library, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_library, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen_library, lv_color_black(), LV_PART_MAIN);
  
  lv_obj_set_style_bg_color(screen_book_reader, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_book_reader, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen_book_reader, lv_color_black(), LV_PART_MAIN);
  
  lv_obj_set_style_bg_color(screen_ai, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_ai, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen_ai, lv_color_black(), LV_PART_MAIN);

  // Register gesture callbacks on screens and make them clickable
  lv_obj_add_flag(screen_book_reader, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(screen_book_reader, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(screen_book_reader, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  
  lv_obj_add_flag(screen_library, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(screen_library, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(screen_library, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  
  lv_obj_add_flag(screen_ai, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(screen_ai, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(screen_ai, global_gesture_cb, LV_EVENT_GESTURE, NULL);

  // Toggle bottom bar when clicking anywhere on the background of the read
  // screen
  lv_obj_add_event_cb(screen_book_reader, toggle_bottombar_cb, LV_EVENT_CLICKED,
                      NULL);

  // --- MAIN SCREEN ---
  lv_obj_set_flex_flow(screen_main, LV_FLEX_FLOW_COLUMN);
  // Clear pad but leave top pad for status bar so header isn't eaten up
  lv_obj_set_style_pad_all(screen_main, 0, 0);
  lv_obj_set_style_pad_top(screen_main, 30, 0);
  lv_obj_set_style_pad_row(screen_main, 0, 0);

  // Black Header
  lv_obj_t* header = lv_obj_create(screen_main);
  lv_obj_set_size(header, LV_PCT(100), 70);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 10, 0); // Inner padding for text

  lv_obj_t* header_title = lv_label_create(header);
  lv_label_set_text(header_title, "Reader");
  lv_obj_set_style_text_color(header_title, lv_color_hex(0xFFFFFF), 0);
  // Use a large built-in font if available (montserrat_28/32 is typical)
  lv_obj_set_style_text_font(header_title, &lv_font_montserrat_24, 0);
  lv_obj_align(header_title, LV_ALIGN_LEFT_MID, 10, 0);

  // Menu List Container
  lv_obj_t* list_cont = lv_obj_create(screen_main);
  lv_obj_set_flex_grow(list_cont, 1); // Take remaining height
  lv_obj_set_width(list_cont, LV_PCT(100));
  lv_obj_set_style_bg_color(list_cont, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(list_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list_cont, 0, 0);
  lv_obj_set_style_radius(list_cont, 0, 0);
  lv_obj_set_style_pad_all(list_cont, 0, 0);
  lv_obj_set_style_pad_row(list_cont, 0, 0);
  lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);

  // Row 1: Continue Reading
  std::string continue_subtitle = "No book";
  if (!g_reading_state.last_book_path.empty()) {
      if (g_book_metadata.count(g_reading_state.last_book_path)) {
          continue_subtitle = g_book_metadata[g_reading_state.last_book_path].title;
      } else {
          continue_subtitle = std::filesystem::path(g_reading_state.last_book_path).filename().string();
      }
  }
  lv_obj_t* row_continue = create_menu_row(list_cont, LV_SYMBOL_PLAY, "Continue Reading", continue_subtitle.c_str());
  if (g_reading_state.last_book_path.empty()) {
      lv_obj_add_event_cb(row_continue, load_screen_cb, LV_EVENT_CLICKED, screen_library);
  } else {
      lv_obj_add_event_cb(row_continue, book_clicked_cb, LV_EVENT_CLICKED, (void *)g_reading_state.last_book_path.c_str());
  }

  // Row 2: Books by Title
  lv_obj_t* row_title = create_menu_row(list_cont, LV_SYMBOL_DIRECTORY, "Books by Title", "Library");
  lv_obj_add_event_cb(row_title, [](lv_event_t* e) {
      build_library_list(SORT_BY_TITLE);
      lv_scr_load(screen_library);
  }, LV_EVENT_CLICKED, NULL);

  // Row 3: Books by Author
  lv_obj_t* row_author = create_menu_row(list_cont, LV_SYMBOL_IMAGE, "Books by Author", "Library");
  lv_obj_add_event_cb(row_author, [](lv_event_t* e) {
      build_library_list(SORT_BY_AUTHOR);
      lv_scr_load(screen_library);
  }, LV_EVENT_CLICKED, NULL);

  // Row 4: AI Assistant
  lv_obj_t* row_ai = create_menu_row(list_cont, LV_SYMBOL_EDIT, "AI Assistant", "Active");
  lv_obj_add_event_cb(row_ai, load_screen_cb, LV_EVENT_CLICKED, screen_ai);

  // Row 5: Settings / Dashboard URL
  char hostname[256];
  std::string dash_url = "radxa-zero.local";
  if (gethostname(hostname, sizeof(hostname)) == 0) {
      dash_url = std::string(hostname) + ".local:8080";
  }
  lv_obj_t* row_dash = create_menu_row(list_cont, LV_SYMBOL_SETTINGS, "Dashboard", dash_url.c_str());
  lv_obj_remove_flag(row_dash, LV_OBJ_FLAG_CLICKABLE); // Just info

  // --- LIBRARY SCREEN ---
  lv_obj_t *lib_title = lv_label_create(screen_library);
  lv_label_set_text(lib_title, "Library Books");
  lv_obj_align(lib_title, LV_ALIGN_TOP_MID, 0, 45); // Shifted down for status bar

  lv_obj_t *lib_back = create_styled_btn(screen_library);
  lv_obj_align(lib_back, LV_ALIGN_BOTTOM_LEFT, 20, -40);
  lv_obj_add_event_cb(lib_back, load_screen_cb, LV_EVENT_CLICKED, screen_main);
  lv_obj_t *lbl_lib_back = lv_label_create(lib_back);
  lv_label_set_text(lbl_lib_back, LV_SYMBOL_HOME);
    lv_obj_center(lbl_lib_back);

  lv_obj_t *lib_refresh = create_styled_btn(screen_library);
  lv_obj_align(lib_refresh, LV_ALIGN_TOP_RIGHT, -20, 40); // Shifted down for status bar
    lv_obj_add_event_cb(lib_refresh, refresh_lib_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_lib_refresh = lv_label_create(lib_refresh);
  lv_label_set_text(lbl_lib_refresh, "Refresh");
    lv_obj_center(lbl_lib_refresh);

  book_list = create_white_container(screen_library);
  lv_obj_set_size(book_list, 440, 660); // Maximized width and height
  lv_obj_align(book_list, LV_ALIGN_TOP_MID, 0, 100); // Placed cleanly below headers
  lv_obj_set_scroll_dir(book_list, LV_DIR_VER); // Only scroll vertically
  lv_obj_add_event_cb(book_list, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_flex_flow(book_list, LV_FLEX_FLOW_COLUMN);

  build_library_list(SORT_BY_TITLE);

  // --- BOOK READER SCREEN ---
  lv_obj_t *reader_topbar = create_white_container(screen_book_reader);
  lv_obj_set_size(reader_topbar, LV_PCT(100), 60);
  lv_obj_align(reader_topbar, LV_ALIGN_TOP_MID, 0, 30); // Shifted down for status bar

  reader_title_label = lv_label_create(reader_topbar);
  lv_label_set_text(reader_title_label, "Reading Book...");
  lv_obj_align(reader_title_label, LV_ALIGN_CENTER, 0, 0);

  // Create a scrollable container for the body of the reader
  lv_obj_t *reader_body = create_white_container(screen_book_reader);
  lv_obj_set_scroll_dir(reader_body, LV_DIR_VER); // Restrict to vertical scroll if any
  lv_obj_add_event_cb(reader_body, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_size(reader_body, 480, 650); // 800 (display) - 30 (status_bar) - 60 (topbar) - 60 (bottombar)
  lv_obj_align(reader_body, LV_ALIGN_TOP_MID, 0, 90); // starts at y=90
  lv_obj_set_style_bg_color(reader_body, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(reader_body, LV_OPA_COVER, 0);
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
  reader_bottombar = create_white_container(screen_book_reader);
  lv_obj_set_size(reader_bottombar, LV_PCT(100), 60);
  lv_obj_align(reader_bottombar, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_set_flex_flow(reader_bottombar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(reader_bottombar, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *reader_back = create_styled_btn(reader_bottombar);
  lv_obj_add_event_cb(reader_back, load_screen_cb, LV_EVENT_CLICKED,
                      screen_library);
  lv_obj_t *lbl_reader_back = lv_label_create(reader_back);
  lv_label_set_text(lbl_reader_back, LV_SYMBOL_HOME);
  lv_obj_center(lbl_reader_back);

  lv_obj_t *btn_prev = create_styled_btn(reader_bottombar);
  lv_obj_add_event_cb(btn_prev, reader_prev_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_prev = lv_label_create(btn_prev);
  lv_label_set_text(lbl_prev, "<- Prev");

  lv_obj_t *btn_jump = create_styled_btn(reader_bottombar);
  lv_obj_add_event_cb(btn_jump, jump_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_jump = lv_label_create(btn_jump);
  lv_label_set_text(lbl_jump, "Jump");

  // Page counter label
  reader_page_label = lv_label_create(reader_bottombar);
  lv_label_set_text(reader_page_label, "- / -");

  lv_obj_t *btn_next = create_styled_btn(reader_bottombar);
  lv_obj_add_event_cb(btn_next, reader_next_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_next = lv_label_create(btn_next);
  lv_label_set_text(lbl_next, "Next ->");

  // --- AI ASSISTANT SCREEN ---
  lv_obj_t *ai_back = create_styled_btn(screen_ai);
  lv_obj_align(ai_back, LV_ALIGN_BOTTOM_LEFT, 20, -40);
  lv_obj_add_event_cb(ai_back, load_screen_cb, LV_EVENT_CLICKED, screen_main);
  lv_obj_t *lbl_ai_back = lv_label_create(ai_back);
  lv_label_set_text(lbl_ai_back, LV_SYMBOL_HOME);
  lv_obj_center(lbl_ai_back);

  lv_obj_t *ai_title = lv_label_create(screen_ai);
  lv_label_set_text(ai_title, "AI Assistant");
  lv_obj_align(ai_title, LV_ALIGN_TOP_MID, 0, 40); // Shifted down for status bar

  lv_obj_t * history_cb = lv_checkbox_create(screen_ai);
  lv_checkbox_set_text(history_cb, "Use Reading History");
  lv_obj_align(history_cb, LV_ALIGN_TOP_LEFT, 20, 80); // Placed cleanly above input bar
  lv_obj_add_state(history_cb, LV_STATE_CHECKED); // Default to checked

  lv_obj_t * clear_history_btn = create_styled_btn(screen_ai);
  lv_obj_set_size(clear_history_btn, 120, 35);
  lv_obj_align_to(clear_history_btn, history_cb, LV_ALIGN_OUT_RIGHT_MID, 40, 0);
  lv_obj_t * clear_lbl = lv_label_create(clear_history_btn);
  lv_label_set_text(clear_lbl, "Clear History");
  lv_obj_center(clear_lbl);
  lv_obj_add_event_cb(clear_history_btn, [](lv_event_t *e) {
      locally_finished_books.clear();
      if (ai_content) {
          lv_obj_clean(ai_content);
          lv_obj_t *lbl = lv_label_create(ai_content);
          lv_label_set_text(lbl, "Reading history cleared!");
      }
  }, LV_EVENT_CLICKED, NULL);

  // Input bar (fixed below the checkbox)
  lv_obj_t *ai_input_bar = create_white_container(screen_ai);
  lv_obj_set_size(ai_input_bar, LV_PCT(100), 50);
  lv_obj_align(ai_input_bar, LV_ALIGN_TOP_MID, 0, 110); // Shifted down
  lv_obj_set_flex_flow(ai_input_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ai_input_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(ai_input_bar, 5, 0);

  ai_content = create_white_container(screen_ai);
  lv_obj_set_scroll_dir(ai_content, LV_DIR_VER);
  lv_obj_add_event_cb(ai_content, global_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_size(ai_content, 460, 540); // Shrunk to leave room for bottombar
  lv_obj_align(ai_content, LV_ALIGN_TOP_MID, 0, 170); // Shifted down
  lv_obj_set_flex_flow(ai_content, LV_FLEX_FLOW_COLUMN);

  ai_input_ta = lv_textarea_create(ai_input_bar);
  lv_textarea_set_one_line(ai_input_ta, true);
  lv_obj_set_flex_grow(ai_input_ta, 1);
  lv_textarea_set_placeholder_text(ai_input_ta, "Ask for book recommendations...");
  
  // Add visible borders so it doesn't blend into the background
  lv_obj_set_style_border_width(ai_input_ta, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(ai_input_ta, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ai_input_ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_color(ai_input_ta, lv_color_hex(0x000000), LV_PART_MAIN);

  // Disable blinking cursor to prevent infinite e-ink refresh loops!
  lv_obj_set_style_anim_duration(ai_input_ta, 0, LV_PART_CURSOR);
  lv_obj_set_style_opa(ai_input_ta, 0, LV_PART_CURSOR);

  lv_obj_t *ai_send_btn = create_styled_btn(ai_input_bar);
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

  lv_obj_t *ai_search_btn = create_styled_btn(ai_input_bar);
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

  // AI Bottom Pagination Bar
  lv_obj_t *ai_bottombar = create_white_container(screen_ai);
  lv_obj_set_size(ai_bottombar, LV_PCT(100), 60);
  lv_obj_align(ai_bottombar, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(ai_bottombar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ai_bottombar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ai_btn_up = create_styled_btn(ai_bottombar);
  lv_obj_t *ai_lbl_up = lv_label_create(ai_btn_up);
  lv_label_set_text(ai_lbl_up, "Page Up");
  lv_obj_add_event_cb(ai_btn_up, [](lv_event_t *e){
      lv_coord_t y = lv_obj_get_scroll_y(ai_content);
      lv_coord_t new_y = std::max((lv_coord_t)0, (lv_coord_t)(y - 500)); // Scroll slightly less than full height for overlap
      lv_obj_scroll_to_y(ai_content, new_y, LV_ANIM_OFF);
      lv_obj_invalidate(screen_ai);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *ai_btn_down = create_styled_btn(ai_bottombar);
  lv_obj_t *ai_lbl_down = lv_label_create(ai_btn_down);
  lv_label_set_text(ai_lbl_down, "Page Down");
  lv_obj_add_event_cb(ai_btn_down, [](lv_event_t *e){
      lv_coord_t y = lv_obj_get_scroll_y(ai_content);
      lv_obj_scroll_to_y(ai_content, y + 500, LV_ANIM_OFF); // Scroll slightly less than full height for overlap
      lv_obj_invalidate(screen_ai);
  }, LV_EVENT_CLICKED, NULL);

  // Create keyboard but keep hidden
  lv_obj_t *ai_kb = lv_keyboard_create(screen_ai);
  lv_keyboard_set_popovers(ai_kb, false);
  lv_obj_set_style_anim_duration(ai_kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(ai_kb, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(ai_kb, lv_color_hex(0x000000), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(ai_kb, 0, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(ai_kb, 0, LV_PART_ITEMS | LV_STATE_PRESSED);
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
                    lv_obj_t* row = create_white_container(ai_content);
                    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_style_pad_all(row, 5, 0);
                    lv_obj_set_style_border_width(row, 1, 0);
                    lv_obj_set_style_border_color(row, lv_color_hex(0xCCCCCC), 0);

                    // Container for text
                    lv_obj_t* txt_cont = create_white_container(row);
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
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    books_snapshot = locally_finished_books;
  }

  std::thread([user_prompt, exact_match, use_reviews, books_snapshot]() {
    std::string target_ip = "10.8.0.1";
    std::ifstream ip_file("llm_ip.txt");
    if (ip_file.is_open()) {
        std::string ip;
        if (std::getline(ip_file, ip)) {
            // Strip any whitespace
            ip.erase(std::remove_if(ip.begin(), ip.end(), ::isspace), ip.end());
            if (!ip.empty()) {
                target_ip = ip;
            }
        }
        ip_file.close();
    }
    
    std::cout << "[AI] Read Dashboard IP: " << target_ip << std::endl;
    std::cout << "[AI] Connecting to LLM API at: " << target_ip << ":8000" << std::endl;
    httplib::Client cli(target_ip, 8000); 
    cli.set_connection_timeout(5, 0);   
    cli.set_read_timeout(60, 0);        

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

    std::string dump = payload.dump();
    std::cout << "[AI] Sending POST /api/recommend" << std::endl;
    std::cout << "[AI] Payload: " << dump << std::endl;
    if (auto res = cli.Post("/api/recommend", dump, "application/json")) {
      if (res->status == 200) {
        try {
            json response = json::parse(res->body);
            httplib::Client proxy_cli(target_ip, 8000); // separate client for proxy calls
            
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
