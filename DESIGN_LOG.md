The goal of this project is to display the output of the [Pico-8](https://lexaloffle.com/pico-8) emulator on a [Hub75](https://en.wikipedia.org/wiki/Hub75) LED display.

A Raspberry Pi 4 is used as the platform to run Pico-8 and drive the LED display. 

Timing is critical to driving the LED display, so we would like to do this with as little overhead as possible.  Ideally we can avoid running a desktop environment.

The hzeller's rpi-rgb-led-matrix library is used to drive the LED display.  This library requires root access or the CAP_SYS_RAWIO capability to access the PWM hardware.  It also prefers to have CAP_SYS_NICE, in order to avoid being starved of CPU time by other processes.

Ideally the Pico-8 emulator would run in a separate process from the LED driver, in order to isolate the timing-critical LED driver from the Pico-8 emulator, and reduce the risk of the escalated capabilities required of the LED driver.

Pico-8 is built with SDL2, which includes a modular driver architecture.  SDL2 includes drivers for directfb, kmsdrm, and one called "raspberry" that appears to render directly to the Raspberry Pi without requiring a window manager.

To achieve this, we will write the LED driver process in **C++** using the `rpi-rgb-led-matrix` library. We will explore using a standard Linux framebuffer (e.g., `/dev/fbX`) to receive pixel data from the Pico-8 emulator rather than creating our own custom IPC mechanism. The LED driver can `mmap` this framebuffer and copy the pixels to the LED display. If standard Linux framebuffer support turns out to be deprecated or problematic on modern Raspberry Pi OS releases, we will explore using `kmsdrm` as an alternative.

To make things testable before integrating the actual Pico-8 emulator, we will add an ASCII mode to the LED driver that displays the contents of the framebuffer as an ASCII art representation of the pixels in the terminal. We will also create a simple "mock Pico-8" program that generates test patterns (like a bouncing square or color wheel) and writes them to the framebuffer. This allows us to verify the framebuffer reading and rendering logic independently.

**Milestone 1**: Implement the mock Pico-8 program and the LED driver. The LED driver will only implement the ASCII mode, reading pixel data from the framebuffer and displaying the contents as an ASCII art representation in the terminal.

## Architecture Learnings: Framebuffer vs KMS
During development, we discovered critical architectural limitations regarding capturing Pico-8's video output on modern Linux (specifically Raspberry Pi OS):

1. **KMS Hardware Scaling**: When running Pico-8 without an X11 desktop, it utilizes the native `vc4-kms-v3d` driver (via SDL2's `kmsdrm` backend). This securely renders the 128x128 game surface *directly* to the physical DRM/KMS hardware 3D planes to be scaled for HDMI output.
2. **The /dev/fb0 Illusion**: Because the drawing happens strictly on the GPU planes, it entirely bypasses the traditional `/dev/fb0` software memory buffer. The `/dev/fb0` node that appears is merely the fallback memory for the Linux text console running invisibly *underneath* the game.
3. **Deprecated Ecosystem**: The historical workaround was to force Pico-8 to render in software using `SDL_VIDEODRIVER=fbcon`. However, SDL has completely removed `fbcon` support from modern releases. Additionally, standard Broadcom scraping tools like `fbcp` and `raspi2fb` fail to compile because Raspberry Pi OS removed the deprecated `Dispmanx` headers they rely on.

**Resolution:** Natively scraping the headless KMS DRM buffer is no longer feasible with standard tools or `fbcon`. Instead of falling back to legacy X11, the modern, high-performance architecture is to utilize a lightweight Wayland Kiosk compositor like `cage`. `cage` can run Pico-8 fullscreen directly on the DRM hardware planes without a desktop environment, and the LED driver can securely extract tear-free frames using the native Wayland `wlr-screencopy-unstable-v1` protocol.

## Generalizing to Any Application

Once the Wayland pipeline was proven with Pico-8, we refactored the hardcoded prototype into a completely generalized utility (`wl_hub75`). 

**The Scaling Challenge:** Pico-8 inherently uses integer scaling (`8x` for a 1080p display, rendering a perfect 1024x1024 box). Our initial implementation was brittle, capturing only this exact scaled box natively through math. 

**The Solution:** We replaced the integer locking with a fully dynamic fractional scaling matrix. Because standard applications span the full 1920x1080 bounds, `wl_hub75` now automatically analyzes the compositor's dimensions, mathematically determines the optimal letterboxing or pillarboxing boundaries to retain the true application aspect ratio, and flawlessly downscales the frame to the LED matrix. 

For maximum flexibility, we introduced CLI arguments (`--crop` and `--stretch`) allowing the utility to act as a precision magnifying glass over any arbitrary application window without requiring modifications to the host software.