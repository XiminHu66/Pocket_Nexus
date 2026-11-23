#pragma once

#include "controls.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PAGE_HOST_COUNT = 4, PAGE_HOST_NONE = 0xff, PAGE_SETTINGS_INVERT = 0x80 };

typedef enum {
    HOST_EMPTY, HOST_OTHER, HOST_PC, HOST_BOOX, HOST_IPHONE, HOST_LABEL_COUNT,
} page_host_label_t;

typedef struct {
    uint8_t type;
    uint8_t bytes[6];
} page_host_address_t;

typedef struct {
    page_host_address_t address;
    uint8_t label;
    uint8_t settings;
} page_host_t;

// Stored as bytes in NVS. Keep slot indices stable when a host is removed.
typedef struct {
    page_host_t hosts[PAGE_HOST_COUNT];
    uint8_t current;
    uint8_t previous;
    uint8_t fallback;
} page_host_book_t;

void page_hosts_init(page_host_book_t *book, uint8_t legacy_settings);
bool page_hosts_valid(const page_host_book_t *book);
bool page_host_exists(const page_host_book_t *book, uint8_t slot);
uint8_t page_host_find(const page_host_book_t *book, const page_host_address_t *address);
uint8_t page_host_first_free(const page_host_book_t *book);
uint8_t page_host_default(const page_host_book_t *book);
uint8_t page_host_return(const page_host_book_t *book, uint8_t target);
bool page_host_add(page_host_book_t *book, uint8_t slot, const page_host_address_t *address,
                   page_host_label_t label, uint8_t settings);
void page_host_remove(page_host_book_t *book, uint8_t slot);
void page_host_connected(page_host_book_t *book, uint8_t slot);
bool page_host_can_send(const page_host_book_t *book, uint8_t target,
                        const page_host_address_t *address, bool encrypted, bool bonded);
bool page_hosts_migrate_iphone(page_host_book_t *book);
uint8_t page_host_label_settings(page_host_label_t label, uint8_t fallback);
const char *page_host_label_name(page_host_label_t label);

#ifdef __cplusplus
}
#endif
