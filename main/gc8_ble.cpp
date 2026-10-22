#include "gc8_ble.h"

#include <cstring>
#include "esp_err.h"
#include "esp_log.h"
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
uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;

int on_write(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctx, void *) {
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    const auto length = OS_MBUF_PKTLEN(ctx->om);
    if (length == 0 || length > 180) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char buffer[181] = {};
    if (ble_hs_mbuf_to_flat(ctx->om, buffer, length, nullptr) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    // Connection smoke test only. Never trust incoming bytes or interpret as vehicle telemetry.
    ESP_LOGI(TAG, "Received %u BLE bytes", static_cast<unsigned>(length));
    return 0;
}

const ble_gatt_chr_def characteristics[] = {
    { .uuid = &write_uuid.u, .access_cb = on_write,
      .arg = nullptr, .descriptors = nullptr,
      .flags = BLE_GATT_CHR_F_WRITE, .min_key_size = 0, .val_handle = nullptr },
    {}
};
const ble_gatt_svc_def services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
      .includes = nullptr, .characteristics = characteristics },
    {}
};

void advertise();
int gap_event(ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE connect status=%d", event->connect.status);
            if (event->connect.status != 0) advertise();
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected");
            advertise();
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
    int rc = ble_gatts_count_cfg(services);
    if (rc == 0) rc = ble_gatts_add_svcs(services);
    if (rc != 0) { ESP_LOGE(TAG, "GATT registration failed: %d", rc); return; }
    ble_hs_cfg.sync_cb = synced;
    nimble_port_freertos_init(host_task);
}
