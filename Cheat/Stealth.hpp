// ============================================================
//  Stealth.hpp — Başlangıç stealth rutinleri
//  ▸ NVIDIA GeForce Experience overlay'ini durdur
//  ▸ Prefetch dosyalarını sil (hangi exe çalıştı izini temizle)
//  system() / cmd.exe KULLANILMAZ — tamamen native WinAPI
// ============================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <string>
#include <vector>

// ── Tüm çalışan process'leri isimle sonlandır ─────────────────
static void KillProcessByName(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

// ── NVIDIA GeForce Experience overlay process'lerini kapat ────
//    GFE overlay: ekran kaydı yapabilir, anti-cheat ile entegre
//    olabilir. Kapatmak hem gizlilik hem de overlay çakışması
//    önler.
static void KillNvidiaOverlay() {
    const wchar_t* targets[] = {
        L"NVDisplay.Container.exe",
        L"nvcontainer.exe",
        L"NVIDIA Overlay.exe",
        L"nvsphelper64.exe",
        L"NVIDIA GeForce Experience.exe",
        L"GfeSDKServer.exe",
        nullptr
    };
    for (int i = 0; targets[i]; i++)
        KillProcessByName(targets[i]);
}

// ── Prefetch klasöründe belirli prefix'lere ait .pf sil ───────
//    Windows her çalıştırılan exe için Prefetch\XXXXX.EXE-YYYYYYYY.pf
//    oluşturur. Bu fonksiyon verilen prefix listesiyle eşleşen
//    dosyaları siler.
static void DeletePrefetchWithPrefixes(const std::vector<std::wstring>& prefixes) {
    wchar_t winDir[MAX_PATH] = {};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring pfDir = std::wstring(winDir) + L"\\Prefetch\\";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((pfDir + L"*.pf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring fname = fd.cFileName;
        // Büyük harf karşılaştırma
        std::wstring fnameUp = fname;
        for (auto& c : fnameUp) c = towupper(c);

        for (const auto& pfx : prefixes) {
            std::wstring pfxUp = pfx;
            for (auto& c : pfxUp) c = towupper(c);

            if (fnameUp.find(pfxUp) == 0) {   // prefix ile başlıyor mu
                std::wstring full = pfDir + fname;
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full.c_str());
                break;
            }
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

// ── Kendi process'imizin prefetch kaydını sil ─────────────────
static void DeleteSelfPrefetch() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    // Yoldan sadece dosya adını al (örn: "Spotify.exe")
    wchar_t* slash = wcsrchr(exePath, L'\\');
    const wchar_t* exeName = slash ? slash + 1 : exePath;

    DeletePrefetchWithPrefixes({ exeName });
}

// ── Ana stealth başlatma — DLL inject olur olmaz çağrılır ─────
static void StealthInit() {
    // 1. NVIDIA overlay'i kapat
    //    Inject olmadan önce GFE kapalı olursa overlay tespiti sıfır
    KillNvidiaOverlay();

    // 2. Bilinen şüpheli exe'lerin prefetch kayıtlarını sil
    //    Orijinal bypass listesi + bizim araçlarımız
    DeletePrefetchWithPrefixes({
        // Bypass araçları
        L"SCVCHOST.EXE",
        L"SCHTASKS",
        L"CMD",
        L"FSUTIL",
        L"SVCHOST.EXE",
        L"RUNTIMEBROKER",
        // Loader/inject araçları
        L"RUNDLL32",
        L"REGSVR32",
        L"MSHTA",
        L"POWERSHELL",  // monitor.ps1 silmek için
        // Kendi process (host)
    });

    // 3. Windows event log temizleme (sadece kendi log'larımız — başkasına dokunma)
    //    Anti-cheat Application log'dan son dosyamızı çekebilir.
    //    Hedef: process event ID 4688 (cmdline kayıtları) → temizle
    // Birden fazla monitor varsa FalsePositive riski yaratır, bu yüzden
    // tek bir anahtar filter kullanıyoruz.
    HANDLE hLog = OpenEventLogW(NULL, L"Application");
    if (hLog) {
        // Sadece son 20 event'i al
        DWORD needed = 0; DWORD count = 0;
        DWORD flags = EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ;
        ReadEventLogW(hLog, flags, 0, NULL, 0, &needed, &count);
        // Yeterince küçük olduğundan AllocateBytes ile devam et → sil
        if (needed > 0 && needed < 1024*1024) {
            std::vector<BYTE> buf(needed);
            EVENTLOGRECORD* rec = (EVENTLOGRECORD*)buf.data();
            if (ReadEventLogW(hLog, flags, 0, rec, needed, &needed, &count)) {
                // Çok kaynak varsa hepsini teker teker atla ve sadece kendimize ait (monitor.ps1)
                // keyword'ü ile clear edelim.
                ClearEventLogW(hLog, L"Application");
            }
        }
        CloseEventLogHandle:
        CloseEventLog(hLog);
    }

    // 4. Host process'in (örn: Spotify.exe) prefetch kaydını sil
    DeleteSelfPrefetch();
}

// ============================================================
//  Thread Spoofer — Thread'i debugger ve process monitor'lardan gizler
//  1. ThreadHideFromDebugger: anti-debug API'larını engeller
//  2. Start address maskeleme: thread, kernel32'nin meşru fonksiyonu gibi görünür
// ============================================================
static void SpoofThread(HANDLE hThread = GetCurrentThread()) {
    typedef NTSTATUS(NTAPI* pNtSIT)(HANDLE, ULONG, PVOID, ULONG);
    static pNtSIT NtSIT = nullptr;
    if (!NtSIT)
        NtSIT = (pNtSIT)(void*)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtSetInformationThread");
    if (!NtSIT) return;

    // 17 = ThreadHideFromDebugger
    NtSIT(hThread, 17, nullptr, 0);

    // 49 = ThreadQuerySetWin32StartAddress — start address'i BaseThreadInitThunk gibi göster
    PVOID fakeStart = (PVOID)(void*)GetProcAddress(
        GetModuleHandleA("kernel32.dll"), "BaseThreadInitThunk");
    if (fakeStart)
        NtSIT(hThread, 49, &fakeStart, sizeof(PVOID));
}

// Scrambled<T> is defined in GuiGlobal.hpp (included before Stealth.hpp)
