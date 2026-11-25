// A Bit Better Traversal Movement (shipped as dwmapi.dll) - smoother field movement for FF7 Rebirth: faster traversal
// animations, steering in the air and on landing, landing where you fall, landing momentum, longer dodge rolls that can
// roll off small ledges, a closer Acrobacias prompt, no running in place against walls, and a hands-on-wall pose.
// Settings: TraversalSpeed.ini next to the DLL (re-read live).
//
// Loading: the game imports dwmapi.dll, so Windows loads this DLL from the game folder at startup; it forwards every
// dwmapi call to System32\dwmapi.dll. The game exe and its files on disk are never modified.
//
// How it works: once a second a background thread walks GUObjectArray, picks the AnimSequence objects that belong to
// playable characters (/Game/Motion/Player/...) and whose names mark them as traversal moves, and sets their
// UAnimSequenceBase::RateScale from TraversalSpeed.ini. In-game tests showed the traversal lock lasts as long as the
// animation, so a faster animation also returns control sooner. The RateScale offset is looked up by name through
// the engine's reflection data. Every read of game memory is guarded with SEH.
//
// Addresses are for the same ff7rebirth_.exe build as Final Foliage Reenabled (verified at startup; otherwise idle).
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <unordered_map>
#include <string>
#include <vector>
#include <atomic>
#include <cmath>
#include <cwctype>

#include "dwmapi_proxy.inc"
extern "C" void* g_dwmapiFn[DWMAPI_EXPORTS] = {};
extern "C" void DwmapiUnresolved() {}

static void InitDwmapiProxy() {
    wchar_t path[MAX_PATH]; UINT n = GetSystemDirectoryW(path, MAX_PATH);
    HMODULE real = nullptr;
    if (n && n < MAX_PATH - 16) { wcscat_s(path, L"\\dwmapi.dll"); real = LoadLibraryW(path); }
    for (int i = 0; i < DWMAPI_EXPORTS; ++i) {
        FARPROC p = real ? GetProcAddress(real, DWMAPI_TABLE[i].name ? DWMAPI_TABLE[i].name : MAKEINTRESOURCEA(DWMAPI_TABLE[i].ordinal)) : nullptr;
        g_dwmapiFn[i] = p ? reinterpret_cast<void*>(p) : reinterpret_cast<void*>(&DwmapiUnresolved);
    }
}

// ---------------------------------------------------------------- build-specific addresses
// One entry per supported game build, picked at startup by the exe's PE TimeDateStamp + SizeOfImage. Addresses and the
// exact entry bytes of each hooked function come from tools/sigmake.py, sigport.py and sigport2.py (tools/build_1004.json).
// GUObjectArray 0 = found at run time by its structure (FindGUObjectArray).
struct GameBuild {
    const char* name; uint32_t stamp, imageSize;
    uint32_t guObjArr, nameBlocks, processEvent, parkourTry, parkourUpdate, ledgeDetect, teleport,
             autoTrav, autoTravNo, autoTravYes, actRequest, promptReach;
    uint8_t pe[17], tryP[18], pu[24], ld[17], tp[14], at[21], pr[9];
};
static const GameBuild GAME_BUILDS[] = {
    { "1.0.0.5", 0x6A16CED2, 0x099D9000,
      0x871EB28, 0x90D3C10, 0x19981F0, 0x345FCC4, 0x1301444, 0x13036A4, 0x1940E14,
      0x1301F93, 0x1301FD8, 0x13028AD, 0x1511E94, 0x12FF4F4,
      { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0xF0, 0x00, 0x00, 0x00 },
      { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x08, 0x06, 0x00, 0x00 },
      { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x40, 0x03, 0x00, 0x00 },
      { 0x40, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x20, 0x07, 0x00, 0x00 },
      { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x2C, 0x02, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x18 },
      { 0x48, 0x8D, 0x94, 0x24, 0x10, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0xF1, 0xFE, 0x20, 0x00, 0xE9, 0x05, 0x09, 0x00, 0x00 },
      { 0xC5, 0xFA, 0x10, 0x8C, 0x24, 0x68, 0x03, 0x00, 0x00 } },
    { "1.0.0.4", 0x68FD6FDE, 0x09BAD000,
      0, 0x929A790, 0x8C03A4, 0xA02F24, 0xEECE8C, 0x10506C0, 0x3722108,
      0xEED88E, 0xEED8D3, 0xEEE1B4, 0x1291D98, 0xEF01E0,
      { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0xF0, 0x00, 0x00, 0x00 },
      { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x48, 0x06, 0x00, 0x00 },
      { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x10, 0x03, 0x00, 0x00 },
      { 0x40, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x20, 0x07, 0x00, 0x00 },
      { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x26, 0x02, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x18 },
      { 0x48, 0x8D, 0x94, 0x24, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0xFA, 0x44, 0x3A, 0x00, 0xE9, 0x11, 0x09, 0x00, 0x00 },
      { 0xC5, 0xFA, 0x10, 0x8C, 0x24, 0x68, 0x03, 0x00, 0x00 } },
};
static const GameBuild* g_build = nullptr;
#define RVA_NameBlocks     (g_build->nameBlocks)
#define RVA_ProcessEvent   (g_build->processEvent)
#define RVA_ParkourTry     (g_build->parkourTry)
#define RVA_ParkourUpdate  (g_build->parkourUpdate)
#define RVA_LedgeDetect    (g_build->ledgeDetect)
#define RVA_TeleportReason (g_build->teleport)
#define RVA_AutoTrav       (g_build->autoTrav)
#define RVA_AutoTravNo     (g_build->autoTravNo)
#define RVA_AutoTravYes    (g_build->autoTravYes)
#define RVA_ActRequest     (g_build->actRequest)
#define RVA_PromptReach    (g_build->promptReach)
#define PE_PROLOGUE        (g_build->pe)
#define TRY_PROLOGUE       (g_build->tryP)
#define PU_PROLOGUE        (g_build->pu)
#define LD_PROLOGUE        (g_build->ld)
#define TP_PROLOGUE        (g_build->tp)
#define AT_BYTES           (g_build->at)
#define PR_ORIG            (g_build->pr)
static std::atomic<uintptr_t> g_guObjArr{0};   // absolute address of GUObjectArray (FUObjectArray) once known

// ---------------------------------------------------------------- engine layout (UE 4.26)
static constexpr int OFF_UObject_Class = 0x10, OFF_UObject_Name = 0x18, OFF_UObject_Outer = 0x20;
static constexpr int OFF_UStruct_Super = 0x40, OFF_UStruct_ChildProps = 0x50;
static constexpr int OFF_FField_Next = 0x20, OFF_FField_Name = 0x28, OFF_FProperty_Offset = 0x4C;

static uintptr_t g_base = 0;
static HMODULE g_module = nullptr;
static FILE* g_log = nullptr;

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

template <class T> static bool Rd(uintptr_t addr, T& out) {
    __try { out = *reinterpret_cast<const T*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool RdBytes(uintptr_t addr, void* out, size_t n) {
    __try { memcpy(out, reinterpret_cast<const void*>(addr), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool WrFloat(uintptr_t addr, float v) {
    __try { *reinterpret_cast<float*>(addr) = v; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------- names
static std::unordered_map<uint32_t, std::string> g_names;

static const std::string& NameStr(uint32_t idx) {
    auto it = g_names.find(idx);
    if (it != g_names.end()) return it->second;
    std::string s;
    uintptr_t block = 0;
    if (Rd(g_base + RVA_NameBlocks + 8 * (idx >> 16), block) && block) {
        uintptr_t e = block + 4 * (idx & 0xFFFF) + 4;
        uint16_t h = 0;
        if (Rd(e, h)) {
            size_t len = h >> 6; bool wide = h & 1;
            if (len && len < 512) {
                if (wide) { for (size_t i = 0; i < len; ++i) { uint16_t c = 0; Rd(e + 2 + 2 * i, c); s += c < 128 ? (char)c : '?'; } }
                else { s.resize(len); if (!RdBytes(e + 2, &s[0], len)) s.clear(); }
            }
        }
    }
    return g_names.emplace(idx, s).first->second;
}

static std::string ObjName(uintptr_t obj) {
    uint32_t idx = 0; int32_t num = 0;
    if (!Rd(obj + OFF_UObject_Name, idx) || !Rd(obj + OFF_UObject_Name + 4, num)) return std::string();
    std::string s = NameStr(idx);
    if (num > 0) s += "_" + std::to_string(num - 1);
    return s;
}

// ---------------------------------------------------------------- settings (TraversalSpeed.ini, reread when saved)
// G_KEEP: [speed] normal_speed, always 1.  G_CUSTOM + i: entry i of [speed] custom (its own rate)
enum Group { G_NONE = -1, G_UP, G_DOWN, G_CLIFF, G_LAND, G_CLIMB, G_LADDER, G_ROPE, G_SQUEEZE, G_START, G_STOP, G_PIVOT,
             G_COUNT, G_KEEP = G_COUNT, G_CUSTOM };
static std::string g_keepNormal = "N_Jump03,N_Jump04";   // comma-separated name prefixes kept at the normal speed
static const wchar_t* GROUP_KEY[G_COUNT] = { L"up", L"down", L"cliff_stop", L"land", L"climb_wall", L"ladder", L"rope", L"squeeze",
                                             L"run_start", L"run_stop", L"turn" };
static const wchar_t* GROUP_DEF[G_COUNT] = { L"1.1", L"1.35", L"1.35", L"1.15", L"1.25", L"1.25", L"1.2", L"1.3", L"1.2", L"1.25", L"1.25" };
static const char* GROUP_LOG[G_COUNT] = { "up", "down", "cliff stop", "land", "climb wall", "ladder", "rope", "squeeze", "run start", "run stop", "turn" };
static float g_speed[G_COUNT] = { 1.1f, 1.35f, 1.35f, 1.15f, 1.25f, 1.25f, 1.2f, 1.3f, 1.2f, 1.25f, 1.25f };
struct CustomRate { std::string prefix; float rate; };
static std::vector<CustomRate> g_custom;   // [speed] custom=Prefix:rate,Prefix:rate (any animation family, overrides the groups)
static bool g_enabled = true;
static FILETIME g_iniTime = {};
static unsigned g_iniGen = 0;

static void IniPath(wchar_t* out, const wchar_t* file) {
    GetModuleFileNameW(g_module, out, MAX_PATH);
    wchar_t* slash = wcsrchr(out, L'\\'); if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - out), file);
}

static const char* DEFAULT_INI =
    "; Traversal Speed - how fast traversal moves play. 1 = the game's normal speed.\r\n"
    "; Faster moves also give you control back sooner. Save this file and the change applies within a second.\r\n"
    "\r\n"
    "[global]\r\n"
    "; 0 turns the mod off (every move back to normal speed)\r\n"
    "enabled=1\r\n"
    "\r\n"
    "[speed]\r\n"
    "; going up: vaulting onto ledges and climbing up (above ~1.2 the hands stop matching the ledge)\r\n"
    "up=1.1\r\n"
    "; going down: jumping off and falling\r\n"
    "down=1.35\r\n"
    "; touching down: landings and landing rolls (slower than down, so a landing settles before the next move)\r\n"
    "land=1.15\r\n"
    "; stopping at a cliff edge\r\n"
    "cliff_stop=1.35\r\n"
    "\r\n";

static const char* STEER_INI =
    "[steer]\r\n"
    "; steer drops and landing rolls toward where you push the stick (or WASD), like normal walking\r\n"
    "enabled=1\r\n"
    "; steer while in the air after dropping off a ledge\r\n"
    "air=1\r\n"
    "; steer the landing (roll) for landing_window seconds after touching down\r\n"
    "landing=1\r\n"
    "landing_window=1.2\r\n"
    "; how fast the character turns toward the stick, in degrees per second\r\n"
    "turn_rate=180\r\n"
    "; keyboard keys (letters or numbers) for forward, left, back, right\r\n"
    "keys=WASD\r\n";

static const char* MOVE_INI =
    "\r\n"
    "[roll]\r\n"
    "; dodge roll outside the traversal moves: travel this many times further (1 = normal)\r\n"
    "distance=1.3\r\n"
    "; 1 = a roll toward an edge keeps going and falls off it (with its speed) instead of sliding along the edge\r\n"
    "off_ledges=1\r\n"
    "; longest stretch per roll, in seconds (the roll itself is detected from the game, keyboard or controller)\r\n"
    "time=0.9\r\n"
    "\r\n"
    "[momentum]\r\n"
    "; keep moving after landing from a drop instead of stopping on the spot: share of the air speed kept (0 = off)\r\n"
    "landing=0.5\r\n"
    "; how long that carry lasts, in seconds (it fades out)\r\n"
    "landing_time=0.35\r\n"
    "; 1 = jump-downs keep their speed through the air and land where the jump takes you (steer it with the stick);\r\n"
    ";     0 = the game brakes you over its chosen landing spot and slides you onto it\r\n"
    "free_fall=1\r\n";

static const char* TRAV_INI =
    "\r\n"
    "[traversal]\r\n"
    "; 0 = a dodge roll can't turn into a climb or jump-down until it has finished (no vaulting up walls mid-roll);\r\n"
    ";     standing next to a ledge, the button still climbs as usual\r\n"
    "; 1 = the game's normal behaviour\r\n"
    "roll_vault=0\r\n"
    "; how long (seconds from the start of a roll) traversal stays blocked; the slow end of the roll is free\r\n"
    "roll_block=0.45\r\n"
    "; seconds after a climb or jump-down ends during which the next one can't start, so close ledges don't cut\r\n"
    "; the landing short (0 = off). The dodge button is also ignored during this short moment.\r\n"
    "cooldown=0.2\r\n";

struct SteerCfg {
    bool enabled = true, air = true, landing = true; float window = 1.2f, rate = 180.f; char keys[4] = { 'W', 'A', 'S', 'D' };
    float rollDist = 1.3f, rollTime = 0.9f; int rollKey = VK_SPACE; float landKeep = 0.5f, landTime = 0.35f;
    bool rollVault = false, offLedges = true, freeFall = true; float cooldown = 0.2f, rollBlock = 0.45f, maxDrop = 450.f, coolMinH = 150.f, promptReach = 1.f; bool wallStop = true, wallHand = true, wallHandFlip = false; float wallHandTime = 0.3f, wallHandRate = 0.f, wallHandBlend = 0.25f, wallHandMinH = 200.f, wallHandPromptReach = 1.f;
    float blend = 1.f, blendMin = 0.1f, locoBlend = 1.f;   // [transitions]
    float sideAngle = 0.f;                                  // [traversal] side_angle
    float rollVaultStart = 0.f;                             // [traversal] roll_vault_start
    bool landCancel = false;                                // [transitions] landing_cancel
    float landCancelMove = 0.15f;                           // [transitions] landing_cancel_move
    uint32_t landCancelFlags = 0x21EF;                      // [transitions] landing_cancel_flags (hex, hidden)
    float rollDistStand = 1.6f, rollMaxRise = 60.f;         // [roll] distance_standing, max_rise (cm)
};
static SteerCfg g_steer;
struct HandAnim { uintptr_t seq; char pc[8]; bool right; };          // wall-hand animation per character folder (PC0000, PC0009, ...)
static HandAnim g_handAnims[8]; static volatile LONG g_handN = 0;
static std::string g_handAnimName = "N_Door21_Open_Loop";   // [traversal] wall_hand_anim
static uintptr_t g_charRoots[2][256]; static std::atomic<int> g_charFront{0}; static std::atomic<int> g_charCount[2];   // other characters' root components

static int KeyCode(const wchar_t* s) {
    if (!_wcsicmp(s, L"SPACE")) return VK_SPACE;
    if (!_wcsicmp(s, L"SHIFT")) return VK_SHIFT;
    if (!_wcsicmp(s, L"CTRL") || !_wcsicmp(s, L"CONTROL")) return VK_CONTROL;
    if (!_wcsicmp(s, L"ALT")) return VK_MENU;
    if (wcslen(s) == 1) return towupper(s[0]);
    return VK_SPACE;
}

// the ini written when none exists (same as the one shipped in the release)
static const char* RELEASE_INI =
    "; A Bit Better Traversal Movement - settings\r\n"
    "; Save this file while the game is running and the change applies within a second (no restart needed).\r\n"
    "; 1 always means \"the game's normal behaviour\" for speeds and multipliers.\r\n"
    "\r\n"
    "[global]\r\n"
    "; 0 turns the whole mod off\r\n"
    "enabled=1\r\n"
    "\r\n"
    "[speed]\r\n"
    "; how fast traversal animations play (faster also gives you control back sooner)\r\n"
    "; going up: vaulting onto ledges and climbing up (above ~1.2 the hands stop matching the ledge)\r\n"
    "up=1.16\r\n"
    "; going down: jumping down from ledges and falling\r\n"
    "down=1.42\r\n"
    "; stopping at a cliff edge\r\n"
    "cliff_stop=1.42\r\n"
    "; landing animations after a drop\r\n"
    "land=1\r\n"
    "; climbing walls with handholds: getting on and off, moving and jumping between holds\r\n"
    "climb_wall=1.25\r\n"
    "; ladders: getting on, climbing up and down, getting off\r\n"
    "ladder=1.25\r\n"
    "; ropes, poles, zip lines (getting on and off) and the grappling hook; rope swings and the zip line ride stay normal\r\n"
    "rope=1.2\r\n"
    "; squeezing through narrow gaps and crawl spaces\r\n"
    "squeeze=1.3\r\n"
    "; starting to run or sprint (including the turn when you start in another direction)\r\n"
    "run_start=1.2\r\n"
    "; stopping from a run or sprint, and slowing from a sprint to a walk\r\n"
    "run_stop=1.25\r\n"
    "; turning around while running, and turning on the spot\r\n"
    "turn=1.25\r\n"
    "; animations that always play at normal speed (name prefixes, comma-separated): the small automatic hop up or down a\r\n"
    "; step while walking looks frozen for a moment when sped up\r\n"
    "normal_speed=N_Jump03,N_Jump04\r\n"
    "; your own speeds for any animation family: name prefix : speed, comma-separated, e.g. N_Narrow01:1.5,N_LadderU:1.4\r\n"
    "; (overrides the groups above; normal_speed still wins)\r\n"
    "custom=\r\n"
    "\r\n"
    "[steer]\r\n"
    "; steer drops and landing rolls toward where you push the stick (or WASD), like normal walking\r\n"
    "enabled=1\r\n"
    "; steer while in the air after dropping off a ledge\r\n"
    "air=1\r\n"
    "; steer the landing for landing_window seconds after touching down\r\n"
    "landing=1\r\n"
    "landing_window=1.2\r\n"
    "; how fast the character turns toward the stick, in degrees per second\r\n"
    "turn_rate=189\r\n"
    "; keyboard keys for forward, left, back, right (letters or numbers)\r\n"
    "keys=WASD\r\n"
    "\r\n"
    "[roll]\r\n"
    "; dodge roll outside combat: travel this many times further (1 = normal)\r\n"
    "distance=1.37\r\n"
    "; how long the roll is stretched, in seconds\r\n"
    "time=0.9\r\n"
    "; 1 = rolling at an edge carries you over it instead of stopping (only when the drop is safe, see max_drop)\r\n"
    "off_ledges=1\r\n"
    "; a roll only goes off an edge when there's walkable ground within this many cm below; bigger drops (big cliffs,\r\n"
    "; out-of-bounds edges) stop the roll at the edge like the normal game\r\n"
    "max_drop=450\r\n"
    "; distance (like the setting above) for a roll started from standing still, where the game's own roll is shorter\r\n"
    "distance_standing=1.6\r\n"
    "; rolling uphill keeps its stretch until it has climbed this many cm (higher = it went up onto something)\r\n"
    "max_rise=60\r\n"
    "\r\n"
    "[momentum]\r\n"
    "; keep moving after landing from a drop instead of stopping on the spot: share of the air speed kept (0 = off)\r\n"
    "landing=0.53\r\n"
    "; how long that carry lasts, in seconds (it fades out)\r\n"
    "landing_time=0.35\r\n"
    "; 1 = land where your fall actually takes you instead of being snapped onto the game's planned landing spot\r\n"
    ";     (the game's placement is kept when you'd land on a different level, e.g. a platform you're meant to reach)\r\n"
    "free_fall=1\r\n"
    "\r\n"
    "[traversal]\r\n"
    "; 0 = a dodge roll can't turn into a climb or jump-down while it's still going (no vaulting up walls mid-roll);\r\n"
    ";     standing next to a ledge, the button still climbs as usual.  1 = the game's normal behaviour\r\n"
    "roll_vault=0\r\n"
    "; how long (seconds) from the start of a roll traversals stay blocked\r\n"
    "roll_block=0.45\r\n"
    "; ...except right at the start: a roll can still turn into a climb or jump-down during its first this-many seconds\r\n"
    ";    (pressing the button close to a ledge), but not when it reaches a wall later (0 = blocked from the start)\r\n"
    "roll_vault_start=0.15\r\n"
    "; short pause (seconds) before another AUTOMATIC traversal can start, so back-to-back ledges don't cut each other's\r\n"
    "; animations. The button (Acrobacias prompt, roll) is never held back by it.\r\n"
    "cooldown=0.13\r\n"
    "; only a traversal that changes height by at least this many cm starts that pause (small step-downs chain freely)\r\n"
    "cooldown_min_height=150\r\n"
    "; how far ahead the game looks for a ledge to show the Acrobacias prompt and let the button start a traversal:\r\n"
    "; 1 = normal, smaller = you have to be closer to the edge\r\n"
    "prompt_reach=0.25\r\n"
    "; 1 = stop the running animation when you push into a wall, instead of running in place\r\n"
    ";     (only walls taller than about half the character; low obstacles, props and other characters don't stop you)\r\n"
    "wall_stop=1\r\n"
    "; hands on the wall while stopped against a tall wall (the character turns to face it squarely)\r\n"
    "wall_hand=1\r\n"
    "; which animation to use for that pose (one the game has loaded), e.g. N_Door21_Open_Loop (two-hand push, 2.0 s),\r\n"
    "; N_Door01_0_Loop (13.3 s), N_Door10_L0 / N_Door10_R0 / N_Door11_L0 / N_Door11_R0 (4.3 s)\r\n"
    "wall_hand_anim=N_Door21_Open_Loop\r\n"
    "; the wall must reach at least this many cm above the feet for the hands pose (Cloud is about 190 cm tall)\r\n"
    "wall_hand_min_height=120\r\n"
    "; seconds into that animation to hold (stay within its length above)\r\n"
    "wall_hand_time=0.8\r\n"
    "; 0 = hold that pose; above 0 = play the animation at this speed instead\r\n"
    "wall_hand_rate=0\r\n"
    "; prompt_reach used while the hands-on-wall pose shows, so a climbable wall offers its prompt from there\r\n"
    "; (1 = the game's normal distance)\r\n"
    "wall_hand_prompt_reach=1\r\n"
    "; also look for a ledge this many degrees to either side of where the character faces, when there's none straight\r\n"
    "; ahead (0 = only straight ahead, like the normal game)\r\n"
    "side_angle=35\r\n"
    "\r\n"
    "[camera]\r\n"
    "; 1 = the camera follows the character down while falling (e.g. after rolling off a ledge) instead of catching up\r\n"
    ";     only after landing. Read once at game start: restart the game after changing these two.\r\n"
    "fall_follow=1\r\n"
    "; how quickly the camera follows height changes (the game's own value is 5)\r\n"
    "fall_follow_speed=40\r\n"
    "\r\n"
    "[transitions]\r\n"
    "; 1 = get control back at the end of a traversal instead of waiting for its animation to finish:\r\n"
    ";     while landing from a drop (feet on the ground), the dodge / Acrobacias button rolls or starts the next traversal\r\n"
    ";     right away; at the end of a vault or climb (feet on the floor), the button or pushing the stick ends it at once\r\n"
    "landing_cancel=1\r\n"
    "; landings after a drop: also let the stick end them, this many seconds in (0 = only the button ends a landing,\r\n"
    "; so the landing animation still plays while you run on)\r\n"
    "landing_cancel_move=0\r\n"
    "; how long the game blends from one animation into the next, as a share of its own blend time (1 = normal,\r\n"
    "; smaller = snappier). blend: in and out of actions (vaults, drops, landings, climbs, rolls) and running\r\n"
    "blend=1\r\n"
    "; blends between the running animations themselves (start, stop, turn, walk to run)\r\n"
    "locomotion_blend=1\r\n"
    "; a blend is never made shorter than this many seconds (unless the game's own blend is already shorter)\r\n"
    "blend_min=0.1\r\n";

static bool LoadIni() {
    wchar_t path[MAX_PATH]; IniPath(path, L"TraversalSpeed.ini");
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) {
        FILE* f = _wfopen(path, L"wb");
        if (f) { fputs(RELEASE_INI, f); fclose(f); }   // a fresh ini = the release defaults
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return false;
    } else {
        wchar_t probe[8];   // ini from v0.1: add the [steer] section once
        if (!GetPrivateProfileStringW(L"steer", L"enabled", L"", probe, 8, path)) {
            FILE* f = _wfopen(path, L"ab");
            if (f) { fputs("\r\n", f); fputs(STEER_INI, f); fclose(f); }
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (!GetPrivateProfileStringW(L"speed", L"land", L"", probe, 8, path)) {   // ini from v0.1/v0.2: add land=
            WritePrivateProfileStringW(L"speed", L"land", L"1.15", path);
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (!GetPrivateProfileStringW(L"roll", L"distance", L"", probe, 8, path)) {   // ini before v0.4: add [roll] and [momentum]
            FILE* f = _wfopen(path, L"ab");
            if (f) { fputs(MOVE_INI, f); fclose(f); }
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (GetPrivateProfileStringW(L"traversal", L"roll_vault", L"", probe, 8, path) &&
            !GetPrivateProfileStringW(L"traversal", L"cooldown", L"", probe, 8, path)) {   // ini before the cooldown
            WritePrivateProfileStringW(L"traversal", L"cooldown", L"0.2", path);
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (GetPrivateProfileStringW(L"traversal", L"roll_vault", L"", probe, 8, path) &&
            !GetPrivateProfileStringW(L"traversal", L"roll_block", L"", probe, 8, path)) {   // ini before roll_block
            WritePrivateProfileStringW(L"traversal", L"roll_block", L"0.45", path);
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (GetPrivateProfileStringW(L"momentum", L"landing", L"", probe, 8, path) &&
            !GetPrivateProfileStringW(L"momentum", L"free_fall", L"", probe, 8, path)) {   // ini before free_fall
            WritePrivateProfileStringW(L"momentum", L"free_fall", L"1", path);
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (!GetPrivateProfileStringW(L"roll", L"off_ledges", L"", probe, 8, path)) {   // ini before v0.6 edge rolls
            WritePrivateProfileStringW(L"roll", L"off_ledges", L"1", path);
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
        if (!GetPrivateProfileStringW(L"traversal", L"roll_vault", L"", probe, 8, path)) {   // ini before v0.6
            // v0.5's keys never had an effect and early v0.6 used space_vault: drop the old section, add the new one
            if (GetPrivateProfileStringW(L"traversal", L"trigger_angle", L"", probe, 8, path) ||
                GetPrivateProfileStringW(L"traversal", L"space_vault", L"", probe, 8, path))
                WritePrivateProfileStringW(L"traversal", nullptr, nullptr, path);
            FILE* f = _wfopen(path, L"ab");
            if (f) { fputs(TRAV_INI, f); fclose(f); }
            GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
        }
    }
    if (CompareFileTime(&fa.ftLastWriteTime, &g_iniTime) == 0 && g_iniGen) return false;
    g_iniTime = fa.ftLastWriteTime;
    wchar_t v[32];
    g_enabled = GetPrivateProfileIntW(L"global", L"enabled", 1, path) != 0;
    for (int g = 0; g < G_COUNT; ++g) {
        if (g >= G_CLIMB && !GetPrivateProfileStringW(L"speed", GROUP_KEY[g], L"", v, 32, path))   // ini before 1.1: add the new groups
            WritePrivateProfileStringW(L"speed", GROUP_KEY[g], GROUP_DEF[g], path);
        GetPrivateProfileStringW(L"speed", GROUP_KEY[g], GROUP_DEF[g], v, 32, path);
        float s = static_cast<float>(_wtof(v));
        g_speed[g] = (s >= 0.25f && s <= 4.f) ? s : 1.f;
    }
    {   // small automatic hops (up or down a step while walking) end their air phase too early when sped up: keep them normal
        wchar_t kn[256] = {};
        if (!GetPrivateProfileStringW(L"speed", L"normal_speed", L"\x01", kn, 256, path) || kn[0] == 1) {
            WritePrivateProfileStringW(L"speed", L"normal_speed", L"N_Jump03,N_Jump04", path);
            wcscpy(kn, L"N_Jump03,N_Jump04");
        }
        std::string k; for (wchar_t* c = kn; *c; ++c) if (*c != L' ') k += (char)*c;
        g_keepNormal = k;
    }
    {   // [speed] custom=N_Narrow01:1.5,N_RopeSwing:1  (name prefix : speed, comma-separated)
        wchar_t cu[512] = {};
        if (!GetPrivateProfileStringW(L"speed", L"custom", L"\x01", cu, 512, path) || cu[0] == 1) {
            WritePrivateProfileStringW(L"speed", L"custom", L"", path); cu[0] = 0;
        }
        std::string s; for (wchar_t* c = cu; *c; ++c) if (*c != L' ') s += (char)*c;
        std::vector<CustomRate> list;
        for (size_t a = 0; a < s.size() && list.size() < 64; ) {
            size_t b = s.find(',', a); if (b == std::string::npos) b = s.size();
            std::string item = s.substr(a, b - a); size_t colon = item.find(':');
            if (colon != std::string::npos && colon > 0) {
                float r = (float)atof(item.c_str() + colon + 1);
                if (r >= 0.25f && r <= 4.f) list.push_back({ item.substr(0, colon), r });
            }
            a = b + 1;
        }
        g_custom.swap(list);
    }
    SteerCfg sc;
    sc.enabled = GetPrivateProfileIntW(L"steer", L"enabled", 1, path) != 0;
    sc.air = GetPrivateProfileIntW(L"steer", L"air", 1, path) != 0;
    sc.landing = GetPrivateProfileIntW(L"steer", L"landing", 1, path) != 0;
    GetPrivateProfileStringW(L"steer", L"landing_window", L"1.2", v, 32, path); sc.window = (float)_wtof(v);
    GetPrivateProfileStringW(L"steer", L"turn_rate", L"180", v, 32, path); sc.rate = (float)_wtof(v);
    if (sc.window < 0.f) sc.window = 0.f; if (sc.window > 5.f) sc.window = 5.f;
    if (sc.rate < 0.f) sc.rate = 0.f; if (sc.rate > 2000.f) sc.rate = 2000.f;
    GetPrivateProfileStringW(L"steer", L"keys", L"WASD", v, 32, path);
    if (wcslen(v) == 4) for (int i = 0; i < 4; ++i) sc.keys[i] = (char)towupper(v[i]);
    GetPrivateProfileStringW(L"roll", L"distance", L"1.3", v, 32, path); sc.rollDist = (float)_wtof(v);
    GetPrivateProfileStringW(L"roll", L"time", L"0.9", v, 32, path); sc.rollTime = (float)_wtof(v);
    GetPrivateProfileStringW(L"roll", L"key", L"SPACE", v, 32, path); sc.rollKey = KeyCode(v);
    GetPrivateProfileStringW(L"momentum", L"landing", L"0.5", v, 32, path); sc.landKeep = (float)_wtof(v);
    GetPrivateProfileStringW(L"momentum", L"landing_time", L"0.35", v, 32, path); sc.landTime = (float)_wtof(v);
    if (!(sc.rollDist >= 0.5f && sc.rollDist <= 3.f)) sc.rollDist = 1.f;
    if (!(sc.rollTime >= 0.f && sc.rollTime <= 3.f)) sc.rollTime = 0.9f;
    if (!(sc.landKeep >= 0.f && sc.landKeep <= 1.5f)) sc.landKeep = 0.f;
    if (!(sc.landTime >= 0.f && sc.landTime <= 2.f)) sc.landTime = 0.35f;
    sc.rollVault = GetPrivateProfileIntW(L"traversal", L"roll_vault", 0, path) != 0;
    sc.offLedges = GetPrivateProfileIntW(L"roll", L"off_ledges", 1, path) != 0;
    if (!GetPrivateProfileStringW(L"roll", L"max_drop", L"", v, 32, path)) WritePrivateProfileStringW(L"roll", L"max_drop", L"450", path);
    GetPrivateProfileStringW(L"roll", L"max_drop", L"450", v, 32, path); sc.maxDrop = (float)_wtof(v);
    if (!(sc.maxDrop >= 0.f && sc.maxDrop <= 5000.f)) sc.maxDrop = 450.f;
    sc.freeFall = GetPrivateProfileIntW(L"momentum", L"free_fall", 1, path) != 0;
    GetPrivateProfileStringW(L"traversal", L"cooldown", L"0.2", v, 32, path); sc.cooldown = (float)_wtof(v);
    if (!(sc.cooldown >= 0.f && sc.cooldown <= 2.f)) sc.cooldown = 0.2f;
    if (!GetPrivateProfileStringW(L"traversal", L"cooldown_min_height", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"cooldown_min_height", L"150", path);
    GetPrivateProfileStringW(L"traversal", L"cooldown_min_height", L"150", v, 32, path); sc.coolMinH = (float)_wtof(v);
    if (!(sc.coolMinH >= 0.f && sc.coolMinH <= 5000.f)) sc.coolMinH = 150.f;
    if (!GetPrivateProfileStringW(L"traversal", L"prompt_reach", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"prompt_reach", L"1", path);
    GetPrivateProfileStringW(L"traversal", L"prompt_reach", L"1", v, 32, path); sc.promptReach = (float)_wtof(v);
    if (!(sc.promptReach >= 0.f && sc.promptReach <= 4.f)) sc.promptReach = 1.f;
    if (!GetPrivateProfileStringW(L"traversal", L"wall_stop", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"wall_stop", L"1", path);
    sc.wallStop = GetPrivateProfileIntW(L"traversal", L"wall_stop", 1, path) != 0;
    if (!GetPrivateProfileStringW(L"traversal", L"wall_hand", L"", v, 32, path)) {
        WritePrivateProfileStringW(L"traversal", L"wall_hand", L"1", path);
        WritePrivateProfileStringW(L"traversal", L"wall_hand_time", L"0.3", path);
        WritePrivateProfileStringW(L"traversal", L"wall_hand_rate", L"0", path);
        WritePrivateProfileStringW(L"traversal", L"wall_hand_flip", L"0", path);
    }
    sc.wallHand = GetPrivateProfileIntW(L"traversal", L"wall_hand", 1, path) != 0;
    {
        wchar_t an[96] = {};
        if (!GetPrivateProfileStringW(L"traversal", L"wall_hand_anim", L"", an, 96, path)) {
            WritePrivateProfileStringW(L"traversal", L"wall_hand_anim", L"N_Door21_Open_Loop", path); wcscpy(an, L"N_Door21_Open_Loop");
        }
        std::string nm; for (wchar_t* c = an; *c; ++c) if (*c != L' ') nm += (char)*c;
        if (nm != g_handAnimName) { g_handN = 0; g_handAnimName = nm; }
    }
    sc.wallHandFlip = GetPrivateProfileIntW(L"traversal", L"wall_hand_flip", 0, path) != 0;
    GetPrivateProfileStringW(L"traversal", L"wall_hand_time", L"0.3", v, 32, path); sc.wallHandTime = (float)_wtof(v);
    GetPrivateProfileStringW(L"traversal", L"wall_hand_rate", L"0", v, 32, path); sc.wallHandRate = (float)_wtof(v);
    if (!(sc.wallHandTime >= 0.f && sc.wallHandTime <= 5.f)) sc.wallHandTime = 0.3f;
    if (!(sc.wallHandRate >= 0.f && sc.wallHandRate <= 3.f)) sc.wallHandRate = 0.f;
    if (!GetPrivateProfileStringW(L"traversal", L"wall_hand_min_height", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"wall_hand_min_height", L"200", path);
    GetPrivateProfileStringW(L"traversal", L"wall_hand_min_height", L"200", v, 32, path); sc.wallHandMinH = (float)_wtof(v);
    if (!(sc.wallHandMinH >= 50.f && sc.wallHandMinH <= 600.f)) sc.wallHandMinH = 200.f;
    if (!GetPrivateProfileStringW(L"traversal", L"wall_hand_prompt_reach", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"wall_hand_prompt_reach", L"1", path);
    GetPrivateProfileStringW(L"traversal", L"wall_hand_prompt_reach", L"1", v, 32, path); sc.wallHandPromptReach = (float)_wtof(v);
    if (!(sc.wallHandPromptReach >= 0.f && sc.wallHandPromptReach <= 4.f)) sc.wallHandPromptReach = 1.f;
    GetPrivateProfileStringW(L"traversal", L"roll_block", L"0.45", v, 32, path); sc.rollBlock = (float)_wtof(v);
    if (!(sc.rollBlock >= 0.f && sc.rollBlock <= 2.f)) sc.rollBlock = 0.45f;
    if (!GetPrivateProfileStringW(L"transitions", L"blend", L"", v, 32, path)) {   // ini before the blend settings
        WritePrivateProfileStringW(L"transitions", L"blend", L"1", path);
        WritePrivateProfileStringW(L"transitions", L"locomotion_blend", L"1", path);
        WritePrivateProfileStringW(L"transitions", L"blend_min", L"0.1", path);
    }
    GetPrivateProfileStringW(L"transitions", L"blend", L"1", v, 32, path); sc.blend = (float)_wtof(v);
    GetPrivateProfileStringW(L"transitions", L"locomotion_blend", L"1", v, 32, path); sc.locoBlend = (float)_wtof(v);
    GetPrivateProfileStringW(L"transitions", L"blend_min", L"0.1", v, 32, path); sc.blendMin = (float)_wtof(v);
    if (!(sc.blend >= 0.f && sc.blend <= 3.f)) sc.blend = 1.f;
    if (!(sc.locoBlend >= 0.f && sc.locoBlend <= 3.f)) sc.locoBlend = 1.f;
    if (!(sc.blendMin >= 0.f && sc.blendMin <= 1.f)) sc.blendMin = 0.1f;
    if (!GetPrivateProfileStringW(L"traversal", L"side_angle", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"side_angle", L"35", path);
    GetPrivateProfileStringW(L"traversal", L"side_angle", L"0", v, 32, path); sc.sideAngle = (float)_wtof(v);
    if (!(sc.sideAngle >= 0.f && sc.sideAngle <= 90.f)) sc.sideAngle = 0.f;
    if (!GetPrivateProfileStringW(L"traversal", L"roll_vault_start", L"", v, 32, path)) WritePrivateProfileStringW(L"traversal", L"roll_vault_start", L"0.15", path);
    GetPrivateProfileStringW(L"traversal", L"roll_vault_start", L"0", v, 32, path); sc.rollVaultStart = (float)_wtof(v);
    if (!(sc.rollVaultStart >= 0.f && sc.rollVaultStart <= 2.f)) sc.rollVaultStart = 0.f;
    if (!GetPrivateProfileStringW(L"transitions", L"landing_cancel", L"", v, 32, path)) { WritePrivateProfileStringW(L"transitions", L"landing_cancel", L"1", path); WritePrivateProfileStringW(L"transitions", L"landing_cancel_move", L"0", path); }
    sc.landCancel = GetPrivateProfileIntW(L"transitions", L"landing_cancel", 1, path) != 0;
    GetPrivateProfileStringW(L"transitions", L"landing_cancel_move", L"0", v, 32, path); sc.landCancelMove = (float)_wtof(v);
    if (!(sc.landCancelMove >= 0.f && sc.landCancelMove <= 2.f)) sc.landCancelMove = 0.f;
    GetPrivateProfileStringW(L"transitions", L"landing_cancel_flags", L"21EF", v, 32, path); sc.landCancelFlags = (uint32_t)wcstoul(v, nullptr, 16);
    if (!GetPrivateProfileStringW(L"roll", L"distance_standing", L"", v, 32, path)) { WritePrivateProfileStringW(L"roll", L"distance_standing", L"1.6", path); WritePrivateProfileStringW(L"roll", L"max_rise", L"60", path); }
    GetPrivateProfileStringW(L"roll", L"distance_standing", L"1.6", v, 32, path); sc.rollDistStand = (float)_wtof(v);
    GetPrivateProfileStringW(L"roll", L"max_rise", L"60", v, 32, path); sc.rollMaxRise = (float)_wtof(v);
    if (!(sc.rollDistStand >= 0.5f && sc.rollDistStand <= 3.f)) sc.rollDistStand = sc.rollDist;
    if (!(sc.rollMaxRise >= 0.f && sc.rollMaxRise <= 300.f)) sc.rollMaxRise = 60.f;
    g_steer = sc;
    ++g_iniGen;
    Log("settings: enabled=%d up=%.2f down=%.2f land=%.2f cliff_stop=%.2f | steer=%d air=%d landing=%d window=%.2fs rate=%.0f keys=%.4s"
        " | roll x%.2f %.2fs off_ledges=%d | landing momentum %.2f %.2fs | roll_vault=%d roll_block=%.2fs cooldown=%.2fs",
        g_enabled, g_speed[G_UP], g_speed[G_DOWN], g_speed[G_LAND], g_speed[G_CLIFF], sc.enabled, sc.air, sc.landing, sc.window, sc.rate, sc.keys,
        sc.rollDist, sc.rollTime, sc.offLedges, sc.landKeep, sc.landTime, sc.rollVault, sc.rollBlock, sc.cooldown);
    Log("settings: climb_wall=%.2f ladder=%.2f rope=%.2f squeeze=%.2f run_start=%.2f run_stop=%.2f turn=%.2f custom=%d entries",
        g_speed[G_CLIMB], g_speed[G_LADDER], g_speed[G_ROPE], g_speed[G_SQUEEZE], g_speed[G_START], g_speed[G_STOP], g_speed[G_PIVOT],
        (int)g_custom.size());
    Log("settings: transitions blend=%.2f locomotion_blend=%.2f blend_min=%.2f | side_angle=%.0f roll_vault_start=%.2f landing_cancel=%d", sc.blend, sc.locoBlend, sc.blendMin, sc.sideAngle, sc.rollVaultStart, (int)sc.landCancel);
    return true;
}

// ---------------------------------------------------------------- which animations
static bool DigitAt(const std::string& s, size_t i) { return i < s.size() && s[i] >= '0' && s[i] <= '9'; }
static bool Starts(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }

// N_Run01_1_L180 -> true for prefix "N_Run" (two digits, then the phase digit)
static bool Phase(const std::string& n, const char* fam, char phase) {
    size_t f = strlen(fam);
    return Starts(n, fam) && DigitAt(n, f) && DigitAt(n, f + 1) && n.size() > f + 3 && n[f + 2] == '_' && n[f + 3] == phase;
}

static Group Classify(const std::string& n) {
    // climb walls, ladders, ropes, narrow gaps (Motion/FieldAction/<Action>00/PCxxxx_*/); idles and the physics-timed rides stay normal
    if (Starts(n, "N_ClimbIdle")) return G_NONE;
    if (Starts(n, "N_ClimbStart") || Starts(n, "N_ClimbWalk") || Starts(n, "N_ClimbJump") || Starts(n, "N_ClimbEnd") ||
        Starts(n, "N_ClimbEND") || Starts(n, "N_ClimbHang")) return G_CLIMB;
    if (Starts(n, "N_Ladder")) return G_LADDER;
    if (Starts(n, "N_RopeSwing") || Starts(n, "N_ZipLine01_1")) return G_NONE;
    if (Starts(n, "N_Rope") || Starts(n, "N_Pole") || Starts(n, "N_ZipLine") || Starts(n, "N_HookShot")) return G_ROPE;
    if (Starts(n, "N_Narrow") || Starts(n, "N_Sukima") || Starts(n, "N_Tunnel") || Starts(n, "N_Kuguri")) return G_SQUEEZE;
    // locomotion transitions: run/sprint starts (_0), stops (_2, _3), 180 turns while running, turning on the spot; loops (_1) stay normal
    if (Phase(n, "N_Run", '0') || Phase(n, "N_Dash", '0') || Starts(n, "N_Accel")) return G_START;
    if (Phase(n, "N_Run", '2') || Phase(n, "N_Dash", '2') || Phase(n, "N_Dash", '3') || Starts(n, "N_DashToWalk") || Starts(n, "N_Brake"))
        return G_STOP;
    if ((Phase(n, "N_Run", '1') && (n.find("180") != std::string::npos)) || (Starts(n, "N_Turn") && DigitAt(n, 6))) return G_PIVOT;
    if (Starts(n, "N_CliffStop")) return G_CLIFF;
    if (Starts(n, "N_ClimbOver")) return G_UP;
    if (Starts(n, "N_Climb") && DigitAt(n, 7)) return G_UP;
    if (Starts(n, "N_Over") && DigitAt(n, 6)) return G_UP;
    // N_Jump01_0 / N_Fall01_0r: phase after the second underscore; _2 = touch-down (landing, roll), _3 = recovery
    if ((Starts(n, "N_Jump") || Starts(n, "N_Fall")) && DigitAt(n, 6)) {
        size_t u = n.find('_', 2);
        if (u != std::string::npos && DigitAt(n, u + 1) && n[u + 1] >= '2') return G_LAND;
        return G_DOWN;
    }
    return G_NONE;
}

// ---------------------------------------------------------------- object walk
static uintptr_t g_animSeqClass = 0;
static int g_rateOff = 0;

struct Entry { uint32_t nameIdx; uintptr_t outer; int8_t group; };
static std::unordered_map<uintptr_t, Entry> g_seen;   // AnimSequence object -> classification

template <class F> static void ForEachObject(F fn) {
    uintptr_t ga = g_guObjArr.load(); if (!ga) return;
    uintptr_t arr = ga + 0x10, chunks = 0; int32_t num = 0;
    if (!Rd(arr, chunks) || !Rd(arr + 0x14, num) || !chunks || num <= 0) return;
    for (int32_t c = 0; c * 65536 < num; ++c) {
        uintptr_t chunk = 0;
        if (!Rd(chunks + 8 * c, chunk) || !chunk) continue;
        int32_t n = num - c * 65536; if (n > 65536) n = 65536;
        for (int32_t i = 0; i < n; ++i) {
            uintptr_t obj = 0;
            if (Rd(chunk + 24 * i, obj) && obj) fn(obj);
        }
    }
}

static bool FindAnimSequenceClass() {
    uintptr_t found = 0;
    ForEachObject([&](uintptr_t obj) {
        if (found) return;
        uintptr_t cls = 0;
        if (!Rd(obj + OFF_UObject_Class, cls) || !cls) return;
        if (ObjName(obj) == "AnimSequence" && ObjName(cls) == "Class") found = obj;
    });
    if (!found) return false;
    // RateScale offset, by name through the class chain's reflected properties
    for (uintptr_t s = found; s && !g_rateOff; ) {
        uintptr_t f = 0; Rd(s + OFF_UStruct_ChildProps, f);
        for (int guard = 0; f && guard < 512; ++guard) {
            uint32_t idx = 0; Rd(f + OFF_FField_Name, idx);
            if (NameStr(idx) == "RateScale") { int32_t off = 0; Rd(f + OFF_FProperty_Offset, off); g_rateOff = off; break; }
            if (!Rd(f + OFF_FField_Next, f)) break;
        }
        if (!Rd(s + OFF_UStruct_Super, s)) break;
    }
    g_animSeqClass = found;
    if (g_rateOff <= 0 || g_rateOff >= 0x1000) { g_rateOff = 0; return false; }   // reflection not ready yet
    Log("AnimSequence class %p, RateScale at +0x%X", (void*)found, g_rateOff);
    return true;
}

// diagnostic: every ProcessEvent call on the Acrobacias prompt widget (FieldAction_Command_C), by function and caller chain
static const bool kDiag = false;   // diagnostics (watch file, prompt widget log, request log, edge-check log): off for release
static std::atomic<uintptr_t> g_iconClass{0};
struct IconCall { uint64_t key; uint32_t fnName; unsigned n; uintptr_t ex[30]; int ne; uint8_t logged; };
static IconCall g_ic[64]; static volatile LONG g_icN = 0;
static unsigned g_appliedGen = 0;
static uintptr_t g_pcClass = 0;
static std::atomic<uintptr_t> g_pc{0};
static std::atomic<uintptr_t> g_playSlotFn{0}, g_stopSlotFn{0};   // UAnimInstance::PlaySlotAnimationAsDynamicMontage / StopSlotAnimation
static std::atomic<uintptr_t> g_traceFn{0}, g_kslCdo{0};   // UKismetSystemLibrary::LineTraceSingle and the library default object
static std::atomic<uintptr_t> g_floorFn{0};       // UFunction UCharacterMovementComponent::K2_ComputeFloorDist
static std::atomic<uintptr_t> g_setLocFn{0};      // UFunction AActor::K2_SetActorLocation (found by the object walk)
static uintptr_t g_resParamObj = 0;                // /Game/DataObject/Resident/ResidentParameter (global tunables)

static void Pass() {
    bool changed = LoadIni();
    if (changed) g_seen.clear();   // re-classify everything (normal_speed may have changed)
    unsigned matched = 0, written = 0, newly = 0, perGroup[G_COUNT] = {}, keptNormal = 0, customN = 0;
    std::unordered_map<uintptr_t, Entry> next; next.reserve(g_seen.size() + 64);
    uintptr_t charRoots[256]; int charN = 0; std::vector<uintptr_t> charCand; charCand.reserve(512);
    uintptr_t pc = 0;
    ForEachObject([&](uintptr_t obj) {
        uintptr_t cls = 0;
        if (!Rd(obj + OFF_UObject_Class, cls)) return;
        if (!g_pcClass) { uint32_t ni = 0; if (Rd(obj + OFF_UObject_Name, ni) && NameStr(ni) == "EndPlayerControllerBP_C") g_pcClass = obj; }
        else if (cls == g_pcClass && !pc && !Starts(ObjName(obj), "Default__")) pc = obj;
        if (!g_resParamObj) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni) && NameStr(ni) == "ResidentParameter" && ObjName(cls) == "EndDataObjectResidentParameter") {
                uintptr_t outer = 0; Rd(obj + OFF_UObject_Outer, outer);
                if (outer && ObjName(outer) == "/Game/DataObject/Resident/ResidentParameter") g_resParamObj = obj;
            }
        }
        if (!g_playSlotFn.load() || !g_stopSlotFn.load()) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni)) {
                const std::string& n = NameStr(ni);
                if ((n == "PlaySlotAnimationAsDynamicMontage" || n == "StopSlotAnimation") && ObjName(cls) == "Function") {
                    uintptr_t outer = 0; Rd(obj + OFF_UObject_Outer, outer);
                    if (outer && ObjName(outer) == "AnimInstance") (n[0] == 'P' ? g_playSlotFn : g_stopSlotFn).store(obj);
                }
            }
        }
        if (cls == g_animSeqClass && g_handN < 8) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni)) {
                const std::string& n = NameStr(ni);
                if (n == g_handAnimName) {
                    uintptr_t outer = 0; Rd(obj + OFF_UObject_Outer, outer);
                    std::string path = outer ? ObjName(outer) : std::string();
                    size_t at = path.find("/Player/PC");
                    bool known = false;
                    for (LONG i = 0; i < g_handN; ++i) if (g_handAnims[i].seq == obj) known = true;
                    if (!known && at != std::string::npos) {
                        HandAnim& h = g_handAnims[g_handN]; h.seq = obj; h.right = false;
                        memcpy(h.pc, path.c_str() + at + 8, 6); h.pc[6] = 0;   // "PC0000"
                        InterlockedIncrement(&g_handN);
                        Log("wall hand animation %s for %s", n.c_str(), h.pc);
                    }
                }
            }
        }
        if (!g_traceFn.load() || !g_kslCdo.load()) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni)) {
                const std::string& n = NameStr(ni);
                if (n == "LineTraceSingle" && ObjName(cls) == "Function") {
                    uintptr_t outer = 0; Rd(obj + OFF_UObject_Outer, outer);
                    if (outer && ObjName(outer) == "KismetSystemLibrary") g_traceFn.store(obj);
                } else if (n == "Default__KismetSystemLibrary") g_kslCdo.store(obj);
            }
        }
        if (!g_floorFn.load()) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni) && NameStr(ni) == "K2_ComputeFloorDist" && ObjName(cls) == "Function") {
                uintptr_t outer = 0; Rd(obj + OFF_UObject_Outer, outer);
                if (outer && ObjName(outer) == "CharacterMovementComponent") g_floorFn.store(obj);
            }
        }
        if (!g_setLocFn.load()) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni) && NameStr(ni) == "K2_SetActorLocation" && ObjName(cls) == "Function") {
                uintptr_t outer = 0; Rd(obj + OFF_UObject_Outer, outer);
                if (outer && ObjName(outer) == "Actor") g_setLocFn.store(obj);
            }
        }
        if (!g_iconClass.load()) {
            uint32_t ni = 0;
            if (Rd(obj + OFF_UObject_Name, ni) && NameStr(ni) == "FieldAction_Command_C" && ObjName(cls) == "WidgetBlueprintGeneratedClass") {
                g_iconClass.store(obj); Log("prompt widget class %p", (void*)obj);
            }
        }
        {   // characters (wall stop ignores them): any class derived from ACharacter (party, NPCs, enemies, animals),
            // plus blueprint classes named like PC0002_..._C / NP0015_..._C
            static std::unordered_map<uintptr_t, int8_t> isChar;
            auto it = isChar.find(cls);
            int8_t c = 0;
            if (it == isChar.end()) {
                std::string cn = ObjName(cls);
                c = cn.size() > 8 && isupper((unsigned char)cn[0]) && isupper((unsigned char)cn[1]) && isdigit((unsigned char)cn[2]) && isdigit((unsigned char)cn[3]) &&
                    isdigit((unsigned char)cn[4]) && isdigit((unsigned char)cn[5]) && cn[6] == '_' && cn.compare(cn.size() - 2, 2, "_C") == 0 ? 1 : 0;
                uintptr_t s = cls;
                for (int d = 0; !c && s && d < 24; ++d) { if (ObjName(s) == "Character") c = 1; if (!Rd(s + OFF_UStruct_Super, s)) break; }
                isChar.emplace(cls, c);
            } else c = it->second;
            if (c && charCand.size() < 4096) {   // AActor::RootComponent is +0x138 in this build
                uintptr_t r = 0; Rd(obj + 0x138, r);
                if (r && !Starts(ObjName(obj), "Default__")) charCand.push_back(r);
            }
        }
        if (cls != g_animSeqClass) return;
        uint32_t nameIdx = 0; uintptr_t outer = 0;
        Rd(obj + OFF_UObject_Name, nameIdx); Rd(obj + OFF_UObject_Outer, outer);
        Entry e;
        auto it = g_seen.find(obj);
        if (it != g_seen.end() && it->second.nameIdx == nameIdx && it->second.outer == outer) {
            e = it->second;
        } else {
            e.nameIdx = nameIdx; e.outer = outer; e.group = G_NONE;
            // the full name: FName stores a trailing "_<n>" as a separate number (N_Fall03_2 = "N_Fall03" + number 3)
            std::string nm = NameStr(nameIdx);
            int32_t num = 0; Rd(obj + OFF_UObject_Name + 4, num);
            if (num > 0) nm += "_" + std::to_string(num - 1);
            Group g = Classify(nm);
            std::string op = outer ? ObjName(outer) : std::string();
            bool ours = Starts(op, "/Game/Motion/Player/") || Starts(op, "/Game/Motion/FieldAction/");
            if (ours) for (size_t i = 0; i < g_custom.size(); ++i)
                if (Starts(nm, g_custom[i].prefix.c_str())) { g = (Group)(G_CUSTOM + (int)i); break; }
            if (g != G_NONE && ours) {
                e.group = (int8_t)g;
                size_t a = 0;
                while (a < g_keepNormal.size()) {
                    size_t b = g_keepNormal.find(',', a); if (b == std::string::npos) b = g_keepNormal.size();
                    if (b > a && nm.compare(0, b - a, g_keepNormal, a, b - a) == 0) { e.group = (int8_t)G_KEEP; break; }
                    a = b + 1;
                }
            }
            ++newly;
        }
        next[obj] = e;
        if (e.group < 0) return;
        ++matched; if (e.group < G_COUNT) ++perGroup[e.group]; else if (e.group == G_KEEP) ++keptNormal; else ++customN;
        float want = 1.f, cur = 0.f;
        if (g_enabled && e.group < G_COUNT) want = g_speed[e.group];
        else if (g_enabled && e.group >= G_CUSTOM && e.group - G_CUSTOM < (int)g_custom.size()) want = g_custom[e.group - G_CUSTOM].rate;
        if (Rd(obj + g_rateOff, cur) && cur != want && WrFloat(obj + g_rateOff, want)) ++written;
    });
    g_seen.swap(next);
    {   // keep the characters near the player (RootComponent +0x138, RelativeLocation +0x11C in this build): towns load hundreds
        float pp[3] = {}; uintptr_t pawn = 0, proot = 0;
        bool havePos = pc && Rd(pc + 0x258, pawn) && pawn && Rd(pawn + 0x138, proot) && proot && RdBytes(proot + 0x11C, pp, 12);
        for (uintptr_t r : charCand) {
            if (charN >= 256) break;
            float c[3]; if (!RdBytes(r + 0x11C, c, 12)) continue;
            float dx = c[0] - pp[0], dy = c[1] - pp[1], dz = c[2] - pp[2];
            if (!havePos || dx * dx + dy * dy + dz * dz > 4000.f * 4000.f) continue;   // 40 m
            charRoots[charN++] = r;
        }
    }
    { int back = 1 - g_charFront.load(); memcpy(g_charRoots[back], charRoots, sizeof(uintptr_t) * charN); g_charCount[back].store(charN); g_charFront.store(back); }
    { static int lastN = -100; if (charN - lastN >= 5 || lastN - charN >= 5) { Log("characters ignored by wall stop: %d", charN); lastN = charN; } }
    if (pc != g_pc.load()) { g_pc.store(pc); Log("player controller %p", (void*)pc); }
    if (written || changed) {
        char buf[512]; int p = 0;
        for (int g = 0; g < G_COUNT; ++g) p += snprintf(buf + p, sizeof(buf) - p, "%s %u, ", GROUP_LOG[g], perGroup[g]);
        Log("animations: %u loaded (%scustom %u, normal speed %u), %u updated", matched, buf, customN, keptNormal, written);
    }
    (void)newly;
}

// ---------------------------------------------------------------- steering
// Main thread (1 Hz, owns the name cache) publishes pointers and reflected offsets; the steering thread (~250 Hz)
// only reads/writes plain memory with them. Steering = turn the controlled party character toward the movement
// input (stick / keys relative to the camera) while falling (mode 3) and shortly after landing (the roll); in the
// air the horizontal velocity turns with him. Vaults and climbs (flying mode 5) are never touched.
static uintptr_t FindField(uintptr_t cls, const char* name) {   // the FProperty itself (first match up the chain)
    for (int depth = 0; cls && depth < 64; ++depth) {
        uintptr_t f = 0; Rd(cls + OFF_UStruct_ChildProps, f);
        for (int guard = 0; f && guard < 1024; ++guard) {
            uint32_t idx = 0; Rd(f + OFF_FField_Name, idx);
            if (!_stricmp(NameStr(idx).c_str(), name)) return f;
            if (!Rd(f + OFF_FField_Next, f)) break;
        }
        if (!Rd(cls + OFF_UStruct_Super, cls)) break;
    }
    return 0;
}

static int FindProp(uintptr_t cls, const char* name) {
    for (int depth = 0; cls && depth < 64; ++depth) {
        uintptr_t f = 0; Rd(cls + OFF_UStruct_ChildProps, f);
        for (int guard = 0; f && guard < 1024; ++guard) {
            uint32_t idx = 0; Rd(f + OFF_FField_Name, idx);
            if (!_stricmp(NameStr(idx).c_str(), name)) { int32_t off = -1; Rd(f + OFF_FProperty_Offset, off); return off; }
            if (!Rd(f + OFF_FField_Next, f)) break;
        }
        if (!Rd(cls + OFF_UStruct_Super, cls)) break;
    }
    return -1;
}

static constexpr int OFF_Scene_C2W = 0x1C0;   // USceneComponent::ComponentToWorld (FTransform, rotation quat first)
struct SteerTargets {
    uintptr_t pc = 0, pawn = 0, root = 0, cmc = 0, setLoc = 0;
    int offPawn = -1, offCamMgr = -1, offCamYaw = -1, offRelRot = -1, offVel = -1, offMode = -1, offRelLoc = -1;
    int pNewLoc = -1, pSweep = -1, pTeleport = -1, parmsSize = 0;
    int pBlock = -1, pPenetr = -1; uint8_t mBlock = 0, mPenetr = 0;   // SweepHitResult.bBlockingHit / bStartPenetrating
    int pNormZ = -1;                                                  // SweepHitResult.ImpactNormal.Z
    int pHitActor = -1, pHitComp = -1;                                // SweepHitResult.Actor / Component (weak ptrs)
    uintptr_t thr[3] = {}; float thrOrig[3] = {};                     // Climb/ParkourJump/Jump ThresholdAngle floats
    uintptr_t floorFn = 0; int fCap = -1, fLine = -1, fSweep = -1, fRad = -1, fParms = 0, fHit = -1, fWalk = -1, fDist = -1, offCapRad = -1;
    uint8_t mHit = 0, mWalk = 0;                                      // K2_ComputeFloorDist parameters (FloorResult fields)
    uintptr_t traceFn = 0, kslCdo = 0; int lWorld = -1, lStart = -1, lEnd = -1, lChan = -1, lComplex = -1, lIgnoreSelf = -1, lParms = 0,
        lHit = -1, lImpact = -1, lNormal = -1, lRet = -1; uint8_t mLHit = 0;   // LineTraceSingle parameters (OutHit fields)
    uintptr_t ai = 0;       // the character's anim instance (mesh AnimScriptInstance)
    uintptr_t actReq = 0;   // the character's action request: [[pawn+0x638]+0x1B8]; +0x90 = action type (4 dodge, 8 parkour)
};
static SteerTargets g_tg[2];
static std::atomic<int> g_tgFront{0};
static int g_offPawn = -1, g_offCamMgr = -1, g_offCamYaw = -1, g_offRoot = -1, g_offCmc = -1, g_offRelRot = -1, g_offVel = -1, g_offMode = -1, g_offRelLoc = -1;
static int g_pNewLoc = -1, g_pSweep = -1, g_pTeleport = -1, g_parmsSize = 0, g_pBlock = -1, g_pPenetr = -1, g_pNormZ = -1;
static int g_pHitActor = -1, g_pHitComp = -1;
static uint8_t g_mBlock = 0, g_mPenetr = 0;
static int g_fCap = -1, g_fLine = -1, g_fSweep = -1, g_fRad = -1, g_fParms = 0, g_fHit = -1, g_fWalk = -1, g_fDist = -1, g_offCapRad = -1;
static uint8_t g_mHit = 0, g_mWalk = 0;
static int g_lWorld = -1, g_lStart = -1, g_lEnd = -1, g_lChan = -1, g_lComplex = -1, g_lIgnoreSelf = -1, g_lParms = 0, g_lHit = -1, g_lImpact = -1, g_lNormal = -1, g_lRet = -1;
static uint8_t g_mLHit = 0;
static uintptr_t g_thr[3] = {}; static float g_thrOrig[3] = {};
static const char* THR_NAMES[3] = { "ClimbThresholdAngle", "ParkourJumpThresholdAngle", "JumpThresholdAngle" };

// ResidentParameter is a frozen table (see tools/dataobj.py): the UObject holds the image base at +0x38; at the base,
// +0x00 TSet elements {FName key, int row, hashNext, hashIndex} (20 B, keys are real FNames in memory) and +0x48 the
// rows TArray (self-relative pointer << 1 | 1). Rows are {FString ParamString, FName ParamName, int ParamInt, float ParamFloat}.
static void ResolveThresholds() {
    if (g_thr[0] || !g_resParamObj) return;
    uintptr_t B = 0; if (!Rd(g_resParamObj + 0x38, B) || !B) return;
    int64_t ve = 0, vr = 0; int32_t ne = 0, nr = 0;
    if (!Rd(B, ve) || !Rd(B + 8, ne) || !Rd(B + 0x48, vr) || !Rd(B + 0x50, nr) || !(ve & 1) || !(vr & 1) || ne <= 0 || nr != ne || ne > 100000) return;
    uintptr_t elems = B + (ve >> 1), rows = B + 0x48 + (vr >> 1);
    uintptr_t found[3] = {}; float orig[3] = {};
    for (int32_t k = 0; k < ne; ++k) {
        uint32_t idx = 0; int32_t row = -1;
        if (!Rd(elems + 20 * k, idx) || !Rd(elems + 20 * k + 8, row) || row < 0 || row >= nr) continue;
        const std::string& n = NameStr(idx);
        for (int i = 0; i < 3; ++i) if (n == THR_NAMES[i]) { found[i] = rows + (uintptr_t)row * 32 + 28; Rd(found[i], orig[i]); }
    }
    for (int i = 0; i < 3; ++i) if (!found[i] || !(orig[i] > 0.f && orig[i] <= 90.f)) { Log("traversal thresholds not found - traversal blocking off"); g_resParamObj = 0; return; }
    for (int i = 0; i < 3; ++i) { g_thr[i] = found[i]; g_thrOrig[i] = orig[i]; }
    Log("traversal thresholds: climb %.1f, parkour jump %.1f, jump %.1f degrees", orig[0], orig[1], orig[2]);
}

// ---------------------------------------------------------------- stop running into walls ([traversal] wall_stop)
// The locomotion animation follows the stick (EndCharacterAnimInstance::CurrentInput), not the actual speed, so
// pushing into a wall plays the run in place. The game-thread tick decides "blocked" (walking, stick pushed, almost no
// speed, a wall right ahead in the stick direction); the anim instance's per-frame update (vtable slot 91,
// NativeUpdateAnimation(float)) is wrapped so that, while blocked, the input it just computed reads as released.
typedef void (*AnimUpdFn)(void*, float);
struct VtHook { uintptr_t vt; AnimUpdFn orig; };
static VtHook g_vh[16]; static volatile LONG g_vhN = 0;
static volatile uint8_t g_wallBlock = 0; static std::atomic<uintptr_t> g_wallAi{0};
static constexpr int VT_AnimUpdate = 91 * 8;
static constexpr int OFF_AI_MoveInputSize = 0x3E0 + 0x18, OFF_AI_AccelInputSize = 0x3E0 + 0x34, OFF_AI_BlendInputSpeed = 0x480;
// ---------------------------------------------------------------- shorter blends ([transitions])
// Pose transitions: the anim instance's bRequestedPoseTransition + RequestedPoseTransitionBlendTime(/LowPriority/Min) are
// set by the game for one frame and turned into an inertial blend; scaled before and after the anim update.
static int g_offPoseReq = -1, g_offPoseT[3] = { -1, -1, -1 };
static float g_poseWritten[3] = { -2.f, -2.f, -2.f };
static std::atomic<unsigned> g_poseScaled{0};
static void ScalePoseRequest(uint8_t* a) {
    if (g_offPoseReq < 0 || !g_enabled) return;
    const float s = g_steer.blend, mn = g_steer.blendMin;
    if (s == 1.f || !a[g_offPoseReq]) return;
    for (int k = 0; k < 3; ++k) {
        if (g_offPoseT[k] < 0) continue;
        float* f = reinterpret_cast<float*>(a + g_offPoseT[k]);
        float v = *f;
        if (v <= 0.f || v == g_poseWritten[k]) continue;
        float nv = v * s, floor = v < mn ? v : mn;
        if (nv < floor) nv = floor;
        *f = nv; g_poseWritten[k] = nv;
        unsigned n = ++g_poseScaled;
        if (n <= 40) Log("blend %s %.3f -> %.3f (locomotion state %u)", k == 0 ? "normal" : k == 1 ? "low-priority" : "min", v, nv,
                         (unsigned)a[0x6EC]);
    }
}

// Locomotion state machine crossfades: class data (BakedStateMachines -> Transitions[].CrossfadeDuration), scaled from the
// originals; re-applied when the ini changes. Main thread only.
static std::unordered_map<uintptr_t, float> g_xfOrig;
static std::unordered_map<uintptr_t, unsigned> g_xfApplied;   // anim class -> ini generation applied
static void ApplyLocoBlend(uintptr_t ai) {
    uintptr_t cls = 0, cc = 0; if (!Rd(ai + OFF_UObject_Class, cls) || !cls || !Rd(cls + OFF_UObject_Class, cc) || !cc) return;
    auto it = g_xfApplied.find(cls);
    if (it != g_xfApplied.end() && it->second == g_iniGen) return;
    g_xfApplied[cls] = g_iniGen;
    if (g_offPoseReq < 0) {
        g_offPoseReq = FindProp(cls, "bRequestedPoseTransition");
        g_offPoseT[0] = FindProp(cls, "RequestedPoseTransitionBlendTime");
        g_offPoseT[1] = FindProp(cls, "RequestedPoseTransitionBlendTimeLowPriority");
        g_offPoseT[2] = FindProp(cls, "RequestedPoseTransitionBlendTimeMin");
        Log("pose transition request at +0x%X (times +0x%X +0x%X +0x%X)", g_offPoseReq, g_offPoseT[0], g_offPoseT[1], g_offPoseT[2]);
    }
    static int offBsm = -2; if (offBsm == -2) offBsm = FindProp(cc, "BakedStateMachines");
    if (offBsm < 0) return;
    const float s = g_enabled ? g_steer.locoBlend : 1.f, mn = g_steer.blendMin;
    uintptr_t md = 0; int32_t mn_ = 0; Rd(cls + offBsm, md); Rd(cls + offBsm + 8, mn_);
    unsigned n = 0;
    for (int32_t m = 0; md && m < mn_ && m < 64; ++m) {
        uintptr_t td = 0; int32_t tn = 0; Rd(md + 48 * m + 0x20, td); Rd(md + 48 * m + 0x28, tn);
        for (int32_t t = 0; td && t < tn && t < 1024; ++t) {
            uintptr_t addr = td + 64 * t + 0x10; float v = 0.f;
            auto o = g_xfOrig.find(addr);
            if (o == g_xfOrig.end()) { if (!Rd(addr, v) || !(v >= 0.f && v <= 3.f)) continue; o = g_xfOrig.emplace(addr, v).first; }
            float base = o->second, nv = base * s, floor = base < mn ? base : mn;
            if (s != 1.f && nv < floor) nv = floor;
            if (WrFloat(addr, nv)) ++n;
        }
    }
    Log("locomotion blends: %u transitions of %s set to x%.2f", n, ObjName(cls).c_str(), s);
}

static void HookAnimUpdate(void* self, float dt) {
    uintptr_t vt = *reinterpret_cast<uintptr_t*>(self);
    ScalePoseRequest(static_cast<uint8_t*>(self));
    for (LONG i = 0; i < g_vhN; ++i) if (g_vh[i].vt == vt) { g_vh[i].orig(self, dt); break; }
    ScalePoseRequest(static_cast<uint8_t*>(self));
    if (g_wallBlock && reinterpret_cast<uintptr_t>(self) == g_wallAi.load()) {
        uint8_t* a = static_cast<uint8_t*>(self); const float z[3] = {};
        a[0x3E0 + 0x01] = 0;                                              // bEnableMoveInput
        memcpy(a + 0x3E0 + 0x0C, z, 12);                                   // MoveInput
        memcpy(a + OFF_AI_MoveInputSize, z, 4); memcpy(a + 0x3E0 + 0x20, z, 4);   // MoveInputSize, MoveInputDuration
        memcpy(a + OFF_AI_AccelInputSize, z, 4); memcpy(a + OFF_AI_BlendInputSpeed, z, 4);
    }
}
static void HookAnimVtable(uintptr_t ai) {
    uintptr_t vt = 0; if (!Rd(ai, vt) || !vt) return;
    for (LONG i = 0; i < g_vhN; ++i) if (g_vh[i].vt == vt) return;
    if (g_vhN >= 16) return;
    uintptr_t* slot = reinterpret_cast<uintptr_t*>(vt + VT_AnimUpdate);
    uintptr_t cur = 0; if (!Rd(reinterpret_cast<uintptr_t>(slot), cur)) return;
    if (cur == reinterpret_cast<uintptr_t>(&HookAnimUpdate) || cur < g_base || cur >= g_base + 0x10000000) return;
    g_vh[g_vhN].vt = vt; g_vh[g_vhN].orig = reinterpret_cast<AnimUpdFn>(cur); InterlockedIncrement(&g_vhN);   // known before the swap
    DWORD old; if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) return;
    *slot = reinterpret_cast<uintptr_t>(&HookAnimUpdate);
    VirtualProtect(slot, 8, old, &old);
    uintptr_t cls = 0; Rd(ai + OFF_UObject_Class, cls);
    Log("anim update wrapped for %s (wall_stop)", ObjName(cls).c_str());
}

static void UpdateSteerTargets() {
    SteerTargets t;
    uintptr_t pc = g_pc.load();
    if (pc) {
        uintptr_t c = 0; Rd(pc + OFF_UObject_Class, c);
        if (g_offPawn < 0) { g_offPawn = FindProp(c, "Pawn"); g_offCamMgr = FindProp(c, "PlayerCameraManager"); }
        uintptr_t cm = 0; if (g_offCamMgr >= 0) Rd(pc + g_offCamMgr, cm);
        if (cm && g_offCamYaw < 0) {
            Rd(cm + OFF_UObject_Class, c); int cache = FindProp(c, "CameraCachePrivate");
            if (cache >= 0) g_offCamYaw = cache + 0x10 + 0x10;   // FCameraCacheEntry{float TimeStamp; pad; FMinimalViewInfo POV{Location, Rotation}} -> Rotation.Yaw
        }
        uintptr_t pawn = 0; if (g_offPawn >= 0) Rd(pc + g_offPawn, pawn);
        uintptr_t pcls = 0; if (pawn) Rd(pawn + OFF_UObject_Class, pcls);
        if (pawn && pcls && Starts(ObjName(pcls), "PC")) {   // party characters only (PC0000_00_Cloud_Standard_C, ...)
            if (g_offRoot < 0) { g_offRoot = FindProp(pcls, "RootComponent"); g_offCmc = FindProp(pcls, "CharacterMovement"); }
            uintptr_t root = 0, cmc = 0;
            if (g_offRoot >= 0) Rd(pawn + g_offRoot, root);
            if (g_offCmc >= 0) Rd(pawn + g_offCmc, cmc);
            if (root && cmc && g_offRelRot < 0) {
                uintptr_t rc = 0, mc = 0; Rd(root + OFF_UObject_Class, rc); Rd(cmc + OFF_UObject_Class, mc);
                g_offRelRot = FindProp(rc, "RelativeRotation"); g_offVel = FindProp(mc, "Velocity"); g_offMode = FindProp(mc, "MovementMode");
                g_offRelLoc = FindProp(rc, "RelativeLocation"); g_offCapRad = FindProp(rc, "CapsuleRadius");
                Log("steer offsets: Pawn +0x%X CameraManager +0x%X CamYaw +0x%X Root +0x%X CMC +0x%X RelRot +0x%X Vel +0x%X Mode +0x%X",
                    g_offPawn, g_offCamMgr, g_offCamYaw, g_offRoot, g_offCmc, g_offRelRot, g_offVel, g_offMode);
            }
            t.pawn = pawn; t.root = root; t.cmc = cmc;
            {   // anim instance: [[pawn+Mesh]+AnimScriptInstance]
                static int offMesh = -1, offAnim = -1;
                if (offMesh < 0) offMesh = FindProp(pcls, "Mesh");
                uintptr_t mesh = 0; if (offMesh >= 0) Rd(pawn + offMesh, mesh);
                if (mesh && offAnim < 0) { uintptr_t mc = 0; Rd(mesh + OFF_UObject_Class, mc); offAnim = FindProp(mc, "AnimScriptInstance"); }
                uintptr_t ai = 0; if (mesh && offAnim >= 0) Rd(mesh + offAnim, ai);
                t.ai = ai;
                if (ai) { HookAnimVtable(ai); ApplyLocoBlend(ai); }
            }
            uintptr_t x = 0, req = 0; uint32_t ty = 0;
            if (Rd(pawn + 0x638, x) && x && Rd(x + 0x1B8, req) && req && Rd(req + 0x90, ty) && ty < 64) t.actReq = req;
        }
        t.pc = pc;
    }
    uintptr_t fn = g_setLocFn.load();
    if (fn && g_pNewLoc < 0) {
        g_pNewLoc = FindProp(fn, "NewLocation"); g_pSweep = FindProp(fn, "bSweep"); g_pTeleport = FindProp(fn, "bTeleport");
        int32_t sz = 0; Rd(fn + 0x58, sz); g_parmsSize = sz;   // UStruct::PropertiesSize
        // SweepHitResult is an FStructProperty (Struct at +0x78); its bool bitfields are FBoolProperty
        // (FieldSize +0x78, ByteOffset +0x79, ByteMask +0x7A, FieldMask +0x7B)
        uintptr_t hitProp = FindField(fn, "SweepHitResult"), hitStruct = 0;
        int32_t hitOff = -1; if (hitProp) { Rd(hitProp + OFF_FProperty_Offset, hitOff); Rd(hitProp + 0x78, hitStruct); }
        if (hitStruct && hitOff >= 0) {
            for (int which = 0; which < 2; ++which) {
                uintptr_t b = FindField(hitStruct, which ? "bStartPenetrating" : "bBlockingHit");
                int32_t off = -1; uint8_t byteOff = 0, mask = 0;
                if (b) { Rd(b + OFF_FProperty_Offset, off); Rd(b + 0x79, byteOff); Rd(b + 0x7A, mask); }
                if (off >= 0 && mask) { (which ? g_pPenetr : g_pBlock) = hitOff + off + byteOff; (which ? g_mPenetr : g_mBlock) = mask; }
            }
            int aOff = FindProp(hitStruct, "Actor"), cOff = FindProp(hitStruct, "Component");
            if (aOff >= 0) g_pHitActor = hitOff + aOff;
            if (cOff >= 0) g_pHitComp = hitOff + cOff;
            int nOff = FindProp(hitStruct, "ImpactNormal");
            if (nOff >= 0) g_pNormZ = hitOff + nOff + 8;
        }
        Log("K2_SetActorLocation %p: NewLocation +%d bSweep +%d bTeleport +%d, %d bytes; hit: blocking +%d/%02X penetrating +%d/%02X",
            (void*)fn, g_pNewLoc, g_pSweep, g_pTeleport, g_parmsSize, g_pBlock, g_mBlock, g_pPenetr, g_mPenetr);
    }
    uintptr_t ff = g_floorFn.load();
    if (ff && g_fCap < 0) {
        g_fCap = FindProp(ff, "CapsuleLocation"); g_fLine = FindProp(ff, "LineDistance"); g_fSweep = FindProp(ff, "SweepDistance");
        g_fRad = FindProp(ff, "SweepRadius"); int32_t sz = 0; Rd(ff + 0x58, sz); g_fParms = sz;
        uintptr_t rp = FindField(ff, "FloorResult"), rs = 0; int32_t ro = -1;
        if (rp) { Rd(rp + OFF_FProperty_Offset, ro); Rd(rp + 0x78, rs); }
        if (rs && ro >= 0) {
            for (int which = 0; which < 2; ++which) {
                uintptr_t b = FindField(rs, which ? "bWalkableFloor" : "bBlockingHit");
                int32_t off = -1; uint8_t byteOff = 0, mask = 0;
                if (b) { Rd(b + OFF_FProperty_Offset, off); Rd(b + 0x79, byteOff); Rd(b + 0x7A, mask); }
                if (off >= 0 && mask) { (which ? g_fWalk : g_fHit) = ro + off + byteOff; (which ? g_mWalk : g_mHit) = mask; }
            }
            int d = FindProp(rs, "FloorDist"); if (d >= 0) g_fDist = ro + d;
        }
        Log("K2_ComputeFloorDist %p: location +%d line +%d sweep +%d radius +%d, %d bytes; floor hit +%d/%02X walkable +%d/%02X dist +%d; capsule radius +0x%X",
            (void*)ff, g_fCap, g_fLine, g_fSweep, g_fRad, g_fParms, g_fHit, g_mHit, g_fWalk, g_mWalk, g_fDist, g_offCapRad);
    }
    uintptr_t lt = g_traceFn.load();
    if (lt && g_lStart < 0) {
        g_lWorld = FindProp(lt, "WorldContextObject"); g_lStart = FindProp(lt, "Start"); g_lEnd = FindProp(lt, "End");
        g_lChan = FindProp(lt, "TraceChannel"); g_lComplex = FindProp(lt, "bTraceComplex"); g_lIgnoreSelf = FindProp(lt, "bIgnoreSelf");
        g_lRet = FindProp(lt, "ReturnValue"); int32_t sz = 0; Rd(lt + 0x58, sz); g_lParms = sz;
        uintptr_t hp = FindField(lt, "OutHit"), hs = 0; int32_t ho = -1;
        if (hp) { Rd(hp + OFF_FProperty_Offset, ho); Rd(hp + 0x78, hs); }
        if (hs && ho >= 0) {
            uintptr_t b = FindField(hs, "bBlockingHit"); int32_t off = -1; uint8_t byteOff = 0, mask = 0;
            if (b) { Rd(b + OFF_FProperty_Offset, off); Rd(b + 0x79, byteOff); Rd(b + 0x7A, mask); }
            if (off >= 0 && mask) { g_lHit = ho + off + byteOff; g_mLHit = mask; }
            int ip = FindProp(hs, "ImpactPoint"), in = FindProp(hs, "ImpactNormal");
            if (ip >= 0) g_lImpact = ho + ip;
            if (in >= 0) g_lNormal = ho + in;
        }
        Log("LineTraceSingle %p (library %p): world +%d start +%d end +%d channel +%d complex +%d ignoreSelf +%d return +%d, %d bytes; hit +%d/%02X impact +%d normal +%d",
            (void*)lt, (void*)g_kslCdo.load(), g_lWorld, g_lStart, g_lEnd, g_lChan, g_lComplex, g_lIgnoreSelf, g_lRet, g_lParms, g_lHit, g_mLHit, g_lImpact, g_lNormal);
    }
    t.traceFn = lt; t.kslCdo = g_kslCdo.load(); t.lWorld = g_lWorld; t.lStart = g_lStart; t.lEnd = g_lEnd; t.lChan = g_lChan;
    t.lComplex = g_lComplex; t.lIgnoreSelf = g_lIgnoreSelf; t.lParms = g_lParms; t.lHit = g_lHit; t.mLHit = g_mLHit;
    t.lImpact = g_lImpact; t.lNormal = g_lNormal; t.lRet = g_lRet;
    t.floorFn = ff; t.fCap = g_fCap; t.fLine = g_fLine; t.fSweep = g_fSweep; t.fRad = g_fRad; t.fParms = g_fParms;
    t.fHit = g_fHit; t.mHit = g_mHit; t.fWalk = g_fWalk; t.mWalk = g_mWalk; t.fDist = g_fDist; t.offCapRad = g_offCapRad;
    t.setLoc = fn; t.pNewLoc = g_pNewLoc; t.pSweep = g_pSweep; t.pTeleport = g_pTeleport; t.parmsSize = g_parmsSize;
    t.pBlock = g_pBlock; t.pPenetr = g_pPenetr; t.mBlock = g_mBlock; t.mPenetr = g_mPenetr; t.pNormZ = g_pNormZ;
    t.pHitActor = g_pHitActor; t.pHitComp = g_pHitComp;
    ResolveThresholds();
    for (int i = 0; i < 3; ++i) { t.thr[i] = g_thr[i]; t.thrOrig[i] = g_thrOrig[i]; }
    t.offPawn = g_offPawn; t.offCamMgr = g_offCamMgr; t.offCamYaw = g_offCamYaw;
    t.offRelRot = g_offRelRot; t.offVel = g_offVel; t.offMode = g_offMode; t.offRelLoc = g_offRelLoc;
    int back = 1 - g_tgFront.load(); g_tg[back] = t; g_tgFront.store(back);
}

struct XState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XState*);
static XInputGetStateFn g_xget = nullptr;

static bool ReadInput(const SteerCfg& sc, float& right, float& fwd) {
    right = fwd = 0.f; float best = 0.f;
    if (g_xget) for (DWORD i = 0; i < 4; ++i) {
        XState s; if (g_xget(i, &s) != ERROR_SUCCESS) continue;
        float x = s.lx / 32767.f, y = s.ly / 32767.f, m = std::sqrt(x * x + y * y);
        if (m > best) { best = m; right = x; fwd = y; }
    }
    if (best < 0.3f) {
        DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) {
            auto k = [](char c) { return (GetAsyncKeyState((unsigned char)c) & 0x8000) ? 1.f : 0.f; };
            fwd = k(sc.keys[0]) - k(sc.keys[2]); right = k(sc.keys[3]) - k(sc.keys[1]);
            best = std::sqrt(right * right + fwd * fwd);
        }
    }
    return best >= 0.3f;
}

static float Wrap(float a) { a = std::fmod(a + 180.f, 360.f); if (a < 0) a += 360.f; return a - 180.f; }

static bool WrBytes(uintptr_t addr, const void* src, size_t n) {
    __try { memcpy(reinterpret_cast<void*>(addr), src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void SteerTick(float dt, uint8_t& prevMode, double& landT, double now, unsigned& writes) {
    const SteerTargets& t = g_tg[g_tgFront.load()];
    SteerCfg sc = g_steer;
    if (!sc.enabled || !t.pc || !t.pawn || t.offPawn < 0 || t.offCamYaw < 0 || t.offRelRot < 0 || t.offVel < 0 || t.offMode < 0) return;
    uintptr_t pawn = 0; if (!Rd(t.pc + t.offPawn, pawn) || pawn != t.pawn) return;   // leader changed: wait for the main thread
    uint8_t mode = 0; if (!Rd(t.cmc + t.offMode, mode)) return;
    if (prevMode == 3 && mode == 1) landT = now;
    prevMode = mode;
    bool active = (mode == 3 && sc.air) || (mode == 1 && sc.landing && now - landT < sc.window);
    if (!active) return;
    float right, fwd; if (!ReadInput(sc, right, fwd)) return;
    uintptr_t cm = 0; if (!Rd(t.pc + t.offCamMgr, cm) || !cm) return;
    float camYaw = 0; if (!Rd(cm + t.offCamYaw, camYaw)) return;
    float rot[3]; if (!RdBytes(t.root + t.offRelRot, rot, 12)) return;
    float want = camYaw + std::atan2(right, fwd) * 57.2957795f;
    float d = Wrap(want - rot[1]), lim = sc.rate * dt;
    float step = d > lim ? lim : (d < -lim ? -lim : d);
    if (std::fabs(step) < 0.01f) return;
    rot[1] = Wrap(rot[1] + step);
    float h = rot[1] * 0.00872664626f;   // half angle in radians
    float quat[4] = { 0.f, 0.f, std::sin(h), std::cos(h) };
    WrBytes(t.root + t.offRelRot, rot, 12);
    WrBytes(t.root + OFF_Scene_C2W, quat, 16);
    if (mode == 3) {
        float v[2]; if (RdBytes(t.cmc + t.offVel, v, 8)) {
            float c = std::cos(step * 0.0174532925f), s = std::sin(step * 0.0174532925f);
            float nv[2] = { v[0] * c - v[1] * s, v[0] * s + v[1] * c };
            WrBytes(t.cmc + t.offVel, nv, 8);
        }
    }
    ++writes;
}

// landing cancel ([transitions] landing_cancel): a traversal (type 8) stays active through its landing with the feet
// already on the ground (mode 1; request flags 0x100 / 0x2000 mark that tail) and every button press is ignored.
// Polled on the ~250 Hz steering thread so the key-down is seen within a few ms, before the game's next frame reads it:
// the traversal is ended then, and the game takes the press as usual (roll, or the next traversal at a ledge).
static bool GameFocused();
static void LandCancelPoll(double now) {
    static bool keyWas = false; static double cancelT = -100, tailT = -100; static unsigned nCancel = 0, nRolled = 0, nMove = 0;
    const SteerCfg& sc = g_steer;
    bool key = GameFocused() && (GetAsyncKeyState(sc.rollKey) & 0x8000);
    if (!key && g_xget) for (DWORD i = 0; i < 4 && !key; ++i) { XState s; if (g_xget(i, &s) == ERROR_SUCCESS && (s.buttons & 0x2000)) key = true; }
    const SteerTargets& t = g_tg[g_tgFront.load()];
    uint32_t act = 0; uint8_t mode = 0;
    if (g_enabled && sc.landCancel && t.actReq && t.cmc && t.offMode >= 0 && Rd(t.actReq + 0x90, act) && Rd(t.cmc + t.offMode, mode)) {
        uint32_t fl = 0; Rd(t.actReq + 0x54, fl);
        uint32_t code0 = 0; Rd(t.actReq + 0x8C, code0);
        // feet on the ground and the traversal in its landing phase (request code ..0A01) or marked as ending (0x2000);
        // never the start of a jump-down (..2201 / ..2601 / ..0601, still on the edge or just touching down)
        bool tail = (act & 8) && mode == 1 && ((code0 & 0xFFFF) == 0x0A01 || (fl & 0x2000));
        if (!tail) tailT = -100; else if (tailT < 0) tailT = now;
        bool byKey = tail && key && !keyWas;                                       // dodge / Acrobacias: right away
        float r, f; bool stick = ReadInput(sc, r, f);
        bool byMove = tail && !byKey && sc.landCancelMove > 0.f && stick && now - tailT >= sc.landCancelMove;   // moving on
        if (byKey || byMove) {
            // the game's own end of a traversal (0x15BBF0D): clear the in-progress bits of the request code,
            // end state 7, clear the traversal bit of the action mask
            uint32_t code = 0; Rd(t.actReq + 0x8C, code);
            const uint32_t newCode = code & 0xFFF1FFFFu, newAct = act & ~8u; const uint8_t endState = 7;
            // first the 'everything allowed' marks the game sets when it ends one itself (0x15BB8E0: 0x15B921C(req,0,0,0x21EF))
            if (sc.landCancelFlags) { uint32_t nf = fl | sc.landCancelFlags; WrBytes(t.actReq + 0x54, &nf, 4); }
            if (WrBytes(t.actReq + 0x8C, &newCode, 4) && WrBytes(t.actReq + 0x338, &endState, 1) && WrBytes(t.actReq + 0x90, &newAct, 4)) {
                cancelT = byKey ? now : -100; tailT = -100;
                if (byMove) ++nMove;
                if (++nCancel <= 30) Log("landing cancelled by %s %.2f s into it (code %08X flags %08X, set %08X)", byKey ? "button" : "stick",
                                         byKey ? 0.f : sc.landCancelMove, code, fl, sc.landCancelFlags);
            }
        }
        if (cancelT > 0 && now - cancelT < 0.3 && (act & 4)) { cancelT = -100; if (++nRolled <= 30) Log("  -> rolled %u of %u button cancels", nRolled, nCancel - nMove); }
    }
    keyWas = key;
}

static DWORD WINAPI SteerThread(LPVOID) {
    HMODULE xi = LoadLibraryW(L"xinput1_4.dll");
    if (!xi) xi = LoadLibraryW(L"xinput9_1_0.dll");
    if (xi) g_xget = reinterpret_cast<XInputGetStateFn>(GetProcAddress(xi, "XInputGetState"));
    Log("steering thread running (controller input %s)", g_xget ? "available" : "NOT available, keyboard only");
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    LARGE_INTEGER freq, prev, cur; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    uint8_t prevMode = 1; double landT = -100.0; unsigned writes = 0, lastLogged = 0; double lastLog = 0;
    for (;;) {
        if (timer) { LARGE_INTEGER due; due.QuadPart = -40000; SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE); WaitForSingleObject(timer, 50); }
        else Sleep(4);
        QueryPerformanceCounter(&cur);
        float dt = (float)(cur.QuadPart - prev.QuadPart) / (float)freq.QuadPart; prev = cur;
        if (dt > 0.05f) dt = 0.05f;
        double now = (double)cur.QuadPart / (double)freq.QuadPart;
        __try { SteerTick(dt, prevMode, landT, now, writes); LandCancelPoll(now); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (now - lastLog > 30.0) { if (writes != lastLogged) Log("steering: %u turn steps so far", writes); lastLogged = writes; lastLog = now; }
    }
    return 0;
}

// ---------------------------------------------------------------- game-thread tick (UObject::ProcessEvent detour)
// ProcessEvent (vtable slot 66 in this build, shared by every class) runs many times per frame on the game thread.
// Its first 16 bytes are relocatable (mov [rsp+10h],rdx; push rbp/rbx/rdi; mov rbp,rsp; mov eax,60h), so they move
// into a trampoline and the function starts with a jump to HookProcessEvent. Once per frame (on the game thread only)
// GameTick stretches dodge rolls and carries landing momentum, moving the character with AActor::K2_SetActorLocation
// and bSweep=true, so walls and obstacles stop him exactly like normal movement does.
typedef void (*ProcessEventFn)(void*, void*, void*);
static ProcessEventFn g_peOrig = nullptr;
// UObject::ProcessEvent = vtable slot 69 (0x228) for plain objects (actors override it with AActor::ProcessEvent,
// which forwards here). Opening: push rbp/rsi/rdi/r12-r15; mov eax,0F0h = 17 relocatable bytes, then a rel32 call.
static thread_local bool t_inTick = false;
static std::atomic<DWORD> g_gameThread{0};

enum SweepResult { SW_FAIL, SW_CLEAR, SW_BLOCKED };

// Move the pawn toward loc with a collision sweep. If the sweep touches anything (or starts inside something),
// the move is undone (teleport back to `from`) and SW_BLOCKED is returned, so the extra movement never pushes the
// character into geometry the game would then resolve by lifting him onto it.
struct HitInfo { bool hit = false, penetrating = false; float nz = 0.f; int32_t actorIdx = -1, compIdx = -1; };

static SweepResult SweepTo(const SteerTargets& t, const float from[3], const float loc[3], HitInfo* hi = nullptr) {
    alignas(16) uint8_t parms[512];
    if (!t.setLoc || t.parmsSize <= 0 || t.parmsSize > (int)sizeof(parms) || t.pNewLoc < 0 || t.pSweep < 0 || t.pBlock < 0) return SW_FAIL;
    memset(parms, 0, t.parmsSize);
    memcpy(parms + t.pNewLoc, loc, 12);
    parms[t.pSweep] = 1;
    if (t.pTeleport >= 0) parms[t.pTeleport] = 0;
    g_peOrig(reinterpret_cast<void*>(t.pawn), reinterpret_cast<void*>(t.setLoc), parms);
    bool penetrating = t.pPenetr >= 0 && (parms[t.pPenetr] & t.mPenetr);
    bool hit = (parms[t.pBlock] & t.mBlock) != 0;
    float nz = 0.f; if (t.pNormZ >= 0) memcpy(&nz, parms + t.pNormZ, 4);
    if (hi) {
        hi->hit = hit; hi->penetrating = penetrating; hi->nz = nz;
        if (t.pHitActor >= 0) memcpy(&hi->actorIdx, parms + t.pHitActor, 4);   // TWeakObjectPtr: ObjectIndex first
        if (t.pHitComp >= 0) memcpy(&hi->compIdx, parms + t.pHitComp, 4);
    }
    bool wall = hit && nz < 0.7f;                    // walkable ground (normal pointing up) doesn't count as blocking
    if (!penetrating && !wall) return SW_CLEAR;
    memset(parms, 0, t.parmsSize);
    memcpy(parms + t.pNewLoc, from, 12);
    if (t.pTeleport >= 0) parms[t.pTeleport] = 1;     // put him back exactly where the game had him, no sweep
    g_peOrig(reinterpret_cast<void*>(t.pawn), reinterpret_cast<void*>(t.setLoc), parms);
    return SW_BLOCKED;
}

// Floor under a capsule placed at `loc` (UCharacterMovementComponent::K2_ComputeFloorDist: the character's own capsule
// and collision channel, swept down up to maxDist). Game thread only.
struct FloorProbe { bool ok = false, hit = false, walkable = false; float dist = 0.f; };
static float CapsuleRadius(const SteerTargets& t) { float r = 34.f; if (t.offCapRad >= 0) Rd(t.root + t.offCapRad, r); return (r > 5.f && r < 200.f) ? r : 34.f; }
static FloorProbe ProbeFloor(const SteerTargets& t, const float loc[3], float maxDist) {
    FloorProbe f; alignas(16) uint8_t parms[512];
    if (!t.floorFn || !t.cmc || t.fParms <= 0 || t.fParms > (int)sizeof(parms) || t.fCap < 0 || t.fLine < 0 || t.fSweep < 0 ||
        t.fRad < 0 || t.fHit < 0 || t.fDist < 0) return f;
    float r = CapsuleRadius(t) * 0.9f;
    memset(parms, 0, t.fParms);
    memcpy(parms + t.fCap, loc, 12); memcpy(parms + t.fLine, &maxDist, 4); memcpy(parms + t.fSweep, &maxDist, 4); memcpy(parms + t.fRad, &r, 4);
    g_peOrig(reinterpret_cast<void*>(t.cmc), reinterpret_cast<void*>(t.floorFn), parms);
    f.ok = true; f.hit = (parms[t.fHit] & t.mHit) != 0; f.walkable = t.fWalk >= 0 && (parms[t.fWalk] & t.mWalk) != 0;
    memcpy(&f.dist, parms + t.fDist, 4);
    return f;
}
// Straight-down line trace (Visibility) ignoring the character. Game thread only. Returns false when unavailable.
struct TraceHit { bool ok = false, hit = false; float z = 0.f, nz = 0.f; };
static TraceHit TraceDown(const SteerTargets& t, const float from[3], float len) {
    TraceHit r; alignas(16) uint8_t parms[1024];
    if (!t.traceFn || !t.kslCdo || t.lParms <= 0 || t.lParms > (int)sizeof(parms) || t.lStart < 0 || t.lEnd < 0 || t.lHit < 0 || t.lImpact < 0) return r;
    memset(parms, 0, t.lParms);
    if (t.lWorld >= 0) memcpy(parms + t.lWorld, &t.pawn, 8);
    float end[3] = { from[0], from[1], from[2] - len };
    memcpy(parms + t.lStart, from, 12); memcpy(parms + t.lEnd, end, 12);
    if (t.lChan >= 0) parms[t.lChan] = 0;                 // TraceTypeQuery1 = Visibility
    if (t.lIgnoreSelf >= 0) parms[t.lIgnoreSelf] = 1;     // ignores the world-context actor (the character)
    g_peOrig(reinterpret_cast<void*>(t.kslCdo), reinterpret_cast<void*>(t.traceFn), parms);
    r.ok = true; r.hit = (parms[t.lHit] & t.mLHit) != 0;
    memcpy(&r.z, parms + t.lImpact + 8, 4);
    if (t.lNormal >= 0) memcpy(&r.nz, parms + t.lNormal + 8, 4);
    return r;
}
// Line trace between two points (Visibility) ignoring the character. Game thread only.
struct SegHit { bool ok = false, hit = false; float n[3] = {}, p[3] = {}; };
static SegHit TraceSeg(const SteerTargets& t, const float from[3], const float to[3]) {
    SegHit r; alignas(16) uint8_t parms[1024];
    if (!t.traceFn || !t.kslCdo || t.lParms <= 0 || t.lParms > (int)sizeof(parms) || t.lStart < 0 || t.lEnd < 0 || t.lHit < 0 || t.lNormal < 0) return r;
    memset(parms, 0, t.lParms);
    if (t.lWorld >= 0) memcpy(parms + t.lWorld, &t.pawn, 8);
    memcpy(parms + t.lStart, from, 12); memcpy(parms + t.lEnd, to, 12);
    if (t.lChan >= 0) parms[t.lChan] = 0;
    if (t.lIgnoreSelf >= 0) parms[t.lIgnoreSelf] = 1;
    g_peOrig(reinterpret_cast<void*>(t.kslCdo), reinterpret_cast<void*>(t.traceFn), parms);
    r.ok = true; r.hit = (parms[t.lHit] & t.mLHit) != 0; memcpy(r.n, parms + t.lNormal, 12);
    if (t.lImpact >= 0) memcpy(r.p, parms + t.lImpact, 12);
    return r;
}
// is there ground within max_drop below his feet, `ahead` cm past `at` (along dir)? Unknown (no trace) counts as yes.
static bool DropOk(const SteerTargets& t, const float at[3], float dirX, float dirY, float ahead, float maxDrop) {
    const float halfH = 96.f;                             // capsule half height (feet = centre - 96)
    float p[3] = { at[0] + dirX * ahead, at[1] + dirY * ahead, at[2] + 50.f };
    TraceHit h = TraceDown(t, p, 50.f + halfH + maxDrop);
    bool ok = !h.ok || h.hit;
    static unsigned logged = 0;
    float drop = h.hit ? (at[2] - halfH) - h.z : 99999.f;
    if (kDiag && logged < 40 && (!ok || !h.ok || drop > 30.f)) {   // edges only (flat ground isn't interesting)
        ++logged;
        Log("edge check %.0f cm ahead: %s, ground %.0f cm below his feet (limit %.0f) normal %.2f -> %s", ahead, h.ok ? (h.hit ? "hit" : "no ground") : "no trace",
            h.hit ? (at[2] - halfH) - h.z : -1.f, maxDrop, h.nz, ok ? "roll on" : "stop at the edge");
    }
    return ok;
}

// Hand on the wall: play the character's wall-contact animation as a dynamic montage on DefaultSlot (held at a chosen
// time when wall_hand_rate is 0) while the wall stop holds him, and blend it out when it lets go.
static uint32_t g_defaultSlotName = 0;   // FName comparison index of "DefaultSlot" (from the anim class's slot node)
static uint32_t FindDefaultSlot(uintptr_t ai) {
    uintptr_t cls = 0; if (!Rd(ai + OFF_UObject_Class, cls)) return 0;
    uintptr_t f = 0; Rd(cls + OFF_UStruct_ChildProps, f);
    for (int guard = 0; f && guard < 4096; ++guard) {
        uintptr_t st = 0; Rd(f + 0x78, st);
        if (st) {
            uint32_t sn = 0; Rd(st + OFF_UObject_Name, sn);
            if (NameStr(sn) == "AnimNode_Slot") {
                int32_t off = -1; Rd(f + OFF_FProperty_Offset, off);
                int so = FindProp(st, "SlotName"); uint32_t idx = 0;
                if (off >= 0 && so >= 0 && Rd(ai + off + so, idx) && NameStr(idx) == "DefaultSlot") return idx;
            }
        }
        if (!Rd(f + OFF_FField_Next, f)) break;
    }
    return 0;
}
static bool g_handOn = false; static uintptr_t g_handAi = 0;
static float* g_prSeen = nullptr; static float* g_prMul = nullptr;   // prompt reach stub data (see InstallPromptReach)
// prompt reach multiplier in use: prompt_reach normally, wall_hand_prompt_reach while the hands-on-wall pose shows
// (so a climbable wall offers its prompt from the pose at the game's normal distance)
static float PromptMulWanted() {
    if (!g_enabled) return 1.f;
    return g_handOn ? g_steer.wallHandPromptReach : g_steer.promptReach;
}
static void ApplyPromptMul() { if (g_prMul) *g_prMul = PromptMulWanted(); }
static void PlayWallHand(const SteerTargets& t, uintptr_t seq, const SteerCfg& sc) {
    uintptr_t fn = g_playSlotFn.load(); alignas(16) uint8_t parms[256];
    if (!fn || !g_defaultSlotName) return;
    int32_t sz = 0; Rd(fn + 0x58, sz); if (sz <= 0 || sz > (int)sizeof(parms)) return;
    static int pA = -2, pS, pBI, pBO, pR, pL, pBT, pT;
    if (pA == -2) {
        pA = FindProp(fn, "Asset"); pS = FindProp(fn, "SlotNodeName"); pBI = FindProp(fn, "BlendInTime"); pBO = FindProp(fn, "BlendOutTime");
        pR = FindProp(fn, "InPlayRate"); pL = FindProp(fn, "LoopCount"); pBT = FindProp(fn, "BlendOutTriggerTime"); pT = FindProp(fn, "InTimeToStartMontageAt");
        Log("PlaySlotAnimationAsDynamicMontage: asset +%d slot +%d blend in +%d out +%d rate +%d loops +%d trigger +%d start +%d, %d bytes", pA, pS, pBI, pBO, pR, pL, pBT, pT, sz);
    }
    if (pA < 0 || pS < 0 || pR < 0) return;
    memset(parms, 0, sz);
    memcpy(parms + pA, &seq, 8); memcpy(parms + pS, &g_defaultSlotName, 4);
    float bi = sc.wallHandBlend, bo = sc.wallHandBlend, rate = sc.wallHandRate > 0.f ? sc.wallHandRate : 0.0001f, trig = -1.f, st = sc.wallHandTime;
    int32_t loops = sc.wallHandRate > 0.f ? 1000 : 1;
    if (pBI >= 0) memcpy(parms + pBI, &bi, 4); if (pBO >= 0) memcpy(parms + pBO, &bo, 4); memcpy(parms + pR, &rate, 4);
    if (pL >= 0) memcpy(parms + pL, &loops, 4); if (pBT >= 0) memcpy(parms + pBT, &trig, 4); if (pT >= 0) memcpy(parms + pT, &st, 4);
    g_peOrig(reinterpret_cast<void*>(t.ai), reinterpret_cast<void*>(fn), parms);
    g_handOn = true; g_handAi = t.ai; ApplyPromptMul();
}
static void StopWallHand(const SteerCfg& sc) {
    uintptr_t fn = g_stopSlotFn.load(); alignas(16) uint8_t parms[64];
    g_handOn = false; ApplyPromptMul();
    if (!fn || !g_handAi || !g_defaultSlotName) return;
    int32_t sz = 0; Rd(fn + 0x58, sz); if (sz <= 0 || sz > (int)sizeof(parms)) return;
    static int pB = -2, pS;
    if (pB == -2) { pB = FindProp(fn, "InBlendOutTime"); pS = FindProp(fn, "SlotNodeName"); }
    if (pS < 0) return;
    memset(parms, 0, sz);
    float bo = sc.wallHandBlend; if (pB >= 0) memcpy(parms + pB, &bo, 4); memcpy(parms + pS, &g_defaultSlotName, 4);
    g_peOrig(reinterpret_cast<void*>(g_handAi), reinterpret_cast<void*>(fn), parms);
}

struct MoveState {
    double last = 0, rollT0 = -100, landT0 = -100;
    uint8_t prevMode = 1; bool keyWas = false, haveLast = false;
    float lastPos[3] = {}, airV[2] = {}, carry[2] = {}, rollZ0 = 0;
    unsigned rollMoves = 0, landMoves = 0, blocked = 0, travBlocks = 0, edgePushes = 0, edgeFalls = 0;
    double blockRollT0 = -100;
    bool dirSet = false, edgeStop = false, pushedThisRoll = false; float dirX = 0, dirY = 0, peak = 0;
    uint32_t lastAct = 0; double travEndT = -100, rollT0Block = -100; unsigned cooldowns = 0, smallTrav = 0; float travZ0 = 0;
    bool ffOn = false, snapGuard = false, haveExpect = false; float ffV[2] = {}, expect[3] = {}; double snapT0 = -100;
    float winSpeed = 0.f, winPos[2] = {}; double winT = -1.0;
    bool standingRoll = false;
    unsigned ffRestores = 0, snapUndos = 0, cliffStops = 0, edgeCarries = 0; bool steppedOff = false, offActive = false; float offPushed = 0.f; bool stretchedThisRoll = false;
};
static MoveState g_ms;
// diagnostics: what stops a jump-down mid-air (filled on the game thread, named and logged by the main thread)
struct DiagEntry { double t; int32_t actor, comp; float nz, vel, moved; uint8_t hit, pen, result; float pos[3]; };
static DiagEntry g_diag[64]; static std::atomic<unsigned> g_diagW{0}; static unsigned g_diagR = 0;
static double g_diagLast = -100; static float g_ffPrev[3] = {}; static bool g_ffHavePrev = false;
static volatile uint8_t g_blockParkour = 0;
static volatile uint8_t g_rollEarly = 0;      // first roll_vault_start seconds of a roll: traversal still allowed
static volatile uint8_t g_carryOn = 0;       // edge carry in progress: the roll's own position writes are skipped
static std::atomic<unsigned> g_carrySkips{0};
static volatile uint8_t g_coolingNow = 0;     // diagnostic: inside the cooldown window
static const bool kCoolBlock = true;
static volatile uint8_t g_sideOnly = 0;       // the ledge in front was only found by the side search (side_angle)
static volatile uint8_t g_coolParkour = 0;    // cooldown after a traversal: no new one starts (the parkour update still runs)   // read by the parkour-update gate stub: 1 = skip traversal this frame

static bool GameFocused() { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid == GetCurrentProcessId(); }

static void GameTick() {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    double now = (double)c.QuadPart / (double)f.QuadPart;
    MoveState& m = g_ms;
    if (now - m.last < 0.004) return;               // at most once per ~frame
    float dt = (float)(now - m.last); if (dt > 0.05f) dt = 0.05f; m.last = now;
    const SteerTargets& t = g_tg[g_tgFront.load()];
    SteerCfg sc = g_steer;
    if (!g_enabled) { g_blockParkour = 0; g_coolParkour = 0; g_rollEarly = 0; return; }
    if (!t.pc || !t.pawn || t.offRelLoc < 0 || t.offMode < 0 || t.offVel < 0 || !t.setLoc) return;
    uintptr_t pawn = 0; if (!Rd(t.pc + t.offPawn, pawn) || pawn != t.pawn) { m.haveLast = false; return; }
    uint8_t mode = 0; if (!Rd(t.cmc + t.offMode, mode)) return;
    float pos[3]; if (!RdBytes(t.root + t.offRelLoc, pos, 12)) return;

    if (m.winT < 0 || now - m.winT > 0.3) { m.winPos[0] = pos[0]; m.winPos[1] = pos[1]; m.winT = now; }
    else if (now - m.winT >= 0.04) {
        float wx = pos[0] - m.winPos[0], wy = pos[1] - m.winPos[1];
        m.winSpeed = std::sqrt(wx * wx + wy * wy) / (float)(now - m.winT);
        m.winPos[0] = pos[0]; m.winPos[1] = pos[1]; m.winT = now;
    }

    // dodge roll: key pressed while walking/standing -> stretch the game's own movement for rollTime seconds
    // a roll = the game's own action request says "dodge" (type 4); works for keyboard and controller alike
    uint32_t act = 0; bool dodging = t.actReq && Rd(t.actReq + 0x90, act) && act == 4;
    // end of vaults and climb-ups ([transitions] landing_cancel): these run in the scripted 'flying' mode (5) until their
    // last frame. In their marked ending (request flags 0x2000) with walkable floor right under the feet, the button or the
    // stick ends them the game's way and puts the character back into walking (SetMovementMode, vtable +0x5F8).
    {
        static bool keyWas = false; static double tailT = -100; static unsigned nUp = 0;
        bool key = GameFocused() && (GetAsyncKeyState(sc.rollKey) & 0x8000);
        if (!key && g_xget) for (DWORD i = 0; i < 4 && !key; ++i) { XState s; if (g_xget(i, &s) == ERROR_SUCCESS && (s.buttons & 0x2000)) key = true; }
        uint32_t fl = 0; if (t.actReq) Rd(t.actReq + 0x54, fl);
        bool tail = sc.landCancel && t.actReq && (act & 8) && mode == 5 && (fl & 0x2000);
        if (!tail) tailT = -100; else if (tailT < 0) tailT = now;
        float r, f; bool stick = ReadInput(sc, r, f);
        if (tail && ((key && !keyWas) || (stick && now - tailT >= sc.landCancelMove))) {
            FloorProbe fp = ProbeFloor(t, pos, 40.f);
            if (fp.ok && fp.hit && fp.walkable && fp.dist < 12.f) {
                uint32_t code = 0; Rd(t.actReq + 0x8C, code);
                uint32_t nf = fl | sc.landCancelFlags, newCode = code & 0xFFF1FFFFu, newAct = act & ~8u; const uint8_t endState = 7;
                uintptr_t vt = 0, fn = 0;
                if (Rd(t.cmc, vt) && vt && Rd(vt + 0x5F8, fn) && fn >= g_base && fn < g_base + 0x10000000 &&
                    WrBytes(t.actReq + 0x54, &nf, 4) && WrBytes(t.actReq + 0x8C, &newCode, 4) && WrBytes(t.actReq + 0x338, &endState, 1) &&
                    WrBytes(t.actReq + 0x90, &newAct, 4)) {
                    reinterpret_cast<void (*)(void*, uint8_t, uint8_t)>(fn)(reinterpret_cast<void*>(t.cmc), 1, 0);   // walking
                    act = newAct; mode = 1; tailT = -100;
                    if (++nUp <= 30) Log("vault/climb ending cancelled by %s (code %08X, floor %.0f cm)", key && !keyWas ? "button" : "stick", code, fp.dist);
                }
            }
        }
        keyWas = key;
    }
    // free fall, part 2: after a jump-down touches down the game slides the character onto its chosen landing spot
    // while the traversal (type 8) is still running. Undo any movement the game adds on its own in that phase.
    if (false && sc.freeFall) {   // off: it fought the game's own landing pin (frame-by-frame flicker); see TeleportHook
        if (m.prevMode == 3 && mode == 1 && act == 8) { m.snapGuard = true; m.snapT0 = now; memcpy(m.expect, pos, 12); m.haveExpect = true; }
        else if (m.snapGuard && (act != 8 || mode != 1 || now - m.snapT0 > 1.2)) m.snapGuard = false;
        else if (m.snapGuard && m.haveExpect) {
            float ex = pos[0] - m.expect[0], ey = pos[1] - m.expect[1];
            if (ex * ex + ey * ey > 1.5f * 1.5f) {
                float back[3] = { m.expect[0], m.expect[1], pos[2] };
                if (SweepTo(t, pos, back) == SW_CLEAR) { ++m.snapUndos; RdBytes(t.root + t.offRelLoc, pos, 12); memcpy(m.lastPos, pos, 12); }
            }
        }
    }

    // a traversal (climb, vault, jump-down) holds action type 8 until it ends (touchdown); the landing plays after.
    // cooldown: no new traversal for a moment after one ends, so back-to-back ledges don't cut each other short
    if (m.lastAct != 8 && act == 8) m.travZ0 = pos[2];
    if (m.lastAct == 8 && act != 8) {   // small automatic step-downs chain freely: only a real height change earns a cooldown
        if (std::fabs(pos[2] - m.travZ0) >= sc.coolMinH) { m.travEndT = now; if (sc.cooldown > 0.f) ++m.cooldowns; }
        else ++m.smallTrav;
    }
    m.lastAct = act;
    bool cooling = sc.cooldown > 0.f && now - m.travEndT < sc.cooldown;
    // no traversal during the fast part of a roll (roll_block seconds from its start, or until the game ends it)
    float rollAge = m.keyWas ? (float)(now - m.rollT0Block) : 0.f;
    bool rollEarly = dodging && rollAge < sc.rollVaultStart;   // the very start of a roll may still turn into a traversal
    bool rollBlocking = dodging && !sc.rollVault && !rollEarly && (m.keyWas ? rollAge < sc.rollBlock : true);
    g_rollEarly = rollEarly ? 1 : 0;
    g_blockParkour = rollBlocking ? 1 : 0;
    g_coolingNow = cooling ? 1 : 0;
    g_coolParkour = (cooling && kCoolBlock) ? 1 : 0;
    if (dodging && !m.keyWas && mode == 1) {
        m.rollT0 = m.blockRollT0 = m.rollT0Block = now; m.rollZ0 = pos[2]; memcpy(m.lastPos, pos, 12); m.haveLast = true;
        m.standingRoll = m.winSpeed < 120.f;   // rolled from (almost) standing still: the game's own roll is shorter then
        m.dirSet = m.edgeStop = m.pushedThisRoll = m.stretchedThisRoll = m.steppedOff = m.offActive = false; m.offPushed = 0.f; m.peak = 0;
        if (!sc.rollVault) ++m.travBlocks;
    }
    m.keyWas = dodging;

    // wall stop: walking, stick pushed, (almost) not moving, and a wall right ahead in the stick direction. Once stopped,
    // he stays stopped while the stick keeps pointing the same way (within ~30 degrees) and the wall is still there.
    {
        static double since = -1.0; static float latchYaw = 0.f; static bool latched = false;
        static float prevXY[2] = {}; static double prevT = -1.0; static float realSpeed = 999.f;
        static unsigned nEval = 0, nSlow = 0, nWall = 0, nOn = 0; static double lastLog = 0; static float stuckFor = 0.f;
        bool want = false;
        if (prevT > 0 && now - prevT >= 0.03) {
            float dx0 = pos[0] - prevXY[0], dy0 = pos[1] - prevXY[1];
            realSpeed = std::sqrt(dx0 * dx0 + dy0 * dy0) / (float)(now - prevT);
            prevXY[0] = pos[0]; prevXY[1] = pos[1]; prevT = now;
        } else if (prevT < 0) { prevXY[0] = pos[0]; prevXY[1] = pos[1]; prevT = now; }
        float right, fwd; uintptr_t cm = 0; float camYaw = 0;
        if (sc.wallStop && mode == 1 && act == 0 && t.ai && t.offCamMgr >= 0 && t.offCamYaw >= 0 && ReadInput(sc, right, fwd) &&
            Rd(t.pc + t.offCamMgr, cm) && cm && Rd(cm + t.offCamYaw, camYaw)) {
            ++nEval;
            float yaw = camYaw + std::atan2(right, fwd) * 57.2957795f;
            bool sameWay = latched && std::fabs(Wrap(yaw - latchYaw)) < 30.f;
            float rot[3] = {}; bool facing = t.offRelRot >= 0 && RdBytes(t.root + t.offRelRot, rot, 12) && std::fabs(Wrap(rot[1] - yaw)) < 45.f;
            float a = yaw * 0.0174532925f, dx = std::cos(a), dy = std::sin(a), rad = CapsuleRadius(t);
            float sp = realSpeed > 900.f ? 0.f : realSpeed;
            // another character right ahead (party member, NPC): never a wall
            bool charAhead = false;
            {
                int f = g_charFront.load(), n = g_charCount[f].load();
                for (int i = 0; i < n && !charAhead; ++i) {
                    uintptr_t r = g_charRoots[f][i]; float c[3];
                    if (r == t.root || !RdBytes(r + t.offRelLoc, c, 12)) continue;
                    float vx = c[0] - pos[0], vy = c[1] - pos[1], along = vx * dx + vy * dy, lat = std::fabs(vx * -dy + vy * dx);
                    if (along > 0.f && along < rad + 130.f && lat < 75.f && std::fabs(c[2] - pos[2]) < 150.f) charAhead = true;
                }
            }
            float reach = rad + 60.f + sp * 0.15f;
            // only walls taller than half the character count (chest and head height); low obstacles are left to the
            // game's automatic step / vault, so he keeps running into them
            const float hz[3] = { 0.f, 20.f, 60.f };   // capsule centre (just above half his height), chest, head
            for (int k = 0; k < 3 && !want; ++k) {
                float from[3] = { pos[0], pos[1], pos[2] + hz[k] }, to[3] = { pos[0] + dx * reach, pos[1] + dy * reach, pos[2] + hz[k] };
                SegHit h = TraceSeg(t, from, to);
                if (charAhead || !h.hit || std::fabs(h.n[2]) >= 0.7f) continue;
                {   // wide enough to be a wall (not a lamp post, sign or branch): a trace one capsule-width to either side hits too
                    float sx = -dy * (rad * 0.6f), sy = dx * (rad * 0.6f); bool side = false;
                    for (int sgn = -1; sgn <= 1 && !side; sgn += 2) {
                        float f2[3] = { from[0] + sx * sgn, from[1] + sy * sgn, from[2] }, t2[3] = { to[0] + sx * sgn, to[1] + sy * sgn, to[2] };
                        SegHit hs = TraceSeg(t, f2, t2);
                        side = hs.hit && std::fabs(hs.n[2]) < 0.7f;
                    }
                    if (!side) continue;
                }
                float face = h.n[0] * dx + h.n[1] * dy;                      // -1 = wall straight ahead
                float dist = std::sqrt((h.p[0] - pos[0]) * (h.p[0] - pos[0]) + (h.p[1] - pos[1]) * (h.p[1] - pos[1])) - rad;
                if (face < -0.1f && (sp < 80.f || sameWay)) want = true;                          // pressed against it
                else if (face < -0.8f && facing && dist < 10.f + sp * 0.12f) want = true;         // about to run into it head-on
            }
            if (want) ++nWall;
            else if (!charAhead && (sp < 30.f || sameWay) && facing) { stuckFor += dt; if (stuckFor > 0.18f) { want = true; ++nSlow; } }   // blocked by something the trace doesn't see
            else stuckFor = 0.f;
            if (want && !latched) latchYaw = yaw;
        } else stuckFor = 0.f;
        if (want) { if (since < 0) since = now; } else { since = -1.0; latched = false; }
        bool wasLatched = g_wallBlock != 0;
        if (want && now - since > 0.04) { latched = true; ++nOn; }
        g_wallAi.store(t.ai);
        g_wallBlock = latched ? 1 : 0;
        // hand on the wall: a wall reaching head height, the configured animation (if this character has it), held at a pose
        if (latched && !wasLatched && sc.wallHand && t.ai && g_handN > 0) {
            uintptr_t pcls = 0; Rd(t.pawn + OFF_UObject_Class, pcls); std::string pn = ObjName(pcls);
            if (!g_defaultSlotName) g_defaultSlotName = FindDefaultSlot(t.ai);
            float a = latchYaw * 0.0174532925f, dx = std::cos(a), dy = std::sin(a), reach = CapsuleRadius(t) + 70.f;
            const float handZ = -96.f + sc.wallHandMinH;   // the wall must reach this high above his feet ([traversal] wall_hand_min_height)
            float from[3] = { pos[0], pos[1], pos[2] + handZ }, to[3] = { pos[0] + dx * reach, pos[1] + dy * reach, pos[2] + handZ };
            SegHit h = TraceSeg(t, from, to);
            if (h.hit && std::fabs(h.n[2]) < 0.7f) {
                uintptr_t seq = 0;
                for (LONG i = 0; i < g_handN; ++i) if (pn.compare(0, 6, g_handAnims[i].pc) == 0) seq = g_handAnims[i].seq;
                if (seq) {
                    // face the wall squarely so a hands-forward animation meets it
                    float rot[3];
                    if (t.offRelRot >= 0 && RdBytes(t.root + t.offRelRot, rot, 12)) {
                        float nl = std::sqrt(h.n[0] * h.n[0] + h.n[1] * h.n[1]);
                        if (nl > 0.1f) {
                            rot[1] = std::atan2(-h.n[1], -h.n[0]) * 57.2957795f;
                            float hh = rot[1] * 0.00872664626f, quat[4] = { 0.f, 0.f, std::sin(hh), std::cos(hh) };
                            WrBytes(t.root + t.offRelRot, rot, 12); WrBytes(t.root + OFF_Scene_C2W, quat, 16);
                        }
                    }
                    PlayWallHand(t, seq, sc);
                }
            }
        }
        if (!latched && g_handOn) StopWallHand(sc);
        if (kDiag && now - lastLog > 5.0 && nEval) { Log("wall stop: %u pushing, %u wall seen, %u stuck without a wall hit, %u stopped", nEval, nWall, nSlow, nOn); nEval = nSlow = nWall = nOn = 0; lastLog = now; }
    }

    // roll off edges: at an edge the game deflects the roll sideways along it. While the roll is still fast, add the
    // missing part so it keeps its starting direction (collision-checked: a wall stops it); with no floor ahead the
    // character leaves the edge, and the fall keeps the roll's speed.
    if (dodging && mode == 1 && m.haveLast && sc.offLedges && dt > 0.f) {
        float dx = pos[0] - m.lastPos[0], dy = pos[1] - m.lastPos[1], d = std::sqrt(dx * dx + dy * dy), sp = d / dt;
        if (!m.dirSet && sp > 400.f && d < 80.f) { m.dirX = dx / d; m.dirY = dy / d; m.dirSet = true; }
        if (sp > m.peak && d < 80.f) m.peak = sp;
        bool deflected = m.dirSet && !m.edgeStop && sp > 200.f && d < 80.f && (dx * m.dirX + dy * m.dirY) < 0.9f * d;
        if (deflected) {
            // the player steering the roll is not a deflection: follow it (later edge checks use the new direction).
            // Steering = the stick has turned away from the roll's start direction (> 25 deg) and the new movement
            // follows the stick. A stick still pointing where the roll started (at the edge) means the game deflected it.
            float right, fwd; uintptr_t cm = 0; float camYaw = 0;
            if (ReadInput(sc, right, fwd) && t.offCamMgr >= 0 && t.offCamYaw >= 0 && Rd(t.pc + t.offCamMgr, cm) && cm && Rd(cm + t.offCamYaw, camYaw)) {
                float stickYaw = camYaw + std::atan2(right, fwd) * 57.2957795f, moveYaw = std::atan2(dy, dx) * 57.2957795f;
                float startYaw = std::atan2(m.dirY, m.dirX) * 57.2957795f;
                float offStart = std::fabs(Wrap(stickYaw - startYaw)), offMove = std::fabs(Wrap(stickYaw - moveYaw));
                if (offStart > 25.f && offMove < offStart && offMove < 40.f) { m.dirX = dx / d; m.dirY = dy / d; deflected = false; }
            }
        }
        if (deflected) {
            // and only at a real edge: a drop of more than 15 cm somewhere just ahead along the start direction
            // (sampled at three distances, so a sloped lip still counts)
            const float halfH = 96.f; float rad = CapsuleRadius(t), maxDropSeen = -99999.f; bool anyOk = false;
            for (float ahead : { rad + 25.f, rad + 60.f, rad + 100.f }) {
                float p[3] = { pos[0] + m.dirX * ahead, pos[1] + m.dirY * ahead, pos[2] + 50.f };
                TraceHit h = TraceDown(t, p, 50.f + halfH + sc.maxDrop + 100.f);
                if (!h.ok) continue;
                anyOk = true;
                float drop = h.hit ? (pos[2] - halfH) - h.z : 99999.f;
                if (drop > maxDropSeen) maxDropSeen = drop;
            }
            if (anyOk && maxDropSeen <= 15.f) deflected = false;
        }
        if (deflected) {
            float cx = m.dirX * d - dx, cy = m.dirY * d - dy, to[3] = { pos[0] + cx, pos[1] + cy, pos[2] };
            SweepResult r = SW_FAIL;
            if (!DropOk(t, to, m.dirX, m.dirY, CapsuleRadius(t) + 30.f, sc.maxDrop)) { m.edgeStop = true; ++m.cliffStops; }   // big cliff / out of bounds
            else r = SweepTo(t, pos, to);
            if (r == SW_CLEAR) { ++m.edgePushes; m.pushedThisRoll = true; RdBytes(t.root + t.offRelLoc, pos, 12); }
            else if (r == SW_BLOCKED) m.edgeStop = true;      // it's a wall, not an edge
        }
    }
    if (m.prevMode == 1 && mode == 3 && sc.offLedges && m.dirSet && now - m.rollT0 < 1.5) {   // left the ground mid-roll
        float v[3];
        float rad = CapsuleRadius(t);
        if ((m.pushedThisRoll || m.stretchedThisRoll) && !DropOk(t, pos, m.dirX, m.dirY, rad + 40.f, sc.maxDrop) &&
            !DropOk(t, pos, m.dirX, m.dirY, 150.f, sc.maxDrop)) {
            if (SweepTo(t, pos, m.lastPos) == SW_CLEAR) { float z[2] = {}; WrBytes(t.cmc + t.offVel, z, 8); RdBytes(t.root + t.offRelLoc, pos, 12); }
            ++m.cliffStops; m.rollT0 = -100; m.dirSet = false;
        } else if (RdBytes(t.cmc + t.offVel, v, 12)) {
            float want = m.winSpeed; if (want > 800.f) want = 800.f;   // keep the speed he really had (no artificial boost)
            float along = v[0] * m.dirX + v[1] * m.dirY;
            if (along < want) { float nv[2] = { m.dirX * want, m.dirY * want }; WrBytes(t.cmc + t.offVel, nv, 8); ++m.edgeFalls; }
        }
    }
    if (!dodging && now - m.rollT0 > 0.1) m.rollT0 = -100;   // the game ended the roll: stop stretching
    if (now - m.rollT0 < sc.rollTime && pos[2] - m.rollZ0 > sc.rollMaxRise) m.rollT0 = -100;   // climbed onto something: stop stretching (slopes keep going)
    const float rollMul = m.standingRoll ? sc.rollDistStand : sc.rollDist;
    if (now - m.rollT0 < sc.rollTime && mode == 1 && rollMul != 1.f && m.haveLast) {
        float dx = pos[0] - m.lastPos[0], dy = pos[1] - m.lastPos[1], d = std::sqrt(dx * dx + dy * dy);
        if (d > 0.5f && d < 80.f) {
            float k = rollMul - 1.f, up = pos[2] - m.lastPos[2] > 0.f ? (pos[2] - m.lastPos[2]) * k : 0.f;   // uphill: follow the slope
            float to[3] = { pos[0] + dx * k, pos[1] + dy * k, pos[2] + up };
            SweepResult r = SW_FAIL;
            if (!DropOk(t, to, dx / d, dy / d, CapsuleRadius(t) + 30.f, sc.maxDrop)) { ++m.cliffStops; m.rollT0 = -100; }
            HitInfo hi;
            if (r != SW_FAIL || m.rollT0 > -100) r = SweepTo(t, pos, to, &hi);
            if (r == SW_CLEAR) { ++m.rollMoves; m.stretchedThisRoll = true; RdBytes(t.root + t.offRelLoc, pos, 12); }
            else if (r == SW_BLOCKED) {                       // touched something: the rest of this roll is vanilla
                ++m.blocked; m.rollT0 = -100;
                static unsigned logged = 0;
                if (kDiag && logged < 20) { ++logged; Log("roll stretch stopped: hit %d penetrating %d normal Z %.2f", hi.hit, hi.penetrating, hi.nz); }
            }
        }
    }
    // edge carry: rolling at a small cliff, the game brakes the roll to a stop 50-100 cm before the edge (as soon as the
    // drop shows up about 1 m ahead). During the roll, once there is ground 40 cm .. max_drop below 100 cm ahead, keep
    // him moving forward (~700 cm/s, collision-checked, at most 2.2 m) until he leaves the ground; the leave-ground code
    // above then gives the fall the roll's speed.
    if (dodging && mode == 1 && sc.offLedges && m.dirSet && now - m.blockRollT0 < 0.8 && m.offPushed < 220.f && dt > 0.f) {
        if (!m.offActive) {
            const float halfH = 96.f;
            float p[3] = { pos[0] + m.dirX * 100.f, pos[1] + m.dirY * 100.f, pos[2] + 50.f };
            TraceHit h = TraceDown(t, p, 50.f + halfH + sc.maxDrop);
            float drop = h.hit ? (pos[2] - halfH) - h.z : 99999.f;
            if (h.ok && h.hit && drop > 40.f && drop <= sc.maxDrop) { m.offActive = true; ++m.edgeCarries; }
        }
        g_carryOn = m.offActive ? 1 : 0;
        if (m.offActive) {
            float step = 700.f * dt; if (step > 25.f) step = 25.f;
            float to[3] = { pos[0] + m.dirX * step, pos[1] + m.dirY * step, pos[2] };
            HitInfo hi; SweepResult r = SweepTo(t, pos, to, &hi);
            float now3[3]; RdBytes(t.root + t.offRelLoc, now3, 12);
            float mx = now3[0] - pos[0], my = now3[1] - pos[1], moved = std::sqrt(mx * mx + my * my);
            if ((r == SW_CLEAR && moved < 0.5f * step) || (r == SW_BLOCKED && hi.nz > 0.25f)) {
                // the sweep stopped on rising ground at the lip: do the rest of the step 20 cm higher (the game then
                // settles him on the ground or he falls)
                float rest = step - (r == SW_CLEAR ? moved : 0.f);
                const float* from = r == SW_CLEAR ? now3 : pos;
                float up[3] = { from[0] + m.dirX * rest, from[1] + m.dirY * rest, from[2] + 20.f }; HitInfo h2;
                r = SweepTo(t, from, up, &h2); if (r == SW_BLOCKED) hi = h2;
                RdBytes(t.root + t.offRelLoc, now3, 12);
                mx = now3[0] - pos[0]; my = now3[1] - pos[1]; moved = std::sqrt(mx * mx + my * my);
            }
            static unsigned logged = 0;
            if (kDiag && logged < 30) { ++logged; Log("edge carry step: %s %.1f of %.1f cm (hit %d normal Z %.2f)", r == SW_CLEAR ? "moved" : "blocked", moved, step, hi.hit, hi.nz); }
            if (r == SW_CLEAR) { m.offPushed += step; m.pushedThisRoll = true; memcpy(pos, now3, 12); }
            else { m.offActive = false; m.offPushed = 999.f; }   // a real wall: leave it to the game
        }
    }
    if (!(dodging && mode == 1 && m.offActive)) g_carryOn = 0;
    memcpy(m.lastPos, pos, 12); m.haveLast = true;

    // free fall, part 1: a jump-down (type 8, falling) is aimed at a landing spot; above it the game cuts the
    // horizontal speed and drops the character straight down. Keep the speed instead (steering still turns it).
    if (mode == 3 && act == 8) {   // diagnostic: stalled mid-air although the velocity says otherwise -> probe ahead
        float v[3];
        if (RdBytes(t.cmc + t.offVel, v, 12) && g_ffHavePrev && dt > 0.f) {
            float vel = std::sqrt(v[0] * v[0] + v[1] * v[1]);
            float mx = pos[0] - g_ffPrev[0], my = pos[1] - g_ffPrev[1], moved = std::sqrt(mx * mx + my * my) / dt;
            unsigned w = g_diagW.load();
            static const bool kDiagJumpDown = false;   // diagnostic off: it confirmed no collision stops the fall
            if (kDiagJumpDown && vel > 400.f && moved < 0.3f * vel && now - g_diagLast > 0.05 && w < 64) {
                g_diagLast = now;
                float to[3] = { pos[0] + v[0] / vel * 30.f, pos[1] + v[1] / vel * 30.f, pos[2] };
                HitInfo hi; SweepResult r = SweepTo(t, pos, to, &hi);
                DiagEntry& e = g_diag[w];
                e.t = now; e.actor = hi.actorIdx; e.comp = hi.compIdx; e.nz = hi.nz; e.vel = vel; e.moved = moved;
                e.hit = hi.hit; e.pen = hi.penetrating; e.result = (uint8_t)r; memcpy(e.pos, pos, 12);
                g_diagW.store(w + 1);
                if (r == SW_CLEAR) RdBytes(t.root + t.offRelLoc, pos, 12);
            }
        }
        memcpy(g_ffPrev, pos, 12); g_ffHavePrev = true;
    } else g_ffHavePrev = false;
    if (sc.freeFall && mode == 3 && act == 8) {
        float v[3];
        if (RdBytes(t.cmc + t.offVel, v, 12)) {
            float sp = std::sqrt(v[0] * v[0] + v[1] * v[1]), keep = std::sqrt(m.ffV[0] * m.ffV[0] + m.ffV[1] * m.ffV[1]);
            if (!m.ffOn) { if (sp > 300.f) { m.ffOn = true; m.ffV[0] = v[0]; m.ffV[1] = v[1]; } }
            else if (sp < 0.6f * keep) { WrBytes(t.cmc + t.offVel, m.ffV, 8); ++m.ffRestores; }
            else { m.ffV[0] = v[0]; m.ffV[1] = v[1]; }
        }
    } else if (mode != 3) m.ffOn = false;

    // landing momentum: keep part of the air speed for a moment after touching down, toward the stick if pushed
    if (mode == 3) { float v[3]; if (RdBytes(t.cmc + t.offVel, v, 12)) { m.airV[0] = v[0]; m.airV[1] = v[1]; } }
    if (m.prevMode == 3 && mode == 1 && sc.landKeep > 0.f) {
        m.landT0 = now; m.carry[0] = m.airV[0] * sc.landKeep; m.carry[1] = m.airV[1] * sc.landKeep;
    }
    m.prevMode = mode;
    if (mode == 1 && sc.landTime > 0.f && now - m.landT0 < sc.landTime) {
        float cx = m.carry[0], cy = m.carry[1], sp = std::sqrt(cx * cx + cy * cy);
        float right, fwd; uintptr_t cm = 0; float camYaw = 0;
        if (sp > 1.f && ReadInput(sc, right, fwd) && Rd(t.pc + t.offCamMgr, cm) && cm && Rd(cm + t.offCamYaw, camYaw)) {
            float a = (camYaw + std::atan2(right, fwd) * 57.2957795f) * 0.0174532925f;
            cx = std::cos(a) * sp; cy = std::sin(a) * sp;
        }
        float fade = 1.f - (float)((now - m.landT0) / sc.landTime);
        float to[3] = { pos[0] + cx * fade * dt, pos[1] + cy * fade * dt, pos[2] };
        if (sp > 1.f) {
            SweepResult r = SweepTo(t, pos, to);
            if (r == SW_CLEAR) { ++m.landMoves; RdBytes(t.root + t.offRelLoc, m.lastPos, 12); }
            else if (r == SW_BLOCKED) { ++m.blocked; m.landT0 = -100; }
        }
    }
    if (m.snapGuard) RdBytes(t.root + t.offRelLoc, m.expect, 12);   // where we left him: the game shouldn't move him from here
}

static std::atomic<unsigned> g_peCalls{0}, g_peGameCalls{0};

static void RecordIconCall(void* fn) {
    uint32_t nm = 0; Rd(reinterpret_cast<uintptr_t>(fn) + OFF_UObject_Name, nm);
    void* frames[62]; USHORT n = CaptureStackBackTrace(1, 62, frames, nullptr);
    uintptr_t ex[30]; int ne = 0;
    for (USHORT i = 0; i < n && ne < 30; ++i) {
        uintptr_t f = reinterpret_cast<uintptr_t>(frames[i]);
        if (f >= g_base && f < g_base + 0x10000000) ex[ne++] = f - g_base;
    }
    uint64_t key = nm;
    for (int i = 0; i < ne && i < 6; ++i) key = key * 1000003ull ^ ex[i];
    LONG i = 0;
    for (; i < g_icN; ++i) if (g_ic[i].key == key) { ++g_ic[i].n; return; }
    if (g_icN < 64) { IconCall& c = g_ic[g_icN]; c.key = key; c.fnName = nm; c.n = 1; memcpy(c.ex, ex, sizeof(ex)); c.ne = ne; c.logged = 0; InterlockedIncrement(&g_icN); }
}
static void LogIconCalls() {
    LONG n = g_icN;
    for (LONG i = 0; i < n; ++i) {
        IconCall& c = g_ic[i]; if (c.logged) continue; c.logged = 1;
        char st[800]; int q = 0;
        for (int k = 0; k < c.ne && q < 760; ++k) q += snprintf(st + q, sizeof(st) - q, " %llx", (unsigned long long)c.ex[k]);
        st[q] = 0;
        Log("PROMPT #%ld %s  stack:%s", i + 1, NameStr(c.fnName).c_str(), st);
    }
    static unsigned tick = 0, last = 0; unsigned sum = 0;
    for (LONG i = 0; i < n; ++i) sum += g_ic[i].n;
    if (++tick % 5 == 0 && sum != last) {
        last = sum; char b[700]; int q = 0;
        for (LONG i = 0; i < n && q < 660; ++i) q += snprintf(b + q, sizeof(b) - q, " #%ld x%u", i + 1, g_ic[i].n);
        b[q] = 0; Log("PROMPT counts:%s", b);
    }
}

static __declspec(noinline) bool CarrySkip(void* self, void* fn) {
    if (GetCurrentThreadId() != g_gameThread.load()) return false;
    {
        // during the edge carry: skip the game's own position writes on the character (actor or its capsule); the mod's
        // moves call the original ProcessEvent directly and never pass here
        const SteerTargets& ct = g_tg[g_tgFront.load()];
        uintptr_t so = reinterpret_cast<uintptr_t>(self);
        if (so == ct.pawn || so == ct.root) {
            static std::unordered_map<uintptr_t, int8_t> verdict;   // UFunction -> 1 skip / 0 let through
            uintptr_t fo = reinterpret_cast<uintptr_t>(fn);
            auto it = verdict.find(fo); int8_t v;
            if (it == verdict.end()) {
                uint32_t ni = 0; Rd(fo + OFF_UObject_Name, ni); const std::string& n = NameStr(ni);
                v = (Starts(n, "K2_SetActorLocation") || Starts(n, "K2_SetWorldLocation") || Starts(n, "K2_SetActorTransform") ||
                     Starts(n, "K2_SetWorldTransform") || Starts(n, "K2_TeleportTo") || Starts(n, "K2_AddActorWorldOffset") ||
                     Starts(n, "K2_AddWorldOffset") || Starts(n, "K2_SetRelativeLocation")) ? 1 : 0;
                verdict.emplace(fo, v);
                if (kDiag) Log("during edge carry: %s.%s -> %s", so == ct.pawn ? "character" : "capsule", n.c_str(), v ? "skipped" : "allowed");
            } else v = it->second;
            if (v) { g_carrySkips.fetch_add(1); return true; }
        }
    }
    return false;
}

static void HookProcessEvent(void* self, void* fn, void* parms) {
    g_peCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_carryOn && fn && self && CarrySkip(self, fn)) return;
    if (uintptr_t ic = kDiag ? g_iconClass.load() : 0) {
        uintptr_t c = 0;
        if (self && Rd(reinterpret_cast<uintptr_t>(self) + OFF_UObject_Class, c) && c == ic) RecordIconCall(fn);
    }
    if (!t_inTick) {
        DWORD tid = GetCurrentThreadId();
        if (tid == g_gameThread.load()) {   // UE's game thread is the process main thread, which loaded this DLL
            g_peGameCalls.fetch_add(1, std::memory_order_relaxed);
            t_inTick = true;
            __try { GameTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            t_inTick = false;
        }
    }
    g_peOrig(self, fn, parms);
}

// Detour a function whose first `len` (>= 14) bytes are relocatable instructions: they move into a trampoline that
// jumps back, and the function starts with jmp [rip+0] -> hook. Returns the trampoline (= the original function).
static void* Detour(uintptr_t target, const uint8_t* prologue, size_t len, void* hook) {
    uint8_t* t = reinterpret_cast<uint8_t*>(target);
    if (len < 14 || len > 32 || memcmp(t, prologue, len) != 0) return nullptr;
    uint8_t* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    memcpy(tramp, t, len);
    tramp[len] = 0xFF; tramp[len + 1] = 0x25; *reinterpret_cast<uint32_t*>(tramp + len + 2) = 0;      // jmp [rip+0]
    *reinterpret_cast<uint64_t*>(tramp + len + 6) = reinterpret_cast<uint64_t>(t + len);
    uint8_t patch[32];
    memset(patch, 0x90, len);
    patch[0] = 0xFF; patch[1] = 0x25; *reinterpret_cast<uint32_t*>(patch + 2) = 0;
    *reinterpret_cast<uint64_t*>(patch + 6) = reinterpret_cast<uint64_t>(hook);
    DWORD old;
    if (!VirtualProtect(t, len, PAGE_EXECUTE_READWRITE, &old)) return nullptr;
    memcpy(t, patch, len);
    VirtualProtect(t, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, len);
    return tramp;
}

static bool InstallProcessEventHook(uintptr_t base) {
    void* orig = Detour(base + RVA_ProcessEvent, PE_PROLOGUE, sizeof(PE_PROLOGUE), reinterpret_cast<void*>(&HookProcessEvent));
    if (!orig) { Log("ProcessEvent prologue differs - roll/landing movement off"); return false; }
    g_peOrig = reinterpret_cast<ProcessEventFn>(orig);
    Log("ProcessEvent hooked (game-thread tick for roll and landing movement)");
    return true;
}

// ---------------------------------------------------------------- no traversal mid-roll ([traversal] roll_vault=0)
// The parkour update (0x1301444) tries traversal by calling ParkourTry (0x345FCC4): true = a climb/vault/jump-down
// was started; false = the update carries on (with the button pressed: it requests the dodge, 0x130257F). The
// character's action request ([[pawn+0x638]+0x1B8]) holds type 4 for exactly as long as a dodge roll plays. With
// roll_vault=0, ParkourTry answers "no" only while that roll is playing, so a roll can't turn into a vault (by
// button or by rolling into a ledge). Standing next to a ledge, the button still climbs, as in the unmodded game.
typedef bool (*ParkourTryFn)(void*, void*, void*, void*, uint8_t*);
static ParkourTryFn g_tryOrig = nullptr;

static bool HookParkourTry(void* self, void* pawn, void* req, void* dir, uint8_t* wantDodge) {
    if (false && g_coolParkour && g_enabled && GetCurrentThreadId() == g_gameThread.load()) {   // 1.3: cooldown = automatic traversal only
        if (wantDodge) *wantDodge = 0;                      // no traversal, and the button doesn't turn into a dodge either
        return false;
    }
    if (g_enabled && !g_steer.rollVault && GetCurrentThreadId() == g_gameThread.load()) {
        const SteerTargets& t = g_tg[g_tgFront.load()];
        uint32_t act = 0;
        (void)pawn;
        if (t.actReq && Rd(t.actReq + 0x90, act) && act == 4 && !g_rollEarly) {
            static double lastCounted = -100;
            if (g_ms.rollT0 != lastCounted) { ++g_ms.travBlocks; lastCounted = g_ms.rollT0; }
            return false;                                   // a roll is playing: no traversal until it ends
        }
    }
    return g_tryOrig(self, pawn, req, dir, wantDodge);
}

// The parkour update itself (P, 0x1301444) also starts AUTOMATIC traversal directly (not through ParkourTry), so it
// is gated too: a register-preserving stub returns 0 ("nothing started") while g_blockParkour is set (a roll is
// playing and roll_vault=0), otherwise it jumps to the original. Opening: mov [rsp+18h],rbx; mov [rsp+20h],rsi;
// push rdi/r12-r15; mov eax,340h = 24 relocatable bytes.
static void InstallParkourUpdateGate(uintptr_t base) {
    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) return;
    void* orig = Detour(base + RVA_ParkourUpdate, PU_PROLOGUE, sizeof(PU_PROLOGUE), stub);
    if (!orig) { VirtualFree(stub, 0, MEM_RELEASE); Log("parkour update differs - traversal blocking during rolls off"); return; }
    uint8_t* p = stub;
    *p++ = 0x48; *p++ = 0xB8; *reinterpret_cast<uint64_t*>(p) = reinterpret_cast<uint64_t>(&g_blockParkour); p += 8;   // mov rax,&flag
    *p++ = 0x80; *p++ = 0x38; *p++ = 0x00;                                                                              // cmp byte [rax],0
    *p++ = 0x75; *p++ = 0x0C;                                                                                           // jne skip
    *p++ = 0x48; *p++ = 0xB8; *reinterpret_cast<uint64_t*>(p) = reinterpret_cast<uint64_t>(orig); p += 8;               // mov rax,orig
    *p++ = 0xFF; *p++ = 0xE0;                                                                                           // jmp rax
    *p++ = 0x31; *p++ = 0xC0; *p++ = 0xC3;                                                                              // skip: xor eax,eax; ret
    FlushInstructionCache(GetCurrentProcess(), stub, 64);
    Log("parkour update gated (no traversal while a roll plays)");
}

// The parkour update starts AUTOMATIC traversal (running off a small ledge) at 0x1301F93:
//   lea rdx,[rsp+110h] ; mov rcx,rsi ; call 0x1511E94 ; jmp 0x13028AD (= "started")
// During the cooldown that call is skipped and the update carries on at 0x1301FD8 as if no traversal was found.
static void InstallAutoTraversalGate(uintptr_t base) {
    uint8_t* t = reinterpret_cast<uint8_t*>(base + RVA_AutoTrav);
    if (memcmp(t, AT_BYTES, sizeof(AT_BYTES)) != 0) { Log("automatic traversal call differs - cooldown covers button traversal only"); return; }
    uint8_t* st = static_cast<uint8_t*>(VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!st) return;
    uint8_t* p = st;
    auto abs64 = [&](uint64_t v) { *reinterpret_cast<uint64_t*>(p) = v; p += 8; };
    *p++ = 0x48; *p++ = 0xB8; abs64(reinterpret_cast<uint64_t>(&g_coolParkour));          // mov rax,&g_coolParkour
    *p++ = 0x80; *p++ = 0x38; *p++ = 0x00;                                               // cmp byte [rax],0
    *p++ = 0x75; *p++ = 52;                                                              // jne blocked (15 + 37 bytes on)
    *p++ = 0x48; *p++ = 0xB8; abs64(reinterpret_cast<uint64_t>(&g_sideOnly));            // mov rax,&g_sideOnly
    *p++ = 0x80; *p++ = 0x38; *p++ = 0x00;                                               // cmp byte [rax],0
    *p++ = 0x75; *p++ = 37;                                                              // jne blocked
    memcpy(p, AT_BYTES, 11); p += 11;                                                    // lea rdx,[rsp+110h] ; mov rcx,rsi
    *p++ = 0x48; *p++ = 0xB8; abs64(base + RVA_ActRequest);                              // mov rax,0x1511E94
    *p++ = 0xFF; *p++ = 0xD0;                                                            // call rax
    *p++ = 0xFF; *p++ = 0x25; *reinterpret_cast<uint32_t*>(p) = 0; p += 4; abs64(base + RVA_AutoTravYes);   // jmp 0x13028AD
    *p++ = 0x45; *p++ = 0x31; *p++ = 0xD2;                                               // blocked: xor r10d,r10d
    *p++ = 0xFF; *p++ = 0x25; *reinterpret_cast<uint32_t*>(p) = 0; p += 4; abs64(base + RVA_AutoTravNo);    // jmp 0x1301FD8
    FlushInstructionCache(GetCurrentProcess(), st, 128);
    uint8_t patch[21]; memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xFF; patch[1] = 0x25; memset(patch + 2, 0, 4); *reinterpret_cast<uint64_t*>(patch + 6) = reinterpret_cast<uint64_t>(st);
    DWORD old; if (!VirtualProtect(t, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(t, patch, sizeof(patch));
    VirtualProtect(t, sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, sizeof(patch));
    Log("automatic traversal gated (cooldown, side-found ledges)");
}

// Diagnostic: every action request (0x1511E94(queue, request*); requests are polymorphic, vtable first) on the game
// thread is recorded by class (vtable RVA, RTTI name), caller and action type, counted inside / outside the cooldown.
typedef uint64_t (*ActReqFn)(void*, void*);
static ActReqFn g_qOrig = nullptr;
static const uint8_t Q_PROLOGUE[15] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20 };
struct ReqSeen { uintptr_t vt, caller; unsigned n, nCool; uint32_t act; uint8_t logged; };
static ReqSeen g_rq[64]; static unsigned g_rqN = 0;
static uint64_t HookActReq(void* q, void* req) {
    if (req && GetCurrentThreadId() == g_gameThread.load()) {
        uintptr_t vt = 0; Rd(reinterpret_cast<uintptr_t>(req), vt);
        uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
        uintptr_t caller = (ra >= g_base && ra < g_base + 0x10000000) ? ra - g_base : 0;
        uintptr_t v = (vt >= g_base && vt < g_base + 0x10000000) ? vt - g_base : vt;
        const SteerTargets& t = g_tg[g_tgFront.load()];
        uint32_t act = 0; if (t.actReq) Rd(t.actReq + 0x90, act);
        unsigned i = 0;
        for (; i < g_rqN; ++i) if (g_rq[i].vt == v && g_rq[i].caller == caller) break;
        if (i == g_rqN && g_rqN < 64) { g_rq[i] = { v, caller, 0, 0, act, 0 }; ++g_rqN; }
        if (i < g_rqN) { ++g_rq[i].n; if (g_coolingNow) ++g_rq[i].nCool; }
    }
    return g_qOrig(q, req);
}
static std::string RttiName(uintptr_t vtRva) {
    uintptr_t col = 0; if (!Rd(g_base + vtRva - 8, col) || !col) return "?";
    int32_t tdRva = 0; if (!Rd(col + 0xC, tdRva) || tdRva <= 0) return "?";
    char buf[96] = {}; RdBytes(g_base + tdRva + 0x10, buf, 95);
    return buf[0] ? std::string(buf) : std::string("?");
}
static void LogRequests() {
    static unsigned tick = 0; if (++tick % 5) return;
    for (unsigned i = 0; i < g_rqN; ++i) {
        ReqSeen& r = g_rq[i];
        if (!r.logged) { r.logged = 1; Log("REQ new: %s (vtable %llx) from %llx, action type %u when first seen", RttiName(r.vt).c_str(), (unsigned long long)r.vt, (unsigned long long)r.caller, r.act); }
    }
    static unsigned lastSum = 0; unsigned sum = 0; for (unsigned i = 0; i < g_rqN; ++i) sum += g_rq[i].n;
    if (sum == lastSum) return; lastSum = sum;
    char buf[900]; int q = 0;
    for (unsigned i = 0; i < g_rqN && q < 860; ++i) q += snprintf(buf + q, sizeof(buf) - q, " [%llx from %llx: %u, %u in cooldown]", (unsigned long long)g_rq[i].vt, (unsigned long long)g_rq[i].caller, g_rq[i].n, g_rq[i].nCool);
    buf[q] = 0;
    Log("REQ counts:%s", buf);
}
static void InstallActReqHook(uintptr_t base) {
    void* orig = Detour(base + RVA_ActRequest, Q_PROLOGUE, sizeof(Q_PROLOGUE), reinterpret_cast<void*>(&HookActReq));
    if (!orig) { Log("action request function differs - request log off"); return; }
    g_qOrig = reinterpret_cast<ActReqFn>(orig);
    Log("action requests logged (diagnostic)");
}

// ---------------------------------------------------------------- prompt distance ([traversal] prompt_reach)
// The per-frame parkour detection (0x12FEEAC -> 0x12FEAE4 -> 0x13036A4) looks for a traversable edge ahead of the
// character; how far ahead is a float argument loaded at 0x12FF4F4 (vmovss xmm1,[rsp+368h]). It decides both when the
// Acrobacias prompt shows and when the button can start a traversal. A stub keeps the game's value, records it, and
// multiplies it by prompt_reach (1 = normal, smaller = the prompt appears closer to the edge).
static void InstallPromptReach(uintptr_t base) {
    uint8_t* t = reinterpret_cast<uint8_t*>(base + RVA_PromptReach);
    if (memcmp(t, PR_ORIG, sizeof(PR_ORIG)) != 0) { Log("prompt reach instruction differs - prompt_reach off"); return; }
    uint8_t* st = nullptr;
    for (int k = 1; k < 4096 && !st; ++k)   // within +-2 GB of the code (rel32 jumps)
        st = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(base + 0x9800000 + (uintptr_t)k * 0x10000), 0x1000,
                                                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!st) { Log("no memory near the game code - prompt_reach off"); return; }
    g_prSeen = reinterpret_cast<float*>(st + 0x40); g_prMul = reinterpret_cast<float*>(st + 0x44);
    *g_prSeen = 0.f; *g_prMul = g_steer.promptReach;
    uint8_t* p = st;
    auto rel = [&](uint8_t* target, int insnLen) { return (int32_t)(target - (p + insnLen)); };
    memcpy(p, PR_ORIG, 9); p += 9;                                                            // vmovss xmm1,[rsp+368h]
    { int32_t d = rel(st + 0x40, 8); uint8_t i[4] = { 0xC5, 0xFA, 0x11, 0x0D }; memcpy(p, i, 4); memcpy(p + 4, &d, 4); p += 8; }   // vmovss [seen],xmm1
    { int32_t d = rel(st + 0x44, 8); uint8_t i[4] = { 0xC5, 0xF2, 0x59, 0x0D }; memcpy(p, i, 4); memcpy(p + 4, &d, 4); p += 8; }   // vmulss xmm1,xmm1,[mul]
    { int32_t d = (int32_t)((t + 9) - (p + 5)); *p = 0xE9; memcpy(p + 1, &d, 4); p += 5; }                                         // jmp back
    FlushInstructionCache(GetCurrentProcess(), st, 0x40);
    uint8_t patch[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    int32_t d = (int32_t)(st - (t + 5)); memcpy(patch + 1, &d, 4);
    DWORD old; if (!VirtualProtect(t, 9, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(t, patch, 9);
    VirtualProtect(t, 9, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 9);
    Log("prompt reach hooked (prompt_reach x%.2f)", g_steer.promptReach);
}
static void PromptReachTick() {
    if (!g_prMul) return;
    static float lastBase = -1.f;
    float base = g_enabled ? g_steer.promptReach : 1.f;   // logged only when the setting changes, not per wall pose
    if (base != lastBase) { lastBase = base; Log("prompt reach multiplier now %.2f (%.2f in the hands-on-wall pose)", base, g_enabled ? g_steer.wallHandPromptReach : 1.f); }
    float want = PromptMulWanted();
    if (*g_prMul != want) *g_prMul = want;
    static float lastSeen = -1.f; static unsigned tick = 0;
    if (++tick % 10 == 0 && *g_prSeen != lastSeen) { lastSeen = *g_prSeen; Log("prompt reach: game value %.4f, used %.4f", lastSeen, lastSeen * want); }
}

static void InstallParkourTryHook(uintptr_t base) {
    void* orig = Detour(base + RVA_ParkourTry, TRY_PROLOGUE, sizeof(TRY_PROLOGUE), reinterpret_cast<void*>(&HookParkourTry));
    if (!orig) { Log("parkour check differs - roll_vault setting off"); return; }
    g_tryOrig = reinterpret_cast<ParkourTryFn>(orig);
    Log("parkour check hooked (roll_vault)");
}

// ---------------------------------------------------------------- wider ledge search ([traversal] side_angle)
// The ledge detection (0x13036A4) looks ahead of the character along its facing: arg 1 is the character's FTransform
// (rotation quat at +0, translation +0x10, scale +0x20), xmm1 the forward reach. When it finds nothing straight ahead,
// it is asked again with the facing turned side_angle degrees right, then left.
typedef bool (*LedgeDetectFn)(const float*, float, void*, void*, void*, void*, void*);
static LedgeDetectFn g_detOrig = nullptr;
static std::atomic<unsigned> g_detCalls{0}, g_detSideHits{0};
static std::atomic<long long> g_detTicks{0};

static bool HookLedgeDetect(const float* xf, float reach, void* out, void* a4, void* a5, void* a6, void* a7) {
    if (g_detOrig(xf, reach, out, a4, a5, a6, a7)) { g_sideOnly = 0; return true; }
    g_sideOnly = 0;
    const float ang = g_steer.sideAngle;
    if (!g_enabled || !(ang > 0.f) || !xf) return false;
    LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
    ++g_detCalls;
    alignas(16) float r[12]; memcpy(r, xf, sizeof(r));
    bool hit = false; float used = 0.f;
    for (float a : { ang, -ang }) {
        float h = a * 0.5f * 0.0174532925f, s = std::sin(h), c = std::cos(h);   // yaw about world Z: q' = (0,0,s,c) * q
        const float x = xf[0], y = xf[1], z = xf[2], w = xf[3];
        r[0] = c * x - s * y; r[1] = c * y + s * x; r[2] = c * z + s * w; r[3] = c * w - s * z;
        if (g_detOrig(r, reach, out, a4, a5, a6, a7)) { hit = true; used = a; break; }
    }
    QueryPerformanceCounter(&t1); g_detTicks += t1.QuadPart - t0.QuadPart;
    if (hit) {
        g_sideOnly = 1;
        unsigned n = ++g_detSideHits;
        if (n <= 20) Log("ledge found %+.0f deg to the side", used);
    }
    return hit;
}

static void InstallLedgeDetectHook(uintptr_t base) {
    void* orig = Detour(base + RVA_LedgeDetect, LD_PROLOGUE, sizeof(LD_PROLOGUE), reinterpret_cast<void*>(&HookLedgeDetect));
    if (!orig) { Log("ledge search differs - side_angle setting off"); return; }
    g_detOrig = reinterpret_cast<LedgeDetectFn>(orig);
    Log("ledge search hooked (side_angle)");
}

// ---------------------------------------------------------------- diagnostics: who starts traversal moves?
// UCharacterMovementComponent::SetMovementMode is virtual (vtable +0x5F8; found via the SetMovementMode UFunction's
// native thunk). The slot in the player's EndCharacterMovementComponent vtable is swapped for a logger that records
// the call stack whenever the player is switched to flying (mode 5 = vaults/climbs) or falling (3), then forwards.
typedef void (*SetMoveModeFn)(void*, uint8_t, uint8_t);
static SetMoveModeFn g_smmOrig = nullptr;
static constexpr int VT_SetMovementMode = 0x5F8;
static unsigned g_smmLogged = 0;

static void HookSetMoveMode(void* self, uint8_t mode, uint8_t custom) {
    const SteerTargets& t = g_tg[g_tgFront.load()];
    if ((mode == 5 || mode == 3) && reinterpret_cast<uintptr_t>(self) == t.cmc && g_smmLogged < 200) {
        ++g_smmLogged;
        void* frames[20]; USHORT n = CaptureStackBackTrace(1, 20, frames, nullptr);
        char buf[600]; int p = 0;
        for (USHORT i = 0; i < n && p < 560; ++i) {
            uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
            if (a >= g_base && a < g_base + 0x10000000) p += snprintf(buf + p, sizeof(buf) - p, " %llx", (unsigned long long)(a - g_base));
            else p += snprintf(buf + p, sizeof(buf) - p, " ?");
        }
        buf[p] = 0;
        LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
        double now = (double)c.QuadPart / (double)f.QuadPart;
        Log("SetMovementMode %d (%.2fs after last Space press) stack:%s", mode, now - g_ms.blockRollT0, buf);
    }
    g_smmOrig(self, mode, custom);
}

static void InstallMoveModeTrace(uintptr_t cmc) {
    static bool done = false;
    if (done || !cmc) return;
    uintptr_t vt = 0; if (!Rd(cmc, vt) || !vt) return;
    void** slot = reinterpret_cast<void**>(vt + VT_SetMovementMode);
    void* orig = nullptr; if (!Rd(reinterpret_cast<uintptr_t>(slot), orig) || !orig) return;
    DWORD old;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) return;
    g_smmOrig = reinterpret_cast<SetMoveModeFn>(orig);
    *slot = reinterpret_cast<void*>(&HookSetMoveMode);
    VirtualProtect(slot, 8, old, &old);
    done = true;
    Log("tracing SetMovementMode (vtable %llx slot +0x%X, original %llx)", (unsigned long long)(vt - g_base), VT_SetMovementMode,
        (unsigned long long)(reinterpret_cast<uintptr_t>(orig) - g_base));
}

// ---------------------------------------------------------------- diagnostics: who requests the Space vault?
// The input-vault controller's state handler F (0x16A2B40) is called through a function-pointer table in .data
// (entry RVA 0x6EECAB0) as handler->vtable[0](handler, state, event, ...). A wrapper in that entry finds the handler
// object H on the first call, then the state-machine struct S (a struct holding H at +0x10; +8 flags, +9 current,
// +0xA next, +0xB previous state). A hardware write watchpoint (DR0, 1 byte) on S+0xA on the game thread then logs
// every write: the writing instruction and the call stack, which leads back to the code that decided "vault".
static constexpr uintptr_t RVA_F = 0x16A2B40, RVA_F_ENTRY = 0x6EECAB0;
static volatile uintptr_t g_lastH = 0;                 // written by the stub on every call of F
static std::atomic<uintptr_t> g_vaultH{0}, g_vaultS{0};
static std::atomic<unsigned> g_wpHits{0};

// ---------------------------------------------------------------- diagnostics: who writes the position in a jump-down?
// A hardware write watchpoint (DR1, 4 bytes) on the player root component's RelativeLocation.X. The writer stores X and Y
// together (vmovsd), so at the trap the horizontal position is current. The handler records every writer that moves
// the character more than 40 cm in one write (a teleport), in any state, with its caller chain.
extern "C" IMAGE_DOS_HEADER __ImageBase;
struct PosWriter { uint64_t key; uintptr_t rip; unsigned count; float maxD; uint32_t act; uint8_t mode, mod, logged; uintptr_t ex[12]; int ne; float from[2], to[2]; };
static PosWriter g_pw[48]; static volatile LONG g_pwN = 0; static volatile LONG g_pwLock = 0;
static uintptr_t g_pwAddr = 0; static float g_pwPrev[2] = {}; static bool g_pwHavePrev = false;
static volatile unsigned g_pwHitsAll = 0, g_pwHitsOther = 0;

static LONG CALLBACK PosWatchVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 2)) return EXCEPTION_CONTINUE_SEARCH;   // DR1
    c->Dr6 = 0;
    ++g_pwHitsAll;
    if (GetCurrentThreadId() != g_gameThread.load()) ++g_pwHitsOther;
    while (InterlockedCompareExchange(&g_pwLock, 1, 0) != 0) YieldProcessor();
    float p[2] = {}; RdBytes(g_pwAddr, p, 8);
    float dx = p[0] - g_pwPrev[0], dy = p[1] - g_pwPrev[1], d = std::sqrt(dx * dx + dy * dy);
    bool had = g_pwHavePrev; float from[2] = { g_pwPrev[0], g_pwPrev[1] };
    g_pwPrev[0] = p[0]; g_pwPrev[1] = p[1]; g_pwHavePrev = true;
    if (had && d > 40.f && d < 5000.f) {
        const SteerTargets& t = g_tg[g_tgFront.load()];
        uint32_t act = 0; uint8_t mode = 0;
        if (t.actReq) Rd(t.actReq + 0x90, act);
        if (t.cmc) Rd(t.cmc + t.offMode, mode);
        uintptr_t me = reinterpret_cast<uintptr_t>(&__ImageBase);
        uint32_t meSize = reinterpret_cast<IMAGE_NT_HEADERS*>(me + __ImageBase.e_lfanew)->OptionalHeader.SizeOfImage;
        void* frames[32]; USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
        uintptr_t ex[12]; int ne = 0; uint8_t mod = 0;
        for (USHORT i = 0; i < n; ++i) {
            uintptr_t f = reinterpret_cast<uintptr_t>(frames[i]);
            if (f >= me && f < me + meSize) mod = 1;
            else if (f >= g_base && f < g_base + 0x10000000 && ne < 12) ex[ne++] = f - g_base;
        }
        uint64_t key = (c->Rip - g_base) ^ mod;
        for (int i = 0; i < ne && i < 6; ++i) key = key * 1000003ull ^ ex[i];
        bool found = false;
        for (LONG i = 0; i < g_pwN; ++i) if (g_pw[i].key == key) { ++g_pw[i].count; if (d > g_pw[i].maxD) g_pw[i].maxD = d; found = true; break; }
        if (!found && g_pwN < 48) {
            PosWriter& w = g_pw[g_pwN];
            w.key = key; w.rip = c->Rip - g_base; w.count = 1; w.maxD = d; w.act = act; w.mode = mode; w.mod = mod; w.logged = 0;
            memcpy(w.ex, ex, sizeof(ex)); w.ne = ne; w.from[0] = from[0]; w.from[1] = from[1]; w.to[0] = p[0]; w.to[1] = p[1];
            InterlockedIncrement(&g_pwN);
        }
    }
    InterlockedExchange(&g_pwLock, 0);
    return EXCEPTION_CONTINUE_EXECUTION;
}

// ---------------------------------------------------------------- diagnostic: file-driven hardware watchpoint (DR2)
// Every second the main loop reads TraversalSpeed.watch (game folder): "<address hex> <len 1|2|4|8> <w|rw>" arms DR2 on every
// thread of the process (re-applied every 5 s for new threads); "off" or an empty file disarms. Each distinct accessor
// (instruction + 6 caller frames) is recorded with a count and its first 12 frames; the main loop logs new ones.
struct WatchHit { uint64_t key; uintptr_t rip; unsigned n; uintptr_t ex[30]; int ne; uint64_t rcx, rdx, r8, rbx, rsi, rdi; uint8_t logged; };
static WatchHit g_wh[64]; static volatile LONG g_whN = 0, g_whLock = 0;
static uintptr_t g_wAddr = 0; static int g_wLen = 0; static bool g_wRW = false; static bool g_wVeh = false; static bool g_wExec = false;
static volatile unsigned g_wHits = 0;

static LONG CALLBACK WatchVeh2(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 4)) return EXCEPTION_CONTINUE_SEARCH;   // DR2
    c->Dr6 &= ~4ull;
    // decide from the hit itself, not from globals that may be changing: an execute breakpoint (RW2 = 00) always resumes
    // with RF set, and a breakpoint that is no longer wanted is cleared in the context the thread resumes with (a
    // concurrent SetThreadContext would otherwise be undone when this context is restored)
    if (((c->Dr7 >> 24) & 3) == 0) c->EFlags |= 0x10000;
    if (!g_wAddr || c->Dr2 != g_wAddr) { c->Dr7 &= ~((0xFull << 24) | (3ull << 4)); return EXCEPTION_CONTINUE_EXECUTION; }
    ++g_wHits;
    while (InterlockedCompareExchange(&g_whLock, 1, 0) != 0) YieldProcessor();
    void* frames[62]; USHORT n = CaptureStackBackTrace(0, 62, frames, nullptr);
    uintptr_t ex[30]; int ne = 0;
    for (USHORT i = 0; i < n && ne < 30; ++i) {
        uintptr_t f = reinterpret_cast<uintptr_t>(frames[i]);
        if (f >= g_base && f < g_base + 0x10000000) ex[ne++] = f - g_base;
    }
    uintptr_t rip = c->Rip >= g_base && c->Rip < g_base + 0x10000000 ? c->Rip - g_base : c->Rip;
    uint64_t key = rip;
    for (int i = 0; i < ne && i < 6; ++i) key = key * 1000003ull ^ ex[i];
    LONG i = 0;
    for (; i < g_whN; ++i) if (g_wh[i].key == key) { ++g_wh[i].n; break; }
    if (i == g_whN && g_whN < 64) {
        WatchHit& w = g_wh[i];
        w.key = key; w.rip = rip; w.n = 1; memcpy(w.ex, ex, sizeof(ex)); w.ne = ne; w.logged = 0;
        w.rcx = c->Rcx; w.rdx = c->Rdx; w.r8 = c->R8; w.rbx = c->Rbx; w.rsi = c->Rsi; w.rdi = c->Rdi;
        InterlockedIncrement(&g_whN);
    }
    InterlockedExchange(&g_whLock, 0);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void ApplyDr2(uintptr_t addr, int len, bool rw, bool exec = false) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {}; te.dwSize = sizeof(te);
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    uint64_t lenBits = exec ? 0 : (len == 8 ? 2 : len == 4 ? 3 : len == 2 ? 1 : 0), rwBits = exec ? 0 : (rw ? 3 : 1);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        SuspendThread(th);
        CONTEXT ctx = {}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(th, &ctx)) {
            ctx.Dr7 &= ~((0xFull << 24) | (3ull << 4));
            if (addr) { ctx.Dr2 = addr; ctx.Dr7 |= (1ull << 4) | (rwBits << 24) | (lenBits << 26); }
            else ctx.Dr2 = 0;
            SetThreadContext(th, &ctx);
        }
        ResumeThread(th); CloseHandle(th);
    }
    CloseHandle(snap);
}

static void WatchFileTick() {
    static unsigned tick = 0; ++tick;
    wchar_t path[MAX_PATH]; IniPath(path, L"TraversalSpeed.watch");
    char buf[128] = {};
    FILE* f = _wfopen(path, L"rb");
    if (f) { fread(buf, 1, sizeof(buf) - 1, f); fclose(f); }
    unsigned long long a = 0; int len = 4; char mode[8] = "w";
    bool on = f && sscanf(buf, "%llx %d %7s", &a, &len, mode) >= 1 && a > 0x10000;
    if (!on) a = 0;
    bool rw = !strcmp(mode, "rw"), ex = !strcmp(mode, "x");
    if (ex) len = 1;
    if (len != 1 && len != 2 && len != 4 && len != 8) len = 4;
    if (a & (len - 1)) a &= ~(unsigned long long)(len - 1);
    bool changed = a != g_wAddr || len != g_wLen || rw != g_wRW || ex != g_wExec;
    if (changed) {
        if (a && !g_wVeh) { AddVectoredExceptionHandler(1, WatchVeh2); g_wVeh = true; }
        g_wAddr = (uintptr_t)a; g_wLen = len; g_wRW = rw; g_wExec = ex;
        while (InterlockedCompareExchange(&g_whLock, 1, 0) != 0) YieldProcessor();
        g_whN = 0; InterlockedExchange(&g_whLock, 0);
        g_wHits = 0;
        ApplyDr2(g_wAddr, len, rw, ex);
        if (a) Log("WATCH armed on %llx (%d bytes, %s)", a, len, ex ? "execute" : rw ? "read/write" : "write"); else Log("WATCH off");
    } else if (g_wAddr && tick % 5 == 0) ApplyDr2(g_wAddr, g_wLen, g_wRW, g_wExec);   // threads created since
    if (!g_wAddr) return;
    LONG n = g_whN;
    for (LONG i = 0; i < n; ++i) {
        WatchHit& w = g_wh[i]; if (w.logged) continue; w.logged = 1;
        char st[800]; int q = 0;
        for (int k = 0; k < w.ne && q < 760; ++k) q += snprintf(st + q, sizeof(st) - q, " %llx", (unsigned long long)w.ex[k]);
        st[q] = 0;
        Log("WATCH #%ld rip %llx  rcx %llx rdx %llx r8 %llx rbx %llx rsi %llx rdi %llx  stack:%s", i + 1, (unsigned long long)w.rip,
            w.rcx, w.rdx, w.r8, w.rbx, w.rsi, w.rdi, st);
    }
    static unsigned lastHits = 0;
    if (tick % 5 == 0 && g_wHits != lastHits) {
        lastHits = g_wHits;
        char bufc[600]; int q = 0;
        for (LONG i = 0; i < n && q < 560; ++i) q += snprintf(bufc + q, sizeof(bufc) - q, " #%ld x%u", i + 1, g_wh[i].n);
        bufc[q] = 0;
        Log("WATCH counts (%u hits):%s", g_wHits, bufc);
    }
}

static const bool kPosWatch = false;   // diagnostic: hardware watchpoint on the player position (found the arrival teleport)
static void ArmPosWatch() {
    static bool vehAdded = false;
    const SteerTargets& t = g_tg[g_tgFront.load()];
    if (!t.root || t.offRelLoc < 0 || !t.actReq || !g_gameThread.load()) return;
    if (t.root + t.offRelLoc == g_pwAddr) return;            // already watching this character; re-arm when it changes
    g_pwAddr = t.root + t.offRelLoc; g_pwHavePrev = false;
    if (!vehAdded) { AddVectoredExceptionHandler(1, PosWatchVeh); vehAdded = true; }
    // set DR1 on every thread of the process except this one (a worker thread might be the writer)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {}; te.dwSize = sizeof(te);
    unsigned armedN = 0; DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        SuspendThread(th);
        CONTEXT ctx = {}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(th, &ctx)) {
            ctx.Dr1 = g_pwAddr;
            ctx.Dr7 = (ctx.Dr7 & ~(0xFull << 20)) | (1ull << 2) | (1ull << 20) | (3ull << 22);   // L1, RW1 = write, LEN1 = 4 bytes
            if (SetThreadContext(th, &ctx)) ++armedN;
        }
        ResumeThread(th); CloseHandle(th);
    }
    CloseHandle(snap);
    Log("position watchpoint armed on %llx (root RelativeLocation.X) in %u threads", (unsigned long long)g_pwAddr, armedN);
}

static void LogPosWriters() {
    static unsigned lastTotal = 0, lastAll = 0, tick = 0; unsigned total = 0;
    if (++tick % 15 == 0 && g_pwHitsAll != lastAll) {
        Log("position watch: %u writes in total (%u from non-game threads)", g_pwHitsAll, g_pwHitsOther); lastAll = g_pwHitsAll;
    }
    LONG n = g_pwN;
    for (LONG i = 0; i < n; ++i) {
        PosWriter& w = g_pw[i]; total += w.count;
        if (w.logged) continue;
        w.logged = 1;
        char buf[400]; int q = 0;
        for (int k = 0; k < w.ne && q < 360; ++k) q += snprintf(buf + q, sizeof(buf) - q, " %llx", (unsigned long long)w.ex[k]);
        buf[q] = 0;
        Log("TELEPORT #%ld %.0f cm (%s) act %u mode %u: %.0f %.0f -> %.0f %.0f  rip %llx  stack:%s", i + 1, w.maxD,
            w.mod ? "by this mod" : "by the game", w.act, w.mode, w.from[0], w.from[1], w.to[0], w.to[1], (unsigned long long)w.rip, buf);
    }
    if (total == lastTotal) return;
    lastTotal = total;
    char buf[600]; int q = 0;
    for (LONG i = 0; i < n && q < 560; ++i) q += snprintf(buf + q, sizeof(buf) - q, " #%ld x%u max %.0f", i + 1, g_pw[i].count, g_pw[i].maxD);
    buf[q] = 0;
    Log("TELEPORT counts:%s", buf);
}

// ---------------------------------------------------------------- free landing ([momentum] free_fall=1)
// A jump-down is a "target jump" aimed at a landing point authored in the level. When it ends, the game teleports the
// character onto that point (0x1940E14 = teleport actor (actor, FTransform*, reason FName*), reason
// FieldTargetJumpForceArrivalTeleport / ...NoNavi), up to 2 m when steering or landing momentum took him elsewhere.
// With free_fall on, that arrival teleport is skipped for the player, so he stays where he actually landed.
// FieldTargetJumpCancelTeleport (the game putting him back when a jump is cancelled) is left alone.
typedef void (*TeleportFn)(void*, const void*, const uint32_t*);
static TeleportFn g_tpOrig = nullptr;
static std::atomic<unsigned> g_tpSkipped{0}, g_tpPassed{0}, g_tpKept{0};
static uint32_t g_tpSeen[16]; static unsigned g_tpSeenN = 0; static uint32_t g_tpSeenKind[16];

// 0 = other, 1 = target-jump arrival, 2 = target-jump cancel (read straight from the name pool: no shared cache)
static int TeleportKind(uint32_t idx) {
    for (unsigned i = 0; i < g_tpSeenN; ++i) if (g_tpSeen[i] == idx) return (int)g_tpSeenKind[i];
    char buf[64] = {}; int kind = 0;
    uintptr_t block = 0;
    if (Rd(g_base + RVA_NameBlocks + 8 * (idx >> 16), block) && block) {
        uintptr_t e = block + 4 * (idx & 0xFFFF) + 4; uint16_t h = 0;
        if (Rd(e, h) && !(h & 1)) { size_t len = h >> 6; if (len > 63) len = 63; RdBytes(e + 2, buf, len); }
    }
    if (!strncmp(buf, "FieldTargetJumpForceArrivalTeleport", 35)) kind = 1;
    else if (!strncmp(buf, "FieldTargetJumpCancelTeleport", 29)) kind = 2;
    if (g_tpSeenN < 16) { g_tpSeen[g_tpSeenN] = idx; g_tpSeenKind[g_tpSeenN] = kind; ++g_tpSeenN; Log("player teleport reason seen: %s", buf); }
    return kind;
}

static void HookTeleport(void* actor, const void* xf, const uint32_t* reason) {
    if (!actor) return;                                        // the original's own null check (skipped by the trampoline)
    const SteerTargets& t = g_tg[g_tgFront.load()];
    if (reason && t.pawn && reinterpret_cast<uintptr_t>(actor) == t.pawn) {
        int kind = TeleportKind(reason[0]);
        if (kind == 1 && g_enabled && g_steer.freeFall && xf && t.root && t.offRelLoc >= 0 && t.cmc && t.offMode >= 0) {
            // FTransform: rotation quat +0, translation +0x10. Skip only when he is (or is about to be) on the target's
            // level: platforms that must be reached keep the game's placement.
            float tg[3], pos[3]; uint8_t mode = 0;
            memcpy(tg, static_cast<const uint8_t*>(xf) + 0x10, 12);
            if (RdBytes(t.root + t.offRelLoc, pos, 12) && Rd(t.cmc + t.offMode, mode)) {
                float dx = pos[0] - tg[0], dy = pos[1] - tg[1], h = std::sqrt(dx * dx + dy * dy), dz = pos[2] - tg[2];
                bool sameLevel = false;
                if (mode == 1) sameLevel = dz > -60.f && dz < 160.f;     // target is at foot level (~+90 when on it)
                else if (mode == 3) {
                    FloorProbe f = ProbeFloor(t, pos, 800.f);
                    sameLevel = f.ok && f.hit && f.walkable && dz - f.dist > -60.f && dz - f.dist < 160.f;
                }
                static unsigned logged = 0;
                if (kDiag && logged < 12) { ++logged; Log("arrival teleport: %.0f cm away, height %+.0f, mode %u -> %s", h, dz, mode, (h < 400.f && sameLevel) ? "skipped" : "kept"); }
                if (h < 400.f && sameLevel) { g_tpSkipped.fetch_add(1); return; }
                g_tpKept.fetch_add(1);
            }
        }
        g_tpPassed.fetch_add(1);
    }
    g_tpOrig(actor, xf, reason);
}

static void InstallTeleportHook(uintptr_t base) {
    uint8_t* t = reinterpret_cast<uint8_t*>(base + RVA_TeleportReason);
    if (memcmp(t, TP_PROLOGUE, sizeof(TP_PROLOGUE)) != 0) { Log("teleport function differs - free landing off"); return; }
    // trampoline: the original continues after its null check: mov [rsp+18h],rbx ; jmp [rip+0] -> +0x0E
    uint8_t* tr = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tr) return;
    memcpy(tr, TP_PROLOGUE + 9, 5);
    tr[5] = 0xFF; tr[6] = 0x25; *reinterpret_cast<uint32_t*>(tr + 7) = 0;
    *reinterpret_cast<uint64_t*>(tr + 11) = reinterpret_cast<uint64_t>(t + 14);
    FlushInstructionCache(GetCurrentProcess(), tr, 64);
    g_tpOrig = reinterpret_cast<TeleportFn>(tr);
    uint8_t patch[14] = { 0xFF, 0x25, 0, 0, 0, 0 };
    *reinterpret_cast<uint64_t*>(patch + 6) = reinterpret_cast<uint64_t>(&HookTeleport);
    DWORD old;
    if (!VirtualProtect(t, 14, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(t, patch, 14);
    VirtualProtect(t, 14, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 14);
    Log("target-jump arrival teleport hooked (free landing)");
}

// F also reads xmm2 (not a declared parameter), so the wrapper is a register-preserving stub:
//   mov rax, &g_lastH ; mov [rax], rcx ; mov rax, F ; jmp rax
static void InstallFWrapper() {
    static bool done = false; if (done) return; done = true;
    void** entry = reinterpret_cast<void**>(g_base + RVA_F_ENTRY);
    void* cur = nullptr; if (!Rd(reinterpret_cast<uintptr_t>(entry), cur) || reinterpret_cast<uintptr_t>(cur) != g_base + RVA_F) {
        Log("vault handler table entry not as expected - trace off"); return;
    }
    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) return;
    uint8_t* p = stub;
    *p++ = 0x48; *p++ = 0xB8; *reinterpret_cast<uint64_t*>(p) = reinterpret_cast<uint64_t>(&g_lastH); p += 8;
    *p++ = 0x48; *p++ = 0x89; *p++ = 0x08;
    *p++ = 0x48; *p++ = 0xB8; *reinterpret_cast<uint64_t*>(p) = reinterpret_cast<uint64_t>(cur); p += 8;
    *p++ = 0xFF; *p++ = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), stub, 64);
    DWORD old; if (!VirtualProtect(entry, 8, PAGE_READWRITE, &old)) return;
    *entry = stub;
    VirtualProtect(entry, 8, old, &old);
    Log("vault handler wrapped (table entry %llx)", (unsigned long long)RVA_F_ENTRY);
}

static LONG CALLBACK WatchVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    uintptr_t W = g_vaultS.load(); uint32_t val = 0; Rd(W, val);   // watched dword: action request +0x90 (type)
    if ((val == 4 || val == 8) && g_wpHits.fetch_add(1) < 80) {
        uint32_t req = 0; Rd(W - 4, req);                            // +0x8C request code
        void* frames[24]; USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
        char buf[700]; int p = 0;
        for (USHORT i = 0; i < n && p < 660; ++i) {
            uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
            if (a >= g_base && a < g_base + 0x10000000) p += snprintf(buf + p, sizeof(buf) - p, " %llx", (unsigned long long)(a - g_base));
        }
        buf[p] = 0;
        LARGE_INTEGER f, q; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&q);
        double now = (double)q.QuadPart / (double)f.QuadPart;
        Log("WATCH action type = %u (request %08x): rip %llx  (%.2fs after Space) stack:%s",
            val, req, (unsigned long long)(c->Rip - g_base), now - g_ms.blockRollT0, buf);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void ArmWatchpoint() {
    // watch the player's action request struct (rsi in the parkour update 0x1301444): [[pawn+0x638]+0x1B8] + 0x90 =
    // action type (8 parkour, 4 dodge). The writer of 8 on a Space press is where the game picks vault over dodge.
    static bool armed = false;
    const SteerTargets& t = g_tg[g_tgFront.load()];
    if (armed || !t.pawn || !g_gameThread.load()) return;
    uintptr_t x = 0, req = 0;
    if (!Rd(t.pawn + 0x638, x) || !x || !Rd(x + 0x1B8, req) || !req) return;
    uint32_t probe = 0; if (!Rd(req + 0x90, probe)) return;
    armed = true;
    uintptr_t S = req + 0x90;
    g_vaultS.store(S);
    Log("action request struct %llx (character %llx), watching +0x90 (now %u)", (unsigned long long)req, (unsigned long long)t.pawn, probe);
    AddVectoredExceptionHandler(1, WatchVeh);
    HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, g_gameThread.load());
    if (!th) { Log("OpenThread failed"); return; }
    SuspendThread(th);
    CONTEXT ctx = {}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(th, &ctx)) {
        ctx.Dr0 = S;
        ctx.Dr7 = (ctx.Dr7 & ~0xF000Full) | 1ull | (1ull << 16) | (3ull << 18);   // L0, RW0 = 01 (write), LEN0 = 11 (4 bytes)
        if (SetThreadContext(th, &ctx)) Log("hardware watchpoint on %llx (action type) armed", (unsigned long long)S);
    }
    ResumeThread(th); CloseHandle(th);
}

static uintptr_t FindGUObjectArray(uintptr_t base);
static DWORD WINAPI MainThread(LPVOID) {
    Log("A Bit Better Traversal Movement v1.4.0-beta running, base=%p", (void*)g_base);
    for (int tries = 0; !g_guObjArr.load(); ++tries) {   // builds without a known address: find the object table
        if (tries == 600) { Log("object table (GUObjectArray) not found - the mod is off"); return 0; }
        uintptr_t a = FindGUObjectArray(g_base);
        if (a) { g_guObjArr.store(a); Log("object table found at +0x%llX", (unsigned long long)(a - g_base)); break; }
        Sleep(1000);
    }
    for (int tries = 0; !FindAnimSequenceClass(); ++tries) {
        if (tries == 600) { Log("AnimSequence class or RateScale not found - the mod is off"); return 0; }
        Sleep(1000);
    }
    if (g_build->guObjArr) {   // known build: check the structural search against the known address (for new builds)
        uintptr_t a = FindGUObjectArray(g_base);
        Log("object table self-test: structure search %s (+0x%llX)", a == g_guObjArr.load() ? "matches" : "DIFFERS",
            (unsigned long long)(a ? a - g_base : 0));
    }
    { HANDLE st = CreateThread(nullptr, 0, SteerThread, nullptr, 0, nullptr); if (st) CloseHandle(st); }
    for (;;) {
        __try { Pass(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("pass fault, retrying"); g_seen.clear(); }
        __try { UpdateSteerTargets(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("steer target fault"); }
        __try { if (kPosWatch) { ArmPosWatch(); LogPosWriters(); } if (kDiag) { WatchFileTick(); LogRequests(); LogIconCalls(); } PromptReachTick(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("position watch fault"); }
        while (g_diagR < g_diagW.load()) {   // name what stopped jump-downs mid-air
            const DiagEntry& e = g_diag[g_diagR++];
            auto describe = [](int32_t idx, char* out, size_t n) {
                out[0] = 0; if (idx <= 0) { snprintf(out, n, "none"); return; }
                uintptr_t arr = g_guObjArr.load() + 0x10, chunks = 0, chunk = 0, obj = 0;
                if (!Rd(arr, chunks) || !Rd(chunks + 8 * (idx / 65536), chunk) || !chunk || !Rd(chunk + 24 * (idx % 65536), obj) || !obj) { snprintf(out, n, "#%d ?", idx); return; }
                uintptr_t cls = 0, outer = 0; Rd(obj + OFF_UObject_Class, cls); Rd(obj + OFF_UObject_Outer, outer);
                snprintf(out, n, "%s (%s) in %s", ObjName(obj).c_str(), cls ? ObjName(cls).c_str() : "?", outer ? ObjName(outer).c_str() : "?");
            };
            char a[300], c[300]; describe(e.actor, a, sizeof(a)); describe(e.comp, c, sizeof(c));
            static const char* RES[3] = { "failed", "clear (pushed forward)", "blocked (undone)" };
            Log("DIAG jump-down stalled at %.0f %.0f %.0f: velocity %.0f but moving %.0f -> probe %s, hit=%d penetrating=%d normalZ=%.2f; actor: %s; component: %s",
                e.pos[0], e.pos[1], e.pos[2], e.vel, e.moved, RES[e.result < 3 ? e.result : 0], e.hit, e.pen, e.nz, a, c);
        }
        {
            static unsigned lastR = 0, lastL = 0, n = 0;
            if (n == 20) Log("ProcessEvent calls in the first ~20 s: %u (game thread %u)", g_peCalls.load(), g_peGameCalls.load());
            if (++n % 30 == 0 && (g_ms.rollMoves != lastR || g_ms.landMoves != lastL)) {
                Log("movement: %u roll stretch steps, %u landing carry steps, %u stopped by obstacles, %u rolls kept from traversal,"
                    " %u edge corrections, %u rolls off edges, %u traversal cooldowns (%u small ones without), %u free-fall speed restores, %u landing snaps undone so far, %u arrival teleports skipped, %u kept (other level), %u other player teleports, %u stopped at big drops, %u edge carries (%u roll position writes skipped)",
                    g_ms.rollMoves, g_ms.landMoves, g_ms.blocked, g_ms.travBlocks, g_ms.edgePushes, g_ms.edgeFalls, g_ms.cooldowns, g_ms.smallTrav,
                    g_ms.ffRestores, g_ms.snapUndos, g_tpSkipped.load(), g_tpKept.load(), g_tpPassed.load(), g_ms.cliffStops, g_ms.edgeCarries, g_carrySkips.load());
                lastR = g_ms.rollMoves; lastL = g_ms.landMoves;
            }
        }
        Sleep(1000);
    }
}

static bool IsSupportedBuild(uintptr_t base) {
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    for (const GameBuild& b : GAME_BUILDS)
        if (nt->FileHeader.TimeDateStamp == b.stamp && nt->OptionalHeader.SizeOfImage == b.imageSize) { g_build = &b; break; }
    if (!g_build) { Log("game exe: TimeDateStamp %08X, image size %08X - not a supported build", nt->FileHeader.TimeDateStamp,
                        nt->OptionalHeader.SizeOfImage); return false; }
    Log("game build %s", g_build->name);
    if (g_build->guObjArr) g_guObjArr.store(base + g_build->guObjArr);
    return true;
}

// GUObjectArray by structure (any build): FUObjectArray in a writable section, whose ObjObjects (+0x10) is
// { Objects**, PreAllocated*, MaxElements, NumElements, MaxChunks, NumChunks } with 65536 objects per chunk, and whose
// first objects carry their own index (UObjectBase::InternalIndex, +0x0C). Run once objects exist (after boot).
static uintptr_t FindGUObjectArray(uintptr_t base) {
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        uintptr_t lo = base + sec->VirtualAddress, hi = lo + sec->Misc.VirtualSize;
        for (uintptr_t a = lo; a + 0x30 <= hi; a += 8) {
            __try {
                const int32_t* f = reinterpret_cast<const int32_t*>(a + 0x20);
                int32_t mx = f[0], num = f[1], mxc = f[2], nc = f[3];
                if (num < 1000 || num > mx || mx > 0x4000000 || nc != (num + 65535) / 65536 || mxc < nc || mxc > 1024) continue;
                uintptr_t chunks = *reinterpret_cast<const uintptr_t*>(a + 0x10);
                if (chunks < 0x10000 || (chunks & 7)) continue;
                uintptr_t c0 = *reinterpret_cast<const uintptr_t*>(chunks);
                if (c0 < 0x10000 || (c0 & 7)) continue;
                bool ok = true;
                for (int i = 1; i < 6 && ok; ++i) {
                    uintptr_t obj = *reinterpret_cast<const uintptr_t*>(c0 + 24 * i);
                    ok = obj >= 0x10000 && !(obj & 7) && *reinterpret_cast<const int32_t*>(obj + 0x0C) == i;
                }
                if (ok) return a;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    return 0;
}

// ---------------------------------------------------------------- camera follows falls ([camera] fall_follow)
// While airborne the field camera eases toward the player's height slowly (CameraModule row DefaultHeightDeadzone:
// height deadzones and follow speeds), so after a roll off a ledge it only catches up once he has landed. The game
// reads that row once, early at startup, so this thread finds the row's values in memory as soon as the table is
// loaded and rewrites them before the camera reads them: no deadzone, and faster follow speeds.
static const float DZ_ORIG[10] = { 5.f, 1.f, 5.f, 0.f, 1.5f, 200.f, 1.f, 5.f, 1.f, 5.f };   // Param0..Param9
static int ScanPatchDeadzone(float speed) {   // plain function (SEH only): returns how many copies were patched
    int n = 0; uint8_t* a = nullptr; MEMORY_BASIC_INFORMATION mbi;
    while (VirtualQuery(a, &mbi, sizeof(mbi))) {
        uint8_t* b = static_cast<uint8_t*>(mbi.BaseAddress); size_t sz = mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE && (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED)) {
            __try {
                const uint32_t key = 0x43480000;   // 200.0f = Param5, then check the whole row around it
                uint32_t* w = reinterpret_cast<uint32_t*>(b); size_t nw = sz / 4;
                for (size_t i = 5; i + 5 < nw; ++i) {
                    if (w[i] != key) continue;
                    float* p = reinterpret_cast<float*>(w + i - 5);
                    if (memcmp(p, DZ_ORIG, sizeof(DZ_ORIG)) != 0) continue;
                    p[0] = 0.f; p[4] = 0.f; p[5] = 0.f;                // height deadzones off
                    p[2] = speed; p[7] = speed; p[9] = speed;          // follow speeds
                    ++n;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        a = b + sz;
        if (reinterpret_cast<uintptr_t>(a) >= 0x7FFFFFFF0000ull) break;
    }
    return n;
}
static DWORD WINAPI CameraPatchThread(LPVOID) {
    wchar_t path[MAX_PATH]; IniPath(path, L"TraversalSpeed.ini");
    wchar_t v[32];
    if (!GetPrivateProfileStringW(L"camera", L"fall_follow", L"", v, 32, path)) WritePrivateProfileStringW(L"camera", L"fall_follow", L"1", path);
    if (!GetPrivateProfileStringW(L"camera", L"fall_follow_speed", L"", v, 32, path)) WritePrivateProfileStringW(L"camera", L"fall_follow_speed", L"40", path);
    if (!GetPrivateProfileIntW(L"global", L"enabled", 1, path) || !GetPrivateProfileIntW(L"camera", L"fall_follow", 1, path)) {
        Log("camera: fall_follow off (vanilla camera height follow)"); return 0;
    }
    GetPrivateProfileStringW(L"camera", L"fall_follow_speed", L"40", v, 32, path);
    float speed = (float)_wtof(v); if (!(speed >= 1.f && speed <= 200.f)) speed = 40.f;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ULONGLONG t0 = GetTickCount64(); int found = 0;
    while (GetTickCount64() - t0 < 180000) {          // the table loads early in startup; keep looking for 3 min
        int n = ScanPatchDeadzone(speed);
        if (n) { found += n; Log("camera: fall_follow applied (%d cop%s, speed %.0f) %.1f s after start", n, n == 1 ? "y" : "ies", speed, (GetTickCount64() - t0) / 1000.0); }
        if (found && GetTickCount64() - t0 > 30000) break;   // one more sweep window for late copies, then stop
        Sleep(found ? 2000 : 100);
    }
    if (!found) Log("camera: camera height table not found - fall_follow not applied");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = mod;
        DisableThreadLibraryCalls(mod);
        InitDwmapiProxy();   // always first: the game and Windows call dwmapi through this DLL
        // only in the game, once per process
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* base = wcsrchr(exe, L'\\'); base = base ? base + 1 : exe;
        if (_wcsicmp(base, L"ff7rebirth_.exe") != 0) return TRUE;
        wchar_t mx[64]; swprintf_s(mx, L"Local\\TraversalSpeed.%lu", GetCurrentProcessId());
        HANDLE m = CreateMutexW(nullptr, FALSE, mx);
        if (!m || GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
        wchar_t path[MAX_PATH]; IniPath(path, L"TraversalSpeed.log");
        g_log = _wfopen(path, L"w");
        Log("A Bit Better Traversal Movement v1.4.0-beta loaded");
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!IsSupportedBuild(g_base)) {
            Log("this game version is not supported (game update?) - the mod is off, the game runs unmodified");
            return TRUE;
        }
        g_gameThread.store(GetCurrentThreadId());   // static imports load on the main thread = UE's game thread
        Log("main (game) thread %lu", GetCurrentThreadId());
        InstallProcessEventHook(g_base);   // before the game runs any game code
        InstallParkourTryHook(g_base);
        InstallLedgeDetectHook(g_base);
        InstallParkourUpdateGate(g_base);
        InstallTeleportHook(g_base);
        InstallAutoTraversalGate(g_base);
        InstallPromptReach(g_base);
        { HANDLE c = CreateThread(nullptr, 0, CameraPatchThread, nullptr, 0, nullptr); if (c) CloseHandle(c); }
        HANDLE h = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
