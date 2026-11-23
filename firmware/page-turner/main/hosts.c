#include "hosts.h"

#include <string.h>

_Static_assert(sizeof(page_host_book_t) == 39, "NVS host book layout must stay stable");

static bool valid_settings(uint8_t settings)
{
    return (settings & ~PAGE_SETTINGS_INVERT) < PROFILE_COUNT;
}

static bool same_address(const page_host_address_t *a, const page_host_address_t *b)
{
    // NimBLE reports public/random identity types 2/3 for some resolved RPAs.
    return ((a->type ^ b->type) & 1) == 0 && memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

void page_hosts_init(page_host_book_t *book, uint8_t legacy_settings)
{
    *book = (page_host_book_t){
        .current = PAGE_HOST_NONE, .previous = PAGE_HOST_NONE,
        .fallback = valid_settings(legacy_settings) ? legacy_settings : PROFILE_PPT,
    };
}

bool page_host_exists(const page_host_book_t *book, uint8_t slot)
{
    return slot < PAGE_HOST_COUNT && book->hosts[slot].label != HOST_EMPTY;
}

bool page_hosts_valid(const page_host_book_t *book)
{
    if (!valid_settings(book->fallback) ||
        (book->current != PAGE_HOST_NONE && !page_host_exists(book, book->current)) ||
        (book->previous != PAGE_HOST_NONE &&
         (!page_host_exists(book, book->previous) || book->previous == book->current))) {
        return false;
    }
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        const page_host_t *host = &book->hosts[slot];
        if (host->label != HOST_EMPTY &&
            (host->label >= HOST_LABEL_COUNT || host->address.type > 1 ||
             !valid_settings(host->settings) || page_host_find(book, &host->address) != slot)) {
            return false;
        }
    }
    return true;
}

uint8_t page_host_find(const page_host_book_t *book, const page_host_address_t *address)
{
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        if (page_host_exists(book, slot) && same_address(address, &book->hosts[slot].address)) {
            return slot;
        }
    }
    return PAGE_HOST_NONE;
}

uint8_t page_host_first_free(const page_host_book_t *book)
{
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        if (!page_host_exists(book, slot)) {
            return slot;
        }
    }
    return PAGE_HOST_NONE;
}

uint8_t page_host_default(const page_host_book_t *book)
{
    if (page_host_exists(book, book->current)) {
        return book->current;
    }
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        if (page_host_exists(book, slot)) {
            return slot;
        }
    }
    return PAGE_HOST_NONE;
}

uint8_t page_host_return(const page_host_book_t *book, uint8_t target)
{
    uint8_t candidate = target == book->current ? book->previous : book->current;
    return page_host_exists(book, candidate) ? candidate : PAGE_HOST_NONE;
}

bool page_host_add(page_host_book_t *book, uint8_t slot, const page_host_address_t *address,
                   page_host_label_t label, uint8_t settings)
{
    if (slot >= PAGE_HOST_COUNT || page_host_exists(book, slot) ||
        page_host_find(book, address) != PAGE_HOST_NONE ||
        label <= HOST_EMPTY || label >= HOST_LABEL_COUNT || !valid_settings(settings)) {
        return false;
    }
    book->hosts[slot] = (page_host_t){ .address = *address, .label = label, .settings = settings };
    book->hosts[slot].address.type &= 1;
    return true;
}

void page_host_remove(page_host_book_t *book, uint8_t slot)
{
    if (!page_host_exists(book, slot)) {
        return;
    }
    book->hosts[slot] = (page_host_t){0};
    if (book->current == slot) {
        book->current = book->previous;
        book->previous = PAGE_HOST_NONE;
    } else if (book->previous == slot) {
        book->previous = PAGE_HOST_NONE;
    }
}

void page_host_connected(page_host_book_t *book, uint8_t slot)
{
    if (page_host_exists(book, slot) && book->current != slot) {
        book->previous = book->current;
        book->current = slot;
    }
}

bool page_host_can_send(const page_host_book_t *book, uint8_t target,
                        const page_host_address_t *address, bool encrypted, bool bonded)
{
    return encrypted && bonded && target < PAGE_HOST_COUNT && page_host_find(book, address) == target;
}

bool page_hosts_migrate_iphone(page_host_book_t *book)
{
    bool changed = false;
    for (uint8_t slot = 0; slot < PAGE_HOST_COUNT; ++slot) {
        page_host_t *host = &book->hosts[slot];
        if (host->label != HOST_IPHONE) {
            continue;
        }
        uint8_t profile = host->settings & ~PAGE_SETTINGS_INVERT;
        uint8_t direction = host->settings & PAGE_SETTINGS_INVERT;
        if (profile == PROFILE_MANGA) {
            host->settings = PROFILE_IPHONE_HORIZONTAL | direction;
            changed = true;
        } else if (profile == PROFILE_VIDEO_WHEEL || profile == PROFILE_VIDEO_KEYS ||
                   profile == PROFILE_MANGA_SCROLL) {
            host->settings = PROFILE_IPHONE_VERTICAL | direction;
            changed = true;
        }
    }
    return changed;
}

uint8_t page_host_label_settings(page_host_label_t label, uint8_t fallback)
{
    switch (label) {
    case HOST_PC: return PROFILE_PPT;
    case HOST_BOOX: return PROFILE_BOOX;
    case HOST_IPHONE: return PROFILE_IPHONE_VERTICAL;
    default: return fallback;
    }
}

const char *page_host_label_name(page_host_label_t label)
{
    static const char *const names[] = { "", "DEVICE", "PC", "BOOX", "IPHONE" };
    return (unsigned)label < HOST_LABEL_COUNT ? names[label] : "";
}
