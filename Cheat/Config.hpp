#pragma once
#include <Windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#pragma comment(lib, "winhttp.lib")

// Overlay watermark toggle — SaveConfig/LoadConfig kullanıyor, en üstte olmalı
inline bool g_showWatermark = true;

// Per-kullanıcı config dizini: %APPDATA%\Microsoft\Windows\ThemeUtils\
// WinscreW gibi isimlerden kaçın — tanınabilir olmamalı.
static inline std::string CfgDir() {
    char appdata[MAX_PATH] = {};
    if (GetEnvironmentVariableA(XorString("APPDATA"), appdata, MAX_PATH) == 0) {
        GetTempPathA(MAX_PATH, appdata);
        std::string dir = std::string(appdata) + XorString("tu\\");
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    std::string dir = std::string(appdata) + XorString("\\Microsoft\\Windows\\ThemeUtils\\");
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

static inline std::string CfgExeDir() { return CfgDir(); }  // compat shim

// Named config path: CfgDir\{name}.dat
static inline std::string CfgNamedPath(const std::string& name) {
    return CfgDir() + name + ".dat";
}
// Default config path
static inline std::string CfgPath() { return CfgNamedPath("config"); }

// Enumerate all *.dat configs in config dir (return name without extension)
static inline std::vector<std::string> ListConfigs() {
    std::vector<std::string> out;
    std::string pattern = CfgDir() + "*.dat";
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string fn = fd.cFileName;
        if (fn.size() > 4 && fn.substr(fn.size()-4) == ".dat")
            out.push_back(fn.substr(0, fn.size()-4));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// Active config name (displayed in header)
static inline std::string& ActiveConfigName() {
    static std::string name = "settings";
    return name;
}


#define CFG_SAVE_B(f,k,v)   fprintf(f, k "=%d\n", (int)(v))
#define CFG_SAVE_I(f,k,v)   fprintf(f, k "=%d\n", (int)(v))
#define CFG_SAVE_F(f,k,v)   fprintf(f, k "=%.6f\n", (double)(v))
#define CFG_SAVE_C(f,k,c)   fprintf(f, k "=%.4f,%.4f,%.4f,%.4f\n", \
    (c).Value.x,(c).Value.y,(c).Value.z,(c).Value.w)
#define CFG_SAVE_S(f,k,v)   fprintf(f, k "=%s\n", (v).c_str())

static inline bool cfgMatch(const char* key, const char* line, char* val, int vlen) {
    size_t kl = strlen(key);
    if (strncmp(line, key, kl) != 0 || line[kl] != '=') return false;
    strncpy_s(val, vlen, line + kl + 1, vlen - 1);
  
    for (int i = (int)strlen(val)-1; i >= 0 && (val[i]=='\n'||val[i]=='\r'); --i) val[i]=0;
    return true;
}
#define CFG_LOAD_B(k,v)  if (cfgMatch(k, key, val, sizeof(val))) { v = (bool)atoi(val); continue; }
#define CFG_LOAD_I(k,v)  if (cfgMatch(k, key, val, sizeof(val))) { v = atoi(val); continue; }
#define CFG_LOAD_F(k,v)  if (cfgMatch(k, key, val, sizeof(val))) { v = (float)atof(val); continue; }
#define CFG_LOAD_C(k,c)  if (cfgMatch(k, key, val, sizeof(val))) { \
    float r,g,b,a; if(sscanf_s(val,"%f,%f,%f,%f",&r,&g,&b,&a)==4) c=ImColor(r,g,b,a); continue; }
#define CFG_LOAD_S(k,v)  if (cfgMatch(k, key, val, sizeof(val))) { v = val; continue; }


static void SaveConfig(const std::string& path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return;

    
    CFG_SAVE_B(f,"silent.enabled",     Cheats::AimAssist::Silent::Enabled);
    CFG_SAVE_I(f,"silent.hotkey",      Cheats::AimAssist::Silent::HotKey);
    CFG_SAVE_I(f,"silent.fov",         Cheats::AimAssist::Silent::Fov);
    CFG_SAVE_I(f,"silent.maxdist",     Cheats::AimAssist::Silent::MaxDistance);
    CFG_SAVE_I(f,"silent.bone",        Cheats::AimAssist::Silent::BoneMode);
    CFG_SAVE_I(f,"silent.misschance",  Cheats::AimAssist::Silent::MissChance);
    CFG_SAVE_B(f,"silent.vischeck",    Cheats::AimAssist::Silent::VisCheck);
    CFG_SAVE_B(f,"silent.skipfriends", Cheats::AimAssist::Silent::SkipFriends);
    CFG_SAVE_B(f,"silent.drawfov",     Cheats::AimAssist::Silent::DrawFov);
    CFG_SAVE_C(f,"silent.fovcolor",    Cheats::AimAssist::Silent::FovColor);

    CFG_SAVE_B(f,"mb.enabled",        Cheats::AimAssist::MagicBullet::Enabled);
    CFG_SAVE_I(f,"mb.bone",           Cheats::AimAssist::MagicBullet::BoneMode);
    CFG_SAVE_I(f,"mb.maxdist",        Cheats::AimAssist::MagicBullet::MaxDistance);
    CFG_SAVE_B(f,"mb.skipfriends",    Cheats::AimAssist::MagicBullet::SkipFriends);
    CFG_SAVE_B(f,"mb.skipnpc",        Cheats::AimAssist::MagicBullet::SkipNPC);

    CFG_SAVE_B(f,"aim.enabled",        Cheats::AimAssist::Aimbot::Enabled);
    CFG_SAVE_I(f,"aim.hotkey",         Cheats::AimAssist::Aimbot::HotKey);
    CFG_SAVE_I(f,"aim.fov",            Cheats::AimAssist::Aimbot::Fov);
    CFG_SAVE_I(f,"aim.smooth",         Cheats::AimAssist::Aimbot::Smooth);
    CFG_SAVE_I(f,"aim.bone",           Cheats::AimAssist::Aimbot::BoneMode);
    CFG_SAVE_I(f,"aim.maxdist",        Cheats::AimAssist::Aimbot::MaxDistance);
    CFG_SAVE_B(f,"aim.sticky",         Cheats::AimAssist::Aimbot::StickyAim);
    CFG_SAVE_I(f,"aim.mode",           Cheats::AimAssist::Aimbot::AimMode);
    CFG_SAVE_I(f,"aim.priority",       Cheats::AimAssist::Aimbot::Priority);
    CFG_SAVE_B(f,"aim.vischeck",       Cheats::AimAssist::Aimbot::VisCheck);
    CFG_SAVE_B(f,"aim.skipfriends",    Cheats::AimAssist::Aimbot::SkipFriends);
    CFG_SAVE_B(f,"aim.drawfov",        Cheats::AimAssist::Aimbot::DrawFov);
    CFG_SAVE_C(f,"aim.fovcolor",       Cheats::AimAssist::Aimbot::FovColor);

  
    CFG_SAVE_B(f,"trig.enabled",       Cheats::AimAssist::Triggerbot::Enabled);
    CFG_SAVE_I(f,"trig.hotkey",        Cheats::AimAssist::Triggerbot::HotKey);
    CFG_SAVE_I(f,"trig.delay",         Cheats::AimAssist::Triggerbot::Delay);
    CFG_SAVE_I(f,"trig.maxdist",       Cheats::AimAssist::Triggerbot::MaxDistance);
    CFG_SAVE_B(f,"trig.skipfriends",   Cheats::AimAssist::Triggerbot::SkipFriends);

   
    CFG_SAVE_B(f,"wpn.infiniteammo",   Cheats::AimAssist::Settings::InfiniteAmmo);
    CFG_SAVE_B(f,"wpn.norecoil",       Cheats::AimAssist::Settings::NoRecoil);
    CFG_SAVE_B(f,"wpn.nospread",       Cheats::AimAssist::Settings::NoSpread);
    CFG_SAVE_B(f,"wpn.noreload",       Cheats::AimAssist::Settings::NoReload);
    CFG_SAVE_B(f,"wpn.crosshair",      Cheats::AimAssist::Settings::Crosshair);
    CFG_SAVE_I(f,"wpn.crosshairtype",  Cheats::AimAssist::Settings::CrosshairSelectedType);
    CFG_SAVE_C(f,"wpn.crosshaircolor", Cheats::AimAssist::Settings::CrosshairColor);
    CFG_SAVE_I(f,"wpn.crosshairsize",  Cheats::AimAssist::Settings::CrosshairSize);
    CFG_SAVE_B(f,"wpn.dyncolor",       Cheats::AimAssist::Settings::DynamicCrosshairColor);

   
    CFG_SAVE_B(f,"esp.rgb",            Cheats::Players::RgbESP::Enabled);
    CFG_SAVE_F(f,"esp.rgb_speed",      Cheats::Players::RgbESP::Speed);
    CFG_SAVE_F(f,"esp.rgb_sat",        Cheats::Players::RgbESP::Saturation);
    CFG_SAVE_F(f,"esp.rgb_val",        Cheats::Players::RgbESP::Value);
    CFG_SAVE_B(f,"esp.wallcheck",      Cheats::Players::VisCheck::Enabled);
    CFG_SAVE_C(f,"esp.viscolor",       Cheats::Players::VisCheck::VisibleColor);
    CFG_SAVE_C(f,"esp.hidcolor",       Cheats::Players::VisCheck::HiddenColor);
    CFG_SAVE_B(f,"esp.skeleton",       Cheats::Players::VisualMarkers::DrawSkeleton::Enabled);
    CFG_SAVE_C(f,"esp.skelcol",        Cheats::Players::VisualMarkers::DrawSkeleton::Color);
    CFG_SAVE_B(f,"esp.box",            Cheats::Players::VisualMarkers::DrawBox::Enabled);
    CFG_SAVE_C(f,"esp.boxcol",         Cheats::Players::VisualMarkers::DrawBox::Color);
    CFG_SAVE_I(f,"esp.boxtype",        Cheats::Players::VisualMarkers::DrawBox::SelectedType);
    CFG_SAVE_B(f,"esp.line",           Cheats::Players::VisualMarkers::DrawLine::Enabled);
    CFG_SAVE_C(f,"esp.linecol",        Cheats::Players::VisualMarkers::DrawLine::Color);
    CFG_SAVE_I(f,"esp.lineloc",        Cheats::Players::VisualMarkers::DrawLine::SelectedLocation);
    CFG_SAVE_B(f,"esp.bonepoints",     Cheats::Players::VisualMarkers::DrawBonePoints::Enabled);
    CFG_SAVE_C(f,"esp.bonecol",        Cheats::Players::VisualMarkers::DrawBonePoints::Color);
    CFG_SAVE_I(f,"esp.ignoreped",      Cheats::Players::Settings::IgnorePed);
    CFG_SAVE_I(f,"esp.ignoredead",     Cheats::Players::Settings::IgnoreDeath);
    CFG_SAVE_I(f,"esp.maxdist",        Cheats::Players::Settings::MaxDistance);

    
    CFG_SAVE_B(f,"pi.name",            Cheats::Players::PlayerInfo::DrawName::Enabled);
    CFG_SAVE_C(f,"pi.namecol",         Cheats::Players::PlayerInfo::DrawName::Color);
    CFG_SAVE_B(f,"pi.id",              Cheats::Players::PlayerInfo::DrawId::Enabled);
    CFG_SAVE_B(f,"pi.dist",            Cheats::Players::PlayerInfo::DrawDistance::Enabled);
    CFG_SAVE_B(f,"pi.weapon",          Cheats::Players::PlayerInfo::DrawWeaponName::Enabled);

  
    CFG_SAVE_B(f,"world.semigod",      Cheats::World::SemiGodMode::Enabled);
    CFG_SAVE_B(f,"world.supersprint",  Cheats::World::SuperSprint::Enabled);
    CFG_SAVE_I(f,"world.sprintspd",    Cheats::World::SuperSprint::Speed);
    CFG_SAVE_B(f,"world.stamina",      Cheats::World::InfiniteStamina::Enabled);
    CFG_SAVE_B(f,"world.explosive",    Cheats::World::ExplosiveBullets::Enabled);
    CFG_SAVE_B(f,"world.fire",         Cheats::World::FireBullets::Enabled);
    CFG_SAVE_B(f,"world.rapidfire",    Cheats::World::RapidFire::Enabled);
    CFG_SAVE_B(f,"world.healtoggle",   Cheats::World::HealToggle::Enabled);
    CFG_SAVE_I(f,"world.healtogglekey",Cheats::World::HealToggle::HotKey);
    CFG_SAVE_B(f,"world.damageboost",  Cheats::World::DamageBoost::Enabled);
    CFG_SAVE_F(f,"world.damagemul",    Cheats::World::DamageBoost::Multiplier);
    CFG_SAVE_B(f,"world.lowdamage",    Cheats::World::LowDamage::Enabled);
    CFG_SAVE_F(f,"world.lowdamagemul", Cheats::World::LowDamage::Multiplier);
    CFG_SAVE_B(f,"world.respawn",      Cheats::World::Respawn::Enabled);
    CFG_SAVE_I(f,"world.respawnkey",   Cheats::World::Respawn::HotKey);
    CFG_SAVE_B(f,"world.unlockcar",    Cheats::World::UnlockCar::Enabled);
    CFG_SAVE_I(f,"world.unlockcarmodkey", Cheats::World::UnlockCar::ModKey);
    CFG_SAVE_B(f,"world.invisible",   Cheats::World::Invisible::Enabled);
    CFG_SAVE_I(f,"world.invisiblekey",Cheats::World::Invisible::HotKey);
    CFG_SAVE_B(f,"world.spectdet",    Cheats::SpectatorDetect::Enabled);
    CFG_SAVE_I(f,"world.spectdet_thr",Cheats::SpectatorDetect::Threshold);
    CFG_SAVE_B(f,"world.lowgrav",     Cheats::LowGravity::Enabled);
    CFG_SAVE_F(f,"world.lowgrav_str", Cheats::LowGravity::Strength);
    CFG_SAVE_B(f,"world.moonjump",    Cheats::MoonJump::Enabled);
    CFG_SAVE_F(f,"world.moonjump_f",  Cheats::MoonJump::Force);


    CFG_SAVE_I(f,"set.menukey",        Cheats::Settings::MenuKey);
    CFG_SAVE_I(f,"set.panickey",       Cheats::Settings::PanicKey);
    CFG_SAVE_I(f,"set.maxplayers",     Cheats::Settings::MaxPlayerCount);
    CFG_SAVE_B(f,"set.streamproof",    Cheats::Settings::StreamProof);
    CFG_SAVE_I(f,"set.theme",          Cheats::Settings::SelectedTheme);
    CFG_SAVE_B(f,"set.watermark",      g_showWatermark);
    CFG_SAVE_S(f,"set.discord_wh",     g_discordWebhook);
    CFG_SAVE_F(f,"set.max_esp_dist",   Cheats::Settings::MaxESPDistance);
    CFG_SAVE_F(f,"set.damage_mult",    Cheats::Settings::DamageMultiplier);
    CFG_SAVE_B(f,"set.unlock_vehicle", Cheats::Settings::UnlockVehicle);
    CFG_SAVE_B(f,"set.low_damage",     Cheats::Settings::LowDamageOn);
    CFG_SAVE_B(f,"set.respawn_death",  Cheats::Settings::RespawnOnDeath);


    for (int fi = 0; fi < (int)friendList.size(); ++fi) {
        const auto& fr = friendList[fi];
        if (fr.alias.empty())
            fprintf(f, "friend.%d=%d\n", fi, fr.id);
        else
            fprintf(f, "friend.%d=%d:%s\n", fi, fr.id, fr.alias.c_str());
    }

    
    for (auto& kv : g_playerAliases)
        if (!kv.second.empty())
            fprintf(f, "alias.%d=%s\n", kv.first, kv.second.c_str());

    fclose(f);
}

static void LoadConfig(const std::string& path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") != 0 || !f) return;

    char line[256], key[128], val[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        // Extract key (everything before '=')
        char* eq = strchr(line, '=');
        if (!eq) continue;
        size_t kl = eq - line;
        if (kl == 0 || kl >= sizeof(key)) continue;
        strncpy_s(key, sizeof(key), line, kl);
        key[kl] = 0;
        strncpy_s(val, sizeof(val), eq + 1, sizeof(val) - 1);
        for (int i=(int)strlen(val)-1;i>=0&&(val[i]=='\n'||val[i]=='\r');--i) val[i]=0;

        // ---- Silent ----
        CFG_LOAD_B("silent.enabled",    Cheats::AimAssist::Silent::Enabled);
        CFG_LOAD_I("silent.hotkey",     Cheats::AimAssist::Silent::HotKey);
        CFG_LOAD_I("silent.fov",        Cheats::AimAssist::Silent::Fov);
        CFG_LOAD_I("silent.maxdist",    Cheats::AimAssist::Silent::MaxDistance);
        CFG_LOAD_I("silent.bone",       Cheats::AimAssist::Silent::BoneMode);
        CFG_LOAD_I("silent.misschance", Cheats::AimAssist::Silent::MissChance);
        CFG_LOAD_B("silent.vischeck",    Cheats::AimAssist::Silent::VisCheck);
        CFG_LOAD_B("silent.skipfriends", Cheats::AimAssist::Silent::SkipFriends);
        CFG_LOAD_B("silent.drawfov",    Cheats::AimAssist::Silent::DrawFov);
        CFG_LOAD_C("silent.fovcolor",   Cheats::AimAssist::Silent::FovColor);
        // ---- Magic Bullet ----
        CFG_LOAD_B("mb.enabled",       Cheats::AimAssist::MagicBullet::Enabled);
        CFG_LOAD_I("mb.bone",          Cheats::AimAssist::MagicBullet::BoneMode);
        CFG_LOAD_I("mb.maxdist",       Cheats::AimAssist::MagicBullet::MaxDistance);
        CFG_LOAD_B("mb.skipfriends",   Cheats::AimAssist::MagicBullet::SkipFriends);
        CFG_LOAD_B("mb.skipnpc",       Cheats::AimAssist::MagicBullet::SkipNPC);
        CFG_LOAD_B("aim.enabled",       Cheats::AimAssist::Aimbot::Enabled);
        CFG_LOAD_I("aim.hotkey",        Cheats::AimAssist::Aimbot::HotKey);
        CFG_LOAD_I("aim.fov",           Cheats::AimAssist::Aimbot::Fov);
        CFG_LOAD_I("aim.smooth",        Cheats::AimAssist::Aimbot::Smooth);
        CFG_LOAD_I("aim.bone",          Cheats::AimAssist::Aimbot::BoneMode);
        CFG_LOAD_I("aim.maxdist",       Cheats::AimAssist::Aimbot::MaxDistance);
        CFG_LOAD_B("aim.sticky",        Cheats::AimAssist::Aimbot::StickyAim);
        CFG_LOAD_I("aim.mode",          Cheats::AimAssist::Aimbot::AimMode);
        CFG_LOAD_I("aim.priority",      Cheats::AimAssist::Aimbot::Priority);
        CFG_LOAD_B("aim.vischeck",       Cheats::AimAssist::Aimbot::VisCheck);
        CFG_LOAD_B("aim.skipfriends",   Cheats::AimAssist::Aimbot::SkipFriends);
        CFG_LOAD_B("aim.drawfov",       Cheats::AimAssist::Aimbot::DrawFov);
        CFG_LOAD_C("aim.fovcolor",      Cheats::AimAssist::Aimbot::FovColor);
        CFG_LOAD_B("trig.enabled",      Cheats::AimAssist::Triggerbot::Enabled);
        CFG_LOAD_I("trig.hotkey",       Cheats::AimAssist::Triggerbot::HotKey);
        CFG_LOAD_I("trig.delay",        Cheats::AimAssist::Triggerbot::Delay);
        CFG_LOAD_I("trig.maxdist",      Cheats::AimAssist::Triggerbot::MaxDistance);
        CFG_LOAD_B("trig.skipfriends",  Cheats::AimAssist::Triggerbot::SkipFriends);
        CFG_LOAD_B("wpn.infiniteammo",  Cheats::AimAssist::Settings::InfiniteAmmo);
        CFG_LOAD_B("wpn.norecoil",      Cheats::AimAssist::Settings::NoRecoil);
        CFG_LOAD_B("wpn.nospread",      Cheats::AimAssist::Settings::NoSpread);
        CFG_LOAD_B("wpn.noreload",      Cheats::AimAssist::Settings::NoReload);
        CFG_LOAD_B("wpn.crosshair",      Cheats::AimAssist::Settings::Crosshair);
        CFG_LOAD_I("wpn.crosshairtype", Cheats::AimAssist::Settings::CrosshairSelectedType);
        CFG_LOAD_C("wpn.crosshaircolor",Cheats::AimAssist::Settings::CrosshairColor);
        CFG_LOAD_I("wpn.crosshairsize", Cheats::AimAssist::Settings::CrosshairSize);
        CFG_LOAD_B("wpn.dyncolor",      Cheats::AimAssist::Settings::DynamicCrosshairColor);
        CFG_LOAD_B("esp.rgb",           Cheats::Players::RgbESP::Enabled);
        CFG_LOAD_F("esp.rgb_speed",     Cheats::Players::RgbESP::Speed);
        CFG_LOAD_F("esp.rgb_sat",       Cheats::Players::RgbESP::Saturation);
        CFG_LOAD_F("esp.rgb_val",       Cheats::Players::RgbESP::Value);
        CFG_LOAD_B("esp.wallcheck",     Cheats::Players::VisCheck::Enabled);
        CFG_LOAD_C("esp.viscolor",      Cheats::Players::VisCheck::VisibleColor);
        CFG_LOAD_C("esp.hidcolor",      Cheats::Players::VisCheck::HiddenColor);
        CFG_LOAD_B("esp.skeleton",      Cheats::Players::VisualMarkers::DrawSkeleton::Enabled);
        CFG_LOAD_C("esp.skelcol",       Cheats::Players::VisualMarkers::DrawSkeleton::Color);
        CFG_LOAD_B("esp.box",           Cheats::Players::VisualMarkers::DrawBox::Enabled);
        CFG_LOAD_C("esp.boxcol",        Cheats::Players::VisualMarkers::DrawBox::Color);
        CFG_LOAD_I("esp.boxtype",       Cheats::Players::VisualMarkers::DrawBox::SelectedType);
        CFG_LOAD_B("esp.line",          Cheats::Players::VisualMarkers::DrawLine::Enabled);
        CFG_LOAD_C("esp.linecol",       Cheats::Players::VisualMarkers::DrawLine::Color);
        CFG_LOAD_I("esp.lineloc",       Cheats::Players::VisualMarkers::DrawLine::SelectedLocation);
        CFG_LOAD_B("esp.bonepoints",    Cheats::Players::VisualMarkers::DrawBonePoints::Enabled);
        CFG_LOAD_C("esp.bonecol",       Cheats::Players::VisualMarkers::DrawBonePoints::Color);
        CFG_LOAD_I("esp.ignoreped",     Cheats::Players::Settings::IgnorePed);
        CFG_LOAD_I("esp.ignoredead",    Cheats::Players::Settings::IgnoreDeath);
        CFG_LOAD_I("esp.maxdist",       Cheats::Players::Settings::MaxDistance);
        CFG_LOAD_B("pi.name",           Cheats::Players::PlayerInfo::DrawName::Enabled);
        CFG_LOAD_C("pi.namecol",        Cheats::Players::PlayerInfo::DrawName::Color);
        CFG_LOAD_B("pi.id",             Cheats::Players::PlayerInfo::DrawId::Enabled);
        CFG_LOAD_B("pi.dist",           Cheats::Players::PlayerInfo::DrawDistance::Enabled);
        CFG_LOAD_B("pi.weapon",         Cheats::Players::PlayerInfo::DrawWeaponName::Enabled);
        CFG_LOAD_B("world.semigod",     Cheats::World::SemiGodMode::Enabled);
        CFG_LOAD_B("world.supersprint", Cheats::World::SuperSprint::Enabled);
        CFG_LOAD_I("world.sprintspd",   Cheats::World::SuperSprint::Speed);
        CFG_LOAD_B("world.stamina",     Cheats::World::InfiniteStamina::Enabled);
        CFG_LOAD_B("world.explosive",   Cheats::World::ExplosiveBullets::Enabled);
        CFG_LOAD_B("world.fire",        Cheats::World::FireBullets::Enabled);
        CFG_LOAD_B("world.rapidfire",   Cheats::World::RapidFire::Enabled);
        CFG_LOAD_B("world.healtoggle",   Cheats::World::HealToggle::Enabled);
        CFG_LOAD_I("world.healtogglekey",Cheats::World::HealToggle::HotKey);
        CFG_LOAD_B("world.damageboost", Cheats::World::DamageBoost::Enabled);
        CFG_LOAD_F("world.damagemul",   Cheats::World::DamageBoost::Multiplier);
        CFG_LOAD_B("world.lowdamage",   Cheats::World::LowDamage::Enabled);
        CFG_LOAD_F("world.lowdamagemul",Cheats::World::LowDamage::Multiplier);
        CFG_LOAD_B("world.respawn",     Cheats::World::Respawn::Enabled);
        CFG_LOAD_I("world.respawnkey",  Cheats::World::Respawn::HotKey);
        CFG_LOAD_B("world.unlockcar",   Cheats::World::UnlockCar::Enabled);
        CFG_LOAD_I("world.unlockcarmodkey",Cheats::World::UnlockCar::ModKey);
        CFG_LOAD_B("world.invisible",   Cheats::World::Invisible::Enabled);
        CFG_LOAD_I("world.invisiblekey",Cheats::World::Invisible::HotKey);
        CFG_LOAD_B("world.spectdet",    Cheats::SpectatorDetect::Enabled);
        CFG_LOAD_I("world.spectdet_thr",Cheats::SpectatorDetect::Threshold);
        CFG_LOAD_B("world.lowgrav",     Cheats::LowGravity::Enabled);
        CFG_LOAD_F("world.lowgrav_str", Cheats::LowGravity::Strength);
        CFG_LOAD_B("world.moonjump",    Cheats::MoonJump::Enabled);
        CFG_LOAD_F("world.moonjump_f",  Cheats::MoonJump::Force);
        CFG_LOAD_I("set.menukey",       Cheats::Settings::MenuKey);
        CFG_LOAD_I("set.panickey",      Cheats::Settings::PanicKey);
        CFG_LOAD_I("set.maxplayers",    Cheats::Settings::MaxPlayerCount);
        // StreamProof ve watermark config'den yüklenmiyor —
        // her zaman GuiGlobal default değeriyle başlar (StreamProof=true, watermark=true)
        CFG_LOAD_I("set.theme",         Cheats::Settings::SelectedTheme);
        CFG_LOAD_S("set.discord_wh",    g_discordWebhook);
        CFG_LOAD_F("set.max_esp_dist",  Cheats::Settings::MaxESPDistance);
        CFG_LOAD_F("set.damage_mult",   Cheats::Settings::DamageMultiplier);
        CFG_LOAD_B("set.unlock_vehicle",Cheats::Settings::UnlockVehicle);
        CFG_LOAD_B("set.low_damage",    Cheats::Settings::LowDamageOn);
        CFG_LOAD_B("set.respawn_death", Cheats::Settings::RespawnOnDeath);

        if (strncmp(key, "friend.", 7) == 0) {
            int fid = 0; char falias[64] = {};
            if (sscanf_s(val, "%d:%63s", &fid, falias, (unsigned)sizeof(falias)) >= 1 && fid > 0) {
               
                bool dup = false;
                for (auto& fr : friendList) if (fr.id == fid) { dup = true; break; }
                if (!dup) friendList.push_back({fid, std::string(falias)});
            }
            continue;
        }        
        if (strncmp(key, "alias.", 6) == 0) {
            int aid = atoi(key + 6);
            if (aid > 0 && val[0] != '\0')
                g_playerAliases[aid] = std::string(val);
            continue;
        }
    }
    fclose(f);
}

// ─── Remote config server (Cloudflare Workers, HTTPS) ──────────────────────
#define CFG_SRV_HOST  L"moon-auth-service.moonsal.workers.dev"
#define CFG_SRV_PORT  443

inline std::string& UserName() { static std::string n = "unknown"; return n; }

static std::string GetHWID() {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
        char guid[48] = {};
        DWORD sz = sizeof(guid);
        LONG rc = RegQueryValueExA(hk, "MachineGuid", nullptr, nullptr,
                                   (LPBYTE)guid, &sz);
        RegCloseKey(hk);
        if (rc == ERROR_SUCCESS) {
            std::string s;
            for (char c : std::string(guid)) {
                if (c == '-') continue;
                s += (char)(c >= 'a' && c <= 'f' ? c - 32 : c);
                if (s.size() >= 8) break;
            }
            if (s.size() == 8) return s;
        }
    }
    DWORD serial = 0;
    GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    char buf[16]; sprintf_s(buf, "%08X", serial);
    return buf;
}

static std::string _CfgHttpGet(const wchar_t* path) {
    std::string out;
    HINTERNET s = WinHttpOpen(L"a", WINHTTP_ACCESS_TYPE_NO_PROXY, 0, 0, 0);
    if (!s) return out;
    HINTERNET c = WinHttpConnect(s, CFG_SRV_HOST, CFG_SRV_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, 0, 0, 0, 0) : nullptr;
    if (r && WinHttpSendRequest(r, 0, 0, 0, 0, 0, 0) && WinHttpReceiveResponse(r, 0)) {
        DWORD n = 0; char buf[4096];
        while (WinHttpQueryDataAvailable(r, &n) && n) {
            DWORD rd = 0; if (n > sizeof(buf)) n = sizeof(buf);
            WinHttpReadData(r, buf, n, &rd);
            out.append(buf, rd);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return out;
}

static void _CfgHttpPost(const wchar_t* path, const std::string& body) {
    HINTERNET s = WinHttpOpen(L"a", WINHTTP_ACCESS_TYPE_NO_PROXY, 0, 0, 0);
    if (!s) return;
    HINTERNET c = WinHttpConnect(s, CFG_SRV_HOST, CFG_SRV_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path, 0, 0, 0, 0) : nullptr;
    if (r) WinHttpSendRequest(r, 0, 0, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (r) { WinHttpReceiveResponse(r, 0); WinHttpCloseHandle(r); }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
}

static std::wstring _HwidPath() {
    std::string h = GetHWID();
    return L"/cfg?h=" + std::wstring(h.begin(), h.end());
}

static void LoadConfigRemote() {
    std::string resp = _CfgHttpGet(_HwidPath().c_str());
    if (resp.size() < 6) {
        // Sunucu kapalı → yerel yedekten yükle
        LoadConfig(CfgPath());
        return;
    }

    auto nl = resp.find('\n');
    if (nl != std::string::npos && resp.compare(0, 5, "NAME:") == 0) {
        UserName() = resp.substr(5, nl - 5);
        ActiveConfigName() = UserName();
        resp = resp.substr(nl + 1);
    }
    if (resp.empty()) return;

    std::string tmp = CfgDir() + "_r.tmp";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") == 0 && f) {
        fwrite(resp.data(), 1, resp.size(), f); fclose(f);
        LoadConfig(tmp);
        // Yerel yedek olarak da kaydet
        CopyFileA(tmp.c_str(), CfgPath().c_str(), FALSE);
        DeleteFileA(tmp.c_str());
    }
}

static void SaveConfigRemote() {
    std::string tmp = CfgDir() + "_r.tmp";
    SaveConfig(tmp);
    // Her zaman yerel yedek de tut
    SaveConfig(CfgPath());
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "rb") != 0 || !f) return;
    std::string body; char buf[256];
    while (fgets(buf, sizeof(buf), f)) body += buf;
    fclose(f);
    DeleteFileA(tmp.c_str());
    _CfgHttpPost(_HwidPath().c_str(), body);
}
// ─────────────────────────────────────────────────────────────────────────
