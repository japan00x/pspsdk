/*
    PSP_HttpsBrowser.cpp
    PSP 用 HTTPS ブラウザ（例：example.com へ GET リクエストを送信）
    TLS ライブラリは wolfSSL を利用（mbedtls は使用しない）
*/

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// wolfSSL ヘッダ
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

// PSP モジュール情報
PSP_MODULE_INFO("PSP_HttpsBrowser", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

// 終了用コールバック
int exit_callback(int arg1, int arg2, void* common) {
    sceKernelExitGame();
    return 0;
}

// コールバックスレッド
int callbackThread(SceSize args, void* argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

// コールバックセットアップ
void setupCallbacks() {
    int thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, NULL);
    if(thid >= 0){
        sceKernelStartThread(thid, 0, NULL);
    }
}

// ネットワーク初期化
int initNetwork() {
    pspDebugScreenPrintf("Initializing network...\n");
    if(pspNetInit() < 0) {
        pspDebugScreenPrintf("pspNetInit failed.\n");
        return -1;
    }
    if(pspNetInetInit() < 0) {
        pspDebugScreenPrintf("pspNetInetInit failed.\n");
        return -1;
    }
    if(pspNetResolverInit() < 0) {
        pspDebugScreenPrintf("pspNetResolverInit failed.\n");
        return -1;
    }
    pspDebugScreenPrintf("Network initialized.\n");
    return 0;
}

// ネットワーク終了処理
void termNetwork() {
    pspNetResolverTerm();
    pspNetInetTerm();
    pspNetTerm();
}

// HTTPS 通信サンプル（example.com へ GET リクエストを送信）
int https_request() {
    const char *server_hostname = "example.com";
    const int server_port = 443;
    const char *GET_request = "GET / HTTP/1.1\r\n"
                              "Host: example.com\r\n"
                              "User-Agent: PSP WolfSSL Browser\r\n"
                              "Connection: close\r\n\r\n";

    int sock = -1;
    int ret = 0;

    // wolfSSL 初期化
    wolfSSL_Init();

    // クライアント用コンテキスト作成（TLSv1.2 クライアント）
    WOLFSSL_METHOD* method = wolfTLSv1_2_client_method();
    if (method == NULL) {
        pspDebugScreenPrintf("wolfTLSv1_2_client_method error\n");
        return -1;
    }
    WOLFSSL_CTX* ctx = wolfSSL_CTX_new(method);
    if (ctx == NULL) {
        pspDebugScreenPrintf("wolfSSL_CTX_new error\n");
        return -1;
    }

    // ソケット作成
    sock = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        pspDebugScreenPrintf("sceNetInetSocket error\n");
        wolfSSL_CTX_free(ctx);
        return -1;
    }

    // サーバアドレス設定
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = sceNetHtons(server_port);
    // example.com の IP アドレス 93.184.216.34 を設定（名前解決を行わない場合）
    server_addr.sin_addr.s_addr = sceNetInetAddr("93.184.216.34");

    pspDebugScreenPrintf("Connecting to %s:%d...\n", server_hostname, server_port);
    if (sceNetInetConnect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        pspDebugScreenPrintf("sceNetInetConnect error\n");
        sceNetInetSocketClose(sock);
        wolfSSL_CTX_free(ctx);
        return -1;
    }
    pspDebugScreenPrintf("Connected to server.\n");

    // wolfSSL オブジェクト作成し、ソケットを設定
    WOLFSSL* ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        pspDebugScreenPrintf("wolfSSL_new error\n");
        sceNetInetSocketClose(sock);
        wolfSSL_CTX_free(ctx);
        return -1;
    }
    wolfSSL_set_fd(ssl, sock);

    // SNI 設定
    ret = wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOSTNAME, server_hostname, strlen(server_hostname));
    if (ret != WOLFSSL_SUCCESS) {
        pspDebugScreenPrintf("wolfSSL_UseSNI error: %d\n", ret);
        wolfSSL_free(ssl);
        sceNetInetSocketClose(sock);
        wolfSSL_CTX_free(ctx);
        return -1;
    }

    // TLS ハンドシェイク
    pspDebugScreenPrintf("Performing TLS handshake...\n");
    ret = wolfSSL_connect(ssl);
    if (ret != WOLFSSL_SUCCESS) {
        int err = wolfSSL_get_error(ssl, ret);
        pspDebugScreenPrintf("wolfSSL_connect error: %d\n", err);
        wolfSSL_free(ssl);
        sceNetInetSocketClose(sock);
        wolfSSL_CTX_free(ctx);
        return -1;
    }
    pspDebugScreenPrintf("TLS handshake successful!\n");

    // GET リクエスト送信
    pspDebugScreenPrintf("Sending GET request...\n");
    ret = wolfSSL_write(ssl, GET_request, strlen(GET_request));
    if (ret <= 0) {
        int err = wolfSSL_get_error(ssl, ret);
        pspDebugScreenPrintf("wolfSSL_write error: %d\n", err);
        wolfSSL_free(ssl);
        sceNetInetSocketClose(sock);
        wolfSSL_CTX_free(ctx);
        return -1;
    }

    // レスポンス受信
    pspDebugScreenPrintf("Receiving response...\n");
    char buf[1024];
    int bytes;
    do {
        memset(buf, 0, sizeof(buf));
        bytes = wolfSSL_read(ssl, buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            pspDebugScreenPrintf("%s", buf);
        }
    } while (bytes > 0);

    pspDebugScreenPrintf("\nConnection closed.\n");

    // クリーンアップ
    wolfSSL_shutdown(ssl);
    wolfSSL_free(ssl);
    sceNetInetSocketClose(sock);
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();

    return 0;
}

extern "C" int main(void) {
    setupCallbacks();
    pspDebugScreenInit();
    pspDebugScreenPrintf("Hello PSP HTTPS Browser (wolfSSL)\n");

    // ネットワーク初期化
    if (initNetwork() < 0) {
        pspDebugScreenPrintf("Network initialization failed. Exiting...\n");
        sceKernelSleepThread();
    }

    // APCTL（アクセスポイント）接続待ち（IP取得待ち）
    pspDebugScreenPrintf("Connecting to network access point...\n");
    {
        SceNetApctlState state;
        memset(&state, 0, sizeof(state));
        while (1) {
            sceNetApctlGetState(&state);
            if (state == SCE_NET_APCTL_STATE_GOT_IP) {
                break;
            }
            sceKernelDelayThread(50 * 1000); // 50ms
        }
    }
    pspDebugScreenPrintf("Network access point connected.\n");

    // HTTPS リクエスト実行
    https_request();

    pspDebugScreenPrintf("Press any key to exit...\n");
    sceKernelDelayThread(5000000);  // 5秒待機

    termNetwork();
    sceKernelExitGame();
    return 0;
}
