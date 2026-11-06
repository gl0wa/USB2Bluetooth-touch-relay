#include "netsvc.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_hogp.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logring.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "relay.h"
#include "usb_touch.h"

#define HOSTNAME "u2b-touch"
#define TOKEN_MAX 65
#define OTA_CHUNK 4096
#define OTA_MAX_IDLE 2 // receive timeouts tolerated, at recv_wait_timeout each

static const char *TAG = "net";
static char s_token[TOKEN_MAX];
static volatile bool s_connected;

static bool authorised(httpd_req_t *r)
{
    char got[TOKEN_MAX] = {0};
    bool ok = false;
    if (s_token[0] && httpd_req_get_hdr_value_str(r, "X-Token", got, sizeof got) == ESP_OK) {
        // Constant-time comparison over the stored token length.
        size_t n = strlen(s_token);
        uint8_t diff = strlen(got) != n;
        for (size_t i = 0; i < n; i++) {
            diff |= (uint8_t)(got[i] ^ s_token[i]);
        }
        ok = diff == 0;
    }
    if (!ok) {
        httpd_resp_send_err(r, HTTPD_401_UNAUTHORIZED, "token required");
    }
    return ok;
}

static bool running_unconfirmed(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

static esp_err_t h_status(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    ble_hogp_status_t b;
    usb_touch_status_t u;
    ble_hogp_get_status(&b);
    usb_touch_get_status(&u);
    wifi_ap_record_t ap = {0};
    esp_wifi_sta_get_ap_info(&ap);
    const esp_partition_t *run = esp_ota_get_running_partition();
    char out[768];
    int n = snprintf(out, sizeof out,
                     "{\"version\":\"%s\",\"partition\":\"%s\",\"unconfirmed\":%d,\"uptime_s\":%" PRIu64 ",\"reset_reason\":%d,"
                     "\"free_heap\":%" PRIu32 ",\"rssi\":%d,\"gestures\":%d,"
                     "\"ble\":{\"connected\":%d,\"encrypted\":%d,\"subscribed\":%d,\"bonds\":%d,\"itvl\":%u,"
                     "\"sent\":%" PRIu32 ",\"failed\":%" PRIu32 ",\"keys\":%" PRIu32 "},"
                     "\"usb\":{\"host\":%d,\"attached\":%d,\"touch\":%d,\"vid\":%u,\"pid\":%u,\"kind\":%u,"
                     "\"iface\":%u,\"contacts\":%u,\"reports\":%" PRIu32 ",\"touches\":%" PRIu32 ",\"errors\":%" PRIu32 "}}\n",
                     esp_app_get_description()->version, run->label, running_unconfirmed(), (uint64_t)(esp_timer_get_time() / 1000000),
                     (int)esp_reset_reason(), esp_get_free_heap_size(), ap.rssi, relay_get_gestures() ? 1 : 0,
                     b.connected, b.encrypted, b.subscribed, b.bonds, b.conn_itvl, b.sent, b.send_failed, b.keys_sent,
                     u.started, u.attached, u.has_touch, u.vid, u.pid, u.kind, u.iface, u.contacts,
                     u.reports, u.touches, u.errors);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, out, n);
}

static int query_int(httpd_req_t *r, const char *key, int fallback)
{
    char q[96], v[16];
    if (httpd_req_get_url_query_str(r, q, sizeof q) == ESP_OK && httpd_query_key_value(q, key, v, sizeof v) == ESP_OK) {
        return atoi(v);
    }
    return fallback;
}

static esp_err_t h_log(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    char q[32], v[16], next_hdr[16];
    uint32_t since = 0, next = 0;
    if (httpd_req_get_url_query_str(r, q, sizeof q) == ESP_OK && httpd_query_key_value(q, "since", v, sizeof v) == ESP_OK) {
        since = strtoul(v, NULL, 10);
    }
    const size_t max = 8192;
    char *buf = malloc(max);
    if (!buf) {
        return httpd_resp_send_500(r);
    }
    size_t n = logring_read(since, buf, max, &next);
    snprintf(next_hdr, sizeof next_hdr, "%" PRIu32, next);
    httpd_resp_set_hdr(r, "X-Next", next_hdr);
    httpd_resp_set_type(r, "text/plain");
    esp_err_t err = httpd_resp_send(r, buf, n);
    free(buf);
    return err;
}

static esp_err_t h_desc(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    uint8_t *d = malloc(1024);
    char line[8];
    if (!d) {
        return httpd_resp_send_500(r);
    }
    httpd_resp_set_type(r, "text/plain");
    for (int i = 0; i < 4; i++) {
        uint8_t iface = 0;
        size_t n = usb_touch_get_descriptor(i, d, 1024, &iface);
        if (n == 0) {
            continue;
        }
        snprintf(line, sizeof line, "%u: ", iface);
        httpd_resp_sendstr_chunk(r, line);
        for (size_t k = 0; k < n; k++) {
            snprintf(line, sizeof line, "%02x", d[k]);
            httpd_resp_sendstr_chunk(r, line);
        }
        httpd_resp_sendstr_chunk(r, "\n");
    }
    free(d);
    return httpd_resp_sendstr_chunk(r, NULL);
}

// Injects a touch into the BLE path, so Bluetooth can be tested without the monitor.
static esp_err_t h_touch(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    int x = query_int(r, "x", -1), y = query_int(r, "y", -1), down = query_int(r, "down", 1);
    if (x < 0 || x > 32767 || y < 0 || y > 32767) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "x and y must be 0..32767");
    }
    relay_submit(down != 0, (uint16_t)x, (uint16_t)y);
    return httpd_resp_sendstr(r, ble_hogp_ready() ? "sent\n" : "queued, BLE not ready\n");
}

// Drags on the device, so points stay ~15 ms apart. One HTTP request per point
// arrives ~90 ms apart over Wi-Fi, which cannot test smooth dragging.
static esp_err_t h_swipe(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    int x1 = query_int(r, "x1", -1), y1 = query_int(r, "y1", -1);
    int x2 = query_int(r, "x2", -1), y2 = query_int(r, "y2", -1);
    int ms = query_int(r, "ms", -1);
    if (x1 < 0 || x1 > 32767 || y1 < 0 || y1 > 32767 ||
        x2 < 0 || x2 > 32767 || y2 < 0 || y2 > 32767 ||
        ms < 50 || ms > 3000) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "x1, y1, x2, y2 must be 0..32767, ms 50..3000");
    }
    int steps = ms / 15;
    if (steps < 1) {
        steps = 1;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x1 + (x2 - x1) * i / steps;
        int y = y1 + (y2 - y1) * i / steps;
        relay_submit(true, (uint16_t)x, (uint16_t)y);
        if (i < steps) {
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
    relay_submit(false, (uint16_t)x2, (uint16_t)y2);
    return httpd_resp_sendstr(r, ble_hogp_ready() ? "sent\n" : "queued, BLE not ready\n");
}

static void reboot_later(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

// Persistent edge-gesture switch. Stored in default NVS, namespace "cfg",
// so re-provisioning (which rewrites devcfg) keeps it. Applies immediately.
static esp_err_t h_config(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    int g = query_int(r, "gestures", -1);
    if (g != 0 && g != 1) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "gestures must be 0 or 1");
    }
    relay_set_gestures(g != 0);
    char out[16];
    int n = snprintf(out, sizeof out, "gestures=%d\n", g);
    return httpd_resp_send(r, out, n);
}

// Sends Back/Home directly, for testing Bluetooth keys without gestures.
// Works regardless of the switch.
static esp_err_t h_key(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    char q[64], v[16];
    if (httpd_req_get_url_query_str(r, q, sizeof q) != ESP_OK ||
        httpd_query_key_value(q, "k", v, sizeof v) != ESP_OK) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "k must be back or home");
    }
    uint8_t bit;
    if (strcmp(v, "back") == 0) {
        bit = 1;
    } else if (strcmp(v, "home") == 0) {
        bit = 2;
    } else {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "k must be back or home");
    }
    bool sent = ble_hogp_send_keys(bit);
    int64_t deadline = esp_timer_get_time() + 200 * 1000;
    while (!sent && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
        sent = ble_hogp_send_keys(bit);
    }
    bool rel = ble_hogp_send_keys(0);
    deadline = esp_timer_get_time() + 200 * 1000;
    while (!rel && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
        rel = ble_hogp_send_keys(0);
    }
    return httpd_resp_sendstr(r, sent && rel ? "sent\n" : "queued, BLE not ready\n");
}

static esp_err_t h_reboot(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    httpd_resp_sendstr(r, "rebooting\n");
    xTaskCreate(reboot_later, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void wifi_pause_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(200)); // let the HTTP response leave first
    esp_wifi_stop();
    ESP_LOGW(TAG, "Wi-Fi off for %d s", (int)(intptr_t)arg);
    vTaskDelay(pdMS_TO_TICKS(1000 * (int)(intptr_t)arg));
    esp_wifi_start();
    vTaskDelete(NULL);
}

// Turns Wi-Fi off for a while, to test Bluetooth without radio sharing.
static esp_err_t h_wifi_pause(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    int seconds = query_int(r, "s", 0);
    if (seconds < 1 || seconds > 600) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "s must be 1..600");
    }
    httpd_resp_sendstr(r, "pausing Wi-Fi\n");
    xTaskCreate(wifi_pause_task, "wifi_pause", 3072, (void *)(intptr_t)seconds, 3, NULL);
    return ESP_OK;
}

static esp_err_t h_forget(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    ble_hogp_clear_bonds();
    return httpd_resp_sendstr(r, "bonds cleared\n");
}

static esp_err_t h_confirm(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    if (running_unconfirmed() && esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        return httpd_resp_send_500(r);
    }
    return httpd_resp_sendstr(r, "confirmed\n");
}

static esp_err_t h_ota(httpd_req_t *r)
{
    if (!authorised(r)) {
        return ESP_OK;
    }
    // esp_ota_begin refuses while the running image is unconfirmed. Reaching this
    // point proves it can receive updates, so accept it rather than lock OTA out.
    if (running_unconfirmed()) {
        ESP_LOGW(TAG, "confirming running image to allow OTA");
        esp_ota_mark_app_valid_cancel_rollback();
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota = 0;
    if (!part || r->content_len == 0 || r->content_len > part->size) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad image size");
    }
    char *buf = malloc(OTA_CHUNK);
    if (!buf) {
        return httpd_resp_send_500(r);
    }
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    size_t left = r->content_len;
    int idle = 0;
    ESP_LOGI(TAG, "OTA: %u bytes to %s", (unsigned)left, part->label);
    while (err == ESP_OK && left > 0) {
        int n = httpd_req_recv(r, buf, left < OTA_CHUNK ? left : OTA_CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++idle < OTA_MAX_IDLE) {
            continue; // the server is single threaded, so never wait forever
        }
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        idle = 0;
        err = esp_ota_write(ota, buf, n);
        left -= n;
    }
    free(buf);
    if (err != ESP_OK) {
        if (ota) {
            esp_ota_abort(ota);
        }
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        return httpd_resp_send_500(r);
    }
    err = esp_ota_end(ota); // validates the image
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(part);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA rejected: %s", esp_err_to_name(err));
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "image rejected");
    }
    ESP_LOGI(TAG, "OTA complete, rebooting");
    httpd_resp_sendstr(r, "ok, rebooting\n");
    xTaskCreate(reboot_later, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 15;
    cfg.max_uri_handlers = 16;
    httpd_handle_t srv;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));
    const httpd_uri_t routes[] = {
        {.uri = "/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/log", .method = HTTP_GET, .handler = h_log},
        {.uri = "/usb/desc", .method = HTTP_GET, .handler = h_desc},
        {.uri = "/touch", .method = HTTP_POST, .handler = h_touch},
        {.uri = "/swipe", .method = HTTP_POST, .handler = h_swipe},
        {.uri = "/config", .method = HTTP_POST, .handler = h_config},
        {.uri = "/key", .method = HTTP_POST, .handler = h_key},
        {.uri = "/ota", .method = HTTP_POST, .handler = h_ota},
        {.uri = "/reboot", .method = HTTP_POST, .handler = h_reboot},
        {.uri = "/forget", .method = HTTP_POST, .handler = h_forget},
        {.uri = "/confirm", .method = HTTP_POST, .handler = h_confirm},
        {.uri = "/wifi-pause", .method = HTTP_POST, .handler = h_wifi_pause},
    };
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) {
        esp_err_t err = httpd_register_uri_handler(srv, &routes[i]);
        if (err != ESP_OK) { // keep booting: losing one route must not lose OTA
            ESP_LOGE(TAG, "route %s: %s", routes[i].uri, esp_err_to_name(err));
        }
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "IP " IPSTR " (%s.local)", IP2STR(&e->ip_info.ip), HOSTNAME);
        s_connected = true;
    }
}

void netsvc_start(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    size_t ssid_len = sizeof ssid, pass_len = sizeof pass, tok_len = sizeof s_token;
    nvs_handle_t h;
    esp_err_t err = nvs_flash_init_partition("devcfg");
    if (err == ESP_OK) {
        err = nvs_open_from_partition("devcfg", "net", NVS_READONLY, &h);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(h, "ssid", ssid, &ssid_len);
        if (err == ESP_OK) {
            err = nvs_get_str(h, "token", s_token, &tok_len);
        }
        if (err == ESP_OK) {
            err = nvs_get_str(h, "pass", pass, &pass_len);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                pass[0] = 0; // open network
                err = ESP_OK;
            }
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "not provisioned (%s): networking disabled", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, HOSTNAME);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, ssid, sizeof wc.sta.ssid);
    memcpy(wc.sta.password, pass, sizeof pass < sizeof wc.sta.password ? sizeof pass : sizeof wc.sta.password);
    memset(pass, 0, sizeof pass);
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(HOSTNAME);
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    http_start();
    ESP_LOGI(TAG, "network service started");
}

bool netsvc_connected(void)
{
    return s_connected;
}
