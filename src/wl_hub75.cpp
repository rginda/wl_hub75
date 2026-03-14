#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include "wl_capture.h"
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>

#include "led-matrix.h"
#include "graphics.h"

using namespace rgb_matrix;

// Define the dimensions of our mock display
const int WIDTH = 128;
const int HEIGHT = 128;
const int BPP = 3; // RGB24

void render_ascii(uint8_t* buffer) {
    // Move cursor to top left using ANSI escape codes to overwrite the previous frame
    std::cout << "\033[H";
    
    // We'll use the upper half block character to render two vertical pixels per character cell
    // This perfectly compensates for the roughly 1:2 aspect ratio of terminal fonts!
    for (int y = 0; y < HEIGHT; y += 2) {
        for (int x = 0; x < WIDTH; ++x) {
            int top_index = (y * WIDTH + x) * BPP;
            uint8_t tr = buffer[top_index    ];
            uint8_t tg = buffer[top_index + 1];
            uint8_t tb = buffer[top_index + 2];

            // If we're at the bottom row and height is odd, bottom pixel is black
            uint8_t br = 0, bg = 0, bb = 0;
            if (y + 1 < HEIGHT) {
                int index = ((y + 1) * WIDTH + x) * BPP;
                br = buffer[index    ];
                bg = buffer[index + 1];
                bb = buffer[index + 2];
            }

            // Foreground color (top pixel), Background color (bottom pixel), and Upper Half Block char
            std::cout << "\033[38;2;" << (int)tr << ";" << (int)tg << ";" << (int)tb 
                      << ";48;2;" << (int)br << ";" << (int)bg << ";" << (int)bb << "m"
                      << "\xE2\x96\x80"; // U+2580 Upper Half Block
        }
        std::cout << "\033[0m" << std::endl; // Reset color at end of line
    }
}

void render_matrix(Canvas *canvas, uint8_t* buffer) {
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            int index = (y * WIDTH + x) * BPP;
            uint8_t r = buffer[index    ];
            uint8_t g = buffer[index + 1];
            uint8_t b = buffer[index + 2];
            canvas->SetPixel(x, y, r, g, b);
        }
    }
}

// extract_pico8_fb removed - handled by wl_capture now

int main(int argc, char *argv[]) {
    bool ascii_mode = false;
    bool info_mode = false;
    bool stretch_mode = false;
    bool fps_mode = false;
    bool profile_mode = false;
    int capture_rate = 30;
    int crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ascii") == 0) {
            ascii_mode = true;
        } else if (strcmp(argv[i], "--info") == 0) {
            info_mode = true;
        } else if (strcmp(argv[i], "--stretch") == 0) {
            stretch_mode = true;
        } else if (strcmp(argv[i], "--fps") == 0) {
            fps_mode = true;
        } else if (strcmp(argv[i], "--profile") == 0) {
            profile_mode = true;
        } else if (strcmp(argv[i], "--capture-rate") == 0 && i + 1 < argc) {
            capture_rate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--crop") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%d,%d,%d,%d", &crop_x, &crop_y, &crop_w, &crop_h) != 4) {
                std::cerr << "Invalid crop format. Use --crop x,y,w,h" << std::endl;
                return 1;
            }
        }
    }

    RGBMatrix::Options matrix_options;
    rgb_matrix::RuntimeOptions runtime_opt;
    matrix_options.hardware_mapping = "regular";
    matrix_options.rows = HEIGHT;
    matrix_options.cols = WIDTH;

    // Use CreateMatrixFromFlags to allow user to override options
    RGBMatrix *matrix = nullptr;
    if (!ascii_mode && !info_mode) {
        matrix = RGBMatrix::CreateFromFlags(&argc, &argv, &matrix_options, &runtime_opt);
        if (matrix == nullptr) {
            std::cerr << "Failed to initialize LED matrix. Pass --ascii for ASCII mode." << std::endl;
            return 1;
        }
    }

    std::cout << "Starting LED Driver! Listening to Wayland Display..." << std::endl;

    if (!wl_capture_init()) {
        if (matrix) delete matrix;
        return 1;
    }

    wl_capture_set_profile(profile_mode);

    FrameCanvas *offscreen = nullptr;
    if (matrix) offscreen = matrix->CreateFrameCanvas();

    // Intermediate buffer to hold the 128x128 extracted frame
    uint8_t frame_buffer[WIDTH * HEIGHT * BPP];

    if (info_mode) {
        if (!wl_capture_frame(frame_buffer, WIDTH, HEIGHT, BPP, crop_x, crop_y, crop_w, crop_h, stretch_mode)) {
            std::cerr << "Failed to capture frame from Wayland for info." << std::endl;
        }
        wl_capture_cleanup();
        if (matrix) delete matrix;
        return 0;
    }

    // Infinite loop processing frames
    auto start_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    double total_capture_time = 0;
    double total_capture_wait_time = 0;
    double total_capture_process_time = 0;
    double total_render_time = 0;

    auto last_capture_time = std::chrono::steady_clock::now();
    uint32_t capture_interval_ms = (capture_rate > 0) ? (1000 / capture_rate) : 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        bool should_capture = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_capture_time).count() >= capture_interval_ms;

        if (should_capture) {
            auto capture_start = std::chrono::steady_clock::now();
            if (!wl_capture_frame(frame_buffer, WIDTH, HEIGHT, BPP, crop_x, crop_y, crop_w, crop_h, stretch_mode)) {
                std::cerr << "Failed to capture frame from Wayland" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            auto capture_end = std::chrono::steady_clock::now();
            total_capture_time += std::chrono::duration<double>(capture_end - capture_start).count();
            
            if (profile_mode) {
                wl_capture_profile_data cp = wl_capture_get_last_profile();
                total_capture_wait_time += cp.wait_time_ms / 1000.0;
                total_capture_process_time += cp.process_time_ms / 1000.0;
            }
            last_capture_time = capture_end;
        }

        auto render_start = std::chrono::steady_clock::now();
        if (ascii_mode) {
            render_ascii(frame_buffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 2 fps
        } else {
            render_matrix(offscreen, frame_buffer);
            offscreen = matrix->SwapOnVSync(offscreen);
        }
        auto render_end = std::chrono::steady_clock::now();
        total_render_time += std::chrono::duration<double>(render_end - render_start).count();

        frame_count++;
        if ((fps_mode || profile_mode) && frame_count >= 100) {
            auto end_time = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = end_time - start_time;
            if (fps_mode) {
                std::cout << "FPS: " << frame_count / elapsed.count() << std::endl;
            }
            if (profile_mode) {
                std::cout << "Profile (avg per frame) - Capture: " << (total_capture_time / frame_count) * 1000 << "ms "
                          << "(Wait: " << (total_capture_wait_time / frame_count) * 1000 << "ms, Proc: " << (total_capture_process_time / frame_count) * 1000 << "ms), "
                          << "Render: " << (total_render_time / frame_count) * 1000 << "ms" << std::endl;
            }
            frame_count = 0;
            total_capture_time = 0;
            total_capture_wait_time = 0;
            total_capture_process_time = 0;
            total_render_time = 0;
            start_time = std::chrono::steady_clock::now();
        }
    }

    // Cleanup
    wl_capture_cleanup();
    if (matrix) delete matrix;

    return 0;
}
