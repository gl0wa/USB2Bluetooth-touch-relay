// The event path shared by the USB input and the network test endpoint.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Coordinates are normalised to 0..32767. Safe to call from any task.
void relay_submit(bool down, uint16_t x, uint16_t y);
