#pragma once
#include "lvgl/lvgl.h"
#include <string>
#include <vector>

// Forward declare to allow the OS wrappers to initialize the UI
void build_tablet_ui();

// Reading tracker
struct FinishedBook {
  std::string title;
  int total_pages;
  int user_rating;
};

extern std::vector<FinishedBook> locally_finished_books;
