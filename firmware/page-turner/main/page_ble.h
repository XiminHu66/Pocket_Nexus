#pragma once

#include "esp_err.h"
#include "hosts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_BLE_NAME "StickS3 Page"

typedef enum {
    BLE_STARTING, BLE_PAIRING, BLE_RECONNECTING, BLE_SECURING, BLE_CONNECTED, BLE_IDLE,
    BLE_SWITCHING, BLE_RETRY_OR_BACK, BLE_ERROR,
} page_ble_state_t;

typedef struct {
    page_ble_state_t state;
    uint32_t session;
    uint8_t selected;
    uint8_t previous;
    uint8_t settings;
    uint8_t labels[PAGE_HOST_COUNT];
    uint8_t target_label;
    uint8_t reports;
    bool storage_failed;
} page_ble_status_t;

esp_err_t page_ble_start(void);
page_ble_status_t page_ble_status(void);
bool page_ble_select(uint8_t slot);
bool page_ble_add(page_host_label_t label);
bool page_ble_forget(uint8_t slot);
bool page_ble_rename(uint8_t slot, page_host_label_t label);
bool page_ble_configure(uint8_t slot, page_profile_t profile, bool invert);
void page_ble_retry(void);
bool page_ble_send(uint32_t session, page_profile_t profile, bool invert, page_action_t action);
void page_ble_set_battery(unsigned percent);

#ifdef __cplusplus
}
#endif
