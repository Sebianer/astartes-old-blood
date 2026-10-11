// Lab build: dinput8 proxy with a file-driven command loop for live memory research.
// Commands go in %LOCALAPPDATA%\AstartesOldBlood\cmd.txt (one per line), output in lab.log.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <map>
#include <share.h>
#include <math.h>

static HMODULE g_real;
typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, LPUNKNOWN u) {
    if (!g_real) { char p[MAX_PATH]; GetSystemDirectoryA(p, MAX_PATH); strcat_s(p, "\\dinput8.dll"); g_real = LoadLibraryA(p); }
    return ((DI8Create_t)GetProcAddress(g_real, "DirectInput8Create"))(h, v, r, o, u);
}

static FILE* g_log;
static void L(const char* f, ...) { va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fflush(g_log); }
static uint8_t* g_base; static size_t g_isz;

static uint8_t* findStr(const char* s) {
    size_t n = strlen(s) + 1;
    for (size_t i = 1; i + n < g_isz; i++) if (g_base[i - 1] == 0 && memcmp(g_base + i, s, n) == 0) return g_base + i;
    return nullptr;
}
// static idCVar: name at +0x30, int +0x28, float +0x2c, next +0x70
static std::map<std::string, uint8_t*> g_cvars;
static bool readable(void* p, size_t n) { MEMORY_BASIC_INFORMATION m; if (!VirtualQuery(p, &m, sizeof m)) return false; return m.State == MEM_COMMIT && !(m.Protect & PAGE_GUARD) && !(m.Protect & PAGE_NOACCESS) && (uint8_t*)p + n <= (uint8_t*)m.BaseAddress + m.RegionSize; }
static void buildCvars() {
    uint8_t* s = findStr("pm_runspeed");
    uint8_t* obj = nullptr;
    for (size_t i = 0; i + 8 < g_isz; i += 8) if (*(uint64_t*)(g_base + i) == (uint64_t)s && readable(g_base + i, 8)) { obj = g_base + i - 0x30; break; }
    L("runspeed obj %p\n", obj);
    // walk both directions isn't possible; walk the next chain from the newest. Gather by scanning .data for objects shaped like cvars instead.
    for (size_t i = 0x30; i + 0x80 < g_isz; i += 8) {
        uint64_t nm = *(uint64_t*)(g_base + i);
        if (nm < (uint64_t)g_base || nm >= (uint64_t)g_base + g_isz) continue;
        uint64_t vp = *(uint64_t*)(g_base + i - 0x28);
        if (vp != (uint64_t)(g_base + i - 0x20)) continue; // value ptr points to inline buffer
        const char* name = (const char*)nm; int k = 0; for (; k < 64 && name[k] > 32 && name[k] < 127; k++); if (k < 2 || name[k] != 0) continue;
        g_cvars[name] = g_base + i - 0x30;
    }
    L("cvars found: %zu\n", g_cvars.size());
}

static std::vector<uint8_t*> g_cand;
static void scanMem(bool isInt, double v, bool filter) {
    std::vector<uint8_t*> out;
    auto match = [&](uint8_t* p) { if (isInt) return *(int32_t*)p == (int32_t)v; float f = *(float*)p; return fabs(f - (float)v) < 0.01f; };
    if (!filter) {
        MEMORY_BASIC_INFORMATION m; uint8_t* a = 0;
        while (VirtualQuery(a, &m, sizeof m)) {
            if (m.State == MEM_COMMIT && (m.Protect & (PAGE_READWRITE)) && !(m.Protect & PAGE_GUARD) && m.Type == MEM_PRIVATE) {
                uint8_t* r = (uint8_t*)m.BaseAddress;
                for (size_t i = 0; i + 4 <= m.RegionSize; i += 4) if (match(r + i)) out.push_back(r + i);
            }
            a = (uint8_t*)m.BaseAddress + m.RegionSize;
        }
    } else for (auto p : g_cand) if (readable(p, 4) && match(p)) out.push_back(p);
    g_cand = out; L("candidates: %zu\n", g_cand.size());
    for (size_t i = 0; i < g_cand.size() && i < 40; i++) L("  %p\n", g_cand[i]);
}
static void dumpAt(uint8_t* p, int len) {
    for (int o = 0; o < len; o += 8) { if (!readable(p + o, 8)) break; uint64_t q = *(uint64_t*)(p + o); float f0, f1; memcpy(&f0, p + o, 4); memcpy(&f1, p + o + 4, 4); L("  +%03x: %016llx f=%g,%g i=%d,%d\n", o, q, f0, f1, (int)q, (int)(q >> 32)); }
}

static __declspec(noinline) void runCmd(char* line) {
    char op[32] = "", a[128] = "", b[64] = "";
    sscanf_s(line, "%31s %127s %63s", op, 32, a, 128, b, 64);
    L("> %s", line);
    if (!strcmp(op, "cvar")) { auto it = g_cvars.find(a); if (it == g_cvars.end()) { L("no cvar\n"); return; } uint8_t* o = it->second; L("%s @%p int=%d float=%g str=%s flags=%x\n", a, o, *(int*)(o + 0x28), *(float*)(o + 0x2c), *(char**)(o + 8), *(int*)(o + 0x48)); }
    else if (!strcmp(op, "setcvar")) { auto it = g_cvars.find(a); if (it == g_cvars.end()) { L("no cvar\n"); return; } uint8_t* o = it->second; double v = atof(b); *(int*)(o + 0x28) = (int)v; *(float*)(o + 0x2c) = (float)v; strncpy_s((char*)(o + 0x10), 16, b, _TRUNCATE); L("set %s=%g\n", a, v); }
    else if (!strcmp(op, "listcvars")) { for (auto& kv : g_cvars) if (strstr(kv.first.c_str(), a)) L("  %s int=%d float=%g flags=%x\n", kv.first.c_str(), *(int*)(kv.second + 0x28), *(float*)(kv.second + 0x2c), *(int*)(kv.second + 0x48)); }
    else if (!strcmp(op, "scanf")) scanMem(false, atof(a), false);
    else if (!strcmp(op, "scani")) scanMem(true, atof(a), false);
    else if (!strcmp(op, "filterf")) scanMem(false, atof(a), true);
    else if (!strcmp(op, "filteri")) scanMem(true, atof(a), true);
    else if (!strcmp(op, "dump")) dumpAt((uint8_t*)_strtoui64(a, 0, 16), atoi(b) ? atoi(b) : 0x100);
    else if (!strcmp(op, "writef")) { *(float*)_strtoui64(a, 0, 16) = (float)atof(b); L("ok\n"); }
    else if (!strcmp(op, "writei")) { *(int*)_strtoui64(a, 0, 16) = atoi(b); L("ok\n"); }
    else if (!strcmp(op, "base")) L("base %p\n", g_base);
    else L("unknown\n");
}

static void safeRun(char* l) { __try { runCmd(l); } __except (1) { L("exception\n"); } }

static DWORD WINAPI worker(LPVOID) {
    char dir[MAX_PATH]; ExpandEnvironmentStringsA("%LOCALAPPDATA%\\AstartesOldBlood", dir, MAX_PATH); CreateDirectoryA(dir, 0);
    std::string logp = std::string(dir) + "\\lab.log", cmdp = std::string(dir) + "\\cmd.txt";
    g_log = _fsopen(logp.c_str(), "w", _SH_DENYNO);
    L("lab loaded\n");
    g_base = (uint8_t*)GetModuleHandleA(nullptr);
    g_isz = ((IMAGE_NT_HEADERS*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    Sleep(20000);
    buildCvars();
    for (;;) {
        Sleep(100);
        FILE* f = _fsopen(cmdp.c_str(), "r", _SH_DENYNO); if (!f) continue;
        std::vector<std::string> lines; char buf[256]; while (fgets(buf, sizeof buf, f)) lines.push_back(buf); fclose(f);
        DeleteFileA(cmdp.c_str());
        for (auto& l : lines) safeRun((char*)l.c_str());
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0, 0, worker, 0, 0, 0); }
    return TRUE;
}
