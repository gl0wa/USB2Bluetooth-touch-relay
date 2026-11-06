// Serial settings while a computer owns the USB port (maintenance mode).
// See docs/user-guide.md and docs/development.md.
#pragma once

// Starts the reader task once; safe to call only when maintenance is true.
void serial_cfg_start(void);
