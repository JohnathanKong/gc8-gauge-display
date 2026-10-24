#include "gc8_ble.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {
constexpr char TAG[] = "gc8_ble";
// UUID a8e31c40-6b8f-4b67-a4f0-4a0b75000001; BLE_UUID128_INIT takes little-endian bytes.
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x00, 0x75, 0x0b, 0x4a, 0xf0, 0xa4,
    0x67, 0x4b, 0x8f, 0x6b, 0x40, 0x1c, 0xe3, 0xa8);
static const ble_uuid128_t write_uuid = BLE_UUID128_INIT(
    0x02, 0x00, 0x00, 0x75, 0x0b, 0x4a, 0xf0, 0xa4,
    0x67, 0x4b, 0x8f, 0x6b, 0x40, 0x1c, 0xe3, 0xa8);
static const ble_uuid128_t garage_event_uuid = BLE_UUID128_INIT(
    0x03, 0x00, 0x00, 0x75, 0x0b, 0x4a, 0xf0, 0xa4,
    0x67, 0x4b, 0x8f, 0x6b, 0x40, 0x1c, 0xe3, 0xa8);
std::atomic<bool> garage_subscribed{false};
std::atomic<uint16_t> garage_connection_handle{0};
uint16_t garage_event_value_handle = 0;
uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
Gc8BleSnapshot snapshot = {};

void show_garage_banner(const char *message) {
    portENTER_CRITICAL(&snapshot_lock);
    std::snprintf(snapshot.garage_banner, sizeof(snapshot.garage_banner),
                  "%s", message);
    snapshot.garage_banner_until_us = esp_timer_get_time() + 3500000LL;
    portEXIT_CRITICAL(&snapshot_lock);
}

// Only the last few meters of a turn can plausibly become misleading
// after Maps falls silent. Never expire a distant turn simply because the car
// is stopped or Samsung throttles its navigation notifications.
bool last_turn_is_near(const char *distance) {
    char *end = nullptr;
    const double n = std::strtod(distance, &end);
    if (end == distance || n < 0) return false;
    while (*end == ' ') ++end;
    double meters = n;
    if (std::strcmp(end, "m") == 0) {}
    else if (std::strcmp(end, "km") == 0) meters *= 1000.0;
    else if (std::strcmp(end, "ft") == 0) meters *= 0.3048;
    else if (std::strcmp(end, "mi") == 0) meters *= 1609.344;
    else return false;
    return meters <= 50.0;
}

Gc8BleSnapshot copy_snapshot() {
    portENTER_CRITICAL(&snapshot_lock);
    Gc8BleSnapshot result = snapshot;
    portEXIT_CRITICAL(&snapshot_lock);
    // A final 10m arrow must not remain displayed for minutes when Maps
    // never explicitly reports arrival. Keep the route active, but retire
    // the arrow after 20s without ANY new valid turn packet. A new turn
    // immediately restores the navigation UI. This is NOT an arrival claim.
    if (result.has_navigation && result.last_guidance_us > 0 &&
        last_turn_is_near(result.distance) &&
        esp_timer_get_time() - result.last_guidance_us >= 20000000LL) {
        result.has_navigation = false;
        result.guidance_stale = true;
    }
    return result;
}
void set_connection(bool connected) {
    portENTER_CRITICAL(&snapshot_lock);
    snapshot.connected = connected;
    if (!connected) {
        snapshot.has_navigation = false;
        snapshot.route_active = false;
        snapshot.guidance_stale = false;
        snapshot.last_guidance_us = 0;
        snapshot.has_demo_afr = false;
        snapshot.destination[0] = '\0';
    }
    portEXIT_CRITICAL(&snapshot_lock);
}
bool valid_ascii(const char *value) {
    for (; *value; ++value) {
        if (static_cast<unsigned char>(*value) < 32 ||
            static_cast<unsigned char>(*value) > 126) return false;
    }
    return true;
}
bool parse_test_message(char *buffer) {
    // A phone reply is a statement about request submission, never proof that
    // the garage door physically moved. The vendor integration is disabled
    // until explicitly configured by the owner.
    constexpr char kGarageResult[] = "GC8|1|GARAGE|RESULT|";
    if (std::strncmp(buffer, kGarageResult, sizeof(kGarageResult)-1) == 0) {
        const char *result = buffer + sizeof(kGarageResult)-1;
        if (std::strcmp(result, "NOT_READY") == 0) show_garage_banner("GARAGE NOT SET UP");
        else if (std::strcmp(result, "SENT") == 0) show_garage_banner("GARAGE SIGNAL SENT");
        else if (std::strcmp(result, "FAILED") == 0) show_garage_banner("GARAGE CMD FAILED");
        else return false;
        return true;
    }
    // Lower BOOT app launch is separate from garage control.
    // Replies only describe Android handoff, never that the app became ready.
    constexpr char kAppResult[] = "GC8|1|APP|RESULT|";
    if (std::strncmp(buffer, kAppResult, sizeof(kAppResult) - 1) == 0) {
        const char *result = buffer + sizeof(kAppResult) - 1;
        if (std::strcmp(result, "OPENED") == 0) show_garage_banner("APP OPEN REQUESTED");
        else if (std::strcmp(result, "ALERT") == 0) show_garage_banner("TAP PHONE NOTICE");
        else if (std::strcmp(result, "SETUP") == 0) show_garage_banner("SET BOOT APP");
        else if (std::strcmp(result, "FAILED") == 0) show_garage_banner("APP ACTION FAILED");
        else return false;
        return true;
    }
    // Phone-local clock sync; no ESP32 Wi-Fi, GPS or RTC required.
    // GC8|1|TIME|HH:MM:SS|12 (or |24). Re-sync from Android every minute
    // to follow user clock preferences, daylight saving and timezone changes.
    constexpr char kTimePrefix[] = "GC8|1|TIME|";
    if (std::strncmp(buffer, kTimePrefix, sizeof(kTimePrefix) - 1) == 0) {
        const char *p = buffer + sizeof(kTimePrefix) - 1;
        const size_t n = std::strlen(p);
        if (n < 11 || n > 15 || p[2] != ':' || p[5] != ':' || p[8] != '|' ||
            ((p[9] != '1' || p[10] != '2') && (p[9] != '2' || p[10] != '4')))
            return false;
        // Accept an old Android TIME packet's optional phone percentage
        // for backward compatibility, but NEVER display or retain it:
        // the icon now reads the Waveshare AXP2101 directly.
        int legacy_phone_percent = 0;
        if (n > 11) {
            if (p[11] != '|' || n == 12) return false;
            for (size_t i = 12; i < n; ++i) {
                if (p[i] < '0' || p[i] > '9') return false;
                legacy_phone_percent = legacy_phone_percent * 10 + (p[i] - '0');
            }
            if (legacy_phone_percent > 100) return false;
        }
        const int positions[] = {0, 1, 3, 4, 6, 7};
        for (const int i : positions) {
            if (p[i] < '0' || p[i] > '9') return false;
        }
        const int hour = (p[0] - '0') * 10 + p[1] - '0';
        const int minute = (p[3] - '0') * 10 + p[4] - '0';
        const int second = (p[6] - '0') * 10 + p[7] - '0';
        if (hour >= 24 || minute >= 60 || second >= 60) return false;
        portENTER_CRITICAL(&snapshot_lock);
        snapshot.clock_seconds = hour * 3600 + minute * 60 + second;
        snapshot.clock_24h = p[9] == '2';
        snapshot.clock_synced_us = esp_timer_get_time();
        snapshot.has_clock = true;
        portEXIT_CRITICAL(&snapshot_lock);
        return true;
    }

    // Explicit near-destination guidance timeout, not a claim of arrival.
    // The real route remains active until Maps actually ends its notification.
    if (std::strcmp(buffer, "GC8|1|HOLD") == 0) {
        portENTER_CRITICAL(&snapshot_lock);
        snapshot.has_navigation = false;
        snapshot.route_active = true;
        snapshot.guidance_stale = true;
        snapshot.last_guidance_us = 0;
        snapshot.maneuver[0] = '\0';
        snapshot.distance[0] = '\0';
        snapshot.road[0] = '\0';
        portEXIT_CRITICAL(&snapshot_lock);
        return true;
    }
    // Notification-only mode: show "Getting directions..." immediately
    // after Maps starts navigation, without capturing the destination.
    if (std::strcmp(buffer, "GC8|1|START") == 0) {
        portENTER_CRITICAL(&snapshot_lock);
        snapshot.route_active = true;
        snapshot.has_navigation = false;
        snapshot.guidance_stale = false;
        snapshot.last_guidance_us = 0;
        snapshot.has_demo_afr = false;
        snapshot.maneuver[0] = '\0';
        snapshot.distance[0] = '\0';
        snapshot.road[0] = '\0';
        snapshot.destination[0] = '\0';
        portEXIT_CRITICAL(&snapshot_lock);
        return true;
    }
    if (std::strcmp(buffer, "GC8|1|CLEAR") == 0) {
        portENTER_CRITICAL(&snapshot_lock);
        snapshot.has_navigation = false;
        snapshot.route_active = false;
        snapshot.guidance_stale = false;
        snapshot.last_guidance_us = 0;
        snapshot.has_demo_afr = false;
        snapshot.maneuver[0] = '\0';
        snapshot.distance[0] = '\0';
        snapshot.road[0] = '\0';
        snapshot.destination[0] = '\0';
        portEXIT_CRITICAL(&snapshot_lock);
        return true;
    }
    // Destination is independent of guidance. Google Maps often starts with
    // "0 m" or an incomplete instruction, sometimes for many seconds.
    // GC8|1|DEST|<selected place> immediately identifies the active route
    // without manufacturing a turn, distance, or street.
    constexpr char kDestinationPrefix[] = "GC8|1|DEST|";
    if (std::strncmp(buffer, kDestinationPrefix,
                     sizeof(kDestinationPrefix) - 1) == 0) {
        const char *place = buffer + sizeof(kDestinationPrefix) - 1;
        if (!*place || !valid_ascii(place) ||
            std::strchr(place, '|') != nullptr ||
            std::strlen(place) >= sizeof(snapshot.destination)) return false;
        portENTER_CRITICAL(&snapshot_lock);
        std::snprintf(snapshot.destination, sizeof(snapshot.destination), "%s", place);
        snapshot.route_active = true;
        snapshot.has_navigation = false;
        snapshot.guidance_stale = false;
        snapshot.last_guidance_us = 0;
        snapshot.has_demo_afr = false;
        snapshot.maneuver[0] = '\0';
        snapshot.distance[0] = '\0';
        snapshot.road[0] = '\0';
        portEXIT_CRITICAL(&snapshot_lock);
        return true;
    }
    // Strict, versioned, pipe-separated ASCII message. Reject malformed data.
    char *parts[5] = {};
    parts[0] = buffer;
    for (int i = 1; i < 5; ++i) {
        char *delimiter = std::strchr(parts[i - 1], '|');
        if (!delimiter) return false;
        *delimiter = '\0';
        parts[i] = delimiter + 1;
    }
    // Optional sixth field carries the final destination. Older five-field
    // test messages remain supported.
    char *destination = std::strchr(parts[4], '|');
    char *demo_afr = nullptr;
    if (destination) {
        *destination = '\0';
        ++destination;
        demo_afr = std::strchr(destination, '|');
        if (demo_afr) { *demo_afr = '\0'; ++demo_afr; }
        if (!valid_ascii(destination) ||
            std::strlen(destination) >= sizeof(snapshot.destination)) return false;
    }
    // A seventh field is allowed only as an explicitly marked DEMO value.
    // Never interpret this as a measured AFR.
    if (demo_afr) {
        if (std::strncmp(demo_afr, "DEMO:", 5) != 0) return false;
        const char *value = demo_afr + 5;
        if (!*value || std::strlen(value) >= sizeof(snapshot.demo_afr)) return false;
        int dots = 0;
        for (const char *p = value; *p; ++p) {
            if (*p == '.') { if (++dots > 1) return false; }
            else if (*p < '0' || *p > '9') return false;
        }
    }
    if (std::strcmp(parts[0], "GC8") ||
        std::strcmp(parts[1], "1") || !valid_ascii(parts[2]) ||
        !valid_ascii(parts[3]) || !valid_ascii(parts[4])) return false;
    if (std::strcmp(parts[2], "RIGHT") && std::strcmp(parts[2], "LEFT") &&
        std::strcmp(parts[2], "STRAIGHT")) return false;
    if (!*parts[3] || !*parts[4] || std::strlen(parts[3]) >= 20 ||
        std::strlen(parts[4]) >= 48) return false;
    portENTER_CRITICAL(&snapshot_lock);
    std::snprintf(snapshot.maneuver, sizeof(snapshot.maneuver), "%s", parts[2]);
    std::snprintf(snapshot.distance, sizeof(snapshot.distance), "%s", parts[3]);
    std::snprintf(snapshot.road, sizeof(snapshot.road), "%s", parts[4]);
    std::snprintf(snapshot.destination, sizeof(snapshot.destination), "%s", destination ? destination : "");
    snapshot.has_demo_afr = demo_afr != nullptr;
    std::snprintf(snapshot.demo_afr, sizeof(snapshot.demo_afr), "%s", demo_afr ? demo_afr + 5 : "");
    snapshot.has_navigation = true;
    snapshot.route_active = true;
    snapshot.guidance_stale = false;
    snapshot.last_guidance_us = esp_timer_get_time();
    portEXIT_CRITICAL(&snapshot_lock);
    return true;
}

int on_write(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    const auto length = OS_MBUF_PKTLEN(ctx->om);
    if (length == 0 || length > 100) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char buffer[101] = {};
    if (ble_hs_mbuf_to_flat(ctx->om, buffer, length, nullptr) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!parse_test_message(buffer)) return BLE_ATT_ERR_UNLIKELY;
    ESP_LOGI(TAG, "Accepted GC8 BLE packet (%u bytes)", static_cast<unsigned>(length));
    return 0;
}

// NimBLE validates an access callback for every GATT characteristic, even
// notify-only ones. This characteristic is never client-readable/writable;
// its 18-byte button events are sent with ble_gatts_notify_custom().
int on_garage_notification_access(uint16_t, uint16_t,
                                  ble_gatt_access_ctxt *ctx, void *) {
    return ctx->op == BLE_GATT_ACCESS_OP_READ_CHR
        ? BLE_ATT_ERR_READ_NOT_PERMITTED
        : BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

// Zero-initialize NimBLE C structs; C++ -Wmissing-field-initializers is an error.
ble_gatt_chr_def characteristics[3] = {};
ble_gatt_svc_def services[2] = {};
void configure_gatt() {
    characteristics[0].uuid = &write_uuid.u;
    characteristics[0].access_cb = on_write;
    characteristics[0].flags = BLE_GATT_CHR_F_WRITE;
    // Add a separate notify-only characteristic; existing WRITE_UUID and
    // its navigation/clock protocol remain unchanged and backwards-compatible.
    characteristics[1].uuid = &garage_event_uuid.u;
    characteristics[1].access_cb = on_garage_notification_access;
    characteristics[1].flags = BLE_GATT_CHR_F_NOTIFY;
    characteristics[1].val_handle = &garage_event_value_handle;
    services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid = &service_uuid.u;
    services[0].characteristics = characteristics;
}

void advertise();
int gap_event(ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE connect status=%d", event->connect.status);
            garage_subscribed.store(false);
            if (event->connect.status == 0) {
                garage_connection_handle.store(event->connect.conn_handle);
                set_connection(true);
            } else advertise();
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected");
            garage_subscribed.store(false);
            set_connection(false);
            advertise();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == garage_event_value_handle) {
                garage_subscribed.store(event->subscribe.cur_notify != 0);
                ESP_LOGI(TAG, "Garage PWR event notify subscribed=%d",
                         static_cast<int>(garage_subscribed.load()));
            }
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            return 0;
        default:
            return 0;
    }
}
void advertise() {
    ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = const_cast<ble_uuid128_t *>(&service_uuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "Advertising fields error=%d", rc); return; }

    ble_hs_adv_fields response = {};
    const char name[] = "GC8 Display";
    response.name = reinterpret_cast<const uint8_t *>(name);
    response.name_len = sizeof(name) - 1;
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) { ESP_LOGE(TAG, "Scan response error=%d", rc); return; }

    ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &params, gap_event, nullptr);
    if (rc != 0) ESP_LOGE(TAG, "Advertising start error=%d", rc);
    else ESP_LOGI(TAG, "Advertising GC8 service");
}
void synced() {
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "BLE address error=%d", rc); return; }
    advertise();
}
void host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
}  // namespace

void gc8_ble_garage_button_pressed() {
    if (!garage_subscribed.load() || !gc8_ble_snapshot().connected ||
        garage_event_value_handle == 0) {
        show_garage_banner("GARAGE: PHONE OFFLINE");
        ESP_LOGW(TAG, "PWR pressed but Companion is not subscribed");
        return;
    }
    // A short 18-byte signal fits the minimum BLE MTU. It is an event ONLY:
    // Meross credentials and garage door control never live on the ESP32.
    constexpr char request[] = "GC8|1|GARAGE|PRESS";
    auto *packet = ble_hs_mbuf_from_flat(request, sizeof(request) - 1);
    if (!packet) {
        show_garage_banner("GARAGE: BLE ERROR");
        return;
    }
    const int rc = ble_gatts_notify_custom(
        garage_connection_handle.load(), garage_event_value_handle, packet);
    if (rc == 0) {
        show_garage_banner("GARAGE REQUESTED");
        ESP_LOGI(TAG, "Garage button event delivered to BLE stack");
    } else {
        show_garage_banner("GARAGE: BLE ERROR");
        ESP_LOGW(TAG, "Garage BLE notification failed: %d", rc);
    }
}

void gc8_ble_app_button_pressed() {
    if (!garage_subscribed.load() || !gc8_ble_snapshot().connected ||
        garage_event_value_handle == 0) {
        show_garage_banner("APP: PHONE OFFLINE");
        ESP_LOGW(TAG, "BOOT pressed but Companion is not subscribed");
        return;
    }
    constexpr char event[] = "GC8|1|APP|PRESS";
    auto *packet = ble_hs_mbuf_from_flat(event, sizeof(event) - 1);
    if (!packet) {
        show_garage_banner("APP: BLE ERROR");
        return;
    }
    const int rc = ble_gatts_notify_custom(
        garage_connection_handle.load(), garage_event_value_handle, packet);
    if (rc == 0) {
        show_garage_banner("APP REQUESTED");
        ESP_LOGI(TAG, "BOOT app event delivered to BLE stack");
    } else {
        show_garage_banner("APP: BLE ERROR");
        ESP_LOGW(TAG, "BOOT BLE notification failed: %d", rc);
    }
}

void gc8_ble_start() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("GC8 Display");
    configure_gatt();
    int rc = ble_gatts_count_cfg(services);
    if (rc == 0) rc = ble_gatts_add_svcs(services);
    if (rc != 0) { ESP_LOGE(TAG, "GATT registration failed: %d", rc); return; }
    ble_hs_cfg.sync_cb = synced;
    nimble_port_freertos_init(host_task);
}

Gc8BleSnapshot gc8_ble_snapshot() {
    return copy_snapshot();
}
