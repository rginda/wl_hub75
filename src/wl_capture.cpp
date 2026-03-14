#include "wl_capture.h"
#include <iostream>
#include <wayland-client.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include <cstring>

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

bool wl_capture_frame(uint8_t* out_buffer, int out_width, int out_height, int out_bpp) {
    if (!display || !screencopy_manager || !output) return false;
    
    frame = zwlr_screencopy_manager_v1_capture_output(screencopy_manager, 0, output);
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
    
    bool ret = false;
    if (frame_ready && shm_data && capture_width > 0 && capture_height > 0) {
        int bytes_per_pixel = capture_stride / capture_width;
        
        static bool first_frame = true;
        if (first_frame) {
            std::cout << "Wayland Capture Info - Width: " << capture_width 
                      << ", Height: " << capture_height 
                      << ", Stride: " << capture_stride 
                      << ", BPP: " << bytes_per_pixel 
                      << ", Format: " << capture_format << std::endl;
            first_frame = false;
        }

        // Pico-8 scales by integer multiples until it maximizes screen space.
        // It maintains a perfect 128x128 aspect ratio.
        int scale = capture_height / out_height; // Max integer scale that fits height (and width since 1080p is wider)
        if (scale < 1) scale = 1;

        int rendered_size = scale * out_height;
        int x_offset = ((int)capture_width - rendered_size) / 2;
        int y_offset = ((int)capture_height - rendered_size) / 2;
        
        for (int y = 0; y < out_height; ++y) {
            for (int x = 0; x < out_width; ++x) {
                // Sample exactly the top-left pixel (or center) of the integer-scaled block
                int src_x = x_offset + (x * scale) + (scale / 2);
                int src_y = y_offset + (y * scale) + (scale / 2);
                
                int dest_index = (y * out_width + x) * out_bpp;
                
                // Bounds guard just in case offsets went negative
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
        ret = true;
    }
    
    if (frame) {
        zwlr_screencopy_frame_v1_destroy(frame);
        frame = nullptr;
    }
    
    return ret;
}
