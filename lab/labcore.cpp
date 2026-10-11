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
#include <functional>


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


static void scanRegions(bool includeImage, const std::function<void(uint8_t*, size_t)>& fn) {
    MEMORY_BASIC_INFORMATION m; uint8_t* a = 0;
    while (VirtualQuery(a, &m, sizeof m)) {
        if (m.State == MEM_COMMIT && (m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(m.Protect & PAGE_GUARD) && (m.Type == MEM_PRIVATE || (includeImage && m.Type == MEM_IMAGE)))
            fn((uint8_t*)m.BaseAddress, m.RegionSize);
        a = (uint8_t*)m.BaseAddress + m.RegionSize;
    }
}
static void findStrCmd(const char* t, bool sub = false) {
    size_t n = strlen(t) + (sub ? 0 : 1); int c = 0;
    scanRegions(true, [&](uint8_t* r, size_t sz) { for (size_t i = 0; i + n <= sz && c < 60; i++) if (r[i] == (uint8_t)t[0] && memcmp(r + i, t, n) == 0) { L("  str %p\n", r + i); c++; } });
    L("found %d\n", c);
}
static void findPtrCmd(uint64_t v) {
    int c = 0;
    scanRegions(true, [&](uint8_t* r, size_t sz) { for (size_t i = 0; i + 8 <= sz && c < 60; i += 8) if (*(uint64_t*)(r + i) == v) { L("  ptr at %p\n", r + i); c++; } });
    L("found %d\n", c);
}
static void dumpS(uint8_t* p, int len) {
    for (int o = 0; o < len; o += 8) {
        if (!readable(p + o, 8)) break; uint64_t q = *(uint64_t*)(p + o); float f0, f1; memcpy(&f0, p + o, 4); memcpy(&f1, p + o + 4, 4);
        char s[48] = ""; if (q > 0x10000 && q < 0x7fffffffffff && readable((void*)q, 32)) { const char* c = (const char*)q; int k = 0; for (; k < 40 && c[k] >= 32 && c[k] < 127; k++) s[k] = c[k]; s[k] = 0; if (k < 3) s[0] = 0; }
        L("  +%03x: %016llx f=%g,%g i=%d,%d %s\n", o, q, f0, f1, (int)q, (int)(q >> 32), s);
    }
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
    else if (!strcmp(op, "findstr")) findStrCmd(a);
    else if (!strcmp(op, "findsub")) findStrCmd(a, true);
    else if (!strcmp(op, "strat")) { const char* p = (const char*)_strtoui64(a, 0, 16); char s[128]; int k = 0; while (k < 120 && readable((void*)(p - 1), 1) && p[-1] >= 32 && p[-1] < 127) { p--; k++; } k = 0; for (; k < 120 && p[k] >= 32 && p[k] < 127; k++) s[k] = p[k]; s[k] = 0; L("  %p '%s'\n", p, s); }
    else if (!strcmp(op, "findptr")) findPtrCmd(_strtoui64(a, 0, 16));
    else if (!strcmp(op, "findptri")) { uint64_t v = _strtoui64(a, 0, 16); int c = 0; for (size_t i = 0; i + 8 <= g_isz; i += 8) if (readable(g_base + i, 8) && *(uint64_t*)(g_base + i) == v) { L("  img+%zx\n", i); if (++c > 40) break; } L("found %d\n", c); }
    else if (!strcmp(op, "near")) { // near <addr> <value> : find float/int value within 0x8000 bytes of addr
        uint8_t* p = (uint8_t*)_strtoui64(a, 0, 16); double v = atof(b);
        for (int o = 0; o < 0x8000; o += 4) { if (!readable(p + o, 4)) continue; if (fabs(*(float*)(p + o) - v) < 0.01 || *(int*)(p + o) == (int)v) L("  +%x %s\n", o, *(int*)(p + o) == (int)v ? "int" : "float"); }
    }
    else if (!strcmp(op, "player")) { uint8_t* gl = *(uint8_t**)(g_base + 0x1823f68); L("gameLocal %p player %p\n", gl, gl ? *(uint8_t**)(gl + 0x161D0) : 0); }
    else if (!strcmp(op, "snap") || !strcmp(op, "diff")) { // snap/diff <addr> <len>: report floats that changed
        static std::vector<uint8_t> s_snap; static uint8_t* s_at;
        uint8_t* p = (uint8_t*)_strtoui64(a, 0, 16); int len = (int)strtol(b, 0, 16); if (!len) len = 0x2000;
        std::vector<uint8_t> cur(len); for (int o = 0; o < len; o += 8) { if (readable(p + o, 8)) memcpy(&cur[o], p + o, 8); }
        if (op[0] == 'd' && s_at == p && (int)s_snap.size() == len) {
            for (int o = 0; o + 4 <= len; o += 4) { float x0 = *(float*)&s_snap[o], x1 = *(float*)&cur[o]; if (x0 != x1 && fabs(x1) > 1 && fabs(x1) < 100000 && fabs(x0) > 1 && fabs(x0) < 100000) L("  +%x %g -> %g\n", o, x0, x1); }
        }
        s_snap = cur; s_at = p; L("snap ok\n");
    }
    else if (!strcmp(op, "aipos")) { // offsets that hold a plausible xyz triple in every live AI, with distinct values
        uint8_t* gl = *(uint8_t**)(g_base + 0x1823f68); if (!gl) return;
        typedef void* (*GetComp_t)(void*); typedef float (*GetF_t)(void*);
        std::vector<uint8_t*> ais;
        for (int i = 0; i < 4096; i++) { uint8_t* e = *(uint8_t**)(gl + 0x161D0 + i * 8); if (!e || *(uint64_t*)e != (uint64_t)g_base + 0xf0d088) continue; void* c = (*(GetComp_t**)e)[0x300 / 8](e); if (c && (*(GetF_t**)c)[0x78 / 8](c) > 0) ais.push_back(e); }
        L("live ais %zu\n", ais.size());
        auto ok = [](float v) { return _finite(v) && fabs(v) < 30000; };
        for (int o = 0x10; o < 0xAD00; o += 4) {
            int good = 0; float firstx = 0; bool distinct = false;
            for (auto e : ais) { float* f = (float*)(e + o); if (ok(f[0]) && ok(f[1]) && ok(f[2]) && fabs(f[0]) > 20 && fabs(f[1]) > 20) { if (!good) firstx = f[0]; else if (f[0] != firstx) distinct = true; good++; } }
            if (good == (int)ais.size() && good > 1 && distinct) { float* f = (float*)(ais[0] + o); L("  +%x  e.g. %g %g %g\n", o, f[0], f[1], f[2]); }
        }
    }
    else if (!strcmp(op, "hp")) {
        uint8_t* e = (uint8_t*)_strtoui64(a, 0, 16);
        typedef void* (*GetComp_t)(void*); typedef float (*GetF_t)(void*);
        void* comp = (*(GetComp_t**)e)[0x300 / 8](e);
        L("comp %p vt=img+%llx\n", comp, comp ? *(uint64_t*)comp - (uint64_t)g_base : 0);
        if (comp) { float h = (*(GetF_t**)comp)[0x78 / 8](comp), m = (*(GetF_t**)comp)[0x80 / 8](comp); L("health %g max %g\n", h, m); dumpS((uint8_t*)comp, 0x100); }
    }
    else if (!strcmp(op, "ais")) {
        uint8_t* gl = **(uint8_t***)(g_base + 0x1823f68) ? *(uint8_t**)(g_base + 0x1823f68) : 0;
        typedef void* (*GetComp_t)(void*); typedef float (*GetF_t)(void*);
        for (int i = 0; i < 4096 && gl; i++) {
            uint8_t* e = *(uint8_t**)(gl + 0x161D0 + i * 8); if (!e) continue;
            if (*(uint64_t*)e != (uint64_t)g_base + 0xf0d088) continue;
            void* comp = (*(GetComp_t**)e)[0x300 / 8](e); float h = comp ? (*(GetF_t**)comp)[0x78 / 8](comp) : -1;
            L("  [%d] %p hp=%g %.60s\n", i, e, h, *(char**)(e + 0x20));
        }
    }
    else if (!strcmp(op, "ents")) {
        uint8_t* gl = (uint8_t*)_strtoui64(a, 0, 16); int off = (int)strtol(b, 0, 16);
        for (int i = 0; i < 4096; i++) { uint8_t* e = *(uint8_t**)(gl + off + i * 8); if (!e || !readable(e, 0x40)) continue; uint64_t vt = *(uint64_t*)e; const char* nm = readable(*(char**)(e + 0x20), 8) ? *(char**)(e + 0x20) : "?"; L("  [%d] %p vt=img+%llx %.60s\n", i, e, vt - (uint64_t)g_base, nm); }
    }
    else if (!strcmp(op, "strs")) { // strs <prefix> [contains]: unique printable strings in memory starting with prefix
        size_t n = strlen(a); std::map<std::string, int> seen;
        scanRegions(true, [&](uint8_t* r, size_t sz) {
            for (size_t i = 1; i + n < sz; i++) {
                if (r[i] != (uint8_t)a[0] || r[i - 1] != 0 || memcmp(r + i, a, n)) continue;
                size_t k = n; while (i + k < sz && k < 160 && r[i + k] >= 32 && r[i + k] < 127) k++;
                if (i + k < sz && r[i + k] == 0) { std::string s((char*)r + i, k); if (!b[0] || strstr(s.c_str(), b)) seen[s]++; }
            }
        });
        for (auto& kv : seen) L("  %s\n", kv.first.c_str());
        L("unique %zu\n", seen.size());
    }
    else if (!strcmp(op, "entsf")) { // entsf <substr>: entities whose name contains substr
        uint8_t* gl = *(uint8_t**)(g_base + 0x1823f68); if (!gl) return;
        for (int i = 0; i < 4096; i++) { uint8_t* e = *(uint8_t**)(gl + 0x161D0 + i * 8); if (!e || !readable(e, 0x40)) continue; const char* nm = *(char**)(e + 0x20); if (!readable((void*)nm, 8) || !strstr(nm, a)) continue; L("  [%d] %p vt=img+%llx %.70s\n", i, e, *(uint64_t*)e - (uint64_t)g_base, nm); }
    }
    else if (!strcmp(op, "ptrsto")) { // ptrsto <obj> <len>: which qwords inside obj point at entities (index + name)
        uint8_t* p = (uint8_t*)_strtoui64(a, 0, 16); int len = (int)strtol(b, 0, 16); if (!len) len = 0x4000;
        uint8_t* gl = *(uint8_t**)(g_base + 0x1823f68); if (!gl) return;
        std::map<uint64_t, int> idx; for (int i = 0; i < 4096; i++) { uint64_t e = *(uint64_t*)(gl + 0x161D0 + i * 8); if (e) idx[e] = i; }
        for (int o = 0; o + 8 <= len; o += 8) { if (!readable(p + o, 8)) continue; uint64_t q = *(uint64_t*)(p + o); auto it = idx.find(q); if (it != idx.end()) L("  +%x -> [%d] %.60s\n", o, it->second, *(char**)(q + 0x20)); }
    }
    else if (!strcmp(op, "dumps")) dumpS((uint8_t*)_strtoui64(a, 0, 16), atoi(b) ? atoi(b) : 0x100);
    else if (!strcmp(op, "base")) L("base %p\n", g_base);
    else if (!strcmp(op, "dumpimg")) {
        FILE* f = 0; fopen_s(&f, a, "wb"); if (!f) { L("open failed\n"); return; }
        std::vector<uint8_t> page(0x1000);
        for (size_t o = 0; o < g_isz; o += 0x1000) { if (readable(g_base + o, 0x1000)) memcpy(page.data(), g_base + o, 0x1000); else memset(page.data(), 0, 0x1000); fwrite(page.data(), 1, 0x1000, f); }
        fclose(f); L("dumped %zu bytes\n", g_isz);
    }
    else L("unknown\n");
}

static void safeRun(char* l) { __try { runCmd(l); } __except (1) { L("exception\n"); } }

extern "C" __declspec(dllexport) void CoreInit(const char* logPath) {
    g_log = _fsopen(logPath, "a", _SH_DENYNO);
    g_base = (uint8_t*)GetModuleHandleA(nullptr);
    g_isz = ((IMAGE_NT_HEADERS*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    buildCvars();
}
extern "C" __declspec(dllexport) void CoreCmd(char* line) { safeRun(line); }
extern "C" __declspec(dllexport) void CoreShutdown() { if (g_log) fclose(g_log); g_log = 0; }
