#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <dwmapi.h>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdio>

#include <mjLib/mjLib.hpp>

// ── ImGui kaldırıldı; ImCompat + pure GDI rendering ──────────
#include "ImCompat.hpp"
#include "GuiGlobal.hpp"


bool keepRunning       = true;
bool isMenuVisible     = false;  // Artık kullanılmıyor — menu localhost:7531'de
bool screenSizeChanged = false;
bool sizeChanged       = false;
bool mainReady         = false;

// Overlay thread handle — FiveM her kapanıp açılışında yeniden başlatılır
static HANDLE g_overlayThread = nullptr;

#include "GDIRenderer.hpp"   // Pure GDI renderer (D3D11/D2D1 yerine — anti-cheat safe)
#include "Stealth.hpp"       // NVIDIA overlay kapatma + prefetch temizleme
#include "GameSDK.hpp"
#include "Config.hpp"
#include "License.hpp"
#include "Cheat.hpp"
#include "Overlay.hpp"
#include "WsClient.hpp"    // Remote WebSocket relay menu
#include "WebServer.hpp"   // Local HTTP menu — localhost:7531

// ──────────────────────────────────────────────────────────────
//  Baglanti durumu
//  g_gameConnected non-static: WebServer.hpp extern ile referans alır
// ──────────────────────────────────────────────────────────────
bool g_gameConnected  = false;   // extern bool g_gameConnected; → WebServer.hpp'de görünür
static bool g_threadsStarted = false;

static void CheatLog(const char*) {}

static bool IsGameAlive() {
    if (!Game.hProcess) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(Game.hProcess, &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
}

static void DisconnectGame() {
    g_gameConnected = false;
    keepRunning = false;          // overlay + worker thread'lere "dur" sinyali
    ResetCave();                  // Bug 3: stale g_cave sıfırla — yeni process bağlantısında yeniden taranacak

    // Overlay thread'i ÖNCE bekle — keepRunning=true yapmadan önce bitmeli
    // Yoksa overlay loop'u yeniden girebilir veya cleanup yarım kalır
    if (g_overlayThread) {
        WaitForSingleObject(g_overlayThread, 4000);
        CloseHandle(g_overlayThread);
        g_overlayThread = nullptr;
    }

    if (Game.hProcess) {
        CloseHandle(Game.hProcess);
        Game.hProcess = nullptr;
    }
    Game.pID         = 0;
    Game.hWnd        = nullptr;
    Offsets.GameBase = 0;

    Sleep(200);        // worker thread'lerin (UpdatePeds vb.) dur sinyalini görmesi için
    keepRunning = true; // overlay + worker thread'ler aktif moda geçsin
}

// ──────────────────────────────────────────────────────────────
//  Thread'leri baslat — SADECE ilk oyun baglantisinda bir kez
// ──────────────────────────────────────────────────────────────
// Thread oluştur, spoof et, detach et
static void SpawnSpoofed(void(*fn)()) {
    HANDLE h = CreateThread(nullptr, 0,
        [](LPVOID p) -> DWORD {
            SpoofThread(GetCurrentThread());
            ((void(*)())p)();
            return 0;
        },
        (LPVOID)fn, 0, nullptr);
    if (h) CloseHandle(h);
}

static void StartWorkerThreads() {
    if (g_threadsStarted) return;
    g_threadsStarted = true;

    SpawnSpoofed([]() { License::Start(); });
    SpawnSpoofed(NameCacheThread);
    SpawnSpoofed(UpdatePeds);
    SpawnSpoofed(UpdateVehicles);
    SpawnSpoofed(AimAssist);
    SpawnSpoofed(Exploits);
    // Yerel HTTP menu — localhost:7531
    std::thread([]() { __try { WebServerThread(); } __except(EXCEPTION_EXECUTE_HANDLER) {} }).detach();
}

// ──────────────────────────────────────────────────────────────
//  Overlay thread
// ──────────────────────────────────────────────────────────────
static void OverlayThreadBody() {
    __try {
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    int errorId = 0;
    OverlayMain(errorId);
}

// Wrapper — C++ nesnesi yok, __try güvenli
static DWORD WINAPI OverlayThread(LPVOID) {
    __try {
        OverlayThreadBody();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // D3D11 crash → Spotify ölmez, sessizce çık
    }
    return 0;
}

// ── FiveM'i öne getir ─────────────────────────────────────────────────────
// Tarayıcı host: minimize etmeye gerek yok, sadece FiveM'i foreground yap.
static void HideDDNetBringFiveM() {
    Sleep(1500);
    HWND hFiveM = FindWindowA(BuildClassName(), nullptr);
    if (hFiveM) {
        HWND hFG = GetForegroundWindow();
        DWORD fgTid = hFG ? GetWindowThreadProcessId(hFG, nullptr) : 0;
        DWORD myTid = GetCurrentThreadId();
        if (fgTid && fgTid != myTid) AttachThreadInput(fgTid, myTid, TRUE);
        SetForegroundWindow(hFiveM);
        BringWindowToTop(hFiveM);
        if (fgTid && fgTid != myTid) AttachThreadInput(fgTid, myTid, FALSE);
        CheatLog("FiveM brought to foreground");
    }
}

// ──────────────────────────────────────────────────────────────
//  Ana cheat thread
//  - Oyun acilinca baglan, thread'leri + overlay'i baslat
//  - Oyun kapaninca bekle, yeniden acilinca oto baglan
//  - END = tamamen kapat
// ──────────────────────────────────────────────────────────────
static void CheatThreadBody() {
    // ── Stealth: NVIDIA overlay kapat + prefetch temizle ─────
    //    Ayrı thread — inject akışını bloklama
    std::thread([]() {
        __try { StealthInit(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }).detach();

    // WS relay client — oyun bağlanmadan önce de çalışır (bağlantı state'i gönderir)
    std::thread([]() { WsClientThread(); }).detach();

    // FiveM'i öne getir (ayrı thread — main loop'u bloklama)
    std::thread([]() { HideDDNetBringFiveM(); }).detach();

    Sleep(3000);

    while (true) {
        CheatLog("CheatThread: loop start");

        if (GetAsyncKeyState(VK_END) & 0x8000) {
            keepRunning = false;
            if (g_gameConnected) {
                RestoreSilent();
                DisconnectGame();
            }
            break;
        }

        bool grcOpen = (FindWindowA(BuildClassName(), NULL) != NULL);
        CheatLog(grcOpen ? "Game window found" : "Game window NOT found");

        if (g_gameConnected) {
            if (!grcOpen || !IsGameAlive())
                DisconnectGame();
        } else {
            if (grcOpen) {
                CheatLog("Game window open, attempting attach...");
                Sleep(2000);
                if (AttachToGameProcess() && FetchOffsetsAndVersion()) {
                    CheatLog("Attached to game successfully!");
                    g_gameConnected = true;
                    keepRunning     = true;

                    // Populate Game.lpRect immediately so WorldToScreen works from frame 1
                    if (Game.hWnd) {
                        RECT r = {};
                        if (GetClientRect(Game.hWnd, &r) && r.right > 0)
                            Game.lpRect = r;
                        if (!Game.lpRect.right) {
                            Game.lpRect.right  = GetSystemMetrics(SM_CXSCREEN);
                            Game.lpRect.bottom = GetSystemMetrics(SM_CYSCREEN);
                        }
                    }

                    // Restore saved settings — without this all settings reset on every inject
                    LoadConfigRemote();
                    CheatLog("Config loaded.");

                    StartWorkerThreads();

                    // Overlay thread'i her FiveM oturumu için yeniden başlat.
                    if (!g_overlayThread) {
                        CheatLog("Starting overlay thread...");
                        g_overlayThread = CreateThread(NULL, 0, OverlayThread, NULL, 0, NULL);
                    }

                    std::thread([]() {
                        Sleep(3000);
                        for (int i = 0; i < 60 && keepRunning; i++) {
                            uintptr_t gw = ReadMemory<uintptr_t>(Offsets.GameBase + Offsets.GameWorld);
                            if (gw) {
                                LocalPlayer.Pointer = ReadMemory<uintptr_t>(gw + Offsets.LocalPlayer);
                                if (LocalPlayer.Pointer) {
                                    int id = ReadMemory<int>(LocalPlayer.Pointer + Offsets.Id);
                                    if (id > 0 && id < 2048) { ScanCitizenNames(id); return; }
                                }
                            }
                            Sleep(1000);
                        }
                    }).detach();
                }
            }
        }

        Sleep(1000);
    }
}

// Wrapper — sadece __try içeriyor, C++ nesnesi yok
static DWORD WINAPI CheatThread(LPVOID) {
    __try {
        CheatThreadBody();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Herhangi bir crash → Spotify'ı öldürme, sessizce çık
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────
//  Anti-analysis kontrolleri
// ──────────────────────────────────────────────────────────────
static bool IsSafeEnvironment() {
    // Debugger kontrolü kaldırıldı - farklı sistemlerde false positive veriyordu
    // ve Spotify'ın crash etmesine neden oluyordu
    return true;
}

// ──────────────────────────────────────────────────────────────
//  DllMain — Loader tarafindan LoadLibrary ile yuklenir
//  DllMain icinde MINIMUM is yap — sadece thread ac ve don
// ──────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Anti-analysis: tehlikeli ortamda çalışma
        if (!IsSafeEnvironment()) return FALSE;

        // WS client init — cheatload.c FudInitParams* geçirir (cheatKey + wsHost + wsPort)
        // NULL ise dev/test ortamı — WsClientThread bağlantıyı atlayacak
        WsClientInit(lpReserved);

        // Sentinel mutex: loader "zaten yüklü mü?" diye bunu kontrol eder
        // PE header silindikten sonra EnumProcessModules işe yaramaz
        {
            // Mutex prefix runtime inşa — "Local\ThS_" bellekte görünmesin
            wchar_t mutPfx[] = { 'L','o','c','a','l','\\','R','P','C','S','S','_',0 };
            wchar_t mutName[64] = {};
            swprintf_s(mutName, 64, L"%s%lu", mutPfx, GetCurrentProcessId());

            // Zaten yüklü mü? (installer overlay penceresi olmadan tekrar inject ettiyse)
            // Önceki instance çalışıyor — ikinci thread başlatma, sadece çık.
            HANDLE hExisting = OpenMutexW(SYNCHRONIZE, FALSE, mutName);
            if (hExisting) {
                CloseHandle(hExisting);
                return TRUE;  // double-load — sessizce çık
            }

            CreateMutexW(nullptr, FALSE, mutName);
            // Mutex kasta kapatılmıyor — process ölünce OS serbest bırakır
        }

        HANDLE h = CreateThread(NULL, 0, CheatThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
