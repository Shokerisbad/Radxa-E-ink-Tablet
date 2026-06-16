#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "lvgl/lvgl.h"
#include "RadxaEPD.h"
#include "RadxaTouch.h"
#include "src/App.h"
#include <mutex>
#include <fstream>

extern std::recursive_mutex lvgl_mutex;

// Define display resolution
// Logical resolution (portrait — how LVGL sees the screen)
#define DISP_HOR_RES 480
#define DISP_VER_RES 800

// Physical resolution (landscape — how the EPD panel is wired)
#define PHYS_WIDTH 800
#define PHYS_HEIGHT 480

// Draw buffer size (1/10 screen size is a good default for LVGL, but for EPD we often use full screen buffer or partial)
// Since EPD requires full frame for SPI transfer, we'll allocate a full screen buffer.
#define DISP_BUF_SIZE (DISP_HOR_RES * DISP_VER_RES)

volatile bool g_running = true;
pid_t g_server_pid = -1;

void signal_handler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    g_running = false;
}

// Custom tick interface for LVGL on Linux
static uint32_t custom_tick_get(void) {
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        auto now = std::chrono::steady_clock::now();
        start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }
    auto now = std::chrono::steady_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return (uint32_t)(now_ms - start_ms);
}

// System Status Bar - Example UI Setup
void create_status_bar() {
    lv_obj_t * status_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(status_bar, DISP_HOR_RES, 30); // Uses logical width (480)
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_white(), 0);
    lv_obj_set_style_border_side(status_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(status_bar, 2, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Wi-Fi Icon
    lv_obj_t * wifi_label = lv_label_create(status_bar);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(wifi_label, NULL, LV_STATE_PRESSED); // no animation
    lv_obj_add_event_cb(wifi_label, [](lv_event_t *e) {
        show_wifi_menu();
    }, LV_EVENT_CLICKED, NULL);

    // VPN Icon
    lv_obj_t * vpn_label = lv_label_create(status_bar);
    lv_label_set_text(vpn_label, ""); // Will be populated with LV_SYMBOL_SHIELD if connected
    lv_obj_align_to(vpn_label, wifi_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    // Battery Icon
    lv_obj_t * batt_label = lv_label_create(status_bar);
    lv_label_set_text(batt_label, LV_SYMBOL_BATTERY_FULL " 100%");
    lv_obj_align(batt_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // Time Label
    lv_obj_t * time_label = lv_label_create(status_bar);
    time_t t_init = time(NULL);
    struct tm tm_init = *localtime(&t_init);
    lv_label_set_text_fmt(time_label, "%02d:%02d", tm_init.tm_hour, tm_init.tm_min);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

    // Refresh Icon
    lv_obj_t * refresh_label = lv_label_create(status_bar);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_align_to(refresh_label, batt_label, LV_ALIGN_OUT_LEFT_MID, -15, 0);
    lv_obj_add_flag(refresh_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(refresh_label, NULL, LV_STATE_PRESSED); // no animation
    lv_obj_add_event_cb(refresh_label, [](lv_event_t *e) {
        update_status_bar();
        if(g_epd_instance) {
            g_epd_instance->force_full_refresh();
            lv_obj_invalidate(lv_scr_act());
        }
    }, LV_EVENT_CLICKED, NULL);

    // Expose labels for manual updates
    extern void init_status_bar_labels(lv_obj_t* w, lv_obj_t* v, lv_obj_t* b, lv_obj_t* t);
    init_status_bar_labels(wifi_label, vpn_label, batt_label, time_label);
}

static lv_obj_t* g_w_lbl = nullptr;
static lv_obj_t* g_v_lbl = nullptr;
static lv_obj_t* g_b_lbl = nullptr;
static lv_obj_t* g_t_lbl = nullptr;

void init_status_bar_labels(lv_obj_t* w, lv_obj_t* v, lv_obj_t* b, lv_obj_t* t) {
    g_w_lbl = w;
    g_v_lbl = v;
    g_b_lbl = b;
    g_t_lbl = t;
}

void update_status_bar() {
    if (!g_w_lbl || !g_v_lbl || !g_b_lbl || !g_t_lbl) return;

    // Update Wi-Fi
    if (RadxaEPD::is_wifi_connected()) {
        lv_label_set_text(g_w_lbl, LV_SYMBOL_WIFI);
    } else {
        lv_label_set_text(g_w_lbl, "No WiFi");
    }

    // Update VPN
    if (RadxaEPD::is_wireguard_connected()) {
        lv_label_set_text(g_v_lbl, LV_SYMBOL_DUMMY " VPN"); // LV_SYMBOL_DUMMY or custom icon
    } else {
        lv_label_set_text(g_v_lbl, "");
    }

    // Update Battery with 5% step logic to avoid constant refreshes
    static int last_displayed_batt = -1;
    int batt = RadxaEPD::get_battery_percentage();
    if (batt >= 0) {
        if (last_displayed_batt == -1 || abs(batt - last_displayed_batt) >= 5 || batt == 100 || batt <= 5) {
            lv_label_set_text_fmt(g_b_lbl, LV_SYMBOL_BATTERY_FULL " %d%%", batt);
            last_displayed_batt = batt;
        }
    }

    // Update Time
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    lv_label_set_text_fmt(g_t_lbl, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

int main(void) {
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "Starting Radxa LVGL Tablet Client...\n";

#ifndef _WIN32
    // Auto-install systemd service
    const char * svc_path = "/etc/systemd/system/tablet.service";
    if (access(svc_path, F_OK) != 0) {
        std::cout << "tablet.service not found. Installing...\n";
        char exe_path[1024];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            std::string exe_str(exe_path);
            std::string dir_str = exe_str.substr(0, exe_str.find_last_of('/'));
            
            std::ofstream svc(svc_path);
            if (svc.is_open()) {
                svc << "[Unit]\n"
                    << "Description=E-ink Tablet App\n"
                    << "After=network.target\n\n"
                    << "[Service]\n"
                    << "Type=simple\n"
                    << "User=root\n"
                    << "WorkingDirectory=" << dir_str << "\n"
                    << "ExecStart=" << exe_str << "\n"
                    << "Restart=on-failure\n"
                    << "RestartSec=3\n"
                    << "StandardOutput=journal\n"
                    << "StandardError=journal\n\n"
                    << "[Install]\n"
                    << "WantedBy=multi-user.target\n";
                svc.close();
                system("systemctl daemon-reload");
                system("systemctl enable tablet.service");
                std::cout << "tablet.service installed and enabled.\n";
            } else {
                std::cerr << "Failed to write tablet.service. Are you running as root?\n";
            }
        }
    }
#endif
 
    // Spawn Web Dashboard server process
    g_server_pid = fork();
    if (g_server_pid == 0) {
        // Child process: search for server.py and execute it
        const char* paths[] = {
            "../WebDashboard/server.py",
            "../../WebDashboard/server.py",
            "./WebDashboard/server.py",
            "/home/radxa/Radxa-E-ink-Tablet/WebDashboard/server.py",
            "/home/radxa/WebDashboard/server.py",
            "WebDashboard/server.py"
        };
        
        const char* selected_path = nullptr;
        for (const char* p : paths) {
            if (access(p, F_OK) == 0) {
                selected_path = p;
                break;
            }
        }
        
        if (selected_path) {
            execlp("python3", "python3", selected_path, NULL);
        } else {
            std::cerr << "[Dashboard] server.py not found in common locations. Dashboard won't start.\n";
        }
        _exit(1); // Exit child immediately if exec fails
    } else if (g_server_pid < 0) {
        std::cerr << "[Dashboard] Failed to fork server process.\n";
    } else {
        std::cout << "[Dashboard] Spawned background Web Dashboard server (PID: " << g_server_pid << ")\n";
    }

    // 1. Initialize LVGL
    lv_init();
    lv_tick_set_cb(custom_tick_get);

    // 2. Initialize Hardware Display Driver
    RadxaEPD epd;
    if (!epd.init()) {
        std::cerr << "Failed to initialize Radxa EPD. Exiting.\n";
        return -1;
    }

    // 3. Register Display Driver in LVGL (v9 API)
    // LVGL sees a 480x800 portrait display. We rotate the pixels in flush_cb.
    static uint8_t * buf1 = (uint8_t *)malloc(DISP_BUF_SIZE * sizeof(lv_color32_t));
    
    lv_display_t * disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_flush_cb(disp, RadxaEPD::flush_cb);
    lv_display_set_buffers(disp, buf1, NULL, DISP_BUF_SIZE * sizeof(lv_color32_t), LV_DISPLAY_RENDER_MODE_DIRECT);

    // No LVGL rotation — we handle it in flush_cb to avoid dimension mismatches

    create_status_bar();

    // Delegate UI creation to the cross-platform App
    build_tablet_ui();

    // 4. Initialize Hardware Touch Driver
    RadxaTouch touch;
    if (touch.init()) {
        lv_indev_t * indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, RadxaTouch::read_cb);

        std::cout << "Touch driver registered with LVGL." << std::endl;
    } else {
        std::cerr << "Warning: Touch driver failed to initialize. Continuing without touch." << std::endl;
    }

    // 5. Main LVGL Loop
    std::cout << "Entering LVGL Main Loop...\n";
    while (g_running) {
        uint32_t sleep_ms;
        {
            std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
            sleep_ms = lv_timer_handler();
        }
        
        // Clamp sleep to preserve touch responsiveness while preventing high CPU usage
        if (sleep_ms < 10) sleep_ms = 10;
        else if (sleep_ms > 40) sleep_ms = 40;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    std::cout << "Putting display to sleep and exiting...\n";
    
    // Clear screen to white gracefully before shutting down
    std::vector<uint8_t> white_buf(PHYS_WIDTH * PHYS_HEIGHT / 8, 0x00); // 0x00 is white in this driver
    epd.refresh_full(white_buf.data());
    
    epd.sleep();
 
    // Terminate spawned Web Dashboard server
    if (g_server_pid > 0) {
        std::cout << "[Dashboard] Terminating Web Dashboard server (PID: " << g_server_pid << ")..." << std::endl;
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
    }

    // Cleanup
    free(buf1);
    // buf2 removed — single buffer is sufficient with manual rotation
    return 0;
}
