The goal of this project is to display the output of the [Pico-8](https://lexaloffle.com/pico-8) emulator on a [Hub75](https://en.wikipedia.org/wiki/Hub75) LED display.

A Raspberry Pi 4 is used as the platform to run Pico-8 and drive the LED display. 

Timing is critical to driving the LED display, so we would like to do this with as little overhead as possible.  Ideally we can avoid running a desktop environment.

The hzeller's rpi-rgb-led-matrix library is used to drive the LED display.  This library requires root access or the CAP_SYS_RAWIO capability to access the PWM hardware.  It also prefers to have CAP_SYS_NICE, in order to avoid being starved of CPU time by other processes.

Ideally the Pico-8 emulator would run in a separate process from the LED driver, in order to isolate the timing-critical LED driver from the Pico-8 emulator, and reduce the risk of the escalated capabilities required of the LED driver.

Pico-8 is built with SDL2, which includes a modular driver architecture.  SDL2 includes drivers for directfb, kmsdrm, and one called "raspberry" that appears to render directly to the Raspberry Pi without requiring a window manager.

To achieve this, we will write the LED driver process in **C++** using the `rpi-rgb-led-matrix` library. We will explore using a standard Linux framebuffer (e.g., `/dev/fbX`) to receive pixel data from the Pico-8 emulator rather than creating our own custom IPC mechanism. The LED driver can `mmap` this framebuffer and copy the pixels to the LED display. If standard Linux framebuffer support turns out to be deprecated or problematic on modern Raspberry Pi OS releases, we will explore using `kmsdrm` as an alternative.

To make things testable before integrating the actual Pico-8 emulator, we will add an ASCII mode to the LED driver that displays the contents of the framebuffer as an ASCII art representation of the pixels in the terminal. We will also create a simple "mock Pico-8" program that generates test patterns (like a bouncing square or color wheel) and writes them to the framebuffer. This allows us to verify the framebuffer reading and rendering logic independently.

**Milestone 1**: Implement the mock Pico-8 program and the LED driver. The LED driver will only implement the ASCII mode, reading pixel data from the framebuffer and displaying the contents as an ASCII art representation in the terminal.