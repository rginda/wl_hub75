#pragma once
#include <stdint.h>

bool wl_capture_init();
void wl_capture_cleanup();
bool wl_capture_frame(uint8_t* out_buffer, int out_width, int out_height, int out_bpp);
