#include <pspkernel.h> 
#include <pspdebug.h> 
#include <pspctrl.h>
#include <psputils.h>
#include <pspdisplay.h>
#include <psptypes.h>
extern "C" {
#include <psprtc.h>
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <math.h>

PSP_MODULE_INFO("PSP_ASCIIArt", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define BMP_FILE_PATH       "ms0:/data/asciiart.bmp"
#define OUTPUT_DIR          "ms0:/data/"
#define ASCII_FILE_SUFFIX   "_ascii_art.txt"
#define TARGET_WIDTH        100
#define CHAR_ASPECT         0.55f
#define GAMMA_CORRECTION    1.15f

#pragma pack(push, 1)
typedef struct {
    unsigned short bfType;
    unsigned int bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;
} BITMAPFILEHEADER;
typedef struct {
    unsigned int biSize;
    int biWidth;
    int biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int biCompression;
    unsigned int biSizeImage;
    int biXPelsPerMeter;
    int biYPelsPerMeter;
    unsigned int biClrUsed;
    unsigned int biClrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

typedef struct {
    int width;
    int height;
    unsigned char* pixels;
} Image;

static int get_shift(uint32_t mask) {
    int shift = 0;
    if (mask == 0) return 0;
    while ((mask & 1) == 0) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

Image* load_and_resize_bmp(const char* path, int tgt_w) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    BITMAPFILEHEADER fh;
    if (fread(&fh, sizeof(fh), 1, fp) != 1) { fclose(fp); return NULL; }
    if (fh.bfType != 0x4D42) { fclose(fp); return NULL; }
    BITMAPINFOHEADER ih;
    if (fread(&ih, sizeof(ih), 1, fp) != 1) { fclose(fp); return NULL; }
    if (ih.biPlanes != 1) { fclose(fp); return NULL; }
    int width = ih.biWidth;
    int height = (ih.biHeight > 0) ? ih.biHeight : -ih.biHeight;
    if (width <= 0 || height <= 0) { fclose(fp); return NULL; }
    int bitCount = ih.biBitCount;
    uint32_t redMask = 0, greenMask = 0, blueMask = 0;
    if (ih.biCompression == 3 || bitCount == 16 || bitCount == 32) {
        if (ih.biCompression == 3) {
            if (fread(&redMask, 4, 1, fp) != 1 || fread(&greenMask, 4, 1, fp) != 1 || fread(&blueMask, 4, 1, fp) != 1) { fclose(fp); return NULL; }
        } else {
            if (bitCount == 16) { redMask = 0x7C00; greenMask = 0x03E0; blueMask = 0x001F; }
            else { redMask = 0x00FF0000; greenMask = 0x0000FF00; blueMask = 0x000000FF; }
        }
    }
    int redShift = get_shift(redMask);
    int greenShift = get_shift(greenMask);
    int blueShift = get_shift(blueMask);
    int redMax = redMask ? (int)(redMask >> redShift) : 255;
    int greenMax = greenMask ? (int)(greenMask >> greenShift) : 255;
    int blueMax = blueMask ? (int)(blueMask >> blueShift) : 255;
    int bottom_up = (ih.biHeight > 0);
    int rowSize = ((bitCount * width + 31) / 32) * 4;
    unsigned char* row_buffer = (unsigned char*)malloc(rowSize);
    if (!row_buffer) { fclose(fp); return NULL; }
    unsigned char palette[1024];
    if (bitCount == 8) {
        int colors = ih.biClrUsed ? ih.biClrUsed : 256;
        if (colors > 256) { free(row_buffer); fclose(fp); return NULL; }
        if (fread(palette, 4, colors, fp) != (size_t)colors) { free(row_buffer); fclose(fp); return NULL; }
    }
    float tgt_hf = ((float)height * (float)tgt_w * CHAR_ASPECT) / (float)width;
    int tgt_h = (int)(tgt_hf + 0.5f);
    if (tgt_h < 1) tgt_h = 1;
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) { free(row_buffer); fclose(fp); return NULL; }
    img->width = tgt_w;
    img->height = tgt_h;
    img->pixels = (unsigned char*)malloc((size_t)tgt_w * tgt_h * 3);
    if (!img->pixels) { free(img); free(row_buffer); fclose(fp); return NULL; }
    int last_src_y = -1;
    for (int y = 0; y < tgt_h; y++) {
        int src_y = (int)((float)y * (float)height / (float)tgt_h);
        if (src_y < 0) src_y = 0;
        if (src_y >= height) src_y = height - 1;
        if (src_y != last_src_y) {
            int file_row = bottom_up ? (height - 1 - src_y) : src_y;
            long row_pos = fh.bfOffBits + (long)file_row * (long)rowSize;
            if (fseek(fp, row_pos, SEEK_SET) != 0 || fread(row_buffer, 1, rowSize, fp) != (size_t)rowSize) { free(img->pixels); free(img); free(row_buffer); fclose(fp); return NULL; }
            last_src_y = src_y;
        }
        for (int x = 0; x < tgt_w; x++) {
            int src_x = (int)((float)x * (float)width / (float)tgt_w);
            if (src_x < 0) src_x = 0;
            if (src_x >= width) src_x = width - 1;
            unsigned char r = 0, g = 0, b = 0;
            int offs = src_x * (bitCount / 8);
            if (bitCount == 24) {
                b = row_buffer[offs + 0];
                g = row_buffer[offs + 1];
                r = row_buffer[offs + 2];
            } else if (bitCount == 32) {
                uint32_t color = *(uint32_t*)(row_buffer + offs);
                r = (unsigned char)((((color & redMask) >> redShift) * 255) / (redMax ? redMax : 255));
                g = (unsigned char)((((color & greenMask) >> greenShift) * 255) / (greenMax ? greenMax : 255));
                b = (unsigned char)((((color & blueMask) >> blueShift) * 255) / (blueMax ? blueMax : 255));
            } else if (bitCount == 16) {
                uint16_t color = *(uint16_t*)(row_buffer + offs);
                r = (unsigned char)((((color & redMask) >> redShift) * 255) / (redMax ? redMax : 31));
                g = (unsigned char)((((color & greenMask) >> greenShift) * 255) / (greenMax ? greenMax : 31));
                b = (unsigned char)((((color & blueMask) >> blueShift) * 255) / (blueMax ? blueMax : 31));
            } else if (bitCount == 8) {
                uint8_t idx = row_buffer[offs];
                b = palette[idx * 4 + 0];
                g = palette[idx * 4 + 1];
                r = palette[idx * 4 + 2];
            } else {
                free(img->pixels); free(img); free(row_buffer); fclose(fp); return NULL;
            }
            int dst = (y * tgt_w + x) * 3;
            img->pixels[dst + 0] = r;
            img->pixels[dst + 1] = g;
            img->pixels[dst + 2] = b;
        }
    }
    free(row_buffer);
    fclose(fp);
    return img;
}

void free_image(Image* img) {
    if (!img) return;
    if (img->pixels) free(img->pixels);
    free(img);
}

const char* asciiRamp = "@$B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,^`'. ";
int asciiRampLen = 0;

char* convert_to_ascii(const Image* img) {
    int w = img->width;
    int h = img->height;
    int n = w * h;
    float* lum = (float*)malloc(sizeof(float) * n);
    if (!lum) return NULL;
    for (int i = 0; i < n; i++) {
        int idx = i * 3;
        float r = (float)img->pixels[idx + 0];
        float g = (float)img->pixels[idx + 1];
        float b = (float)img->pixels[idx + 2];
        float y = (0.299f * r + 0.587f * g + 0.114f * b);
        y = 255.0f * powf(y / 255.0f, 1.0f / GAMMA_CORRECTION);
        lum[i] = y;
    }
    char* out = (char*)malloc((size_t)(w + 1) * h + 1);
    if (!out) { free(lum); return NULL; }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            int level = (int)floorf((lum[i] * (asciiRampLen - 1)) / 255.0f + 0.5f);
            if (level < 0) level = 0;
            if (level >= asciiRampLen) level = asciiRampLen - 1;
            out[y * (w + 1) + x] = asciiRamp[level];
            float quant = (float)level * (255.0f / (asciiRampLen - 1));
            float err = lum[i] - quant;
            if (x + 1 < w) lum[i + 1] += err * 7.0f / 16.0f;
            if (y + 1 < h) {
                if (x > 0) lum[i + w - 1] += err * 3.0f / 16.0f;
                lum[i + w] += err * 5.0f / 16.0f;
                if (x + 1 < w) lum[i + w + 1] += err * 1.0f / 16.0f;
            }
        }
        out[y * (w + 1) + w] = '\n';
    }
    out[(w + 1) * h] = '\0';
    float avg = 0.0f;
    for (int i = 0; i < n; i++) avg += lum[i];
    avg /= (float)n;
    if (avg < 128.0f) {
        for (int i = 0; out[i]; i++) {
            if (out[i] == '\n') continue;
            const char* p = strchr(asciiRamp, out[i]);
            if (!p) continue;
            int idx = (int)(p - asciiRamp);
            int inv = asciiRampLen - 1 - idx;
            out[i] = asciiRamp[inv];
        }
    }
    free(lum);
    return out;
}

void make_filename(char* buffer, int bufferSize, const char* suffix) {
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    char name[128];
    snprintf(name, sizeof(name), "%02d%02d_%02d%02d%02d%s", lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, suffix);
    snprintf(buffer, bufferSize, "%s%s", OUTPUT_DIR, name);
}

int save_text_file(const char* path, const char* text) {
    FILE* fp = fopen(path, "w");
    if (!fp) return -1;
    fputs(text, fp);
    fclose(fp);
    return 0;
}

int wait_for_button_press() {
    SceCtrlData pad;
    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons) return pad.Buttons;
        sceDisplayWaitVblankStart();
    }
}

int main(void) {
    asciiRampLen = (int)strlen(asciiRamp);
    pspDebugScreenInit();
    pspDebugScreenPrintf("PSP ASCII Art Converter\n");
    pspDebugScreenPrintf("Input BMP: %s\n", BMP_FILE_PATH);
    pspDebugScreenPrintf("Press X to start conversion\n");
    while (1) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CROSS) break;
        sceDisplayWaitVblankStart();
    }
    pspDebugScreenPrintf("\nStarting conversion...\n");
    Image* img = load_and_resize_bmp(BMP_FILE_PATH, TARGET_WIDTH);
    if (!img) {
        pspDebugScreenPrintf("Failed to load or resize image.\n");
        pspDebugScreenPrintf("Press any button to exit...\n");
        wait_for_button_press();
        sceKernelExitGame();
        return 0;
    }
    char* asciiArt = convert_to_ascii(img);
    if (!asciiArt) {
        pspDebugScreenPrintf("Failed to convert to ASCII.\n");
        free_image(img);
        pspDebugScreenPrintf("Press any button to exit...\n");
        wait_for_button_press();
        sceKernelExitGame();
        return 0;
    }
    char asciiFilePath[256];
    make_filename(asciiFilePath, sizeof(asciiFilePath), ASCII_FILE_SUFFIX);
    if (save_text_file(asciiFilePath, asciiArt) == 0) {
        pspDebugScreenPrintf("Saved to %s\n", asciiFilePath);
    } else {
        pspDebugScreenPrintf("Failed to save ASCII art\n");
    }
    free(asciiArt);
    free_image(img);
    pspDebugScreenPrintf("Press any button to exit...\n");
    wait_for_button_press();
    sceKernelExitGame();
    return 0;
}
