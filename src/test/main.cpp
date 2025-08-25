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

PSP_MODULE_INFO("PSP_ASCIIArt", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define BMP_FILE_PATH       "ms0:/data/asciiart.bmp"
#define OUTPUT_DIR          "ms0:/data/"
#define ASCII_FILE_SUFFIX   "_ascii_art.txt"
#define TARGET_SIZE         100

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
    while (mask && (mask & 1) == 0) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

Image* load_and_resize_bmp(const char* path, int tgt_w, int tgt_h) {
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    BITMAPFILEHEADER fileHeader;
    if (fread(&fileHeader, sizeof(BITMAPFILEHEADER), 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }
    if (fileHeader.bfType != 0x4D42) {
        fclose(fp);
        return NULL;
    }
    BITMAPINFOHEADER infoHeader;
    if (fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }
    if (infoHeader.biPlanes != 1 || (infoHeader.biBitCount != 8 && infoHeader.biBitCount != 16 && infoHeader.biBitCount != 24) ||
        (infoHeader.biCompression != 0 && infoHeader.biCompression != 3)) {
        fclose(fp);
        return NULL;
    }
    if (infoHeader.biCompression == 3 && infoHeader.biBitCount != 16) {
        fclose(fp);
        return NULL;
    }
    int width = infoHeader.biWidth;
    int height = (infoHeader.biHeight > 0) ? infoHeader.biHeight : -infoHeader.biHeight;
    int bottom_up = (infoHeader.biHeight > 0);
    int bitCount = infoHeader.biBitCount;
    int rowSize = ((bitCount * width + 31) / 32) * 4;
    unsigned char* row_buffer = (unsigned char*)malloc(rowSize);
    if (!row_buffer) {
        fclose(fp);
        return NULL;
    }
    unsigned char palette[1024] = {0};
    uint32_t redMask = 0, greenMask = 0, blueMask = 0;
    int redShift = 0, greenShift = 0, blueShift = 0;
    int redMax = 0, greenMax = 0, blueMax = 0;
    if (bitCount == 8) {
        int colors = infoHeader.biClrUsed ? infoHeader.biClrUsed : 256;
        if (colors > 256 || fread(palette, 4, colors, fp) != (size_t)colors) {
            free(row_buffer);
            fclose(fp);
            return NULL;
        }
    } else if (bitCount == 16) {
        if (infoHeader.biCompression == 0) {
            redMask = 0x7C00;
            greenMask = 0x03E0;
            blueMask = 0x001F;
        } else if (infoHeader.biCompression == 3) {
            if (fread(&redMask, 4, 1, fp) != 1 || fread(&greenMask, 4, 1, fp) != 1 || fread(&blueMask, 4, 1, fp) != 1) {
                free(row_buffer);
                fclose(fp);
                return NULL;
            }
        }
        redShift = get_shift(redMask);
        redMax = (redMask >> redShift);
        greenShift = get_shift(greenMask);
        greenMax = (greenMask >> greenShift);
        blueShift = get_shift(blueMask);
        blueMax = (blueMask >> blueShift);
        if (redMax == 0 || greenMax == 0 || blueMax == 0) {
            free(row_buffer);
            fclose(fp);
            return NULL;
        }
    }
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) {
        free(row_buffer);
        fclose(fp);
        return NULL;
    }
    img->width = tgt_w;
    img->height = tgt_h;
    img->pixels = (unsigned char*)malloc(tgt_w * tgt_h * 3);
    if (!img->pixels) {
        free(img);
        free(row_buffer);
        fclose(fp);
        return NULL;
    }
    int last_src_y = -1;
    unsigned char* newPixels = img->pixels;
    for (int y = 0; y < tgt_h; y++) {
        int src_y = y * height / tgt_h;
        if (src_y != last_src_y) {
            int file_row = bottom_up ? (height - 1 - src_y) : src_y;
            long row_pos = fileHeader.bfOffBits + file_row * (long)rowSize;
            if (fseek(fp, row_pos, SEEK_SET) != 0 || fread(row_buffer, 1, rowSize, fp) != (size_t)rowSize) {
                free(img->pixels);
                free(img);
                free(row_buffer);
                fclose(fp);
                return NULL;
            }
            last_src_y = src_y;
        }
        for (int x = 0; x < tgt_w; x++) {
            int src_x = x * width / tgt_w;
            unsigned char r = 0, g = 0, b = 0;
            int offs = src_x * (bitCount / 8);
            if (bitCount == 24) {
                b = row_buffer[offs + 0];
                g = row_buffer[offs + 1];
                r = row_buffer[offs + 2];
            } else if (bitCount == 16) {
                uint16_t color = *(uint16_t*)(row_buffer + offs);
                r = (((color & redMask) >> redShift) * 255) / redMax;
                g = (((color & greenMask) >> greenShift) * 255) / greenMax;
                b = (((color & blueMask) >> blueShift) * 255) / blueMax;
            } else if (bitCount == 8) {
                uint8_t idx = row_buffer[offs];
                b = palette[idx * 4 + 0];
                g = palette[idx * 4 + 1];
                r = palette[idx * 4 + 2];
            }
            int dst = (y * tgt_w + x) * 3;
            newPixels[dst + 0] = r;
            newPixels[dst + 1] = g;
            newPixels[dst + 2] = b;
        }
    }
    free(row_buffer);
    fclose(fp);
    return img;
}

void free_image(Image* img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        free(img);
    }
}

const char* asciiChars = "@#S%?*+;:,. ";
int asciiCharsLen = 12;

char* convert_to_ascii(const Image* img) {
    int bufSize = (img->width + 1) * img->height + 1;
    char* asciiArt = (char*)malloc(bufSize);
    if (!asciiArt) return NULL;
    int pos = 0;
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            int index = (y * img->width + x) * 3;
            int r = img->pixels[index + 0];
            int g = img->pixels[index + 1];
            int b = img->pixels[index + 2];
            int gray = (r * 30 + g * 59 + b * 11) / 100;
            int charIndex = (gray * (asciiCharsLen - 1)) / 255;
            asciiArt[pos++] = asciiChars[charIndex];
        }
        asciiArt[pos++] = '\n';
    }
    asciiArt[pos] = '\0';
    return asciiArt;
}

void make_filename(char* buffer, int bufferSize, const char* suffix) {
    u64 tick;
    sceRtcGetCurrentTick(&tick);
    unsigned int t = (unsigned int)(tick & 0xffffffff);
    snprintf(buffer, bufferSize, "%s%u%s", OUTPUT_DIR, t, suffix);
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
        if (pad.Buttons) {
            return pad.Buttons;
        }
        sceDisplayWaitVblankStart();
    }
}

int main(void) {
    pspDebugScreenInit();
    pspDebugScreenPrintf("PSP ASCII Art Converter\n");
    pspDebugScreenPrintf("Input BMP: %s\n", BMP_FILE_PATH);
    pspDebugScreenPrintf("Press X to start conversion\n");

    while (1) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CROSS) {
            break;
        }
        sceDisplayWaitVblankStart();
    }

    pspDebugScreenPrintf("\nStarting conversion...\n");

    Image* img = load_and_resize_bmp(BMP_FILE_PATH, TARGET_SIZE, TARGET_SIZE);
    if (!img) {
        pspDebugScreenPrintf("Abnormal: Failed to load or resize image.\n");
        pspDebugScreenPrintf("Press any button to exit...\n");
        wait_for_button_press();
        sceKernelExitGame();
        return 0;
    }

    char* asciiArt = convert_to_ascii(img);
    if (!asciiArt) {
        pspDebugScreenPrintf("Abnormal: Failed to convert to ASCII.\n");
        free_image(img);
        pspDebugScreenPrintf("Press any button to exit...\n");
        wait_for_button_press();
        sceKernelExitGame();
        return 0;
    }

    char asciiFilePath[256];
    make_filename(asciiFilePath, sizeof(asciiFilePath), ASCII_FILE_SUFFIX);
    if (save_text_file(asciiFilePath, asciiArt) == 0) {
        pspDebugScreenPrintf("Success: Saved to %s\n", asciiFilePath);
    } else {
        pspDebugScreenPrintf("Abnormal: Failed to save ASCII art\n");
    }

    free(asciiArt);
    free_image(img);

    pspDebugScreenPrintf("Press any button to exit...\n");
    wait_for_button_press();
    sceKernelExitGame();
    return 0;
}
