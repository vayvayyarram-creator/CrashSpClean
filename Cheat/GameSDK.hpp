// ============================================================
//  GameSDK  —  GTA V / FiveM game structures, offsets, Ped
// ============================================================

#include <SimpleMath.h>
#pragma comment(lib, "winhttp.lib")
#include <winhttp.h>
#pragma comment(lib, "iphlpapi.lib")
#include <iphlpapi.h>

// "grcWindow" string'i bellekte düz literal olarak görünmemesi için
// runtime'da karakter karakter inşa ediliyor.
static const char* BuildClassName() {
    static char cls[10] = {};
    // g r c W i n d o w \0
    cls[0]='g'; cls[1]='r'; cls[2]='c';
    cls[3]='W'; cls[4]='i'; cls[5]='n';
    cls[6]='d'; cls[7]='o'; cls[8]='w'; cls[9]='\0';
    return cls;
}

static struct GameStruct {
    DWORD       pID         = 0;
    HANDLE      hProcess    = nullptr;
    HWND        hWnd        = nullptr;
    std::string Path;
    std::string Version;
    RECT        lpRect      = {};
    POINT       lpPoint     = {};
} Game;

struct OffsetsStruct {
    uintptr_t GameBase              = 0;
    uintptr_t CitizenPlayernamesBase= 0;
    uintptr_t NetBase               = 0;
    uintptr_t GameWorld             = 0;
    uintptr_t ReplayInterface       = 0;
    uintptr_t ViewPort              = 0;
    uintptr_t Camera                = 0;
    uintptr_t BoneList              = 0;
    uintptr_t WeaponManager         = 0;
    uintptr_t HandleBullet          = 0;
    uintptr_t VisibleFlag           = 0;
    uintptr_t BlipList              = 0;
    uintptr_t Vehicle               = 0;
    uintptr_t Waypoint              = 0;
    uintptr_t LocalPlayer           = 0;
    uintptr_t PlayerInfo            = 0;
    uintptr_t Id                    = 0;
    uintptr_t Health                = 0;
    uintptr_t MaxHealth             = 0;
    uintptr_t Armor                 = 0;
    // Ek offsetler (b3570 ile doğrulandı)
    uintptr_t WeaponInfo            = 0;    // PlayerInfo + 0x10C8 → m_pInfo
    uintptr_t DoorLock              = 0;    // Vehicle + 0x13C0
    uintptr_t FragInst              = 0;    // Ped + 0x1430
    uintptr_t Invisible             = 0;    // GameBase + 0x6AE372
    uintptr_t WeatherBase           = 0;    // GameBase + 0x25D503C
} Offsets;

#include "Memory.hpp"

// ============================================================
//  Helpers
// ============================================================
float GetDistance(Vector3 a, Vector3 b) {
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x*x + y*y + z*z);
}

bool Vec3Empty(const Vector3& v) { return v == Vector3(0.f, 0.f, 0.f); }

Vector3 Normalize(const Vector3& v) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    // Sorun 2 düzeltme: exact 0 karşılaştırması yerine eşik.
    // len çok küçük ama sıfır değilse (denormal float) böldükten sonra
    // 5e19 gibi değerler çıkar — isfinite=true geçer, kameraya yazılır, crash.
    if (len < 1e-6f) return {0,0,0};
    return {v.x/len, v.y/len, v.z/len};
}

Vector3 Vec3Transform(Vector3* vIn, Matrix* mIn) {
    return {
        vIn->x*mIn->_11 + vIn->y*mIn->_21 + vIn->z*mIn->_31 + mIn->_41,
        vIn->x*mIn->_12 + vIn->y*mIn->_22 + vIn->z*mIn->_32 + mIn->_42,
        vIn->x*mIn->_13 + vIn->y*mIn->_23 + vIn->z*mIn->_33 + mIn->_43
    };
}

// ============================================================
//  Bone enum
// ============================================================
enum Bone : int { Head=0, LeftFoot, RightFoot, LeftAnkle, RightAnkle,
                  LeftHand, RightHand, Neck, Hip };

struct Bones {
    Vector3 Head;       char p0[4]{};
    Vector3 LeftFoot;   char p1[4]{};
    Vector3 RightFoot;  char p2[4]{};
    Vector3 LeftAnkle;  char p3[4]{};
    Vector3 RightAnkle; char p4[4]{};
    Vector3 LeftHand;   char p5[4]{};
    Vector3 RightHand;  char p6[4]{};
    Vector3 Neck;       char p7[4]{};
    Vector3 Hip;
};

// ============================================================
//  Friend list
// ============================================================
struct Friend { int id; std::string alias; }; // alias: bos ise gercek isim gosterilir
std::vector<Friend> friendList;
// Oyunculara ozel isim alias'lari (ID -> alias)
std::unordered_map<int,std::string> g_playerAliases;

bool IsFriend(int searchId) {
    if (searchId < 0 || friendList.empty()) return false;
    for (const Friend& f : friendList)
        if (f.id == searchId) return true;
    return false;
}
void RemoveFriend(int removeId) {
    friendList.erase(std::remove_if(friendList.begin(), friendList.end(),
        [removeId](const Friend& f){ return f.id == removeId; }), friendList.end());
}

// ============================================================
//  Vehicle struct
// ============================================================
struct Vehicle {
    uintptr_t Pointer;
    Vector2   Location;
    float     Health;
    float     Distance;
};

// ============================================================
//  Player name — HTTP /players.json üzerinden alınır.
//  citizen-playernames-five.dll analizi (2026-05-10 build):
//   · DLL artık .data'da basit linked list KULLANMIYOR.
//   · 0x31100 = __security_cookie, 0x31EC8 = GTA5 ASLR delta,
//     0x31FA8 = TLS slot index, 0x31C18 = hook registration list.
//   · İsimler heap + TLS'e yazılıyor; external erişim için
//     HTTP /players.json API'si tek güvenilir yoldur.
// ============================================================

// Name cache (ID -> name), mutex ile korunan
static std::unordered_map<int, std::string> g_nameCache;
static std::mutex                            g_nameMutex;
static DWORD                                 g_nameCacheTime = 0;
static bool                                  g_nameReady     = false;

// Kalıcı isim cache'i — UpdatePeds() her tick'te Ped listesini yeniden oluşturur,
// bu map sayesinde bir kez öğrenilen isimler kaybolmaz.
static std::unordered_map<int, std::string> g_pedNamePersist;

// Çözümlenemeyen ID'ler — "P#" gösterilen oyuncular.
static std::unordered_set<int> g_unresolvedIds;

// Yeni P# göründüğünde NameCacheThread'e "hemen yenile" sinyali
static volatile bool g_needsImmediateRefresh = false;

// DLL scan artık desteklenmiyor (yeni build'de yapı değişti) — stub
static uintptr_t s_namesListOff = 0;  // kullanılmıyor, uyumluluk için
static void RefreshNameCache() { /* DLL linked-list approach retired */ }

// ============================================================
//  HTTP players.json — FiveM sunucusundan oyuncu isimleri
//  Sunucu IP keşfi: GTA5 TCP bağlantıları (birincil) +
//                   crashometry dosyası (ikincil)
// ============================================================
static std::string g_srvIp;
static int         g_srvPort = 0;

// ── Yardımcı: bir ip:port çiftinin geçerli /players.json döndürüp döndürmediğini test et
// Test modunda: sadece ilk 512 byte kontrol et, tam JSON bekleme yok.
static bool ProbePlayersJson(const std::string& ip, int port) {
    std::wstring wIp(ip.begin(), ip.end());
    HINTERNET hS = WinHttpOpen(XorString(L"a"), WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;
    DWORD t = 2000;
    WinHttpSetOption(hS, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof t);
    WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof t);
    HINTERNET hC = WinHttpConnect(hS, wIp.c_str(), (INTERNET_PORT)port, 0);
    bool ok = false;
    if (hC) {
        HINTERNET hR = WinHttpOpenRequest(hC, XorString(L"GET"), XorString(L"/players.json"),
                                          nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hR) {
            if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hR, nullptr)) {
                char buf[8] = {};
                DWORD got = 0;
                WinHttpReadData(hR, buf, 7, &got);
                ok = (got > 0 && buf[0] == '[');  // JSON array başlıyor
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
    return ok;
}

// ── Birincil: GTA5 TCP bağlantılarından FiveM sunucu IP:port bul
// FiveM, oyun sunucusuna TCP üzerinden bağlanır (HTTP endpoint aynı port).
static bool DiscoverServerFromTCP() {
    if (!Game.pID) return false;

    DWORD sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (!sz) return false;
    std::vector<BYTE> buf(sz + 4096);
    if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) return false;

    auto* tbl = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
    for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
        auto& row = tbl->table[i];
        if (row.dwOwningPid != Game.pID)           continue;
        if (row.dwState    != MIB_TCP_STATE_ESTAB)  continue;

        WORD remPort = ntohs((WORD)row.dwRemotePort);
        // FiveM sunucuları: genellikle 22000-45000 aralığı; loopback atla
        if (remPort < 22000 || remPort > 45000)    continue;
        if (row.dwRemoteAddr == 0x0100007F)         continue; // 127.0.0.1

        IN_ADDR addr; addr.S_un.S_addr = row.dwRemoteAddr;
        char ipBuf[20] = {};
        inet_ntop(AF_INET, &addr, ipBuf, sizeof(ipBuf));

        // Bu bağlantının gerçekten FiveM sunucusu olduğunu doğrula
        if (ProbePlayersJson(std::string(ipBuf), (int)remPort)) {
            g_srvIp   = ipBuf;
            g_srvPort = (int)remPort;
            char dbg[80];
            sprintf(dbg, "[Names] TCP discovery: %s:%d", ipBuf, (int)remPort);
            Debug(dbg, LOG_INFO);
            return true;
        }
    }
    return false;
}

// ── İkincil: Crashometry dosyasından IP oku (MessagePack binary taraması)
// Crashometry bir MessagePack binary dosyasıdır; ASCII string anahtarları
// length-prefix'li olarak içinde tutulur. "last_server\0" substring'ini
// ve ardından "ip:port" formatında bir string'i bul.
static bool ParseCrashometry() {
    HKEY hKey = nullptr;
    WCHAR wBuf[MAX_PATH] = {};
    DWORD sz2 = sizeof(wBuf);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      XorString(L"SOFTWARE\\CitizenFX\\FiveM"),
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    bool ok = (RegQueryValueExW(hKey, XorString(L"Last Run Location"),
                                nullptr, nullptr, (LPBYTE)wBuf, &sz2) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    if (!ok) return false;

    char aPath[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, wBuf, -1, aPath, MAX_PATH, nullptr, nullptr);
    std::string basePath(aPath);
    if (!basePath.empty() && basePath.back() != '\\') basePath += '\\';

    // Olası crashometry konumları
    const char* kPaths[] = {
        "data\\cache\\crashometry",
        "FiveM.app\\data\\cache\\crashometry",
        nullptr
    };
    std::string raw;
    for (int pi = 0; kPaths[pi]; ++pi) {
        std::string fp = basePath + kPaths[pi];
        HANDLE hF = CreateFileA(fp.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hF == INVALID_HANDLE_VALUE) continue;
        DWORD fsz = GetFileSize(hF, nullptr);
        if (fsz > 0 && fsz < 8 * 1024 * 1024) {
            raw.resize(fsz); DWORD nr = 0;
            ReadFile(hF, &raw[0], fsz, &nr, nullptr);
            raw.resize(nr);
        }
        CloseHandle(hF);
        if (!raw.empty()) break;
    }
    if (raw.empty()) return false;

    // MessagePack'ta string key'leri: ölçek byte'ı (0xa0-0xbf inline veya
    // 0xd9/0xda/0xdb length-prefixed) + ASCII baytları.
    // "last_server" içeren her konumda hemen ardından gelen
    // printable ip:port string'ini ara.
    const std::string kKey = XorString("last_server");
    bool found = false;
    for (size_t p = 0; p + kKey.size() < raw.size(); ++p) {
        // substring match — "last_server_url" yerine "last_server" ile başlayan AND
        // hemen ardından 'l','u','r','l' gelmeyen
        if (raw.compare(p, kKey.size(), kKey) != 0)    continue;
        if (raw[p + kKey.size()] == 'l')                continue; // "_url" atla

        // Bu konumdan +0..+32 aralığında ip:port formatında bir string ara
        for (size_t off = p + kKey.size(); off < p + kKey.size() + 64 && off < raw.size(); ++off) {
            unsigned char c = (unsigned char)raw[off];
            if (c < 0x20) continue;       // non-printable: atla
            // Printable başlangıç — tüm string'i oku
            std::string candidate;
            for (size_t j = off; j < raw.size() && j < off + 64; ++j) {
                char cc = raw[j];
                if (cc == '\0' || (unsigned char)cc < 0x20) break;
                candidate += cc;
            }
            // ip:port formatı mı?
            auto col = candidate.rfind(':');
            if (col == std::string::npos || col == 0 || col >= candidate.size()-1) break;
            std::string ip = candidate.substr(0, col);
            int port = 0;
            for (char x : candidate.substr(col+1)) {
                if (!isdigit((unsigned char)x)) break;
                port = port * 10 + (x - '0');
            }
            if (!ip.empty() && port > 1024 && port <= 65535) {
                g_srvIp   = ip;
                g_srvPort = port;
                char dbg[80];
                sprintf(dbg, "[Names] Crashometry: %s:%d", ip.c_str(), port);
                Debug(dbg, LOG_INFO);
                found = true;
            }
            break;
        }
        if (found) break;
    }
    return found;
}

// ── Sunucu adresini bul: TCP keşfi → crashometry fallback
static bool FindServerAddress() {
    if (!g_srvIp.empty() && g_srvPort > 0) return true;
    if (DiscoverServerFromTCP()) return true;
    if (ParseCrashometry())      return true;
    return false;
}

// WinHTTP ile GET istegi
static std::string WinHttpGet(const std::string& ip, int port, const char* path) {
    std::wstring wIp(ip.begin(), ip.end());
    HINTERNET hS = WinHttpOpen(XorString(L"a"), WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return "";
    DWORD t = 3000;
    WinHttpSetOption(hS, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof t);
    WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof t);
    WinHttpSetOption(hS, WINHTTP_OPTION_SEND_TIMEOUT,    &t, sizeof t);
    HINTERNET hC = WinHttpConnect(hS, wIp.c_str(), (INTERNET_PORT)port, 0);
    if (!hC) { WinHttpCloseHandle(hS); return ""; }
    std::wstring wPath(path, path + strlen(path));
    HINTERNET hR = WinHttpOpenRequest(hC, XorString(L"GET"), wPath.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return ""; }
    bool ok = WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
           && WinHttpReceiveResponse(hR, nullptr);
    std::string result;
    if (ok) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
            std::vector<char> tmp(avail + 1, 0);
            DWORD got = 0;
            if (!WinHttpReadData(hR, tmp.data(), avail, &got)) break;
            result.append(tmp.data(), got);
            if (result.size() > 256 * 1024) break;
        }
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return result;
}

// players.json parse et ve cache'e ekle
static void FetchNamesFromHttp() {
    if (!FindServerAddress()) return;

    std::string json = WinHttpGet(g_srvIp, g_srvPort, XorString("/players.json"));
    if (json.size() < 2 || json[0] != '[') {
        static int s_httpFails = 0;
        // 4 art arda başarısızlıkta IP'yi sıfırla — bir sonraki döngüde yeniden keşfeder
        if (++s_httpFails >= 4) {
            s_httpFails  = 0;
            g_srvIp.clear();
            g_srvPort    = 0;
        }
        return;
    }
    static int s_httpFails = 0; s_httpFails = 0;

    std::unordered_map<int, std::string> additions;
    size_t pos = 0;
    while (pos < json.size()) {
        size_t s = json.find('{', pos);
        if (s == std::string::npos) break;
        size_t e = json.find('}', s);
        if (e == std::string::npos) break;
        pos = e + 1;

        // "id": N
        auto ip2 = json.find("\"id\":", s);
        if (ip2 == std::string::npos || ip2 > e) continue;
        ip2 += 5;
        while (ip2 < e && json[ip2] == ' ') ++ip2;
        int id = 0; bool hd = false;
        while (ip2 < e && isdigit((unsigned char)json[ip2])) { id = id*10+(json[ip2++]-'0'); hd=true; }
        if (!hd || id < 0 || id >= 2048) continue;

        // "name": "..."
        auto np = json.find("\"name\":", s);
        if (np == std::string::npos || np > e) continue;
        np += 7;
        while (np < e && json[np] != '"') ++np;
        if (np >= e) continue;
        ++np;  // opening "
        std::string name;
        while (np < e && json[np] != '"') {
            char c = json[np++];
            if (c == '\\') { if (np < e) ++np; continue; }
            name += c;
        }
        if (!name.empty()) additions[id] = std::move(name);
    }

    if (!additions.empty()) {
        std::lock_guard<std::mutex> lk(g_nameMutex);
        for (auto& kv : additions) {
            g_nameCache.insert_or_assign(kv.first, kv.second);
            g_pedNamePersist.insert_or_assign(kv.first, kv.second);
            g_unresolvedIds.erase(kv.first);   // artık çözüldü — P# kaldır
        }
        g_nameReady = true;
        // HTTP getirisi unresolved'ı temizlediyse immediate flag'i kapat
        if (g_unresolvedIds.empty()) g_needsImmediateRefresh = false;
        char dbg[64];
        sprintf(dbg, "[Names] HTTP: %zu isim alindi", additions.size());
        Debug(dbg, LOG_INFO);
    }
}

// Arka planda sürekli yenile.
// HTTP /players.json: birincil yöntem.
//   · İlk iterasyonda hemen çalışır (lastHttp=0, GetTickCount() büyük).
//   · İsim bulunamazsa (P# varsa) 3s'de bir, normalda 20s'de bir.
//   · g_needsImmediateRefresh=true ise anında çalışır.
// TCP sunucu keşfi: her 15 saniyede yeniden dener (IP değişebilir).
static void NameCacheThread() {
    DWORD lastHttp     = 0;
    DWORD lastTcpProbe = 0;
    while (true) {
        bool hasUnresolved = false;
        {
            std::lock_guard<std::mutex> lk(g_nameMutex);
            hasUnresolved = !g_unresolvedIds.empty();
        }

        DWORD now = GetTickCount();

        // 15 saniyede bir TCP keşfi yeniden dene (sunucu değişmiş olabilir)
        if (now - lastTcpProbe >= 15000u) {
            lastTcpProbe = now;
            if (g_srvIp.empty()) {
                DiscoverServerFromTCP();
                if (g_srvIp.empty()) ParseCrashometry();
            }
        }

        // Anlık yenileme sinyali (yeni P# göründü)
        if (g_needsImmediateRefresh) {
            g_needsImmediateRefresh = false;
            FetchNamesFromHttp();
            lastHttp = GetTickCount();
        } else {
            // P# olan oyuncu varsa 3s, yoksa 20s'de bir HTTP çek
            DWORD interval = hasUnresolved ? 3000u : 20000u;
            if (now - lastHttp >= interval) {
                lastHttp = now;
                FetchNamesFromHttp();
            }
        }

        // P# varsa 500ms, yoksa 1s döngü
        Sleep(hasUnresolved ? 500u : 1000u);
    }
}

// Geri uyumluluk: GetPedName(id)
static bool IsValidName(const char* buf, size_t maxlen = 32) {
    size_t len = strnlen(buf, maxlen);
    // En az 3 karakter — 2 karakterlik garbage geçmesini engeller
    if (len < 3 || len > 40) return false;
    int printable = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x20) return false;
        if (c >= 0x20 && c <= 0x7E) ++printable;
    }
    // En az yarısı standart ASCII olmalı (salt garbage unicode değil)
    return printable >= (int)(len / 2 + 1);
}

std::string GetPedName(int pedId) {
    if (pedId <= 0) return "";
    // Kullanici alias'i varsa once onu don
    {
        std::lock_guard<std::mutex> lk(g_nameMutex);
        auto ait = g_playerAliases.find(pedId);
        if (ait != g_playerAliases.end() && !ait->second.empty())
            return ait->second;
    }
    // Cache'ten bak
    {
        std::lock_guard<std::mutex> lk(g_nameMutex);
        auto it = g_nameCache.find(pedId);
        if (it != g_nameCache.end()) return it->second;
    }
    return "";
}

// Artik kullanilmiyor — eski koda uyumluluk icin bos stub
void ScanCitizenNames(int) {}

// Artik kullanilmiyor — stub
struct CitNameCache { std::unordered_map<int,std::string> names; bool ready=false; bool scanning=false; DWORD cacheTime=0; } g_cit;

// FALLBACK — PlayerInfo pointer'indan isim okuma girisimi (genellikle basarisiz)
static std::string TryReadNameFromPlayerInfo(uintptr_t pedPtr) {
    if (!pedPtr) return "";
    uintptr_t pi = ReadMemory<uintptr_t>(pedPtr + Offsets.PlayerInfo);
    if (pi < 0x100000ULL) return "";
    static const int kOff[] = {0xA0, 0x20, 0x78, 0x80, 0x90, 0x98, 0x170, 0x28, 0xB0, 0xC0};
    for (int off : kOff) {
        char buf[32] = {};
        if (!ReadProcessMemory(Game.hProcess, (LPVOID)(pi + off), buf, 28, nullptr)) continue;
        if (IsValidName(buf, 28)) return std::string(buf, strnlen(buf, 28));
        uintptr_t ptr = *reinterpret_cast<uintptr_t*>(buf);
        if (ptr >= 0x100000ULL && ptr <= 0x7FFFFFFFFFFFULL) {
            char buf2[32] = {};
            if (ReadProcessMemory(Game.hProcess, (LPVOID)ptr, buf2, 28, nullptr) && IsValidName(buf2, 28))
                return std::string(buf2, strnlen(buf2, 28));
        }
    }
    return "";
}

// ESKI KOD — artik kullanilmiyor, derleme uyumlulugu icin stub
std::string GetPedName_unused(int pedId) {
    if (pedId < 0) return "P#" + std::to_string(pedId);
    auto ait = g_playerAliases.find(pedId);
    if (ait != g_playerAliases.end() && !ait->second.empty())
        return ait->second;
    if (!Offsets.CitizenPlayernamesBase || !g_cit.ready)
        return "P#" + std::to_string(pedId);

    DWORD now = GetTickCount();
    if (now - g_cit.cacheTime > 4000) { g_cit.names.clear(); }
    auto it = g_cit.names.find(pedId);
    if (it != g_cit.names.end()) return it->second;

    return "P#" + std::to_string(pedId);
}

// ============================================================
//  Ped class
// ============================================================
class Ped {
public:
    uintptr_t   Pointer       = 0;
    uintptr_t   PlayerInfo    = 0;
    uintptr_t   WeaponManager = 0;
    Vector3     Position      = {};
    Matrix      BoneMatrix    = {};
    Vector3     BoneList[9]   = {};
    bool        IsFriend_     = false;
    float       Distance      = 0.f;  // LocalPlayer'a olan uzaklık (m)
    std::string Name;           // Update() tarafından doldurulur — GetName() sadece bunu döner

    bool GetPlayer(uintptr_t& Base) {
        Pointer = Base;
        return Pointer != 0;
    }

    bool Update() {
        if (!Pointer) return false;
        PlayerInfo = ReadMemory<uintptr_t>(Pointer + Offsets.PlayerInfo);
        // Require a valid PlayerInfo (proves this is a real networked player, not garbage)
        if (!PlayerInfo || PlayerInfo < 0x100000ULL) return false;
        Position   = ReadMemory<Vector3>(Pointer + 0x90);
        BoneMatrix = ReadMemory<Matrix>(Pointer + 0x60);
        UpdateBones();
        RefreshName();
        return true;
    }

    // Oyuncu adını cache'den çeker.
    // "P#" gösteriliyorsa → g_unresolvedIds'e eklenir → NameCacheThread daha sık HTTP çeker.
    void RefreshName() {
        std::string found;
        int foundNetId = -1;

        // ── 1. Birincil offset listesi (hızlı yol) ──────────────────────────────
        //    Offsets.Id yapılandırılmışsa önce onu dene, sonra en yaygın FiveM offsetleri.
        //    FiveM netId: 1-2047 arası (16-bit). Limit >= 2048 kullan.
        {
            const uintptr_t kPrimary[] = {
                (uintptr_t)Offsets.Id,
                0xC8, 0xD0, 0xE8, 0x88
            };
            for (uintptr_t off : kPrimary) {
                if (!off) continue;
                uint16_t netId = ReadMemory<uint16_t>(PlayerInfo + off);
                if (netId == 0 || netId >= 2048) continue;
                if (foundNetId < 0) foundNetId = (int)netId;
                std::string n = GetPedName((int)netId);
                if (!n.empty()) { found = n; foundNetId = (int)netId; break; }
            }
        }

        // ── 2. Bulunamadıysa genişletilmiş offset taraması ──────────────────────
        if (found.empty()) {
            const uintptr_t kExtended[] = {
                0xD8, 0xF0, 0xA8, 0xB8, 0x78, 0x100, 0x108, 0x110, 0x118, 0x120
            };
            for (uintptr_t off : kExtended) {
                uint16_t netId = ReadMemory<uint16_t>(PlayerInfo + off);
                if (netId == 0 || netId >= 2048) continue;
                if (foundNetId < 0) foundNetId = (int)netId;
                std::string n = GetPedName((int)netId);
                if (!n.empty()) { found = n; foundNetId = (int)netId; break; }
            }
        }

        // ── 2.5. Geniş tarama: PlayerInfo içinde 0x60–0x1C0 arasını 4'er bayt adımla tara
        if (found.empty()) {
            for (uintptr_t off = 0x60; off <= 0x1C0; off += 4) {
                uint16_t netId = ReadMemory<uint16_t>(PlayerInfo + off);
                if (netId == 0 || netId >= 2048) continue;
                std::string n = GetPedName((int)netId);
                if (!n.empty()) {
                    found      = n;
                    foundNetId = (int)netId;
                    break;
                }
                if (foundNetId < 0) foundNetId = (int)netId;
            }
        }

        // ── 3. Persist cache: bul → kaydet + unresolved'dan çıkar
        //                      bulamadı → önceki tick'ten geri yükle
        if (foundNetId > 0) {
            std::lock_guard<std::mutex> lk(g_nameMutex);
            if (!found.empty()) {
                g_pedNamePersist[foundNetId] = found;
                g_unresolvedIds.erase(foundNetId);        // artık çözüldü
            } else {
                auto it = g_pedNamePersist.find(foundNetId);
                if (it != g_pedNamePersist.end()) {
                    found = it->second;                   // önceden bilinen isim
                } else {
                    bool isNew = (g_unresolvedIds.find(foundNetId) == g_unresolvedIds.end());
                    g_unresolvedIds.insert(foundNetId);   // "P#" gösterilecek → yenile
                    if (isNew) g_needsImmediateRefresh = true; // YENİ P# → anında HTTP
                }
            }
        }

        // ── 4. Son çare: PlayerInfo üzerinden doğrudan okuma girişimi ───────────
        if (found.empty())
            found = TryReadNameFromPlayerInfo(Pointer);

        Name = std::move(found);
    }

    void UpdateBones() {
        Bones b = ReadMemory<Bones>(Pointer + Offsets.BoneList);
        BoneList[Head]       = Vec3Transform(&b.Head,       &BoneMatrix);
        BoneList[LeftFoot]   = Vec3Transform(&b.LeftFoot,   &BoneMatrix);
        BoneList[RightFoot]  = Vec3Transform(&b.RightFoot,  &BoneMatrix);
        BoneList[LeftAnkle]  = Vec3Transform(&b.LeftAnkle,  &BoneMatrix);
        BoneList[RightAnkle] = Vec3Transform(&b.RightAnkle, &BoneMatrix);
        BoneList[LeftHand]   = Vec3Transform(&b.LeftHand,   &BoneMatrix);
        BoneList[RightHand]  = Vec3Transform(&b.RightHand,  &BoneMatrix);
        BoneList[Neck]       = Vec3Transform(&b.Neck,       &BoneMatrix);
        BoneList[Hip]        = Vec3Transform(&b.Hip,        &BoneMatrix);

        // Eğer kemik pozisyonu (0,0,0) geliyorsa (offset hatalı veya animasyon verisi
        // henüz hazır değil), kafa pozisyonundan tahmin yoluyla doldur.
        // GTA V Z-yukarı koordinat sistemi; kafa en yüksek Z değerinde.
        const Vector3& h = BoneList[Head];
        if (!Vec3Empty(h)) {
            if (Vec3Empty(BoneList[Neck]))
                BoneList[Neck]      = { h.x,        h.y, h.z - 0.12f };
            if (Vec3Empty(BoneList[Hip]))        // "Chest" seçeneği bu kemiği kullanır
                BoneList[Hip]       = { h.x,        h.y, h.z - 0.42f };
            if (Vec3Empty(BoneList[LeftHand]))
                BoneList[LeftHand]  = { h.x - 0.38f, h.y, h.z - 0.52f };
            if (Vec3Empty(BoneList[RightHand]))
                BoneList[RightHand] = { h.x + 0.38f, h.y, h.z - 0.52f };
            if (Vec3Empty(BoneList[LeftFoot]))
                BoneList[LeftFoot]  = { h.x - 0.13f, h.y, h.z - 1.58f };
            if (Vec3Empty(BoneList[RightFoot]))
                BoneList[RightFoot] = { h.x + 0.13f, h.y, h.z - 1.58f };
            if (Vec3Empty(BoneList[LeftAnkle]))
                BoneList[LeftAnkle]  = { h.x - 0.11f, h.y, h.z - 1.48f };
            if (Vec3Empty(BoneList[RightAnkle]))
                BoneList[RightAnkle] = { h.x + 0.11f, h.y, h.z - 1.48f };
        }
    }

    int GetId() {
        uintptr_t pi = ReadMemory<uintptr_t>(Pointer + Offsets.PlayerInfo);
        if (!pi) return -1;
        return ReadMemory<int>(pi + Offsets.Id);
    }

    std::string GetName() {
        if (!Name.empty()) return Name;

        // Alias kontrolü (kullanıcı elle isim verdiyse)
        int id = GetId();
        if (id > 0) {
            {
                std::lock_guard<std::mutex> lk(g_nameMutex);
                auto ait = g_playerAliases.find(id);
                if (ait != g_playerAliases.end() && !ait->second.empty())
                    return ait->second;
            }
            // "P#" döndürülecek → bu ID'yi unresolved'a ekle,
            // NameCacheThread agresif HTTP moduna geçer.
            {
                std::lock_guard<std::mutex> lk(g_nameMutex);
                bool isNew = (g_unresolvedIds.find(id) == g_unresolvedIds.end());
                g_unresolvedIds.insert(id);
                if (isNew) g_needsImmediateRefresh = true; // anında yenile
            }
            return "P#" + std::to_string(id);
        }
        return "";
    }

    std::string GetWeaponName() {
        uintptr_t wm   = ReadMemory<uintptr_t>(Pointer + Offsets.WeaponManager);
        uintptr_t info = ReadMemory<uintptr_t>(wm + 0x20);
        return ReadString(ReadMemory<uintptr_t>(info + 0x5F0));
    }

    float GetHealth() { return ReadMemory<float>(Pointer + Offsets.Health); }
    float GetArmor()  { return ReadMemory<float>(Pointer + Offsets.Armor);  }

    bool IsPlayer() {
        if (GetId() <= 0) return false;
        return PlayerInfo != 0;
    }

    bool IsDead()    { return Vec3Empty(Position); }

    bool IsVisible() {
        BYTE f = ReadMemory<BYTE>(Pointer + Offsets.VisibleFlag);
        return !(f == 36 || f == 0 || f == 4 || f == 2 ||
                 (f & 256) || (f & 512) || (f & 1024) || (f & 2048));
    }
};

// ============================================================
//  DemoUser  —  local player info shown in the menu
// ============================================================
struct DemoUserStruct {
    int         Id         = 0;
    std::string Name       = "Player";
    std::string WeaponName = "";
    float       Health     = 0.f;
    float       Armor      = 0.f;
} DemoUser;

// ============================================================
//  Attach — External: FiveM penceresini bul, handle aç.
// ============================================================
bool AttachToGameProcess() {
    Game.hWnd = FindWindowA(BuildClassName(), nullptr);
    if (!Game.hWnd) return false;
    GetWindowThreadProcessId(Game.hWnd, &Game.pID);
    if (!Game.pID) return false;
    if (Game.hProcess) { CloseHandle(Game.hProcess); Game.hProcess = nullptr; }
    Game.hProcess = OpenProcess(
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
        FALSE, Game.pID);
    return Game.hProcess != nullptr;
}

// ============================================================
//  Fetch game path & build version from CitizenFX.ini
// ============================================================
bool FetchGamePathAndVersion() {
    if (Game.Path.empty() && Game.hProcess) {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameExA(Game.hProcess, NULL, path, MAX_PATH) > 0)
            Game.Path = path;
    }
    if (Game.Version.empty() && !Game.Path.empty()) {
        size_t pos = Game.Path.find(XorString("data\\cache\\subprocess"));
        if (pos != std::string::npos) {
            std::string ini = Game.Path.substr(0, pos) + XorString("CitizenFX.ini");
            char ver[32] = {};
            if (GetPrivateProfileStringA(XorString("Game"), XorString("SavedBuildNumber"),
                                         "", ver, sizeof(ver), ini.c_str()) > 0)
                Game.Version = ver;
        }
    }
    return !Game.Path.empty();
}

// ============================================================
//  Detect version & fill offsets
// ============================================================
bool FetchOffsetsAndVersion() {
    // External: FiveM base adresi için TlHelp32 taraması (30sn timeout)
    for (int i = 0; i < 300 && !Offsets.GameBase; ++i) {
        Offsets.GameBase = GetBaseAddress();
        if (!Offsets.GameBase) Sleep(100);
    }
    if (!Offsets.GameBase) return false;

    // Bu DLL'ler opsiyonel — bulunamazsa isim sistemi çalışmaz ama hile çalışır
    uintptr_t prevCitizenBase = Offsets.CitizenPlayernamesBase;
    for (int i = 0; i < 50 && !Offsets.CitizenPlayernamesBase; i++) {
        Offsets.CitizenPlayernamesBase = GetBaseAddress(XorString("citizen-playernames-five.dll"));
        Sleep(100);
    }
    // DLL adresi değiştiyse (yeniden yüklendi) → bulunan offset geçersizdir, sıfırla
    if (Offsets.CitizenPlayernamesBase != prevCitizenBase)
        s_namesListOff = 0;

    for (int i = 0; i < 50 && !Offsets.NetBase; i++) {
        Offsets.NetBase = GetBaseAddress(XorString("net.dll"));
        Sleep(100);
    }

    FetchGamePathAndVersion();

    // ----- version blocks -----
    struct VersionEntry {
        const char* ver;
        uintptr_t GW,RI,VP,CAM,BL,WM,HB,VF,BL2,VEH,WP,LP,PI,ID,HP,MHP,ARM;
    };
    // ver  |  GW        RI        VP        CAM       BL    WM     HB        VF     BL2       VEH   WP        LP PI     ID   HP    MHP   ARM
    static const VersionEntry versions[] = {
        // b3570 — en guncel (World 0x25EC580)
        {"3570",0x25EC580,0x1FB0418,0x2058BA0,0x2059778,0x410,0x10B8,0x102D550,0x145C,0x2061870,0x0D10,0x2047D50,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b3407
        {"3407",0x25D7108,0x1F9A9D8,0x20431C0,0x20440C8,0x410,0x10B8,0x102FF8C,0x145C,0x20440C8,0x0D10,0x2047D50,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b3323
        {"3323",0x25C15B0,0x1F85458,0x202DC50,0x202E878,0x410,0x10B8,0x1026CB0,0x145C,0x2002888,0x0D10,0x2022DE0,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b3258
        {"3258",0x25B14B0,0x1FBD4F0,0x201DBA0,0x201E7D0,0x410,0x10B8,0x101A660,0x145C,0x2002FA0,0x0D10,0x2023400,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b3095
        {"3095",0x2593320,0x1F58B58,0x20019E0,0x20025B8,0x410,0x10B8,0x100F5A4,0x145C,0x2002888,0x0D10,0x2002FA0,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b2944
        {"2944",0x257BEA0,0x1F42068,0x1FEAAC0,0x1FEB968,0x410,0x10B8,0x1003F80,0x145C,0x1FEB968,0x0D10,0x1FF3130,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b2802
        {"2802",0x254D448,0x1F5B820,0x1FBC100,0x1FBCCD8,0x410,0x10B8,0xFF716C, 0x145C,0x1FBCFA8,0x0D10,0x1FBD6E0,0x8,0x10A8,0xE8,0x280,0x284,0x150C},
        // b2699
        {"2699",0x26684D8,0x20304C8,0x20D8C90,0x20D9868,0x430,0x10D8,0xFF9D90, 0x147C,0x20D9B38,0xD30, 0x20E1420,0x8,0x10C8,0x88,0x280,0x2A0, 0x1530},
        // b2612
        {"2612",0x2567DB0,0x1F77EF0,0x1FD8570,0x1FD9418,0x430,0x10D8,0xFF340C, 0x147C,0x1FD9418,0xD30, 0x1FDDD20,0x8,0x10C8,0x88,0x280,0x2A0, 0x1530},
        // b2545
        {"2545",0x25667E8,0x1F2E7A8,0x1FD6F70,0x1FD7E18,0x430,0x10D8,0xFF1B40, 0x147C,0x1FD7E18,0xD30, 0x1F4F940,0x8,0x10A8,0x88,0x280,0x2A0, 0x1530},
        // b2372
        {"2372",0x252DCD8,0x1F05208,0x1F9E9F0,0x1F9F898,0x430,0x10D8,0xFE7EF8, 0x142C,0x1F9F898,0xD30, 0x1F9FFA0,0x8,0x10C8,0x88,0x280,0x2A0, 0x14E0},
        // b2189
        {"2189",0x24E6D90,0x1EE18A8,0x1F888C0,0x1F89768,0x430,0x10D8,0xFE2154, 0x142C,0x1F89768,0xD30, 0x1F6EF80,0x8,0x10C8,0x78,0x280,0x2A0, 0x14E0},
        // b2060
        {"2060",0x24C8858,0x1EC3828,0x1F6A7E0,0x1F6B940,0x430,0x10D8,0xFCE6EC, 0x142C,0x1F6B940,0xD28, 0x1F4F940,0x8,0x10A8,0x78,0x280,0x2A0, 0x14E0},
    };

    auto applyVersion = [&](const VersionEntry& v) {
        Offsets.GameWorld=v.GW; Offsets.ReplayInterface=v.RI; Offsets.ViewPort=v.VP;
        Offsets.Camera=v.CAM; Offsets.BoneList=v.BL; Offsets.WeaponManager=v.WM;
        Offsets.HandleBullet=v.HB; Offsets.VisibleFlag=v.VF; Offsets.BlipList=v.BL2;
        Offsets.Vehicle=v.VEH; Offsets.Waypoint=v.WP; Offsets.LocalPlayer=v.LP;
        Offsets.PlayerInfo=v.PI; Offsets.Id=v.ID; Offsets.Health=v.HP;
        Offsets.MaxHealth=v.MHP; Offsets.Armor=v.ARM;
        // Ped-level sabit offsetler (build'den bağımsız — b3570 ile doğrulandı)
        Offsets.WeaponInfo  = 0x10C8;   // PlayerInfo + 0x10C8 → m_pInfo
        Offsets.DoorLock    = 0x13C0;   // Vehicle + 0x13C0
        Offsets.FragInst    = 0x1430;   // Ped + 0x1430
        // GameBase offsetleri — yalnızca bilinen build'ler için (diğerlerinde 0)
        if (strcmp(v.ver, "3570") == 0 || strcmp(v.ver, "3407") == 0 ||
            strcmp(v.ver, "3323") == 0 || strcmp(v.ver, "3258") == 0) {
            Offsets.Invisible   = 0x6AE372;    // GameBase + 0x6AE372
            Offsets.WeatherBase = 0x25D503C;   // GameBase + 0x25D503C
        }
        Game.Version = v.ver;
        char buf[64];
        sprintf(buf, XorString("Version detected: %s"), v.ver);
        Debug(buf, LOG_SUCCESSFUL);
    };

    while (true) {
        // 1) Try exact build number match from CitizenFX.ini first
        if (!Game.Version.empty()) {
            for (auto& v : versions) {
                if (strncmp(Game.Version.c_str(), v.ver, strlen(v.ver)) == 0) {
                    applyVersion(v);
                    return true;
                }
            }
        }

        // 2) Fall back: pick first version whose game pointers are non-null
        for (auto& v : versions) {
            auto gw = ReadMemory<uintptr_t>(Offsets.GameBase + v.GW);
            auto vp = ReadMemory<uintptr_t>(Offsets.GameBase + v.VP);
            auto ri = ReadMemory<uintptr_t>(Offsets.GameBase + v.RI);
            if (gw && vp && ri) {
                applyVersion(v);
                return true;
            }
        }
        Sleep(500);
    }
}
