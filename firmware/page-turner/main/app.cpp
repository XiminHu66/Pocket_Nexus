#include <cstdio>
#include <cstring>

#include "M5Unified.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "page_ble.h"

namespace {
constexpr gpio_num_t front_pin = GPIO_NUM_11;
constexpr gpio_num_t side_pin = GPIO_NUM_12;
constexpr uint32_t status_ms = 10000;
constexpr uint32_t confirm_ms = 8000;
constexpr uint32_t battery_interval_ms = 30000;
constexpr uint32_t mode_menu_ms = 15000;

enum class Menu { None, Mode, Variant, Pairing };

struct Mode {
    const char *title;
    page_profile_t primary;
    page_profile_t alternate;
    const char *primary_label;
    const char *alternate_label;
};

constexpr Mode modes[] = {
    {"BOOX", PROFILE_BOOX, PROFILE_COUNT, "", ""},
    {"PPT", PROFILE_PPT, PROFILE_COUNT, "", ""},
    {"MANGA", PROFILE_MANGA, PROFILE_MANGA_SCROLL, "Page arrows", "Page Up / Down"},
    {"VIDEO", PROFILE_VIDEO_KEYS, PROFILE_VIDEO_WHEEL, "PC arrow keys", "Phone wheel"},
};
constexpr unsigned mode_count = sizeof(modes) / sizeof(modes[0]);

unsigned mode_index(page_profile_t active)
{
    for (unsigned i = 0; i < mode_count; ++i) {
        if (modes[i].primary == active || modes[i].alternate == active) {
            return i;
        }
    }
    return 1;
}

page_profile_t profile = PROFILE_PPT;
bool inverted;
bool display_ready;
bool display_on;
bool usb_ready;
Menu menu = Menu::None;
unsigned selected_mode;
bool use_alternate;
bool low_battery;
bool storage_error;
bool bluetooth_error;
uint32_t display_until;
uint32_t menu_until;
int battery_level = -1;
nvs_handle_t config;
bool config_open;

bool before(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(deadline - now) > 0;
}

void show(page_ble_status_t status, uint32_t now)
{
    if (!display_ready) {
        return;
    }
    auto &display = M5.Display;
    const char *title = nullptr;
    const char *line = "";
    if (storage_error) {
        title = "STORAGE ERROR";
        line = "See USB log";
    } else if (bluetooth_error || status.state == BLE_ERROR) {
        title = "BLE ERROR";
        line = "See USB log";
    } else if (menu == Menu::Pairing) {
        title = "FORGET DEVICE?";
        line = "A: Yes   B: Cancel";
    } else if (menu == Menu::Mode || menu == Menu::Variant) {
        title = menu == Menu::Mode ? "SELECT MODE" : modes[selected_mode].title;
        line = "A: Cycle   B: Use";
    } else {
        switch (status.state) {
        case BLE_STARTING: title = "STARTING"; break;
        case BLE_PAIRING: title = "PAIRING"; line = PAGE_BLE_NAME; break;
        case BLE_RECONNECTING: title = "RECONNECTING"; line = "Open host Bluetooth"; break;
        case BLE_SECURING: title = "CONNECTING"; line = "Securing Bluetooth"; break;
        case BLE_CONNECTED: title = low_battery ? "LOW BATTERY" : "CONNECTED";
            line = "A: Next   B: Back"; break;
        case BLE_IDLE: title = "DISCONNECTED";
            line = status.bonded ? "Tap A or B to retry" : "Hold A+B for 5 sec"; break;
        case BLE_ERROR: break;
        }
    }
    if (!display_on) {
        display.wakeup();
    }
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.drawString(PAGE_BLE_NAME, 8, 15);
    display.setTextSize(1.5f);
    display.drawString(title, 8, 60);
    display.setTextSize(1);
    display.drawString(line, 8, 89);
    if (menu == Menu::Mode) {
        for (unsigned i = 0; i < mode_count; ++i) {
            display.setTextColor(i == selected_mode ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
            display.drawString(i == selected_mode ? ">" : " ", 8, 111 + i * 14);
            display.drawString(modes[i].title, 22, 111 + i * 14);
        }
    } else if (menu == Menu::Variant) {
        const Mode &mode = modes[selected_mode];
        display.setTextColor(!use_alternate ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
        display.drawString(mode.primary_label, 16, 118);
        display.setTextColor(use_alternate ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
        display.drawString(mode.alternate_label, 16, 140);
    } else if (menu == Menu::None) {
        if (status.state == BLE_PAIRING) {
            display.drawString("Open host Bluetooth", 8, 104);
        }
        display.drawString("Hold A+B: mode", 8, 156);
    }
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(page_profile_name(profile), 8, 190);
    if (battery_level >= 0) {
        char description[20];
        std::snprintf(description, sizeof(description), "Battery %d%%", battery_level);
        display.drawString(description, 8, 207);
    }
    display.setBrightness(35);
    display_on = true;
    display_until = menu == Menu::None ? now + status_ms : menu_until;
}

void update_battery()
{
    if (!display_ready) {
        return;
    }
    battery_level = M5.Power.getBatteryLevel();
    if (battery_level < 0 || battery_level > 100) {
        battery_level = -1;
        return;
    }
    if (M5.Power.isCharging() || battery_level >= 20) {
        low_battery = false;
    } else if (battery_level <= 15) {
        low_battery = true;
    }
    page_ble_set_battery(static_cast<unsigned>(battery_level));
}

esp_err_t load_config()
{
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_open("page_turner", NVS_READWRITE, &config);
    if (result != ESP_OK) {
        return result;
    }
    config_open = true;
    uint8_t value;
    // Store both choices in one key so interrupted writes cannot split them.
    if (nvs_get_u8(config, "settings", &value) == ESP_OK && (value & 0x7f) < PROFILE_COUNT) {
        profile = static_cast<page_profile_t>(value & 0x7f);
        inverted = (value & 0x80) != 0;
    }
    return ESP_OK;
}

bool save_config(page_profile_t next_profile, bool next_inverted)
{
    if (!config_open) {
        std::puts("Settings unavailable");
        return false;
    }
    if (next_profile == profile && next_inverted == inverted) {
        return true;
    }
    uint8_t value = static_cast<uint8_t>(next_profile) | (next_inverted ? 0x80 : 0);
    esp_err_t err = nvs_set_u8(config, "settings", value);
    if (err == ESP_OK) {
        err = nvs_commit(config);
    }
    if (err != ESP_OK) {
        std::printf("Settings write failed: %s\n", esp_err_to_name(err));
        return false;
    }
    profile = next_profile;
    inverted = next_inverted;
    return true;
}

void choose_mode(page_profile_t next_profile)
{
    if (save_config(next_profile, inverted)) {
        menu = Menu::None;
    }
}

void handle_menu_button(page_action_t action, uint32_t now)
{
    bool front = action == PAGE_NEXT;
    switch (menu) {
    case Menu::Pairing:
        if (front) {
            page_ble_clear_pairing();
        }
        menu = Menu::None;
        break;
    case Menu::Mode: {
        menu_until = now + mode_menu_ms;
        if (front) {
            selected_mode = (selected_mode + 1) % mode_count;
        } else {
            const Mode &mode = modes[selected_mode];
            if (mode.alternate == PROFILE_COUNT) {
                choose_mode(mode.primary);
            } else {
                use_alternate = profile == mode.alternate;
                menu = Menu::Variant;
            }
        }
        break;
    }
    case Menu::Variant: {
        menu_until = now + mode_menu_ms;
        if (front) {
            use_alternate = !use_alternate;
        } else {
            const Mode &mode = modes[selected_mode];
            choose_mode(use_alternate ? mode.alternate : mode.primary);
        }
        break;
    }
    case Menu::None:
        break;
    }
}

bool handle_command(const char *line)
{
    if (std::strcmp(line, "help") == 0) {
        std::puts("status | mode boox/ppt/manga/manga-scroll/video-pc/video-phone | invert on/off");
        return false;
    }
    if (std::strcmp(line, "status") == 0) {
        auto status = page_ble_status();
        std::printf("Mode %s, inverted %s, bonded %s, connected %s, battery %d%%\n",
                    page_profile_name(profile), inverted ? "yes" : "no", status.bonded ? "yes" : "no",
                    status.state == BLE_CONNECTED ? "yes" : "no", battery_level);
        return false;
    }

    static constexpr struct { const char *command; page_profile_t target; } shortcuts[] = {
        {"mode boox", PROFILE_BOOX}, {"mode ppt", PROFILE_PPT},
        {"mode manga", PROFILE_MANGA}, {"mode manga-scroll", PROFILE_MANGA_SCROLL},
        {"mode video-pc", PROFILE_VIDEO_KEYS}, {"mode video-phone", PROFILE_VIDEO_WHEEL},
        {"profile volume", PROFILE_BOOX}, {"profile page", PROFILE_PPT}, {"profile arrow", PROFILE_MANGA},
    };
    for (const auto &shortcut : shortcuts) {
        if (std::strcmp(line, shortcut.command) == 0) {
            if (!save_config(shortcut.target, inverted)) {
                return false;
            }
            menu = Menu::None;
            std::printf("Saved %s\n", page_profile_name(profile));
            return true;
        }
    }
    bool invert_on = std::strcmp(line, "invert on") == 0;
    if (!invert_on && std::strcmp(line, "invert off") != 0) {
        std::puts("Unknown command. Type help.");
        return false;
    }
    if (!save_config(profile, invert_on)) {
        return false;
    }
    std::printf("Inverted %s\n", inverted ? "yes" : "no");
    return true;
}

bool poll_serial()
{
    if (!usb_ready) {
        return false;
    }
    static char line[48];
    static size_t length;
    static bool discard;
    uint8_t bytes[32];
    bool redraw = false;
    int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
    for (int i = 0; i < count; ++i) {
        uint8_t ch = bytes[i];
        if (ch == '\n' || ch == '\r') {
            if (length && !discard) {
                line[length] = '\0';
                redraw |= handle_command(line);
            }
            length = 0;
            discard = false;
        } else if (ch == 8 || ch == 127) {
            if (length > 0) {
                --length;
            }
        } else if (ch >= 32 && ch <= 126) {
            if (length + 1 < sizeof(line)) {
                line[length++] = static_cast<char>(ch);
            } else {
                discard = true;
            }
        }
    }
    return redraw;
}
} // namespace

extern "C" void app_main()
{
    auto board = M5.config();
    board.internal_imu = false;
    board.internal_rtc = false;
    board.internal_mic = false;
    board.internal_spk = false;
    board.external_imu = false;
    board.external_rtc = false;
    board.output_power = false;
    M5.begin(board);
    display_ready = M5.getBoard() == m5::board_t::board_M5StickS3;
    if (display_ready) {
        M5.Display.setRotation(0);
        M5.Display.setTextWrap(false);
        M5.Display.setBrightness(0);
    } else {
        std::puts("Board detection did not identify StickS3. Check the hardware before use.");
        return;
    }

    gpio_config_t buttons_config{};
    buttons_config.pin_bit_mask = (1ULL << front_pin) | (1ULL << side_pin);
    buttons_config.mode = GPIO_MODE_INPUT;
    buttons_config.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&buttons_config));
    auto usb_config = usb_serial_jtag_driver_config_t{ .tx_buffer_size = 256, .rx_buffer_size = 256 };
    usb_ready = usb_serial_jtag_driver_install(&usb_config) == ESP_OK;
    std::printf("StickS3 Page %s. Type help.\n", esp_app_get_description()->version);

    esp_err_t err = load_config();
    storage_error = err != ESP_OK;
    if (storage_error) {
        std::printf("NVS unavailable: %s\n", esp_err_to_name(err));
    } else {
        update_battery();
        err = page_ble_start();
        bluetooth_error = err != ESP_OK;
        if (bluetooth_error) {
            std::printf("BLE unavailable: %s\n", esp_err_to_name(err));
        }
    }

    page_buttons_t buttons{};
    page_ble_status_t previous = page_ble_status();
    page_ble_status_t press_status{};
    Menu press_menu = Menu::None;
    page_profile_t press_profile = profile;
    bool press_inverted = inverted;
    uint32_t last_battery_update = 0;
    show(previous, static_cast<uint32_t>(esp_timer_get_time() / 1000));
    for (;;) {
        uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        page_ble_status_t status = page_ble_status();
        bool redraw = status.state != previous.state || status.bonded != previous.bonded;
        previous = status;

        if (now - last_battery_update >= battery_interval_ms) {
            last_battery_update = now;
            bool was_low = low_battery;
            update_battery();
            redraw |= low_battery && !was_low;
        }
        if (menu != Menu::None && !before(now, menu_until)) {
            menu = Menu::None;
            redraw = true;
        }

        uint8_t raw = (gpio_get_level(front_pin) == 0 ? KEY_FRONT : 0) |
                      (gpio_get_level(side_pin) == 0 ? KEY_SIDE : 0);
        if (buttons.gesture == 0 && buttons.raw == 0 && raw != 0) {
            press_status = status;
            press_menu = menu;
            press_profile = profile;
            press_inverted = inverted;
        }
        page_action_t action = page_buttons_poll(&buttons, raw, now);
        if (action == PAGE_PAIR_GESTURE) {
            menu = Menu::None;
            if (status.bonded) {
                menu_until = now + confirm_ms;
                menu = Menu::Pairing;
            } else {
                page_ble_retry();
            }
            redraw = true;
        } else if (action == PAGE_MODE_GESTURE) {
            if (menu == Menu::None) {
                selected_mode = mode_index(profile);
                menu_until = now + mode_menu_ms;
                menu = Menu::Mode;
            } else {
                menu = Menu::None;
            }
            redraw = true;
        } else if ((action == PAGE_NEXT || action == PAGE_PREV) && press_menu == menu) {
            if (menu != Menu::None) {
                handle_menu_button(action, now);
                redraw = true;
            } else if (press_profile == profile && press_inverted == inverted) {
                if (press_status.state == BLE_CONNECTED && status.state == BLE_CONNECTED) {
                    page_ble_send(press_status.session, profile, inverted, action);
                } else {
                    if (status.bonded && (status.state == BLE_IDLE || status.state == BLE_ERROR)) {
                        page_ble_retry();
                    }
                    redraw = true;
                }
            }
        }
        redraw |= poll_serial();
        if (redraw) {
            show(status, now);
        } else if (display_on && !before(now, display_until)) {
            M5.Display.setBrightness(0);
            M5.Display.sleep();
            display_on = false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
