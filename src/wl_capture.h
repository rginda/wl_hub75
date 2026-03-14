#pragma once
#include <stdint.h>

bool wl_capture_init();
void wl_capture_cleanup();
bool wl_capture_frame(uint8_t* out_buffer, int out_width, int out_height, int out_bpp,
                      int crop_x, int crop_y, int crop_w, int crop_h, bool stretch);

// Profiling data
struct wl_capture_profile_data {
    double wait_time_ms;
    double process_time_ms;
};

void wl_capture_set_profile(bool enable);
wl_capture_profile_data wl_capture_get_last_profile();
