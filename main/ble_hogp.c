#include "ble_hogp.h"
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

#define DEVICE_NAME "Usb2BT Touch"
#define APPEARANCE_DIGITIZER 0x03C5
#define TOUCH_REPORT_ID 1
#define KEYS_REPORT_ID 2
#define REPORT_TYPE_INPUT 1

// pid.codes test VID/PID. A project-specific PID can replace it before release.
#define PNP_VID 0x1209
#define PNP_PID 0x0002

enum { CHR_REPORT_MAP, CHR_HID_INFO, CHR_CONTROL, CHR_TOUCH, CHR_BATTERY, CHR_PNP, CHR_MFG };

static const char *TAG = "ble";

// Contact ID is what binds Linux hid-multitouch, which sets INPUT_PROP_DIRECT.
// Without it the Shield maps a digitizer as a pointer. Do not add a Feature
// report back: the kernel GET_REPORTs it on every reconnect and the resulting
// timeout delays the input device by about 6 s. The trailing Consumer Control
// collection (report 2: bit0 AC Back, bit1 AC Home) was tested through uhid:
// touch stays touchScreen direct and KEY_BACK/KEY_HOMEPAGE reach the kernel.
// Toggling gestures never re-pairs: the map stays identical when keys are off.
// See docs/android-compat.md before changing this.
static const uint8_t s_report_map[] = {
    0x05, 0x0D,       // Usage Page (Digitizer)
    0x09, 0x04,       // Usage (Touch Screen)
    0xA1, 0x01,       // Collection (Application)
    0x85, TOUCH_REPORT_ID,
    0x09, 0x22,       //   Usage (Finger)
    0xA1, 0x02,       //   Collection (Logical)
    0x09, 0x42,       //     Usage (Tip Switch)
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x09, 0x32,       //     Usage (In Range)
    0x81, 0x02,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03, // padding
    0x09, 0x51,       //     Usage (Contact ID)
    0x25, 0x0F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30, 0x09, 0x31, // X, Y
    0x15, 0x00, 0x26, 0xFF, 0x7F, 0x35, 0x00, 0x46, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0,             //   End Collection
    0x05, 0x0D,       //   Usage Page (Digitizer)
    0x09, 0x54,       //   Usage (Contact Count)
    0x15, 0x00, 0x25, 0x0A, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0xC0,             // End Collection
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, KEYS_REPORT_ID,
    0x0A, 0x24, 0x02, // Usage (AC Back)
    0x0A, 0x23, 0x02, // Usage (AC Home)
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03, // padding
    0xC0,             // End Collection
};

static uint16_t s_touch_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static volatile bool s_encrypted, s_subscribed;
static volatile bool s_standard_hid;
static volatile uint16_t s_itvl;
static volatile uint32_t s_sent, s_failed, s_keys_sent;

static void advertise(void);

static int append(struct ble_gatt_access_ctxt *ctxt, const void *data, size_t len)
{
    return os_mbuf_append(ctxt->om, data, len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0; // Control Point writes carry nothing we act on
    }
    switch ((int)(intptr_t)arg) {
    case CHR_REPORT_MAP:
        return append(ctxt, s_report_map, sizeof s_report_map);
    case CHR_HID_INFO: {
        static const uint8_t info[] = {0x11, 0x01, 0x00, 0x02}; // HID 1.11, normally connectable
        return append(ctxt, info, sizeof info);
    }
    case CHR_TOUCH: {
        if (s_standard_hid) {
            static const uint8_t idle[7] = {0};
            return append(ctxt, idle, sizeof idle);
        }
        static const uint8_t idle[8] = {TOUCH_REPORT_ID, 0, 0, 0, 0, 0, 0, 0};
        return append(ctxt, idle, sizeof idle);
    }
    case CHR_BATTERY: {
        static const uint8_t level = 100;
        return append(ctxt, &level, 1);
    }
    case CHR_PNP: {
        static const uint8_t pnp[] = {0x02, PNP_VID & 0xFF, PNP_VID >> 8, PNP_PID & 0xFF, PNP_PID >> 8, 0x00, 0x01};
        return append(ctxt, pnp, sizeof pnp);
    }
    case CHR_MFG:
        return append(ctxt, "usb2bluetooth-touch-relay", 25);
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int report_ref_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    const uint8_t *ref = arg;
    return append(ctxt, ref, 2);
}

// Android mode declares Report ID 0 so the single Input Report characteristic
// carries both reports with their own first byte as report ID. Standard mode
// declares Report ID 1. Set once before init and latched for the whole run.
static uint8_t s_touch_ref[2] = {0, REPORT_TYPE_INPUT};

void ble_hogp_set_standard_hid(bool standard)
{
    s_standard_hid = standard;
    s_touch_ref[0] = standard ? TOUCH_REPORT_ID : 0;
}

#define CHR(u16, id, fl) {.uuid = BLE_UUID16_DECLARE(u16), .access_cb = chr_access, .arg = (void *)(intptr_t)(id), .flags = (fl)}
#define READ_ENC (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define REPORT_REF(ref) \
    (struct ble_gatt_dsc_def[]) { \
        {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, \
         .access_cb = report_ref_access, .arg = (void *)(ref)}, {0} }

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A), // Device Information
        .characteristics = (struct ble_gatt_chr_def[]) {
            CHR(0x2A50, CHR_PNP, BLE_GATT_CHR_F_READ),
            CHR(0x2A29, CHR_MFG, BLE_GATT_CHR_F_READ),
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F), // Battery
        .characteristics = (struct ble_gatt_chr_def[]) {
            CHR(0x2A19, CHR_BATTERY, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY),
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812), // Human Interface Device
        .characteristics = (struct ble_gatt_chr_def[]) {
            CHR(0x2A4B, CHR_REPORT_MAP, READ_ENC),
            CHR(0x2A4A, CHR_HID_INFO, READ_ENC),
            CHR(0x2A4C, CHR_CONTROL, BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC),
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_access,
                .arg = (void *)(intptr_t)CHR_TOUCH, .flags = READ_ENC | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_touch_handle, .descriptors = REPORT_REF(s_touch_ref),
            },
            {0},
        },
    },
    {0},
};

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d", ev->connect.status);
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=0x%x", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_encrypted = s_subscribed = false;
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        s_encrypted = ev->enc_change.status == 0;
        ESP_LOGI(TAG, "encryption status=%d", ev->enc_change.status);
        if (s_encrypted && ble_gap_conn_find(ev->enc_change.conn_handle, &d) == 0) {
            ESP_LOGI(TAG, "bonded=%d authenticated=%d keysize=%d", d.sec_state.bonded,
                     d.sec_state.authenticated, d.sec_state.key_size);
            const struct ble_gap_upd_params p = {
                .itvl_min = 6, .itvl_max = 12, .latency = 0, .supervision_timeout = 200,
            };
            ble_gap_update_params(ev->enc_change.conn_handle, &p);
        }
        break;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0) {
            s_itvl = d.conn_itvl;
            ESP_LOGI(TAG, "conn itvl=%u latency=%u timeout=%u", d.conn_itvl, d.conn_latency, d.supervision_timeout);
        }
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_touch_handle) {
            s_subscribed = ev->subscribe.cur_notify;
            ESP_LOGI(TAG, "touch notifications %s (reason %d)", s_subscribed ? "on" : "off", ev->subscribe.reason);
        }
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "passkey action %d", ev->passkey.params.action);
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu=%u", ev->mtu.value);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The host lost its keys, for example after "forget device". Accept the new pairing.
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields f = {0};
    const char *name = ble_svc_gap_device_name();
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance = APPEARANCE_DIGITIZER;
    f.appearance_is_present = 1;
    f.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(0x1812)};
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.name = (uint8_t *)name;
    f.name_len = strlen(name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) {
        ESP_LOGE(TAG, "adv fields rc=%d", rc);
        return;
    }
    const struct ble_gap_adv_params p = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 48,  // 30 ms: quick reconnects, and power is not a concern here
        .itvl_max = 80,  // 50 ms
    };
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start rc=%d", rc);
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    advertise();
    ESP_LOGI(TAG, "advertising as \"%s\"", DEVICE_NAME);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset reason=%d", reason);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_hogp_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(s_services));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(s_services));
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gap_device_appearance_set(APPEARANCE_DIGITIZER);
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}

bool ble_hogp_ready(void)
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE && s_encrypted && s_subscribed;
}

bool ble_hogp_send_touch(bool down, uint16_t x, uint16_t y)
{
    // Read the handle once: if the link drops here, the send fails with ENOTCONN.
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE || !s_encrypted || !s_subscribed) {
        return false;
    }
    if (s_standard_hid) {
        // Standard HOGP: the report ID lives in the Report Reference
        // descriptor, not the payload.
        const uint8_t r[7] = {
            down ? 0x03 : 0x00, 0x00, x & 0xFF, x >> 8, y & 0xFF, y >> 8, down ? 1 : 0,
        };
        struct os_mbuf *om = ble_hs_mbuf_from_flat(r, sizeof r);
        if (!om || ble_gatts_notify_custom(conn, s_touch_handle, om) != 0) {
            s_failed++;
            return false;
        }
        s_sent++;
        return true;
    }
    // Android mode: the Report Reference declares ID 0, so the payload carries
    // its own first byte as report ID.
    const uint8_t r[8] = {
        TOUCH_REPORT_ID, down ? 0x03 : 0x00, 0x00, x & 0xFF, x >> 8, y & 0xFF, y >> 8, down ? 1 : 0,
    };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(r, sizeof r);
    if (!om || ble_gatts_notify_custom(conn, s_touch_handle, om) != 0) {
        s_failed++;
        return false;
    }
    s_sent++;
    return true;
}

// One Input Report characteristic carries both touch (report 1) and keys
// (report 2): the Android TV stack only read the Report Reference and
// subscribed to the CCCD of the first Input Report characteristic, so a second
// characteristic never delivered keys. See docs/android-compat.md. In standard
// mode keys are unavailable and this returns false.
bool ble_hogp_send_keys(uint8_t bits)
{
    if (s_standard_hid) {
        return false;
    }
    // Read the handle once: if the link drops here, the send fails with ENOTCONN.
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE || !s_encrypted || !s_subscribed) {
        return false;
    }
    const uint8_t r[2] = {KEYS_REPORT_ID, (uint8_t)(bits & 0x03)};
    struct os_mbuf *om = ble_hs_mbuf_from_flat(r, sizeof r);
    if (!om || ble_gatts_notify_custom(conn, s_touch_handle, om) != 0) {
        return false;
    }
    s_keys_sent++;
    return true;
}

void ble_hogp_get_status(ble_hogp_status_t *out)
{
    int bonds = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &bonds);
    *out = (ble_hogp_status_t) {
        .connected = s_conn != BLE_HS_CONN_HANDLE_NONE,
        .encrypted = s_encrypted,
        .subscribed = s_subscribed,
        .standard_hid = s_standard_hid,
        .bonds = bonds,
        .conn_itvl = s_itvl,
        .sent = s_sent,
        .send_failed = s_failed,
        .keys_sent = s_keys_sent,
    };
}

void ble_hogp_clear_bonds(void)
{
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_store_clear();
    ESP_LOGW(TAG, "all bonds cleared");
}
