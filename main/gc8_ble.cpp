#include "gc8_ble.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#include "store/config/ble_store_config.h"

// ESP-IDF NimBLE implements this C function but does not declare it in its
// public headers; follow Espressif's HID example with explicit C linkage.
extern "C" void ble_store_config_init(void);

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
// Standard HID-over-GATT Consumer Control (0x1812). Android handles this
// input itself, even when GC8 Companion is not the foreground Activity.
static const ble_uuid16_t hid_service_uuid = BLE_UUID16_INIT(0x1812);
// HID-over-GATT hosts also discover the Device Information PnP ID.
static const ble_uuid16_t device_info_uuid = BLE_UUID16_INIT(0x180A);
static const ble_uuid16_t pnp_id_uuid = BLE_UUID16_INIT(0x2A50);
static const ble_uuid16_t hid_info_uuid = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t hid_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t hid_control_uuid = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t hid_report_uuid = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t hid_protocol_uuid = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t hid_report_ref_uuid = BLE_UUID16_INIT(0x2908);

// Report ID 1, one Consumer Control bit: Usage 0x0CF (Voice Command).
// The GATT Report value is 0x01 while pressed, 0x00 on release; Report ID
// lives in the Report Reference descriptor, not in the GATT value bytes.
constexpr uint8_t hid_report_map[] = {
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  // Report ID 1
    0x09, 0xCF,  // Usage (Voice Command)
    0x15, 0x00,  // Logical Min 0
    0x25, 0x01,  // Logical Max 1
    0x75, 0x01,  // Report Size 1
    0x95, 0x01,  // Report Count 1
    0x81, 0x02,  // Input: variable, absolute
    0x75, 0x07,  // 7 padding bits
    0x95, 0x01,
    0x81, 0x03,  // Input: constant padding
    0xC0,        // End Voice Command collection
    // A separate MEDIA_PLAY_PAUSE report for Android's documented headset
    // long-press voice-search path. Do not replace the working voice key.
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x02,  // Report ID 2; own GATT Report characteristic
    0x09, 0xCD,  // Usage (Play/Pause)
    0x15, 0x00,  // Logical Min 0
    0x25, 0x01,  // Logical Max 1
    0x75, 0x01,  // Report Size 1
    0x95, 0x01,  // Report Count 1
    0x81, 0x02,  // Input: variable, absolute
    0x75, 0x07,  // 7 padding bits
    0x95, 0x01,
    0x81, 0x03,  // Input: constant padding
    0xC0         // End media collection
};
constexpr uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x02}; // HID 1.11, normally connectable
constexpr uint8_t hid_report_reference[] = {0x01, 0x01}; // ID 1, input report
constexpr uint8_t hid_media_report_reference[] = {0x02, 0x01}; // ID 2, input report
// USB-IF vendor Espressif 0x303A (ESP32-S3 silicon), product 0x0001,
// firmware rev 0x0100. Describes the private GC8 HID function, not a USB port.
constexpr uint8_t hid_pnp_id[] = {0x02, 0x3A, 0x30, 0x01, 0x00, 0x00, 0x01};
std::atomic<bool> hid_subscribed{false};
std::atomic<bool> hid_media_subscribed{false};
std::atomic<bool> media_key_busy{false};
std::atomic<uint16_t> active_ble_connections{0};
std::atomic<uint32_t> total_ble_connections{0};
std::atomic<uint32_t> total_ble_disconnects{0};
std::atomic<int> last_ble_disconnect_reason{-1};
std::atomic<uint32_t> submitted_voice_reports{0};
std::atomic<uint32_t> submitted_media_reports{0};
std::atomic<uint16_t> hid_connection_handle{0};
uint16_t hid_report_value_handle = 0;
uint16_t hid_media_report_value_handle = 0;
std::atomic<uint16_t> hid_media_connection_handle{0};
uint8_t hid_protocol_mode = 1;
uint8_t hid_current_report = 0;
uint8_t hid_media_current_report = 0;
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
        snapshot.guidance_title[0] = '\0';
        snapshot.guidance_hint[0] = '\0';
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
        snapshot.guidance_title[0] = '\0';
        snapshot.guidance_hint[0] = '\0';
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
        snapshot.guidance_title[0] = '\0';
        snapshot.guidance_hint[0] = '\0';
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
        snapshot.guidance_title[0] = '\0';
        snapshot.guidance_hint[0] = '\0';
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
        snapshot.guidance_title[0] = '\0';
        snapshot.guidance_hint[0] = '\0';
        portEXIT_CRITICAL(&snapshot_lock);
        return true;
    }
    // Text-only Maps guidance for cases with no reliable turn bearing or
    // distance. Supported only when the phone explicitly reports this text:
    // GC8|1|INFO|EXIT PARKING LOT|TOWARD COURTYARD LN
    // GC8|1|INFO|DESTINATION RIGHT|FOLLOW GOOGLE MAPS
    // This NEVER asserts arrival, and never manufactures a turn arrow.
    constexpr char kInfoPrefix[] = "GC8|1|INFO|";
    if (std::strncmp(buffer, kInfoPrefix, sizeof(kInfoPrefix) - 1) == 0) {
        char *heading = buffer + sizeof(kInfoPrefix) - 1;
        char *separator = std::strchr(heading, '|');
        if (!separator) return false;
        *separator = '\0';
        const char *hint = separator + 1;
        if (!*heading || !valid_ascii(heading) || !valid_ascii(hint) ||
            std::strchr(hint, '|') != nullptr ||
            std::strlen(heading) >= sizeof(snapshot.guidance_title) ||
            std::strlen(hint) >= sizeof(snapshot.guidance_hint)) return false;
        portENTER_CRITICAL(&snapshot_lock);
        std::snprintf(snapshot.guidance_title, sizeof(snapshot.guidance_title),
                      "%s", heading);
        std::snprintf(snapshot.guidance_hint, sizeof(snapshot.guidance_hint),
                      "%s", hint);
        snapshot.has_navigation = false;
        snapshot.route_active = true;
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
    snapshot.guidance_title[0] = '\0';
    snapshot.guidance_hint[0] = '\0';
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

// HID-only GATT characteristic handlers. The GC8 app service remains untouched.
int hid_append(ble_gatt_access_ctxt *ctx, const uint8_t *bytes, size_t length) {
    return os_mbuf_append(ctx->om, bytes, length) == 0
        ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
int hid_info_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, hid_info, sizeof(hid_info));
}
int hid_report_map_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, hid_report_map, sizeof(hid_report_map));
}
int hid_report_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, &hid_current_report, sizeof(hid_current_report));
}
int hid_media_report_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, &hid_media_current_report, sizeof(hid_media_current_report));
}
int hid_report_ref_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, hid_report_reference, sizeof(hid_report_reference));
}
int hid_media_report_ref_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, hid_media_report_reference, sizeof(hid_media_report_reference));
}
int hid_protocol_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return hid_append(ctx, &hid_protocol_mode, 1);
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR || OS_MBUF_PKTLEN(ctx->om) != 1)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t requested = 0;
    if (ble_hs_mbuf_to_flat(ctx->om, &requested, 1, nullptr) != 0 || requested > 1)
        return BLE_ATT_ERR_UNLIKELY;
    // Consumer Control has no Boot Protocol report.
    if (requested == 0) return BLE_ATT_ERR_UNLIKELY;
    hid_protocol_mode = requested;
    return 0;
}
int hid_control_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR || OS_MBUF_PKTLEN(ctx->om) != 1)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t value = 0;
    if (ble_hs_mbuf_to_flat(ctx->om, &value, 1, nullptr) != 0 || value > 1)
        return BLE_ATT_ERR_UNLIKELY;
    return 0;
}
int hid_pnp_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    return hid_append(ctx, hid_pnp_id, sizeof(hid_pnp_id));
}
ble_gatt_chr_def device_info_characteristics[2] = {};
ble_gatt_dsc_def hid_report_descriptors[2] = {};
ble_gatt_dsc_def hid_media_report_descriptors[2] = {};
ble_gatt_chr_def hid_characteristics[7] = {};

// Zero-initialize NimBLE C structs; C++ -Wmissing-field-initializers is an error.
ble_gatt_chr_def characteristics[3] = {};
ble_gatt_svc_def services[4] = {};
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

    hid_report_descriptors[0].uuid = &hid_report_ref_uuid.u;
    hid_report_descriptors[0].att_flags = BLE_ATT_F_READ;
    hid_report_descriptors[0].access_cb = hid_report_ref_access;
    hid_media_report_descriptors[0].uuid = &hid_report_ref_uuid.u;
    hid_media_report_descriptors[0].att_flags = BLE_ATT_F_READ;
    hid_media_report_descriptors[0].access_cb = hid_media_report_ref_access;

    // HID Information
    hid_characteristics[0].uuid = &hid_info_uuid.u;
    hid_characteristics[0].access_cb = hid_info_access;
    hid_characteristics[0].flags = BLE_GATT_CHR_F_READ;
    // Report Map
    hid_characteristics[1].uuid = &hid_report_map_uuid.u;
    hid_characteristics[1].access_cb = hid_report_map_access;
    hid_characteristics[1].flags = BLE_GATT_CHR_F_READ;
    // Report value: encrypted bonded HID host only.
    hid_characteristics[2].uuid = &hid_report_uuid.u;
    hid_characteristics[2].access_cb = hid_report_access;
    hid_characteristics[2].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                                   BLE_GATT_CHR_F_READ_ENC;
    hid_characteristics[2].descriptors = hid_report_descriptors;
    hid_characteristics[2].val_handle = &hid_report_value_handle;
    // HID Control Point
    hid_characteristics[3].uuid = &hid_control_uuid.u;
    hid_characteristics[3].access_cb = hid_control_access;
    hid_characteristics[3].flags = BLE_GATT_CHR_F_WRITE_NO_RSP;
    // Protocol Mode
    hid_characteristics[4].uuid = &hid_protocol_uuid.u;
    hid_characteristics[4].access_cb = hid_protocol_access;
    hid_characteristics[4].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP;
    // Report 2: Play/Pause for the optional headset-style long hold.
    hid_characteristics[5].uuid = &hid_report_uuid.u;
    hid_characteristics[5].access_cb = hid_media_report_access;
    hid_characteristics[5].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                                   BLE_GATT_CHR_F_READ_ENC;
    hid_characteristics[5].descriptors = hid_media_report_descriptors;
    hid_characteristics[5].val_handle = &hid_media_report_value_handle;
    services[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[1].uuid = &hid_service_uuid.u;
    services[1].characteristics = hid_characteristics;
    device_info_characteristics[0].uuid = &pnp_id_uuid.u;
    device_info_characteristics[0].access_cb = hid_pnp_access;
    device_info_characteristics[0].flags = BLE_GATT_CHR_F_READ;
    services[2].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[2].uuid = &device_info_uuid.u;
    services[2].characteristics = device_info_characteristics;
}

void advertise();
int gap_event(ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE connect status=%d", event->connect.status);
            if (event->connect.status == 0) {
                ++active_ble_connections;
                ++total_ble_connections;
            }
            // A successful connect consumes the advertising procedure.
            // Keep advertising for a second GATT client where supported.
            advertise();
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ++total_ble_disconnects;
            last_ble_disconnect_reason.store(event->disconnect.reason);
            if (active_ble_connections.load() > 0) --active_ble_connections;
            ESP_LOGW(TAG, "BLE disconnected, handle=%u reason=%d (total=%u)",
                     static_cast<unsigned>(event->disconnect.conn.conn_handle),
                     event->disconnect.reason,
                     static_cast<unsigned>(total_ble_disconnects.load()));
            if (event->disconnect.conn.conn_handle == garage_connection_handle.load()) {
                garage_subscribed.store(false);
                set_connection(false);
            }
            if (event->disconnect.conn.conn_handle == hid_connection_handle.load())
                hid_subscribed.store(false);
            if (event->disconnect.conn.conn_handle == hid_media_connection_handle.load())
                hid_media_subscribed.store(false);
            advertise();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == garage_event_value_handle) {
                garage_subscribed.store(event->subscribe.cur_notify != 0);
                garage_connection_handle.store(event->subscribe.conn_handle);
                set_connection(event->subscribe.cur_notify != 0);
                ESP_LOGI(TAG, "Garage PWR event notify subscribed=%d",
                         static_cast<int>(garage_subscribed.load()));
            }
            if (event->subscribe.attr_handle == hid_report_value_handle) {
                hid_subscribed.store(event->subscribe.cur_notify != 0);
                hid_connection_handle.store(event->subscribe.conn_handle);
                ESP_LOGI(TAG, "Android HID voice notify subscribed=%d",
                         static_cast<int>(hid_subscribed.load()));
            }
            if (event->subscribe.attr_handle == hid_media_report_value_handle) {
                hid_media_subscribed.store(event->subscribe.cur_notify != 0);
                hid_media_connection_handle.store(event->subscribe.conn_handle);
                ESP_LOGI(TAG, "Android HID media notify subscribed=%d",
                         static_cast<int>(hid_media_subscribed.load()));
            }
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "BLE security changed, status=%d",
                     event->enc_change.status);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            return 0;
        default:
            return 0;
    }
}
void advertise() {
    // BLE_GAP_EVENT_ADV_COMPLETE and CONNECT/DISCONNECT can arrive back to
    // back. Calling adv_set_fields while an advertisement is already active
    // can make NimBLE report EBUSY and impede reconnection.
    if (ble_gap_adv_active()) return;
    ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = const_cast<ble_uuid128_t *>(&service_uuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.uuids16 = const_cast<ble_uuid16_t *>(&hid_service_uuid);
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.appearance = 0x03C0; // Bluetooth Appearance: generic HID
    fields.appearance_is_present = 1;
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

namespace {
// Best-effort diagnostic, not an assistant launch. If Companion's custom
// GATT subscription is active, log what the Waveshare physically detected.
// Every diagnostic fits the 20-byte default ATT payload and doesn't require
// a Companion connection for the HID key itself.
void notify_voice_diagnostic(const char *kind) {
    if (!garage_subscribed.load() || garage_event_value_handle == 0) return;
    char diagnostic[20] = {};
    const int length = std::snprintf(diagnostic, sizeof(diagnostic),
                                     "GC8|1|VOICE|%s", kind);
    if (length < 1 || length >= static_cast<int>(sizeof(diagnostic))) return;
    auto *data = ble_hs_mbuf_from_flat(diagnostic, length);
    if (data) {
        int rc = ble_gatts_notify_custom(garage_connection_handle.load(),
                                         garage_event_value_handle, data);
        if (rc != 0) ESP_LOGW(TAG, "Voice diagnostic notify failed=%d", rc);
    }
}

// Android may treat a long media hold as a headset voice request. This is
// experimental and independent of the original Voice Command report ID 1.
// Always send a release, and run separately to keep garage polling alive.
constexpr uint32_t kHeadsetHoldMs = 950;
void media_hold_task(void *) {
    const uint16_t conn = hid_media_connection_handle.load();
    ble_gap_conn_desc desc = {};
    bool sent_down = false;
    if (!hid_media_subscribed.load() || hid_media_report_value_handle == 0 ||
        ble_gap_conn_find(conn, &desc) != 0 ||
        !desc.sec_state.encrypted || !desc.sec_state.bonded) {
        show_garage_banner("MEDIA KEY NOT PAIRED");
        ESP_LOGW(TAG, "HID media report not bonded/encrypted/subscribed");
        notify_voice_diagnostic("UNSUB");
    } else {
        const uint8_t down_value = 1;
        auto *down = ble_hs_mbuf_from_flat(&down_value, 1);
        if (down) {
            const int rc = ble_gatts_notify_custom(conn, hid_media_report_value_handle, down);
            if (rc == 0) {
                sent_down = true;
                hid_media_current_report = 1;
                ESP_LOGI(TAG, "HID media down accepted, holding %u ms",
                         static_cast<unsigned>(kHeadsetHoldMs));
                vTaskDelay(pdMS_TO_TICKS(kHeadsetHoldMs));
            } else {
                ESP_LOGW(TAG, "HID media down failed rc=%d", rc);
            }
        }
        if (sent_down) {
            const uint8_t up_value = 0;
            auto *up = ble_hs_mbuf_from_flat(&up_value, 1);
            if (up) {
                const int rc = ble_gatts_notify_custom(conn, hid_media_report_value_handle, up);
                if (rc == 0) {
                    ++submitted_media_reports;
                    show_garage_banner("HEADSET KEY SENT");
                    notify_voice_diagnostic("MEDIA");
                    ESP_LOGI(TAG, "HID media long press/release submitted");
                } else {
                    show_garage_banner("MEDIA BLE ERROR");
                    notify_voice_diagnostic("FAIL");
                    ESP_LOGW(TAG, "HID media release failed rc=%d", rc);
                }
            } else {
                show_garage_banner("MEDIA BLE ERROR");
                notify_voice_diagnostic("FAIL");
            }
        } else {
            show_garage_banner("MEDIA BLE ERROR");
            notify_voice_diagnostic("FAIL");
        }
    }
    hid_media_current_report = 0;
    media_key_busy.store(false);
    vTaskDelete(nullptr);
}
} // namespace

void gc8_ble_media_button_pressed() {
    notify_voice_diagnostic("HOLD");
    if (media_key_busy.exchange(true)) return;
    if (xTaskCreate(media_hold_task, "hid_media_hold", 4096,
                    nullptr, 3, nullptr) != pdPASS) {
        media_key_busy.store(false);
        show_garage_banner("MEDIA BLE ERROR");
        ESP_LOGE(TAG, "Unable to start HID headset hold");
    }
}

void gc8_ble_app_button_pressed() {
    notify_voice_diagnostic("TAP");
    // Bottom BOOT emits a real HID key; the Android GC8 app need not be open.
    if (!hid_subscribed.load() || hid_report_value_handle == 0) {
        show_garage_banner("PAIR VOICE REMOTE");
        ESP_LOGW(TAG, "HID not subscribed; pair GC8 Display in phone Bluetooth settings");
        notify_voice_diagnostic("UNSUB");
        return;
    }
    const uint16_t conn = hid_connection_handle.load();
    ble_gap_conn_desc desc = {};
    if (ble_gap_conn_find(conn, &desc) != 0 ||
        !desc.sec_state.encrypted || !desc.sec_state.bonded) {
        show_garage_banner("PAIR VOICE REMOTE");
        ESP_LOGW(TAG, "HID connection is not bonded and encrypted");
        notify_voice_diagnostic("UNSUB");
        return;
    }
    // Press and release the standard Consumer Control Voice Command usage
    // (0x0CF). The OS maps the HID report to KEYCODE_VOICE_ASSIST.
    const uint8_t pressed = 1;
    const uint8_t released = 0;
    auto *down = ble_hs_mbuf_from_flat(&pressed, 1);
    if (!down) {
        show_garage_banner("VOICE BLE ERROR");
        return;
    }
    int rc = ble_gatts_notify_custom(conn, hid_report_value_handle, down);
    if (rc != 0) {
        show_garage_banner("VOICE BLE ERROR");
        notify_voice_diagnostic("FAIL");
        ESP_LOGW(TAG, "HID key-down send failed: %d", rc);
        return;
    }
    hid_current_report = pressed;
    vTaskDelay(pdMS_TO_TICKS(90));
    auto *up = ble_hs_mbuf_from_flat(&released, 1);
    if (!up) {
        show_garage_banner("VOICE BLE ERROR");
        return;
    }
    rc = ble_gatts_notify_custom(conn, hid_report_value_handle, up);
    if (rc == 0) {
        hid_current_report = 0;
        ++submitted_voice_reports;
        show_garage_banner("VOICE KEY SENT");
        notify_voice_diagnostic("SENT");
        ESP_LOGI(TAG, "Consumer HID Voice Command press/release sent");
    } else {
        show_garage_banner("VOICE BLE ERROR");
        notify_voice_diagnostic("FAIL");
        ESP_LOGW(TAG, "HID key-up send failed: %d", rc);
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
    ble_svc_gap_device_appearance_set(0x03C0); // Match advertised generic HID appearance
    // Bonding is mandatory for HID input. Do not erase NVS on regular boot.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
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

Gc8BleDiagnostics gc8_ble_diagnostics() {
    return {garage_subscribed.load(), hid_subscribed.load(),
            hid_media_subscribed.load(), active_ble_connections.load(),
            total_ble_connections.load(), total_ble_disconnects.load(),
            last_ble_disconnect_reason.load(),
            submitted_voice_reports.load(), submitted_media_reports.load()};
}
