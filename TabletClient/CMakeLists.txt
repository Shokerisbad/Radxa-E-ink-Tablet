cmake_minimum_required(VERSION 3.10)
project(RadxaTabletClient C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)

# Include LVGL
# The LVGL source is inside the Visual Studio port folder
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lv_port_pc_visual_studio/LvglPlatform/lvgl)

# Tell LVGL exactly where our configuration file is located
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_port_pc_visual_studio/LvglWindowsSimulator/lv_conf.h CACHE PATH "Path to lv_conf.h")

if(EXISTS ${LVGL_DIR}/CMakeLists.txt)
    add_subdirectory(${LVGL_DIR})
else()
    message(WARNING "LVGL CMakeLists.txt not found at ${LVGL_DIR}. You may need to adjust the path.")
endif()

# We need the lv_conf.h directory in the include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lv_port_pc_visual_studio/LvglWindowsSimulator)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lv_port_pc_visual_studio/LvglPlatform)

# --- ADDED: Find ZLIB to fix the libzip linking error ---
find_package(ZLIB REQUIRED)
# --------------------------------------------------------

# Our source files for the Linux/Radxa build
set(SOURCES
    main.cpp
    RadxaEPD.cpp
    RadxaTouch.cpp
    src/App.cpp
    lv_port_pc_visual_studio/LvglWindowsSimulator/epubHandler.cpp
    lv_port_pc_visual_studio/LvglWindowsSimulator/pdfHandler.cpp
)

add_executable(TabletClient ${SOURCES})

# Link libraries
# gpiod for GPIO control
# pthread for threading (used by standard library threads)
# --- ADDED: MuPDF missing dependencies ---
target_link_libraries(TabletClient PRIVATE
    gpiod
    pthread
    zip
    # MuPDF and its massive web of dependencies:
    mupdf
    mupdf-third
    freetype
    harfbuzz
    jpeg
    openjp2
    jbig2dec
    gumbo
    mujs
    m           # Standard math library
    z           # Standard zlib library (Moved to the absolute bottom)
)

if(TARGET lvgl)
    target_link_libraries(TabletClient PRIVATE lvgl)
endif()
