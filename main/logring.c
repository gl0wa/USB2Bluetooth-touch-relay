#include "logring.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define RING_SIZE (24 * 1024)

static char s_ring[RING_SIZE];
static uint32_t s_head; // total bytes ever written
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_console;
static volatile bool s_console_on = true;

static int ring_vprintf(const char *fmt, va_list args)
{
    char line[256];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = n < (int)sizeof line ? (size_t)n : sizeof line - 1;
        taskENTER_CRITICAL(&s_lock);
        for (size_t i = 0; i < len; i++) {
            s_ring[s_head++ % RING_SIZE] = line[i];
        }
        taskEXIT_CRITICAL(&s_lock);
    }
    return s_console_on && s_console ? s_console(fmt, args) : n;
}

void logring_init(void)
{
    s_console = esp_log_set_vprintf(ring_vprintf);
}

void logring_set_console(bool enabled)
{
    s_console_on = enabled;
}

size_t logring_read(uint32_t since, char *out, size_t max, uint32_t *next)
{
    taskENTER_CRITICAL(&s_lock);
    uint32_t head = s_head;
    uint32_t oldest = head > RING_SIZE ? head - RING_SIZE : 0;
    uint32_t pos = since < oldest || since > head ? oldest : since;
    size_t n = head - pos < max ? head - pos : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_ring[(pos + i) % RING_SIZE];
    }
    taskEXIT_CRITICAL(&s_lock);
    *next = pos + n;
    return n;
}
