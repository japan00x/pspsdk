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

PSP_MODULE_INFO("PSP_ASCIIArt", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define BMP_FILE_PATH       "ms0:/data/asciiart.bmp"
#define OUTPUT_DIR          "ms0:/data/"
#define ASCII_FILE_SUFFIX   "_ascii_art.txt"
#define COLOR_FILE_SUFFIX   "_color.dat"
#define TARGET_WIDTH        80

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

Image* load_bmp(const char* path) {
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
    if (infoHeader.biBitCount != 24 || infoHeader.biCompression != 0) {
        fclose(fp);
        return NULL;
    }
    int rowSize = ((infoHeader.biBitCount * infoHeader.biWidth + 31) / 32) * 4;
    int absHeight = (infoHeader.biHeight > 0) ? infoHeader.biHeight : -infoHeader.biHeight;
    unsigned char* data = (unsigned char*)malloc(infoHeader.biWidth * absHeight * 3);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    fseek(fp, fileHeader.bfOffBits, SEEK_SET);
    for (int y = 0; y < absHeight; y++) {
        unsigned char* row = (unsigned char*)malloc(rowSize);
        if (fread(row, 1, rowSize, fp) != (unsigned int)rowSize) {
            free(row);
            free(data);
            fclose(fp);
            return NULL;
        }
        int destY = (infoHeader.biHeight > 0) ? (absHeight - 1 - y) : y;
        memcpy(data + destY * infoHeader.biWidth * 3, row, infoHeader.biWidth * 3);
        free(row);
    }
    fclose(fp);
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) {
        free(data);
        return NULL;
    }
    img->width = infoHeader.biWidth;
    img->height = absHeight;
    img->pixels = data;
    return img;
}

void free_image(Image* img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        free(img);
    }
}

Image* resize_image(const Image* src, int targetWidth) {
    int targetHeight = (src->height * targetWidth) / src->width;
    unsigned char* newPixels = (unsigned char*)malloc(targetWidth * targetHeight * 3);
    if (!newPixels) return NULL;
    for (int y = 0; y < targetHeight; y++) {
        int srcY = y * src->height / targetHeight;
        for (int x = 0; x < targetWidth; x++) {
            int srcX = x * src->width / targetWidth;
            int srcIndex = (srcY * src->width + srcX) * 3;
            int dstIndex = (y * targetWidth + x) * 3;
            newPixels[dstIndex + 0] = src->pixels[srcIndex + 0]; 
            newPixels[dstIndex + 1] = src->pixels[srcIndex + 1]; 
            newPixels[dstIndex + 2] = src->pixels[srcIndex + 2]; 
        }
    }
    Image* resized = (Image*)malloc(sizeof(Image));
    if (!resized) {
        free(newPixels);
        return NULL;
    }
    resized->width = targetWidth;
    resized->height = targetHeight;
    resized->pixels = newPixels;
    return resized;
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

int save_color_data(const char* path, const Image* img) {
    FILE* fp = fopen(path, "w");
    if (!fp) return -1;
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            int index = (y * img->width + x) * 3;
            int r = img->pixels[index + 0];
            int g = img->pixels[index + 1];
            int b = img->pixels[index + 2];
            fprintf(fp, "%d,%d:%d,%d,%d\n", x, y, r, g, b);
        }
    }
    fclose(fp);
    return 0;
}

int wait_for_button_press() {
    SceCtrlData pad;
    pspDebugScreenPrintf("Please press a button...\n");
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
    pspDebugScreenPrintf("Please place the BMP file in ms0:/data/.\n");
    pspDebugScreenPrintf("Press START to toggle color information saving (initially OFF)\n");
    pspDebugScreenPrintf("Press X to start conversion\n\n");

    int saveColor = 0;

    while (1) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) {
            saveColor = !saveColor;
            pspDebugScreenPrintf("Color saving: %s\n", saveColor ? "ON" : "OFF");
            sceKernelDelayThread(300000); 
        }
        if (pad.Buttons & PSP_CTRL_CROSS) { 
            break;
        }
        sceDisplayWaitVblankStart();
    }

    pspDebugScreenPrintf("\nStarting conversion...\n");

    Image* srcImg = load_bmp(BMP_FILE_PATH);
    if (!srcImg) {
        pspDebugScreenPrintf("Failed to load image.\n");
        sceKernelSleepThread();
        sceKernelExitGame();
        return 0;
    }
    pspDebugScreenPrintf("Image loaded: %d x %d\n", srcImg->width, srcImg->height);

    Image* resizedImg = resize_image(srcImg, TARGET_WIDTH);
    if (!resizedImg) {
        pspDebugScreenPrintf("Failed to resize image.\n");
        free_image(srcImg);
        sceKernelSleepThread();
        sceKernelExitGame();
        return 0;
    }
    pspDebugScreenPrintf("Resized: %d x %d\n", resizedImg->width, resizedImg->height);

    char* asciiArt = convert_to_ascii(resizedImg);
    if (!asciiArt) {
        pspDebugScreenPrintf("Failed to convert to ASCII.\n");
        free_image(srcImg);
        free_image(resizedImg);
        sceKernelSleepThread();
        sceKernelExitGame();
        return 0;
    }
    pspDebugScreenPrintf("\n[ ASCII Art ]\n");
    pspDebugScreenPrintf("%s\n", asciiArt);

    char asciiFilePath[256];
    make_filename(asciiFilePath, sizeof(asciiFilePath), ASCII_FILE_SUFFIX);
    if (save_text_file(asciiFilePath, asciiArt) == 0) {
        pspDebugScreenPrintf("Saved ASCII art: %s\n", asciiFilePath);
    } else {
        pspDebugScreenPrintf("Failed to save ASCII art\n");
    }
    if (saveColor) {
        char colorFilePath[256];
        make_filename(colorFilePath, sizeof(colorFilePath), COLOR_FILE_SUFFIX);
        if (save_color_data(colorFilePath, srcImg) == 0) {
            pspDebugScreenPrintf("Saved color data: %s\n", colorFilePath);
        } else {
            pspDebugScreenPrintf("Failed to save color data\n");
        }
    }

    free(asciiArt);
    free_image(srcImg);
    free_image(resizedImg);

    pspDebugScreenPrintf("\nPress any button to exit...\n");
    wait_for_button_press();
    sceKernelExitGame();
    return 0;
}
