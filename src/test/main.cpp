#include <pspkernel.h>
#include <pspdebug.h>

PSP_MODULE_INFO("Tutorial", 0, 1, 0);

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

auto main() -> int {
	setupCallbacks();
	pspDebugScreenInit();
	
	pspDebugScreenPrintf("Hello World!");
}