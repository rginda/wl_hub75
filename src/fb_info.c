#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int main(int argc, char *argv[]) {
    const char *fb_path = "/dev/fb0";
    if (argc > 1) {
        fb_path = argv[1];
    }

    int fb_fd = open(fb_path, O_RDWR);
    if (fb_fd == -1) {
        perror("Error: cannot open framebuffer device");
        return 1;
    }
    printf("Successfully opened %s\n", fb_path);

    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;

    // Get fixed screen information
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        close(fb_fd);
        return 1;
    }

    // Get variable screen information
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        close(fb_fd);
        return 1;
    }

    printf("\n--- Fixed Screen Info ---\n");
    printf("ID:                   %s\n", finfo.id);
    printf("Smem start:           0x%lx\n", finfo.smem_start);
    printf("Smem length:          %u bytes\n", finfo.smem_len);
    printf("Type:                 %u\n", finfo.type);
    printf("Visual:               %u\n", finfo.visual);
    printf("Line length:          %u bytes\n", finfo.line_length);

    printf("\n--- Variable Screen Info ---\n");
    printf("Visible resolution:   %d x %d\n", vinfo.xres, vinfo.yres);
    printf("Virtual resolution:   %d x %d\n", vinfo.xres_virtual, vinfo.yres_virtual);
    printf("Offset:               %d x %d\n", vinfo.xoffset, vinfo.yoffset);
    printf("Bits per pixel:       %d\n", vinfo.bits_per_pixel);
    printf("Grayscale:            %d\n", vinfo.grayscale);

    printf("\n--- Color Bitfields ---\n");
    printf("Red:                  offset: %2d, length: %2d, msb_right: %d\n",
           vinfo.red.offset, vinfo.red.length, vinfo.red.msb_right);
    printf("Green:                offset: %2d, length: %2d, msb_right: %d\n",
           vinfo.green.offset, vinfo.green.length, vinfo.green.msb_right);
    printf("Blue:                 offset: %2d, length: %2d, msb_right: %d\n",
           vinfo.blue.offset, vinfo.blue.length, vinfo.blue.msb_right);
    printf("Transparency:         offset: %2d, length: %2d, msb_right: %d\n",
           vinfo.transp.offset, vinfo.transp.length, vinfo.transp.msb_right);

    close(fb_fd);
    return 0;
}
