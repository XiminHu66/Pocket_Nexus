#include <cstdio>
#include <cstring>

#include "M5Unified.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "page_ble.h"

namespace {
constexpr gpio_num_t front_pin = GPIO_NUM_11;
constexpr gpio_num_t side_pin = GPIO_NUM_12;
constexpr uint32_t status_ms = 10000;
constexpr uint32_t battery_interval_ms = 30000;
constexpr uint32_t mode_menu_ms = 15000;

enum class Menu { None, Mode, Variant, Devices, Manage, HostActions, Label, Forget };
constexpr uint8_t add_item = PAGE_HOST_COUNT;
constexpr uint8_t manage_item = PAGE_HOST_COUNT + 1;

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
    {"VIDEO", PROFILE_VIDEO_KEYS, PROFILE_VIDEO_WHEEL, "PC arrow keys", "Mouse wheel"},
    {"IPHONE", PROFILE_IPHONE_VERTICAL, PROFILE_IPHONE_HORIZONTAL, "Vertical swipe", "Horizontal swipe"},
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
uint8_t host_choice;
uint8_t managed_host;
bool forget_action;
bool rename_host;
page_host_label_t label_choice = HOST_OTHER;
bool low_battery;
bool storage_error;
bool bluetooth_error;
uint32_t display_until;
uint32_t menu_until;
int battery_level = -1;

bool before(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(deadline - now) > 0;
}

void host_name(char *out, size_t length, uint8_t slot, uint8_t label)
{
    if (slot < PAGE_HOST_COUNT && label != HOST_EMPTY) {
        std::snprintf(out, length, "%s %u", page_host_label_name(static_cast<page_host_label_t>(label)),
                      static_cast<unsigned>(slot + 1));
    } else {
        std::snprintf(out, length, "%s", PAGE_BLE_NAME);
    }
}

bool has_empty_slot(const page_ble_status_t &status)
{
    for (uint8_t label : status.labels) {
        if (label == HOST_EMPTY) {
            return true;
        }
    }
    return false;
}

bool menu_item_visible(const page_ble_status_t &status, uint8_t item, bool device_menu)
{
    if (item < PAGE_HOST_COUNT) {
        return status.labels[item] != HOST_EMPTY;
    }
    if (item == add_item) {
        return device_menu && has_empty_slot(status);
    }
    for (uint8_t label : status.labels) {
        if (label != HOST_EMPTY) {
            return device_menu;
        }
    }
    return false;
}

uint8_t next_host_choice(const page_ble_status_t &status, uint8_t from, bool device_menu)
{
    for (unsigned i = 1; i <= manage_item + 1U; ++i) {
        uint8_t candidate = (from + i) % (manage_item + 1U);
        if (menu_item_visible(status, candidate, device_menu)) {
            return candidate;
        }
    }
    return from;
}

void render_choice(unsigned row, bool selected, const char *text)
{
    auto &display = M5.Display;
    display.setTextColor(selected ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
    display.drawString(selected ? ">" : " ", 8, 108 + row * 13);
    display.drawString(text, 20, 108 + row * 13);
}

bool needs_phone_pointer(const page_ble_status_t &status)
{
    auto mode = static_cast<page_profile_t>(status.settings & ~PAGE_SETTINGS_INVERT);
    return page_swipe_for(mode, false, PAGE_NEXT) != SWIPE_NONE &&
           !(status.reports & (1U << REPORT_MOUSE));
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
    } else if (menu == Menu::Forget) {
        title = "FORGET DEVICE?";
        line = "A: Forget  B: Cancel";
    } else if (menu == Menu::Devices || menu == Menu::Manage ||
               menu == Menu::HostActions || menu == Menu::Label) {
        switch (menu) {
        case Menu::Devices: title = "DEVICES"; break;
        case Menu::Manage: title = "MANAGE"; break;
        case Menu::HostActions: title = "HOST SETTINGS"; break;
        default: title = rename_host ? "NAME DEVICE" : "ADD DEVICE"; break;
        }
        line = "A: Cycle   B: Select";
    } else if (menu == Menu::Mode || menu == Menu::Variant) {
        title = menu == Menu::Mode ? "SELECT MODE" : modes[selected_mode].title;
        line = "A: Cycle   B: Use";
    } else {
        switch (status.state) {
        case BLE_STARTING: title = "STARTING"; break;
        case BLE_PAIRING: title = "PAIRING"; line = PAGE_BLE_NAME; break;
        case BLE_RECONNECTING: title = "RECONNECTING"; line = "Open host Bluetooth"; break;
        case BLE_SECURING: title = "CONNECTING"; line = "Securing Bluetooth"; break;
        case BLE_CONNECTED:
            title = needs_phone_pointer(status) ? "NO POINTER" : (low_battery ? "LOW BATTERY" : "CONNECTED");
            line = needs_phone_pointer(status) ? "Enable AssistiveTouch" : "A: Next   B: Back";
            break;
        case BLE_IDLE: title = "DISCONNECTED";
            line = status.selected < PAGE_HOST_COUNT ? "Tap A or B to retry" : "Add via device menu"; break;
        case BLE_SWITCHING: title = "SWITCHING"; break;
        case BLE_RETRY_OR_BACK: title = "NO CONNECTION";
            line = "A: Retry   B: Return"; break;
        case BLE_ERROR: break;
        }
    }
    if (!display_on) {
        display.wakeup();
    }
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    char device[20];
    host_name(device, sizeof(device), status.selected, status.target_label);
    display.drawString(device, 8, 15);
    display.setTextSize(1.5f);
    display.drawString(title, 8, 60);
    display.setTextSize(1);
    display.drawString(line, 8, 89);
    if (menu == Menu::Devices || menu == Menu::Manage) {
        unsigned row = 0;
        for (uint8_t item = 0; item <= manage_item; ++item) {
            if (!menu_item_visible(status, item, menu == Menu::Devices)) {
                continue;
            }
            if (item < PAGE_HOST_COUNT) {
                char name[20];
                host_name(name, sizeof(name), item, status.labels[item]);
                render_choice(row++, host_choice == item, name);
                if (item == status.selected && status.state == BLE_CONNECTED) {
                    display.drawString("*", 102, 108 + (row - 1) * 13);
                }
            } else {
                render_choice(row++, host_choice == item, item == add_item ? "Add device" : "Manage devices");
            }
        }
    } else if (menu == Menu::HostActions) {
        render_choice(0, !forget_action, "Name");
        render_choice(1, forget_action, "Forget");
    } else if (menu == Menu::Label) {
        for (unsigned label = HOST_OTHER; label < HOST_LABEL_COUNT; ++label) {
            const auto candidate = static_cast<page_host_label_t>(label);
            render_choice(label - HOST_OTHER, candidate == label_choice, page_host_label_name(candidate));
        }
    } else if (menu == Menu::Forget) {
        if (managed_host < PAGE_HOST_COUNT) {
            char name[20];
            host_name(name, sizeof(name), managed_host, status.labels[managed_host]);
            display.drawString(name, 8, 118);
        }
    } else if (menu == Menu::Mode) {
        for (unsigned i = 0; i < mode_count; ++i) {
            render_choice(i, i == selected_mode, modes[i].title);
        }
    } else if (menu == Menu::Variant) {
        const Mode &mode = modes[selected_mode];
        render_choice(0, !use_alternate, mode.primary_label);
        render_choice(1, use_alternate, mode.alternate_label);
    } else if (menu == Menu::None) {
        if (status.state == BLE_PAIRING) {
            display.drawString("Open host Bluetooth", 8, 104);
        } else if (status.state == BLE_CONNECTED && needs_phone_pointer(status)) {
            display.drawString("Check iPhone Devices", 8, 104);
        } else if (status.state == BLE_RETRY_OR_BACK && status.previous < PAGE_HOST_COUNT) {
            char name[20];
            host_name(name, sizeof(name), status.previous, status.labels[status.previous]);
            display.drawString(name, 8, 110);
        }
        display.drawString("A+B 1s: mode", 8, 140);
        display.drawString("A+B 3s: devices", 8, 156);
    }
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(page_profile_name(profile), 8, 190);
    if (status.storage_failed) {
        display.drawString("Settings not saved", 8, 207);
    } else if (battery_level >= 0) {
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

bool save_config(page_profile_t next_profile, bool next_inverted)
{
    auto status = page_ble_status();
    if (status.state == BLE_STARTING || status.state == BLE_SWITCHING ||
        status.state == BLE_SECURING || status.state == BLE_ERROR) {
        return false;
    }
    uint8_t slot = status.selected;
    if (slot >= PAGE_HOST_COUNT || status.labels[slot] == HOST_EMPTY) {
        slot = PAGE_HOST_NONE;
    }
    return page_ble_configure(slot, next_profile, next_inverted);
}

void choose_mode(page_profile_t next_profile)
{
    if (save_config(next_profile, inverted)) {
        menu = Menu::None;
    }
}

void open_devices(const page_ble_status_t &status, uint32_t now)
{
    menu_until = now + mode_menu_ms;
    menu = Menu::Devices;
    if (status.previous < PAGE_HOST_COUNT && status.labels[status.previous] != HOST_EMPTY) {
        host_choice = status.previous;
    } else if (status.selected < PAGE_HOST_COUNT && status.labels[status.selected] != HOST_EMPTY) {
        host_choice = status.selected;
    } else {
        host_choice = next_host_choice(status, manage_item, true);
    }
}

void handle_menu_button(page_action_t action, const page_ble_status_t &status, uint32_t now)
{
    bool front = action == PAGE_NEXT;
    menu_until = now + mode_menu_ms;
    switch (menu) {
    case Menu::Devices:
        if (front) {
            host_choice = next_host_choice(status, host_choice, true);
        } else if (host_choice == add_item && has_empty_slot(status)) {
            rename_host = false;
            label_choice = HOST_OTHER;
            menu = Menu::Label;
        } else if (host_choice == manage_item) {
            host_choice = next_host_choice(status, manage_item, false);
            menu = Menu::Manage;
        } else if (host_choice < PAGE_HOST_COUNT && status.labels[host_choice] != HOST_EMPTY &&
                   page_ble_select(host_choice)) {
            menu = Menu::None;
        }
        break;
    case Menu::Manage:
        if (front) {
            host_choice = next_host_choice(status, host_choice, false);
        } else if (host_choice < PAGE_HOST_COUNT) {
            managed_host = host_choice;
            forget_action = false;
            menu = Menu::HostActions;
        }
        break;
    case Menu::HostActions:
        if (front) {
            forget_action = !forget_action;
        } else if (forget_action) {
            menu = Menu::Forget;
        } else {
            rename_host = true;
            label_choice = static_cast<page_host_label_t>(status.labels[managed_host]);
            menu = Menu::Label;
        }
        break;
    case Menu::Label:
        if (front) {
            label_choice = static_cast<page_host_label_t>(
                label_choice + 1 < HOST_LABEL_COUNT ? label_choice + 1 : HOST_OTHER);
        } else if (rename_host ? page_ble_rename(managed_host, label_choice) : page_ble_add(label_choice)) {
            menu = Menu::None;
        }
        break;
    case Menu::Forget:
        if (!front || page_ble_forget(managed_host)) {
            menu = Menu::None;
        }
        break;
    case Menu::Mode: {
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
        std::puts("status | mode boox/ppt/manga/manga-scroll/video-pc/video-phone/iphone-vertical/iphone-horizontal | invert on/off");
        return false;
    }
    if (std::strcmp(line, "status") == 0) {
        auto status = page_ble_status();
        char device[20];
        host_name(device, sizeof(device), status.selected, status.target_label);
        std::printf("%s, mode %s, inverted %s, connected %s, mouse report %s, battery %d%%\n", device,
                    page_profile_name(profile), inverted ? "yes" : "no",
                    status.state == BLE_CONNECTED ? "yes" : "no",
                    status.reports & (1U << REPORT_MOUSE) ? "ready" : "unavailable", battery_level);
        return false;
    }

    static constexpr struct { const char *command; page_profile_t target; } shortcuts[] = {
        {"mode boox", PROFILE_BOOX}, {"mode ppt", PROFILE_PPT},
        {"mode manga", PROFILE_MANGA}, {"mode manga-scroll", PROFILE_MANGA_SCROLL},
        {"mode video-pc", PROFILE_VIDEO_KEYS}, {"mode video-phone", PROFILE_VIDEO_WHEEL},
        {"mode iphone-vertical", PROFILE_IPHONE_VERTICAL}, {"mode iphone-horizontal", PROFILE_IPHONE_HORIZONTAL},
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

    esp_err_t err = nvs_flash_init();
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
        bool redraw = status.state != previous.state || status.selected != previous.selected ||
                      status.target_label != previous.target_label || status.settings != previous.settings ||
                      status.storage_failed != previous.storage_failed ||
                      needs_phone_pointer(status) != needs_phone_pointer(previous) ||
                      std::memcmp(status.labels, previous.labels, sizeof(status.labels)) != 0;
        previous = status;
        profile = static_cast<page_profile_t>(status.settings & ~PAGE_SETTINGS_INVERT);
        inverted = (status.settings & PAGE_SETTINGS_INVERT) != 0;

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
        if (action == PAGE_DEVICE_GESTURE) {
            open_devices(status, now);
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
                handle_menu_button(action, status, now);
                redraw = true;
            } else if (press_profile == profile && press_inverted == inverted) {
                if (press_status.state == BLE_CONNECTED && status.state == BLE_CONNECTED) {
                    if (!page_ble_send(press_status.session, profile, inverted, action) &&
                        needs_phone_pointer(status)) {
                        redraw = true;
                    }
                } else if (press_status.state == BLE_RETRY_OR_BACK && status.state == BLE_RETRY_OR_BACK) {
                    if (action == PAGE_PREV) {
                        page_ble_select(status.previous);
                    } else {
                        page_ble_retry();
                    }
                    redraw = true;
                } else if (press_status.state == BLE_IDLE && status.state == BLE_IDLE) {
                    page_ble_retry();
                    redraw = true;
                } else {
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
