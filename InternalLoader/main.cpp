/*
 * InternalLoader/main.cpp  —  Moon Private Internal Launcher
 *
 * Kullanici acar → lisans aktivasyonu (native Win32 GUI) →
 * Cloudflare Workers'dan SpCrashReport.dll indirir (XOR-sifreli) →
 * FiveM process'ine diske yazmadan reflective inject eder.
 * IAT local GetProcAddress ile doldurulur (system DLL adresleri tum
 * process'lerde ayni boot oturumunda esit — ASLR per-boot, per-process degil).
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* ── Sunucu bilgileri (Cloudflare Workers, HTTPS) ────────────────── */
#define SRV_HOST  L"moon-auth-service.moonsal.workers.dev"
#define SRV_PORT  443

/* ── XOR obfuscation (key 0x5B) ────────────────────────────────── */
static void _xd(char *d, const unsigned char *s, int n) {
    for (int i = 0; i < n; i++) d[i] = (char)(s[i] ^ 0x5Bu);
}
static void _aw(const char *s, wchar_t *d, int n) {
    for (int i = 0; i < n-1 && s[i]; i++) d[i] = (wchar_t)(unsigned char)s[i];
    d[n-1] = 0;
}

/* ── HWID ───────────────────────────────────────────────────────── */
static void _hwid(char *out, int sz) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
        char guid[64] = {};
        DWORD gsz = sizeof(guid);
        if (RegQueryValueExA(hk, "MachineGuid", NULL, NULL,
                             (LPBYTE)guid, &gsz) == ERROR_SUCCESS) {
            int n = 0;
            for (char *p = guid; *p && n < 8; p++) {
                if (*p == '-') continue;
                out[n++] = (*p >= 'a' && *p <= 'f') ? (char)(*p - 32) : *p;
            }
            if (n == 8) { out[8] = 0; RegCloseKey(hk); return; }
        }
        RegCloseKey(hk);
    }
    DWORD serial = 0;
    GetVolumeInformationA("C:\\", NULL, 0, &serial, NULL, NULL, NULL, 0);
    snprintf(out, sz, "%08X", (unsigned)serial);
}

/* ── WinHTTP yardimcilari ───────────────────────────────────────── */
static char *_http_str(const wchar_t *path) {
    HINTERNET s = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_NO_PROXY, 0, 0, 0);
    if (!s) return NULL;
    HINTERNET c = WinHttpConnect(s, SRV_HOST, SRV_PORT, 0);
    if (!c) { WinHttpCloseHandle(s); return NULL; }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL,
                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return NULL; }
    DWORD t = 15000;
    WinHttpSetOption(r, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
    WinHttpSetOption(r, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    char *buf = NULL; DWORD tot = 0;
    if (WinHttpSendRequest(r, 0, 0, 0, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD av = 0;
        while (WinHttpQueryDataAvailable(r, &av) && av > 0) {
            char *tmp = (char *)realloc(buf, tot + av + 1);
            if (!tmp) { free(buf); buf = NULL; tot = 0; break; }
            buf = tmp; DWORD rd = 0;
            if (WinHttpReadData(r, buf + tot, av, &rd)) tot += rd;
        }
        if (buf) {
            buf[tot] = 0;
            while (tot > 0 && (buf[tot-1]=='\r'||buf[tot-1]=='\n'||buf[tot-1]==' '))
                buf[--tot] = 0;
        }
    }
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return buf;
}

static unsigned char *_http_raw(const wchar_t *path, DWORD *out_sz) {
    *out_sz = 0;
    HINTERNET s = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_NO_PROXY, 0, 0, 0);
    if (!s) return NULL;
    HINTERNET c = WinHttpConnect(s, SRV_HOST, SRV_PORT, 0);
    if (!c) { WinHttpCloseHandle(s); return NULL; }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL,
                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return NULL; }
    DWORD t = 30000;
    WinHttpSetOption(r, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
    WinHttpSetOption(r, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    unsigned char *buf = NULL; DWORD tot = 0;
    if (WinHttpSendRequest(r, 0, 0, 0, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD av = 0;
        while (WinHttpQueryDataAvailable(r, &av) && av > 0) {
            unsigned char *tmp = (unsigned char *)realloc(buf, tot + av + 1);
            if (!tmp) { free(buf); buf = NULL; tot = 0; break; }
            buf = tmp; DWORD rd = 0;
            if (WinHttpReadData(r, buf + tot, av, &rd)) tot += rd;
        }
        if (buf) buf[tot] = 0;
    }
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    *out_sz = tot; return buf;
}

/* ── Auth / Activate / Download ────────────────────────────────── */
static int _auth(const char *hwid, char tok[128]) {
    static const unsigned char _p[] = {0x74,0x38,0x2E,0x2B,0x33,0x64,0x33,0x66,0};
    char pre[16]; _xd(pre, _p, 8); pre[8] = 0;
    char p8[256]; snprintf(p8, sizeof(p8), "%s%s", pre, hwid);
    wchar_t pw[256]; _aw(p8, pw, 256);
    char *resp = _http_str(pw); if (!resp) return 0;
    int r = 0;
    if (resp[0]=='O'&&resp[1]=='K'&&resp[2]==':') { strncpy(tok, resp+3, 127); r=1; }
    else if (resp[0]=='E') r=-2; else if (resp[0]=='U') r=-3; else if (resp[0]=='D') r=-1;
    free(resp); return r;
}

static int _activate(const char *hwid, const char *key) {
    static const unsigned char _p[] = {
        0x74,0x3A,0x38,0x2F,0x32,0x2D,0x3A,0x2F,0x3E,0x64,0x33,0x66,0};
    static const unsigned char _k[] = {0x7D,0x30,0x66,0};
    char pre[16]; _xd(pre, _p, 12); pre[12] = 0;
    char kpfx[8]; _xd(kpfx, _k, 3); kpfx[3] = 0;
    char p8[512]; snprintf(p8, sizeof(p8), "%s%s%s%s", pre, hwid, kpfx, key);
    wchar_t pw[512]; _aw(p8, pw, 512);
    char *resp = _http_str(pw); if (!resp) return 0;
    int r = (resp[0]=='O'&&resp[1]=='K') ? 1 : 0;
    free(resp); return r;
}

static unsigned char *_dl(const char *tok, DWORD *sz_out) {
    static const unsigned char _p[] = {0x74,0x2F,0x37,0x3E,0x35,0x66,0};
    char pre[16]; _xd(pre, _p, 6); pre[6] = 0;
    char p8[256]; snprintf(p8, sizeof(p8), "%s%s", pre, tok);
    wchar_t pw[256]; _aw(p8, pw, 256);
    DWORD sz = 0;
    unsigned char *data = _http_raw(pw, &sz);
    if (!data || sz < 0x200) { free(data); return NULL; }
    DWORD kl = (DWORD)strlen(tok);
    for (DWORD i = 0; i < sz; i++) data[i] ^= (unsigned char)tok[i % kl];
    *sz_out = sz; return data;
}

/* ── FiveM process bul ──────────────────────────────────────────── */
static DWORD _find_fivem(void) {
    /* "grcWindow" runtime'da insa ediliyor */
    static char cls[10] = {};
    cls[0]='g'; cls[1]='r'; cls[2]='c';
    cls[3]='W'; cls[4]='i'; cls[5]='n';
    cls[6]='d'; cls[7]='o'; cls[8]='w'; cls[9]=0;
    HWND hw = FindWindowA(cls, NULL);
    if (!hw) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(hw, &pid);
    return pid;
}

/* ── x64 shellcode: DllMain(imageBase, DLL_PROCESS_ATTACH, NULL) ─
 *  Thread param (rcx) = imageBase (CreateRemoteThread'den gelir)
 *
 *  Offset 0:  53              push rbx
 *  Offset 1:  48 83 EC 28     sub rsp, 28
 *  Offset 5:  BA 01 00 00 00  mov edx, 1     (DLL_PROCESS_ATTACH)
 *  Offset 10: 45 33 C0        xor r8d, r8d   (NULL)
 *  Offset 13: 48 B8 [8 bytes] mov rax, entry_addr
 *  Offset 23: FF D0           call rax
 *  Offset 25: 48 83 C4 28     add rsp, 28
 *  Offset 29: 5B              pop rbx
 *  Offset 30: C3              ret
 */
static const unsigned char _sc_tmpl[] = {
    0x53,
    0x48, 0x83, 0xEC, 0x28,
    0xBA, 0x01, 0x00, 0x00, 0x00,
    0x45, 0x33, 0xC0,
    0x48, 0xB8, 0,0,0,0,0,0,0,0,  /* entry_addr placeholder: offset 15 */
    0xFF, 0xD0,
    0x48, 0x83, 0xC4, 0x28,
    0x5B,
    0xC3
};
#define SC_ADDR_OFF 15

/* ── Remote manual map + shellcode inject ───────────────────────── */
static BOOL _inject(HANDLE hProc, const unsigned char *raw, SIZE_T rsz) {
    if (rsz < sizeof(IMAGE_DOS_HEADER)) return FALSE;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)raw;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    const IMAGE_NT_HEADERS64 *nt =
        (const IMAGE_NT_HEADERS64 *)(raw + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    SIZE_T imgSz  = nt->OptionalHeader.SizeOfImage;
    SIZE_T totSz  = imgSz + sizeof(_sc_tmpl);

    /* Hedef process'te RWX bellek ayir */
    LPVOID rBase = VirtualAllocEx(hProc, NULL, totSz,
                                   MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!rBase) return FALSE;

    /* Yerel calisma kopyasi hazirla */
    unsigned char *img = (unsigned char *)calloc(imgSz, 1);
    if (!img) { VirtualFreeEx(hProc, rBase, 0, MEM_RELEASE); return FALSE; }

    /* Header + section kopyala */
    memcpy(img, raw, nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER *sec =
        (const IMAGE_SECTION_HEADER *)((const BYTE *)(nt + 1));
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!sec[i].SizeOfRawData) continue;
        memcpy(img + sec[i].VirtualAddress,
               raw + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    }

    /* Relocation fixup (yeni ImageBase = rBase) */
    LONGLONG delta = (LONGLONG)((UINT_PTR)rBase - nt->OptionalHeader.ImageBase);
    if (delta) {
        const IMAGE_DATA_DIRECTORY *rd =
            &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        BYTE *blk = img + rd->VirtualAddress, *end = blk + rd->Size;
        while (blk < end) {
            IMAGE_BASE_RELOCATION *b = (IMAGE_BASE_RELOCATION *)blk;
            if (!b->SizeOfBlock) break;
            DWORD cnt = (b->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD *e = (WORD *)(b + 1);
            for (DWORD j = 0; j < cnt; j++) {
                if ((e[j] >> 12) == IMAGE_REL_BASED_DIR64) {
                    UINT_PTR *p = (UINT_PTR *)(img + b->VirtualAddress + (e[j] & 0x0FFF));
                    *p += (UINT_PTR)delta;
                }
            }
            blk += b->SizeOfBlock;
        }
    }

    /* IAT doldur — system DLL adresleri bu boot'ta tum process'lerde ayni */
    const IMAGE_DATA_DIRECTORY *impD =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impD->VirtualAddress) {
        IMAGE_IMPORT_DESCRIPTOR *imp =
            (IMAGE_IMPORT_DESCRIPTOR *)(img + impD->VirtualAddress);
        for (; imp->Name; imp++) {
            HMODULE hd = LoadLibraryA((const char *)(img + imp->Name));
            if (!hd) continue;
            IMAGE_THUNK_DATA64
                *thk  = (IMAGE_THUNK_DATA64 *)(img + imp->FirstThunk),
                *orig = (IMAGE_THUNK_DATA64 *)(img +
                        (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                 : imp->FirstThunk));
            for (; orig->u1.AddressOfData; thk++, orig++) {
                FARPROC fn = NULL;
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                    fn = GetProcAddress(hd, (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
                else {
                    IMAGE_IMPORT_BY_NAME *ibn =
                        (IMAGE_IMPORT_BY_NAME *)(img + orig->u1.AddressOfData);
                    fn = GetProcAddress(hd, ibn->Name);
                }
                thk->u1.Function = (ULONGLONG)fn;
            }
        }
    }

    /* Duzeltilmis image'i remote process'e yaz */
    SIZE_T wr = 0;
    if (!WriteProcessMemory(hProc, rBase, img, imgSz, &wr) || wr != imgSz) {
        free(img); VirtualFreeEx(hProc, rBase, 0, MEM_RELEASE); return FALSE;
    }
    free(img);

    /* Shellcode'u hazirla: entry point adresini patch et */
    unsigned char sc[sizeof(_sc_tmpl)];
    memcpy(sc, _sc_tmpl, sizeof(_sc_tmpl));
    UINT_PTR entryAddr = (UINT_PTR)rBase + nt->OptionalHeader.AddressOfEntryPoint;
    memcpy(sc + SC_ADDR_OFF, &entryAddr, sizeof(entryAddr));

    /* Shellcode'u image'in hemen ardindan yaz */
    LPVOID scAddr = (LPVOID)((BYTE *)rBase + imgSz);
    WriteProcessMemory(hProc, scAddr, sc, sizeof(sc), &wr);

    /* Remote thread: shellcode'u calistir, imageBase'i parametre olarak gec */
    HANDLE ht = CreateRemoteThread(hProc, NULL, 0,
                 (LPTHREAD_START_ROUTINE)scAddr, rBase, 0, NULL);
    if (!ht) { VirtualFreeEx(hProc, rBase, 0, MEM_RELEASE); return FALSE; }
    WaitForSingleObject(ht, 8000);
    CloseHandle(ht);
    return TRUE;
}

/* ── Self-delete ────────────────────────────────────────────────── */
static void _selfDelete(void) {
    char p[MAX_PATH] = {};
    if (GetModuleFileNameA(NULL, p, MAX_PATH))
        MoveFileExA(p, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
}

/* ══════════════════════════════════════════════════════════════════
   Win32 GUI  (MoonLoader ile ayni tabanli, baslik farklı)
   ══════════════════════════════════════════════════════════════════ */
#define WM_SETSTATE (WM_USER + 1)
#define ID_EDIT     101
#define ID_BTN      102
#define W_W         400
#define W_H         210

static COLORREF C(DWORD rgb) { return RGB(rgb>>16,(rgb>>8)&0xFF,rgb&0xFF); }
#define CB_BG   0x0D0D0Du
#define CB_CARD 0x1A1A1Au
#define CB_BDR  0x2A2A2Au
#define CB_TXT  0xE0E0E0u
#define CB_MUT  0x888888u
#define CB_ACC  0x5C6BC0u
#define CB_OK   0x50DC78u
#define CB_ERR  0xDC4646u
#define CB_WRN  0xF0A040u

static HWND   g_hwnd  = NULL;
static HWND   g_hEdit = NULL;
static HWND   g_hBtn  = NULL;
static HFONT  g_fNorm = NULL;
static HFONT  g_fBold = NULL;
static HBRUSH g_brEdit = NULL;

static char g_hwid[32]  = {};
static char g_msg[256]  = "Connecting...";
static int  g_msgCol    = 0;
static int  g_showInput = 0;

static char   g_keyBuf[128] = {};
static HANDLE g_keyEvent    = NULL;

static void _paint(HDC hdc, HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    HBRUSH bgBr = CreateSolidBrush(C(CB_BG));
    FillRect(hdc, &rc, bgBr); DeleteObject(bgBr);
    HPEN bdrPen = CreatePen(PS_SOLID, 1, C(CB_BDR));
    SelectObject(hdc, bdrPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right-1, rc.bottom-1);
    DeleteObject(bdrPen);
    HBRUSH cardBr = CreateSolidBrush(C(CB_CARD));
    RECT tr = {0, 0, rc.right, 44};
    FillRect(hdc, &tr, cardBr); DeleteObject(cardBr);
    HPEN sepPen = CreatePen(PS_SOLID, 1, C(CB_BDR));
    SelectObject(hdc, sepPen);
    MoveToEx(hdc, 0, 44, NULL); LineTo(hdc, rc.right, 44);
    DeleteObject(sepPen);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_fBold);
    SetTextColor(hdc, C(CB_TXT));
    TextOutA(hdc, 18, 14, "Moon Private", 12);
    SelectObject(hdc, g_fNorm);
    SetTextColor(hdc, C(CB_MUT));
    TextOutA(hdc, 108, 17, "v2.0  internal", 14);
    SetTextColor(hdc, C(CB_MUT));
    TextOutA(hdc, rc.right - 24, 15, "x", 1);
    SetTextColor(hdc, C(CB_MUT));
    TextOutA(hdc, 18, 58, "HWID", 4);
    SetTextColor(hdc, C(CB_TXT));
    TextOutA(hdc, 68, 58, g_hwid, (int)strlen(g_hwid));
    COLORREF sc;
    switch (g_msgCol) {
        case 1:  sc = C(CB_OK);  break;
        case 2:  sc = C(CB_ERR); break;
        case 3:  sc = C(CB_WRN); break;
        default: sc = C(CB_MUT); break;
    }
    SetTextColor(hdc, sc);
    RECT msgRc = {18, 82, rc.right - 18, 148};
    DrawTextA(hdc, g_msg, -1, &msgRc, DT_LEFT | DT_WORDBREAK);
    if (g_showInput) {
        SetTextColor(hdc, C(CB_MUT));
        TextOutA(hdc, 18, 152, "License Key", 11);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = GetModuleHandleA(NULL);
        g_fNorm  = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                   DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_fBold  = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                   DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_brEdit = CreateSolidBrush(RGB(0x11, 0x11, 0x11));
        g_keyEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_hEdit = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | ES_AUTOHSCROLL,
            18, 170, 270, 26, hwnd, (HMENU)ID_EDIT, hi, NULL);
        g_hBtn  = CreateWindowA("BUTTON", "Activate",
            WS_CHILD | BS_OWNERDRAW,
            296, 170, 86, 26, hwnd, (HMENU)ID_BTN, hi, NULL);
        SendMessage(g_hEdit, WM_SETFONT, (WPARAM)g_fNorm, TRUE);
        SendMessage(g_hBtn,  WM_SETFONT, (WPARAM)g_fNorm, TRUE);
        ShowWindow(g_hEdit, SW_HIDE);
        ShowWindow(g_hBtn,  SW_HIDE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        _paint(hdc, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlID == ID_BTN) {
            BOOL pr = (d->itemState & ODS_SELECTED);
            HBRUSH br = CreateSolidBrush(pr ? C(0x4A56A0) : C(CB_ACC));
            FillRect(d->hDC, &d->rcItem, br); DeleteObject(br);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, RGB(255,255,255));
            SelectObject(d->hDC, g_fNorm);
            DrawTextA(d->hDC, "Activate", -1, &d->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return TRUE;
    }
    case WM_CTLCOLOREDIT: {
        SetTextColor((HDC)wp, C(CB_TXT));
        SetBkColor  ((HDC)wp, RGB(0x11, 0x11, 0x11));
        return (LRESULT)g_brEdit;
    }
    case WM_COMMAND: {
        if (LOWORD(wp) == ID_BTN) {
            GetWindowTextA(g_hEdit, g_keyBuf, sizeof(g_keyBuf));
            if (strlen(g_keyBuf) >= 4) SetEvent(g_keyEvent);
        }
        return 0;
    }
    case WM_SETSTATE: {
        g_showInput = (int)wp;
        ShowWindow(g_hEdit, g_showInput ? SW_SHOW : SW_HIDE);
        ShowWindow(g_hBtn,  g_showInput ? SW_SHOW : SW_HIDE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProc(hwnd, msg, wp, lp);
        if (r == HTCLIENT) {
            POINT pt = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y <= 44) return HTCAPTION;
        }
        return r;
    }
    case WM_LBUTTONUP: {
        POINT pt = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
        RECT rc; GetClientRect(hwnd, &rc);
        if (pt.x >= rc.right-34 && pt.x <= rc.right-6 && pt.y >= 6 && pt.y <= 34)
            PostQuitMessage(0);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void _setMsg(const char *msg, int col, int showInput) {
    strncpy(g_msg, msg, sizeof(g_msg) - 1);
    g_msgCol = col;
    PostMessage(g_hwnd, WM_SETSTATE, (WPARAM)showInput, 0);
}

/* ══════════════════════════════════════════════════════════════════
   Worker thread
   ══════════════════════════════════════════════════════════════════ */
static DWORD WINAPI WorkerThread(LPVOID) {
    char tok[128] = {};
    int  tries    = 0;

    /* ── 1. Lisans dogrula ───────────────────────────────────── */
    while (tries++ < 25) {
        tok[0] = 0;
        int r = _auth(g_hwid, tok);

        if (r == 1) break;  /* Token alindi, DLL indir */

        if (r == -1) {
            _selfDelete();
            _setMsg("Access denied. This copy has been revoked.", 2, 0);
            Sleep(3000); PostMessage(g_hwnd, WM_DESTROY, 0, 0); return 0;
        }
        if (r == -2) {
            _setMsg("License expired. Contact support to renew.", 3, 0);
            Sleep(INFINITE); return 0;
        }
        if (r == -3) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Activation required.\nHWID: %s", g_hwid);
            _setMsg(buf, 3, 1);
            DWORD wr = WaitForSingleObject(g_keyEvent, 10 * 60 * 1000);
            if (wr == WAIT_OBJECT_0 && strlen(g_keyBuf) >= 4) {
                _setMsg("Activating...", 0, 0);
                if (_activate(g_hwid, g_keyBuf)) {
                    _setMsg("Key activated! Connecting...", 1, 0);
                    Sleep(800); tries--;
                } else {
                    _setMsg("Invalid key or already used. Try again.", 2, 1);
                    tries--;
                }
                g_keyBuf[0] = 0;
            }
            Sleep(2000); continue;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Connecting to server... (%d)", tries);
        _setMsg(buf, 0, 0);
        Sleep(20000);
    }

    if (!tok[0]) {
        _setMsg("Could not reach server.", 2, 0);
        Sleep(INFINITE); return 0;
    }

    /* ── 2. DLL indir ───────────────────────────────────────── */
    _setMsg("Downloading...", 0, 0);
    DWORD sz = 0;
    unsigned char *dll = _dl(tok, &sz);
    if (!dll || sz < 0x200) {
        free(dll);
        _setMsg("Download failed.", 2, 0);
        Sleep(INFINITE); return 0;
    }

    /* ── 3. FiveM'i bekle ───────────────────────────────────── */
    _setMsg("Waiting for FiveM...", 3, 0);
    DWORD pid = 0;
    for (int w = 0; w < 300 && !pid; w++) {  /* Maks. 10 dakika */
        pid = _find_fivem();
        if (!pid) Sleep(2000);
    }
    if (!pid) {
        free(dll);
        _setMsg("FiveM not found. Start FiveM and try again.", 2, 0);
        Sleep(INFINITE); return 0;
    }

    /* ── 4. FiveM'e inject et ───────────────────────────────── */
    _setMsg("Injecting...", 0, 0);
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        free(dll);
        _setMsg("Cannot open FiveM process. Run as administrator.", 2, 0);
        Sleep(INFINITE); return 0;
    }

    BOOL ok = _inject(hProc, dll, sz);
    free(dll);
    CloseHandle(hProc);

    if (!ok) {
        _setMsg("Injection failed.", 2, 0);
        Sleep(INFINITE); return 0;
    }

    /* ── 5. Basarili — pencereyi kapat ──────────────────────── */
    _setMsg("Injected!", 1, 0);
    Sleep(1500);
    PostMessage(g_hwnd, WM_DESTROY, 0, 0);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
   WinMain
   ══════════════════════════════════════════════════════════════════ */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    _hwid(g_hwid, sizeof(g_hwid));

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MoonInternal";
    RegisterClassExA(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    g_hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW, "MoonInternal", "Moon Private",
        WS_POPUP | WS_VISIBLE,
        (sx - W_W) / 2, (sy - W_H) / 2, W_W, W_H,
        NULL, NULL, hi, NULL);

    HANDLE wt = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    if (wt) CloseHandle(wt);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
