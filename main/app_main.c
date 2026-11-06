#include <stdbool.h>
#include "ble_hogp.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid_touch.h"
#include "led_strip.h"
#include "logring.h"
#include "netsvc.h"
#include "nvs_flash.h"
#include "relay.h"
#include "touch_map.h"
#include "usb_touch.h"

#define PIN_BUTTON GPIO_NUM_0
#define PIN_LED GPIO_NUM_21
#define PC_DETECT_MS 1500       // time allowed for a PC to show up on USB after reset
#define HEALTHY_AFTER_S 30      // uptime on Wi-Fi before a new OTA image is accepted
#define ROLLBACK_AFTER_S 180    // give up on an unconfirmed image if Wi-Fi never comes up
#define FORGET_HOLD_MS 5000
#define EDGE_RETRY_MS 200

typedef struct {
    bool down;
    uint16_t x, y;
} touch_evt_t;

static const char *TAG = "app";
static QueueHandle_t s_touch_q;
static touch_map_t s_map;
static led_strip_handle_t s_led;

void relay_submit(bool down, uint16_t x, uint16_t y)
{
    touch_evt_t e = {.down = down};
    touch_map_apply(&s_map, x, y, &e.x, &e.y);
    if (xQueueSend(s_touch_q, &e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "touch queue full");
    }
}

// Coalesces moves when BLE is slower than the panel, but never drops a press or release.
static void relay_task(void *arg)
{
    (void)arg;
    touch_evt_t e, next;
    bool was_down = false;
    for (;;) {
        xQueueReceive(s_touch_q, &e, portMAX_DELAY);
        while (e.down && was_down && xQueuePeek(s_touch_q, &next, 0) == pdTRUE && next.down) {
            xQueueReceive(s_touch_q, &e, 0);
        }
        bool edge = e.down != was_down;
        int64_t deadline = esp_timer_get_time() + EDGE_RETRY_MS * 1000;
        while (!ble_hogp_send_touch(e.down, e.x, e.y) && edge && ble_hogp_ready() &&
               esp_timer_get_time() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        was_down = e.down;
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

static void check_rollback(bool *pending)
{
    int64_t up_s = esp_timer_get_time() / 1000000;
    if (netsvc_connected() && up_s >= HEALTHY_AFTER_S) {
        esp_ota_mark_app_valid_cancel_rollback();
        *pending = false;
        ESP_LOGI(TAG, "image confirmed");
    } else if (up_s >= ROLLBACK_AFTER_S) {
        ESP_LOGE(TAG, "image not confirmed, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

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
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gpio_config_t btn = {.pin_bit_mask = 1ULL << PIN_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&btn);
    led_init();
    set_led(16, 16, 16);

    bool pending = false;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK) {
        pending = state == ESP_OTA_IMG_PENDING_VERIFY;
    }

    touch_map_default(&s_map);
    s_touch_q = xQueueCreate(64, sizeof(touch_evt_t));
    xTaskCreate(relay_task, "relay", 3072, NULL, 7, NULL);

    ble_hogp_init();
    netsvc_start();

    bool maintenance = pc_attached();
    if (maintenance) {
        ESP_LOGW(TAG, "PC detected: maintenance mode, USB host disabled");
    } else {
        ESP_LOGI(TAG, "no PC: starting USB host, console moves to the network log");
        logring_set_console(false);
        usb_touch_start();
    }

    int64_t held_since = 0;
    for (bool blink = false;; blink = !blink) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (pending) {
            check_rollback(&pending);
        }

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
        } else {
            held_since = 0;
        }

        usb_touch_status_t u;
        usb_touch_get_status(&u);
        if (maintenance) {
            set_led(0, 0, blink ? 24 : 4);          // blue: USB belongs to the PC
        } else if (!u.has_touch) {
            set_led(blink ? 24 : 0, 0, 0);          // red blink: no touch device
        } else if (!ble_hogp_ready()) {
            set_led(blink ? 24 : 0, blink ? 12 : 0, 0); // amber blink: waiting for Bluetooth
        } else {
            set_led(0, 16, 0);                      // green: relaying
        }
    }
}
