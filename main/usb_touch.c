#include "usb_touch.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid_touch.h"
#include "relay.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"

#define MAX_IFACES 4
#define MAX_DESC 1024
#define RAW_LOG_LIMIT 64 // reports logged in hex per attach, for bring-up
#define MAX_RECOVERIES 5

typedef struct {
    hid_host_device_handle_t h;
    uint8_t iface;
    uint16_t desc_len;
    uint8_t desc[MAX_DESC];
    ht_layout_t layout;
    ht_state_t state;
    uint32_t raw_logged;
    uint8_t recoveries; // attempts since the last good report
} iface_t;

typedef enum { EVT_CONNECTED, EVT_RECOVER } evt_type_t;

typedef struct {
    evt_type_t type;
    hid_host_device_handle_t h;
} evt_t;

static const char *TAG = "usb";
static iface_t s_if[MAX_IFACES];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_active = -1;
static usb_touch_status_t s_st;
static QueueHandle_t s_events;

static void log_hex(const char *label, const uint8_t *d, size_t n)
{
    char line[3 * 32 + 1];
    for (size_t i = 0; i < n; i += 32) {
        size_t len = n - i < 32 ? n - i : 32, w = 0;
        for (size_t k = 0; k < len; k++) {
            w += (size_t)snprintf(&line[w], sizeof line - w, "%02x ", d[i + k]);
        }
        ESP_LOGI(TAG, "%s[%u]: %s", label, (unsigned)i, line);
    }
}

static iface_t *find_iface(hid_host_device_handle_t h)
{
    for (int i = 0; i < MAX_IFACES; i++) {
        if (s_if[i].h == h) {
            return &s_if[i];
        }
    }
    return NULL;
}

static void choose_active(void)
{
    int best = -1;
    taskENTER_CRITICAL(&s_lock);
    for (int i = 0; i < MAX_IFACES; i++) {
        if (s_if[i].h && s_if[i].layout.kind != HT_KIND_NONE &&
            (best < 0 || s_if[i].layout.kind > s_if[best].layout.kind)) {
            best = i;
        }
    }
    s_active = best;
    s_st.has_touch = best >= 0;
    if (best >= 0) {
        s_st.kind = (uint8_t)s_if[best].layout.kind;
        s_st.iface = s_if[best].iface;
        s_st.contacts = s_if[best].layout.contacts;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (best >= 0) {
        ESP_LOGI(TAG, "using interface %u: kind=%d contacts=%d", s_st.iface, s_st.kind, s_st.contacts);
    }
}

static void on_report(iface_t *f)
{
    uint8_t data[128];
    size_t len = 0;
    if (hid_host_device_get_raw_input_report_data(f->h, data, sizeof data, &len) != ESP_OK) {
        return;
    }
    s_st.reports++;
    f->recoveries = 0;
    if (f->raw_logged < RAW_LOG_LIMIT) {
        f->raw_logged++;
        log_hex(f == &s_if[s_active < 0 ? 0 : s_active] ? "in" : "in-other", data, len);
    }
    if (s_active < 0 || f != &s_if[s_active]) {
        return;
    }
    ht_touch_t t;
    bool was_tracking = f->state.tracking;
    if (ht_decode(&f->layout, data, len, &f->state, &t)) {
        s_st.touches++;
        if (t.down || was_tracking) {
            relay_submit(t.down, t.x, t.y);
        }
    }
}

static void iface_event(hid_host_device_handle_t h, const hid_host_interface_event_t ev, void *arg)
{
    (void)arg;
    iface_t *f = find_iface(h);
    switch (ev) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        if (f) {
            on_report(f);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        s_st.errors++;
        ESP_LOGW(TAG, "transfer error");
        {
            // The driver does not resubmit a failed transfer, so input stops until restarted.
            evt_t e = {.type = EVT_RECOVER, .h = h};
            xQueueSend(s_events, &e, 0);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "interface disconnected");
        if (f) {
            bool was_touching = s_active >= 0 && f == &s_if[s_active] && f->state.tracking;
            taskENTER_CRITICAL(&s_lock);
            f->h = NULL;
            f->desc_len = 0;
            taskEXIT_CRITICAL(&s_lock);
            choose_active();
            if (was_touching) {
                relay_submit(false, 0, 0); // never leave the host with a stuck contact
            }
        }
        hid_host_device_close(h);
        s_st.attached = false;
        for (int i = 0; i < MAX_IFACES; i++) {
            s_st.attached |= s_if[i].h != NULL;
        }
        break;
    default:
        break;
    }
}

static void open_iface(hid_host_device_handle_t h)
{
    hid_host_dev_params_t p;
    if (hid_host_device_get_params(h, &p) != ESP_OK) {
        return;
    }
    // Reserve the slot first, so a disconnect during setup can find it.
    taskENTER_CRITICAL(&s_lock);
    iface_t *f = find_iface(NULL);
    if (f) {
        f->h = h;
        f->iface = p.iface_num;
        f->desc_len = 0;
        f->raw_logged = 0;
        f->recoveries = 0;
        memset(&f->state, 0, sizeof f->state);
        memset(&f->layout, 0, sizeof f->layout);
    }
    taskEXIT_CRITICAL(&s_lock);
    if (!f) {
        ESP_LOGW(TAG, "too many interfaces, ignoring %u", p.iface_num);
        return;
    }

    const hid_host_device_config_t cfg = {.callback = iface_event};
    esp_err_t err = hid_host_device_open(h, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open iface %u: %s", p.iface_num, esp_err_to_name(err));
        taskENTER_CRITICAL(&s_lock);
        f->h = NULL;
        taskEXIT_CRITICAL(&s_lock);
        return;
    }

    hid_host_dev_info_t info;
    if (hid_host_get_device_info(h, &info) == ESP_OK) {
        s_st.vid = info.VID;
        s_st.pid = info.PID;
    }
    ESP_LOGI(TAG, "HID %04x:%04x iface=%u subclass=%u proto=%u", s_st.vid, s_st.pid, p.iface_num, p.sub_class, p.proto);

    size_t len = 0;
    const uint8_t *desc = hid_host_get_report_descriptor(h, &len);
    if (desc && len) {
        // The driver frees its copy on disconnect, so take ours straight away.
        size_t keep = len < MAX_DESC ? len : MAX_DESC;
        memcpy(f->desc, desc, keep);
        f->desc_len = (uint16_t)keep;
        log_hex("desc", f->desc, keep);
        ht_parse_descriptor(f->desc, keep, &f->layout);
    } else {
        ESP_LOGW(TAG, "no report descriptor on iface %u", p.iface_num);
    }

    // A disconnect during setup clears the slot; the handle is then already closed.
    taskENTER_CRITICAL(&s_lock);
    bool still_here = f->h == h;
    if (!still_here) {
        f->desc_len = 0;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (!still_here) {
        ESP_LOGW(TAG, "iface %u went away during setup", p.iface_num);
        return;
    }
    s_st.attached = true;
    choose_active();

    err = hid_host_device_start(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start iface %u: %s", p.iface_num, esp_err_to_name(err));
    }
}

static void recover_iface(hid_host_device_handle_t h)
{
    iface_t *f = find_iface(h);
    if (!f) {
        return;
    }
    if (s_active >= 0 && f == &s_if[s_active] && f->state.tracking) {
        f->state.tracking = false;
        relay_submit(false, 0, 0);
    }
    if (f->recoveries >= MAX_RECOVERIES) {
        ESP_LOGE(TAG, "iface %u: giving up after %d restarts", f->iface, MAX_RECOVERIES);
        taskENTER_CRITICAL(&s_lock);
        f->layout.kind = HT_KIND_NONE; // reported as no touch device, so the LED shows it
        taskEXIT_CRITICAL(&s_lock);
        choose_active();
        return;
    }
    f->recoveries++;
    vTaskDelay(pdMS_TO_TICKS(100 * f->recoveries));
    hid_host_device_stop(h);
    esp_err_t err = hid_host_device_start(h);
    ESP_LOGW(TAG, "iface %u restart %d: %s", f->iface, f->recoveries, esp_err_to_name(err));
}

static void driver_event(hid_host_device_handle_t h, const hid_host_driver_event_t ev, void *arg)
{
    (void)arg;
    if (ev == HID_HOST_DRIVER_EVENT_CONNECTED) {
        evt_t e = {.type = EVT_CONNECTED, .h = h};
        xQueueSend(s_events, &e, 0); // opening blocks, so leave the driver callback first
    }
}

static void lib_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static void app_task(void *arg)
{
    (void)arg;
    evt_t e;
    for (;;) {
        if (xQueueReceive(s_events, &e, portMAX_DELAY) == pdTRUE) {
            if (e.type == EVT_CONNECTED) {
                open_iface(e.h);
            } else {
                recover_iface(e.h);
            }
        }
    }
}

void usb_touch_start(void)
{
    const usb_host_config_t host = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&host));
    s_events = xQueueCreate(8, sizeof(evt_t));
    xTaskCreate(lib_task, "usb_lib", 4096, NULL, 6, NULL);
    xTaskCreate(app_task, "usb_app", 8192, NULL, 4, NULL);
    const hid_host_driver_config_t drv = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 6144,
        .core_id = tskNO_AFFINITY,
        .callback = driver_event,
    };
    ESP_ERROR_CHECK(hid_host_install(&drv));
    s_st.started = true;
    ESP_LOGI(TAG, "USB host started");
}

void usb_touch_get_status(usb_touch_status_t *out)
{
    *out = s_st;
}

size_t usb_touch_get_descriptor(int index, uint8_t *out, size_t max, uint8_t *iface)
{
    if (index < 0 || index >= MAX_IFACES) {
        return 0;
    }
    taskENTER_CRITICAL(&s_lock);
    size_t n = s_if[index].desc_len < max ? s_if[index].desc_len : max;
    memcpy(out, s_if[index].desc, n);
    *iface = s_if[index].iface;
    taskEXIT_CRITICAL(&s_lock);
    return n;
}
