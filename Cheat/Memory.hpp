// ============================================================
//  Memory — Dual mode: External (NtRVM) + Internal (direct ptr)
//
//  External: DLL ayri bir process'te (Chrome, goodbyedpi, wsl…)
//            NtReadVirtualMemory / NtWriteVirtualMemory kullanir.
//
//  Internal: DLL FiveM'e inject edilmis — ayni address space.
//            Dogrudan pointer dereference kullanir, handle yok.
//
//  Mod tespiti: Game.pID == GetCurrentProcessId()
//  → inject sonrasi AttachToGameProcess FiveM'in kendi PID'ini
//    set eder; bu kosul saglaninca internal mod devreye girer.
// ============================================================

static inline bool _isInternal() {
    return Game.pID != 0 && Game.pID == GetCurrentProcessId();
}

/* ── External: NtReadVirtualMemory / NtWriteVirtualMemory ─────── */
typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI* pfnNtRVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS (NTAPI* pfnNtWVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

static pfnNtRVM g_NtRead  = nullptr;
static pfnNtWVM g_NtWrite = nullptr;

static void InitNtFunctions() {
    if (g_NtRead) return;
    static char sNtdll[] = {'n','t','d','l','l','.','d','l','l',0};
    static char sNtR[]   = {'N','t','R','e','a','d','V','i','r','t','u','a','l','M','e','m','o','r','y',0};
    static char sNtW[]   = {'N','t','W','r','i','t','e','V','i','r','t','u','a','l','M','e','m','o','r','y',0};
    HMODULE ntdll = GetModuleHandleA(sNtdll);
    if (!ntdll) return;
    g_NtRead  = (pfnNtRVM)GetProcAddress(ntdll, sNtR);
    g_NtWrite = (pfnNtWVM)GetProcAddress(ntdll, sNtW);
}

/* ── ReadMemory ─────────────────────────────────────────────────── */
template<class T>
T ReadMemory(uintptr_t address) {
    if (!address) return T{};
    if (_isInternal()) {
        /* FiveM icinde: dogrudan pointer, SEH ile koru */
        __try { return *reinterpret_cast<T*>(address); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return T{}; }
    }
    if (!Game.hProcess) return T{};
    InitNtFunctions();
    T out{};
    if (g_NtRead)
        g_NtRead(Game.hProcess, reinterpret_cast<PVOID>(address), &out, sizeof(T), nullptr);
    return out;
}

/* ── WriteMemory ────────────────────────────────────────────────── */
template<class T>
void WriteMemory(uintptr_t address, T value) {
    if (!address) return;
    if (_isInternal()) {
        __try { *reinterpret_cast<T*>(address) = value; }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }
    if (!Game.hProcess) return;
    InitNtFunctions();
    if (g_NtWrite)
        g_NtWrite(Game.hProcess, reinterpret_cast<PVOID>(address), &value, sizeof(T), nullptr);
}

/* ── WriteBytes (patch / code cave) ────────────────────────────── */
void WriteBytes(uintptr_t address, uint8_t* patch, size_t size) {
    if (!address || !patch || !size) return;
    if (_isInternal()) {
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &old);
        memcpy(reinterpret_cast<void*>(address), patch, size);
        if (old) VirtualProtect(reinterpret_cast<LPVOID>(address), size, old, &old);
        return;
    }
    if (!Game.hProcess) return;
    InitNtFunctions();
    DWORD old = 0;
    VirtualProtectEx(Game.hProcess, reinterpret_cast<LPVOID>(address), size,
                     PAGE_EXECUTE_READWRITE, &old);
    if (g_NtWrite)
        g_NtWrite(Game.hProcess, reinterpret_cast<PVOID>(address), patch, size, nullptr);
    if (old) VirtualProtectEx(Game.hProcess, reinterpret_cast<LPVOID>(address), size, old, &old);
}

/* ── ReadString ─────────────────────────────────────────────────── */
std::string ReadString(uintptr_t addr) {
    if (!addr) return {};
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 255; ++i) {
        char c = ReadMemory<char>(addr + i);
        if (!c) break;
        out += c;
    }
    return out;
}

/* ── GetBaseAddress ─────────────────────────────────────────────── */
uintptr_t GetBaseAddress() {
    if (_isInternal())
        return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)); /* FiveM.exe base */
    MODULEENTRY32W me = {}; me.dwSize = sizeof(me);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, Game.pID);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me))
        base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
    CloseHandle(snap);
    return base;
}

uintptr_t GetBaseAddress(const std::string& moduleName) {
    MODULEENTRY32W me = {}; me.dwSize = sizeof(me);
    DWORD pid = _isInternal() ? GetCurrentProcessId() : Game.pID;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    std::wstring wName(moduleName.begin(), moduleName.end());
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, wName.c_str()) == 0) {
                base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}
