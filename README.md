# wl_hub75

A high-performance, generalized Wayland capture utility for displaying screen output on Hub75 LED matrices via a Raspberry Pi.

This tool securely and efficiently extracts tear-free frames from any application running in a Wayland Kiosk compositor (like `cage`) and perfectly maps them to a physical LED matrix using the `rpi-rgb-led-matrix` library.

## Features

- **Zero-Overhead Native Capture**: Directly intercepts hardware-accelerated GUI frames via Wayland's `wlr-screencopy-unstable-v1` protocol without polling or memory copying `/dev/fb0`.
- **Dynamic Letterboxing**: Automatically computes aspect ratios to beautifully letterbox or pillarbox widescreen sources down to your matrix's physical size without stretching.
- **Precision Cropping**: Focus on a specific region of your monitor, effectively turning the matrix into a customizable magnifying glass. 
- **Aspect Ratio Stretching**: Completely optionally stretch the source video to maximize LED engagement ignoring geometric ratios.

## Requirements

* **Hardware:** Raspberry Pi (3/4/5) with supported Hub75 LED matrix controller hardware.
* **Libraries**:
  * `libwayland-client` & `wayland-protocols`
  * `cage` (Wayland Kiosk Compositor)
  * `seatd` (Secure User-space Device access)
  * hzeller's `rpi-rgb-led-matrix`

## Installation & Setup

1. **Fetch the Repository & Submodules:**
   Because the LED driver relies on hzeller's `rpi-rgb-led-matrix` as a Git submodule, you must initialize it:
   ```bash
   # If cloning fresh:
   git clone --recursive git@github.com:rginda/wl_hub75.git
   cd wl_hub75
   
   # Or, if you already cloned the repo normally:
   git submodule update --init --recursive
   ```

2. **Install Dependencies:**
   ```bash
   sudo apt-get install -y libwayland-dev wayland-protocols cage seatd
   ```

2. **Configure Permissions (Crucial!):**
   By default, normal users cannot directly steal control of the physical GPU/TTY from an SSH session. `seatd` brokers this access securely. You must add your user to the `video`, `render`, and `seat` groups. 
   
   Additionally, the LED matrix requires direct GPIO memory access, which requires the `kmem` and `gpio` hardware groups:
   ```bash
   sudo usermod -aG video,render,seat,kmem,gpio $USER
   sudo systemctl enable --now seatd
   ```
   **Important**: Log out and log back in to apply the group changes.

3. **Build:**
   ```bash
   mkdir -p build && cd build
   cmake ..
   make
   ```

## Usage

Because the `rpi-rgb-led-matrix` driver requires direct hardware access to the GPIO pins, it historically needed `sudo`. However, running as `root` breaks access to your normal user's Wayland socket! 

Instead of `sudo`, grant the binary the exact capabilities it needs:

```bash
sudo setcap 'cap_sys_rawio,cap_sys_nice=eip' build/wl_hub75
```

Now, first launch your target application (e.g., the Pico-8 emulator, a terminal, or a browser) inside the `cage` compositor:

```bash
cage -- ./pico-8/pico8_64 -splore
```

Then, in a second terminal session, launch `wl_hub75` as your normal user to intercept the display and push it to the LED matrix:

```bash
# Basic Usage: Auto-scales and letterboxes the entire 1080p Wayland display
WAYLAND_DISPLAY=wayland-1 ./build/wl_hub75

# Stretch Mode: Ignores aspect ratio and forces the image to fill the matrix
WAYLAND_DISPLAY=wayland-1 ./build/wl_hub75 --stretch

# Precision Crop: Only captures a specific 1024x1024 region (x=448, y=28)
# Perfect for exact integer-scaling of apps like Pico-8!
WAYLAND_DISPLAY=wayland-1 ./build/wl_hub75 --crop 448,28,1024,1024

# ASCII Test Mode: Renders to your terminal instead of the physical matrix for debugging
WAYLAND_DISPLAY=wayland-1 ./build/wl_hub75 --ascii
```

## History & Design
For an architectural deep-dive into why standard Linux framebuffer (`/dev/fb0`) parsing was abandoned in favor of Wayland/KMS DRM, see the [DESIGN_LOG.md](DESIGN_LOG.md).
