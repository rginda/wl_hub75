#include "wl_capture.h"
#include <iostream>
#include <wayland-client.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include <cstring>
#include <chrono>

// Wayland capture state
static struct wl_display *display = nullptr;
static struct wl_registry *registry = nullptr;
static struct wl_shm *shm = nullptr;
static struct wl_output *output = nullptr;
static struct zwlr_screencopy_manager_v1 *screencopy_manager = nullptr;

static struct zwlr_screencopy_frame_v1 *frame = nullptr;
static struct wl_buffer *buffer = nullptr;

static int shm_fd = -1;
static uint8_t *shm_data = nullptr;
static size_t shm_size = 0;

static uint32_t capture_width = 0;
static uint32_t capture_height = 0;
static uint32_t capture_stride = 0;
static uint32_t capture_format = 0;

static bool frame_ready = false;
static bool frame_failed = false;

static bool profiling_enabled = false;
static wl_capture_profile_data last_profile = {0, 0};

// Allocates anonymous memory mapping for SHM transfers
static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wl_capture_shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Screencopy callbacks
static void frame_handle_buffer(void *data, struct zwlr_screencopy_frame_v1 *f,
                                uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    capture_format = format;
    capture_width = width;
    capture_height = height;
    capture_stride = stride;
    
    size_t size = stride * height;
    if (size != shm_size) {
        if (shm_data) munmap(shm_data, shm_size);
        if (shm_fd >= 0) close(shm_fd);
        
        shm_fd = allocate_shm_file(size);
        if (shm_fd < 0) {
            std::cerr << "Failed to allocate Wayland SHM file." << std::endl;
            return;
        }
        shm_data = (uint8_t *)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        shm_size = size;
    }
    
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, shm_fd, size);
    if (buffer) wl_buffer_destroy(buffer);
    buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool);
    
    zwlr_screencopy_frame_v1_copy(f, buffer);
}

static void frame_handle_flags(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t flags) {}
static void frame_handle_ready(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    frame_ready = true;
}
static void frame_handle_failed(void *data, struct zwlr_screencopy_frame_v1 *f) {
    frame_failed = true;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
};

// Registry callbacks to find required globals
static void registry_handle_global(void *data, struct wl_registry *r, uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = (struct wl_shm *)wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (!output) {
            output = (struct wl_output *)wl_registry_bind(r, name, &wl_output_interface, 1);
        }
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        screencopy_manager = (struct zwlr_screencopy_manager_v1 *)wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *r, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

bool wl_capture_init() {
    display = wl_display_connect(nullptr);
    if (!display) {
        std::cerr << "Failed to connect to Wayland display. Is WAYLAND_DISPLAY set and the compositor running?" << std::endl;
        return false;
    }
    
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, nullptr);
    
    // Dispatch to negotiate globals
    wl_display_roundtrip(display);
    
    if (!shm || !output || !screencopy_manager) {
        std::cerr << "Compositor is missing required interfaces (shm/output/screencopy)." << std::endl;
        std::cerr << "Ensure you are using a compositor matching `cage` that supports wlr-screencopy-unstable." << std::endl;
        wl_capture_cleanup();
        return false;
    }
    return true;
}

void wl_capture_cleanup() {
    if (buffer) wl_buffer_destroy(buffer);
    if (frame) zwlr_screencopy_frame_v1_destroy(frame);
    if (shm_data) munmap(shm_data, shm_size);
    if (shm_fd >= 0) close(shm_fd);
    
    buffer = nullptr;
    frame = nullptr;
    shm_data = nullptr;
    shm_fd = -1;
    shm_size = 0;
    
    if (screencopy_manager) zwlr_screencopy_manager_v1_destroy(screencopy_manager);
    if (output) wl_output_destroy(output);
    if (shm) wl_shm_destroy(shm);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    
    screencopy_manager = nullptr;
    output = nullptr;
    shm = nullptr;
    registry = nullptr;
    display = nullptr;
}

bool wl_capture_frame(uint8_t* out_buffer, int out_width, int out_height, int out_bpp, 
                      int crop_x, int crop_y, int crop_w, int crop_h, bool stretch) {
    if (!display || !screencopy_manager || !output) return false;
    
    auto wait_start = std::chrono::steady_clock::now();

    bool is_region_capture = (crop_w > 0 && crop_h > 0);

    if (is_region_capture) {
        frame = zwlr_screencopy_manager_v1_capture_output_region(screencopy_manager, 0, output, crop_x, crop_y, crop_w, crop_h);
    } else {
        frame = zwlr_screencopy_manager_v1_capture_output(screencopy_manager, 0, output);
    }

    if (!frame) return false;
    
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, nullptr);
    
    frame_ready = false;
    frame_failed = false;
    
    // Pump Wayland events until the compositor signals our frame is fully copied to SHM
    while (!frame_ready && !frame_failed) {
        if (wl_display_dispatch(display) < 0) {
            break;
        }
    }
    
    auto wait_end = std::chrono::steady_clock::now();
    if (profiling_enabled) {
        last_profile.wait_time_ms = std::chrono::duration<double, std::milli>(wait_end - wait_start).count();
    }

    bool ret = false;
    if (frame_ready && shm_data && capture_width > 0 && capture_height > 0) {
        auto process_start = std::chrono::steady_clock::now();
        int bytes_per_pixel = capture_stride / capture_width;
        
        static bool first_frame = true;
        if (first_frame) {
            std::cout << "Wayland Capture Info - Width: " << capture_width 
                      << ", Height: " << capture_height 
                      << ", Stride: " << capture_stride 
                      << ", BPP: " << bytes_per_pixel 
                      << ", Format: " << capture_format 
                      << (is_region_capture ? " (Region Capture)" : " (Full Capture)") << std::endl;
            first_frame = false;
        }

        // Processing variables
        int proc_crop_x = crop_x;
        int proc_crop_y = crop_y;
        int proc_crop_w = crop_w;
        int proc_crop_h = crop_h;

        if (is_region_capture) {
            // If we did a region capture, the source buffer starts at 0,0 and has size capture_width x capture_height
            proc_crop_x = 0;
            proc_crop_y = 0;
            proc_crop_w = capture_width;
            proc_crop_h = capture_height;
        } else if (proc_crop_w <= 0 || proc_crop_h <= 0) {
            // Full capture, no crop specified, use entire screen
            proc_crop_x = 0;
            proc_crop_y = 0;
            proc_crop_w = capture_width;
            proc_crop_h = capture_height;
        }

        // Clip crop to screen bounds constraints (only for full capture)
        if (!is_region_capture) {
            if (proc_crop_x < 0) proc_crop_x = 0;
            if (proc_crop_y < 0) proc_crop_y = 0;
            if (proc_crop_x + proc_crop_w > (int)capture_width) proc_crop_w = capture_width - proc_crop_x;
            if (proc_crop_y + proc_crop_h > (int)capture_height) proc_crop_h = capture_height - proc_crop_y;
        }

        // Render targets
        int render_w = out_width;
        int render_h = out_height;
        int offset_x = 0;
        int offset_y = 0;

        if (!stretch) {
            // Calculate aspect ratio preserving scaling (Letterbox / Pillarbox)
            float crop_aspect = (float)proc_crop_w / proc_crop_h;
            float out_aspect = (float)out_width / out_height;

            if (crop_aspect > out_aspect) {
                // Image is wider than matrix: Pillarbox (black bars top/bottom)
                render_w = out_width;
                render_h = (int)(out_width / crop_aspect);
                offset_y = (out_height - render_h) / 2;
            } else {
                // Image is taller than matrix: Letterbox (black bars left/right)
                render_h = out_height;
                render_w = (int)(out_height * crop_aspect);
                offset_x = (out_width - render_w) / 2;
            }
        }

        for (int y = 0; y < out_height; ++y) {
            for (int x = 0; x < out_width; ++x) {
                int dest_index = (y * out_width + x) * out_bpp;

                // Check if current pixel is inside the active render area (not black bars)
                if (x < offset_x || x >= offset_x + render_w ||
                    y < offset_y || y >= offset_y + render_h) {
                    out_buffer[dest_index]     = 0;
                    out_buffer[dest_index + 1] = 0;
                    out_buffer[dest_index + 2] = 0;
                    continue;
                }

                // Map out_buffer coordinate to crop space
                // (x - offset_x) / render_w gives 0.0 to 1.0 progress horizontally
                float norm_x = (float)(x - offset_x + 0.5f) / render_w;
                float norm_y = (float)(y - offset_y + 0.5f) / render_h;

                int src_x = proc_crop_x + (int)(norm_x * proc_crop_w);
                int src_y = proc_crop_y + (int)(norm_y * proc_crop_h);
                
                // Bounds guard
                if (src_x < 0 || src_y < 0 || src_x >= (int)capture_width || src_y >= (int)capture_height) {
                    out_buffer[dest_index]     = 0;
                    out_buffer[dest_index + 1] = 0;
                    out_buffer[dest_index + 2] = 0;
                    continue;
                }
                
                const uint8_t* p = shm_data + src_y * capture_stride + src_x * bytes_per_pixel;
                
                if (bytes_per_pixel >= 4) {
                    // Wayland typically defaults to XRGB8888 or ARGB8888 (Little Endian: B, G, R, A)
                    out_buffer[dest_index]     = p[2]; // R
                    out_buffer[dest_index + 1] = p[1]; // G
                    out_buffer[dest_index + 2] = p[0]; // B
                } else if (bytes_per_pixel == 2) {
                    // Fallback to RGB565 format parsing
                    uint16_t val = *(const uint16_t*)p;
                    out_buffer[dest_index]     = ((val >> 11) & 0x1F) * 255 / 31;
                    out_buffer[dest_index + 1] = ((val >> 5) &  0x3F) * 255 / 63;
                    out_buffer[dest_index + 2] = (val & 0x1F)         * 255 / 31;
                } else {
                    out_buffer[dest_index]     = 0;
                    out_buffer[dest_index + 1] = 0;
                    out_buffer[dest_index + 2] = 0;
                }
            }
        }
        auto process_end = std::chrono::steady_clock::now();
        if (profiling_enabled) {
            last_profile.process_time_ms = std::chrono::duration<double, std::milli>(process_end - process_start).count();
        }
        ret = true;
    }
    
    if (frame) {
        zwlr_screencopy_frame_v1_destroy(frame);
        frame = nullptr;
    }
    
    return ret;
}

void wl_capture_set_profile(bool enable) {
    profiling_enabled = enable;
}

wl_capture_profile_data wl_capture_get_last_profile() {
    return last_profile;
}
