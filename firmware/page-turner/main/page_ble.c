#include "page_ble.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/bas/ble_svc_bas.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

enum {
    PAIR_MS = 90000, RECONNECT_MS = 60000, SECURITY_MS = 12000,
    RELEASE_MS = 30, RELEASE_RETRY_MS = 15, MAX_RELEASE_TRIES = 4,
    SNAP_STATE = 0x07, SNAP_BONDED = 0x08, SNAP_KBD = 0x10, SNAP_CONSUMER = 0x20,
    SESSION_SHIFT = 8, CMD_RETRY = 1, CMD_CLEAR = 2, CMD_BATTERY = 4,
};

static const char *TAG = "page_ble";
// USB identity of the ESP32-S3's built-in serial/JTAG interface on this board.
static const uint8_t pnp_id[] = { 2, 0x3a, 0x30, 0x01, 0x10, 0x01, 0x00 };
static const char manufacturer[] = "M5Stack";
static const uint8_t hid_info[] = { 0x11, 0x01, 0, 0 };
static const uint8_t keyboard_ref[] = { REPORT_KEYBOARD, 1 };
static const uint8_t consumer_ref[] = { REPORT_CONSUMER, 1 };
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
};

// The BLE host task owns the link and timers. The application sees an atomic
// snapshot and submits at most one report for that exact connection session.
static _Atomic uint32_t snapshot;
static _Atomic uint32_t pending_page;
static _Atomic unsigned commands;
static _Atomic unsigned battery_percent = 100;
static _Atomic bool started;
static struct ble_npl_event command_event;
static struct ble_npl_callout release_timer, security_timer;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t keyboard_handle, consumer_handle;
static uint32_t session;
static int64_t adv_until, security_until, release_at;
static uint8_t own_addr_type;
static unsigned subscriptions;
static unsigned release_tries;
static page_report_t active_report;
static ble_addr_t peer;
static bool bonded, encrypted, suspended, synced, clearing, boot_window_opened;

static int on_gap(struct ble_gap_event *event, void *arg);
static void advertise(void);

static void publish(page_ble_state_t state)
{
    unsigned flags = bonded ? SNAP_BONDED : 0;
    if (state == BLE_CONNECTED && !suspended) {
        flags |= subscriptions;
    }
    atomic_store(&snapshot, (session << SESSION_SHIFT) | flags | state);
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
        if (encrypted) {
            publish(BLE_CONNECTED);
        }
        return 0;
    }
    case 0x2a4d: {
        const uint8_t *reference = arg;
        uint8_t zeros[8] = {0};
        const uint8_t *bytes = active_report.id == reference[0] ? active_report.bytes : zeros;
        return append_value(ctxt->om, bytes, reference[0] == REPORT_KEYBOARD ? 8 : 1);
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
    return append_value(ctxt->om, arg, 2);
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
              .arg = (void *)keyboard_ref, .val_handle = &keyboard_handle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = report_reference,
                    .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .arg = (void *)keyboard_ref },
                  {0},
              } },
            { .uuid = BLE_UUID16_DECLARE(0x2a4d), .access_cb = hid_access,
              .arg = (void *)consumer_ref, .val_handle = &consumer_handle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = report_reference,
                    .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .arg = (void *)consumer_ref },
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

static bool same_peer(const ble_addr_t *addr)
{
    // NimBLE returns identity types with the identity bit set for resolved RPAs.
    return ((addr->type ^ peer.type) & 1) == 0 && memcmp(addr->val, peer.val, 6) == 0;
}

static bool read_bond(void)
{
    int count = 0;
    int rc = ble_store_util_bonded_peers(&peer, &count, 1);
    if (rc != 0) {
        ESP_LOGE(TAG, "Read bonds: %d", rc);
        return false;
    }
    bonded = count == 1;
    peer.type &= 1;
    return true;
}

static void advertise(void)
{
    int64_t remaining = adv_until - now_ms();
    if (remaining <= 0) {
        publish(BLE_IDLE);
        return;
    }

    int rc = bonded ? ble_gap_wl_set(&peer, 1) : ble_gap_wl_set(NULL, 0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Set accept list: %d", rc);
        publish(BLE_ERROR);
        return;
    }
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_BREDR_UNSUP | (bonded ? 0 : BLE_HS_ADV_F_DISC_GEN),
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
            .disc_mode = bonded ? BLE_GAP_DISC_MODE_NON : BLE_GAP_DISC_MODE_GEN,
            .filter_policy = bonded ? BLE_HCI_ADV_FILT_CONN : BLE_HCI_ADV_FILT_NONE,
            .itvl_min = bonded ? 160 : 96,
            .itvl_max = bonded ? 240 : 144,
        };
        rc = ble_gap_adv_start(own_addr_type, NULL, (int32_t)remaining, &params, on_gap, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "Start advertising: %d", rc);
        publish(BLE_ERROR);
        return;
    }
    publish(bonded ? BLE_RECONNECTING : BLE_PAIRING);
}

static void open_window(void)
{
    adv_until = now_ms() + (bonded ? RECONNECT_MS : PAIR_MS);
    advertise();
}

static void erase_bond(void)
{
    clearing = false;
    if (!read_bond() || (bonded && ble_gap_unpair(&peer) != 0) ||
        !read_bond() || bonded) {
        ESP_LOGE(TAG, "Unable to clear BLE bond");
        publish(BLE_ERROR);
        return;
    }
    open_window();
}

static uint16_t report_handle(uint8_t id)
{
    return id == REPORT_KEYBOARD ? keyboard_handle : consumer_handle;
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
    uint8_t zeros[8] = {0};
    int rc = notify(report_handle(active_report.id), zeros, active_report.size);
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

static int on_gap(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status != 0) {
            advertise();
            break;
        }
        conn = event->connect.conn_handle;
        session = (session + 1) & 0xffffff;
        publish(BLE_SECURING);
        int64_t remaining = adv_until - now_ms();
        if (remaining <= 0 || ble_gap_conn_find(conn, &desc) != 0 ||
            (bonded && !same_peer(&desc.peer_id_addr))) {
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
        if (conn != event->enc_change.conn_handle) {
            break;
        }
        if (clearing || event->enc_change.status != 0 || ble_gap_conn_find(conn, &desc) != 0 ||
            !desc.sec_state.encrypted || !desc.sec_state.bonded ||
            (bonded && !same_peer(&desc.peer_id_addr)) ||
            !read_bond() || !bonded || !same_peer(&desc.peer_id_addr)) {
            encrypted = false;
            publish(BLE_SECURING);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        ble_npl_callout_stop(&security_timer);
        encrypted = true;
        publish(BLE_CONNECTED);
        // The central can decline these parameters. Units are 1.25 ms and 10 ms.
        const struct ble_gap_upd_params params = {
            .itvl_min = 12, .itvl_max = 24, .latency = 9, .supervision_timeout = 400,
        };
        ble_gap_update_params(conn, &params);
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        bool trusted_link = encrypted || event->disconnect.conn.sec_state.bonded;
        ble_npl_callout_stop(&security_timer);
        ble_npl_callout_stop(&release_timer);
        conn = BLE_HS_CONN_HANDLE_NONE;
        subscriptions = 0;
        active_report = (page_report_t){0};
        encrypted = suspended = false;
        publish(BLE_IDLE);
        atomic_store(&pending_page, 0);
        if (clearing) {
            erase_bond();
        } else if (trusted_link && bonded) {
            open_window();
        } else {
            advertise();
        }
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE: {
        if (event->subscribe.conn_handle != conn) {
            break;
        }
        uint16_t attr = event->subscribe.attr_handle;
        unsigned flag = attr == keyboard_handle ? SNAP_KBD : attr == consumer_handle ? SNAP_CONSUMER : 0;
        if (event->subscribe.cur_notify) {
            subscriptions |= flag;
        } else {
            subscriptions &= ~flag;
        }
        if (encrypted) {
            publish(BLE_CONNECTED);
        }
        break;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        publish(BLE_IDLE);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // A host that forgot its keys needs a physical reset confirmation.
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:
        break;
    }
    return 0;
}

static void on_command(struct ble_npl_event *event)
{
    (void)event;
    unsigned requests = atomic_exchange(&commands, 0);
    if (!synced) {
        atomic_store(&pending_page, 0);
        return;
    }
    if (requests & CMD_BATTERY) {
        ble_svc_bas_battery_level_set((uint8_t)atomic_load(&battery_percent));
    }
    if ((requests & CMD_CLEAR) && !clearing) {
        clearing = true;
        if (ble_gap_adv_active()) {
            ble_gap_adv_stop();
        }
        adv_until = 0;
        encrypted = false;
        publish(BLE_SECURING);
        if (conn == BLE_HS_CONN_HANDLE_NONE) {
            erase_bond();
        } else {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
    } else if ((requests & CMD_RETRY) && !clearing && conn == BLE_HS_CONN_HANDLE_NONE &&
               !ble_gap_adv_active()) {
        open_window();
    }

    uint32_t command = atomic_load(&pending_page);
    if (command == 0 || active_report.id != 0) {
        return;
    }
    page_report_t report = page_report_for((page_profile_t)(command & 3), (command & 4) != 0,
                                           (page_action_t)((command >> 3) & 3));
    unsigned flag = report.id == REPORT_KEYBOARD ? SNAP_KBD : SNAP_CONSUMER;
    if (report.id != 0 && encrypted && !suspended && (subscriptions & flag) &&
        command >> SESSION_SHIFT == session &&
        notify(report_handle(report.id), report.bytes, report.size) == 0) {
        active_report = report;
        release_tries = 0;
        release_at = now_ms() + RELEASE_MS;
        ble_npl_callout_reset(&release_timer, ble_npl_time_ms_to_ticks32(RELEASE_MS));
    } else {
        atomic_store(&pending_page, 0);
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
    ble_npl_callout_stop(&security_timer);
    ble_npl_callout_stop(&release_timer);
    synced = clearing = encrypted = suspended = false;
    conn = BLE_HS_CONN_HANDLE_NONE;
    subscriptions = 0;
    active_report = (page_report_t){0};
    publish(BLE_STARTING);
    atomic_store(&pending_page, 0);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0 ||
        !read_bond()) {
        publish(BLE_ERROR);
        return;
    }
    synced = true;
    ble_svc_bas_battery_level_set((uint8_t)atomic_load(&battery_percent));
    if (bonded || !boot_window_opened) {
        boot_window_opened = true;
        open_window();
    } else {
        advertise();
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
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
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
        return ESP_FAIL;
    }
    ble_store_config_init();
    struct ble_npl_eventq *queue = nimble_port_get_dflt_eventq();
    ble_npl_event_init(&command_event, on_command, NULL);
    ble_npl_callout_init(&release_timer, queue, on_release, NULL);
    ble_npl_callout_init(&security_timer, queue, on_security_timeout, NULL);
    atomic_store(&started, true);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

page_ble_status_t page_ble_status(void)
{
    uint32_t value = atomic_load(&snapshot);
    return (page_ble_status_t){
        .state = value & SNAP_STATE, .bonded = (value & SNAP_BONDED) != 0,
        .session = value >> SESSION_SHIFT,
    };
}

static void command(unsigned operation)
{
    if (atomic_load(&started)) {
        atomic_fetch_or(&commands, operation);
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &command_event);
    }
}

void page_ble_retry(void)
{
    command(CMD_RETRY);
}

void page_ble_clear_pairing(void)
{
    command(CMD_CLEAR);
}

void page_ble_set_battery(unsigned percent)
{
    if (percent <= 100 && atomic_exchange(&battery_percent, percent) != percent) {
        command(CMD_BATTERY);
    }
}

bool page_ble_send(uint32_t expected_session, page_profile_t profile, bool invert, page_action_t action)
{
    uint32_t state = atomic_load(&snapshot);
    unsigned flag = profile == PROFILE_VOLUME ? SNAP_CONSUMER : SNAP_KBD;
    if ((action != PAGE_NEXT && action != PAGE_PREV) || (unsigned)profile >= PROFILE_COUNT ||
        (state & SNAP_STATE) != BLE_CONNECTED || !(state & flag) ||
        state >> SESSION_SHIFT != expected_session) {
        return false;
    }
    uint32_t empty = 0;
    uint32_t request = (expected_session << SESSION_SHIFT) | (action << 3) | (invert ? 4 : 0) | profile;
    if (!atomic_compare_exchange_strong(&pending_page, &empty, request)) {
        return false;
    }
    command(0);
    return true;
}
