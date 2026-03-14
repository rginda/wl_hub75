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
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ascii") == 0) {
            ascii_mode = true;
        } else if (strcmp(argv[i], "--info") == 0) {
            info_mode = true;
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

    std::cout << "Starting LED Driver. Listening to Wayland Display..." << std::endl;

    if (!wl_capture_init()) {
        if (matrix) delete matrix;
        return 1;
    }

    Canvas *canvas = matrix; // Matrix itself implements canvas

    // Intermediate buffer to hold the 128x128 extracted pico8 frame
    uint8_t pico8_buffer[WIDTH * HEIGHT * BPP];

    if (info_mode) {
        if (!wl_capture_frame(pico8_buffer, WIDTH, HEIGHT, BPP)) {
            std::cerr << "Failed to capture frame from Wayland for info." << std::endl;
        }
        wl_capture_cleanup();
        if (matrix) delete matrix;
        return 0;
    }

    // Infinite loop processing frames
    while (true) {
        if (!wl_capture_frame(pico8_buffer, WIDTH, HEIGHT, BPP)) {
            std::cerr << "Failed to capture frame from Wayland" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (ascii_mode) {
            render_ascii(pico8_buffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 2 fps
        } else {
            render_matrix(canvas, pico8_buffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 fps
        }
    }

    // Cleanup
    wl_capture_cleanup();
    if (matrix) delete matrix;

    return 0;
}
