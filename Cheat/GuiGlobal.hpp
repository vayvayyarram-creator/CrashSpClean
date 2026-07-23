// ============================================================
//  Scrambled<T> — XOR-encrypted value wrapper
//  Protects cheat flags from AC memory pattern scanning.
//  Usage: Scrambled<bool> Enabled;  — transparently replaces bool.
// ============================================================
#ifndef SCRAMBLED_DEFINED
#define SCRAMBLED_DEFINED
#include <Windows.h>
template<typename T>
struct Scrambled {
    static_assert(sizeof(T) <= 8, "Scrambled<T>: max 8 bytes");
    volatile UINT64 _enc = 0;
    volatile UINT64 _key = 0;
    Scrambled() = default;
    explicit Scrambled(T v) { write(v); }
    void write(T v) {
        UINT64 k = (UINT64)(uintptr_t)this ^ (UINT64)GetTickCount64()
                 ^ ((UINT64)GetCurrentThreadId() << 32);
        UINT64 raw = 0; memcpy(&raw, &v, sizeof(T));
        _key = k; _enc = raw ^ k;
    }
    T read() const { UINT64 raw = _enc ^ _key; T r{}; memcpy(&r, &raw, sizeof(T)); return r; }
    operator T()        const { return read(); }
    Scrambled& operator=(T v)      { write(v); return *this; }
    bool operator==(T v)     const { return read() == v; }
    bool operator!=(T v)     const { return read() != v; }
};
#endif

namespace GuiSetting {
    inline ImVec2 Size                  = { 960, 600 };
    inline ImVec4 Color1;
    inline ImVec4 Color2;
    inline ImVec4 Color3;
    inline ImVec4 Color4;
    inline ImVec4 Background            = ImColor(22, 22, 28, 255);
    inline ImVec4 InputBg               = ImColor(30, 30, 40, 255);
    inline ImVec4 TextActive            = ImColor(255, 255, 255, 255);
    inline ImVec4 Shadow                = ImColor(40, 42, 56, 80);
    inline ImVec4 DecorativeRectBackground;
    inline ImVec4 ChildRect;
    inline ImVec4 Black;
    inline ImVec4 Black1;
    inline ImVec4 BlackIn;
    inline ImVec4 BorderChild;
    inline ImVec4 LineChild;
    inline ImVec4 ShadowTab;
    inline ImVec4 CheckboxBackground;
    inline ImVec4 CheckboxInBackground;
    inline ImVec4 CircleCheckbox;
    inline ImVec4 CircleCheckboxIn;
    inline ImVec4 Separator;
    inline ImVec4 SwitchInActive;
    inline ImVec4 SwitchText;
    inline ImVec4 SwitchTextIn;
    // Ek renkler (GuiItems / UpdateTheme tarafindan kullanilir)
    inline ImVec4 Accent;
    inline ImVec4 AccentDim;
    inline ImVec4 WindowBg;
    inline ImVec4 PanelBg;
    inline ImVec4 ChildBg;
    inline ImVec4 Border;
    inline ImVec4 CheckboxBg;
    inline ImVec4 CheckboxFill;
    inline ImVec4 SwitchOff;
    inline ImVec4 SwitchOn;
    inline ImVec4 TextMuted             = ImColor(160, 160, 170, 255);
}

// ============================================================
//  Cheats:: — tum hile ayarlari
//  NOT: 'inline' ekli — GuiGlobal.hpp birden fazla TU'ya dahil
//  edildiginden ODR ihlalini onlemek icin gerekli (C++17)
// ============================================================
namespace Cheats {
    namespace AimAssist {
        namespace Settings {
            inline bool Crosshair              = false;
            inline int  CrosshairSelectedType  = 0;
            inline const char* CrosshairType[10]{ "Type 1","Type 2","Type 3","Type 4","Type 5",
                                                   "Type 6","Type 7","Type 8","Type 9","Type 10" };
            inline ImColor CrosshairColor      = ImColor(255,255,255);
            inline int  CrosshairSize          = 15;
            inline bool DynamicCrosshairColor  = false;
            inline bool IgnorePed              = true;
            inline bool IgnoreDeath            = true;
            inline bool IgnoreInvisible        = false;
            inline bool InfiniteAmmo           = false;
            inline bool NoRecoil               = false;
            inline bool NoSpread               = false;
            inline bool NoReload               = false;
            inline bool NoRange                = false;
        }

        namespace Silent {
            inline Scrambled<bool> Enabled;
            inline int  HotKey      = 0;
            inline bool ToggleMode  = false;  // false=Hold, true=Toggle
            inline int  Fov         = 150;
            inline int  BoneMode    = 0;
            inline int  MaxDistance = 300;
            inline bool DrawFov     = false;
            inline ImColor FovColor = ImColor(100,220,180);
            inline int  FovWeight   = 1;
            inline bool VisCheck    = false;
            inline int  MissChance    = 0;
            inline bool SkipFriends = true;
            inline bool DrawTarget  = false;
            inline int  SelectedDrawTargetType = 0;
            inline ImColor DrawTargetColor = ImColor(255,80,80,200);
        }

        namespace MagicBullet {
            inline Scrambled<bool> Enabled;
            inline int  BoneMode    = 0;   // 0=Kafa, 1=Gogus, 2=Bacak, 3=Rastgele
            inline int  MaxDistance = 600;
            inline bool SkipFriends = true;
            inline bool SkipNPC     = true;
        }

        namespace Aimbot {
            inline Scrambled<bool> Enabled;
            inline int  HotKey      = 0;
            inline bool ToggleMode  = false;  // false=Hold, true=Toggle
            inline int  Fov         = 150;
            inline int  Smooth      = 5;
            inline int  BoneMode    = 0;
            inline int  MaxDistance = 300;
            inline bool StickyAim   = true;
            inline int  AimMode     = 0;
            inline int  Priority    = 0;
            inline bool DrawFov     = false;
            inline ImColor FovColor = ImColor(100,200,255);
            inline int  FovWeight   = 1;
            inline bool VisCheck    = false;
            inline bool SkipFriends = true;
            inline bool DrawTarget  = false;
            inline int  SelectedDrawTargetType = 0;
            inline ImColor DrawTargetColor = ImColor(80,200,255,200);
        }

        namespace Triggerbot {
            inline bool Enabled      = false;
            inline int  HotKey       = 0;
            inline int  Delay        = 5;
            inline int  MaxDistance  = 300;
            inline bool SkipFriends  = true;
        }
    }

    namespace Players {
        namespace Settings {
            inline bool IgnorePed      = true;
            inline bool IgnoreDeath    = true;
            inline bool IgnoreInvisible= false;
            inline int  MaxDistance    = 300;
        }

        namespace VisCheck {
            inline bool Enabled          = false;
            inline ImColor VisibleColor  = ImColor( 80,220,120,255);  // yesil = gorulur
            inline ImColor HiddenColor   = ImColor(220, 70, 70,255);  // kirmizi = duvar arkasi
        }

        namespace FriendESP {
            // Arkadaşları farklı renkte göster (skip etmek yerine)
            inline bool    Enabled = true;
            inline ImColor Color   = ImColor(0, 220, 255, 255);  // cyan — arkadaş rengi
        }

        namespace TagCount {
            // Aynı clan tag'ini paylaşan oyuncu sayısını üstlerinde göster
            // Örn: "[WINS] PlayerX" → "[WINS]" tag'ini taşıyan kaç kişi varsa o sayıyı gösterir
            inline bool    Enabled = false;
            inline ImColor Color   = ImColor(255, 220, 80, 255);  // altın sarısı
        }

        namespace OffscreenESP {
            // Aktifken oyun üstündeki overlay ESP durur;
            // localhost:7531/esp'de tarayıcı tabanlı canlı radar çalışır.
            inline bool Enabled = false;
        }

        namespace RgbESP {
            // Her oyuncu ESP elemanı rengarenk döner (HSV hue cycling)
            inline bool  Enabled = false;
            inline float Speed   = 1.0f;  // döngü hızı (hue/sn)
            inline float Saturation = 1.0f;
            inline float Value      = 1.0f;
        }

        namespace VisualMarkers {
            namespace GlobalSettings {
                inline int  SelectedLineType   = 1;
                inline const char* LineTypes[2]{ "Basic","Outlined" };
                inline int  LineWeight         = 2;
            }
            namespace DrawSkeleton {
                inline bool Enabled    = false;
                inline ImColor Color   = ImColor(255,255,255);
            }
            namespace DrawBox {
                inline bool Enabled    = false;
                inline ImColor Color   = ImColor(255,155,0);
                inline int  SelectedType = 0;
                inline const char* Types[3]{ "Corner","2D","3D" };
            }
            namespace DrawLine {
                inline bool Enabled            = false;
                inline ImColor Color           = ImColor(69,158,255);
                inline int  SelectedLocation   = 2;
                inline const char* Locations[3]{ "Top","Center","Bottom" };
            }
            namespace DrawBonePoints {
                inline bool    Enabled = false;
                inline ImColor Color   = ImColor(255,200,60,220);
                inline int     Radius  = 3;
            }
        }

        namespace PlayerInfo {
            namespace GlobalSettings {
                inline int  SelectedFont       = 0;
                inline const char* Fonts[2]    = { "Roboto","Roboto Bold" };
                inline int  SelectedFontType   = 0;
                inline const char* FontTypes[2]{ "Basic","Outlined" };
                inline int  MaxDistance        = 70;
            }
            namespace DrawId {
                inline bool Enabled            = false;
                inline ImColor Color           = ImColor(255,255,255);
                inline int  SelectedLocation   = 0;
                inline const char* Locations[2]{ "Top","Bottom" };
            }
            namespace DrawName {
                inline bool Enabled            = false;
                inline ImColor Color           = ImColor(5,255,132);
                inline int  SelectedLocation   = 0;
                inline const char* Locations[2]{ "Top","Bottom" };
            }
            namespace DrawWeaponName {
                inline bool Enabled            = false;
                inline ImColor Color           = ImColor(255,255,255);
                inline int  SelectedLocation   = 1;
                inline const char* Locations[2]{ "Top","Bottom" };
            }
            namespace DrawDistance {
                inline bool Enabled            = false;
                inline ImColor Color           = ImColor(255,97,220);
                inline int  SelectedLocation   = 1;
                inline const char* Locations[2]{ "Top","Bottom" };
            }
        }

        namespace StatusBars {
            namespace GlobalSettings {
                inline int MaxHealth = 200;
                inline int MaxArmor  = 100;
            }
            namespace DrawHealthBar {
                inline bool Enabled            = false;
                inline int  SelectedLocation   = 0;
                inline const char* Locations[4]{ "Left","Right","Up","Down" };
            }
            namespace HealthBoost {
                inline bool Enabled = false;
                inline int  Value   = 50;
                inline int  HotKey  = 0;
            }
            namespace DrawArmorBar {
                inline bool Enabled            = false;
                inline int  SelectedLocation   = 1;
                inline const char* Locations[4]{ "Left","Right","Up","Down" };
            }
            namespace ArmorBoost {
                inline bool Enabled = false;
                inline int  Value   = 50;
                inline int  HotKey  = 0;
            }
        }
    }

    namespace Vehicles {
        namespace Settings {
            inline bool IgnoreLocalVehicle = true;
            inline int  MaxVehicleCount    = 500;
            inline int  MaxDistance        = 300;
        }
        namespace DrawPoint {
            inline bool Enabled            = false;
            inline ImColor Color           = ImColor(239,255,21);
            inline int  SelectedLineType   = 1;
            inline const char* LineTypes[2]{ "Basic","Outlined" };
            inline int  Size               = 7;
        }
        namespace DrawLine {
            inline bool Enabled            = false;
            inline ImColor Color           = ImColor(123,166,7);
            inline int  SelectedLocation   = 2;
            inline const char* Locations[3]{ "Top","Center","Bottom" };
            inline int  SelectedLineType   = 1;
            inline const char* LineTypes[2]{ "Basic","Outlined" };
        }
        namespace DrawDistance {
            inline bool Enabled            = false;
            inline ImColor Color           = ImColor(255,255,255);
            inline int  SelectedFont       = 0;
            inline const char* Fonts[2]    = { "Roboto","Roboto Bold" };
            inline int  SelectedFontType   = 1;  // default: outlined
            inline const char* FontTypes[2]{ "Basic","Outlined" };
            inline int  MaxDistance        = 70;
        }
        namespace DrawHealthBar { inline bool Enabled = false; }
        namespace VehicleFix {
            inline bool Enabled = false;
            inline int  HotKey  = 0;
        }
        namespace VehicleGodMode {
            // Binilen aracın HP'sini her tick 1000'de tutar
            inline bool Enabled = false;
        }
        namespace SpeedBoost {
            // Araç hareket ederken hızı bu değere (km/h) yükseltir
            inline bool Enabled = false;
            inline int  KmH     = 150;  // hedef hız km/h (m/s = KmH / 3.6)
        }
        namespace VehicleFlip {
            // Hotkey'e basınca devrilen aracı düzeltir
            inline bool Enabled = false;
            inline int  HotKey  = 0;
        }
    }

    namespace World {
        namespace NoClip {
            inline bool Enabled        = false;
            inline int  MovementSpeed  = 5;
            inline int  ForwardKey     = 0x57; // W
            inline int  BackwardKey    = 0x53; // S
            inline int  LeftKey        = 0x41; // A
            inline int  RightKey       = 0x44; // D
            inline int  UpKey          = 0x20; // Space
            inline int  DownKey        = 0xA2; // Ctrl
        }
        namespace SemiGodMode    { inline bool Enabled = false; }
        namespace SuperSprint    { inline bool Enabled = false; inline int  Speed = 5; }
        namespace InfiniteStamina{ inline bool Enabled = false; }
        namespace ExplosiveBullets{ inline bool Enabled = false; }
        namespace FireBullets    { inline bool Enabled = false; }
        namespace RapidFire      { inline bool Enabled = false; }

        // ── Heal toggle (HP / Armor dönüşümlü) ──────────────────
        namespace HealToggle {
            inline bool Enabled   = false;
            inline int  HotKey    = 0;
            inline bool NextIsHP  = true;  // true=HP bas, false=Armor bas
        }

        // ── Armor Fill — tuşa basınca zırhı 100'e doldur ─────────
        namespace ArmorFill {
            inline bool Enabled = false;
            inline int  HotKey  = 0;
        }

        // ── Yeni özellikler ──────────────────────────────────────
        namespace DamageBoost {
            inline bool  Enabled    = false;
            inline float Multiplier = 2.0f;   // x2 hasar çarpanı
        }
        namespace LowDamage {
            inline bool  Enabled    = false;
            inline float Multiplier = 2.0f;  // gelen hasarı bu değere böl
        }
        namespace Respawn {
            // Ölümde anında canlanır (HP sıfırsa 200'e yazar)
            inline bool Enabled = false;
            inline int  HotKey  = 0;
        }
        namespace UnlockCar {
            // ModKey basılı tutarken F'ye basıldığında en yakın aracın kilidini açar
            inline bool Enabled = false;
            inline int  ModKey  = 0;   // basılı tutulacak tuş (0 = yok, sadece F yeter)
        }

        namespace Invisible {
            // Local player'ı diğer oyunculardan gizler
            // GameBase + 0x6AE372 → görünürlük baytı (0 = görünmez, orijinal = görünür)
            inline bool Enabled = false;
            inline int  HotKey  = 0;   // 0 = tuş yok, sadece toggle ile aç/kapat
        }

        namespace Teleport {
            struct TeleportLocation { const char* Name; float x, y, z; };
            inline const TeleportLocation Locations[] = {
                { "Legion Square",              190.52f,   -873.23f,  31.5f  },
                { "Paleto Bay",                -138.52f,   6356.99f,  31.49f },
                { "Main LS Customs",           -365.43f,   -131.81f,  37.87f },
                { "IAA Roof",                   134.09f,   -637.86f, 262.85f },
                { "FIB Roof",                  -150.13f,   -754.59f, 262.87f },
                { "Maze Bank",                  -75.02f,   -818.22f, 326.18f },
                { "Mount Chiliad",              495.0f,    5589.0f,  795.0f  },
                { "Casino",                     911.96f,     38.34f,  80.72f },
                { "Prison",                    1702.08f,   2650.51f,  45.56f },
                { "Military Base",            -2751.12f,   3316.4f,   32.81f },
                { "Void",                     15000.0f,  15000.0f,    0.0f   },
                { "Hospital Central LS",        339.85f,  -1394.56f,  32.51f },
                { "Pillbox Medical",            307.87f,   -595.55f,  43.28f },
                { "Sandy Shores Medical",      1839.6f,   3672.93f,   34.28f },
                { "Lester's House",            1273.9f,  -1719.3f,   54.77f  },
                { "Pacific Standard Vault",     255.85f,    217.03f, 101.68f },
                { "Police Station",             436.49f,   -982.17f,  30.70f },
                { "Humane Labs",               3619.75f,   2742.74f,  28.69f },
                { "Floyd's Apartment",        -1150.7f,  -1520.7f,   10.63f  },
                { "Trevor's Meth Lab",         1391.77f,   3608.72f,  38.94f },
            };
            inline int SelectedIndex = 0;
        }
    }

    namespace SpectatorDetect {
        // Seni spectate eden oyuncuları tespit et (aynı konum tespiti)
        inline bool Enabled = false;
        // Kaç ardışık tick aynı konumda olursa "spectating" sayılsın
        inline int  Threshold = 8;  // ~80ms @ 10ms tick
    }

    namespace LowGravity {
        // Düşüş hızını azalt — sanki yerçekimi düşük gibi
        inline bool  Enabled    = false;
        inline float Strength   = 0.65f; // 0–1: ne kadar düşüş iptal edilsin (1=tam uçuş)
    }

    namespace MoonJump {
        // Space basılıyken sürekli yukarı git
        inline bool Enabled  = false;
        inline float Force   = 0.4f;  // her frame eklenecek Z boost (metre)
    }

    namespace Settings {
        inline bool StreamProof    = true;
        inline int  MenuKey        = 45;  // INSERT
        inline int  PanicKey       = 123; // F12
        inline int  MaxPlayerCount = 500;
        inline int  SelectedTheme  = 1;   // 0=Emerald  1=Ocean  2=Rose
        inline float MaxESPDistance = 400.f; // metres — üzeri çizilmez (ESP lag fix)
        inline float DamageMultiplier = 1.0f; // Magic bullet/multi-damage fix
        inline bool  UnlockVehicle = false;   // Lock state bit reset
        inline bool  LowDamageOn    = false;   // ters damage mod (godmode entegre)
        inline bool  RespawnOnDeath = false;   // dead-state revive
        inline float LowDamageMultiplier = 0.25f; // 25% indirgenmiş hasar
    }
}

// ============================================================
//  Spectator list — Exploits thread yazar, DrawCheat okur
// ============================================================
#include <vector>
#include <mutex>
inline std::vector<int> g_spectatorIds;
inline std::mutex       g_spectatorMutex;

// ============================================================
//  Pending player action (web menu → C++ thread)
//  WebServer POST /api/action → bu struct'ı doldurur
//  Exploits thread → her tick kontrol eder
// ============================================================
enum PendingActionType { PA_NONE=0, PA_TELEPORT_TO=1, PA_COPY_OUTFIT=2 };
struct PendingAction {
    volatile PendingActionType type = PA_NONE;
    volatile int targetId = 0;
};
inline PendingAction g_pendingAction;

// ============================================================
//  Toast notification system  (top-center geçici bildirimler)
// ============================================================
#include <vector>
#include <string>
struct Toast { std::string msg; float timer; ImU32 color; };
inline std::vector<Toast> g_toasts;

// ── Discord Webhook log sistemi ──────────────────────────────────────────────
inline std::string g_discordWebhook;   // Gui.hpp Settings sayfasından set edilir

inline void DiscordLog(const char* fmt, ...) {
    if (g_discordWebhook.empty()) return;
    char buf[512]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    std::string url = g_discordWebhook;
    std::string txt = buf;
    std::thread([url, txt]() {
        std::wstring wUrl(url.begin(), url.end());
        URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{}, path[1024]{};
        uc.lpszHostName = host; uc.dwHostNameLength = 255;
        uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 1023;
        if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc)) return;

        HINTERNET hS = WinHttpOpen(L"Moon/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hS) return;
        DWORD t = 5000;
        WinHttpSetOption(hS, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof t);
        WinHttpSetOption(hS, WINHTTP_OPTION_SEND_TIMEOUT,    &t, sizeof t);
        WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof t);

        HINTERNET hC = WinHttpConnect(hS, host, uc.nPort, 0);
        if (!hC) { WinHttpCloseHandle(hS); return; }

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hR = WinHttpOpenRequest(hC, L"POST", path, nullptr,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return; }

        // JSON escape + body
        std::string body = "{\"content\":\"";
        for (unsigned char c : txt) {
            if (c == '"')  body += "\\\"";
            else if (c == '\\') body += "\\\\";
            else if (c == '\n') body += "\\n";
            else if (c >= 32)   body += (char)c;
        }
        body += "\",\"username\":\"Moon\"}";

        WinHttpSendRequest(hR, L"Content-Type: application/json\r\n", (DWORD)-1,
                           (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
        WinHttpReceiveResponse(hR, nullptr);
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    }).detach();
}

inline void PushToast(const char* msg,
                      ImU32 col = IM_COL32(100,220,140,255),
                      float dur  = 2.5f) {
    (void)col; (void)dur;
    DiscordLog("%s", msg);
}
