#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <cmath>

// Define the dimensions of our mock display
const int WIDTH = 128;
const int HEIGHT = 128;
const int BPP = 3; // RGB24
const size_t FILE_SIZE = WIDTH * HEIGHT * BPP;

const char* SHM_FILE = "/tmp/pico8_fb";

void write_pattern(uint8_t* buffer, int frame) {
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            int index = (y * WIDTH + x) * BPP;
            
            // Generate some scrolling, shifting colors
            uint8_t r = static_cast<uint8_t>((sin(x * 0.1 + frame * 0.05) + 1.0) * 127.5);
            uint8_t g = static_cast<uint8_t>((cos(y * 0.1 + frame * 0.05) + 1.0) * 127.5);
            uint8_t b = static_cast<uint8_t>((sin((x + y) * 0.05 - frame * 0.05) + 1.0) * 127.5);

            // Draw a bouncing white square on top
            int sq_x = static_cast<int>((sin(frame * 0.05) + 1.0) * 0.5 * (WIDTH - 10));
            int sq_y = static_cast<int>((cos(frame * 0.07) + 1.0) * 0.5 * (HEIGHT - 10));

            if (x >= sq_x && x < sq_x + 10 && y >= sq_y && y < sq_y + 10) {
                r = 255;
                g = 255;
                b = 255;
            }

            buffer[index    ] = r;
            buffer[index + 1] = g;
            buffer[index + 2] = b;
        }
    }
}

int main() {
    std::cout << "Starting Mock Pico-8. Writing to " << SHM_FILE << std::endl;

    // Open/Create the shared memory file
    int fd = open(SHM_FILE, O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        std::cerr << "Failed to open shared memory file: " << SHM_FILE << std::endl;
        return 1;
    }

    // Ensure the file is the correct size
    if (ftruncate(fd, FILE_SIZE) == -1) {
        std::cerr << "Failed to truncate shared memory file to correct size." << std::endl;
        close(fd);
        return 1;
    }

    // Memory map the file
    void* ptr = mmap(nullptr, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "Memory mapping failed." << std::endl;
        close(fd);
        return 1;
    }

    uint8_t* buffer = static_cast<uint8_t*>(ptr);
    int frame = 0;

    std::cout << "Running... Press Ctrl+C to stop." << std::endl;

    // Infinite loop generating frames
    while (true) {
        write_pattern(buffer, frame);
        frame++;

        // Sleep to roughly simulate 60fps (16.6ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Cleanup (unreachable due to infinite loop, but good practice)
    munmap(ptr, FILE_SIZE);
    close(fd);
    unlink(SHM_FILE);

    return 0;
}
