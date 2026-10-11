// Lab loader: dinput8 proxy that hot-loads labcore.dll and feeds it commands from cmd.txt.
#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <string>
#include <vector>

static HMODULE g_real;
typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, LPUNKNOWN u) {
    if (!g_real) { char p[MAX_PATH]; GetSystemDirectoryA(p, MAX_PATH); strcat_s(p, "\\dinput8.dll"); g_real = LoadLibraryA(p); }
    if (!g_real) return E_FAIL;
    auto fn = (DI8Create_t)GetProcAddress(g_real, "DirectInput8Create");
    return fn ? fn(h, v, r, o, u) : E_FAIL;
}

static std::string g_dir;
static HMODULE g_core; static int g_gen;
typedef void (*Init_t)(const char*); typedef void (*Cmd_t)(char*); typedef void (*Shut_t)();
static void logLine(const char* s) { FILE* f = _fsopen((g_dir + "\\lab.log").c_str(), "a", _SH_DENYNO); if (f) { fputs(s, f); fclose(f); } }

static void loadCore() {
    if (g_core) { ((Shut_t)GetProcAddress(g_core, "CoreShutdown"))(); FreeLibrary(g_core); g_core = 0; }
    std::string src = g_dir + "\\labcore.dll", dst = g_dir + "\\labcore_" + std::to_string(++g_gen) + ".dll";
    if (!CopyFileA(src.c_str(), dst.c_str(), FALSE)) { logLine("core copy failed\n"); return; }
    g_core = LoadLibraryA(dst.c_str());
    if (!g_core) { logLine("core load failed\n"); return; }
    ((Init_t)GetProcAddress(g_core, "CoreInit"))((g_dir + "\\lab.log").c_str());
    logLine("core loaded\n");
}

static DWORD WINAPI worker(LPVOID) {
    char d[MAX_PATH]; ExpandEnvironmentStringsA("%LOCALAPPDATA%\\AstartesOldBlood", d, MAX_PATH); CreateDirectoryA(d, 0); g_dir = d;
    FILE* f = _fsopen((g_dir + "\\lab.log").c_str(), "w", _SH_DENYNO); if (f) fclose(f);
    logLine("loader up\n");
    std::string cmdp = g_dir + "\\cmd.txt";
    for (;;) {
        Sleep(100);
        FILE* c = _fsopen(cmdp.c_str(), "r", _SH_DENYNO); if (!c) continue;
        std::vector<std::string> lines; char buf[512]; while (fgets(buf, sizeof buf, c)) lines.push_back(buf); fclose(c);
        DeleteFileA(cmdp.c_str());
        for (auto& l : lines) {
            if (l.compare(0, 6, "reload") == 0) loadCore();
            else if (g_core) ((Cmd_t)GetProcAddress(g_core, "CoreCmd"))((char*)l.c_str());
            else logLine("no core; send reload\n");
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0, 0, worker, 0, 0, 0); }
    return TRUE;
}
