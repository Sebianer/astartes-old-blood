// Astartes: The Old Blood - in-game part. A dinput8.dll proxy loaded by Wolfenstein: The Old Blood.
// Reads %LOCALAPPDATA%\AstartesOldBlood\session.ini (written by the launcher) and plays the picked Space Marine 2 class:
// class stats, ability on V, deployment moment, voice barks and a class HUD drawn on the game's OpenGL frame.
// Without session.ini, or on a different game build, it does nothing but forward DirectInput8Create.
#include <windows.h>
#include <mmsystem.h>
#include <gl/GL.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <share.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include "MinHook.h"
#include "gen_data.h"

// ---------------------------------------------------------------- proxy
static HMODULE g_real;
typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, LPUNKNOWN u) {
    if (!g_real) { char p[MAX_PATH]; GetSystemDirectoryA(p, MAX_PATH); strcat_s(p, "\\dinput8.dll"); g_real = LoadLibraryA(p); }
    if (!g_real) return E_FAIL;
    auto fn = (DI8Create_t)GetProcAddress(g_real, "DirectInput8Create");
    return fn ? fn(h, v, r, o, u) : E_FAIL;
}

// ---------------------------------------------------------------- basics
static std::string g_dir, g_content;
static FILE* g_log;
static void L(const char* f, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fflush(g_log);
}
static double now() { static LARGE_INTEGER fq; if (!fq.QuadPart) QueryPerformanceFrequency(&fq); LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / fq.QuadPart; }
static float frand() { return (float)rand() / RAND_MAX; }

static uint8_t* g_base; static size_t g_isz;
static bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION m; if (!p || !VirtualQuery(p, &m, sizeof m)) return false;
    return m.State == MEM_COMMIT && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (const uint8_t*)p + n <= (uint8_t*)m.BaseAddress + m.RegionSize;
}

static std::map<std::string, std::string> readIni(const std::string& path) {
    std::map<std::string, std::string> d; FILE* f = _fsopen(path.c_str(), "r", _SH_DENYNO); if (!f) return d;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t e = s.find('='); if (e != std::string::npos) d[s.substr(0, e)] = s.substr(e + 1);
    }
    fclose(f); return d;
}

// ---------------------------------------------------------------- cvars (static idCVar objects inside the exe)
struct CVar { uint8_t* o = nullptr; float def = 0; };
static std::unordered_map<std::string, CVar> g_cvars;
static bool scanCvars() {
    g_cvars.clear();
    for (size_t i = 0x30; i + 0x80 < g_isz; i += 8) {
        uint64_t nm = *(uint64_t*)(g_base + i);
        if (nm < (uint64_t)g_base || nm >= (uint64_t)g_base + g_isz) continue;
        if (*(uint64_t*)(g_base + i - 0x28) != (uint64_t)(g_base + i - 0x20)) continue; // value ptr -> inline buffer
        const char* name = (const char*)nm; int k = 0; for (; k < 64 && name[k] > 32 && name[k] < 127; k++); if (k < 2 || name[k]) continue;
        CVar c; c.o = g_base + i - 0x30; c.def = *(float*)(c.o + 0x2c); g_cvars[name] = c;
    }
    return g_cvars.count("pm_runspeed") && g_cvars.count("g_damageScale");
}
static CVar* cv(const char* n) { auto it = g_cvars.find(n); return it == g_cvars.end() ? nullptr : &it->second; }
static void setCvar(const char* n, float v) {
    CVar* c = cv(n); if (!c) return;
    if (*(float*)(c->o + 0x2c) == v && *(int*)(c->o + 0x28) == (int)v) return;
    *(int*)(c->o + 0x28) = (int)v; *(float*)(c->o + 0x2c) = v;
    char b[16]; if (v == (int)v) sprintf_s(b, "%d", (int)v); else sprintf_s(b, "%g", v); strncpy_s((char*)(c->o + 0x10), 16, b, _TRUNCATE);
}
static float getCvar(const char* n) { CVar* c = cv(n); return c ? *(float*)(c->o + 0x2c) : 0; }
// Writes base * mult, where base is whatever the game itself last set (difficulty, cinematics), so we scale, never replace.
struct Scaled { const char* name; float base; float written; };
static Scaled g_scaled[] = { {"pm_runspeed", 0, NAN}, {"pm_sprintspeed", 0, NAN}, {"pm_jumpheight", 0, NAN}, {"g_damageScale", 0, NAN}, {"timescale", 0, NAN} };
static void scaleCvar(int i, float mult) {
    Scaled& s = g_scaled[i]; if (!cv(s.name)) return;
    float cur = getCvar(s.name);
    if (cur != s.written) s.base = cur;
    float v = s.base * mult; setCvar(s.name, v); s.written = getCvar(s.name);
}

// ---------------------------------------------------------------- game objects
typedef void* (*GetComp_t)(void*); typedef float (*GetF_t)(void*);
typedef void (*PostEvent_t)(void*, void*, int);
static uint8_t* gameLocal() { uint8_t* g = *(uint8_t**)(g_base + HK_gameLocal); return readable(g, HK_entities + 4096 * 8) ? g : nullptr; }
static uint8_t* entity(uint8_t* gl, int i) { return *(uint8_t**)(gl + HK_entities + i * 8); }
static uint8_t* healthComp(uint8_t* e) { return (uint8_t*)(*(GetComp_t**)e)[HK_vfHealthComp / 8](e); }
static float compHealth(uint8_t* c) { return (*(GetF_t**)c)[HK_vfHealth / 8](c); }
static float compMax(uint8_t* c) { return (*(GetF_t**)c)[HK_vfMaxHealth / 8](c); }
static bool isAI(uint8_t* e) { return e && readable(e, 0x60) && *(uint64_t*)e == (uint64_t)(g_base + HK_vtAI); }
// AIs that are in the world right now (the game's active list), not spawn templates waiting in the entity table
static std::vector<uint8_t*> activeAIs(uint8_t* gl) {
    std::vector<uint8_t*> out;
    uint8_t** list = *(uint8_t***)(gl + HK_activeList); int n = *(int*)(gl + HK_activeCount);
    if (n <= 0 || n > 16384 || !readable(list, n * 8)) return out;
    for (int i = 0; i < n; i++) if (isAI(list[i])) out.push_back(list[i]);
    return out;
}

// AI health float offset inside its health component, verified at runtime by read-back through the game's getter.
static int g_aiHealthOff = HK_aiHealth;
static bool g_aiHealthOk = false;
static bool setAIHealth(uint8_t* c, float v) {
    float* p = (float*)(c + g_aiHealthOff); float old = *p; *p = v;
    if (!g_aiHealthOk) {
        if (compHealth(c) != v) { *p = old; L("AI health offset +%x did not read back; damage features off\n", g_aiHealthOff); g_aiHealthOff = -1; return false; }
        g_aiHealthOk = true; L("AI health offset +%x verified\n", g_aiHealthOff);
    }
    return true;
}
// Zero health alone does not kill an AI; the turret leaves its target at a sliver so the next hit of any kind kills.
static int g_killMode = 0; // debug: 1 = also post the kill event (does not kill idAI2 - verified 2026-10-11)
static void killAI(uint8_t* e, uint8_t* c) {
    if (g_aiHealthOff < 0) return;
    setAIHealth(c, g_killMode == 1 ? 0.f : 0.5f);
    if (g_killMode == 1) ((PostEvent_t)(g_base + HK_postEvent))(e, g_base + HK_evKill, 0);
}
typedef void (*PostEventF_t)(void*, void*, int, ...);

// ---------------------------------------------------------------- audio (two waveOut channels: voice, sfx)
struct Wav { WAVEFORMATEX fmt{}; std::vector<char> pcm; };
static bool loadWav(const std::string& path, Wav& w) {
    FILE* f = _fsopen(path.c_str(), "rb", _SH_DENYNO); if (!f) return false;
    std::vector<char> b; fseek(f, 0, SEEK_END); b.resize(ftell(f)); fseek(f, 0, SEEK_SET); fread(b.data(), 1, b.size(), f); fclose(f);
    if (b.size() < 44 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
    bool haveFmt = false;
    for (size_t p = 12; p + 8 <= b.size();) {
        uint32_t sz = *(uint32_t*)(b.data() + p + 4);
        if (!memcmp(b.data() + p, "fmt ", 4)) { memcpy(&w.fmt, b.data() + p + 8, min(sz, (uint32_t)16)); w.fmt.cbSize = 0; haveFmt = true; }
        else if (!memcmp(b.data() + p, "data", 4)) { sz = (uint32_t)min((size_t)sz, b.size() - p - 8); w.pcm.assign(b.data() + p + 8, b.data() + p + 8 + sz); }
        p += 8 + sz + (sz & 1);
    }
    return haveFmt && w.fmt.wFormatTag == WAVE_FORMAT_PCM && !w.pcm.empty();
}
struct Channel {
    HWAVEOUT h = 0; WAVEFORMATEX fmt{}; WAVEHDR hdr{}; std::mutex m;
    void play(const Wav* w) {
        if (!w) return;
        std::lock_guard<std::mutex> lk(m);
        if (h && memcmp(&fmt, &w->fmt, sizeof fmt)) { waveOutReset(h); waveOutUnprepareHeader(h, &hdr, sizeof hdr); waveOutClose(h); h = 0; hdr = {}; }
        if (!h) { fmt = w->fmt; if (waveOutOpen(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { h = 0; return; } }
        waveOutReset(h);
        if (hdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(h, &hdr, sizeof hdr);
        hdr = {}; hdr.lpData = (LPSTR)w->pcm.data(); hdr.dwBufferLength = (DWORD)w->pcm.size();
        waveOutPrepareHeader(h, &hdr, sizeof hdr); waveOutWrite(h, &hdr, sizeof hdr);
    }
    void stop() { std::lock_guard<std::mutex> lk(m); if (h) waveOutReset(h); }
    bool busy() { std::lock_guard<std::mutex> lk(m); return h && (hdr.dwFlags & WHDR_PREPARED) && !(hdr.dwFlags & WHDR_DONE); }
};
static Channel g_voiceCh, g_sfxCh;
static std::map<std::string, std::vector<Wav>> g_voice; // role -> takes
static std::map<std::string, Wav> g_sfx;
static std::map<std::string, double> g_voiceLast;

static void loadAudio(const char* classId) {
    WIN32_FIND_DATAA fd;
    std::string pat = g_content + "\\voice\\" + classId + "_*.wav";
    HANDLE fh = FindFirstFileA(pat.c_str(), &fd);
    if (fh != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName; // <class>_<role>_<n>.wav
            size_t a = strlen(classId) + 1, b = n.rfind('_'); if (b == std::string::npos || b <= a) continue;
            Wav w; if (loadWav(g_content + "\\voice\\" + n, w)) g_voice[n.substr(a, b - a)].push_back(std::move(w));
        } while (FindNextFileA(fh, &fd));
        FindClose(fh);
    }
    for (auto& ab : kAbilities) { Wav w; if (loadWav(g_content + "\\sfx\\" + ab.sfx + ".wav", w)) g_sfx[ab.sfx] = std::move(w); }
    L("audio: %zu voice roles, %zu sfx\n", g_voice.size(), g_sfx.size());
}
static const VoiceDef* voiceDef(const char* role) { for (auto& v : kVoice) if (!strcmp(v.id, role)) return &v; return nullptr; }
// Says a line of this role; ability/deploy lines always play, others obey chance and cooldown.
static void say(const char* role, bool force = false) {
    auto it = g_voice.find(role); if (it == g_voice.end() || it->second.empty()) return;
    const VoiceDef* d = voiceDef(role); double t = now();
    if (!force && d) {
        if (t - g_voiceLast[role] < d->cooldown || frand() > d->chance || g_voiceCh.busy()) return;
    }
    g_voiceLast[role] = t;
    g_voiceCh.play(&it->second[rand() % it->second.size()]);
}
static void sfx(const char* id) { auto it = g_sfx.find(id); if (it != g_sfx.end()) g_sfxCh.play(&it->second); }

// ---------------------------------------------------------------- session + test reporting
static const ClassDef* g_class; static const AbilityDef* g_ab;
static bool g_skipIntro, g_deployMoment, g_debug;
static std::string g_testChecks; static int g_testAutoAbility;
static std::map<std::string, bool> g_reported;
static void check(const char* name, bool ok, const char* why = "") {
    if (g_testChecks.empty() || g_reported[name]) return;
    g_reported[name] = true;
    FILE* f = _fsopen(g_testChecks.c_str(), "a", _SH_DENYNO); if (!f) return;
    if (ok) fprintf(f, "pass %s\n", name); else fprintf(f, "fail %s: %s\n", name, why);
    fclose(f); L("test check %s %s\n", name, ok ? "pass" : "fail");
}
// The launcher passes Melty's test file (testFile= in session.ini) only during a Melty test run.
static void readTestFile(const std::string& p) {
    if (p.empty()) return;
    FILE* f = _fsopen(p.c_str(), "r", _SH_DENYNO); if (!f) return;
    char b[4096]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f);
    const char* k = strstr(b, "\"checks\""); if (!k) return;
    k = strchr(k + 8, '"'); if (!k) return; k++;
    std::string s; for (; *k && *k != '"'; k++) { if (*k == '\\' && k[1]) { k++; s += (*k == '/' ? '/' : *k); } else s += *k; }
    g_testChecks = s; g_testAutoAbility = 1; L("melty test run, checks -> %s\n", s.c_str());
}

// ---------------------------------------------------------------- shared state for the HUD
static volatile bool g_inGame;
static volatile double g_deployAt = -100, g_abilityStart = -100, g_abilityReadyAt = 0;
static volatile float g_health = 1, g_armour = 0;
static volatile bool g_hudDrawn;

// ---------------------------------------------------------------- game tick (worker thread)
static uint8_t* g_lastPlayer; static double g_playerSeen; static float g_spawnPos[3]; static bool g_deployed, g_spawnSampled; static float g_spawnHealth;
struct AIState { float health; float scale; }; // scale = damage multiplier already folded into its health
static std::unordered_map<uint8_t*, AIState> g_aiHealth;
static double g_nextTurret, g_lowHealthArmed = 1;
static bool g_keyWasDown;

static float* playerPos(uint8_t* p) { return (float*)(p + HK_plPos); }

static void applyStats(double t) {
    bool active = t - g_abilityStart < g_ab->duration;
    // class stats are relative to Old Blood's defaults (run 260, sprint 450, jump 62)
    float run = g_class->runSpeed / 260.f, sprint = g_class->sprintSpeed / 450.f, jump = g_class->jumpHeight / 62.f, taken = g_class->damageTaken, ts = 1;
    if (active) switch (g_ab->effect) {
        case FX_surge: run *= g_ab->speedMul; sprint *= g_ab->speedMul; jump *= g_ab->jumpMul; break;
        case FX_dash: run *= g_ab->speedMul; sprint *= g_ab->speedMul; break;
        case FX_invuln: taken = 0; break;
        case FX_slowmo: ts = g_ab->speedMul; break;
        default: break;
    }
    scaleCvar(0, run); scaleCvar(1, sprint); scaleCvar(2, jump); scaleCvar(3, taken); scaleCvar(4, ts);
}

static void useAbility(uint8_t* player, double t) {
    g_abilityStart = t; g_abilityReadyAt = t + g_ab->cooldown; g_nextTurret = t;
    say("ability", true); sfx(g_ab->sfx);
    L("ability %s\n", g_ab->id);
    if (g_ab->effect == FX_restore) {
        uint8_t* c = healthComp(player);
        if (c) { *(float*)(c + HK_plHealth) = compMax(c); *(float*)(c + HK_plArmour) = *(float*)(c + HK_plArmourMax); }
    }
    check("ability-used", true);
}

static void tickAIs(uint8_t* gl, uint8_t* player, double t) {
    bool turret = g_ab->effect == FX_turret && t - g_abilityStart < g_ab->duration && t >= g_nextTurret;
    float mult = g_class->damageDealt * ((g_ab->effect == FX_damageBuff && t - g_abilityStart < g_ab->duration) ? g_ab->power : 1.f);
    float* pp = playerPos(player); uint8_t* best = nullptr; uint8_t* bestC = nullptr; float bestD = g_ab->range * g_ab->range;
    std::unordered_map<uint8_t*, AIState> seen;
    for (uint8_t* e : activeAIs(gl)) {
        uint8_t* c = healthComp(e); if (!c) continue;
        float h = compHealth(c);
        auto it = g_aiHealth.find(e);
        AIState s = it != g_aiHealth.end() ? it->second : AIState{ h, 1.f };
        if (s.health > 0 && h <= 0) { say("kill"); check("kill-bark", true); }
        // Damage dealt: an AI's remaining health is kept divided by the current multiplier, so every hit counts
        // mult times and deaths still happen inside the game's own damage code.
        if (h > 0 && s.scale != mult && g_aiHealthOff >= 0) {
            float nh = max(0.5f, h * s.scale / mult);
            if (setAIHealth(c, nh)) { h = nh; s.scale = mult; }
        }
        s.health = h; seen[e] = s;
        if (turret && h > 0.5f) {
            float* ap = (float*)(e + HK_aiPos); float dx = ap[0] - pp[0], dy = ap[1] - pp[1], dz = ap[2] - pp[2], d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) { bestD = d; best = e; bestC = c; }
        }
    }
    g_aiHealth.swap(seen);
    if (turret) {
        g_nextTurret = t + g_ab->interval;
        if (best && g_aiHealthOff >= 0) {
            sfx(g_ab->sfx);
            float h = compHealth(bestC) - g_ab->power / g_aiHealth[best].scale;
            if (h <= 0.5f) killAI(best, bestC); else setAIHealth(bestC, h);
            g_aiHealth[best].health = compHealth(bestC);
        }
    }
}

static void tick() {
    double t = now();
    uint8_t* gl = gameLocal();
    uint8_t* player = gl ? entity(gl, 0) : nullptr;
    if (player && (!readable(player, HK_plPos + 12) || *(uint64_t*)player != (uint64_t)(g_base + HK_vtPlayer))) player = nullptr;
    if (player != g_lastPlayer) {
        g_lastPlayer = player; g_deployed = false; g_aiHealth.clear();
        if (player) { g_playerSeen = t; g_spawnSampled = false; g_spawnHealth = -1; L("player %p\n", player); }
    }
    g_inGame = player != nullptr;
    applyStats(t);
    if (!player) return;
    check("class-applied", fabsf(getCvar("pm_runspeed") - g_scaled[0].base * g_class->runSpeed / 260.f) < 0.5f || t - g_abilityStart < g_ab->duration);

    uint8_t* c = healthComp(player);
    if (c) {
        float mx = compMax(c), h = *(float*)(c + HK_plHealth), am = *(float*)(c + HK_plArmourMax);
        g_health = mx > 0 ? h / mx : 0; g_armour = am > 0 ? *(float*)(c + HK_plArmour) / am : 0;
        if (g_health < 0.3f && h > 0 && g_lowHealthArmed) { say("lowHealth"); g_lowHealthArmed = 0; }
        if (g_health > 0.6f) g_lowHealthArmed = 1;
    }

    // deployment: the first moment the player moves or gets hurt. Loading screens ("Press E to continue") keep
    // the player still and unhurt, so those are the real signals; 90 s is only a fallback.
    float ph = c ? *(float*)(c + HK_plHealth) : 0;
    if (!g_spawnSampled && t - g_playerSeen > 2) { memcpy(g_spawnPos, playerPos(player), 12); g_spawnHealth = ph; g_spawnSampled = true; }
    if (!g_deployed && g_spawnSampled) {
        float* p = playerPos(player); float dx = p[0] - g_spawnPos[0], dy = p[1] - g_spawnPos[1];
        if (dx * dx + dy * dy > 25 || ph < g_spawnHealth || t - g_playerSeen > 90) {
            g_deployed = true;
            if (g_deployMoment) {
                g_deployAt = t; say("deploy", true);
                if (c) *(float*)(c + HK_plArmour) = *(float*)(c + HK_plArmourMax);
                check("deploy-moment", true);
            }
            g_abilityReadyAt = min((double)g_abilityReadyAt, t);
            L("deployed\n");
        }
    }

    bool fg = false; { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); fg = pid == GetCurrentProcessId(); }
    bool down = fg && (GetAsyncKeyState(HUD_abilityKey) & 0x8000);
    bool wantAbility = (down && !g_keyWasDown) || (g_testAutoAbility == 1 && g_deployed && t - g_deployAt > 4);
    g_keyWasDown = down;
    if (wantAbility && g_deployed && t >= g_abilityReadyAt) { if (g_testAutoAbility == 1) g_testAutoAbility = 2; useAbility(player, t); }
    static bool wasReady = true; bool ready = t >= g_abilityReadyAt;
    if (ready && !wasReady) say("abilityReady");
    wasReady = ready;

    if (gl) tickAIs(gl, player, t);
}

// ---------------------------------------------------------------- HUD (SwapBuffers hook, OpenGL 1.x immediate mode)
typedef BOOL(WINAPI* Swap_t)(HDC);
static Swap_t o_wglSwap, o_gdiSwap;
static thread_local int t_swapDepth;
static GLuint g_cardTex; static bool g_cardFailed; static GLuint g_font; static int g_fontPx; static HFONT g_hfont;
static HGLRC g_hudCtx; static bool g_hudCtxFailed;

static void loadCardTex() {
    if (g_cardTex || g_cardFailed) return;
    std::string p = g_content + "\\card_" + g_class->id + ".bgra";
    FILE* f = _fsopen(p.c_str(), "rb", _SH_DENYNO); if (!f) { g_cardFailed = true; return; }
    std::vector<uint8_t> px(396 * 630 * 4); fread(px.data(), 1, px.size(), f); fclose(f);
    glGenTextures(1, &g_cardTex); glBindTexture(GL_TEXTURE_2D, g_cardTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 396, 630, 0, 0x80E1 /*GL_BGRA*/, GL_UNSIGNED_BYTE, px.data());
}

static void ensureFont(HDC hdc, int px) {
    if (g_font && g_fontPx == px) return;
    if (g_font) glDeleteLists(g_font, 96);
    if (g_hfont) DeleteObject(g_hfont);
    g_hfont = CreateFontA(-px, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Segoe UI");
    HGDIOBJ old = SelectObject(hdc, g_hfont);
    g_font = glGenLists(96); wglUseFontBitmapsA(hdc, 32, 96, g_font);
    SelectObject(hdc, old); g_fontPx = px;
}
static void text(float x, float y, const char* s, const unsigned char* col, float alpha) {
    glColor4ub(0, 0, 0, (GLubyte)(200 * alpha)); glRasterPos2f(x + 1, y + 1); glListBase(g_font - 32); glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
    glColor4ub(col[0], col[1], col[2], (GLubyte)(col[3] * alpha)); glRasterPos2f(x, y); glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}
static void rect(float x, float y, float w, float h, const unsigned char* c, float alpha) {
    glColor4ub(c[0], c[1], c[2], (GLubyte)(c[3] * alpha));
    glBegin(GL_QUADS); glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h); glEnd();
}

static void drawHud(HDC hdc) {
    if (!g_inGame || !g_deployed) return;
    RECT rc; HWND wnd = WindowFromDC(hdc); if (!wnd || !GetClientRect(wnd, &rc)) return;
    int W = rc.right, H = rc.bottom; if (W < 64 || H < 64) return;
    double t = now(); float since = (float)(t - g_deployAt);
    float alpha = 1; bool powering = g_deployMoment && since >= 0 && since < HUD_powerOnSeconds;
    if (powering) { float k = since / HUD_powerOnSeconds; alpha = k < 0.6f ? ((int)(since * 18) % 3 ? 0.25f : 0.9f) * k / 0.6f : 1; }

    // our own compatibility context: only our state lives here, the game's core context is untouched
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glViewport(0, 0, W, H); glDrawBuffer(GL_BACK);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    loadCardTex(); ensureFont(hdc, max(12, (int)(H * HUD_textH)));
    float ph = H * HUD_portraitH, pw = ph * 396.f / 630.f, x = W * HUD_portraitX, y = H * HUD_portraitY, pad = ph * 0.03f;
    rect(x - pad, y - pad, pw + 2 * pad, ph + 2 * pad, HUD_frameColor, alpha);
    if (g_cardTex) {
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_cardTex); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1, 1, 1, alpha);
        glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(x, y); glTexCoord2f(1, 0); glVertex2f(x + pw, y); glTexCoord2f(1, 1); glVertex2f(x + pw, y + ph); glTexCoord2f(0, 1); glVertex2f(x, y + ph); glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    // ability bar under the portrait
    float by = y + ph + pad * 2, bh = H * HUD_barH, bw = pw + 2 * pad, bx = x - pad;
    rect(bx, by, bw, bh, HUD_frameColor, alpha);
    float act = (float)((t - g_abilityStart) / g_ab->duration), cool = (float)(1 - (g_abilityReadyAt - t) / g_ab->cooldown);
    if (act >= 0 && act < 1) rect(bx, by, bw * (1 - act), bh, HUD_activeColor, alpha);
    else if (t >= g_abilityReadyAt) rect(bx, by, bw, bh, HUD_readyColor, alpha);
    else rect(bx, by, bw * max(0.f, min(1.f, cool)), bh, HUD_chargeColor, alpha);
    // armour pips to the right of the portrait
    static const unsigned char armourCol[4] = { 120, 200, 255, 220 };
    float ax = x + pw + pad * 3, aw = pw * 0.12f;
    rect(ax, y, aw, ph, HUD_frameColor, alpha);
    float a = max(0.f, min(1.f, (float)g_armour)); rect(ax, y + ph * (1 - a), aw, ph * a, armourCol, alpha);
    // text
    static const unsigned char white[4] = { 235, 235, 235, 255 };
    char line[96];
    sprintf_s(line, "%s", g_class->name); text(x, by + bh + g_fontPx * 1.2f, line, HUD_readyColor, alpha);
    if (act >= 0 && act < 1) sprintf_s(line, "%s  ACTIVE", g_ab->name);
    else if (t >= g_abilityReadyAt) sprintf_s(line, "[%c] %s  READY", HUD_abilityKey, g_ab->name);
    else sprintf_s(line, "%s  %.0fs", g_ab->name, ceil(g_abilityReadyAt - t));
    text(x, by + bh + g_fontPx * 2.4f, line, white, alpha);
    if (powering) {
        static const unsigned char cyan[4] = { 140, 220, 255, 255 };
        text(W * 0.5f - g_fontPx * 7, H * 0.32f, "ARMOUR SYSTEMS ONLINE", cyan, alpha);
        sprintf_s(line, "%s  -  FOR THE EMPEROR", g_class->name); text(W * 0.5f - g_fontPx * 6, H * 0.32f + g_fontPx * 1.4f, line, HUD_readyColor, alpha);
    }

    if (!g_hudDrawn) { g_hudDrawn = true; L("HUD drawn %dx%d\n", W, H); check("hud-drawn", glGetError() == GL_NO_ERROR, "GL error"); }
}

static void drawInOwnContext(HDC hdc) {
    HGLRC prev = wglGetCurrentContext(); HDC prevDC = wglGetCurrentDC();
    if (!g_hudCtx && !g_hudCtxFailed) {
        g_hudCtx = wglCreateContext(hdc);
        if (!g_hudCtx) { g_hudCtxFailed = true; L("HUD context failed %lu\n", GetLastError()); return; }
        L("HUD context created (game context %p)\n", prev);
    }
    if (!g_hudCtx || !wglMakeCurrent(hdc, g_hudCtx)) return;
    drawHud(hdc);
    wglMakeCurrent(prevDC, prev);
}
static void onSwap(HDC hdc) {
    if (t_swapDepth++ == 0 && g_inGame && g_deployed) {
        __try { drawInOwnContext(hdc); }
        __except (EXCEPTION_EXECUTE_HANDLER) { static bool once; if (!once) { once = true; L("HUD exception\n"); } }
    }
}
static BOOL WINAPI hk_wglSwap(HDC h) { onSwap(h); BOOL r = o_wglSwap(h); t_swapDepth--; return r; }
static BOOL WINAPI hk_gdiSwap(HDC h) { onSwap(h); BOOL r = o_gdiSwap(h); t_swapDepth--; return r; }

// ---------------------------------------------------------------- debug commands (session.ini debug=1): cmd.txt -> hook.log
static void debugCmd(char* line) {
    char op[32] = "", a[64] = "", b[64] = ""; sscanf_s(line, "%31s %63s %63s", op, 32, a, 64, b, 64);
    L("> %s", line);
    if (!strcmp(op, "ability")) { g_abilityReadyAt = 0; uint8_t* gl = gameLocal(); if (gl && entity(gl, 0)) useAbility(entity(gl, 0), now()); }
    else if (!strcmp(op, "class")) { // class <id>: switch class live (testing)
        for (auto& c : kClasses) if (!strcmp(c.id, a)) { g_class = &c; g_ab = &kAbilities[c.ability]; }
        g_voiceCh.stop(); g_sfxCh.stop(); g_voice.clear(); g_sfx.clear(); loadAudio(g_class->id);
        if (g_cardTex) { g_cardTex = 0; } g_cardFailed = false; // reloaded on the HUD thread (old texture leaks once; debug only)
        L("class %s ability %s\n", g_class->id, g_ab->id);
    }
    else if (!strcmp(op, "killmode")) { g_killMode = atoi(a); L("killmode %d\n", g_killMode); }
    else if (!strcmp(op, "set")) { setCvar(a, (float)atof(b)); L("%s = %g\n", a, getCvar(a)); }
    else if (!strcmp(op, "cvar")) { CVar* c = cv(a); if (c) L("%s = %g (default %g)\n", a, getCvar(a), c->def); }
    else if (!strcmp(op, "ais")) {
        uint8_t* gl = gameLocal(); if (!gl) return;
        uint8_t* pl = entity(gl, 0); float* pp = pl ? playerPos(pl) : nullptr;
        for (uint8_t* e : activeAIs(gl)) {
            uint8_t* c = healthComp(e); float* ap = (float*)(e + HK_aiPos);
            float d = pp ? sqrtf((ap[0] - pp[0]) * (ap[0] - pp[0]) + (ap[1] - pp[1]) * (ap[1] - pp[1]) + (ap[2] - pp[2]) * (ap[2] - pp[2])) : -1;
            L("  %p hp %g/%g dist %.0f %.40s\n", e, c ? compHealth(c) : -1, c ? compMax(c) : -1, d, *(char**)(e + 0x20));
        }
    }
    else if (!strcmp(op, "hurt")) { // hurt <entity> <amount>
        uint8_t* e = (uint8_t*)_strtoui64(a, 0, 16); if (!isAI(e)) { L("not an AI\n"); return; }
        uint8_t* c = healthComp(e); float h = compHealth(c) - (float)atof(b);
        if (h <= 0) killAI(e, c); else setAIHealth(c, h);
        L("hp now %g\n", compHealth(c));
    }
    else if (!strcmp(op, "comp")) { // comp <entity>: health component + the code of its health getter
        uint8_t* e = (uint8_t*)_strtoui64(a, 0, 16); uint8_t* c = healthComp(e);
        uint8_t* fn = (uint8_t*)(*(void***)c)[HK_vfHealth / 8];
        L("comp %p vt img+%llx getter img+%llx hp %g\n", c, *(uint64_t*)c - (uint64_t)g_base, (uint64_t)(fn - g_base), compHealth(c));
        char hex[200] = ""; for (int i = 0; i < 48; i++) sprintf_s(hex + i * 3, 4, "%02x ", fn[i]); L("  code %s\n", hex);
        for (int o = 0; o < 0x100; o += 4) { float f = *(float*)(c + o); int v = *(int*)(c + o); if ((f > 0.5f && f < 100000) || (v > 0 && v < 100000)) L("  +%02x f=%g i=%d\n", o, f, v); }
    }
    else if (!strcmp(op, "dump")) { uint8_t* p = (uint8_t*)_strtoui64(a, 0, 16); int n = atoi(b) ? atoi(b) : 0x80; for (int o = 0; o < n; o += 8) if (readable(p + o, 8)) L("  +%03x %016llx f=%g,%g\n", o, *(uint64_t*)(p + o), *(float*)(p + o), *(float*)(p + o + 4)); }
    else if (!strcmp(op, "writef")) { *(float*)_strtoui64(a, 0, 16) = (float)atof(b); L("ok\n"); }
    else if (!strcmp(op, "post")) { // post <entity|component> <event rva> [float]: experiment with game events
        uint8_t* e = (uint8_t*)_strtoui64(a, 0, 16); uint8_t* ev = g_base + strtol(b, 0, 16);
        char c3[32] = ""; sscanf_s(line, "%*s %*s %*s %31s", c3, 32);
        if (c3[0]) ((PostEventF_t)(g_base + HK_postEvent))(e, ev, 0, (double)atof(c3)); else ((PostEventF_t)(g_base + HK_postEvent))(e, ev, 0);
        L("posted\n");
    }
    else if (!strcmp(op, "aioff")) { g_aiHealthOff = (int)strtol(a, 0, 16); g_aiHealthOk = false; L("aioff %x\n", g_aiHealthOff); }
    else if (!strcmp(op, "say")) say(a, true);
    else if (!strcmp(op, "sfx")) sfx(a);
    else L("unknown\n");
}
static void safeDebugCmd(char* l) { __try { debugCmd(l); } __except (EXCEPTION_EXECUTE_HANDLER) { L("exception\n"); } }
static void pollDebug() {
    std::string p = g_dir + "\\cmd.txt";
    FILE* c = _fsopen(p.c_str(), "r", _SH_DENYNO); if (!c) return;
    std::vector<std::string> lines; char buf[256]; while (fgets(buf, sizeof buf, c)) lines.push_back(buf); fclose(c);
    DeleteFileA(p.c_str());
    for (auto& l : lines) safeDebugCmd((char*)l.c_str());
}

// ---------------------------------------------------------------- startup
static void safeTick() { __try { tick(); } __except (EXCEPTION_EXECUTE_HANDLER) { static int n; if (n++ < 5) L("tick exception\n"); } }

static void hookSwap() {
    if (MH_Initialize() != MH_OK) { L("MinHook init failed\n"); return; }
    HMODULE gl = LoadLibraryA("opengl32.dll"), gdi = LoadLibraryA("gdi32.dll");
    void* w = GetProcAddress(gl, "wglSwapBuffers"); void* g = GetProcAddress(gdi, "SwapBuffers");
    if (w && MH_CreateHook(w, (void*)hk_wglSwap, (void**)&o_wglSwap) == MH_OK) MH_EnableHook(w);
    if (g && MH_CreateHook(g, (void*)hk_gdiSwap, (void**)&o_gdiSwap) == MH_OK) MH_EnableHook(g);
    L("swap hooks set\n");
}

static DWORD WINAPI worker(LPVOID) {
    char d[MAX_PATH]; ExpandEnvironmentStringsA("%LOCALAPPDATA%\\AstartesOldBlood", d, MAX_PATH); CreateDirectoryA(d, 0); g_dir = d;
    g_log = _fsopen((g_dir + "\\hook.log").c_str(), "w", _SH_DENYNO);
    L("Astartes: The Old Blood hook up\n");
    auto ini = readIni(g_dir + "\\session.ini");
    for (auto& c : kClasses) if (ini["class"] == c.id) g_class = &c;
    if (!g_class) { L("no session (start the game from the Astartes launcher); staying vanilla\n"); return 0; }
    g_ab = &kAbilities[g_class->ability];
    g_skipIntro = ini["skipIntro"] == "1"; g_deployMoment = ini["deployMoment"] != "0"; g_debug = ini["debug"] == "1";
    g_content = ini.count("content") ? ini["content"] : g_dir + "\\content";
    srand(GetTickCount());
    g_base = (uint8_t*)GetModuleHandleA(nullptr);
    g_isz = ((IMAGE_NT_HEADERS*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    if (g_isz != HK_imageSize) { L("unknown game build (image size %zx); staying vanilla\n", g_isz); return 0; }
    readTestFile(ini["testFile"]);
    L("class %s, ability %s, skipIntro %d, deployMoment %d\n", g_class->id, g_ab->id, g_skipIntro, g_deployMoment);
    check("hook-loaded", true);

    // cvars are constructed by the exe's startup code, which runs after this DLL loads: wait for them
    double t0 = now();
    while (!scanCvars()) { if (now() - t0 > 60) { L("cvars never appeared\n"); return 0; } Sleep(5); }
    L("cvars: %zu after %.2fs\n", g_cvars.size(), now() - t0);
    // the intro check reads this cvar early; +set can't change it (flag 0x20000), so hold it at 1 for the first seconds
    double introUntil = g_skipIntro ? now() + 25 : 0;
    while (now() < introUntil - 24) { setCvar("com_skipIntroVideo", 1); Sleep(1); }
    loadAudio(g_class->id);
    hookSwap();
    for (;;) {
        double t = now();
        if (t < introUntil) { while (now() - t < 0.016) { setCvar("com_skipIntroVideo", 1); Sleep(1); } continue; }
        safeTick();
        if (g_debug) pollDebug();
        Sleep(16);
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0, 0, worker, 0, 0, 0); }
    return TRUE;
}
