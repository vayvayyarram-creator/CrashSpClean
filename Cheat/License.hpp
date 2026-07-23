#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#include <string>
#pragma comment(lib, "winhttp.lib")

// Scrambled<T> — GuiGlobal.hpp'dan geliyor (once include edilmeli)

/* ── License state ──────────────────────────────────────────── */
namespace License {

    namespace State {
        inline Scrambled<bool> Valid;       // son ping OK
        inline Scrambled<bool> Checked;     // ilk ping yapildi
        inline Scrambled<bool> Expired;     // suresi dolmus
        inline Scrambled<bool> Activated;   // key girildi ve kabul edildi
        inline std::string ExpiresAt;       // "Xd Yh" veya "unlimited"
        inline std::string LastError;
        inline std::string UserTag;         // sunucudan gelen tag/kullanici adi
    }

    /* 4-byte rotating XOR key */
    static const unsigned char _LK[4] = {0x3F, 0x7A, 0x11, 0xC4};
    static void _lxd(char *d, const unsigned char *s, int n) {
        for (int i = 0; i < n; i++) d[i] = (char)(s[i] ^ _LK[i & 3]);
    }

    /* ── Thread hiding ──────────────────────────────────────── */
    static void _hideThread(HANDLE h = GetCurrentThread()) {
        typedef LONG (NTAPI *_tSIT)(HANDLE, ULONG, PVOID, ULONG);
        static _tSIT _fn = nullptr;
        if (!_fn) {
            /* "NtSetInformationThread" ^ {0x3F,0x7A,0x11,0xC4} */
            static const unsigned char _sS[] = {
                0x71,0x0E,0x42,0xA1,0x4B,0x33,0x7F,0xA2,0x50,0x08,
                0x7C,0xA5,0x4B,0x13,0x7E,0xA2,0x6B,0x12,0x63,0xA1,
                0x5E,0x1E, 0
            };
            /* "ntdll.dll" ^ key */
            static const unsigned char _sN[] = {
                0x51,0x0E,0x75,0xA8,0x53,0x54,0x75,0xA8,0x53, 0
            };
            char nd[12], ns[28];
            _lxd(nd, _sN, 9);  nd[9]  = 0;
            _lxd(ns, _sS, 22); ns[22] = 0;
            HMODULE hN = GetModuleHandleA(nd);
            if (hN) _fn = (_tSIT)(void*)GetProcAddress(hN, ns);
        }
        if (_fn) _fn(h, 17, nullptr, 0);
    }

    /* ── HWID: FNV-1a hash (MachineGuid + ComputerName) ────────
       Roblox tarafiyla ayni algoritma — tek DB'de cakisma olmaz */
    static std::string GetHWID() {
        std::string raw;

        HKEY hk;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Microsoft\\Cryptography",
                0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
            char guid[128] = {};
            DWORD sz = sizeof(guid);
            RegQueryValueExA(hk, "MachineGuid", nullptr, nullptr, (LPBYTE)guid, &sz);
            RegCloseKey(hk);
            raw += guid;
        }

        char cn[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD cnSz = sizeof(cn);
        GetComputerNameA(cn, &cnSz);
        raw += cn;

        unsigned long long h = 14695981039346656037ULL;
        for (char c : raw) { h ^= (unsigned char)c; h *= 1099511628211ULL; }

        /* manual %016llX (wsprintf bunu desteklemez) */
        char buf[17] = {};
        const char *hx = "0123456789ABCDEF";
        for (int i = 15; i >= 0; i--) { buf[i] = hx[h & 0xF]; h >>= 4; }
        return std::string(buf, 16);
    }

    /* ── Key kaydet / yukle (HKCU registry, kamufle CLSID) ───── */
    static const char _kReg[] =
        "SOFTWARE\\Classes\\CLSID\\{D3E54C21-B1A9-4F3E-8C12-9A1B7E3F2D0C}";

    static void _saveKey(const std::string &key) {
        HKEY hk;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, _kReg, 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, nullptr)
                != ERROR_SUCCESS) return;
        RegSetValueExA(hk, "Default", 0, REG_SZ,
                       (LPBYTE)key.c_str(), (DWORD)key.size() + 1);
        RegCloseKey(hk);
    }

    static std::string _loadKey() {
        HKEY hk;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, _kReg, 0, KEY_READ, &hk)
                != ERROR_SUCCESS) return "";
        char buf[128] = {};
        DWORD sz = sizeof(buf), type = 0;
        RegQueryValueExA(hk, "Default", nullptr, &type, (LPBYTE)buf, &sz);
        RegCloseKey(hk);
        std::string k(buf);
        /* trim */
        while (!k.empty() && (k.back()=='\r'||k.back()=='\n'||k.back()==' '))
            k.pop_back();
        return k;
    }

    static void _deleteKey() {
        RegDeleteKeyA(HKEY_CURRENT_USER, _kReg);
    }

    /* ── Minimal JSON string value extractor ─────────────────── */
    static std::string _jval(const std::string &json, const std::string &field) {
        std::string search = "\"" + field + "\"";
        size_t p = json.find(search);
        if (p == std::string::npos) return "";
        p = json.find(":", p + search.size());
        if (p == std::string::npos) return "";
        p = json.find("\"", p);
        if (p == std::string::npos) return "";
        p++;
        size_t e = json.find("\"", p);
        if (e == std::string::npos) return "";
        return json.substr(p, e - p);
    }

    /* ── WinHTTP HTTPS POST → JSON body ─────────────────────── */
    static std::string _httpPost(const std::string &body) {
        std::string result;

        /* "archive.rent" ^ {0x3F,0x7A,0x11,0xC4} */
        static const unsigned char _sDom[] = {
            0x5E,0x08,0x72,0xAC,0x56,0x0C,0x74,0xEA,
            0x4D,0x1F,0x7F,0xB0, 0
        };
        /* "/storage/sync" ^ key */
        static const unsigned char _sPath[] = {
            0x10,0x09,0x65,0xAB,0x4D,0x1B,0x76,0xA1,
            0x10,0x09,0x68,0xAA,0x5C, 0
        };

        char dom[16], pth[16];
        _lxd(dom, _sDom,  12); dom[12] = 0;
        _lxd(pth, _sPath, 13); pth[13] = 0;

        std::wstring wDom(dom, dom + 12);
        std::wstring wPth(pth, pth + 13);

        HINTERNET hSes = WinHttpOpen(L"Mozilla/5.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSes) return result;

        HINTERNET hCon = WinHttpConnect(hSes, wDom.c_str(),
                                        443, 0);
        if (!hCon) { WinHttpCloseHandle(hSes); return result; }

        HINTERNET hReq = WinHttpOpenRequest(hCon, L"POST", wPth.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hReq) {
            WinHttpCloseHandle(hCon);
            WinHttpCloseHandle(hSes);
            return result;
        }

        DWORD to = 10000;
        WinHttpSetOption(hReq, WINHTTP_OPTION_CONNECT_TIMEOUT, &to, sizeof(to));
        WinHttpSetOption(hReq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));

        /* "X-Cache-Token: Kx9mP2nQ" ^ {0x3F,0x7A,0x11,0xC4} — secret client header */
        static const unsigned char _sHN[] = {
            0x67,0x57,0x52,0xA5,0x5C,0x12,0x74,0xE9,0x6B,0x15,0x7A,0xA1,0x51, 0
        };
        static const unsigned char _sHV[] = {
            0x74,0x02,0x28,0xA9,0x6F,0x48,0x7F,0x95, 0
        };
        char hnam[16], hval[12];
        _lxd(hnam, _sHN, 13); hnam[13] = 0;
        _lxd(hval, _sHV,  8); hval[8]  = 0;
        std::string hdrStr = std::string(hnam) + ": " + hval;
        std::wstring wHdr(hdrStr.begin(), hdrStr.end());
        WinHttpAddRequestHeaders(hReq, wHdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        const wchar_t *ct = L"Content-Type: application/json";
        BOOL ok = WinHttpSendRequest(hReq, ct, (DWORD)-1,
                    (LPVOID)body.c_str(), (DWORD)body.size(),
                    (DWORD)body.size(), 0)
               && WinHttpReceiveResponse(hReq, nullptr);

        if (ok) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD rd = 0;
                if (WinHttpReadData(hReq, chunk.data(), avail, &rd))
                    result.append(chunk.data(), rd);
            }
        }

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hCon);
        WinHttpCloseHandle(hSes);
        return result;
    }

    /* ── Verify: archive.rent/storage/sync → durum kodu ────────
       1 = gecerli   -1 = suresi dolmus   0 = banlı   -2 = baglanti hatası */
    static int _verify(const std::string &key) {
        if (key.empty()) return -2;

        std::string hwid = GetHWID();
        std::string body = "{\"token\":\"" + key + "\","
                           "\"device\":\"" + hwid + "\","
                           "\"product\":\"fivem\"}";

        std::string resp = _httpPost(body);
        if (resp.empty()) return -2;

        std::string code = _jval(resp, "code");
        std::string info = _jval(resp, "info");
        std::string ttl  = _jval(resp, "ttl");
        std::string tag  = _jval(resp, "tag");

        if (code == "ok") {
            State::Valid     = true;
            State::Expired   = false;
            State::ExpiresAt = ttl;
            State::UserTag   = tag;
            State::LastError.clear();
            return 1;
        }

        State::Valid = false;
        if (info == "expired") {
            State::Expired   = true;
            State::LastError = "License expired";
            return -1;
        }
        if (info == "blocked" || info == "disabled") {
            State::LastError = "License denied";
            return 0;
        }
        State::LastError = info.empty() ? "Connection error" : info;
        return -2;
    }

    /* ── Self-delete ────────────────────────────────────────── */
    static void _selfDelete() {
        char p[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, p, MAX_PATH))
            MoveFileExA(p, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    /* ── Activate: key gir, sunucuya dogrulat, kaydet ────────── */
    static bool Activate(const std::string &key) {
        if (key.empty()) return false;
        int r = _verify(key);
        if (r == 1) {
            _saveKey(key);
            State::Activated = true;
            return true;
        }
        if (State::LastError.empty())
            State::LastError = "Invalid or expired key";
        return false;
    }

    /* ── 8-char MachineGuid HWID (cheatload.c/InternalLoader ile ayni format) ── */
    static std::string _hwid8() {
        HKEY hk;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Microsoft\\Cryptography",
                0, KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
            return "";
        char guid[128] = {};
        DWORD sz = sizeof(guid);
        RegQueryValueExA(hk, "MachineGuid", nullptr, nullptr, (LPBYTE)guid, &sz);
        RegCloseKey(hk);
        std::string out;
        for (char c : std::string(guid)) {
            if (c == '-') continue;
            out += (char)toupper((unsigned char)c);
            if (out.size() == 8) break;
        }
        return out;
    }

    /* ── HTTP GET /ping?h=HWID → "OK:exp:tag" | "EXPIRED" | "DENY" ─────────
       CFG_SRV_HOST / port 6066 — Config.hpp'tan (Source.cpp include sirasi ile geliyor) */
    static std::string _httpPing(const std::string &hwid) {
        std::string result;
        HINTERNET hSes = WinHttpOpen(L"Mozilla/5.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSes) return result;

        HINTERNET hCon = WinHttpConnect(hSes, CFG_SRV_HOST, 6066, 0);
        if (!hCon) { WinHttpCloseHandle(hSes); return result; }

        std::wstring wPath = L"/ping?h=" + std::wstring(hwid.begin(), hwid.end());
        HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", wPath.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hReq) {
            WinHttpCloseHandle(hCon);
            WinHttpCloseHandle(hSes);
            return result;
        }

        DWORD to = 8000;
        WinHttpSetOption(hReq, WINHTTP_OPTION_CONNECT_TIMEOUT, &to, sizeof(to));
        WinHttpSetOption(hReq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));

        if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD rd = 0;
                if (WinHttpReadData(hReq, chunk.data(), avail, &rd))
                    result.append(chunk.data(), rd);
            }
        }
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hCon);
        WinHttpCloseHandle(hSes);
        return result;
    }

    /* ── Ping cevabini isle: "OK:expires_at:tag" | "OK" | "EXPIRED" | "DENY" ──
       expires_at içinde ':' olabilir (ör. "28:20"), bu yüzden rfind ile
       EN SON ':' bulunur → sağı=tag, solu=expires_at. */
    static void _applyPingResp(const std::string &resp) {
        if (resp.size() >= 3 && resp.substr(0, 3) == "OK:") {
            std::string rest = resp.substr(3);
            size_t col = rest.rfind(':');   // son ':' → tag sınırı
            std::string exp_part = (col != std::string::npos) ? rest.substr(0, col) : "";
            std::string tag_part = (col != std::string::npos) ? rest.substr(col + 1) : rest;
            State::Valid   = true;
            State::Expired = false;
            if (!exp_part.empty()) State::ExpiresAt = exp_part;
            if (!tag_part.empty()) State::UserTag   = tag_part;
        } else if (resp == "OK") {
            State::Valid   = true;
            State::Expired = false;
        } else if (resp == "EXPIRED") {
            State::Valid   = false;
            State::Expired = true;
        } else if (resp == "DENY") {
            State::Valid = false;
        }
        // empty resp = ag hatasi → mevcut durumu koru
    }

    /* ── Background ping thread ─────────────────────────────── */
    static void PingThread() {
        _hideThread();

        Sleep(5000);  /* Loader'in DLL'i map etmesini bekle */

        /* Optimistik baslangic — ilk ping cevabi gelince guncellenir */
        State::Valid   = true;
        State::Checked = true;
        State::Expired = false;
        if (State::UserTag.empty())
            State::UserTag = "Licensed";

        std::string hwid = _hwid8();

        /* Ilk ping — UserTag ve ExpiresAt guncellenir */
        _applyPingResp(_httpPing(hwid));
        State::Checked = true;

        while (State::Valid) {
            Sleep(300000u); /* 5 dakikada bir kontrol */
            _applyPingResp(_httpPing(hwid));
        }

        /* DENY/EXPIRED gelirse — sadece sessizce dur, ExitProcess yok */
        while (true) Sleep(3600000u);
    }

    /* ── DLL init'te bir kez cagir ─────────────────────────── */
    static void Start() {
        HANDLE h = CreateThread(nullptr, 0,
            [](LPVOID) -> DWORD { PingThread(); return 0; },
            nullptr, 0, nullptr);
        if (h) {
            _hideThread(h);
            CloseHandle(h);
        }
    }

} // namespace License
