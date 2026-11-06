// USB Serial/JTAG settings console, maintenance mode only (a computer owns
// the USB PHY there; in USB host mode the serial port does not exist).
//
// Console interaction: with CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG the startup
// VFS talks to the FIFO directly ("no driver" mode). Installing the
// usb_serial_jtag driver and switching the VFS to it with
// usb_serial_jtag_vfs_use_driver() routes both ESP_LOG output and these
// responses through the same TX ring buffer, so logging keeps working while
// this task owns reading. Without the switch the driver ISR and the direct
// FIFO access would compete for the same hardware. This task is the only
// reader (the app never reads stdin), polls with a short timeout so nothing
// blocks, and writes each response in a single usb_serial_jtag_write_bytes
// call as one "U2B {...}\n" line that the web page filters from log output.
#include "serial_cfg.h"
#include <string.h>
#include "ble_hogp.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay.h"

#define U2B_LINE_MAX 160

static const char *TAG = "serial";
static bool s_started;

#ifdef CONFIG_U2B_NETWORK
#define U2B_BUILD "dev"
#else
#define U2B_BUILD "user"
#endif

static void send_line(const char *s, size_t n)
{
    // One call so the line is appended to the TX ring without interleaving.
    usb_serial_jtag_write_bytes(s, n, pdMS_TO_TICKS(100));
}

static void send_json(const char *json)
{
    char out[U2B_LINE_MAX + 8];
    int n = snprintf(out, sizeof out, "U2B %s\n", json);
    if (n > 0) {
        send_line(out, (size_t)n);
    }
}

static void send_status_ok(void)
{
    ble_hogp_status_t b = {0};
    ble_hogp_get_status(&b);
    bool gest_eff = relay_get_gestures();
    bool gest_saved = relay_get_gestures_saved();
    bool hid_run = relay_get_standard_hid();
    bool hid_saved = relay_get_standard_hid_saved();
    char json[256];
    snprintf(json, sizeof json,
             "{\"ok\":true,\"version\":\"%s\",\"build\":\"%s\","
             "\"gestures\":%d,\"gestures_saved\":%d,"
             "\"standard_hid\":%d,\"standard_hid_saved\":%d,"
             "\"bonds\":%d,\"reboot_required\":%s}",
             esp_app_get_description()->version, U2B_BUILD,
             gest_eff ? 1 : 0, gest_saved ? 1 : 0,
             hid_run ? 1 : 0, hid_saved ? 1 : 0,
             b.bonds, (hid_saved != hid_run) ? "true" : "false");
    send_json(json);
}

static void send_err(const char *msg)
{
    char json[128];
    snprintf(json, sizeof json, "{\"ok\":false,\"error\":\"%s\"}", msg);
    send_json(json);
}

static void handle_line(char *line)
{
    // Strip trailing CR (web page sends \n; terminals may send \r\n).
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = 0;
    }
    if (n == 0) {
        return;
    }
    if (strcmp(line, "status") == 0) {
        send_status_ok();
    } else if (strcmp(line, "gestures on") == 0) {
        relay_set_gestures(true);
        send_status_ok();
    } else if (strcmp(line, "gestures off") == 0) {
        relay_set_gestures(false);
        send_status_ok();
    } else if (strcmp(line, "hid android") == 0) {
        relay_save_standard_hid(false);
        send_status_ok();
    } else if (strcmp(line, "hid standard") == 0) {
        relay_save_standard_hid(true);
        send_status_ok();
    } else if (strcmp(line, "forget") == 0) {
        ble_hogp_clear_bonds();
        send_status_ok();
    } else if (strcmp(line, "reboot") == 0) {
        send_json("{\"ok\":true}");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else {
        send_err("unknown command");
    }
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[U2B_LINE_MAX + 1];
    size_t len = 0;
    bool overflow = false;
    for (;;) {
        char buf[32];
        int r = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(20));
        if (r <= 0) {
            continue;
        }
        for (int i = 0; i < r; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (overflow) {
                    overflow = false;
                    len = 0;
                    send_err("line too long");
                } else {
                    line[len] = 0;
                    handle_line(line);
                    len = 0;
                }
            } else if (overflow) {
                // Discard until end of line; bounded buffer, no growth.
            } else if (len < U2B_LINE_MAX) {
                line[len++] = c;
            } else {
                overflow = true;
            }
        }
    }
}

void serial_cfg_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
            ESP_LOGW(TAG, "driver install failed, serial settings off");
            return;
        }
    }
    // Route console output through the same driver so ESP_LOG keeps working.
    usb_serial_jtag_vfs_use_driver();
    xTaskCreate(serial_task, "serial_cfg", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "serial settings on (maintenance mode)");
}
