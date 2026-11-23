#include "page_ble.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/bas/ble_svc_bas.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

enum {
    PAIR_MS = 90000, RECONNECT_MS = 60000, SWITCH_MS = 12000, SECURITY_MS = 12000,
    RELEASE_MS = 30, RELEASE_RETRY_MS = 15, MAX_RELEASE_TRIES = 4,
    REQUEST_PROFILE = 0x07, REQUEST_INVERT = 0x08, REQUEST_PREVIOUS = 0x10, REQUEST_VALID = 0x20,
    SESSION_SHIFT = 8, CMD_RETRY = 1, CMD_BATTERY = 2,
};

_Static_assert(PROFILE_COUNT <= REQUEST_PROFILE + 1, "HID request profile must fit");

static const char *TAG = "page_ble";
// USB identity of the ESP32-S3's built-in serial/JTAG interface on this board.
static const uint8_t pnp_id[] = { 2, 0x3a, 0x30, 0x01, 0x10, 0x01, 0x00 };
static const char manufacturer[] = "M5Stack";
static const uint8_t hid_info[] = { 0x11, 0x01, 0, 0 };
struct input_report {
    uint8_t reference[2];
    uint8_t size;
};
static const struct input_report keyboard_input = { { REPORT_KEYBOARD, 1 }, REPORT_KEYBOARD_SIZE };
static const struct input_report consumer_input = { { REPORT_CONSUMER, 1 }, REPORT_CONSUMER_SIZE };
static const struct input_report mouse_input = { { REPORT_MOUSE, 1 }, REPORT_MOUSE_SIZE };
static const uint8_t report_map[] = {
    // Keyboard, standard 8-byte input (modifiers, reserved, six usage slots).
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, REPORT_KEYBOARD,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
    0x75, 0x08, 0x95, 0x06, 0x15, 0x00, 0x25, 0x65,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0,
    // Consumer control, two independent volume bits plus six padding bits.
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, REPORT_CONSUMER,
    0x09, 0xe9, 0x09, 0xea, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x01, 0xc0,
    // Mouse: button byte, signed relative X, Y and wheel. Only wheel is emitted.
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, REPORT_MOUSE,
    0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81,
    0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xc0, 0xc0,
};

typedef enum { HOST_CMD_NONE, HOST_CMD_SELECT, HOST_CMD_ADD, HOST_CMD_FORGET,
               HOST_CMD_RENAME, HOST_CMD_CONFIG } host_operation_t;

// The host task owns the directory, live connection and timers. Only copies of
// status and small commands cross from the controls task into NimBLE.
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static page_ble_status_t snapshot = {
    .state = BLE_STARTING, .selected = PAGE_HOST_NONE, .previous = PAGE_HOST_NONE,
    .settings = PROFILE_PPT,
};
static _Atomic uint32_t pending_page;
static _Atomic unsigned pending_host;
static _Atomic unsigned commands;
static _Atomic unsigned battery_percent = 100;
static _Atomic bool started;
static struct ble_npl_event command_event, advertising_event;
static struct ble_npl_callout release_timer, security_timer;
static page_host_book_t book;
static nvs_handle_t host_store;
static uint8_t legacy_settings = PROFILE_PPT;
static unsigned transition;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t input_handles[REPORT_MOUSE + 1];
static uint32_t session;
static int64_t adv_until, security_until, release_at;
static uint8_t own_addr_type;
static uint8_t target = PAGE_HOST_NONE;
static uint8_t pair_slot = PAGE_HOST_NONE;
static page_host_label_t pair_label;
static unsigned subscriptions, release_tries;
static page_report_t active_report;
static bool encrypted, suspended, synced, initialized, manual_window, storage_failed;

static int on_gap(struct ble_gap_event *event, void *arg);

static unsigned report_flag(uint8_t id)
{
    return 1U << id;
}

static uint8_t current_settings(void)
{
    return page_host_exists(&book, target) ? book.hosts[target].settings : book.fallback;
}

static void publish(page_ble_state_t state)
{
    page_ble_status_t status = {
        .state = state, .session = session,
        .selected = pair_slot != PAGE_HOST_NONE ? pair_slot : target,
        .previous = page_host_return(&book, pair_slot != PAGE_HOST_NONE ? PAGE_HOST_NONE : target),
        .settings = current_settings(), .target_label = pair_slot != PAGE_HOST_NONE ? pair_label :
            (page_host_exists(&book, target) ? book.hosts[target].label : HOST_EMPTY),
        .reports = state == BLE_CONNECTED && !suspended && !transition ? subscriptions : 0,
        .storage_failed = storage_failed,
    };
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        status.labels[slot] = book.hosts[slot].label;
    }
    portENTER_CRITICAL(&status_lock);
    snapshot = status;
    portEXIT_CRITICAL(&status_lock);
}

static int append_value(struct os_mbuf *om, const void *bytes, uint16_t size)
{
    return os_mbuf_append(om, bytes, size) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int hid_access(uint16_t handle, uint16_t attribute, struct ble_gatt_access_ctxt *ctxt,
                      void *arg)
{
    (void)handle;
    (void)attribute;
    switch (ble_uuid_u16(ctxt->chr->uuid)) {
    case 0x2a4a:
        return append_value(ctxt->om, hid_info, sizeof(hid_info));
    case 0x2a4b:
        return append_value(ctxt->om, report_map, sizeof(report_map));
    case 0x2a4c: {
        uint8_t value;
        if (OS_MBUF_PKTLEN(ctxt->om) != 1 || os_mbuf_copydata(ctxt->om, 0, 1, &value) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (value > 1) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        suspended = value == 0;
        if (encrypted && !transition) {
            publish(BLE_CONNECTED);
        }
        return 0;
    }
    case 0x2a4d: {
        const struct input_report *input = arg;
        uint8_t zeros[REPORT_KEYBOARD_SIZE] = {0};
        const uint8_t *bytes = active_report.id == input->reference[0] ? active_report.bytes : zeros;
        return append_value(ctxt->om, bytes, input->size);
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int report_reference(uint16_t conn_handle, uint16_t attribute,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attribute;
    const struct input_report *input = arg;
    return append_value(ctxt->om, input->reference, sizeof(input->reference));
}

static int device_info_access(uint16_t handle, uint16_t attribute,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)handle;
    (void)attribute;
    (void)arg;
    if (ble_uuid_u16(ctxt->chr->uuid) == 0x2a50) {
        return append_value(ctxt->om, pnp_id, sizeof(pnp_id));
    }
    return append_value(ctxt->om, manufacturer, sizeof(manufacturer) - 1);
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180a),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2a29), .access_cb = device_info_access,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2a50), .access_cb = device_info_access,
              .flags = BLE_GATT_CHR_F_READ },
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2a4a), .access_cb = hid_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
            { .uuid = BLE_UUID16_DECLARE(0x2a4b), .access_cb = hid_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
            { .uuid = BLE_UUID16_DECLARE(0x2a4c), .access_cb = hid_access,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC },
            { .uuid = BLE_UUID16_DECLARE(0x2a4d), .access_cb = hid_access,
              .arg = (void *)&keyboard_input, .val_handle = &input_handles[REPORT_KEYBOARD],
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = report_reference,
                    .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .arg = (void *)&keyboard_input },
                  {0},
              } },
            { .uuid = BLE_UUID16_DECLARE(0x2a4d), .access_cb = hid_access,
              .arg = (void *)&consumer_input, .val_handle = &input_handles[REPORT_CONSUMER],
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = report_reference,
                    .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .arg = (void *)&consumer_input },
                  {0},
              } },
            { .uuid = BLE_UUID16_DECLARE(0x2a4d), .access_cb = hid_access,
              .arg = (void *)&mouse_input, .val_handle = &input_handles[REPORT_MOUSE],
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = report_reference,
                    .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .arg = (void *)&mouse_input },
                  {0},
              } },
            {0},
        },
    },
    {0},
};

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static page_host_address_t host_address(const ble_addr_t *addr)
{
    page_host_address_t address = { .type = addr->type & 1 };
    memcpy(address.bytes, addr->val, sizeof(address.bytes));
    return address;
}

static ble_addr_t ble_address(uint8_t slot)
{
    const page_host_address_t *address = &book.hosts[slot].address;
    ble_addr_t peer = { .type = address->type };
    memcpy(peer.val, address->bytes, sizeof(peer.val));
    return peer;
}

static void save_host_book(void)
{
    esp_err_t err = nvs_set_blob(host_store, "hosts_v1", &book, sizeof(book));
    if (err == ESP_OK) {
        err = nvs_commit(host_store);
    }
    storage_failed = err != ESP_OK;
    if (storage_failed) {
        ESP_LOGE(TAG, "Save hosts: %s", esp_err_to_name(err));
    }
}

static bool read_bonds(ble_addr_t *peers, int *count)
{
    *count = 0;
    int rc = ble_store_util_bonded_peers(peers, count, PAGE_HOST_COUNT);
    if (rc != 0) {
        ESP_LOGE(TAG, "Read bonds: %d", rc);
        return false;
    }
    return true;
}

static bool load_hosts(void)
{
    ble_addr_t peers[PAGE_HOST_COUNT];
    int count;
    if (!read_bonds(peers, &count)) {
        return false;
    }
    page_hosts_init(&book, legacy_settings);
    size_t size = sizeof(book);
    page_host_book_t saved;
    esp_err_t err = nvs_get_blob(host_store, "hosts_v1", &saved, &size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "Load hosts: %s", esp_err_to_name(err));
        return false;
    }
    bool valid = err == ESP_OK && size == sizeof(saved) && page_hosts_valid(&saved);
    if (valid) {
        book = saved;
    }
    // Reconcile after power loss between persisting a bond and saving its slot.
    uint8_t present = 0;
    for (int i = 0; i < count; ++i) {
        page_host_address_t address = host_address(&peers[i]);
        uint8_t slot = page_host_find(&book, &address);
        if (slot != PAGE_HOST_NONE) {
            present |= 1U << slot;
        }
    }
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        if (!(present & (1U << slot))) {
            page_host_remove(&book, slot);
        }
    }
    for (int i = 0; i < count; ++i) {
        page_host_address_t address = host_address(&peers[i]);
        if (page_host_find(&book, &address) == PAGE_HOST_NONE) {
            page_host_add(&book, page_host_first_free(&book), &address, HOST_OTHER, book.fallback);
        }
    }
    if (!valid || memcmp(&saved, &book, sizeof(book)) != 0) {
        save_host_book();
    }
    return true;
}

static bool pairing(void)
{
    return pair_slot < PAGE_HOST_COUNT;
}

static void advertising_finished(void)
{
    bool return_available = page_host_exists(&book, book.current) &&
                            (pairing() || target != book.current);
    publish(manual_window && return_available ? BLE_RETRY_OR_BACK : BLE_IDLE);
}

static void advertise(struct ble_npl_event *event)
{
    (void)event;
    if (!synced || transition || conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) {
        return;
    }
    int64_t remaining = adv_until - now_ms();
    bool adding = pairing();
    if (remaining <= 0 || (!adding && !page_host_exists(&book, target))) {
        advertising_finished();
        return;
    }

    ble_addr_t peer = adding ? (ble_addr_t){0} : ble_address(target);
    int rc = ble_gap_wl_set(adding ? NULL : &peer, adding ? 0 : 1);
    if (rc != 0) {
        ESP_LOGE(TAG, "Set accept list: %d", rc);
        advertising_finished();
        return;
    }

    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_BREDR_UNSUP | (adding ? BLE_HS_ADV_F_DISC_GEN : 0),
        .name = (const uint8_t *)PAGE_BLE_NAME,
        .name_len = sizeof(PAGE_BLE_NAME) - 1,
        .name_is_complete = 1,
        .appearance = 0x03c1,
        .appearance_is_present = 1,
        .uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(0x1812) },
        .num_uuids16 = 1,
        .uuids16_is_complete = 1,
    };
    rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0) {
        struct ble_gap_adv_params params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = adding ? BLE_GAP_DISC_MODE_GEN : BLE_GAP_DISC_MODE_NON,
            .filter_policy = adding ? BLE_HCI_ADV_FILT_NONE : BLE_HCI_ADV_FILT_CONN,
            .itvl_min = adding ? 96 : 160,
            .itvl_max = adding ? 144 : 240,
        };
        rc = ble_gap_adv_start(own_addr_type, NULL, (int32_t)remaining, &params, on_gap, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "Start advertising: %d", rc);
        advertising_finished();
        return;
    }
    publish(adding ? BLE_PAIRING : BLE_RECONNECTING);
}

static void schedule_advertising(void)
{
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &advertising_event);
}

static void open_window(bool manual)
{
    manual_window = manual;
    adv_until = now_ms() + (pairing() ? PAIR_MS : (manual ? SWITCH_MS : RECONNECT_MS));
    schedule_advertising();
}

static bool is_stored(const page_host_address_t *address)
{
    ble_addr_t peers[PAGE_HOST_COUNT];
    int count;
    if (!read_bonds(peers, &count)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        page_host_address_t candidate = host_address(&peers[i]);
        if (candidate.type == address->type &&
            memcmp(candidate.bytes, address->bytes, sizeof(candidate.bytes)) == 0) {
            return true;
        }
    }
    return false;
}

static int notify(uint16_t handle, const uint8_t *bytes, uint8_t size)
{
    struct os_mbuf *buffer = ble_hs_mbuf_from_flat(bytes, size);
    return buffer == NULL ? BLE_HS_ENOMEM : ble_gatts_notify_custom(conn, handle, buffer);
}

static void on_release(struct ble_npl_event *event)
{
    (void)event;
    if (conn == BLE_HS_CONN_HANDLE_NONE || active_report.id == 0) {
        return;
    }
    int64_t remaining = release_at - now_ms();
    if (remaining > 0) {
        ble_npl_callout_reset(&release_timer, ble_npl_time_ms_to_ticks32((uint32_t)remaining + 5));
        return;
    }
    uint8_t zeros[REPORT_KEYBOARD_SIZE] = {0};
    int rc = notify(input_handles[active_report.id], zeros, active_report.size);
    if (rc != 0 && ++release_tries < MAX_RELEASE_TRIES) {
        release_at = now_ms() + RELEASE_RETRY_MS;
        ble_npl_callout_reset(&release_timer, ble_npl_time_ms_to_ticks32(RELEASE_RETRY_MS));
        return;
    }
    active_report = (page_report_t){0};
    atomic_store(&pending_page, 0);
    if (rc != 0) {
        ESP_LOGW(TAG, "Release notify failed: %d", rc);
        encrypted = false;
        publish(BLE_SECURING);
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void on_security_timeout(struct ble_npl_event *event)
{
    (void)event;
    if (conn == BLE_HS_CONN_HANDLE_NONE || encrypted) {
        return;
    }
    int64_t remaining = security_until - now_ms();
    if (remaining > 0) {
        ble_npl_callout_reset(&security_timer, ble_npl_time_ms_to_ticks32((uint32_t)remaining + 5));
    } else {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static bool forget_bond(uint8_t slot)
{
    ble_addr_t peer = ble_address(slot);
    page_host_address_t address = book.hosts[slot].address;
    int rc = ble_gap_unpair(&peer);
    ble_addr_t peers[PAGE_HOST_COUNT];
    int count;
    if (!read_bonds(peers, &count)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        page_host_address_t actual = host_address(&peers[i]);
        if (actual.type == address.type && memcmp(actual.bytes, address.bytes, sizeof(actual.bytes)) == 0) {
            ESP_LOGE(TAG, "Forget host: %d", rc);
            return false;
        }
    }
    page_host_remove(&book, slot);
    save_host_book();
    return true;
}

static void finish_transition(void)
{
    host_operation_t operation = transition & 0xff;
    uint8_t slot = (transition >> 8) & 0xff;
    page_host_label_t label = transition >> 16;
    transition = HOST_CMD_NONE;

    if (operation == HOST_CMD_FORGET) {
        if (!forget_bond(slot)) {
            publish(BLE_ERROR);
            return;
        }
        if (target == slot || !page_host_exists(&book, target)) {
            target = page_host_default(&book);
        }
        pair_slot = PAGE_HOST_NONE;
        if (target == PAGE_HOST_NONE) {
            publish(BLE_IDLE);
        } else {
            open_window(false);
        }
    } else if (operation == HOST_CMD_ADD) {
        pair_slot = slot;
        pair_label = label;
        open_window(true);
    } else if (operation == HOST_CMD_SELECT) {
        target = slot;
        pair_slot = PAGE_HOST_NONE;
        open_window(true);
    }
}

static void change_host(unsigned request)
{
    if (transition) {
        return;
    }
    if (ble_gap_adv_active()) {
        int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGE(TAG, "Stop advertising: %d", rc);
            return;
        }
    }
    transition = request;
    session = (session + 1) & 0xffffff;
    publish(BLE_SWITCHING);
    if (conn != BLE_HS_CONN_HANDLE_NONE && active_report.id != 0 && encrypted) {
        uint8_t zeros[REPORT_KEYBOARD_SIZE] = {0};
        notify(input_handles[active_report.id], zeros, active_report.size);
        ble_npl_callout_stop(&release_timer);
        active_report = (page_report_t){0};
    }
    atomic_store(&pending_page, 0);
    if (conn == BLE_HS_CONN_HANDLE_NONE) {
        finish_transition();
    } else {
        int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGE(TAG, "Disconnect host: %d", rc);
            transition = HOST_CMD_NONE;
            publish(encrypted ? BLE_CONNECTED : BLE_SECURING);
        }
    }
}

static bool authorize_connection(const struct ble_gap_conn_desc *desc)
{
    page_host_address_t address = host_address(&desc->peer_id_addr);
    if (pairing()) {
        uint8_t settings = page_host_label_settings(pair_label, book.fallback);
        if (!desc->sec_state.encrypted || !desc->sec_state.bonded || !is_stored(&address) ||
            !page_host_add(&book, pair_slot, &address, pair_label, settings)) {
            return false;
        }
        target = pair_slot;
        pair_slot = PAGE_HOST_NONE;
        page_host_connected(&book, target);
        save_host_book();
        return true;
    }
    if (!page_host_can_send(&book, target, &address, desc->sec_state.encrypted, desc->sec_state.bonded)) {
        return false;
    }
    if (book.current != target) {
        page_host_connected(&book, target);
        save_host_book();
    }
    return true;
}

static int on_gap(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status != 0) {
            schedule_advertising();
            break;
        }
        conn = event->connect.conn_handle;
        if (transition) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        session = (session + 1) & 0xffffff;
        publish(BLE_SECURING);
        if (now_ms() >= adv_until || ble_gap_conn_find(conn, &desc) != 0) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        page_host_address_t address = host_address(&desc.peer_id_addr);
        uint8_t slot = page_host_find(&book, &address);
        if (pairing() ? slot != PAGE_HOST_NONE : slot == PAGE_HOST_NONE || slot != target) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        security_until = now_ms() + SECURITY_MS;
        ble_npl_callout_reset(&security_timer, ble_npl_time_ms_to_ticks32(SECURITY_MS));
        int rc = ble_gap_security_initiate(conn);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "Start link security: %d", rc);
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        if (conn != event->enc_change.conn_handle || transition) {
            break;
        }
        if (event->enc_change.status != 0 || ble_gap_conn_find(conn, &desc) != 0 ||
            !authorize_connection(&desc)) {
            encrypted = false;
            publish(BLE_SECURING);
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        ble_npl_callout_stop(&security_timer);
        manual_window = false;
        encrypted = true;
        publish(BLE_CONNECTED);
        const struct ble_gap_upd_params params = {
            .itvl_min = 12, .itvl_max = 24, .latency = 9, .supervision_timeout = 400,
        };
        ble_gap_update_params(conn, &params);
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        bool was_connected = encrypted ||
            (!manual_window && !pairing() && target == book.current && event->disconnect.conn.sec_state.bonded);
        ble_npl_callout_stop(&security_timer);
        ble_npl_callout_stop(&release_timer);
        conn = BLE_HS_CONN_HANDLE_NONE;
        subscriptions = 0;
        active_report = (page_report_t){0};
        encrypted = suspended = false;
        atomic_store(&pending_page, 0);
        if (transition) {
            finish_transition();
        } else if (was_connected) {
            publish(BLE_RECONNECTING);
            open_window(false);
        } else {
            schedule_advertising();
        }
        break;
    }
    case BLE_GAP_EVENT_TERM_FAILURE:
        if (transition && event->term_failure.conn_handle == conn) {
            ESP_LOGW(TAG, "Host switch disconnect failed: %d", event->term_failure.status);
            transition = HOST_CMD_NONE;
            publish(encrypted ? BLE_CONNECTED : BLE_SECURING);
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        if (event->subscribe.conn_handle != conn) {
            break;
        }
        uint16_t attr = event->subscribe.attr_handle;
        for (uint8_t id = REPORT_KEYBOARD; id <= REPORT_MOUSE; ++id) {
            if (input_handles[id] == attr) {
                unsigned flag = report_flag(id);
                subscriptions = event->subscribe.cur_notify ? subscriptions | flag : subscriptions & ~flag;
                break;
            }
        }
        if (encrypted && !transition) {
            publish(BLE_CONNECTED);
        }
        break;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (conn == BLE_HS_CONN_HANDLE_NONE && !transition) {
            advertising_finished();
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:
        break;
    }
    return 0;
}

static void apply_host_command(unsigned request)
{
    host_operation_t operation = request & 0xff;
    uint8_t slot = (request >> 8) & 0xff;
    uint8_t value = request >> 16;
    bool occupied = page_host_exists(&book, slot);

    if (operation == HOST_CMD_CONFIG) {
        if (occupied && slot == target && !pairing() && !transition) {
            if (book.hosts[slot].settings != value) {
                book.hosts[slot].settings = value;
                save_host_book();
            }
            publish(page_ble_status().state);
        } else if (slot == PAGE_HOST_NONE && target == PAGE_HOST_NONE) {
            if (book.fallback != value) {
                book.fallback = value;
                save_host_book();
            }
            publish(page_ble_status().state);
        }
    } else if (operation == HOST_CMD_RENAME && occupied) {
        book.hosts[slot].label = value;
        save_host_book();
        publish(page_ble_status().state);
    } else if (operation == HOST_CMD_ADD) {
        uint8_t free_slot = page_host_first_free(&book);
        if (free_slot != PAGE_HOST_NONE) {
            change_host(HOST_CMD_ADD | ((unsigned)free_slot << 8) | ((unsigned)value << 16));
        }
    } else if (operation == HOST_CMD_SELECT && occupied) {
        if (slot != target || pairing() || !encrypted) {
            change_host(request);
        }
    } else if (operation == HOST_CMD_FORGET && occupied) {
        if (encrypted && !transition && slot != target) {
            if (!forget_bond(slot)) {
                ESP_LOGE(TAG, "Host %u was not removed", (unsigned)slot + 1);
            }
            publish(BLE_CONNECTED);
        } else {
            change_host(request);
        }
    }
}

static void on_command(struct ble_npl_event *event)
{
    (void)event;
    unsigned requests = atomic_exchange(&commands, 0);
    unsigned host_command = atomic_exchange(&pending_host, 0);
    if (!synced) {
        atomic_store(&pending_page, 0);
        return;
    }
    if (requests & CMD_BATTERY) {
        ble_svc_bas_battery_level_set((uint8_t)atomic_load(&battery_percent));
    }
    if (host_command) {
        apply_host_command(host_command);
    }
    if ((requests & CMD_RETRY) && !transition && conn == BLE_HS_CONN_HANDLE_NONE &&
        !ble_gap_adv_active() && (pairing() || page_host_exists(&book, target))) {
        open_window(manual_window);
    }

    uint32_t command = atomic_load(&pending_page);
    if (command == 0 || active_report.id != 0) {
        return;
    }
    page_report_t report = page_report_for((page_profile_t)(command & REQUEST_PROFILE),
                                           (command & REQUEST_INVERT) != 0,
                                           (command & REQUEST_PREVIOUS) ? PAGE_PREV : PAGE_NEXT);
    if (report.id == 0 || !encrypted || transition || suspended ||
        (command & (REQUEST_PROFILE | REQUEST_INVERT)) !=
            ((current_settings() & ~PAGE_SETTINGS_INVERT) |
             ((current_settings() & PAGE_SETTINGS_INVERT) ? REQUEST_INVERT : 0)) ||
        !(subscriptions & report_flag(report.id)) ||
        command >> SESSION_SHIFT != session ||
        notify(input_handles[report.id], report.bytes, report.size) != 0) {
        atomic_store(&pending_page, 0);
        return;
    }
    if (report.id == REPORT_MOUSE) {
        // Relative wheel movement is a one-shot report with no pressed buttons.
        atomic_store(&pending_page, 0);
        return;
    }
    active_report = report;
    release_tries = 0;
    release_at = now_ms() + RELEASE_MS;
    ble_npl_callout_reset(&release_timer, ble_npl_time_ms_to_ticks32(RELEASE_MS));
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
    ble_npl_callout_stop(&security_timer);
    ble_npl_callout_stop(&release_timer);
    synced = encrypted = suspended = false;
    transition = HOST_CMD_NONE;
    conn = BLE_HS_CONN_HANDLE_NONE;
    subscriptions = 0;
    active_report = (page_report_t){0};
    publish(BLE_STARTING);
    atomic_store(&pending_page, 0);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        publish(BLE_ERROR);
        return;
    }
    synced = true;
    ble_svc_bas_battery_level_set((uint8_t)atomic_load(&battery_percent));
    if (!initialized) {
        if (!load_hosts()) {
            synced = false;
            publish(BLE_ERROR);
            return;
        }
        initialized = true;
        target = page_host_default(&book);
        if (target == PAGE_HOST_NONE) {
            pair_slot = page_host_first_free(&book);
            pair_label = HOST_OTHER;
        }
        open_window(false);
    } else {
        schedule_advertising();
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t page_ble_start(void)
{
    esp_err_t err = nvs_open("page_turner", NVS_READWRITE, &host_store);
    if (err != ESP_OK) {
        return err;
    }
    nvs_get_u8(host_store, "settings", &legacy_settings);
    err = nimble_port_init();
    if (err != ESP_OK) {
        nvs_close(host_store);
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1; // Just Works on this two-button device.
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_bas_init();
    ble_svc_gap_device_name_set(PAGE_BLE_NAME);
    ble_svc_gap_device_appearance_set(0x03c1);
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) {
        nimble_port_deinit();
        nvs_close(host_store);
        return ESP_FAIL;
    }
    ble_store_config_init();
    struct ble_npl_eventq *queue = nimble_port_get_dflt_eventq();
    ble_npl_event_init(&command_event, on_command, NULL);
    ble_npl_event_init(&advertising_event, advertise, NULL);
    ble_npl_callout_init(&release_timer, queue, on_release, NULL);
    ble_npl_callout_init(&security_timer, queue, on_security_timeout, NULL);
    atomic_store(&started, true);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

page_ble_status_t page_ble_status(void)
{
    portENTER_CRITICAL(&status_lock);
    page_ble_status_t copy = snapshot;
    portEXIT_CRITICAL(&status_lock);
    return copy;
}

static void command(unsigned operation)
{
    if (atomic_load(&started)) {
        atomic_fetch_or(&commands, operation);
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &command_event);
    }
}

static bool submit_host(host_operation_t operation, uint8_t slot, uint8_t value)
{
    if (!atomic_load(&started)) {
        return false;
    }
    unsigned empty = 0;
    unsigned request = (unsigned)operation | ((unsigned)slot << 8) | ((unsigned)value << 16);
    if (!atomic_compare_exchange_strong(&pending_host, &empty, request)) {
        return false;
    }
    command(0);
    return true;
}

bool page_ble_select(uint8_t slot)
{
    return slot < PAGE_HOST_COUNT && submit_host(HOST_CMD_SELECT, slot, 0);
}

bool page_ble_add(page_host_label_t label)
{
    return label > HOST_EMPTY && label < HOST_LABEL_COUNT &&
           submit_host(HOST_CMD_ADD, PAGE_HOST_NONE, label);
}

bool page_ble_forget(uint8_t slot)
{
    return slot < PAGE_HOST_COUNT && submit_host(HOST_CMD_FORGET, slot, 0);
}

bool page_ble_rename(uint8_t slot, page_host_label_t label)
{
    return slot < PAGE_HOST_COUNT && label > HOST_EMPTY && label < HOST_LABEL_COUNT &&
           submit_host(HOST_CMD_RENAME, slot, label);
}

bool page_ble_configure(uint8_t slot, page_profile_t profile, bool invert)
{
    return (unsigned)profile < PROFILE_COUNT &&
           submit_host(HOST_CMD_CONFIG, slot, (unsigned)profile | (invert ? PAGE_SETTINGS_INVERT : 0));
}

void page_ble_retry(void)
{
    command(CMD_RETRY);
}

void page_ble_set_battery(unsigned percent)
{
    if (percent <= 100 && atomic_exchange(&battery_percent, percent) != percent) {
        command(CMD_BATTERY);
    }
}

bool page_ble_send(uint32_t expected_session, page_profile_t profile, bool invert, page_action_t action)
{
    page_ble_status_t status = page_ble_status();
    page_report_t report = page_report_for(profile, invert, action);
    if (report.id == 0 || status.state != BLE_CONNECTED || !(status.reports & report_flag(report.id)) ||
        status.session != expected_session || atomic_load(&pending_host) != 0 ||
        status.settings != ((unsigned)profile | (invert ? PAGE_SETTINGS_INVERT : 0))) {
        return false;
    }
    uint32_t empty = 0;
    uint32_t request = (expected_session << SESSION_SHIFT) | REQUEST_VALID | (unsigned)profile |
                       (action == PAGE_PREV ? REQUEST_PREVIOUS : 0) | (invert ? REQUEST_INVERT : 0);
    if (!atomic_compare_exchange_strong(&pending_page, &empty, request)) {
        return false;
    }
    command(0);
    return true;
}
