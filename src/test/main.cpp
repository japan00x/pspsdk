/*
    PSP ASCII Art Converter
    画像（ms0:/data/asciiart.bmp）を読み込み、ASCII アートに変換して表示・保存する PSP 用サンプルコード。
    ・PSPSDK 標準の関数のみを使用（pspdebug, pspctrl, pspkernel, pspdisplay 等）
    ・簡易な 24bit BMP ローダー実装（BMP は 24bit uncompressed のみ対応）
    ・画像は横幅 80 にリサイズして ASCII 化（グレースケール変換＋文字マッピング）
    ・生成結果は ms0:/data/ にタイムスタンプ付きファイルとして保存
    ・カラー情報保存は、START ボタンでオン/オフ切替
*/

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psputils.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// モジュール情報
PSP_MODULE_INFO("PSP_ASCIIArt", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

// 定数
#define BMP_FILE_PATH       "ms0:/data/asciiart.bmp"
#define OUTPUT_DIR          "ms0:/data/"
#define ASCII_FILE_SUFFIX   "_ascii_art.txt"
#define COLOR_FILE_SUFFIX   "_color.dat"
#define TARGET_WIDTH        80

// BMP ヘッダ定義（リトルエンディアン前提）
#pragma pack(push, 1)
typedef struct {
    unsigned short bfType;      // 'BM'
    unsigned int bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    unsigned int biSize;
    int biWidth;
    int biHeight;       // 正の場合：下から上に格納
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

// 画像データ構造体
typedef struct {
    int width;
    int height;
    // 各ピクセルは 3 バイト (R,G,B) で格納。上方向に先頭が最上段
    unsigned char* pixels;
} Image;

// BMP ローダー（24bit uncompressed BMP のみ対応）
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
    if (fileHeader.bfType != 0x4D42) { // 'BM'
        fclose(fp);
        return NULL;
    }
    BITMAPINFOHEADER infoHeader;
    if (fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }
    if (infoHeader.biBitCount != 24 || infoHeader.biCompression != 0) {
        // 非対応フォーマット
        fclose(fp);
        return NULL;
    }
    // BMP の行サイズは4バイト境界
    int rowSize = ((infoHeader.biBitCount * infoHeader.biWidth + 31) / 32) * 4;
    int absHeight = (infoHeader.biHeight > 0) ? infoHeader.biHeight : -infoHeader.biHeight;
    unsigned char* data = (unsigned char*)malloc(infoHeader.biWidth * absHeight * 3);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    // fseek 到 pixel データ開始位置
    fseek(fp, fileHeader.bfOffBits, SEEK_SET);
    // BMP は下から上に格納される（biHeight 正の場合）
    for (int y = 0; y < absHeight; y++) {
        unsigned char* row = (unsigned char*)malloc(rowSize);
        if (fread(row, 1, rowSize, fp) != (unsigned int)rowSize) {
            free(row);
            free(data);
            fclose(fp);
            return NULL;
        }
        // 行の格納先：上から順に配置するため、反転する
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

// BMP 解放
void free_image(Image* img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        free(img);
    }
}

// 画像リサイズ（最近傍法）：ターゲット幅に合わせ、ターゲット高さは元比率維持
// 出力は新しく確保した Image（RGB 3バイト/ピクセル）を返す
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
            newPixels[dstIndex + 0] = src->pixels[srcIndex + 0]; // R
            newPixels[dstIndex + 1] = src->pixels[srcIndex + 1]; // G
            newPixels[dstIndex + 2] = src->pixels[srcIndex + 2]; // B
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

// ASCII 変換
// グレースケール変換：gray = (R*30 + G*59 + B*11) / 100
// マッピング文字列（濃い順）："@#S%?*+;:,. "
const char* asciiChars = "@#S%?*+;:,. ";
int asciiCharsLen = 12;

char* convert_to_ascii(const Image* img) {
    // 各行末尾に改行、全体の終端に '\0'
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

// 現在時刻からファイル名文字列を作成（例："MMddHHmmss" + suffix）
void make_filename(char* buffer, int bufferSize, const char* suffix) {
    SceRtcTick tick;
    sceRtcGetCurrentTick(&tick);
    // PSP の tick は 1 tick = 1/1000000 sec などと仮定し、シンプルに下位桁を使う
    // ここでは簡易に tick.tick の下位 10 桁を利用
    unsigned int t = (unsigned int)(tick.tick & 0xffffffff);
    snprintf(buffer, bufferSize, "%s%u%s", OUTPUT_DIR, t, suffix);
}

// ファイルへ文字列を保存
int save_text_file(const char* path, const char* text) {
    FILE* fp = fopen(path, "w");
    if (!fp) return -1;
    fputs(text, fp);
    fclose(fp);
    return 0;
}

// カラー情報保存：元画像全体の各ピクセルごとに "x,y:R,G,B\n" として出力
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

// PSP コントローラーの入力をチェックし、ボタンが押されたかを返す
int wait_for_button_press() {
    SceCtrlData pad;
    pspDebugScreenPrintf("押すボタンを入力してください...\n");
    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons) {
            return pad.Buttons;
        }
        sceDisplayWaitVblankStart();
    }
}

// メイン
int main(void) {
    // 初期化
    pspDebugScreenInit();
    pspDebugScreenPrintf("PSP ASCII Art Converter\n");
    pspDebugScreenPrintf("入力BMP: %s\n", BMP_FILE_PATH);
    pspDebugScreenPrintf("※ ms0:/data/ に BMP ファイルを配置してください。\n");
    pspDebugScreenPrintf("※ START ボタンでカラー情報保存の ON/OFF を切替（初期は OFF）\n");
    pspDebugScreenPrintf("※ X ボタンで変換開始\n\n");

    // カラー保存フラグ（START ボタンで ON/OFF 切替）
    int saveColor = 0;

    // メニュー入力ループ
    while (1) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) {
            saveColor = !saveColor;
            pspDebugScreenPrintf("カラー保存: %s\n", saveColor ? "ON" : "OFF");
            sceKernelDelayThread(300000); // 300ms 待ち（デバウンス）
        }
        if (pad.Buttons & PSP_CTRL_CROSS) { // X ボタンで変換開始
            break;
        }
        sceDisplayWaitVblankStart();
    }

    pspDebugScreenPrintf("\n変換開始...\n");

    // 画像読み込み
    Image* srcImg = load_bmp(BMP_FILE_PATH);
    if (!srcImg) {
        pspDebugScreenPrintf("画像の読み込みに失敗しました。\n");
        sceKernelSleepThread();
        sceKernelExitGame();
        return 0;
    }
    pspDebugScreenPrintf("画像読み込み完了: %d x %d\n", srcImg->width, srcImg->height);

    // 画像リサイズ（ターゲット幅 TARGET_WIDTH）
    Image* resizedImg = resize_image(srcImg, TARGET_WIDTH);
    if (!resizedImg) {
        pspDebugScreenPrintf("画像のリサイズに失敗しました。\n");
        free_image(srcImg);
        sceKernelSleepThread();
        sceKernelExitGame();
        return 0;
    }
    pspDebugScreenPrintf("リサイズ完了: %d x %d\n", resizedImg->width, resizedImg->height);

    // ASCII 変換
    char* asciiArt = convert_to_ascii(resizedImg);
    if (!asciiArt) {
        pspDebugScreenPrintf("ASCII 変換に失敗しました。\n");
        free_image(srcImg);
        free_image(resizedImg);
        sceKernelSleepThread();
        sceKernelExitGame();
        return 0;
    }
    pspDebugScreenPrintf("\n【 ASCII Art 】\n");
    pspDebugScreenPrintf("%s\n", asciiArt);

    // ファイル保存
    char asciiFilePath[256];
    make_filename(asciiFilePath, sizeof(asciiFilePath), ASCII_FILE_SUFFIX);
    if (save_text_file(asciiFilePath, asciiArt) == 0) {
        pspDebugScreenPrintf("ASCII アートを保存: %s\n", asciiFilePath);
    } else {
        pspDebugScreenPrintf("ASCII アートの保存に失敗\n");
    }
    if (saveColor) {
        char colorFilePath[256];
        make_filename(colorFilePath, sizeof(colorFilePath), COLOR_FILE_SUFFIX);
        if (save_color_data(colorFilePath, srcImg) == 0) {
            pspDebugScreenPrintf("カラー情報を保存: %s\n", colorFilePath);
        } else {
            pspDebugScreenPrintf("カラー情報の保存に失敗\n");
        }
    }

    // 後片付け
    free(asciiArt);
    free_image(srcImg);
    free_image(resizedImg);

    pspDebugScreenPrintf("\n終了するには任意のボタンを押してください...\n");
    wait_for_button_press();
    sceKernelExitGame();
    return 0;
}
