// ─────────────────────────────────────────────────────────────────────────────
//  WsClient.hpp — DLL-side WebSocket relay client (FULL FEATURE PARITY)
//
//  Key names are identical to WebServer.hpp so browser menu works with the
//  same JSON keys for both read (state) and write (set command).
//
//  Init:   WsClientInit(lpReserved)  — called from DllMain before any thread
//  Entry:  WsClientThread()          — started from CheatThreadBody
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <string>
#pragma comment(lib, "winhttp.lib")

// ── Init params — must match cheatload.c _FudInit byte-for-byte ─────────────
struct FudInitParams {
    char          cheatKey[72];
    char          wsHost[128];
    INTERNET_PORT wsPort;
};

static char          g_wsCheatKey[72] = {};
static wchar_t       g_wsHostW[128]   = {};
static INTERNET_PORT g_wsPort         = INTERNET_DEFAULT_HTTPS_PORT;
static volatile bool g_wsRunning      = false;

static void WsClientInit(void* lpReserved) {
    if (!lpReserved) return;
    auto* p = (FudInitParams*)lpReserved;
    memcpy(g_wsCheatKey, p->cheatKey, sizeof(g_wsCheatKey));
    g_wsPort = p->wsPort ? p->wsPort : INTERNET_DEFAULT_HTTPS_PORT;
    MultiByteToWideChar(CP_ACP, 0, p->wsHost, -1, g_wsHostW, 128);
}

// ── Hex colour helpers ────────────────────────────────────────────────────────
static std::string _colToHex(ImColor c) {
    ImVec4 v = (ImVec4)c;
    char buf[8];
    snprintf(buf,sizeof(buf),"#%02x%02x%02x",
        (int)(v.x*255.f+.5f),(int)(v.y*255.f+.5f),(int)(v.z*255.f+.5f));
    return buf;
}
static void _hexToCol(const char* h, ImColor& out) {
    if (!h || h[0]!='#' || strlen(h)<7) return;
    unsigned r=0,g=0,b=0;
    sscanf_s(h+1,"%02x%02x%02x",&r,&g,&b);
    out = ImColor((int)r,(int)g,(int)b,255);
}

// ── JSON write helpers ────────────────────────────────────────────────────────
static int _jb(char* d,int off,int lim,const char* k,bool v){
    return snprintf(d+off,lim-off,"\"%s\":%s,",k,v?"true":"false"); }
static int _ji(char* d,int off,int lim,const char* k,int v){
    return snprintf(d+off,lim-off,"\"%s\":%d,",k,v); }
static int _jf(char* d,int off,int lim,const char* k,float v){
    return snprintf(d+off,lim-off,"\"%s\":%.4f,",k,(double)v); }
static int _jc(char* d,int off,int lim,const char* k,ImColor v){
    std::string h=_colToHex(v);
    return snprintf(d+off,lim-off,"\"%s\":\"%s\",",k,h.c_str()); }

// ── JSON read helpers ─────────────────────────────────────────────────────────
static const char* _jfv(const char* j,const char* key){
    char pat[80]; snprintf(pat,sizeof(pat),"\"%s\"",key);
    const char* p=strstr(j,pat);
    if(!p) return nullptr;
    p+=strlen(pat);
    while(*p==' '||*p=='\t'||*p==':') p++;
    return p;
}
static bool _jStr(const char* j,const char* key,char* out,int len){
    const char* p=_jfv(j,key);
    if(!p||*p!='"') return false; p++;
    int i=0; while(*p&&*p!='"'&&i<len-1) out[i++]=*p++;
    out[i]=0; return true;
}
static bool _jBool(const char* j,const char* key,bool& out){
    const char* p=_jfv(j,key);
    if(!p) return false;
    if(strncmp(p,"true" ,4)==0){out=true; return true;}
    if(strncmp(p,"false",5)==0){out=false;return true;}
    return false;
}
static bool _jInt(const char* j,const char* key,int& out){
    const char* p=_jfv(j,key);
    if(!p) return false;
    if(*p=='-'||(*p>='0'&&*p<='9')){out=atoi(p);return true;}
    return false;
}
static bool _jFlt(const char* j,const char* key,float& out){
    const char* p=_jfv(j,key);
    if(!p) return false;
    if(*p=='-'||(*p>='0'&&*p<='9')){out=(float)atof(p);return true;}
    return false;
}
static void _jEscape(const char* src,char* dst,int dstLen){
    int i=0,d=0;
    while(src[i]&&d<dstLen-3){ char c=src[i++];
        if(c=='"'||c=='\\') dst[d++]='\\'; dst[d++]=c; }
    dst[d]=0;
}

// ── Command dispatcher ────────────────────────────────────────────────────────
// Keys match WebServer.hpp BuildSettingsJson / ApplySettingsJson exactly.
static void WsApply(const char* json) {
    char cmd[32]={};
    if(!_jStr(json,"cmd",cmd,sizeof(cmd))) return;

    // ── set: single-field update ──────────────────────────────────────────────
    if(strcmp(cmd,"set")==0){
        char k[64]={}; bool b; int iv; float fv; char sv[32]={};
        if(!_jStr(json,"key",k,sizeof(k))) return;

#define SBS(KEY,VAR) if(!strcmp(k,KEY)&&_jBool(json,"val",b)){VAR=(bool)b;return;}
#define SB(KEY,VAR)  if(!strcmp(k,KEY)&&_jBool(json,"val",b)){VAR=b;return;}
#define SI(KEY,VAR)  if(!strcmp(k,KEY)&&_jInt(json,"val",iv)){VAR=iv;return;}
#define SF(KEY,VAR)  if(!strcmp(k,KEY)&&_jFlt(json,"val",fv)){VAR=fv;return;}
#define SC(KEY,VAR)  if(!strcmp(k,KEY)&&_jStr(json,"val",sv,sizeof(sv))){_hexToCol(sv,VAR);return;}

        // ── Silent ──
        SBS("silent_en",     Cheats::AimAssist::Silent::Enabled)
        SI ("silent_hk",     Cheats::AimAssist::Silent::HotKey)
        SB ("silent_toggle", Cheats::AimAssist::Silent::ToggleMode)
        SI ("silent_fov",    Cheats::AimAssist::Silent::Fov)
        SI ("silent_dist",   Cheats::AimAssist::Silent::MaxDistance)
        SI ("silent_miss",   Cheats::AimAssist::Silent::MissChance)
        SI ("silent_bone",   Cheats::AimAssist::Silent::BoneMode)
        SB ("silent_fov_draw",Cheats::AimAssist::Silent::DrawFov)
        SI ("silent_fov_w",  Cheats::AimAssist::Silent::FovWeight)
        SC ("c_silent_fov",  Cheats::AimAssist::Silent::FovColor)
        SB ("silent_vis",    Cheats::AimAssist::Silent::VisCheck)
        SB ("silent_skip",   Cheats::AimAssist::Silent::SkipFriends)

        // ── Magic Bullet ──
        SBS("mb_en",     Cheats::AimAssist::MagicBullet::Enabled)
        SI ("mb_bone",   Cheats::AimAssist::MagicBullet::BoneMode)
        SI ("mb_dist",   Cheats::AimAssist::MagicBullet::MaxDistance)
        SB ("mb_skipfr", Cheats::AimAssist::MagicBullet::SkipFriends)
        SB ("mb_skipnpc",Cheats::AimAssist::MagicBullet::SkipNPC)

        // ── Aimbot ──
        SBS("aim_en",      Cheats::AimAssist::Aimbot::Enabled)
        SI ("aim_hk",      Cheats::AimAssist::Aimbot::HotKey)
        SB ("aim_toggle",  Cheats::AimAssist::Aimbot::ToggleMode)
        SI ("aim_fov",     Cheats::AimAssist::Aimbot::Fov)
        SI ("aim_smooth",  Cheats::AimAssist::Aimbot::Smooth)
        SI ("aim_dist",    Cheats::AimAssist::Aimbot::MaxDistance)
        SI ("aim_bone",    Cheats::AimAssist::Aimbot::BoneMode)
        SI ("aim_mode",    Cheats::AimAssist::Aimbot::AimMode)
        SI ("aim_prio",    Cheats::AimAssist::Aimbot::Priority)
        SB ("aim_fov_draw",Cheats::AimAssist::Aimbot::DrawFov)
        SI ("aim_fov_w",   Cheats::AimAssist::Aimbot::FovWeight)
        SC ("c_aim_fov",   Cheats::AimAssist::Aimbot::FovColor)
        SB ("aim_sticky",  Cheats::AimAssist::Aimbot::StickyAim)
        SB ("aim_vis",     Cheats::AimAssist::Aimbot::VisCheck)
        SB ("aim_skip",    Cheats::AimAssist::Aimbot::SkipFriends)

        // ── Triggerbot ──
        SB("trig_en",   Cheats::AimAssist::Triggerbot::Enabled)
        SI("trig_hk",   Cheats::AimAssist::Triggerbot::HotKey)
        SI("trig_delay",Cheats::AimAssist::Triggerbot::Delay)
        SI("trig_dist", Cheats::AimAssist::Triggerbot::MaxDistance)
        SB("trig_skip", Cheats::AimAssist::Triggerbot::SkipFriends)

        // ── Weapon ──
        SB("wpn_ammo",     Cheats::AimAssist::Settings::InfiniteAmmo)
        SB("wpn_recoil",   Cheats::AimAssist::Settings::NoRecoil)
        SB("wpn_spread",   Cheats::AimAssist::Settings::NoSpread)
        SB("wpn_reload",   Cheats::AimAssist::Settings::NoReload)
        SB("wpn_rapid",    Cheats::World::RapidFire::Enabled)
        SB("wpn_crosshair",Cheats::AimAssist::Settings::Crosshair)
        SI("wpn_xtype",    Cheats::AimAssist::Settings::CrosshairSelectedType)
        SI("wpn_xsize",    Cheats::AimAssist::Settings::CrosshairSize)

        // ── ESP / RGB ──
        SB("esp_rgb",    Cheats::Players::RgbESP::Enabled)
        SF("esp_rgb_spd",Cheats::Players::RgbESP::Speed)
        SF("esp_rgb_sat",Cheats::Players::RgbESP::Saturation)
        SF("esp_rgb_val",Cheats::Players::RgbESP::Value)

        // ── ESP / VisCheck ──
        SB("esp_vis",   Cheats::Players::VisCheck::Enabled)
        SC("c_esp_vis", Cheats::Players::VisCheck::VisibleColor)
        SC("c_esp_hid", Cheats::Players::VisCheck::HiddenColor)

        // ── ESP / FriendESP ──
        SB("friend_esp_en",Cheats::Players::FriendESP::Enabled)
        SC("c_friend_esp", Cheats::Players::FriendESP::Color)

        // ── ESP / TagCount ──
        SB("tagcount_en",Cheats::Players::TagCount::Enabled)
        SC("c_tagcount", Cheats::Players::TagCount::Color)

        // ── ESP / GlobalSettings ──
        SI("esp_linetype",Cheats::Players::VisualMarkers::GlobalSettings::SelectedLineType)
        SI("esp_linew",   Cheats::Players::VisualMarkers::GlobalSettings::LineWeight)

        // ── ESP / Skeleton ──
        SB("esp_skel",  Cheats::Players::VisualMarkers::DrawSkeleton::Enabled)
        SC("c_esp_skel",Cheats::Players::VisualMarkers::DrawSkeleton::Color)

        // ── ESP / Box ──
        SB("esp_box",   Cheats::Players::VisualMarkers::DrawBox::Enabled)
        SI("esp_boxtype",Cheats::Players::VisualMarkers::DrawBox::SelectedType)
        SC("c_esp_box", Cheats::Players::VisualMarkers::DrawBox::Color)

        // ── ESP / Line ──
        SB("esp_line",   Cheats::Players::VisualMarkers::DrawLine::Enabled)
        SI("esp_lineloc",Cheats::Players::VisualMarkers::DrawLine::SelectedLocation)
        SC("c_esp_line", Cheats::Players::VisualMarkers::DrawLine::Color)

        // ── ESP / BonePoints ──
        SB("esp_bone",  Cheats::Players::VisualMarkers::DrawBonePoints::Enabled)
        SI("esp_bone_r",Cheats::Players::VisualMarkers::DrawBonePoints::Radius)
        SC("c_esp_bone",Cheats::Players::VisualMarkers::DrawBonePoints::Color)

        // ── ESP / Settings ──
        SI("esp_dist",  Cheats::Players::Settings::MaxDistance)
        if(!strcmp(k,"esp_noped") &&_jBool(json,"val",b)){Cheats::Players::Settings::IgnorePed   =(int)b;return;}
        if(!strcmp(k,"esp_nodead")&&_jBool(json,"val",b)){Cheats::Players::Settings::IgnoreDeath =(int)b;return;}
        SB("esp_offscreen",Cheats::Players::OffscreenESP::Enabled)

        // ── Player Info ──
        SI("pi_maxdist", Cheats::Players::PlayerInfo::GlobalSettings::MaxDistance)
        SB("pi_name",    Cheats::Players::PlayerInfo::DrawName::Enabled)
        SI("pi_name_loc",Cheats::Players::PlayerInfo::DrawName::SelectedLocation)
        SC("c_pi_name",  Cheats::Players::PlayerInfo::DrawName::Color)
        SB("pi_id",      Cheats::Players::PlayerInfo::DrawId::Enabled)
        SI("pi_id_loc",  Cheats::Players::PlayerInfo::DrawId::SelectedLocation)
        SC("c_pi_id",    Cheats::Players::PlayerInfo::DrawId::Color)
        SB("pi_dist",    Cheats::Players::PlayerInfo::DrawDistance::Enabled)
        SI("pi_dist_loc",Cheats::Players::PlayerInfo::DrawDistance::SelectedLocation)
        SC("c_pi_dist",  Cheats::Players::PlayerInfo::DrawDistance::Color)
        SB("pi_weapon",  Cheats::Players::PlayerInfo::DrawWeaponName::Enabled)
        SI("pi_wpn_loc", Cheats::Players::PlayerInfo::DrawWeaponName::SelectedLocation)
        SC("c_pi_weapon",Cheats::Players::PlayerInfo::DrawWeaponName::Color)

        // ── Status Bars ──
        SB("sb_hp",      Cheats::Players::StatusBars::DrawHealthBar::Enabled)
        SI("sb_hp_loc",  Cheats::Players::StatusBars::DrawHealthBar::SelectedLocation)
        SB("sb_armor",   Cheats::Players::StatusBars::DrawArmorBar::Enabled)
        SI("sb_armor_loc",Cheats::Players::StatusBars::DrawArmorBar::SelectedLocation)

        // ── Vehicles ──
        SB("veh_pt",      Cheats::Vehicles::DrawPoint::Enabled)
        SC("c_veh_pt",    Cheats::Vehicles::DrawPoint::Color)
        SI("veh_pt_size", Cheats::Vehicles::DrawPoint::Size)
        SB("veh_ln",      Cheats::Vehicles::DrawLine::Enabled)
        SC("c_veh_ln",    Cheats::Vehicles::DrawLine::Color)
        SI("veh_ln_loc",  Cheats::Vehicles::DrawLine::SelectedLocation)
        SB("veh_dist_en", Cheats::Vehicles::DrawDistance::Enabled)
        SC("c_veh_dist",  Cheats::Vehicles::DrawDistance::Color)
        SB("veh_hp",      Cheats::Vehicles::DrawHealthBar::Enabled)
        SB("veh_ignlocal",Cheats::Vehicles::Settings::IgnoreLocalVehicle)
        SI("veh_maxcount",Cheats::Vehicles::Settings::MaxVehicleCount)
        SI("veh_maxdist", Cheats::Vehicles::Settings::MaxDistance)
        SB("veh_fix",     Cheats::Vehicles::VehicleFix::Enabled)
        SI("veh_fix_hk",  Cheats::Vehicles::VehicleFix::HotKey)
        SB("veh_god",     Cheats::Vehicles::VehicleGodMode::Enabled)
        SB("veh_spd",     Cheats::Vehicles::SpeedBoost::Enabled)
        SI("veh_spd_kmh", Cheats::Vehicles::SpeedBoost::KmH)
        SB("veh_flip",    Cheats::Vehicles::VehicleFlip::Enabled)
        SI("veh_flip_hk", Cheats::Vehicles::VehicleFlip::HotKey)

        // ── World ──
        SB("w_noclip",       Cheats::World::NoClip::Enabled)
        SI("w_noclip_spd",   Cheats::World::NoClip::MovementSpeed)
        SB("w_semigod",      Cheats::World::SemiGodMode::Enabled)
        SB("w_sprint",       Cheats::World::SuperSprint::Enabled)
        SI("w_sprintspd",    Cheats::World::SuperSprint::Speed)
        SB("w_stamina",      Cheats::World::InfiniteStamina::Enabled)
        SB("w_explode",      Cheats::World::ExplosiveBullets::Enabled)
        SB("w_fire",         Cheats::World::FireBullets::Enabled)
        SB("w_healtoggle",   Cheats::World::HealToggle::Enabled)
        SI("w_healtogglekey",Cheats::World::HealToggle::HotKey)
        SB("w_armorfill",    Cheats::World::ArmorFill::Enabled)
        SI("w_armorfillkey", Cheats::World::ArmorFill::HotKey)
        SB("w_damageboost",  Cheats::World::DamageBoost::Enabled)
        SF("w_damagemul",    Cheats::World::DamageBoost::Multiplier)
        SB("w_lowdamage",    Cheats::World::LowDamage::Enabled)
        SF("w_lowdamagemul", Cheats::World::LowDamage::Multiplier)
        SB("w_respawn",      Cheats::World::Respawn::Enabled)
        SB("w_unlockcar",    Cheats::World::UnlockCar::Enabled)
        SI("w_unlockcar_mk", Cheats::World::UnlockCar::ModKey)
        SB("w_invisible",    Cheats::World::Invisible::Enabled)
        SI("w_invisible_hk", Cheats::World::Invisible::HotKey)
        SB("w_spectdet",     Cheats::SpectatorDetect::Enabled)
        SI("w_spectdet_thr", Cheats::SpectatorDetect::Threshold)
        SB("w_lowgrav",      Cheats::LowGravity::Enabled)
        SF("w_lowgrav_str",  Cheats::LowGravity::Strength)
        SB("w_moonjump",     Cheats::MoonJump::Enabled)
        SF("w_moonjump_f",   Cheats::MoonJump::Force)

        // ── Settings ──
        SB("s_stream",  Cheats::Settings::StreamProof)
        SI("s_theme",   Cheats::Settings::SelectedTheme)
        SI("s_maxplrs", Cheats::Settings::MaxPlayerCount)

#undef SBS
#undef SB
#undef SI
#undef SF
#undef SC
        return;
    }

    // ── tp: absolute coords ───────────────────────────────────────────────────
    if(strcmp(cmd,"tp")==0){
        float x=0,y=0,z=0;
        _jFlt(json,"x",x); _jFlt(json,"y",y); _jFlt(json,"z",z);
        Vector3 pos{x,y,z};
        std::thread([pos](){ __try{PositionTeleport(pos);}__except(EXCEPTION_EXECUTE_HANDLER){} }).detach();
        return;
    }

    // ── tp_player: teleport to player by ID ──────────────────────────────────
    if(strcmp(cmd,"tp_player")==0){
        int id=0; _jInt(json,"id",id);
        g_pendingAction.type=PA_TELEPORT_TO; g_pendingAction.targetId=id; return;
    }

    // ── tp_preset: preset location ───────────────────────────────────────────
    if(strcmp(cmd,"tp_preset")==0){
        int idx=0; _jInt(json,"idx",idx);
        int cnt=(int)(sizeof(Cheats::World::Teleport::Locations)/sizeof(Cheats::World::Teleport::Locations[0]));
        if(idx>=0&&idx<cnt){
            auto& loc=Cheats::World::Teleport::Locations[idx];
            Vector3 pos{loc.x,loc.y,loc.z};
            std::thread([pos](){ __try{PositionTeleport(pos);}__except(EXCEPTION_EXECUTE_HANDLER){} }).detach();
        }
        return;
    }

    // ── outfit: copy outfit from player ──────────────────────────────────────
    if(strcmp(cmd,"outfit")==0){
        int src=0; _jInt(json,"src",src);
        g_pendingAction.type=PA_COPY_OUTFIT; g_pendingAction.targetId=src; return;
    }

    // ── save: persist to remote config ───────────────────────────────────────
    if(strcmp(cmd,"save")==0){
        std::thread([](){__try{SaveConfigRemote();}__except(EXCEPTION_EXECUTE_HANDLER){}}).detach();
        return;
    }
}

// ── State serialiser ──────────────────────────────────────────────────────────
// Produces the same keys as WebServer.hpp BuildSettingsJson  +  player array.
#define JB(k,v) do{ int n=_jb(buf,off,lim,k,(bool)(v)); if(n>0&&off+n<lim) off+=n; }while(0)
#define JI(k,v) do{ int n=_ji(buf,off,lim,k,(int)(v));  if(n>0&&off+n<lim) off+=n; }while(0)
#define JF(k,v) do{ int n=_jf(buf,off,lim,k,(float)(v));if(n>0&&off+n<lim) off+=n; }while(0)
#define JC(k,v) do{ int n=_jc(buf,off,lim,k,(v));       if(n>0&&off+n<lim) off+=n; }while(0)

static void WsBuildState(char* buf, int lim) {
    int off=0;

    // ── Player list ──────────────────────────────────────────────────────────
    static char plist[16384];
    plist[0]='['; plist[1]=0; int pl=1;
    {
        std::vector<Ped> snap;
        { std::lock_guard<std::mutex> lk(g_pedMutex); snap=PedList; }
        Vector3 lpos={};
        if(LocalPlayer.Pointer) lpos=ReadMemory<Vector3>(LocalPlayer.Pointer+0x90);

        char nb[128],ns[256];
        for(int i=0;i<(int)snap.size()&&pl<(int)sizeof(plist)-180;i++){
            if(i>0&&pl<(int)sizeof(plist)-2) plist[pl++]=',';
            int   id   = snap[i].GetId();
            float hp   = snap[i].GetHealth();
            float arm  = snap[i].GetArmor();
            float dist = GetDistance(snap[i].Position,lpos);
            bool  vis  = snap[i].IsVisible();
            bool  fr   = IsFriend(id);
            const std::string& nm=snap[i].Name;
            strncpy_s(nb,sizeof(nb),nm.c_str(),_TRUNCATE);
            _jEscape(nb,ns,sizeof(ns));
            std::string wpn=snap[i].GetWeaponName();
            char wns[128]={};_jEscape(wpn.c_str(),wns,sizeof(wns));
            pl+=snprintf(plist+pl,sizeof(plist)-pl,
                "{\"id\":%d,\"name\":\"%s\",\"wpn\":\"%s\","
                "\"hp\":%.0f,\"armor\":%.0f,\"dist\":%.0f,"
                "\"vis\":%s,\"friend\":%s}",
                id,ns,wns,hp,arm,dist,
                vis?"true":"false",fr?"true":"false");
        }
    }
    if(pl<(int)sizeof(plist)-2){plist[pl++]=']';plist[pl]=0;}

    // ── Self stats ───────────────────────────────────────────────────────────
    float myHp=0,myAr=0; char myPos[64]="0,0,0";
    if(LocalPlayer.Pointer){
        myHp=ReadMemory<float>(LocalPlayer.Pointer+Offsets.Health);
        myAr=ReadMemory<float>(LocalPlayer.Pointer+Offsets.Armor);
        Vector3 p=ReadMemory<Vector3>(LocalPlayer.Pointer+0x90);
        snprintf(myPos,sizeof(myPos),"%.1f,%.1f,%.1f",p.x,p.y,p.z);
    }

    // ── JSON body ────────────────────────────────────────────────────────────
    off+=snprintf(buf+off,lim-off,"{\"type\":\"state\",");
    JB("connected",  g_gameConnected);
    off+=snprintf(buf+off,lim-off,"\"myHp\":%.0f,\"myArmor\":%.0f,\"myPos\":[%s],",myHp,myAr,myPos);

    // Silent
    JB("silent_en",      (bool)Cheats::AimAssist::Silent::Enabled);
    JI("silent_hk",      Cheats::AimAssist::Silent::HotKey);
    JB("silent_toggle",  Cheats::AimAssist::Silent::ToggleMode);
    JI("silent_fov",     Cheats::AimAssist::Silent::Fov);
    JI("silent_dist",    Cheats::AimAssist::Silent::MaxDistance);
    JI("silent_miss",    Cheats::AimAssist::Silent::MissChance);
    JI("silent_bone",    Cheats::AimAssist::Silent::BoneMode);
    JB("silent_fov_draw",Cheats::AimAssist::Silent::DrawFov);
    JI("silent_fov_w",   Cheats::AimAssist::Silent::FovWeight);
    JC("c_silent_fov",   Cheats::AimAssist::Silent::FovColor);
    JB("silent_vis",     Cheats::AimAssist::Silent::VisCheck);
    JB("silent_skip",    Cheats::AimAssist::Silent::SkipFriends);

    // MagicBullet
    JB("mb_en",     (bool)Cheats::AimAssist::MagicBullet::Enabled);
    JI("mb_bone",   Cheats::AimAssist::MagicBullet::BoneMode);
    JI("mb_dist",   Cheats::AimAssist::MagicBullet::MaxDistance);
    JB("mb_skipfr", Cheats::AimAssist::MagicBullet::SkipFriends);
    JB("mb_skipnpc",Cheats::AimAssist::MagicBullet::SkipNPC);

    // Aimbot
    JB("aim_en",      (bool)Cheats::AimAssist::Aimbot::Enabled);
    JI("aim_hk",      Cheats::AimAssist::Aimbot::HotKey);
    JB("aim_toggle",  Cheats::AimAssist::Aimbot::ToggleMode);
    JI("aim_fov",     Cheats::AimAssist::Aimbot::Fov);
    JI("aim_smooth",  Cheats::AimAssist::Aimbot::Smooth);
    JI("aim_dist",    Cheats::AimAssist::Aimbot::MaxDistance);
    JI("aim_bone",    Cheats::AimAssist::Aimbot::BoneMode);
    JI("aim_mode",    Cheats::AimAssist::Aimbot::AimMode);
    JI("aim_prio",    Cheats::AimAssist::Aimbot::Priority);
    JB("aim_fov_draw",Cheats::AimAssist::Aimbot::DrawFov);
    JI("aim_fov_w",   Cheats::AimAssist::Aimbot::FovWeight);
    JC("c_aim_fov",   Cheats::AimAssist::Aimbot::FovColor);
    JB("aim_sticky",  Cheats::AimAssist::Aimbot::StickyAim);
    JB("aim_vis",     Cheats::AimAssist::Aimbot::VisCheck);
    JB("aim_skip",    Cheats::AimAssist::Aimbot::SkipFriends);

    // Triggerbot
    JB("trig_en",   Cheats::AimAssist::Triggerbot::Enabled);
    JI("trig_hk",   Cheats::AimAssist::Triggerbot::HotKey);
    JI("trig_delay",Cheats::AimAssist::Triggerbot::Delay);
    JI("trig_dist", Cheats::AimAssist::Triggerbot::MaxDistance);
    JB("trig_skip", Cheats::AimAssist::Triggerbot::SkipFriends);

    // Weapon
    JB("wpn_ammo",     Cheats::AimAssist::Settings::InfiniteAmmo);
    JB("wpn_recoil",   Cheats::AimAssist::Settings::NoRecoil);
    JB("wpn_spread",   Cheats::AimAssist::Settings::NoSpread);
    JB("wpn_reload",   Cheats::AimAssist::Settings::NoReload);
    JB("wpn_rapid",    Cheats::World::RapidFire::Enabled);
    JB("wpn_crosshair",Cheats::AimAssist::Settings::Crosshair);
    JI("wpn_xtype",    Cheats::AimAssist::Settings::CrosshairSelectedType);
    JI("wpn_xsize",    Cheats::AimAssist::Settings::CrosshairSize);

    // ESP/RGB
    JB("esp_rgb",    Cheats::Players::RgbESP::Enabled);
    JF("esp_rgb_spd",Cheats::Players::RgbESP::Speed);
    JF("esp_rgb_sat",Cheats::Players::RgbESP::Saturation);
    JF("esp_rgb_val",Cheats::Players::RgbESP::Value);

    // ESP/VisCheck
    JB("esp_vis",   Cheats::Players::VisCheck::Enabled);
    JC("c_esp_vis", Cheats::Players::VisCheck::VisibleColor);
    JC("c_esp_hid", Cheats::Players::VisCheck::HiddenColor);

    // Friend/Tag
    JB("friend_esp_en",Cheats::Players::FriendESP::Enabled);
    JC("c_friend_esp", Cheats::Players::FriendESP::Color);
    JB("tagcount_en",  Cheats::Players::TagCount::Enabled);
    JC("c_tagcount",   Cheats::Players::TagCount::Color);

    // ESP global
    JI("esp_linetype",Cheats::Players::VisualMarkers::GlobalSettings::SelectedLineType);
    JI("esp_linew",   Cheats::Players::VisualMarkers::GlobalSettings::LineWeight);

    // Skeleton
    JB("esp_skel",  Cheats::Players::VisualMarkers::DrawSkeleton::Enabled);
    JC("c_esp_skel",Cheats::Players::VisualMarkers::DrawSkeleton::Color);

    // Box
    JB("esp_box",   Cheats::Players::VisualMarkers::DrawBox::Enabled);
    JI("esp_boxtype",Cheats::Players::VisualMarkers::DrawBox::SelectedType);
    JC("c_esp_box", Cheats::Players::VisualMarkers::DrawBox::Color);

    // Line
    JB("esp_line",   Cheats::Players::VisualMarkers::DrawLine::Enabled);
    JI("esp_lineloc",Cheats::Players::VisualMarkers::DrawLine::SelectedLocation);
    JC("c_esp_line", Cheats::Players::VisualMarkers::DrawLine::Color);

    // BonePoints
    JB("esp_bone",  Cheats::Players::VisualMarkers::DrawBonePoints::Enabled);
    JI("esp_bone_r",Cheats::Players::VisualMarkers::DrawBonePoints::Radius);
    JC("c_esp_bone",Cheats::Players::VisualMarkers::DrawBonePoints::Color);

    // ESP settings
    JI("esp_dist",     Cheats::Players::Settings::MaxDistance);
    JB("esp_noped",    (bool)Cheats::Players::Settings::IgnorePed);
    JB("esp_nodead",   (bool)Cheats::Players::Settings::IgnoreDeath);
    JB("esp_offscreen",Cheats::Players::OffscreenESP::Enabled);

    // Player Info
    JI("pi_maxdist", Cheats::Players::PlayerInfo::GlobalSettings::MaxDistance);
    JB("pi_name",    Cheats::Players::PlayerInfo::DrawName::Enabled);
    JI("pi_name_loc",Cheats::Players::PlayerInfo::DrawName::SelectedLocation);
    JC("c_pi_name",  Cheats::Players::PlayerInfo::DrawName::Color);
    JB("pi_id",      Cheats::Players::PlayerInfo::DrawId::Enabled);
    JI("pi_id_loc",  Cheats::Players::PlayerInfo::DrawId::SelectedLocation);
    JC("c_pi_id",    Cheats::Players::PlayerInfo::DrawId::Color);
    JB("pi_dist",    Cheats::Players::PlayerInfo::DrawDistance::Enabled);
    JI("pi_dist_loc",Cheats::Players::PlayerInfo::DrawDistance::SelectedLocation);
    JC("c_pi_dist",  Cheats::Players::PlayerInfo::DrawDistance::Color);
    JB("pi_weapon",  Cheats::Players::PlayerInfo::DrawWeaponName::Enabled);
    JI("pi_wpn_loc", Cheats::Players::PlayerInfo::DrawWeaponName::SelectedLocation);
    JC("c_pi_weapon",Cheats::Players::PlayerInfo::DrawWeaponName::Color);

    // Status Bars
    JB("sb_hp",       Cheats::Players::StatusBars::DrawHealthBar::Enabled);
    JI("sb_hp_loc",   Cheats::Players::StatusBars::DrawHealthBar::SelectedLocation);
    JB("sb_armor",    Cheats::Players::StatusBars::DrawArmorBar::Enabled);
    JI("sb_armor_loc",Cheats::Players::StatusBars::DrawArmorBar::SelectedLocation);

    // Vehicles
    JB("veh_pt",      Cheats::Vehicles::DrawPoint::Enabled);
    JC("c_veh_pt",    Cheats::Vehicles::DrawPoint::Color);
    JI("veh_pt_size", Cheats::Vehicles::DrawPoint::Size);
    JB("veh_ln",      Cheats::Vehicles::DrawLine::Enabled);
    JC("c_veh_ln",    Cheats::Vehicles::DrawLine::Color);
    JI("veh_ln_loc",  Cheats::Vehicles::DrawLine::SelectedLocation);
    JB("veh_dist_en", Cheats::Vehicles::DrawDistance::Enabled);
    JC("c_veh_dist",  Cheats::Vehicles::DrawDistance::Color);
    JB("veh_hp",      Cheats::Vehicles::DrawHealthBar::Enabled);
    JB("veh_ignlocal",Cheats::Vehicles::Settings::IgnoreLocalVehicle);
    JI("veh_maxcount",Cheats::Vehicles::Settings::MaxVehicleCount);
    JI("veh_maxdist", Cheats::Vehicles::Settings::MaxDistance);
    JB("veh_fix",     Cheats::Vehicles::VehicleFix::Enabled);
    JI("veh_fix_hk",  Cheats::Vehicles::VehicleFix::HotKey);
    JB("veh_god",     Cheats::Vehicles::VehicleGodMode::Enabled);
    JB("veh_spd",     Cheats::Vehicles::SpeedBoost::Enabled);
    JI("veh_spd_kmh", Cheats::Vehicles::SpeedBoost::KmH);
    JB("veh_flip",    Cheats::Vehicles::VehicleFlip::Enabled);
    JI("veh_flip_hk", Cheats::Vehicles::VehicleFlip::HotKey);

    // World
    JB("w_noclip",       Cheats::World::NoClip::Enabled);
    JI("w_noclip_spd",   Cheats::World::NoClip::MovementSpeed);
    JB("w_semigod",      Cheats::World::SemiGodMode::Enabled);
    JB("w_sprint",       Cheats::World::SuperSprint::Enabled);
    JI("w_sprintspd",    Cheats::World::SuperSprint::Speed);
    JB("w_stamina",      Cheats::World::InfiniteStamina::Enabled);
    JB("w_explode",      Cheats::World::ExplosiveBullets::Enabled);
    JB("w_fire",         Cheats::World::FireBullets::Enabled);
    JB("w_healtoggle",   Cheats::World::HealToggle::Enabled);
    JI("w_healtogglekey",Cheats::World::HealToggle::HotKey);
    JB("w_armorfill",    Cheats::World::ArmorFill::Enabled);
    JI("w_armorfillkey", Cheats::World::ArmorFill::HotKey);
    JB("w_damageboost",  Cheats::World::DamageBoost::Enabled);
    JF("w_damagemul",    Cheats::World::DamageBoost::Multiplier);
    JB("w_lowdamage",    Cheats::World::LowDamage::Enabled);
    JF("w_lowdamagemul", Cheats::World::LowDamage::Multiplier);
    JB("w_respawn",      Cheats::World::Respawn::Enabled);
    JB("w_unlockcar",    Cheats::World::UnlockCar::Enabled);
    JI("w_unlockcar_mk", Cheats::World::UnlockCar::ModKey);
    JB("w_invisible",    Cheats::World::Invisible::Enabled);
    JI("w_invisible_hk", Cheats::World::Invisible::HotKey);
    JB("w_spectdet",     Cheats::SpectatorDetect::Enabled);
    JI("w_spectdet_thr", Cheats::SpectatorDetect::Threshold);
    JB("w_lowgrav",      Cheats::LowGravity::Enabled);
    JF("w_lowgrav_str",  Cheats::LowGravity::Strength);
    JB("w_moonjump",     Cheats::MoonJump::Enabled);
    JF("w_moonjump_f",   Cheats::MoonJump::Force);

    // Settings
    JB("s_stream",  Cheats::Settings::StreamProof);
    JI("s_theme",   Cheats::Settings::SelectedTheme);
    JI("s_maxplrs", Cheats::Settings::MaxPlayerCount);

    // Players array
    off+=snprintf(buf+off,lim-off,"\"players\":%s",plist);
    if(off<lim) buf[off++]='}';
    if(off<lim) buf[off]=0;
}
#undef JB
#undef JI
#undef JF
#undef JC

// ── SEH wrappers (must be outside functions that use C++ unwinding) ──────────
static void _SafeBuildState(char* buf, size_t sz) {
    __try { WsBuildState(buf, sz); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void _SafeApply(const char* buf) {
    __try { WsApply(buf); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ── WebSocket connection ──────────────────────────────────────────────────────
static bool _WsConnect() {
    if(!g_wsHostW[0]||!g_wsCheatKey[0]) return false;

    HINTERNET hSess=WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hSess) return true;

    HINTERNET hConn=WinHttpConnect(hSess,g_wsHostW,g_wsPort,0);
    if(!hConn){WinHttpCloseHandle(hSess);return true;}

    DWORD flags=WINHTTP_FLAG_SECURE;
    if(g_wsPort==80) flags=0;

    HINTERNET hReq=WinHttpOpenRequest(hConn,L"GET",L"/ws",nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
    if(!hReq){WinHttpCloseHandle(hConn);WinHttpCloseHandle(hSess);return true;}

#ifdef _DEBUG
    DWORD opt=SECURITY_FLAG_IGNORE_UNKNOWN_CA|SECURITY_FLAG_IGNORE_CERT_CN_INVALID|
              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(hReq,WINHTTP_OPTION_SECURITY_FLAGS,&opt,sizeof(opt));
#endif

    if(!WinHttpSetOption(hReq,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0)||
       !WinHttpSendRequest(hReq,WINHTTP_NO_ADDITIONAL_HEADERS,0,nullptr,0,0,0)||
       !WinHttpReceiveResponse(hReq,nullptr)){
        WinHttpCloseHandle(hReq);WinHttpCloseHandle(hConn);WinHttpCloseHandle(hSess);
        return true;
    }

    HINTERNET hWs=WinHttpWebSocketCompleteUpgrade(hReq,0);
    WinHttpCloseHandle(hReq);
    if(!hWs){WinHttpCloseHandle(hConn);WinHttpCloseHandle(hSess);return true;}

    // Identify
    {
        char ident[128];
        int n=snprintf(ident,sizeof(ident),
            "{\"type\":\"client\",\"cheatKey\":\"%s\"}",g_wsCheatKey);
        if(WinHttpWebSocketSend(hWs,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
               (PVOID)ident,(DWORD)n)!=ERROR_SUCCESS){
            WinHttpWebSocketClose(hWs,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);
            WinHttpCloseHandle(hWs);WinHttpCloseHandle(hConn);WinHttpCloseHandle(hSess);
            return true;
        }
    }

    // shared_ptr<atomic> — sender thread function'dan uzun yasayabilir
    auto alive = std::make_shared<std::atomic<bool>>(true);

    // State sender thread — every 100 ms
    std::thread([hWs, alive](){
        static char stBuf[65536];
        DWORD last=GetTickCount();
        while(alive->load(std::memory_order_relaxed)){
            DWORD now=GetTickCount();
            if(now-last>=100){
                stBuf[0]=0;
                _SafeBuildState(stBuf,sizeof(stBuf));
                if(stBuf[0]){
                    DWORD r=WinHttpWebSocketSend(hWs,
                        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                        (PVOID)stBuf,(DWORD)strlen(stBuf));
                    if(r!=ERROR_SUCCESS){alive->store(false,std::memory_order_relaxed);break;}
                }
                last=now;
            }
            Sleep(10);
        }
    }).detach();

    // Receive loop
    static char recvBuf[65536];
    DWORD recvLen=0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE btype;

    while(g_wsRunning && alive->load(std::memory_order_relaxed)){
        recvLen=0;
        DWORD r=WinHttpWebSocketReceive(hWs,recvBuf,sizeof(recvBuf)-1,&recvLen,&btype);
        if(r!=ERROR_SUCCESS) break;
        if(btype==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        if(btype==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE||
           btype==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE){
            recvBuf[recvLen]=0;
            _SafeApply(recvBuf);
        }
    }

    alive->store(false, std::memory_order_relaxed); // sender'a dur sinyali
    WinHttpWebSocketClose(hWs,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);
    WinHttpCloseHandle(hWs);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return true;
}

static void WsClientThread(){
    g_wsRunning=true;
    while(g_wsRunning){
        bool retry=_WsConnect();
        if(!retry) break;
        if(g_wsRunning) Sleep(5000);
    }
}
