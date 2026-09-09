/*
 * DJI Camera Remote Control - BLE Communication Layer (Zephyr Bluetooth host)
 *
 * GATT Client for connecting to DJI cameras. Uses the Zephyr Bluetooth host on
 * top of Nordic's SoftDevice Controller, with connection callbacks registered
 * through BT_CONN_CB_DEFINE and per-operation GATT parameter structures.
 *
 * Key differences from the previous NimBLE implementation:
 * - No single GAP event callback; Zephyr splits it into conn callbacks
 *   (connected/disconnected), a scan callback and per-operation callbacks
 * - No conn_handle; operations take a `struct bt_conn *`.  The public API
 *   keeps the uint16_t conn_id, which here is bt_conn_index()
 * - GATT parameter structs (discover/write/read/subscribe) must stay alive
 *   for the duration of the operation, so they live in a per-slot context
 * - Notifications are delivered through bt_gatt_subscribe() instead of a
 *   manual CCCD write plus a global notification event
 * - bt_le_scan_start() has no timeout argument, so the scan timeout is driven
 *   by a k_work_delayable
 * - GAP (0x1800) and GATT (0x1801) services are registered by the Zephyr host
 *   automatically, so there is no ble_svc_gap_init()/ble_svc_gatt_init()
 *   equivalent.  The device name comes from CONFIG_BT_DEVICE_NAME
 */

#include <string.h>
#include <stdint.h>
#include "ble.h"
#include "esp_log.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>

#define TAG "BLE"

/* Zephyr log module -- replaces the ESP-IDF per-tag log level */
LOG_MODULE_REGISTER(ble, CONFIG_DJI_REMOTE_LOG_LEVEL);

/* ----------------------------------------------------------------
 *  Static state
 * ---------------------------------------------------------------- */

static bool s_connecting = false;
static bool s_ble_ready = false;
static bool s_advertising_active = false;

static ble_notify_callback_t s_notify_cb = NULL;
static connect_logic_state_callback_t s_state_cb = NULL;

static scan_controller_t s_scan_controller = {
    .mode = SCAN_MODE_IDLE,
    .target_slot = -1,
    .autoconnect_pending = {false, false, false},
    .scan_timeout_ms = 0,
    .scan_start_time = 0
};

static bool s_ble_scan_active = false;

ble_profile_t s_ble_profiles[BLE_MAX_CAMERAS] = {0};

static int s_active_scan_camera_index = -1;

/*
 * Per-slot Zephyr context.
 *
 * The Zephyr GATT API is asynchronous and keeps a pointer to the parameter
 * struct until the operation completes, so every slot owns its own set.
 */
typedef struct {
    struct bt_conn *conn;
    struct bt_gatt_exchange_params mtu_params;
    struct bt_gatt_discover_params disc_params;
    struct bt_gatt_subscribe_params sub_params;
    struct bt_gatt_write_params write_params;
    struct bt_gatt_read_params read_params;
} ble_conn_ctx_t;

static ble_conn_ctx_t s_conn_ctx[BLE_MAX_CAMERAS];

/* ----------------------------------------------------------------
 *  UUIDs
 * ---------------------------------------------------------------- */

#define REMOTE_TARGET_SERVICE_UUID  0xFFF0
#define REMOTE_NOTIFY_CHAR_UUID     0xFFF4
#define REMOTE_WRITE_CHAR_UUID      0xFFF5

/* ----------------------------------------------------------------
 *  Forward declarations
 * ---------------------------------------------------------------- */

static void try_to_connect(int camera_index, const uint8_t *addr);

/* GATT discovery chain */
static void on_mtu_exchanged(struct bt_conn *conn, uint8_t err,
                             struct bt_gatt_exchange_params *params);
static uint8_t on_svc_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params);
static uint8_t on_chr_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params);
static uint8_t on_dsc_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params);

/* ----------------------------------------------------------------
 *  Profile helpers
 * ---------------------------------------------------------------- */

static void init_ble_profiles(void) {
    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        s_ble_profiles[i].conn_id = UINT16_MAX;
        s_ble_profiles[i].camera_index = -1;
        s_ble_profiles[i].notify_char_handle = 0;
        s_ble_profiles[i].write_char_handle = 0;
        s_ble_profiles[i].read_char_handle = 0;
        s_ble_profiles[i].cccd_handle = 0;
        s_ble_profiles[i].service_start_handle = 0;
        s_ble_profiles[i].service_end_handle = 0;
        memset(s_ble_profiles[i].remote_bda, 0, 6);
        memset(s_ble_profiles[i].target_name, 0, sizeof(s_ble_profiles[i].target_name));
        memset(s_ble_profiles[i].target_mac, 0, 6);
        s_ble_profiles[i].connection_status.is_connected = false;
        s_ble_profiles[i].handle_discovery.notify_char_handle_found = false;
        s_ble_profiles[i].handle_discovery.write_char_handle_found = false;
        memset(&s_conn_ctx[i], 0, sizeof(s_conn_ctx[i]));
    }
    ESP_LOGI(TAG, "Initialized %d BLE profiles for multi-camera support", BLE_MAX_CAMERAS);
}

static ble_profile_t* get_profile_by_camera_index(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        return NULL;
    }
    return &s_ble_profiles[camera_index];
}

static ble_profile_t* get_profile_by_conn_id(uint16_t conn_id) {
    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        if (s_ble_profiles[i].camera_index >= 0 &&
            s_ble_profiles[i].conn_id == conn_id &&
            s_ble_profiles[i].connection_status.is_connected) {
            return &s_ble_profiles[i];
        }
    }
    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        if (s_ble_profiles[i].camera_index >= 0 && s_ble_profiles[i].conn_id == conn_id) {
            return &s_ble_profiles[i];
        }
    }
    return NULL;
}

/* Zephyr-specific: map a connection object back to its slot */
static ble_profile_t* get_profile_by_conn(struct bt_conn *conn) {
    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        if (s_conn_ctx[i].conn == conn) {
            return &s_ble_profiles[i];
        }
    }
    return NULL;
}

static ble_conn_ctx_t* get_ctx_by_conn_id(uint16_t conn_id) {
    ble_profile_t *p = get_profile_by_conn_id(conn_id);
    if (!p || p->camera_index < 0) {
        return NULL;
    }
    return &s_conn_ctx[p->camera_index];
}

/* ----------------------------------------------------------------
 *  Address byte-order conversion
 *
 *  Our persistent storage / target_mac / remote_bda fields store addresses
 *  big-endian (MSB first): E4:7A:2C:D1:C8:B8
 *  bt_addr_le_t.a.val is little-endian (LSB first): B8:C8:D1:2C:7A:E4
 *  This helper converts between the two representations.
 * ---------------------------------------------------------------- */

static void bda_reverse(const uint8_t *src, uint8_t *dst) {
    dst[0] = src[5];
    dst[1] = src[4];
    dst[2] = src[3];
    dst[3] = src[2];
    dst[4] = src[1];
    dst[5] = src[0];
}

/* ----------------------------------------------------------------
 *  Advertisement parsing helpers
 * ---------------------------------------------------------------- */

static const uint8_t* find_adv_field(const uint8_t *data, uint8_t data_len,
                                     uint8_t type, uint8_t *out_len)
{
    int i = 0;
    while (i < data_len) {
        uint8_t len = data[i];
        if (len == 0 || (i + len + 1) > data_len) break;
        if (data[i + 1] == type) {
            *out_len = len - 1;
            return &data[i + 2];
        }
        i += len + 1;
    }
    *out_len = 0;
    return NULL;
}

static uint8_t is_dji_camera_adv(const uint8_t *data, uint8_t data_len) {
    uint8_t mfg_len = 0;
    const uint8_t *mfg = find_adv_field(data, data_len,
                                         BT_DATA_MANUFACTURER_DATA, &mfg_len);
    if (mfg && mfg_len >= 5 &&
        mfg[0] == 0xAA && mfg[1] == 0x08 && mfg[4] == 0xFA) {
        return 1;
    }
    return 0;
}

static uint32_t get_dji_device_id(const uint8_t *data, uint8_t data_len) {
    uint8_t mfg_len = 0;
    const uint8_t *mfg = find_adv_field(data, data_len,
                                         BT_DATA_MANUFACTURER_DATA, &mfg_len);
    if (mfg && mfg_len >= 5 &&
        mfg[0] == 0xAA && mfg[1] == 0x08 && mfg[4] == 0xFA) {
        return (uint32_t)((uint16_t)mfg[2] | ((uint16_t)mfg[3] << 8));
    }
    return 0;
}

static const char* extract_device_name(const uint8_t *data, uint8_t data_len) {
    static char name_buf[64];
    uint8_t name_len = 0;

    /* Try complete name first, then shortened/incomplete name */
    const uint8_t *name = find_adv_field(data, data_len,
                                          BT_DATA_NAME_COMPLETE, &name_len);
    if (!name || name_len == 0) {
        name = find_adv_field(data, data_len,
                               BT_DATA_NAME_SHORTENED, &name_len);
    }
    if (name && name_len > 0) {
        size_t copy_len = name_len < sizeof(name_buf) - 1 ? name_len : sizeof(name_buf) - 1;
        memcpy(name_buf, name, copy_len);
        name_buf[copy_len] = '\0';
        return name_buf;
    }
    return "DJI Camera";
}

/* ----------------------------------------------------------------
 *  Scan timeout
 *
 *  bt_le_scan_start() has no timeout argument (NimBLE's ble_gap_disc() had
 *  one), so the "scan complete" path is driven by a delayed work item.
 * ---------------------------------------------------------------- */

static void on_scan_complete(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_scan_timeout_work, on_scan_complete);

static void reset_scan_controller(void) {
    s_scan_controller.mode = SCAN_MODE_IDLE;
    s_scan_controller.target_slot = -1;
    s_scan_controller.scan_timeout_ms = 0;
    s_scan_controller.scan_start_time = 0;
    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        s_scan_controller.autoconnect_pending[i] = false;
    }
}

static void on_scan_complete(struct k_work *work) {
    ARG_UNUSED(work);

    if (s_ble_scan_active) {
        bt_le_scan_stop();
        s_ble_scan_active = false;
    }

    ESP_LOGI(TAG, "Scan complete (mode=%d)", s_scan_controller.mode);

    if (s_connecting) {
        return;
    }

    switch (s_scan_controller.mode) {
    case SCAN_MODE_PAIRING:
        ESP_LOGI(TAG, "Pairing scan completed, waiting for user selection");
        break;
    case SCAN_MODE_AUTOCONNECT_BOOT: {
        extern bool connect_logic_is_slot_found(int slot_index);
        extern void connect_logic_mark_slot_not_found_during_boot(int slot_index);
        for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
            if (s_scan_controller.autoconnect_pending[i]) {
                if (!connect_logic_is_slot_found(i)) {
                    ESP_LOGW(TAG, "Autoconnect timeout: Camera slot %d not found", i);
                    connect_logic_mark_slot_not_found_during_boot(i);
                } else {
                    ESP_LOGI(TAG, "Autoconnect: Camera slot %d was found", i);
                }
            }
        }
        ESP_LOGI(TAG, "Autoconnect scan completed");
        break;
    }
    case SCAN_MODE_SLOT_RECONNECT:
        if (s_scan_controller.target_slot >= 0 &&
            s_scan_controller.target_slot < BLE_MAX_CAMERAS &&
            !s_connecting) {
            ESP_LOGW(TAG, "Slot reconnect timeout: Camera %d not found",
                     s_scan_controller.target_slot);
        }
        break;
    default:
        break;
    }

    reset_scan_controller();
}

/* ----------------------------------------------------------------
 *  Scan result callback
 *
 *  Replaces the BLE_GAP_EVENT_DISC branch of the NimBLE GAP event callback.
 * ---------------------------------------------------------------- */

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    ARG_UNUSED(adv_type);

    ble_profile_t *profile = NULL;
    const uint8_t *adv = buf->data;
    uint8_t adv_len = (uint8_t)MIN(buf->len, UINT8_MAX);

    /* Pure scan responses (name only, no manufacturer data) can update
     * the displayed name of an already-discovered camera during pairing.
     * Note: some controllers combine ADV_IND + SCAN_RSP into a single
     * report, so we must NOT return here -- always fall through to the
     * is_dji_camera_adv() check which handles combined reports. */
    if (!is_dji_camera_adv(adv, adv_len)) {
        if (s_scan_controller.mode == SCAN_MODE_PAIRING) {
            const char *rsp_name = extract_device_name(adv, adv_len);
            if (strcmp(rsp_name, "DJI Camera") != 0) {
                uint8_t bda_be[6];
                bda_reverse(addr->a.val, bda_be);
                extern void ui_pairing_update_discovered_camera_name(
                        const char *name, const uint8_t *mac);
                ui_pairing_update_discovered_camera_name(rsp_name, bda_be);
            }
        }
        return;
    }

    const char *adv_name_str = extract_device_name(adv, adv_len);
    uint32_t device_id       = get_dji_device_id(adv, adv_len);

    uint8_t bda_be[6];
    bda_reverse(addr->a.val, bda_be);

    ESP_LOGI(TAG, "Found device: %s RSSI=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X "
             "device_id=0x%04X mode=%d",
             adv_name_str, rssi,
             bda_be[0], bda_be[1], bda_be[2], bda_be[3], bda_be[4], bda_be[5],
             (unsigned int)device_id, s_scan_controller.mode);

    switch (s_scan_controller.mode) {
    case SCAN_MODE_PAIRING: {
        extern void ui_pairing_add_discovered_camera(const char *name,
                const uint8_t *mac, int8_t rssi, uint32_t device_id);
        ui_pairing_add_discovered_camera(adv_name_str, bda_be, rssi, device_id);
        break;
    }
    case SCAN_MODE_AUTOCONNECT_BOOT: {
        for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
            if (!s_scan_controller.autoconnect_pending[i]) continue;
            profile = get_profile_by_camera_index(i);
            if (!profile) continue;

            if (memcmp(profile->target_mac, bda_be, 6) == 0) {
                ESP_LOGI(TAG, "AUTOCONNECT_BOOT: Found camera for slot %d: %s", i, adv_name_str);
                extern void connect_logic_mark_slot_found(int slot_index);
                connect_logic_mark_slot_found(i);
                s_scan_controller.autoconnect_pending[i] = false;

                bool all_resolved = true;
                for (int j = 0; j < BLE_MAX_CAMERAS; j++) {
                    if (s_scan_controller.autoconnect_pending[j]) {
                        all_resolved = false;
                        break;
                    }
                }
                if (all_resolved) {
                    ESP_LOGI(TAG, "All autoconnect slots found, stopping scan early");
                    k_work_reschedule(&s_scan_timeout_work, K_NO_WAIT);
                }
                break;
            }
        }
        break;
    }
    case SCAN_MODE_SLOT_RECONNECT: {
        if (s_scan_controller.target_slot < 0 ||
            s_scan_controller.target_slot >= BLE_MAX_CAMERAS) break;

        profile = get_profile_by_camera_index(s_scan_controller.target_slot);
        if (!profile) break;

        if (memcmp(profile->target_mac, bda_be, 6) == 0) {
            ESP_LOGI(TAG, "SLOT_RECONNECT: Found target for slot %d: %s",
                     s_scan_controller.target_slot, adv_name_str);
            s_active_scan_camera_index = s_scan_controller.target_slot;
            try_to_connect(s_scan_controller.target_slot, bda_be);
        }
        break;
    }
    default:
        break;
    }
}

/* ----------------------------------------------------------------
 *  GATT discovery chain callbacks
 * ---------------------------------------------------------------- */

static void on_mtu_exchanged(struct bt_conn *conn, uint8_t err,
                             struct bt_gatt_exchange_params *params)
{
    ARG_UNUSED(params);

    ble_profile_t *profile = get_profile_by_conn(conn);
    if (!profile) return;

    int camera_index = profile->camera_index;
    ble_conn_ctx_t *ctx = &s_conn_ctx[camera_index];

    if (err != 0) {
        ESP_LOGW(TAG, "Camera %d: MTU exchange status=%d, using default", camera_index, err);
    }
    ESP_LOGI(TAG, "Camera %d: MTU=%d", camera_index, bt_gatt_get_mtu(conn));

    /* Discover ALL services (not just 0xFFF0) to match the original
       Bluedroid/NimBLE behavior. Some peripherals require full GATT
       database discovery before enabling notifications. */
    ctx->disc_params.uuid         = NULL;
    ctx->disc_params.func         = on_svc_discovered;
    ctx->disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    ctx->disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    ctx->disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

    int rc = bt_gatt_discover(conn, &ctx->disc_params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Camera %d: service discovery failed: %d", camera_index, rc);
        s_active_scan_camera_index = -1;
    }
}

static uint8_t on_svc_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);

    ble_profile_t *profile = get_profile_by_conn(conn);
    if (!profile) return BT_GATT_ITER_STOP;

    int camera_index = profile->camera_index;
    ble_conn_ctx_t *ctx = &s_conn_ctx[camera_index];

    if (attr != NULL) {
        const struct bt_gatt_service_val *svc = attr->user_data;
        uint16_t uuid16 = 0;
        if (svc->uuid->type == BT_UUID_TYPE_16) {
            uuid16 = BT_UUID_16(svc->uuid)->val;
        }
        if (uuid16 == REMOTE_TARGET_SERVICE_UUID) {
            profile->service_start_handle = attr->handle;
            profile->service_end_handle   = svc->end_handle;
            ESP_LOGI(TAG, "Camera %d: Service 0x%04X found: start=%d, end=%d",
                     camera_index, REMOTE_TARGET_SERVICE_UUID,
                     attr->handle, svc->end_handle);
        } else {
            ESP_LOGD(TAG, "Camera %d: Skipping service 0x%04X (start=%d, end=%d)",
                     camera_index, uuid16, attr->handle, svc->end_handle);
        }
        return BT_GATT_ITER_CONTINUE;
    }

    /* attr == NULL: discovery of this type is finished */
    if (profile->service_start_handle != 0) {
        ctx->disc_params.uuid         = NULL;
        ctx->disc_params.func         = on_chr_discovered;
        ctx->disc_params.start_handle = profile->service_start_handle;
        ctx->disc_params.end_handle   = profile->service_end_handle;
        ctx->disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

        int rc = bt_gatt_discover(conn, &ctx->disc_params);
        if (rc != 0) {
            ESP_LOGE(TAG, "Camera %d: characteristic discovery failed: %d", camera_index, rc);
            s_active_scan_camera_index = -1;
        }
    } else {
        ESP_LOGE(TAG, "Camera %d: DJI service not found", camera_index);
        s_active_scan_camera_index = -1;
    }
    return BT_GATT_ITER_STOP;
}

static uint8_t on_chr_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);

    ble_profile_t *profile = get_profile_by_conn(conn);
    if (!profile) return BT_GATT_ITER_STOP;

    int camera_index = profile->camera_index;
    ble_conn_ctx_t *ctx = &s_conn_ctx[camera_index];

    if (attr != NULL) {
        const struct bt_gatt_chrc *chrc = attr->user_data;
        uint16_t uuid16 = 0;
        if (chrc->uuid->type == BT_UUID_TYPE_16) {
            uuid16 = BT_UUID_16(chrc->uuid)->val;
        }

        if (uuid16 == REMOTE_NOTIFY_CHAR_UUID) {
            profile->notify_char_handle = chrc->value_handle;
            profile->handle_discovery.notify_char_handle_found = true;
            ESP_LOGI(TAG, "Camera %d: Notify Char (0x%04X) found, handle=0x%x",
                     camera_index, uuid16, chrc->value_handle);
        } else if (uuid16 == REMOTE_WRITE_CHAR_UUID) {
            profile->write_char_handle = chrc->value_handle;
            profile->handle_discovery.write_char_handle_found = true;
            ESP_LOGI(TAG, "Camera %d: Write Char (0x%04X) found, handle=0x%x",
                     camera_index, uuid16, chrc->value_handle);
        } else {
            ESP_LOGI(TAG, "Camera %d: Char 0x%04X found, handle=0x%x (def=0x%x)",
                     camera_index, uuid16, chrc->value_handle, attr->handle);
        }
        return BT_GATT_ITER_CONTINUE;
    }

    if (profile->notify_char_handle != 0) {
        uint16_t dsc_end = profile->service_end_handle;
        if (profile->write_char_handle > profile->notify_char_handle) {
            dsc_end = profile->write_char_handle - 1;
        }

        ctx->disc_params.uuid         = NULL;
        ctx->disc_params.func         = on_dsc_discovered;
        ctx->disc_params.start_handle = profile->notify_char_handle;
        ctx->disc_params.end_handle   = dsc_end;
        ctx->disc_params.type         = BT_GATT_DISCOVER_DESCRIPTOR;

        int rc = bt_gatt_discover(conn, &ctx->disc_params);
        if (rc != 0) {
            ESP_LOGE(TAG, "Camera %d: descriptor discovery failed: %d", camera_index, rc);
            s_active_scan_camera_index = -1;
        }
    } else {
        s_active_scan_camera_index = -1;
        ESP_LOGI(TAG, "Camera %d: GATT discovery complete (no notify char)", camera_index);
    }
    return BT_GATT_ITER_STOP;
}

static uint8_t on_dsc_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);

    ble_profile_t *profile = get_profile_by_conn(conn);
    if (!profile) return BT_GATT_ITER_STOP;

    int camera_index = profile->camera_index;

    if (attr != NULL) {
        if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC) == 0) {
            if (profile->cccd_handle == 0) {
                profile->cccd_handle = attr->handle;
                ESP_LOGI(TAG, "Camera %d: CCCD found, handle=0x%x",
                         camera_index, attr->handle);
            }
        }
        return BT_GATT_ITER_CONTINUE;
    }

    s_active_scan_camera_index = -1;
    ESP_LOGI(TAG, "Camera %d: GATT discovery complete (cccd=0x%04x notify=0x%04x write=0x%04x)",
             camera_index, profile->cccd_handle,
             profile->notify_char_handle, profile->write_char_handle);
    return BT_GATT_ITER_STOP;
}

/* ----------------------------------------------------------------
 *  GATT read/write/notify completion callbacks
 * ---------------------------------------------------------------- */

static uint8_t on_read_complete(struct bt_conn *conn, uint8_t err,
                                struct bt_gatt_read_params *params,
                                const void *data, uint16_t length)
{
    ARG_UNUSED(params);
    ARG_UNUSED(data);
    ARG_UNUSED(length);

    if (err != 0) {
        ESP_LOGE(TAG, "Read failed, conn_handle=%d status=%d", bt_conn_index(conn), err);
    }
    return BT_GATT_ITER_STOP;
}

static void on_write_complete(struct bt_conn *conn, uint8_t err,
                              struct bt_gatt_write_params *params)
{
    uint16_t conn_id = bt_conn_index(conn);

    if (err != 0) {
        ESP_LOGE(TAG, "Write failed, conn_handle=%d attr_handle=0x%x status=%d",
                 conn_id, params ? params->handle : 0, err);
        ble_profile_t *profile = get_profile_by_conn(conn);
        if (profile) {
            profile->connection_status.is_connected = false;
            ESP_LOGW(TAG, "Camera %d: Marking as disconnected due to write failure",
                     profile->camera_index);
        }
    } else {
        ESP_LOGI(TAG, "Write complete, conn_handle=%d attr_handle=0x%x",
                 conn_id, params ? params->handle : 0);
    }
}

/*
 * Notification callback.
 *
 * Replaces the BLE_GAP_EVENT_NOTIFY_RX branch of the NimBLE GAP event
 * callback.  Zephyr delivers the payload as a flat buffer, so no os_mbuf
 * copy is needed.
 */
static uint8_t on_notify(struct bt_conn *conn,
                         struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length)
{
    if (data == NULL) {
        ESP_LOGI(TAG, "Unsubscribed from handle 0x%04x", params ? params->value_handle : 0);
        return BT_GATT_ITER_STOP;
    }

    ble_profile_t *profile = get_profile_by_conn(conn);

    ESP_LOGI(TAG, "NOTIFY_RX: conn_handle=%d attr_handle=0x%x len=%d",
             bt_conn_index(conn), params ? params->value_handle : 0, length);

    if (s_notify_cb && profile) {
        s_notify_cb(profile->camera_index, (const uint8_t *)data, length);
    } else if (!s_notify_cb) {
        ESP_LOGW(TAG, "NOTIFY_RX: s_notify_cb is NULL!");
    } else if (!profile) {
        ESP_LOGW(TAG, "NOTIFY_RX: no profile for conn_handle=%d", bt_conn_index(conn));
    }
    return BT_GATT_ITER_CONTINUE;
}

/* ----------------------------------------------------------------
 *  Connection callbacks
 *
 *  Replace the BLE_GAP_EVENT_CONNECT / BLE_GAP_EVENT_DISCONNECT branches
 *  of the NimBLE GAP event callback.
 * ---------------------------------------------------------------- */

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    ble_profile_t *profile = NULL;
    struct bt_conn_info info;

    s_connecting = false;

    if (err != 0) {
        ESP_LOGE(TAG, "Connection failed, status=%d (slot %d)",
                 err, s_active_scan_camera_index);
        if (s_active_scan_camera_index >= 0) {
            ble_conn_ctx_t *ctx = &s_conn_ctx[s_active_scan_camera_index];
            if (ctx->conn) {
                bt_conn_unref(ctx->conn);
                ctx->conn = NULL;
            }
        }
        s_active_scan_camera_index = -1;
        return;
    }

    if (bt_conn_get_info(conn, &info) != 0) {
        ESP_LOGE(TAG, "bt_conn_get_info failed");
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }

    /* Convert the Zephyr LE address to BE for comparison with stored MACs */
    uint8_t peer_bda_be[6];
    bda_reverse(info.le.dst->a.val, peer_bda_be);

    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        if (s_ble_profiles[i].camera_index >= 0 &&
            memcmp(s_ble_profiles[i].target_mac, peer_bda_be, 6) == 0) {
            profile = &s_ble_profiles[i];
            break;
        }
    }
    if (!profile && s_active_scan_camera_index >= 0) {
        profile = get_profile_by_camera_index(s_active_scan_camera_index);
    }
    if (!profile) {
        ESP_LOGW(TAG, "Connection to unknown device, terminating");
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }

    int slot = profile->camera_index;
    if (slot < 0 || slot >= BLE_MAX_CAMERAS) {
        for (int j = 0; j < BLE_MAX_CAMERAS; j++) {
            if (&s_ble_profiles[j] == profile) {
                profile->camera_index = j;
                slot = j;
                break;
            }
        }
    }

    /*
     * Take our own reference to the connection.  bt_conn_le_create() already
     * handed one out for the slot that initiated the connection; for any other
     * path (e.g. a camera connecting back to our wake advertisement) we need
     * to claim one here.
     */
    if (s_conn_ctx[slot].conn == NULL) {
        s_conn_ctx[slot].conn = bt_conn_ref(conn);
    }

    profile->conn_id = bt_conn_index(conn);
    profile->connection_status.is_connected = true;
    memcpy(profile->remote_bda, peer_bda_be, 6);

    ESP_LOGI(TAG, "Camera %d connected! conn_handle=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             slot, profile->conn_id,
             peer_bda_be[0], peer_bda_be[1], peer_bda_be[2],
             peer_bda_be[3], peer_bda_be[4], peer_bda_be[5]);

    s_conn_ctx[slot].mtu_params.func = on_mtu_exchanged;
    int rc = bt_gatt_exchange_mtu(conn, &s_conn_ctx[slot].mtu_params);
    if (rc != 0) {
        ESP_LOGW(TAG, "Camera %d: MTU exchange request failed: %d, continuing", slot, rc);
        on_mtu_exchanged(conn, 0, &s_conn_ctx[slot].mtu_params);
    }
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    ble_profile_t *profile = get_profile_by_conn(conn);

    if (profile) {
        int camera_idx = profile->camera_index;
        profile->connection_status.is_connected = false;
        profile->conn_id = UINT16_MAX;
        profile->handle_discovery.write_char_handle_found = false;
        profile->handle_discovery.notify_char_handle_found = false;
        profile->notify_char_handle = 0;
        profile->write_char_handle = 0;
        profile->cccd_handle = 0;
        ESP_LOGI(TAG, "Camera %d disconnected, reason=0x%x", camera_idx, reason);

        if (camera_idx >= 0 && camera_idx < BLE_MAX_CAMERAS) {
            bt_conn_unref(s_conn_ctx[camera_idx].conn);
            s_conn_ctx[camera_idx].conn = NULL;
        }

        if (s_active_scan_camera_index == camera_idx) {
            s_active_scan_camera_index = -1;
        }
    } else {
        ESP_LOGW(TAG, "Disconnect for unknown conn_handle=%d, reason=0x%x",
                 bt_conn_index(conn), reason);
    }

    s_connecting = false;
    if (s_state_cb) {
        s_state_cb();
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected_cb,
    .disconnected = disconnected_cb,
};

/* ----------------------------------------------------------------
 *  Initialization
 * ---------------------------------------------------------------- */

esp_err_t ble_init(void) {
    init_ble_profiles();

    /*
     * bt_enable() blocks until the host is ready, so the NimBLE
     * sync-semaphore dance is not needed here.
     *
     * Persistent storage (the NVS equivalent) is initialised by the
     * application through the Zephyr settings subsystem; security is
     * disabled entirely (CONFIG_BT_SMP=n) to match the original firmware,
     * where DJI cameras withhold notifications from clients that advertise
     * encryption support but never pair.
     */
    int rc = bt_enable(NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "bt_enable failed: %d", rc);
        return ESP_FAIL;
    }

    s_ble_ready = true;

    ESP_LOGI(TAG, "ble_init success (Zephyr Bluetooth host)!");
    return ESP_OK;
}

/* ----------------------------------------------------------------
 *  Scanning
 * ---------------------------------------------------------------- */

esp_err_t ble_start_scan(scan_mode_t mode, int target_slot, uint32_t timeout_ms) {
    if (mode == SCAN_MODE_PAIRING || mode == SCAN_MODE_SLOT_RECONNECT) {
        if (target_slot < 0 || target_slot >= BLE_MAX_CAMERAS) {
            ESP_LOGE(TAG, "Invalid target_slot %d for mode %d", target_slot, mode);
            return ESP_FAIL;
        }
    }

    s_scan_controller.mode = mode;
    s_scan_controller.target_slot = target_slot;
    s_scan_controller.scan_timeout_ms = timeout_ms;
    s_scan_controller.scan_start_time = k_uptime_get_32();

    if (mode != SCAN_MODE_AUTOCONNECT_BOOT) {
        for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
            s_scan_controller.autoconnect_pending[i] = false;
        }
    }

    ESP_LOGI(TAG, "Starting scan: mode=%d, target_slot=%d, timeout=%u ms",
             mode, target_slot, timeout_ms);

    /* Disable duplicate filtering so we receive both ADV_IND (manufacturer
     * data for DJI detection) and SCAN_RSP (device name) as separate events.
     * Deduplication is handled at the application level (MAC-based). */
    struct bt_le_scan_param scan_params = {
        .type     = BT_LE_SCAN_TYPE_ACTIVE,
        .options  = BT_LE_SCAN_OPT_NONE,
        .interval = 0x0050,
        .window   = 0x0030,
    };

    int rc = bt_le_scan_start(&scan_params, scan_cb);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan: %d", rc);
        s_scan_controller.mode = SCAN_MODE_IDLE;
        return ESP_FAIL;
    }

    s_ble_scan_active = true;
    if (timeout_ms > 0) {
        k_work_reschedule(&s_scan_timeout_work, K_MSEC(timeout_ms));
    }

    ESP_LOGI(TAG, "Scan started (mode=%d, slot=%d)", mode, target_slot);
    return ESP_OK;
}

esp_err_t ble_stop_scan(void) {
    k_work_cancel_delayable(&s_scan_timeout_work);

    if (s_scan_controller.mode == SCAN_MODE_IDLE) {
        if (s_ble_scan_active) {
            bt_le_scan_stop();
            s_ble_scan_active = false;
        }
        ESP_LOGI(TAG, "No active scan to stop");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping scan (mode was %d)", s_scan_controller.mode);
    bt_le_scan_stop();
    s_ble_scan_active = false;

    reset_scan_controller();

    return ESP_OK;
}

scan_mode_t ble_get_scan_mode(void) {
    return s_scan_controller.mode;
}

void ble_set_autoconnect_pending(bool pending[BLE_MAX_CAMERAS]) {
    for (int i = 0; i < BLE_MAX_CAMERAS; i++) {
        s_scan_controller.autoconnect_pending[i] = pending[i];
        if (pending[i]) {
            ESP_LOGI(TAG, "Slot %d marked for autoconnect", i);
        }
    }
}

bool ble_is_scanning(void) {
    return s_ble_scan_active;
}

/* ----------------------------------------------------------------
 *  Connection management
 * ---------------------------------------------------------------- */

esp_err_t ble_start_scanning_and_connect(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        ESP_LOGE(TAG, "Invalid camera index: %d", camera_index);
        return ESP_FAIL;
    }

    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p) {
        ESP_LOGE(TAG, "Failed to get profile for camera %d", camera_index);
        return ESP_FAIL;
    }

    bool has_target = false;
    for (int i = 0; i < 6; i++) {
        if (p->target_mac[i] != 0) { has_target = true; break; }
    }

    s_active_scan_camera_index = camera_index;
    scan_mode_t mode = has_target ? SCAN_MODE_SLOT_RECONNECT : SCAN_MODE_PAIRING;

    ESP_LOGI(TAG, "Scan started for camera %d (mode=%d)", camera_index, mode);
    return ble_start_scan(mode, camera_index, 30000);
}

/*
 * Start an outgoing connection to `addr_be` (big-endian MAC).
 *
 * DJI cameras advertise with a public address, which is what the original
 * firmware hardcoded as well.
 */
static int connect_to_addr(int camera_index, const uint8_t *addr_be)
{
    bt_addr_le_t peer_addr;

    peer_addr.type = BT_ADDR_LE_PUBLIC;
    bda_reverse(addr_be, peer_addr.a.val);

    /* Zephyr cannot scan and connect simultaneously */
    if (s_ble_scan_active) {
        bt_le_scan_stop();
        s_ble_scan_active = false;
        k_work_cancel_delayable(&s_scan_timeout_work);
    }

    return bt_conn_le_create(&peer_addr, BT_CONN_LE_CREATE_CONN,
                             BT_LE_CONN_PARAM_DEFAULT,
                             &s_conn_ctx[camera_index].conn);
}

static void try_to_connect(int camera_index, const uint8_t *addr) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        ESP_LOGE(TAG, "Invalid camera index: %d", camera_index);
        return;
    }

    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p) {
        ESP_LOGE(TAG, "Failed to get profile for camera %d", camera_index);
        return;
    }

    if (s_connecting) {
        ESP_LOGW(TAG, "Already in connecting state, please wait...");
        return;
    }

    bool is_valid = false;
    for (int i = 0; i < 6; i++) {
        if (addr[i] != 0) { is_valid = true; break; }
    }
    if (!is_valid) {
        ESP_LOGE(TAG, "Invalid device address (all zeros) for camera %d", camera_index);
        return;
    }

    memcpy(p->remote_bda, addr, 6);
    s_connecting = true;

    ESP_LOGI(TAG, "Camera %d: Connecting to %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             camera_index, p->target_name,
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    int rc = connect_to_addr(camera_index, addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Camera %d: bt_conn_le_create failed: %d", camera_index, rc);
        s_connecting = false;
    }
}

void ble_set_reconnecting(bool flag) {
    (void)flag;
    ESP_LOGW(TAG, "ble_set_reconnecting() is deprecated, use ble_start_scan() with appropriate mode");
}

bool ble_get_reconnecting(void) {
    return (s_scan_controller.mode == SCAN_MODE_SLOT_RECONNECT ||
            s_scan_controller.mode == SCAN_MODE_AUTOCONNECT_BOOT);
}

esp_err_t ble_reconnect(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        ESP_LOGE(TAG, "Invalid camera index: %d", camera_index);
        return ESP_FAIL;
    }

    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p) {
        ESP_LOGE(TAG, "Failed to get profile for camera %d", camera_index);
        return ESP_FAIL;
    }

    bool is_valid = false;
    for (int i = 0; i < 6; i++) {
        if (p->target_mac[i] != 0) { is_valid = true; break; }
    }
    if (!is_valid) {
        ESP_LOGE(TAG, "Camera %d: No valid target device set", camera_index);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera %d: Reconnecting to %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             camera_index, p->target_name,
             p->target_mac[0], p->target_mac[1], p->target_mac[2],
             p->target_mac[3], p->target_mac[4], p->target_mac[5]);

    s_active_scan_camera_index = camera_index;
    return ble_start_scan(SCAN_MODE_SLOT_RECONNECT, camera_index, 30000);
}

esp_err_t ble_connect_direct(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        ESP_LOGE(TAG, "ble_connect_direct: Invalid camera index: %d", camera_index);
        return ESP_FAIL;
    }

    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p) {
        ESP_LOGE(TAG, "ble_connect_direct: Failed to get profile for camera %d", camera_index);
        return ESP_FAIL;
    }

    bool has_valid_mac = false;
    for (int i = 0; i < 6; i++) {
        if (p->target_mac[i] != 0) { has_valid_mac = true; break; }
    }
    if (!has_valid_mac) {
        ESP_LOGE(TAG, "ble_connect_direct: Camera %d has no valid target MAC", camera_index);
        return ESP_FAIL;
    }

    if (s_connecting) {
        ESP_LOGW(TAG, "ble_connect_direct: Already connecting, please wait...");
        return ESP_FAIL;
    }

    if (!s_ble_ready) {
        ESP_LOGE(TAG, "ble_connect_direct: Bluetooth host not ready");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AUTOCONNECT_BOOT direct connect for slot %d", camera_index);
    ESP_LOGI(TAG, "  Target: %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             p->target_name,
             p->target_mac[0], p->target_mac[1], p->target_mac[2],
             p->target_mac[3], p->target_mac[4], p->target_mac[5]);

    memcpy(p->remote_bda, p->target_mac, 6);
    s_active_scan_camera_index = camera_index;
    s_connecting = true;

    int rc = connect_to_addr(camera_index, p->target_mac);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_connect_direct: bt_conn_le_create failed for camera %d: %d",
                 camera_index, rc);
        s_connecting = false;
        s_active_scan_camera_index = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ble_connect_direct: connection initiated for camera %d", camera_index);
    return ESP_OK;
}

esp_err_t ble_disconnect(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        ESP_LOGE(TAG, "Invalid camera index: %d", camera_index);
        return ESP_FAIL;
    }

    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p) {
        ESP_LOGE(TAG, "Failed to get profile for camera %d", camera_index);
        return ESP_FAIL;
    }

    if (p->connection_status.is_connected && s_conn_ctx[camera_index].conn != NULL) {
        ESP_LOGI(TAG, "Disconnecting camera %d", camera_index);
        bt_conn_disconnect(s_conn_ctx[camera_index].conn,
                           BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        p->connection_status.is_connected = false;
    } else {
        ESP_LOGW(TAG, "Camera %d is not connected", camera_index);
    }

    return ESP_OK;
}

/* ----------------------------------------------------------------
 *  Read / Write / Notify
 * ---------------------------------------------------------------- */

esp_err_t ble_read(uint16_t conn_id, uint16_t handle) {
    ble_profile_t* p = get_profile_by_conn_id(conn_id);
    ble_conn_ctx_t* ctx = get_ctx_by_conn_id(conn_id);
    if (!p || !ctx || !ctx->conn || !p->connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected or invalid conn_id, skip read");
        return ESP_FAIL;
    }

    ctx->read_params.func          = on_read_complete;
    ctx->read_params.handle_count  = 1;
    ctx->read_params.single.handle = handle;
    ctx->read_params.single.offset = 0;

    int rc = bt_gatt_read(ctx->conn, &ctx->read_params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Camera %d: read failed: %d", p->camera_index, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_write_without_response(uint16_t conn_id, uint16_t handle,
                                     const uint8_t *data, size_t length)
{
    ble_profile_t* p = get_profile_by_conn_id(conn_id);
    ble_conn_ctx_t* ctx = get_ctx_by_conn_id(conn_id);
    if (!p || !ctx || !ctx->conn || !p->connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected or invalid conn_id, skip write_without_response");
        return ESP_FAIL;
    }

    int rc = bt_gatt_write_without_response(ctx->conn, handle, data,
                                            (uint16_t)length, false);
    if (rc != 0) {
        ESP_LOGE(TAG, "Camera %d: write_no_rsp failed: %d", p->camera_index, rc);
        if (rc == -ENOTCONN) {
            p->connection_status.is_connected = false;
            ESP_LOGW(TAG, "Camera %d: Marking as disconnected due to BLE write failure",
                     p->camera_index);
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_write_with_response(uint16_t conn_id, uint16_t handle,
                                  const uint8_t *data, size_t length)
{
    ble_profile_t* p = get_profile_by_conn_id(conn_id);
    ble_conn_ctx_t* ctx = get_ctx_by_conn_id(conn_id);
    if (!p || !ctx || !ctx->conn || !p->connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected or invalid conn_id, skip write_with_response");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera %d: write_rsp conn=%d handle=0x%x len=%d",
             p->camera_index, conn_id, handle, (int)length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, length > 64 ? 64 : length, ESP_LOG_INFO);

    ctx->write_params.func   = on_write_complete;
    ctx->write_params.handle = handle;
    ctx->write_params.offset = 0;
    ctx->write_params.data   = data;
    ctx->write_params.length = (uint16_t)length;

    int rc = bt_gatt_write(ctx->conn, &ctx->write_params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Camera %d: write failed: %d", p->camera_index, rc);
        if (rc == -ENOTCONN) {
            p->connection_status.is_connected = false;
            ESP_LOGW(TAG, "Camera %d: Marking as disconnected due to BLE write failure",
                     p->camera_index);
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
 * Enable notifications.
 *
 * On NimBLE this was a plain CCCD write plus a global notification event; the
 * Zephyr host instead routes notifications through the subscribe parameters,
 * so the CCCD write and the callback registration happen together here.
 */
esp_err_t ble_register_notify(uint16_t conn_id, uint16_t char_handle) {
    ble_profile_t* p = get_profile_by_conn_id(conn_id);
    ble_conn_ctx_t* ctx = get_ctx_by_conn_id(conn_id);
    if (!p || !ctx || !ctx->conn || !p->connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected or invalid conn_id, skip register_notify");
        return ESP_FAIL;
    }

    if (p->cccd_handle == 0) {
        ESP_LOGE(TAG, "Camera %d: CCCD handle not discovered", p->camera_index);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera %d: Writing CCCD 0x%04x -> [01 00] (enable notify)",
             p->camera_index, p->cccd_handle);

    ctx->sub_params.notify       = on_notify;
    ctx->sub_params.value        = BT_GATT_CCC_NOTIFY;
    ctx->sub_params.value_handle = char_handle ? char_handle : p->notify_char_handle;
    ctx->sub_params.ccc_handle   = p->cccd_handle;

    int rc = bt_gatt_subscribe(ctx->conn, &ctx->sub_params);
    if (rc != 0 && rc != -EALREADY) {
        ESP_LOGE(TAG, "Camera %d: register_notify (CCCD write) failed: %d",
                 p->camera_index, rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera %d: Notifications enabled (CCCD handle=0x%x)",
             p->camera_index, p->cccd_handle);
    return ESP_OK;
}

esp_err_t ble_unregister_notify(uint16_t conn_id, uint16_t char_handle) {
    ARG_UNUSED(char_handle);

    ble_profile_t* p = get_profile_by_conn_id(conn_id);
    ble_conn_ctx_t* ctx = get_ctx_by_conn_id(conn_id);
    if (!p || !ctx || !ctx->conn || !p->connection_status.is_connected ||
        p->cccd_handle == 0) {
        ESP_LOGW(TAG, "Cannot unregister notify");
        return ESP_FAIL;
    }

    int rc = bt_gatt_unsubscribe(ctx->conn, &ctx->sub_params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Camera %d: unregister_notify failed: %d", p->camera_index, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void ble_set_notify_callback(ble_notify_callback_t cb) {
    s_notify_cb = cb;
}

void ble_set_state_callback(connect_logic_state_callback_t cb) {
    s_state_cb = cb;
}

/* ----------------------------------------------------------------
 *  Advertising (wake broadcast)
 *
 *  The camera is woken by a connectable undirected advertisement carrying
 *  manufacturer-specific data: "WKP" followed by the camera MAC in reverse
 *  byte order.  DJI cameras filter on the PDU type when scanning for wake
 *  packets, so the advertisement must be connectable (ADV_IND).
 * ---------------------------------------------------------------- */

/* "WKP" + 6 MAC bytes -- the payload of the 0xFF manufacturer data field */
static uint8_t adv_payload[9] = { 'W','K','P','1','2','3','4','5','6' };

static void stop_adv_after_3s(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_adv_timer, stop_adv_after_3s);

static void stop_adv_after_3s(struct k_work *work) {
    ARG_UNUSED(work);

    bt_le_adv_stop();
    s_advertising_active = false;
    ESP_LOGI(TAG, "Wake broadcast advertising stopped after 3 seconds");
}

esp_err_t ble_stop_advertising_early(void) {
    if (!s_advertising_active) {
        ESP_LOGW(TAG, "Advertising not active, cannot stop early");
        return ESP_ERR_INVALID_STATE;
    }

    k_work_cancel_delayable(&s_adv_timer);

    int rc = bt_le_adv_stop();
    if (rc != 0 && rc != -EALREADY) {
        ESP_LOGW(TAG, "Failed to stop advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising stopped early");
    }

    s_advertising_active = false;
    return ESP_OK;
}

static esp_err_t start_adv_with_data(const uint8_t *payload, size_t payload_len) {
    /* Keep the original behaviour of not scanning while the wake broadcast
     * is on air, even though the SoftDevice Controller supports both. */
    if (s_ble_scan_active) {
        bt_le_scan_stop();
        s_ble_scan_active = false;
    }

    struct bt_data ad[] = {
        BT_DATA(BT_DATA_MANUFACTURER_DATA, payload, payload_len),
    };

    struct bt_le_adv_param adv_params = {
        .id           = BT_ID_DEFAULT,
        .options      = BT_LE_ADV_OPT_CONN,
        .interval_min = 0x20,
        .interval_max = 0x40,
    };

    int rc = bt_le_adv_start(&adv_params, ad, ARRAY_SIZE(ad), NULL, 0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return ESP_FAIL;
    }

    s_advertising_active = true;
    k_work_reschedule(&s_adv_timer, K_MSEC(3000));

    return ESP_OK;
}

esp_err_t ble_start_advertising(void) {
    ble_profile_t* p = get_profile_by_camera_index(0);
    if (!p) {
        ESP_LOGE(TAG, "Error: Camera 0 profile not available!");
        return ESP_ERR_INVALID_STATE;
    }

    if (memcmp(p->remote_bda, "\x00\x00\x00\x00\x00\x00", 6) == 0) {
        ESP_LOGE(TAG, "Error: Camera 0 remote_bda not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < 6; i++) {
        adv_payload[3 + i] = p->remote_bda[5 - i];
    }

    ESP_LOGI(TAG, "Modified Advertising Data (with MAC):");
    ESP_LOG_BUFFER_HEX(TAG, adv_payload, sizeof(adv_payload));

    esp_err_t ret = start_adv_with_data(adv_payload, sizeof(adv_payload));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Advertising started (will auto-stop after 3s)");
    }
    return ret;
}

esp_err_t ble_wake_camera(const uint8_t* camera_mac) {
    if (!camera_mac) {
        ESP_LOGE(TAG, "Camera MAC address is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting wake broadcast for camera: %02X:%02X:%02X:%02X:%02X:%02X",
             camera_mac[0], camera_mac[1], camera_mac[2],
             camera_mac[3], camera_mac[4], camera_mac[5]);

    static uint8_t wake_adv_payload[9] = { 'W','K','P','1','2','3','4','5','6' };

    for (int i = 0; i < 6; i++) {
        wake_adv_payload[3 + i] = camera_mac[5 - i];
    }

    ESP_LOGI(TAG, "Wake broadcast data: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             wake_adv_payload[0], wake_adv_payload[1], wake_adv_payload[2],
             wake_adv_payload[3], wake_adv_payload[4], wake_adv_payload[5],
             wake_adv_payload[6], wake_adv_payload[7], wake_adv_payload[8]);

    esp_err_t ret = start_adv_with_data(wake_adv_payload, sizeof(wake_adv_payload));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wake advertising started (will auto-stop after 3s)");
    }
    return ret;
}

/* ----------------------------------------------------------------
 *  Info getters
 * ---------------------------------------------------------------- */

const char* ble_get_connected_device_name(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return NULL;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (p && p->connection_status.is_connected) return p->target_name;
    return NULL;
}

const uint8_t* ble_get_connected_device_mac(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return NULL;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (p && p->connection_status.is_connected) return p->remote_bda;
    return NULL;
}

bool ble_get_connected_device_info(int camera_index, char* name, size_t name_size, uint8_t* mac) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS || !name || !mac) return false;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p || !p->connection_status.is_connected) return false;

    strncpy(name, p->target_name, name_size - 1);
    name[name_size - 1] = '\0';
    memcpy(mac, p->remote_bda, 6);
    return true;
}

uint16_t ble_get_conn_id(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return 0;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    return (p && p->conn_id != UINT16_MAX) ? p->conn_id : 0;
}

uint16_t ble_get_write_handle(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return 0;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    return p ? p->write_char_handle : 0;
}

uint16_t ble_get_notify_handle(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return 0;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    return p ? p->notify_char_handle : 0;
}

uint16_t ble_get_cccd_handle(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return 0;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    return p ? p->cccd_handle : 0;
}

bool ble_is_camera_connected(int camera_index) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) return false;
    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    return p ? p->connection_status.is_connected : false;
}

/* ----------------------------------------------------------------
 *  Target device configuration
 * ---------------------------------------------------------------- */

void ble_set_target_device(int camera_index, const char* name, const uint8_t* mac) {
    if (camera_index < 0 || camera_index >= BLE_MAX_CAMERAS) {
        ESP_LOGE(TAG, "Invalid camera index: %d", camera_index);
        return;
    }
    if (!name || !mac) {
        ESP_LOGE(TAG, "Invalid name or MAC address");
        return;
    }

    ble_profile_t* p = get_profile_by_camera_index(camera_index);
    if (!p) {
        ESP_LOGE(TAG, "Failed to get profile for camera %d", camera_index);
        return;
    }

    strncpy(p->target_name, name, sizeof(p->target_name) - 1);
    p->target_name[sizeof(p->target_name) - 1] = '\0';
    memcpy(p->target_mac, mac, 6);
    p->camera_index = camera_index;

    p->connection_status.is_connected = false;
    p->conn_id = UINT16_MAX;
    p->notify_char_handle = 0;
    p->write_char_handle = 0;
    p->cccd_handle = 0;
    p->handle_discovery.notify_char_handle_found = false;
    p->handle_discovery.write_char_handle_found = false;

    ESP_LOGI(TAG, "Camera %d target device set: %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             camera_index, name,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
