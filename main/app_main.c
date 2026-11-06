#include <stdbool.h>
#include "ble_hogp.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#ifdef CONFIG_U2B_NETWORK
#include "esp_event.h"
#include "esp_ota_ops.h"
#endif
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gesture.h"
#include "hid_touch.h"
#include "led_strip.h"
#include "logring.h"
#ifdef CONFIG_U2B_NETWORK
#include "netsvc.h"
#endif
#include "nvs.h"
#include "nvs_flash.h"
#include "relay.h"
#include "serial_cfg.h"
#include "touch_map.h"
#include "usb_touch.h"

#define PIN_BUTTON GPIO_NUM_0
#define PIN_LED GPIO_NUM_21
#define PC_DETECT_MS 1500       // time allowed for a PC to show up on USB after reset
#ifdef CONFIG_U2B_NETWORK
#define HEALTHY_S 30            // continuous Wi-Fi, then touch device, needed to confirm an image
#define ROLLBACK_AFTER_S 180    // give up on an unconfirmed image if Wi-Fi never stabilises
#endif
#define FORGET_HOLD_MS 5000
#define PRESS_RETRY_MS 200
#define BUTTON_SHORT_MS 1000
#define KEY_RETRY_MS 200
#define GESTURE_NVS_NS "cfg"
#define GESTURE_NVS_KEY "gestures"
#define STD_HID_NVS_KEY "std_hid"

typedef struct {
    bool is_key; // button Back request; down/x/y ignored
    bool down;
    uint16_t x, y;
} touch_evt_t;

static const char *TAG = "app";
static QueueHandle_t s_touch_q;
static touch_map_t s_map;
static led_strip_handle_t s_led;
static gesture_t s_gesture;
static bool s_gestures_enabled = true;
static bool s_standard_hid;
// Volatile copy of the D-pad mode for other tasks (main loop LED,
// /status). Written only by relay_task, the single gesture engine consumer,
// so plain bool reads elsewhere are safe.
static volatile bool s_dpad_active;

static void enqueue(touch_evt_t e)
{
    if (xQueueSend(s_touch_q, &e, 0) != pdTRUE) {
        // Drop the oldest, so the newest state (possibly a release) always survives.
        touch_evt_t old;
        xQueueReceive(s_touch_q, &old, 0);
        xQueueSend(s_touch_q, &e, 0);
    }
}

void relay_submit(bool down, uint16_t x, uint16_t y)
{
    touch_evt_t e = {.is_key = false, .down = down};
    touch_map_apply(&s_map, x, y, &e.x, &e.y);
    enqueue(e);
}

void relay_request_back(void)
{
    enqueue((touch_evt_t) {.is_key = true});
}

static bool gestures_load(void)
{
    nvs_handle_t h;
    uint8_t v = 1;
    if (nvs_open(GESTURE_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, GESTURE_NVS_KEY, &v) != ESP_OK) {
            v = 1;
        }
        nvs_close(h);
    }
    return v != 0;
}

void relay_set_gestures(bool enabled)
{
    s_gestures_enabled = enabled;
    // In standard HID mode keys cannot be sent, so the engine stays
    // disabled without changing the saved setting.
    gesture_set_enabled(&s_gesture, enabled && !s_standard_hid);
    nvs_handle_t h;
    if (nvs_open(GESTURE_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, GESTURE_NVS_KEY, enabled ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "gestures %s", enabled ? "on" : "off");
}

bool relay_get_gestures(void)
{
    // Reports the effective switch: forced off in standard HID mode, where
    // keys cannot be sent and swipes must stay touches.
    return s_gestures_enabled && !s_standard_hid;
}

bool relay_get_dpad(void)
{
    return s_dpad_active;
}

static bool std_hid_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(GESTURE_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, STD_HID_NVS_KEY, &v) != ESP_OK) {
            v = 0;
        }
        nvs_close(h);
    }
    return v != 0;
}

bool relay_get_standard_hid(void)
{
    return s_standard_hid;
}

bool relay_get_standard_hid_saved(void)
{
    return std_hid_load();
}

bool relay_get_gestures_saved(void)
{
    return s_gestures_enabled;
}

// Persists the HID mode only; the running BLE mode stays until a reboot.
void relay_save_standard_hid(bool standard)
{
    nvs_handle_t h;
    if (nvs_open(GESTURE_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, STD_HID_NVS_KEY, standard ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "standard_hid %d saved (reboot and re-pair to apply)", standard ? 1 : 0);
}

// Same send logic and retry semantics as before, shared by live and replayed touches.
static void send_touch_evt(bool down, uint16_t x, uint16_t y, bool *sent_down)
{
    bool sent = ble_hogp_send_touch(down, x, y);
    if (!down && *sent_down) {
        while (!sent && ble_hogp_ready()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            sent = ble_hogp_send_touch(false, x, y);
        }
        *sent_down = false;
    } else if (down && !*sent_down) {
        int64_t deadline = esp_timer_get_time() + PRESS_RETRY_MS * 1000;
        while (!sent && ble_hogp_ready() && esp_timer_get_time() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(5));
            sent = ble_hogp_send_touch(true, x, y);
        }
        *sent_down = sent;
    }
}

static void send_key_bits(uint8_t bits, const char *why)
{
    bool sent = ble_hogp_send_keys(bits);
    int64_t deadline = esp_timer_get_time() + KEY_RETRY_MS * 1000;
    while (!sent && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
        sent = ble_hogp_send_keys(bits);
    }
    sent = ble_hogp_send_keys(0);
    deadline = esp_timer_get_time() + KEY_RETRY_MS * 1000;
    while (!sent && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
        sent = ble_hogp_send_keys(0);
    }
    ESP_LOGI(TAG, "%s", why);
}

static const char *key_label(uint8_t key)
{
    switch (key) {
    case GESTURE_KEY_BACK:
        return "gesture Back";
    case GESTURE_KEY_HOME:
        return "gesture Home";
    case GESTURE_KEY_UP:
        return "gesture Up";
    case GESTURE_KEY_DOWN:
        return "gesture Down";
    case GESTURE_KEY_LEFT:
        return "gesture Left";
    case GESTURE_KEY_RIGHT:
        return "gesture Right";
    case GESTURE_KEY_SELECT:
        return "gesture Select";
    default:
        return "gesture key";
    }
}

static void send_gesture_out(const gesture_out_t *out, bool *sent_down)
{
    for (uint8_t i = 0; i < out->n; i++) {
        const gesture_ev_t *ev = &out->ev[i];
        if (ev->is_key) {
            // send_key_bits does press and release, so act on the press event only.
            if (ev->down) {
                send_key_bits(ev->key, key_label(ev->key));
            }
        } else if (ble_hogp_ready()) {
            send_touch_evt(ev->down, ev->x, ev->y, sent_down);
        } else {
            *sent_down = false; // the host drops the contact with the connection
        }
    }
}

// Logs D-pad mode transitions once each. Call with the mode before and after
// a feed/tick plus send.
static void log_dpad_change(bool before, bool after)
{
    if (before == after) {
        return;
    }
    ESP_LOGI(TAG, "dpad %s", after ? "on" : "off");
}

// The engine runs here only, the single consumer, so it needs no lock.
// Move coalescing stays but only while the engine is passing the contact
// through: a held edge press must keep every sample or it could miss the
// trigger. gesture_is_passthrough is false during HELD, FIRED and DPAD
// contacts, so D-pad samples are kept. On timeout the hold expiry is polled
// through gesture_tick.
static void relay_task(void *arg)
{
    (void)arg;
    touch_evt_t e, next;
    bool sent_down = false;
    for (;;) {
        if (xQueueReceive(s_touch_q, &e, pdMS_TO_TICKS(20)) != pdTRUE) {
            bool before = gesture_dpad_active(&s_gesture);
            gesture_out_t out;
            gesture_tick(&s_gesture, (uint32_t)(esp_timer_get_time() / 1000), &out);
            send_gesture_out(&out, &sent_down);
            bool after = gesture_dpad_active(&s_gesture);
            s_dpad_active = after;
            log_dpad_change(before, after);
            continue;
        }
        if (e.is_key) {
            if (relay_get_gestures()) {
                send_key_bits(GESTURE_KEY_BACK, "button Back");
            }
            continue;
        }
        while (e.down && sent_down && gesture_is_passthrough(&s_gesture) &&
               xQueuePeek(s_touch_q, &next, 0) == pdTRUE && next.down && !next.is_key) {
            xQueueReceive(s_touch_q, &e, 0);
        }
        bool before = gesture_dpad_active(&s_gesture);
        gesture_out_t out;
        gesture_feed(&s_gesture, e.down, e.x, e.y, (uint32_t)(esp_timer_get_time() / 1000), &out);
        send_gesture_out(&out, &sent_down);
        bool after = gesture_dpad_active(&s_gesture);
        s_dpad_active = after;
        log_dpad_change(before, after);
    }
}

static void set_led(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led) {
        led_strip_set_pixel(s_led, 0, r, g, b);
        led_strip_refresh(s_led);
    }
}

static void led_init(void)
{
    const led_strip_config_t strip = {.strip_gpio_num = PIN_LED, .max_leds = 1};
    const led_strip_rmt_config_t rmt = {.resolution_hz = 10 * 1000 * 1000};
    if (led_strip_new_rmt_device(&strip, &rmt, &s_led) != ESP_OK) {
        s_led = NULL;
    }
}

// A PC sends SOF packets, a charger or the monitor does not. This keeps the serial
// console and normal flashing working whenever the board is plugged into a computer.
static bool pc_attached(void)
{
    int64_t until = esp_timer_get_time() + PC_DETECT_MS * 1000;
    while (esp_timer_get_time() < until) {
        if (usb_serial_jtag_is_connected()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

// An image is confirmed only once it has run the path most likely to crash it:
// stable Wi-Fi, then a touch device attached in USB host mode. Until then any reset
// rolls back. Without a touch device it stays unconfirmed but keeps running, and
// `POST /confirm` or the next OTA upload accepts it. Development build only;
// the user build has no networking and no timed rollback.
#ifdef CONFIG_U2B_NETWORK
static void check_rollback(bool *pending)
{
    static int64_t wifi_since, touch_since;
    static bool wifi_proven;
    int64_t now = esp_timer_get_time() / 1000000;

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        *pending = false; // confirmed through the network API
        return;
    }

    wifi_since = !netsvc_connected() ? 0 : wifi_since ? wifi_since : now;
    wifi_proven |= wifi_since && now - wifi_since >= HEALTHY_S;
    if (!wifi_proven) {
        if (now >= ROLLBACK_AFTER_S) {
            ESP_LOGE(TAG, "no stable Wi-Fi on the new image, rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        return;
    }

    usb_touch_status_t u;
    usb_touch_get_status(&u);
    touch_since = !u.has_touch ? 0 : touch_since ? touch_since : now;
    if (touch_since && now - touch_since >= HEALTHY_S) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            *pending = false;
            ESP_LOGI(TAG, "image confirmed");
        } else {
            ESP_LOGE(TAG, "confirm failed: %s", esp_err_to_name(err));
        }
    }
}
#endif

void app_main(void)
{
    logring_init();
    ESP_LOGI(TAG, "version %s", esp_app_get_description()->version);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
#ifdef CONFIG_U2B_NETWORK
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif

    gpio_config_t btn = {.pin_bit_mask = 1ULL << PIN_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&btn);
    led_init();
    set_led(16, 16, 16);

#ifdef CONFIG_U2B_NETWORK
    bool pending = false;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK) {
        pending = state == ESP_OTA_IMG_PENDING_VERIFY;
    }
#endif

    touch_map_default(&s_map);
    s_gestures_enabled = gestures_load();
    s_standard_hid = std_hid_load();
    gesture_cfg_t gc = gesture_default_cfg();
    // Standard HID has no keys: force the engine off without changing the
    // saved gestures setting, or swipes would be swallowed for nothing.
    gc.enabled = s_gestures_enabled && !s_standard_hid;
    gesture_init(&s_gesture, gc);
    s_dpad_active = false;
    s_touch_q = xQueueCreate(64, sizeof(touch_evt_t));
    xTaskCreate(relay_task, "relay", 4096, NULL, 7, NULL); // logs, so needs room for vsnprintf

    ble_hogp_set_standard_hid(s_standard_hid);
    ble_hogp_init();
#ifdef CONFIG_U2B_NETWORK
    netsvc_start();
#endif

    bool maintenance = pc_attached();
    if (maintenance) {
        ESP_LOGW(TAG, "PC detected: maintenance mode, USB host disabled");
        serial_cfg_start();
    } else {
        ESP_LOGI(TAG, "no PC: starting USB host, console moves to the network log");
        logring_set_console(false);
        usb_touch_start();
    }

    int64_t held_since = 0;
    for (bool blink = false;; blink = !blink) {
        vTaskDelay(pdMS_TO_TICKS(250));
#ifdef CONFIG_U2B_NETWORK
        if (pending) {
            check_rollback(&pending);
        }
#endif

        if (gpio_get_level(PIN_BUTTON) == 0) {
            int64_t now = esp_timer_get_time();
            if (!held_since) {
                held_since = now;
            } else if (now - held_since > FORGET_HOLD_MS * 1000LL) {
                ble_hogp_clear_bonds();
                held_since = 0;
                set_led(32, 0, 32);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else if (held_since) {
            if (esp_timer_get_time() - held_since < BUTTON_SHORT_MS * 1000LL) {
                relay_request_back(); // relay_task sends it if gestures are on
            }
            held_since = 0;
        }

        usb_touch_status_t u;
        usb_touch_get_status(&u);
        if (maintenance) {
            set_led(0, 0, blink ? 24 : 4);          // blue: USB belongs to the PC
        } else if (!u.has_touch) {
            set_led(blink ? 24 : 0, 0, 0);          // red blink: no touch device
        } else if (!ble_hogp_ready()) {
            set_led(blink ? 24 : 0, blink ? 24 : 0, 0); // yellow blink: waiting for Bluetooth
        } else if (s_dpad_active) {
            set_led(0, 16, 16);                     // cyan: D-pad mode
        } else {
            set_led(0, 16, 0);                      // green: relaying
        }
    }
}
