// Loads a proxy dinput8.dll the way the game does and creates a DirectInput8 device, to catch crashes before touching the game.
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
DEFINE_GUID(IID_IDirectInput8A_, 0xBF798030, 0x483A, 0x4DA2, 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00);
typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
int main(int argc, char** argv) {
    HMODULE m = LoadLibraryA(argc > 1 ? argv[1] : "dinput8.dll");
    if (!m) { printf("load failed %lu\n", GetLastError()); return 1; }
    auto fn = (DI8Create_t)GetProcAddress(m, "DirectInput8Create");
    if (!fn) { printf("no export\n"); return 1; }
    void* di = 0;
    HRESULT hr = fn(GetModuleHandleA(0), 0x0800, IID_IDirectInput8A_, &di, 0);
    printf("DirectInput8Create hr=%08lx obj=%p\n", hr, di);
    Sleep(argc > 2 ? atoi(argv[2]) : 1500);
    printf("alive\n");
    return hr == 0 && di ? 0 : 2;
}
