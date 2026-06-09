#pragma once
#include "lvgl/lvgl.h"
#include <string>
#include <vector>

// Forward declare to allow the OS wrappers to initialize the UI
void build_tablet_ui();
void show_wifi_menu();
void update_status_bar();

// Reading tracker
struct FinishedBook {
  std::string title;
  int total_pages;
  int user_rating;
};

extern std::vector<FinishedBook> locally_finished_books;

struct TypographySettings {
    const lv_font_t* font;
    int chars_per_line;
    int line_height;
    std::string name;
};

extern std::vector<TypographySettings> g_font_options;
extern int g_current_font_index;

