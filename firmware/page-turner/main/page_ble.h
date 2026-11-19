#pragma once

#include "controls.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_BLE_NAME "StickS3 Page"

typedef enum {
    BLE_STARTING, BLE_PAIRING, BLE_RECONNECTING, BLE_SECURING, BLE_CONNECTED, BLE_IDLE, BLE_ERROR
} page_ble_state_t;

typedef struct {
    page_ble_state_t state;
    bool bonded;
    uint32_t session;
} page_ble_status_t;

esp_err_t page_ble_start(void);
page_ble_status_t page_ble_status(void);
void page_ble_retry(void);
void page_ble_clear_pairing(void);
bool page_ble_send(uint32_t session, page_profile_t profile, bool invert, page_action_t action);
void page_ble_set_battery(unsigned percent);

#ifdef __cplusplus
}
#endif
