#include <pspkernel.h>
#include <pspdebug.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <stdio.h>
#include <string.h>

// mbedtls ライブラリのヘッダ
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_internal.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

// モジュール情報
PSP_MODULE_INFO("PSP_HttpsBrowser", 0, 1, 0);

// 終了用コールバック
int exit_callback(int arg1, int arg2, void* common) {
    sceKernelExitGame();
    return 0;
}

int callbackThread(SceSize args, void* argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

void setupCallbacks() {
    int thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, NULL);
    if(thid >= 0){
        sceKernelStartThread(thid, 0, NULL);
    }
}

// HTTPS 通信サンプル（example.com へ GET リクエストを送信）
int https_request()
{
    const char *server_name = "example.com";
    const char *server_port = "443";
    const char *GET_request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    int ret;

    // mbedtls用各種コンテキスト初期化
    mbedtls_net_context server_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    
    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    // ランダム生成器の初期化
    const char *pers = "psp_https_browser";
    if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                      (const unsigned char *) pers, strlen(pers))) != 0) {
        pspDebugScreenPrintf("mbedtls_ctr_drbg_seed returned -0x%04X\n", -ret);
        goto exit;
    }

    // サーバへ TCP 接続
    pspDebugScreenPrintf("Connecting to %s:%s...\n", server_name, server_port);
    if((ret = mbedtls_net_connect(&server_fd, server_name, server_port, MBEDTLS_NET_PROTO_TCP)) != 0) {
        pspDebugScreenPrintf("mbedtls_net_connect returned -0x%04X\n", -ret);
        goto exit;
    }

    // SSL設定の初期化
    if((ret = mbedtls_ssl_config_defaults(&conf,
                    MBEDTLS_SSL_IS_CLIENT,
                    MBEDTLS_SSL_TRANSPORT_STREAM,
                    MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        pspDebugScreenPrintf("mbedtls_ssl_config_defaults returned -0x%04X\n", -ret);
        goto exit;
    }

    // ここで証明書検証を無効化（古い証明書による接続障害回避のため）
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    // 必要に応じてデバッグ出力も設定可能
    // mbedtls_ssl_conf_dbg(&conf, mbedtls_debug, NULL);

    if((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
        pspDebugScreenPrintf("mbedtls_ssl_setup returned -0x%04X\n", -ret);
        goto exit;
    }

    // サーバ名設定（SNI 対応）
    if((ret = mbedtls_ssl_set_hostname(&ssl, server_name)) != 0) {
        pspDebugScreenPrintf("mbedtls_ssl_set_hostname returned -0x%04X\n", -ret);
        goto exit;
    }

    // BIO を設定してネットソケットと接続
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    // SSL ハンドシェイク
    pspDebugScreenPrintf("Performing the SSL/TLS handshake...\n");
    while((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            pspDebugScreenPrintf("mbedtls_ssl_handshake returned -0x%04X\n", -ret);
            goto exit;
        }
    }
    pspDebugScreenPrintf("Handshake successful!\n");

    // GETリクエスト送信
    pspDebugScreenPrintf("Sending GET request...\n");
    ret = mbedtls_ssl_write(&ssl, (const unsigned char *) GET_request, strlen(GET_request));
    if(ret <= 0) {
        pspDebugScreenPrintf("mbedtls_ssl_write returned -0x%04X\n", -ret);
        goto exit;
    }

    // レスポンスを受信して画面に表示
    pspDebugScreenPrintf("Receiving response...\n");
    unsigned char buf[1024];
    do {
        memset(buf, 0, sizeof(buf));
        ret = mbedtls_ssl_read(&ssl, buf, sizeof(buf)-1);
        if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if(ret <= 0)
            break;
        buf[ret] = '\0';
        pspDebugScreenPrintf("%s", buf);
    } while(1);

    pspDebugScreenPrintf("\nConnection closed.\n");

exit:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}

int main() {
    setupCallbacks();
    pspDebugScreenInit();
    pspDebugScreenPrintf("Hello PSP HTTPS Browser\n");

    // PSP ネットワーク初期化
    pspDebugScreenPrintf("Initializing network...\n");
    if(pspNetInit() < 0) {
        pspDebugScreenPrintf("pspNetInit failed.\n");
        sceKernelSleepThread();
    }
    if(pspNetInetInit() < 0) {
        pspDebugScreenPrintf("pspNetInetInit failed.\n");
        sceKernelSleepThread();
    }
    if(pspNetResolverInit() < 0) {
        pspDebugScreenPrintf("pspNetResolverInit failed.\n");
        sceKernelSleepThread();
    }
    pspDebugScreenPrintf("Network initialized.\n");

    // HTTPSリクエスト実行
    https_request();

    // 終了待ち
    pspDebugScreenPrintf("Press any key to exit...\n");
    sceKernelDelayThread(5000000);
    
    // ネットワーク終了処理
    pspNetResolverTerm();
    pspNetInetTerm();
    pspNetTerm();

    sceKernelExitGame();
    return 0;
}
