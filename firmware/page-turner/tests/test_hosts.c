#include "hosts.h"

#include <assert.h>
#include <stdio.h>

static const page_host_address_t pc = { 0, { 1, 2, 3, 4, 5, 6 } };
static const page_host_address_t boox = { 1, { 3, 4, 5, 6, 7, 8 } };
static const page_host_address_t phone = { 1, { 12, 13, 14, 15, 16, 17 } };
static const page_host_address_t spare = { 0, { 21, 22, 23, 24, 25, 26 } };

static void test_migration(void)
{
    const uint8_t legacy = PROFILE_BOOX | PAGE_SETTINGS_INVERT;
    page_host_book_t book;
    page_hosts_init(&book, legacy);
    assert(page_hosts_valid(&book));
    assert(book.fallback == legacy);
    assert(page_host_default(&book) == PAGE_HOST_NONE);
    assert(page_host_add(&book, page_host_first_free(&book), &boox, HOST_OTHER, book.fallback));
    assert(page_host_default(&book) == 0);
    page_host_connected(&book, 0);
    assert(book.hosts[0].settings == legacy);
    assert(book.current == 0 && book.previous == PAGE_HOST_NONE);
    assert(page_hosts_valid(&book));
}

static void test_switch_sequence(void)
{
    page_host_book_t book;
    page_hosts_init(&book, PROFILE_PPT);
    assert(page_host_add(&book, 0, &pc, HOST_PC, page_host_label_settings(HOST_PC, book.fallback)));
    assert(page_host_add(&book, 1, &boox, HOST_BOOX, page_host_label_settings(HOST_BOOX, book.fallback)));
    assert(page_host_add(&book, 2, &phone, HOST_IPHONE, page_host_label_settings(HOST_IPHONE, book.fallback)));
    assert(page_host_add(&book, 3, &spare, HOST_OTHER, PROFILE_MANGA_SCROLL | PAGE_SETTINGS_INVERT));
    assert(page_host_first_free(&book) == PAGE_HOST_NONE);
    assert(!page_host_add(&book, 0, &spare, HOST_PC, PROFILE_PPT));
    assert(!page_host_add(&book, PAGE_HOST_NONE, &pc, HOST_PC, PROFILE_PPT));

    page_host_connected(&book, 0);
    assert(page_host_return(&book, 1) == 0);
    assert(!page_host_can_send(&book, 1, &pc, true, true));
    assert(!page_host_can_send(&book, 1, &boox, false, true));
    assert(!page_host_can_send(&book, 1, &boox, true, false));
    assert(!page_host_can_send(&book, PAGE_HOST_NONE, &boox, true, true));
    assert(page_host_can_send(&book, 1, &boox, true, true));
    page_host_connected(&book, 1);
    assert(page_host_return(&book, 1) == 0);
    assert(page_host_return(&book, 2) == 1); // B from a failed switch to iPhone
    assert(page_host_return(&book, PAGE_HOST_NONE) == 1); // B from a failed add

    page_host_address_t resolved_phone = phone;
    resolved_phone.type = 3;
    assert(page_host_can_send(&book, 2, &resolved_phone, true, true));
    page_host_connected(&book, 2);
    assert(page_host_default(&book) == 2 && book.previous == 1);
    page_host_connected(&book, 1);
    assert(page_host_default(&book) == 1 && book.previous == 2);
    page_host_connected(&book, 1); // reconnect preserves the previous device
    assert(book.previous == 2);

    assert(book.hosts[0].settings == PROFILE_PPT);
    assert(book.hosts[1].settings == PROFILE_BOOX);
    assert(book.hosts[2].settings == PROFILE_IPHONE_VERTICAL);
    assert(book.hosts[3].settings == (PROFILE_MANGA_SCROLL | PAGE_SETTINGS_INVERT));

    page_host_remove(&book, 1);
    assert(page_host_first_free(&book) == 1);
    assert(page_host_default(&book) == 2 && book.previous == PAGE_HOST_NONE);
    assert(!page_host_can_send(&book, 1, &boox, true, true));
    assert(!page_host_add(&book, 1, &resolved_phone, HOST_IPHONE, PROFILE_VIDEO_WHEEL));
    assert(page_host_add(&book, 1, &boox, HOST_BOOX, PROFILE_BOOX));
    assert(page_host_find(&book, &spare) == 3);
    assert(page_hosts_valid(&book));

    book.hosts[3].settings = 0xff;
    assert(!page_hosts_valid(&book));
}

static void test_iphone_profile_upgrade(void)
{
    page_host_book_t book;
    page_hosts_init(&book, PROFILE_PPT);
    assert(page_host_add(&book, 0, &pc, HOST_PC, PROFILE_MANGA));
    assert(page_host_add(&book, 1, &boox, HOST_BOOX, PROFILE_BOOX));
    assert(page_host_add(&book, 2, &phone, HOST_IPHONE, PROFILE_VIDEO_WHEEL));
    assert(page_host_add(&book, 3, &spare, HOST_IPHONE, PROFILE_MANGA | PAGE_SETTINGS_INVERT));
    page_host_connected(&book, 2);
    page_host_connected(&book, 0);
    assert(page_hosts_migrate_iphone(&book));
    assert(book.hosts[0].settings == PROFILE_MANGA && book.hosts[1].settings == PROFILE_BOOX);
    assert(book.hosts[2].settings == PROFILE_IPHONE_VERTICAL);
    assert(book.hosts[3].settings == (PROFILE_IPHONE_HORIZONTAL | PAGE_SETTINGS_INVERT));
    assert(book.current == 0 && book.previous == 2);
    assert(page_host_can_send(&book, 2, &phone, true, true));
    assert(!page_hosts_migrate_iphone(&book));
    assert(page_hosts_valid(&book));

    book.hosts[2].settings = PROFILE_IPHONE_SCROLL | PAGE_SETTINGS_INVERT;
    assert(page_hosts_valid(&book));
    assert(!page_hosts_migrate_iphone(&book));
    page_host_connected(&book, 2);
    page_host_connected(&book, 0);
    assert(book.hosts[2].settings == (PROFILE_IPHONE_SCROLL | PAGE_SETTINGS_INVERT));
    assert(book.hosts[0].settings == PROFILE_MANGA && book.hosts[1].settings == PROFILE_BOOX);
}

int main(void)
{
    test_migration();
    test_switch_sequence();
    test_iphone_profile_upgrade();
    puts("PASS: legacy settings, iPhone profile upgrade, four hosts, per-host modes, recent switch, fail-back, identity routing and removal");
    return 0;
}
