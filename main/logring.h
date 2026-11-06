// Keeps recent log output in RAM so it can be read over the network while the
// USB port is in host mode and the serial console is unavailable.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void logring_init(void);
void logring_set_console(bool enabled);

// Copies bytes starting at absolute position `since`. Returns the count copied
// and sets `next` to the position to resume from. Old data is skipped if the
// ring has wrapped past `since`.
size_t logring_read(uint32_t since, char *out, size_t max, uint32_t *next);
