#include <mutex>

extern bool g_gameConnected;
// Overlay display size - Overlay.hpp'de tanımlanır
extern OverlayIO g_io;

static std::mutex g_pedMutex;
static std::mutex g_vehMutex;
// ── Görünüm matrisi önbelleği (render thread → ESP lag azaltır) ──
static std::mutex g_vmMutex;
static Matrix     g_cachedVM = {};  // UpdatePeds thread'i günceller

// ── Aim key toggle state (Exploits thread'inde güncellenir, her yerden okunur) ──
static bool s_silentToggleOn = false;
static bool s_aimbotToggleOn = false;

// Hold veya Toggle moduna göre aim key aktif mi?
// key=0 → tuş atanmamış → her zaman aktif
static inline bool AimKeyActive(int key, bool toggleMode, bool toggleOn) {
    if (!key) return true;
    if (toggleMode) return toggleOn;
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}
static uintptr_t GameWorld          = 0;
static uintptr_t GameViewPort       = 0;
static uintptr_t GameReplayInterface= 0;
static Matrix    ViewMatrix         = {};
static Ped              LocalPlayer;
static Ped*             SilentPed  = nullptr;
static Ped*             AimbotPed  = nullptr;
static std::vector<Ped>     PedList;
static std::vector<Vehicle> VehicleList;

bool WorldToScreen(const Matrix& vm, const Vector3& w, Vector2& out) {
    Matrix v = vm.Transpose();
    Vector4 r2(v._21,v._22,v._23,v._24);
    Vector4 r3(v._31,v._32,v._33,v._34);
    Vector4 r4(v._41,v._42,v._43,v._44);
    Vector3 p;
    p.x = r2.x*w.x + r2.y*w.y + r2.z*w.z + r2.w;
    p.y = r3.x*w.x + r3.y*w.y + r3.z*w.z + r3.w;
    p.z = r4.x*w.x + r4.y*w.y + r4.z*w.z + r4.w;
    if (p.z <= 0.1f) return false;
    float iz = 1.f / p.z;  p.x *= iz; p.y *= iz;
    float sw = (float)Game.lpRect.right, sh = (float)Game.lpRect.bottom;
    out.x = sw*0.5f + 0.5f*p.x*sw;
    out.y = sh*0.5f - 0.5f*p.y*sh;
    return true;
}

bool IsTargetInCrosshair(const Vector2& sp) {
    float cx = (float)Game.lpRect.right  * 0.5f;
    float cy = (float)Game.lpRect.bottom * 0.5f;
    return fabsf(sp.x-cx) <= 10.f && fabsf(sp.y-cy) <= 10.f;
}


void UpdatePeds() {
    while (true) {
        if (!keepRunning || !g_gameConnected) { Sleep(50); continue; }
        std::vector<Ped> updated;
        GameWorld             = ReadMemory<uintptr_t>(Offsets.GameBase + Offsets.GameWorld);
        GameViewPort          = ReadMemory<uintptr_t>(Offsets.GameBase + Offsets.ViewPort);
        GameReplayInterface   = ReadMemory<uintptr_t>(Offsets.GameBase + Offsets.ReplayInterface);
        LocalPlayer.Pointer   = ReadMemory<uintptr_t>(GameWorld + Offsets.LocalPlayer);

        if (Game.hWnd) {
            RECT r = {};
            if (GetClientRect(Game.hWnd, &r) && r.right > 0)
                Game.lpRect = r;
            else if (!Game.lpRect.right) {
                Game.lpRect.right  = GetSystemMetrics(SM_CXSCREEN);
                Game.lpRect.bottom = GetSystemMetrics(SM_CYSCREEN);
            }
        }

        // Görünüm matrisini worker thread'de güncelle → DrawCheat'e gerek kalmaz
        {
            Matrix vm = ReadMemory<Matrix>(GameViewPort + 0x24C);
            std::lock_guard<std::mutex> lk(g_vmMutex);
            g_cachedVM = vm;
        }

        uintptr_t elp  = ReadMemory<uintptr_t>(GameReplayInterface + 0x18);
        uintptr_t el   = ReadMemory<uintptr_t>(elp + 0x100);
        if (!el) el    = ReadMemory<uintptr_t>(elp + 0x108);

        // Local player position — distance culling için
        Vector3 localPos = (LocalPlayer.Pointer) ?
            ReadMemory<Vector3>(LocalPlayer.Pointer + 0x90) : Vector3{};

        int maxPlayerCount = Cheats::Settings::MaxPlayerCount;
        updated.reserve(maxPlayerCount);
        for (int i = 0; i < maxPlayerCount; ++i) {
            Ped ped;
            uintptr_t player = ReadMemory<uintptr_t>(el + i*0x10);
            if (!player || player == LocalPlayer.Pointer) continue;
            if (!ped.GetPlayer(player))  continue;
            // Önce position oku → uzak ped'ler skip
            uintptr_t pInfo = ReadMemory<uintptr_t>(player + 0x10C8);
            Vector3 pPos = (pInfo) ? ReadMemory<Vector3>(pInfo + 0x90) : Vector3{};
            Vector3 diff = localPos - pPos;
            float dist2 = diff.x*diff.x + diff.y*diff.y + diff.z*diff.z;
            // 300m uzaklaştıkça frame'lerde yavaşlama → adaptive skip
            if (dist2 > 90000.f) continue;  // >300m²  >  ~300m uzak
            if (!ped.Update()) continue;
            ped.Distance = sqrtf(dist2);
            updated.push_back(ped);
        }
        { std::lock_guard<std::mutex> lk(g_pedMutex); PedList = std::move(updated); }

        // Adaptive Sleep — yoğun update'lerde daha sık, aksi halde slow
        // Önceki update süresine göre ayarla. Şimdilik 16ms → 60Hz, FPS artışı sağlandı.
        Sleep(isMenuVisible ? 8 : 16);
    }
}


void UpdateVehicles() {
    while (true) {
        if (!keepRunning || !g_gameConnected) { Sleep(50); continue; }
        if (Cheats::Vehicles::DrawPoint::Enabled ||
            Cheats::Vehicles::DrawLine::Enabled  ||
            Cheats::Vehicles::DrawDistance::Enabled ||
            Cheats::Vehicles::DrawHealthBar::Enabled)
        {
            std::vector<Vehicle> updated;
            uintptr_t vi  = ReadMemory<uintptr_t>(GameReplayInterface + 0x10);
            uintptr_t vl  = ReadMemory<uintptr_t>(vi + 0x180);
            int       cnt = ReadMemory<int>(vi + 0x188);
            if (cnt <= 0 || cnt > Cheats::Vehicles::Settings::MaxVehicleCount) { Sleep(10); continue; }

            Matrix vm = ReadMemory<Matrix>(GameViewPort + 0x24C);
            Vector3 localPos = ReadMemory<Vector3>(LocalPlayer.Pointer + 0x90);

            for (int i = 0; i < cnt; ++i) {
                uintptr_t vp = ReadMemory<uintptr_t>(vl + i*0x10);
                if (!vp) continue;
                Vector3 vpos = ReadMemory<Vector3>(vp + 0x90);
                Vector2 scr;
                if (!WorldToScreen(vm, vpos, scr)) continue;
                Vector3 diff = localPos - vpos;
                float dist = sqrtf(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
                if (dist > Cheats::Vehicles::Settings::MaxDistance) continue;
                if ((int)dist == 0 && Cheats::Vehicles::Settings::IgnoreLocalVehicle) continue;
                Vehicle veh;
                veh.Pointer = vp;
                veh.Location = scr;
                veh.Health = ReadMemory<float>(vp + Offsets.Health);
                veh.Distance = dist;
                updated.push_back(veh);
            }
            { std::lock_guard<std::mutex> lk(g_vehMutex); VehicleList = updated; }
        }
        Sleep(10);
    }
}


// ── RGB / HSV renk yardımcısı ─────────────────────────────────────────────
// h=0..1, s=0..1, v=0..1 → ImColor
static inline ImColor HsvToImColor(float h, float s, float v, float a = 1.f) {
    h = fmodf(h, 1.f); if (h < 0.f) h += 1.f;
    float hh = h * 6.f;
    int   i  = (int)hh;
    float f  = hh - (float)i;
    float p  = v * (1.f - s);
    float q  = v * (1.f - s * f);
    float t  = v * (1.f - s * (1.f - f));
    float r, g, b;
    switch (i % 6) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default:r=v; g=p; b=q; break;
    }
    return ImColor((int)(r*255),(int)(g*255),(int)(b*255),(int)(a*255));
}

// hueOffset: 0..1 — her oyuncu için farklı renk başlangıcı (golden ratio dağılımı)
static inline ImColor GetRgbColor(float hueOffset = 0.f) {
    float spd = Cheats::Players::RgbESP::Speed;
    float sat = Cheats::Players::RgbESP::Saturation;
    float val = Cheats::Players::RgbESP::Value;
    float h   = fmodf((float)(GetTickCount64() / 1000.0) * spd + hueOffset, 1.f);
    return HsvToImColor(h, sat, val);
}

// ── DrawTextElement — D3DRenderer ile (ImGui kaldırıldı) ────────
void DrawTextElement(const ImVec2& pos, const ImVec2& sz,
                     const std::string& text, bool enabled, int locType,
                     float locTop, float locBot, int /*font*/, int fontType, ImU32 col)
{
    if (!enabled || text.empty()) return;
    float tw = g_D3DR.TextWidth(text.c_str());
    float th = g_D3DR.TextHeight();
    ImVec2 tp;
    if (locType == 0) { tp.x = pos.x + (sz.x - tw)*0.5f; tp.y = pos.y - th - locTop; }
    else              { tp.x = pos.x + (sz.x - tw)*0.5f; tp.y = pos.y + sz.y + locBot; }

    // Arka plan kutusu kaldırıldı — TextShadow 4-yön siyah outline ile okunabilir
    D2D1_COLOR_F dc = D2DFromImU32(col);
    g_D3DR.TextShadow(tp.x, tp.y, dc, text.c_str(), fontType != 0);
}


void DrawCheat() {
    // Game pencere boyutunu güncelle
    if (Game.hWnd) {
        RECT r = {};
        if (GetClientRect(Game.hWnd, &r) && r.right > 0)
            Game.lpRect = r;
    }
    if (!Game.lpRect.right) {
        Game.lpRect.right  = GetSystemMetrics(SM_CXSCREEN);
        Game.lpRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    float sw = (float)Game.lpRect.right, sh = (float)Game.lpRect.bottom;
    ImVec2 center(sw*0.5f, sh*0.5f);

    
    // ── FOV çemberi (D3DRenderer ile) ───────────────────────────
    static double s_lastTime = 0.0;
    double nowT = FudGetTime();
    float  dt   = (float)(nowT - s_lastTime);
    if (dt > 0.1f) dt = 0.016f;
    s_lastTime = nowT;
    g_io.DeltaTime = dt;

    auto drawFovCircle = [&](bool draw, float radius, ImColor baseCol, float baseW,
                              bool hasTarget, float& lockProg) {
        if (!draw) return;
        lockProg = hasTarget ? ImMin(lockProg + dt * 4.f, 1.f)
                             : ImMax(lockProg - dt * 6.f, 0.f);

        // Renk: kilitleme ilerlemesiyle yeşile doğru geçiş
        D2D1_COLOR_F col = D2DFromImColor(baseCol);
        float lw = baseW;
        if (lockProg > 0.f) {
            float p = lockProg;
            col.r = col.r + (0.39f - col.r) * p;
            col.g = col.g + (0.87f - col.g) * p;
            col.b = col.b + (0.51f - col.b) * p;
            lw = baseW + lockProg * 1.5f;
        }
        g_D3DR.Circle(center.x, center.y, radius, col, lw);

        // Kilit tamamlandı → köşe tick işaretleri
        if (lockProg >= 1.f) {
            float pulse = (float)(sin(nowT * 8.0) * 0.5 + 0.5);
            float tickLen = 8.f + pulse * 4.f;
            D2D1_COLOR_F tc = D2DFromRGBA(99,220,130,(int)(200+55*pulse));
            for (int qi = 0; qi < 4; ++qi) {
                float ang = -IM_PI*0.5f + qi * IM_PI*0.5f;
                float tx = center.x + cosf(ang)*radius, ty = center.y + sinf(ang)*radius;
                float ex = center.x + cosf(ang)*(radius+tickLen), ey = center.y + sinf(ang)*(radius+tickLen);
                g_D3DR.Line(tx,ty,ex,ey,tc,2.f);
            }
        }
    };

    static float s_silentLock = 0.f, s_aimLock = 0.f;
    bool silentKeyHeld2 = AimKeyActive(Cheats::AimAssist::Silent::HotKey,
                                       Cheats::AimAssist::Silent::ToggleMode,
                                       s_silentToggleOn);
    bool aimbotKeyHeld2 = AimKeyActive(Cheats::AimAssist::Aimbot::HotKey,
                                       Cheats::AimAssist::Aimbot::ToggleMode,
                                       s_aimbotToggleOn);

    drawFovCircle(Cheats::AimAssist::Silent::DrawFov,
                  (float)Cheats::AimAssist::Silent::Fov,
                  Cheats::AimAssist::Silent::FovColor,
                  (float)Cheats::AimAssist::Silent::FovWeight,
                  SilentPed != nullptr && silentKeyHeld2,
                  s_silentLock);
    drawFovCircle(Cheats::AimAssist::Aimbot::DrawFov,
                  (float)Cheats::AimAssist::Aimbot::Fov,
                  Cheats::AimAssist::Aimbot::FovColor,
                  (float)Cheats::AimAssist::Aimbot::FovWeight,
                  AimbotPed != nullptr && aimbotKeyHeld2,
                  s_aimLock);

    // Önbellek'ten view matrix al (ReadMemory render thread'inde yok → FPS artışı)
    Matrix vm;
    { std::lock_guard<std::mutex> lk(g_vmMutex); vm = g_cachedVM; }
    if (!LocalPlayer.Update()) return;

    DemoUser.Id         = LocalPlayer.GetId();
    DemoUser.Name       = LocalPlayer.GetName();
    DemoUser.WeaponName = LocalPlayer.GetWeaponName();
    DemoUser.Health     = LocalPlayer.GetHealth();
    DemoUser.Armor      = LocalPlayer.GetArmor();

    // Offscreen ESP aktifken overlay tamamen kapanır — tarayıcıda /esp gösterilir
    if (Cheats::Players::OffscreenESP::Enabled) return;

    // ── Spectator uyarısı ────────────────────────────────────────
    {
        std::lock_guard<std::mutex> sl(g_spectatorMutex);
        if (!g_spectatorIds.empty()) {
            char specBuf[64];
            snprintf(specBuf, sizeof(specBuf), "! SPECTATOR (%d)", (int)g_spectatorIds.size());
            float tw = g_D3DR.TextWidth(specBuf);
            float tx = center.x - tw * 0.5f;
            float ty = 40.f;
            g_D3DR.RectFilled(tx - 6.f, ty - 3.f, tx + tw + 6.f, ty + g_D3DR.TextHeight() + 3.f,
                              D2D1::ColorF(0.8f, 0.1f, 0.1f, 0.75f));
            g_D3DR.TextShadow(tx, ty, D2DFromRGBA(255, 240, 80), specBuf);
        }
    }

    // ── WaveShield FOV uyarısı ───────────────────────────────────
    // Silent aim açıkken FOV > 45 olursa WaveShield bullet açısını
    // tespit edip ban atar. Kullanıcıyı uyar.
    if ((bool)Cheats::AimAssist::Silent::Enabled &&
        Cheats::AimAssist::Silent::Fov > 45)
    {
        const char* warnMsg = "! WAVESHIELD BAN RISKI — Silent FOV cok yuksek (>45)";
        float tw = g_D3DR.TextWidth(warnMsg);
        float tx = center.x - tw * 0.5f;
        float ty = 65.f;   // spectator uyarısının hemen altı
        g_D3DR.RectFilled(tx - 6.f, ty - 3.f, tx + tw + 6.f, ty + g_D3DR.TextHeight() + 3.f,
                          D2D1::ColorF(0.85f, 0.35f, 0.0f, 0.80f));
        g_D3DR.TextShadow(tx, ty, D2DFromRGBA(255, 255, 80), warnMsg);
    }

    std::vector<Ped> localPeds;
    { std::lock_guard<std::mutex> lk(g_pedMutex); localPeds = PedList; }

    // ── Tag extract helper — ismin başındaki ilk kelimeyi tag olarak alır ──
    // Desteklenen formatlar: [WINS], <WINS>, |WINS|, WINS., .WINS, ABC vb.
    // Kural: ilk boşluktan önceki kısım tag — boşluk yoksa (tek kelime isim) tag yok.
    auto extractTag = [](const std::string& name) -> std::string {
        if (name.empty()) return {};
        size_t sp = name.find(' ');
        if (sp == std::string::npos || sp == 0) return {};
        return name.substr(0, sp);
    };

    // ── Tag Count: loop'tan önce tüm oyuncuların tag'lerini say ──
    std::unordered_map<std::string, int> tagCounts;
    if (Cheats::Players::TagCount::Enabled) {
        for (auto& p : localPeds)
            if (!IsFriend(p.GetId())) {
                std::string t = extractTag(p.GetName());
                if (!t.empty()) tagCounts[t]++;
            }
    }

    int g_espPedIdx = 0;
    for (auto& ped : localPeds) {
        int curPedIdx = g_espPedIdx++;   // RGB hue offset için

        if (!ped.Pointer || !ped.PlayerInfo) continue;
        // ESP lag fix: uzak oyuncular skip (> 600m da görünür ama cull 300m+)
        if (ped.Distance > 0.f && ped.Distance > Cheats::Settings::MaxESPDistance) continue;

        Vector2 pBase{}, pHead{}, pNeck{}, pLFoot{}, pRFoot{};
        if (!WorldToScreen(vm, ped.Position,       pBase) ||
            !WorldToScreen(vm, ped.BoneList[Head], pHead) ||
            !WorldToScreen(vm, ped.BoneList[Neck], pNeck)) continue;

        // Ayak kemikleri araçta veya yerde yatarken başarısız olabilir → tahmini kullan
        if (!WorldToScreen(vm, ped.BoneList[LeftFoot],  pLFoot))
            pLFoot = { pBase.x, pNeck.y + fabsf(pNeck.y - pHead.y) * 4.5f };
        if (!WorldToScreen(vm, ped.BoneList[RightFoot], pRFoot))
            pRFoot = { pBase.x, pNeck.y + fabsf(pNeck.y - pHead.y) * 4.5f };

        float h2n   = pNeck.y - pHead.y;
        float pTop  = pHead.y - h2n*2.5f;
        float pBot  = (pLFoot.y > pRFoot.y ? pLFoot.y : pRFoot.y)*1.001f;
        float pH    = pBot - pTop;
        float pW    = pH / 3.5f;
        float pDist = GetDistance(ped.Position, LocalPlayer.Position);

        
        if (Cheats::AimAssist::Settings::Crosshair) {
            ImU32 ccU = Cheats::AimAssist::Settings::CrosshairColor;
            int   cz = Cheats::AimAssist::Settings::CrosshairSize;
            int   ct = Cheats::AimAssist::Settings::CrosshairSelectedType;
            float th = 1.5f, h = (float)cz*0.5f;
            if (Cheats::AimAssist::Settings::DynamicCrosshairColor &&
                (IsTargetInCrosshair(pBase)||IsTargetInCrosshair(pHead)||
                 IsTargetInCrosshair(pNeck)||IsTargetInCrosshair(pLFoot)||IsTargetInCrosshair(pRFoot)))
                ccU = IM_COL32(255,0,0,255);
            D2D1_COLOR_F cc = D2DFromImU32(ccU);
            D2D1_COLOR_F red = D2DFromRGBA(255,0,0);
            float cx=center.x, cy=center.y;
            if      (ct==0) { g_D3DR.Line(cx-h,cy,cx+h,cy,cc,th); g_D3DR.Line(cx,cy-h,cx,cy+h,cc,th); }
            else if (ct==1) { float g2=(float)cz*0.6f; g_D3DR.Line(cx-h-g2,cy,cx-h,cy,cc,th); g_D3DR.Line(cx+h,cy,cx+h+g2,cy,cc,th); g_D3DR.Line(cx,cy-h-g2,cx,cy-h,cc,th); g_D3DR.Line(cx,cy+h,cx,cy+h+g2,cc,th); }
            else if (ct==2) { g_D3DR.Circle(cx,cy,(float)cz*0.75f,cc,th); }
            else if (ct==3) { g_D3DR.Rect(cx-h,cy-h,cx+h,cy+h,cc,th); }
            else if (ct==4) { /* triangle — approximate with 3 lines */
                g_D3DR.Line(cx,cy-(float)cz, cx-(float)cz*0.8f,cy+(float)cz*0.8f,cc,th);
                g_D3DR.Line(cx,cy-(float)cz, cx+(float)cz*0.8f,cy+(float)cz*0.8f,cc,th);
                g_D3DR.Line(cx-(float)cz*0.8f,cy+(float)cz*0.8f, cx+(float)cz*0.8f,cy+(float)cz*0.8f,cc,th); }
            else if (ct==5) { g_D3DR.Line(cx-(float)cz,cy,cx+(float)cz,cy,cc,th); g_D3DR.Line(cx,cy-(float)cz,cx,cy+(float)cz,cc,th); }
            else if (ct==6) { g_D3DR.CircleFilled(cx,cy,(float)cz*0.25f,cc); }
            else if (ct==7) { g_D3DR.Circle(cx,cy,(float)cz,cc,th); g_D3DR.CircleFilled(cx,cy,(float)cz*0.25f,red); }
        }

        // Target markers
        bool silentActive = SilentPed &&
            AimKeyActive(Cheats::AimAssist::Silent::HotKey,
                         Cheats::AimAssist::Silent::ToggleMode, s_silentToggleOn);
        if (silentActive && Cheats::AimAssist::Silent::DrawTarget && ped.GetId()==SilentPed->GetId())
        {
            int t=Cheats::AimAssist::Silent::SelectedDrawTargetType;
            ImU32 tc=Cheats::AimAssist::Silent::DrawTargetColor;
            if      (t==0) g_D3DR.CircleFilled(pBase.x,(pTop+pBot)*0.5f,5.f,D2DFromImU32(tc));
            else if (t==1) g_D3DR.Rect(pBase.x-pW*0.75f,pTop,pBase.x+pW*0.75f,pBot,D2DFromImU32(tc),2.f);
            else           g_D3DR.TextShadow(pBase.x,pTop,D2DFromImU32(tc),"Silent");
        }
        bool aimbotActive = AimbotPed &&
            AimKeyActive(Cheats::AimAssist::Aimbot::HotKey,
                         Cheats::AimAssist::Aimbot::ToggleMode, s_aimbotToggleOn);
        if (aimbotActive && Cheats::AimAssist::Aimbot::DrawTarget && ped.GetId()==AimbotPed->GetId())
        {
            int t=Cheats::AimAssist::Aimbot::SelectedDrawTargetType;
            ImU32 tc=Cheats::AimAssist::Aimbot::DrawTargetColor;
            if      (t==0) g_D3DR.CircleFilled(pBase.x,(pTop+pBot)*0.5f,5.f,D2DFromImU32(tc));
            else if (t==1) g_D3DR.Rect(pBase.x-pW*0.75f,pTop,pBase.x+pW*0.75f,pBot,D2DFromImU32(tc),2.f);
            else           g_D3DR.TextShadow(pBase.x,pTop,D2DFromImU32(tc),"Aimbot");
        }

        if (pDist > Cheats::Players::Settings::MaxDistance) continue;
        if (Cheats::Players::Settings::IgnorePed       && !ped.IsPlayer())  continue;
        if (Cheats::Players::Settings::IgnoreDeath     && ped.IsDead())     continue;
        if (Cheats::Players::Settings::IgnoreInvisible && !ped.IsVisible()) continue;
        // Arkadaş kontrolü: FriendESP açıksa farklı renkte çiz, kapalıysa tamamen atla
        bool isFriend = IsFriend(ped.GetId());
        if (isFriend && !Cheats::Players::FriendESP::Enabled) continue;

        bool pedVis = ped.IsVisible();
        // RGB modunda her oyuncu 0.618... (altın oran) ile ötelenmiş farklı tonda
        const float kGolden = 0.6180339887f;
        auto resolveColor = [&](ImColor normalCol) -> ImColor {
            // RGB ESP — arkadaş ve VisCheck önceliği korunur
            if (Cheats::Players::RgbESP::Enabled && !isFriend) {
                ImColor rgb = GetRgbColor(curPedIdx * kGolden);
                // Gizlenmiş oyuncu → RGB'yi karartılmış ver
                if (!pedVis) {
                    auto& v = rgb.Value;
                    return ImColor(v.x*0.4f, v.y*0.4f, v.z*0.4f, v.w);
                }
                return rgb;
            }
            // Arkadaşlar → her zaman FriendESP rengi (VisCheck'ten bağımsız)
            if (isFriend) return Cheats::Players::FriendESP::Color;
            if (Cheats::Players::VisCheck::Enabled)
                return pedVis ? ImColor(Cheats::Players::VisCheck::VisibleColor)
                              : ImColor(Cheats::Players::VisCheck::HiddenColor);
            if (!pedVis) return ImColor(normalCol.Value.x*0.3f,normalCol.Value.y*0.3f,normalCol.Value.z*0.3f,normalCol.Value.w);
            return normalCol;
        };

        // Skeleton — sadece ince çizgiler, yuvarlak nokta/eklem yok
        if (Cheats::Players::VisualMarkers::DrawSkeleton::Enabled) {
            ImColor sc = resolveColor(Cheats::Players::VisualMarkers::DrawSkeleton::Color);
            ImU32  sc32 = (ImU32)sc;
            // İnce skeleton: LineWeight ayarını 0.55x ile scale et, minimum 1px
            float lw    = ImMax(1.0f, (float)Cheats::Players::VisualMarkers::GlobalSettings::LineWeight * 0.55f);
            bool outline = Cheats::Players::VisualMarkers::GlobalSettings::SelectedLineType == 1;
            ImU32 shadow = IM_COL32(0,0,0,180);

            // 9 kemiği bir kez project et
            Vector2 bS[9] = {};
            bool    bOk[9] = {};
            for (int bi = 0; bi < 9; ++bi)
                if (!Vec3Empty(ped.BoneList[bi]))
                    bOk[bi] = WorldToScreen(vm, ped.BoneList[bi], bS[bi]);

            // Çizgi fonksiyonu — yuvarlak nokta/eklem YOK
            auto drawLine = [&](Vector2 a, Vector2 b) {
                if (outline) g_D3DR.Line(a.x, a.y, b.x, b.y, D2DFromImU32(shadow), lw + 1.5f);
                g_D3DR.Line(a.x, a.y, b.x, b.y, D2DFromImU32(sc32), lw);
            };

            // Omurga: boyun → kalça
            if (bOk[Neck] && bOk[Hip]) drawLine(bS[Neck], bS[Hip]);

            // Kollar: boyun → omuz (%35 interpolasyon) → el
            if (bOk[Neck] && bOk[LeftHand]) {
                Vector2 lShoulder = {
                    bS[Neck].x + (bS[LeftHand].x  - bS[Neck].x) * 0.35f,
                    bS[Neck].y + (bS[LeftHand].y  - bS[Neck].y) * 0.35f
                };
                drawLine(bS[Neck], lShoulder);
                drawLine(lShoulder, bS[LeftHand]);
            }
            if (bOk[Neck] && bOk[RightHand]) {
                Vector2 rShoulder = {
                    bS[Neck].x + (bS[RightHand].x - bS[Neck].x) * 0.35f,
                    bS[Neck].y + (bS[RightHand].y - bS[Neck].y) * 0.35f
                };
                drawLine(bS[Neck], rShoulder);
                drawLine(rShoulder, bS[RightHand]);
            }

            // Bacaklar: kalça → ayak bileği → ayak
            if (bOk[Hip]       && bOk[LeftAnkle])  drawLine(bS[Hip],       bS[LeftAnkle]);
            if (bOk[LeftAnkle] && bOk[LeftFoot])   drawLine(bS[LeftAnkle], bS[LeftFoot]);
            else if (bOk[Hip]  && bOk[LeftFoot])   drawLine(bS[Hip],       bS[LeftFoot]);

            if (bOk[Hip]        && bOk[RightAnkle]) drawLine(bS[Hip],        bS[RightAnkle]);
            if (bOk[RightAnkle] && bOk[RightFoot])  drawLine(bS[RightAnkle], bS[RightFoot]);
            else if (bOk[Hip]   && bOk[RightFoot])  drawLine(bS[Hip],        bS[RightFoot]);
        }

        // Bone Points
        if (Cheats::Players::VisualMarkers::DrawBonePoints::Enabled) {
            ImColor bpc = resolveColor(Cheats::Players::VisualMarkers::DrawBonePoints::Color);
            float   bpr = (float)Cheats::Players::VisualMarkers::DrawBonePoints::Radius;
            for (int bi = 0; bi < 9; ++bi) {
                if (Vec3Empty(ped.BoneList[bi])) continue;
                Vector2 bs;
                if (WorldToScreen(vm, ped.BoneList[bi], bs))
                    g_D3DR.CircleFilled(bs.x, bs.y, bpr, D2DFromImColor(bpc));
            }
        }

        // Box
        if (Cheats::Players::VisualMarkers::DrawBox::Enabled) {
            ImColor bc = resolveColor(Cheats::Players::VisualMarkers::DrawBox::Color);
            float nw=pW*1.5f, lw=(float)Cheats::Players::VisualMarkers::GlobalSettings::LineWeight;
            ImVec2 tl(pBase.x-nw*0.5f,pTop), br(pBase.x+nw*0.5f,pBot);
            int bt=Cheats::Players::VisualMarkers::DrawBox::SelectedType;
            ImU32 bcU=(ImU32)bc;
            if (bt==0) {
                float cs=nw*0.3f;
                D2D1_COLOR_F bcc=D2DFromImU32(bcU);
                g_D3DR.Line(tl.x,tl.y,tl.x+cs,tl.y,bcc,2.f); g_D3DR.Line(tl.x,tl.y,tl.x,tl.y+cs,bcc,2.f);
                g_D3DR.Line(br.x,tl.y,br.x-cs,tl.y,bcc,2.f); g_D3DR.Line(br.x,tl.y,br.x,tl.y+cs,bcc,2.f);
                g_D3DR.Line(tl.x,br.y,tl.x+cs,br.y,bcc,2.f); g_D3DR.Line(tl.x,br.y,tl.x,br.y-cs,bcc,2.f);
                g_D3DR.Line(br.x,br.y,br.x-cs,br.y,bcc,2.f); g_D3DR.Line(br.x,br.y,br.x,br.y-cs,bcc,2.f);
            } else {
                g_D3DR.Rect(tl.x,tl.y,br.x,br.y,D2DFromImU32(bcU),lw);
            }
        }

        // Snapline — ince çizgi (LineWeight 0.5x scale)
        if (Cheats::Players::VisualMarkers::DrawLine::Enabled) {
            ImColor lc = resolveColor(Cheats::Players::VisualMarkers::DrawLine::Color);
            int loc=Cheats::Players::VisualMarkers::DrawLine::SelectedLocation;
            float sx=sw*0.5f, sy=(loc==0)?0.f:(loc==1)?sh*0.5f:sh;
            float tx=pBase.x, ty=(pTop+pBot)*0.5f;
            float lw=ImMax(1.0f,(float)Cheats::Players::VisualMarkers::GlobalSettings::LineWeight*0.5f);
            ImU32 lcU=(ImU32)lc;
            if (Cheats::Players::VisualMarkers::GlobalSettings::SelectedLineType==1)
                g_D3DR.Line(sx,sy,tx,ty,D2DFromRGBA(0,0,0,200),lw+1.f);
            g_D3DR.Line(sx,sy,tx,ty,D2DFromImU32(lcU),lw);
        }

        // Player info text
        if (pDist <= Cheats::Players::PlayerInfo::GlobalSettings::MaxDistance) {
            float bh=pBot-pTop, bw2=bh/3.5f;
            ImVec2 pos2(pBase.x-bw2*0.5f,pTop), sz2(bw2,bh);
            int fn=Cheats::Players::PlayerInfo::GlobalSettings::SelectedFont;
            int ft=Cheats::Players::PlayerInfo::GlobalSettings::SelectedFontType;
            float yT=7.f,yB=7.f,step=15.f;
            auto addInfo=[&](bool en,int loc,ImColor col,const std::string& txt){
                if(!en||txt.empty())return;
                if(loc==0){DrawTextElement(pos2,sz2,txt,true,0,yT,0.f,fn,ft,col);yT+=step;}
                else      {DrawTextElement(pos2,sz2,txt,true,1,0.f,yB,fn,ft,col);yB+=step;}
            };
            addInfo(Cheats::Players::PlayerInfo::DrawName::Enabled,
                    Cheats::Players::PlayerInfo::DrawName::SelectedLocation,
                    Cheats::Players::PlayerInfo::DrawName::Color, ped.GetName());
            addInfo(Cheats::Players::PlayerInfo::DrawId::Enabled,
                    Cheats::Players::PlayerInfo::DrawId::SelectedLocation,
                    Cheats::Players::PlayerInfo::DrawId::Color, std::to_string(ped.GetId()));
            addInfo(Cheats::Players::PlayerInfo::DrawWeaponName::Enabled,
                    Cheats::Players::PlayerInfo::DrawWeaponName::SelectedLocation,
                    Cheats::Players::PlayerInfo::DrawWeaponName::Color, ped.GetWeaponName());
            addInfo(Cheats::Players::PlayerInfo::DrawDistance::Enabled,
                    Cheats::Players::PlayerInfo::DrawDistance::SelectedLocation,
                    Cheats::Players::PlayerInfo::DrawDistance::Color,
                    "["+std::to_string((int)pDist)+"m]");

            // Tag Count ESP — aynı clan tag'ini taşıyan oyuncu sayısını üstte göster
            // Örn: "[WINS]" tag'i taşıyan 14 kişi varsa hepsinin üstünde "14" yazar
            if (Cheats::Players::TagCount::Enabled && !isFriend) {
                std::string tag = extractTag(ped.GetName());
                if (!tag.empty()) {
                    auto it = tagCounts.find(tag);
                    if (it != tagCounts.end()) {
                        char cnt_buf[16];
                        snprintf(cnt_buf, sizeof(cnt_buf), "%d", it->second);
                        // Üste stack et (yT zaten doğru konumda)
                        addInfo(true, 0, Cheats::Players::TagCount::Color, std::string(cnt_buf));
                    }
                }
            }
        }

        // Health & Armor bars — 0=Left 1=Right (dikey) | 2=Up 3=Down (yatay)
        // Aynı yatay konumda ikisi de seçilmişse üst üste binmesin, sıralanır.
        {
            bool hEn  = Cheats::Players::StatusBars::DrawHealthBar::Enabled;
            bool aEn  = Cheats::Players::StatusBars::DrawArmorBar::Enabled;
            int  hLoc = Cheats::Players::StatusBars::DrawHealthBar::SelectedLocation;
            int  aLoc = Cheats::Players::StatusBars::DrawArmorBar::SelectedLocation;

            float nw  = pW * 1.5f;
            float bH  = pBot - pTop + 2.f;
            float bW  = ImMax(2.f, 4.f - std::min(pDist / 100.f, 3.f));  // dikey bar genişliği

            // Yatay bar boyutları
            float hbH = ImMax(2.f, 4.f - std::min(pDist / 100.f, 2.f));  // ince yükseklik
            float gap  = 2.f;                                               // kutuya boşluk
            float stkG = 2.f;                                               // barlar arası boşluk

            // Yığılma (stacking): ikisi de aynı yatay konumdaysa armor bir adım kaydırılır
            // health=sıfır offset, armor=kaydırılmış
            float aStackOff = 0.f;
            if (hEn && aEn && hLoc == aLoc && (hLoc == 2 || hLoc == 3)) {
                // Up → armor daha yukarı; Down → armor daha aşağı
                aStackOff = (hLoc == 2) ? -(hbH + stkG) : (hbH + stkG);
            }

            // Sağlık rengi (yeşil→sarı→kırmızı)
            float hR = ped.GetHealth() / (float)Cheats::Players::StatusBars::GlobalSettings::MaxHealth;
            if (hR < 0.f) hR = 0.f; if (hR > 1.f) hR = 1.f;
            int hr_r = 255, hr_g = 0;
            if      (hR >= 0.8f) { hr_r = (int)(255*(1.f-(hR-0.8f)*5)); hr_g = 255; }
            else if (hR >= 0.6f) { hr_r = 128; hr_g = 255; }
            else if (hR >= 0.4f) { hr_g = 192; }
            else if (hR >= 0.2f) { hr_g = 100; }

            float aR = ped.GetArmor() / (float)Cheats::Players::StatusBars::GlobalSettings::MaxArmor;
            if (aR < 0.f) aR = 0.f; if (aR > 1.f) aR = 1.f;

            // Bar çizim lambdası
            auto drawBar = [&](int loc, float ratio,
                               int fr, int fg, int fb,   // dolgu rengi
                               float stackOff)
            {
                float bx = pBase.x - nw * 0.5f;

                if (loc == 0 || loc == 1) {
                    // Dikey bar (sol/sağ)
                    float tx = (loc == 0) ? (bx - bW - 3.f) : (bx + nw + 3.f);
                    g_D3DR.RectFilled(tx, pTop, tx+bW, pTop+bH,        D2DFromRGBA(40,40,40,160));
                    g_D3DR.RectFilled(tx, pTop+bH-bH*ratio, tx+bW, pTop+bH, D2DFromRGBA(fr,fg,fb,255));
                } else {
                    // Yatay bar (üst/alt)
                    float ty = (loc == 2) ? (pTop - hbH - gap + stackOff)
                                          : (pBot + gap + stackOff);
                    g_D3DR.RectFilled(bx, ty, bx+nw, ty+hbH,            D2DFromRGBA(40,40,40,160));
                    g_D3DR.RectFilled(bx, ty, bx+nw*ratio, ty+hbH,      D2DFromRGBA(fr,fg,fb,255));
                }
            };

            if (hEn) drawBar(hLoc, hR, hr_r, hr_g, 0, 0.f);
            if (aEn) drawBar(aLoc, aR, 0, (int)(aR*100), (int)(aR*255), aStackOff);
        }
    }

    // Vehicle ESP
    if (Cheats::Vehicles::DrawPoint::Enabled || Cheats::Vehicles::DrawLine::Enabled ||
        Cheats::Vehicles::DrawDistance::Enabled || Cheats::Vehicles::DrawHealthBar::Enabled)
    {
        std::vector<Vehicle> localVehs;
        { std::lock_guard<std::mutex> lk(g_vehMutex); localVehs = VehicleList; }
        for (auto& v : localVehs) {
            ImVec2 vp2(v.Location.x, v.Location.y);
            if (Cheats::Vehicles::DrawLine::Enabled) {
                int loc=Cheats::Vehicles::DrawLine::SelectedLocation;
                float sx=sw*0.5f, sy=(loc==0)?0.f:(loc==1)?sh*0.5f:sh;
                ImU32 vlcU=(ImU32)Cheats::Vehicles::DrawLine::Color;
                if (Cheats::Vehicles::DrawLine::SelectedLineType==1)
                    g_D3DR.Line(sx,sy,vp2.x,vp2.y,D2DFromRGBA(0,0,0,255),3.f);
                g_D3DR.Line(sx,sy,vp2.x,vp2.y,D2DFromImU32(vlcU),1.5f);
            }
            if (Cheats::Vehicles::DrawPoint::Enabled) {
                float psz=(float)Cheats::Vehicles::DrawPoint::Size;
                ImU32 vpcU=(ImU32)Cheats::Vehicles::DrawPoint::Color;
                g_D3DR.CircleFilled(vp2.x,vp2.y,psz,D2DFromImU32(vpcU));
                if (Cheats::Vehicles::DrawPoint::SelectedLineType==1)
                    g_D3DR.Circle(vp2.x,vp2.y,psz+1.f,D2DFromRGBA(0,0,0,255),1.5f);
            }
            if (Cheats::Vehicles::DrawDistance::Enabled) {
                char dtxt[32]; snprintf(dtxt,sizeof(dtxt),"%.0fm",v.Distance);
                ImU32 vdcU=(ImU32)Cheats::Vehicles::DrawDistance::Color;
                if (Cheats::Vehicles::DrawDistance::SelectedFontType==0)
                    g_D3DR.Text(vp2.x-10.f,vp2.y-30.f,D2DFromImU32(vdcU),dtxt);
                else
                    g_D3DR.TextShadow(vp2.x-10.f,vp2.y-30.f,D2DFromImU32(vdcU),dtxt);
            }
            if (Cheats::Vehicles::DrawHealthBar::Enabled) {
                float hR=v.Health/1000.f; int r=255,g=0;
                if(hR>=0.8f){r=(int)(255*(1.f-(hR-0.8f)*5));g=255;}
                else if(hR>=0.6f){r=128;g=255;}else if(hR>=0.4f){g=192;}
                float bpx=vp2.x-25.f, bpy=vp2.y-20.f;
                g_D3DR.RectFilled(bpx,bpy,bpx+50.f,bpy+5.f,D2DFromRGBA(0,0,0,255));
                g_D3DR.RectFilled(bpx,bpy,bpx+50.f*hR,bpy+5.f,D2DFromRGBA(r,g,0,255));
            }
        }
    }

    // ── Watermark (sol üst köşe) — tek satır, ince ──────────────
    if (g_showWatermark) {
        const std::string& tag = License::State::UserTag;
        char line[64];
        snprintf(line, sizeof(line), "Moon  |  %s",
                 tag.empty() ? "User" : tag.c_str());

        float wx  = 10.f, wy = 8.f;
        float bw  = g_D3DR.TextWidth(line, true) + 16.f;
        float bh  = 18.f;   // ince yükseklik

        // Arka plan
        g_D3DR.RectFilled(wx - 6.f, wy - 3.f, wx + bw, wy + bh,
                          D2D1::ColorF(0.06f, 0.06f, 0.08f, 1.0f));
        // Sol yeşil şerit
        g_D3DR.RectFilled(wx - 6.f, wy - 3.f, wx - 2.f, wy + bh,
                          D2D1::ColorF(0.15f, 0.76f, 0.43f, 1.0f));

        g_D3DR.TextShadow(wx, wy, D2D1::ColorF(0xFFFFFF), line, true);
    }
}

// ============================================================
//  GetTargetBone — maps BoneMode to actual bone index
// ============================================================
int GetTargetBone(int boneMode) {
    switch (boneMode) {
        case 0: return Head;
        case 1: return Hip;                            // Torso ≈ hip/center mass
        case 2: return (rand() & 1) ? LeftFoot : RightFoot;  // Legs
        case 3: { static const int k[]={Head,Neck,Hip,LeftHand,RightHand,LeftFoot,RightFoot};
                  return k[rand() % 7]; }              // Random
        default: return Head;
    }
}

// ============================================================
//  FindTarget
// ============================================================
Ped FindTarget(int fov, int dist, int boneMode, int priority = 0,
               bool visCheck = false, bool skipFriends = true) {
    int area = GetTargetBone(boneMode);
    Ped best; float bestVal = 9999.f;
    Matrix vm;
    { std::lock_guard<std::mutex> lk(g_vmMutex); vm = g_cachedVM; }
    Vector2 center((float)Game.lpRect.right*0.5f,(float)Game.lpRect.bottom*0.5f);
    std::vector<Ped> snapPeds;
    { std::lock_guard<std::mutex> lk(g_pedMutex); snapPeds = PedList; }
    int pedIdx = 0;
    for (auto& ped : snapPeds) {
        if (!LocalPlayer.Update()) break;
        if (!ped.Update()) continue;
        float pd = GetDistance(ped.Position, LocalPlayer.Position);
        if (pd > dist) continue;
        if (Cheats::AimAssist::Settings::IgnorePed       && !ped.IsPlayer())  continue;
        if (Cheats::AimAssist::Settings::IgnoreDeath     && ped.IsDead())     continue;
        if (Cheats::AimAssist::Settings::IgnoreInvisible && !ped.IsVisible()) continue;
        if (visCheck && !ped.IsVisible()) continue;
        if (skipFriends && IsFriend(ped.GetId())) continue;
        Vector2 sp;
        if (!WorldToScreen(vm, ped.BoneList[area], sp)) continue;
        float cf = fabsf((center - sp).Length());
        if (cf > fov) continue;
        float val = (priority == 1) ? pd
                  : (priority == 2) ? ped.GetHealth()
                  : cf;
        if (val < bestVal) { best = ped; bestVal = val; }
    }
    return best;
}

Vector3 CalcAngle(Vector3 cam, Vector3 target) {
    float d = GetDistance(cam, target);
    if (d < 0.001f) return {};
    return { (target.x-cam.x)/d, (target.y-cam.y)/d, (target.z-cam.z)/d };
}

void NormalizeAngles(Vector3& a) {
    while (a.x >  89.f) a.x -= 180.f;
    while (a.x < -89.f) a.x += 180.f;
    while (a.y >  180.f) a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
}



Vector3 EndBulletPos;


// Reconnect'te WS hook sıfırla — forward decl, tanım aşağıda
void ResetCave();

// ── ApplySilent / RestoreSilent — MYNX birebir port ──────────────────────────
// Cave: moduleBase + 0x34E  (PE header gap, her build'de güvenli boş alan)
// Weapon bypass cave: moduleBase + 0x600
// Her çağrıda JMP + tam 33-byte cave yeniden yazılır (MYNX davranışı).
// Cam spoof YOK → kamera kilitlenmez, 3fe_aimredline tetiklenmez.

void ApplySilent() {
    if (!Offsets.HandleBullet || !Game.hProcess) return;
    uint64_t handleBulletAddr = Offsets.GameBase + Offsets.HandleBullet;
    uint64_t allocPtr         = Offsets.GameBase + 0x34E; // MYNX: moduleBase + 0x34E

    union { float f; uint32_t i; } X, Y, Z;
    X.f = EndBulletPos.x;
    Y.f = EndBulletPos.y;
    Z.f = EndBulletPos.z;

    // JMP HandleBullet → cave  (rel32)
    int32_t jmpOffset = (int32_t)(allocPtr - (handleBulletAddr + 5));
    uint8_t jmpBytes[5] = {
        0xE9,
        (uint8_t)(jmpOffset & 0xFF),       (uint8_t)((jmpOffset >> 8)  & 0xFF),
        (uint8_t)((jmpOffset >> 16) & 0xFF),(uint8_t)((jmpOffset >> 24) & 0xFF)
    };
    WriteBytes(handleBulletAddr, jmpBytes, 5);

    // 33-byte cave:
    //   MOV [R9],   X  (7 bytes)
    //   MOV [R9+4], Y  (8 bytes)
    //   MOV [R9+8], Z  (8 bytes)
    //   MOVSS XMM3,[R9](5 bytes)
    //   JMP back       (5 bytes)
    int32_t jmpBackOffset = (int32_t)((handleBulletAddr + 5) - (allocPtr + 33));
    uint8_t caveBytes[33] = {
        0x41, 0xC7, 0x01,
        (uint8_t)(X.i & 0xFF), (uint8_t)((X.i >> 8) & 0xFF),
        (uint8_t)((X.i >> 16) & 0xFF), (uint8_t)((X.i >> 24) & 0xFF),
        0x41, 0xC7, 0x41, 0x04,
        (uint8_t)(Y.i & 0xFF), (uint8_t)((Y.i >> 8) & 0xFF),
        (uint8_t)((Y.i >> 16) & 0xFF), (uint8_t)((Y.i >> 24) & 0xFF),
        0x41, 0xC7, 0x41, 0x08,
        (uint8_t)(Z.i & 0xFF), (uint8_t)((Z.i >> 8) & 0xFF),
        (uint8_t)((Z.i >> 16) & 0xFF), (uint8_t)((Z.i >> 24) & 0xFF),
        0xF3, 0x41, 0x0F, 0x10, 0x19,
        0xE9,
        (uint8_t)(jmpBackOffset & 0xFF),       (uint8_t)((jmpBackOffset >> 8)  & 0xFF),
        (uint8_t)((jmpBackOffset >> 16) & 0xFF),(uint8_t)((jmpBackOffset >> 24) & 0xFF)
    };
    WriteBytes(allocPtr, caveBytes, 33);
}

void RestoreSilent() {
    if (!Offsets.HandleBullet || !Game.hProcess) return;
    uint64_t handleBulletAddr = Offsets.GameBase + Offsets.HandleBullet;
    uint64_t allocPtr         = Offsets.GameBase + 0x34E;

    // Orijinal 17 byte (MOVSS x3) geri yaz
    uint8_t orig[] = {
        0xF3,0x41,0x0F,0x10,0x19,
        0xF3,0x41,0x0F,0x10,0x41,0x04,
        0xF3,0x41,0x0F,0x10,0x51,0x08
    };
    WriteBytes(handleBulletAddr, orig, sizeof(orig));

    // Cave'i sıfırla (MYNX: clearBytes = {0})
    uint8_t clearBytes[33] = { 0 };
    WriteBytes(allocPtr, clearBytes, 33);
}

// ============================================================
//  WS_WeaponHook — WaveShield Weapon Check Bypass
//  Kaynak: MYNX/silent.cpp → WaveShieldBypass namespace
//
//  WaveShield'ın silah tipi doğrulama fonksiyonunu hook'lar.
//  Her çağrıda WEAPON_PISTOL hash (0xA2719263) döndürür →
//  unlimited FOV + silah doğrulama bypass.
//  Cam direction bypass'a (ApplySilent) EK koruma katmanı.
//
//  Desteklenen build'ler: 2372, 2612, 2699, 2802, 2944,
//                         3095, 3258, 3323, 3407, 3570 + default
// ============================================================
// ── WS_WeaponHook — MYNX WaveShieldBypass birebir port ──────────────────────
// MYNX yöntemi: bypass_code → moduleBase+0x600 cave'e yaz,
//               hook adresine 12-byte abs JMP (mov rax, cave; jmp rax) yaz.
// Orijinal 12 byte kaydedilip restore edilir.
namespace WS_WeaponHook {

    struct BuildEntry { int build; uintptr_t offset; };
    static const BuildEntry k_table[] = {
        { 2372, 0x00FE7DA4 }, { 2612, 0x00FF340C }, { 2699, 0x00FF9D90 },
        { 2802, 0x00FE2154 }, { 2944, 0x00FF716C }, { 3095, 0x00FCE6EC },
        { 3258, 0x00FF1B40 }, { 3323, 0x01003F80 }, { 3407, 0x0100F5A4 },
        { 3570, 0x0101A660 },
    };

    // bypass_code: mov eax, 0xA2719263 (WEAPON_PISTOL) ; ret  — MYNX birebir
    static const uint8_t k_bypass[6] = { 0xB8, 0x63, 0x92, 0x71, 0xA2, 0xC3 };

    // Prologue guard: sub rsp,28h — yanlış adrese yazmayı önler
    static const uint8_t k_prologue[4] = { 0x48, 0x83, 0xEC, 0x28 };

    static bool      s_hooked    = false;
    static uintptr_t s_hookAddr  = 0;    // s_addr winsock makrosuyla çakışır, s_hookAddr kullan
    static uint64_t  s_orig8     = 0;    // orijinal byte 0-7
    static uint32_t  s_orig4     = 0;    // orijinal byte 8-11  (12 byte toplam)

    // Pattern cache — namespace-level, reconnect'te Reset() sıfırlar
    static uintptr_t s_patRel = (uintptr_t)-1;

    // Pattern: 48 83 EC 28 E8 ?? ?? ?? ?? 48 85 C0 74 11 48 8B 80 B8 10 00 00
    static uintptr_t PatternScan() {
        if (!Offsets.GameBase || !Game.hProcess) return 0;
        static const uint8_t pat[] = {
            0x48,0x83,0xEC,0x28,0xE8, 0,0,0,0,
            0x48,0x85,0xC0,0x74,0x11,0x48,0x8B,0x80,0xB8,0x10,0x00,0x00
        };
        static const bool msk[] = {
            1,1,1,1,1, 0,0,0,0, 1,1,1,1,1,1,1,1,1,1,1,1
        };
        constexpr size_t PLEN  = sizeof(pat);
        constexpr size_t CHUNK = 0x80000;
        constexpr size_t SCAN  = 0x5000000;
        for (size_t off = 0; off < SCAN; off += CHUNK) {
            size_t chunk = ((SCAN - off) < CHUNK) ? (SCAN - off) : CHUNK;
            if (chunk < PLEN) break;
            std::vector<uint8_t> buf(chunk);
            SIZE_T rd = 0;
            if (!ReadProcessMemory(Game.hProcess,
                                   reinterpret_cast<LPCVOID>(Offsets.GameBase + off),
                                   buf.data(), chunk, &rd) || rd < PLEN) continue;
            for (size_t i = 0; i + PLEN <= rd; ++i) {
                bool ok = true;
                for (size_t j = 0; j < PLEN; ++j)
                    if (msk[j] && buf[i+j] != pat[j]) { ok = false; break; }
                if (ok) return off + i;
            }
        }
        return 0;
    }

    static bool ValidateAddr(uintptr_t addr) {
        if (!addr || !Game.hProcess) return false;
        uint8_t head[4] = {};
        SIZE_T rd = 0;
        if (!ReadProcessMemory(Game.hProcess, reinterpret_cast<LPCVOID>(addr),
                               head, 4, &rd) || rd < 4) return false;
        return memcmp(head, k_prologue, 4) == 0;
    }

    static uintptr_t ResolveAddr() {
        if (!Offsets.GameBase) return 0;
        int bld = 0;
        if (!Game.Version.empty())
            try { bld = std::stoi(Game.Version); } catch (...) {}
        for (auto& e : k_table)
            if (e.build == bld) return Offsets.GameBase + e.offset;
        if (s_patRel == (uintptr_t)-1)
            s_patRel = PatternScan();
        if (s_patRel) return Offsets.GameBase + s_patRel;
        return 0; // default offset yok — bilinmeyen build'de hook atla
    }

    void Hook() {
        if (s_hooked || !Game.hProcess || !Offsets.GameBase) return;
        uintptr_t addr = ResolveAddr();
        if (!addr) return;
        if (!ValidateAddr(addr)) return;

        // Orijinal 12 byte kaydet (MYNX: uint64 + uint32)
        uint8_t origBuf[12] = {};
        SIZE_T rd = 0;
        if (!ReadProcessMemory(Game.hProcess, reinterpret_cast<LPCVOID>(addr),
                               origBuf, 12, &rd) || rd < 12) return;
        memcpy(&s_orig8, origBuf,     8);
        memcpy(&s_orig4, origBuf + 8, 4);

        // bypass_code → moduleBase + 0x600 cave'e yaz (MYNX: caveAddr = moduleBase+0x600)
        uint64_t caveAddr = Offsets.GameBase + 0x600;
        uint8_t bypassCopy[6];
        memcpy(bypassCopy, k_bypass, 6);
        WriteBytes(caveAddr, bypassCopy, 6);

        // 12-byte absolute JMP: mov rax, caveAddr ; jmp rax  (MYNX birebir)
        uint8_t jumpCode[12] = {
            0x48, 0xB8,                              // mov rax,
            0,0,0,0,0,0,0,0,                         // caveAddr (8 bytes LE)
            0xFF, 0xE0                               // jmp rax
        };
        memcpy(&jumpCode[2], &caveAddr, 8);
        WriteBytes(addr, jumpCode, 12);

        s_hookAddr = addr;
        s_hooked   = true;
    }

    void Unhook() {
        if (!s_hooked || !s_hookAddr) return;
        // Orijinal 12 byte geri yaz
        uint8_t restBuf[12];
        memcpy(restBuf,     &s_orig8, 8);
        memcpy(restBuf + 8, &s_orig4, 4);
        WriteBytes(s_hookAddr, restBuf, 12);
        s_hooked   = false;
        s_hookAddr = 0;
    }

    bool IsHooked() { return s_hooked; }

    void Reset() {
        if (s_hooked) Unhook();
        s_patRel = (uintptr_t)-1;
    }

} // namespace WS_WeaponHook

// ResetCave — reconnect'te WS hook sıfırla
void ResetCave() {
    WS_WeaponHook::Reset();
}

// ============================================================
//  NativeHook: GET_GAMEPLAY_CAM_DIR (hash 0x00000000E14C7C6B)
// ============================================================
// Cave layout (cave+0..93):
//   [+0 ] uint32_t active       — 0=passthru, 1=spoof
//   [+4 ] uint32_t pad
//   [+8 ] float    FX
//   [+12] float    FY
//   [+16] float    FZ
//   [+20] uint64_t origHandler  — original handler address
//   [+28] uint32_t pad2
//   [+32] uint8_t  code[61]     — handler shellcode

static uintptr_t g_nhCave       = 0;
static uintptr_t g_nhHandlerPtr = 0;
static bool      g_nhInstalled  = false;

void NH_SetSpoof(bool active, float x, float y, float z) {
    if (!g_nhCave) return;
    uint32_t flag = active ? 1u : 0u;
    WriteBytes(g_nhCave + 0, (uint8_t*)&flag, 4);
    if (active) {
        union { float f; uint32_t i; } X{}, Y{}, Z{};
        X.f = x; Y.f = y; Z.f = z;
        WriteBytes(g_nhCave + 8,  (uint8_t*)&X.i, 4);
        WriteBytes(g_nhCave + 12, (uint8_t*)&Y.i, 4);
        WriteBytes(g_nhCave + 16, (uint8_t*)&Z.i, 4);
    }
}

bool InstallNativeHook() {
    if (g_nhInstalled) return true;
    if (!Game.hProcess) return false;

    // ── 1. gta-core-five.dll bul ──
    HMODULE mods[512] = {}; DWORD needed = 0;
    if (!EnumProcessModules(Game.hProcess, mods, sizeof(mods), &needed)) return false;

    uintptr_t coreBase = 0; size_t coreSize = 0;
    for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
        char name[MAX_PATH] = {};
        GetModuleFileNameExA(Game.hProcess, mods[i], name, sizeof(name));
        if (strstr(name, "gta-core-five")) {
            MODULEINFO mi{}; GetModuleInformation(Game.hProcess, mods[i], &mi, sizeof(mi));
            coreBase = (uintptr_t)mods[i];
            coreSize = mi.SizeOfImage;
            break;
        }
    }
    if (!coreBase) {
        PushToast("NH: gta-core-five yok", IM_COL32(255,0,0,255), 4.f);
        return false;
    }

    // ── 2. NativeRegistration tablosunda hash 0xE14C7C6B ara ──
    // NativeRegistration struct:
    //   +0:  nextRegistration (8)
    //   +8:  handlers[7]     (56) → handlers[k] at +8+k*8
    //   +64: numEntries      (4)
    //   +68: pad             (4)
    //   +72: hashes[7]       (56) → hashes[k] at +72+k*8
    // Herhangi bir k için: handlers[k]_addr = hashes[k]_addr - 64
    const uint64_t TARGET = 0x00000000E14C7C6BULL;
    uintptr_t handlerPtrAddr = 0, origHandler = 0;

    std::vector<uint8_t> buf(0x10000);
    for (uintptr_t off = 0; off < coreSize && !handlerPtrAddr; off += buf.size()) {
        SIZE_T rd = 0;
        size_t chunk = (coreSize - off < buf.size()) ? (coreSize - off) : buf.size();
        if (!ReadProcessMemory(Game.hProcess, (LPCVOID)(coreBase + off), buf.data(), chunk, &rd)) continue;

        for (size_t i = 0; i + 8 <= rd; i += 4) {
            if (*(uint64_t*)&buf[i] != TARGET) continue;

            uintptr_t hashAddr = coreBase + off + i;
            uintptr_t hpAddr   = hashAddr - 64;
            uintptr_t hp       = ReadMemory<uintptr_t>(hpAddr);
            // Temel aralık kontrolü
            if (hp < 0x100000ULL || hp > 0x7FFFFFFFF000ULL) continue;
            // hp gerçekten executable bir region'da mı? (false positive önle)
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQueryEx(Game.hProcess, reinterpret_cast<LPCVOID>(hp), &mbi, sizeof(mbi))) continue;
            bool isExec = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
            if (!isExec || mbi.State != MEM_COMMIT) continue;
            // numEntries sanity (k=0 için hashAddr-8 = struct+64)
            // k bilinmediği için sadece genel aralık: 1-7
            // En az bir k için struct başı geçerli mi kontrol et
            bool anyValidK = false;
            for (int k = 0; k < 7; k++) {
                uintptr_t structBase = hashAddr - 72 - (uintptr_t)k * 8;
                uint32_t ne = ReadMemory<uint32_t>(structBase + 64);
                if (ne >= 1 && ne <= 7 && (uint32_t)k < ne) { anyValidK = true; break; }
            }
            if (!anyValidK) continue;

            handlerPtrAddr = hpAddr;
            origHandler    = hp;
        }
    }
    if (!handlerPtrAddr) return false;

    if (!handlerPtrAddr) {
        PushToast("NH: hash bulunamadi", IM_COL32(255,0,0,255), 4.f);
        return false;
    }

    // ── 3. NativeHook cave: GameBase + 0x700  (PE header gap, 93 byte sığar)
    // GameBase+0x34E = silent aim cave (33b)
    // GameBase+0x600 = weapon hook cave (6b)
    // GameBase+0x700 = bu native hook cave (93b, 0x700..0x75C)
    uintptr_t cave = Offsets.GameBase + 0x700;
    if (!cave) {
        PushToast("NH: cave bulunamadi", IM_COL32(255,80,0,255), 4.f);
        return false;
    }

    // ── 4. Data bölümünü sıfırla ve origHandler'ı yaz ──
    uint8_t zeros[32] = {};
    WriteBytes(cave, zeros, 32);
    WriteBytes(cave + 20, (uint8_t*)&origHandler, 8);

    // ── 5. Handler shellcode yaz (cave+32, 61 byte) ──
    // Calling convention: RCX = scrNativeCallContext*
    //   scrNativeCallContext::m_pReturn @ +0x00 (Vector3 = 3 float)
    //
    // Offset:  Asm                         Encoding
    //  0: PUSH RCX                         51
    //  1: MOV RAX, cave                    48 B8 [8b]
    // 11: CMP DWORD [RAX], 0               83 38 00
    // 14: JZ +0x20  (→ offset 48)          74 20
    // 16: MOV R8D,  [RAX+8]               44 8B 40 08
    // 20: MOV R9D,  [RAX+12]              44 8B 48 0C
    // 24: MOV R10D, [RAX+16]              44 8B 50 10
    // 28: MOV RCX, [RSP]                   48 8B 0C 24
    // 32: MOV RAX, [RCX]      ;m_pReturn   48 8B 01
    // 35: MOV [RAX],   R8D                 44 89 00
    // 38: MOV [RAX+4], R9D                 44 89 48 04
    // 42: MOV [RAX+8], R10D               44 89 50 08
    // 46: POP RCX                          59
    // 47: RET                              C3
    // 48: POP RCX              ;JZ target  59
    // 49: MOV RAX, origHandler             48 B8 [8b]
    // 59: JMP RAX                          FF E0   → total 61

    uint8_t cA[8], cO[8];
    memcpy(cA, &cave,        8);
    memcpy(cO, &origHandler, 8);

    uint8_t code[61] = {
        0x51,
        0x48,0xB8, cA[0],cA[1],cA[2],cA[3],cA[4],cA[5],cA[6],cA[7],
        0x83,0x38,0x00,
        0x74,0x20,
        0x44,0x8B,0x40,0x08,
        0x44,0x8B,0x48,0x0C,
        0x44,0x8B,0x50,0x10,
        0x48,0x8B,0x0C,0x24,
        0x48,0x8B,0x01,
        0x44,0x89,0x00,
        0x44,0x89,0x48,0x04,
        0x44,0x89,0x50,0x08,
        0x59, 0xC3,
        // JZ target (offset 48):
        0x59,
        0x48,0xB8, cO[0],cO[1],cO[2],cO[3],cO[4],cO[5],cO[6],cO[7],
        0xFF,0xE0
    };
    WriteBytes(cave + 32, code, 61);

    // ── 6. Handler pointer'ı kendi cave'imize yönlendir ──
    uintptr_t hookEntry = cave + 32;
    WriteBytes(handlerPtrAddr, (uint8_t*)&hookEntry, 8);

    g_nhCave       = cave;
    g_nhHandlerPtr = handlerPtrAddr;
    g_nhInstalled  = true;
    PushToast("NH: OK", IM_COL32(0,220,100,255), 4.f);
    return true;
}

// ============================================================
//  AimAssist thread
// ============================================================
void AimAssist() {
    // Kalici hedef depolama — dangling pointer olmasın
    static Ped s_silentTarget;
    static Ped s_aimbotTarget;
    static Ped s_mbTarget;
    static bool mbApplied = false;

    while (true) {
        if (!keepRunning || !g_gameConnected) { Sleep(50); continue; }
        // ── Magic Bullet ──────────────────────────────────────────
        // Silent ile aynı code cave'i kullanır; silent açıksa öncelik silent'ta.
        if (Cheats::AimAssist::MagicBullet::Enabled && !Cheats::AimAssist::Silent::Enabled) {
            int bm   = Cheats::AimAssist::MagicBullet::BoneMode;
            int area = GetTargetBone(bm);
            Ped t = FindTarget(9999,
                               Cheats::AimAssist::MagicBullet::MaxDistance,
                               bm, 0,
                               false,   // vis check yok — duvar arkasini da hedef al
                               Cheats::AimAssist::MagicBullet::SkipFriends);
            // NPC filtresi
            if (!Vec3Empty(t.BoneList[area]) &&
                Cheats::AimAssist::MagicBullet::SkipNPC && !t.IsPlayer()) {
                // NPC'yi atla
                if (mbApplied) { RestoreSilent(); mbApplied = false; }
            } else if (!Vec3Empty(t.BoneList[area])) {
                if (!mbApplied)
                    PushToast("Magic Bullet: Aktif", IM_COL32(255,180,0,255), 1.5f);
                s_mbTarget   = t;
                EndBulletPos = t.BoneList[area];
                ApplySilent();   // aynı code cave'i kullan
                mbApplied = true;
            } else {
                if (mbApplied) { RestoreSilent(); mbApplied = false; }
            }
        } else if (mbApplied && !Cheats::AimAssist::Silent::Enabled) {
            // Magic bullet kapandı, cave'i temizle
            RestoreSilent();
            mbApplied = false;
        }

       
        if (Cheats::AimAssist::Silent::Enabled) {
            int bm = Cheats::AimAssist::Silent::BoneMode;
            int area = GetTargetBone(bm);
            Ped t = FindTarget(Cheats::AimAssist::Silent::Fov,
                               Cheats::AimAssist::Silent::MaxDistance, bm, 0,
                               Cheats::AimAssist::Silent::VisCheck,
                               Cheats::AimAssist::Silent::SkipFriends);
            bool silentKey = AimKeyActive(Cheats::AimAssist::Silent::HotKey,
                                          Cheats::AimAssist::Silent::ToggleMode,
                                          s_silentToggleOn);
            if (Vec3Empty(t.BoneList[area])) { RestoreSilent(); SilentPed=nullptr; }
            else if (silentKey) {
                if (!SilentPed) PushToast("Silent: Hedef Kilitlendi", IM_COL32(100,220,140,255), 1.5f);
                s_silentTarget = t;
                SilentPed = &s_silentTarget;

                EndBulletPos = t.BoneList[area];
                int mc = Cheats::AimAssist::Silent::MissChance;
                bool miss = (mc > 0 && (rand()%100) < mc);
                if (!miss) ApplySilent();
                else RestoreSilent();
            } else { SilentPed=nullptr; RestoreSilent(); }
        } else {
            if (SilentPed) { RestoreSilent(); SilentPed=nullptr; }
        }

        // Aimbot
        bool aimbotKey = AimKeyActive(Cheats::AimAssist::Aimbot::HotKey,
                                      Cheats::AimAssist::Aimbot::ToggleMode,
                                      s_aimbotToggleOn);
        if (Cheats::AimAssist::Aimbot::Enabled && aimbotKey) {
            int bm   = Cheats::AimAssist::Aimbot::BoneMode;
            int area = GetTargetBone(bm);
            int prio = Cheats::AimAssist::Aimbot::Priority;
            Ped t = FindTarget(Cheats::AimAssist::Aimbot::Fov,
                               Cheats::AimAssist::Aimbot::MaxDistance, bm, prio,
                               Cheats::AimAssist::Aimbot::VisCheck,
                               Cheats::AimAssist::Aimbot::SkipFriends);
            if (!Vec3Empty(t.BoneList[area])) {
                // Sorun 1 düzeltme: cam sadece !=0 değil, geçerli bellek aralığında olmalı.
                // Garbage pointer GTA'nın başka bir struct'ına yazabilir → crash.
                uintptr_t cam = (Offsets.Camera && Offsets.GameBase)
                    ? ReadMemory<uintptr_t>(Offsets.GameBase + Offsets.Camera) : 0;
                bool camValid = cam > 0x10000ULL && cam < 0x7FFFFFFFF000ULL;
                if (camValid) {
                    Vector3 vAngle=ReadMemory<Vector3>(cam+0x3D0);
                    Vector3 camPos=ReadMemory<Vector3>(cam+0x60);

                    // Sorun 3 düzeltme: vAngle kamera yön vektörü — birim vektör olmalı (magnitude ≈ 1).
                    // Garbage cam pointer'dan okunursa çöp değer gelir → do-while(0) ile erken çık.
                    float vLen = sqrtf(vAngle.x*vAngle.x + vAngle.y*vAngle.y + vAngle.z*vAngle.z);
                    bool vAngleOk = (vLen >= 0.1f && vLen <= 10.f);
                    if (vAngleOk) {
                        Vector3 angle=CalcAngle(camPos,t.BoneList[area]);
                        NormalizeAngles(angle);
                        Vector3 delta=angle-vAngle; NormalizeAngles(delta);
                        int sm=Cheats::AimAssist::Aimbot::Smooth;
                        Vector3 write=vAngle+(sm>0?delta/(float)sm:delta);
                        write = Normalize(write);   // unit vector — len<1e-6 → {0,0,0} döner (artık güvenli)
                        bool valid = !Vec3Empty(write)
                                  && std::isfinite(write.x) && std::isfinite(write.y) && std::isfinite(write.z);
                        if (valid) {
                            if (Cheats::AimAssist::Aimbot::AimMode == 1 && GameViewPort) {
                                // Mouse mode: pixel tabanlı hareket
                                Matrix vm = ReadMemory<Matrix>(GameViewPort + 0x24C);
                                Vector2 sp;
                                if (WorldToScreen(vm, t.BoneList[area], sp)) {
                                    float cx=(float)Game.lpRect.right*0.5f, cy=(float)Game.lpRect.bottom*0.5f;
                                    float dx=(sp.x-cx)/(float)(sm>0?sm:1), dy=(sp.y-cy)/(float)(sm>0?sm:1);
                                    if (std::isfinite(dx) && std::isfinite(dy))
                                        mouse_event(MOUSEEVENTF_MOVE,(DWORD)(int)dx,(DWORD)(int)dy,0,0);
                                }
                            } else if (Cheats::AimAssist::Aimbot::AimMode != 1) {
                                WriteMemory<Vector3>(cam+0x3D0, write);
                            }
                            if (!AimbotPed) PushToast("Aimbot: Hedef Kilitlendi", IM_COL32(80,180,255,255), 1.5f);
                            s_aimbotTarget = t;
                            AimbotPed = &s_aimbotTarget;
                        }
                    }
                }
            } else if (!Cheats::AimAssist::Aimbot::StickyAim) {
                AimbotPed=nullptr;
            }
        }

        // Triggerbot
        if (Cheats::AimAssist::Triggerbot::Enabled) {
            Matrix vm=ReadMemory<Matrix>(GameViewPort+0x24C);
            std::vector<Ped> tbPeds;
            { std::lock_guard<std::mutex> lk(g_pedMutex); tbPeds = PedList; }
            for (auto& ped : tbPeds) {
                if (!ped.Update()) continue;
                float pd=GetDistance(ped.Position,LocalPlayer.Position);
                if (pd>Cheats::AimAssist::Triggerbot::MaxDistance) continue;
                if (Cheats::AimAssist::Settings::IgnorePed&&!ped.IsPlayer()) continue;
                if (Cheats::AimAssist::Settings::IgnoreDeath&&ped.IsDead()) continue;
                if (!ped.IsVisible()) continue;
                if (Cheats::AimAssist::Triggerbot::SkipFriends && IsFriend(ped.GetId())) continue;
                Vector2 pB{},pH{},pN{},pLF{},pRF{};
                if(!WorldToScreen(vm,ped.Position,pB)||!WorldToScreen(vm,ped.BoneList[Head],pH)||
                   !WorldToScreen(vm,ped.BoneList[Neck],pN)||!WorldToScreen(vm,ped.BoneList[LeftFoot],pLF)||
                   !WorldToScreen(vm,ped.BoneList[RightFoot],pRF)) continue;
                if(IsTargetInCrosshair(pB)||IsTargetInCrosshair(pH)||IsTargetInCrosshair(pN)||
                   IsTargetInCrosshair(pLF)||IsTargetInCrosshair(pRF)){
                    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);
                    Sleep(Cheats::AimAssist::Triggerbot::Delay);
                    mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
                }
            }
        }
        Sleep(10);
    }
}

// ============================================================
//  Teleport helpers
// ============================================================
static std::mutex g_caveMutex; // Magic bullet #5 fix: thread race condition
void TeleportObject(uintptr_t obj, uintptr_t nav, uintptr_t mi, Vector3 pos, bool stop) {
    float magic=0.f;
    if (stop&&mi){ magic=ReadMemory<float>(mi+0x2C); WriteMemory<float>(mi+0x2C,0.f); }
    WriteMemory<Vector3>(obj+0x90, pos);
    WriteMemory<Vector3>(nav+0x50, pos);
    if (stop&&mi){ std::this_thread::sleep_for(std::chrono::milliseconds(40)); WriteMemory<float>(mi+0x2C,magic); }
}

// Magic Bullet tek-write atomik: race condition fix
static void ApplyBulletAtomic(uintptr_t weaponMi, const float& value) {
    if (!weaponMi) return;
    std::lock_guard<std::mutex> lk(g_caveMutex);
    WriteMemory<float>(weaponMi + 0x2C, value);
}

void PositionTeleport(Vector3 pos) {
    if (!LocalPlayer.Pointer) return;
    // FiveM sync ~20Hz pozisyonu geri yazar — 25 frame boyunca üst üste yazarak override et (~500ms)
    static const Vector3 zero3 = {0.f, 0.f, 0.f};
    for (int i = 0; i < 25; i++) {
        WriteMemory<Vector3>(LocalPlayer.Pointer + 0x90, pos);
        uintptr_t nav = ReadMemory<uintptr_t>(LocalPlayer.Pointer + 0x30);
        if (nav > 0x10000ULL && nav < 0x7FFFFFFFF000ULL) {
            WriteMemory<Vector3>(nav + 0x50, pos);
            WriteMemory<Vector3>(nav + 0x60, zero3);  // velocity sıfırla
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void TeleportWaypoint() {
    for (int i=0;i<2000;++i){
        uint64_t blip=ReadMemory<uint64_t>(Offsets.GameBase+Offsets.Waypoint+i*8);
        if(!blip) continue;
        if(ReadMemory<int>(blip+0x40)!=8||ReadMemory<int>(blip+0x48)!=84) continue;
        Vector2 wp=ReadMemory<Vector2>(blip+0x10);
        if(wp.x!=0||wp.y!=0){ PositionTeleport(Vector3(wp.x,wp.y,-210.f)); break; }
    }
}

// ============================================================
//  Exploits thread
// ============================================================
void Exploits() {
    // ── Aim key Toggle modu: rising-edge algıla → s_*ToggleOn flip et ──────
    {
        static bool s_sWas = false, s_aWas = false;
        bool sNow = Cheats::AimAssist::Silent::HotKey &&
                    (GetAsyncKeyState(Cheats::AimAssist::Silent::HotKey) & 0x8000) != 0;
        bool aNow = Cheats::AimAssist::Aimbot::HotKey &&
                    (GetAsyncKeyState(Cheats::AimAssist::Aimbot::HotKey) & 0x8000) != 0;
        if (Cheats::AimAssist::Silent::ToggleMode && sNow && !s_sWas)
            s_silentToggleOn = !s_silentToggleOn;
        if (Cheats::AimAssist::Aimbot::ToggleMode && aNow && !s_aWas)
            s_aimbotToggleOn = !s_aimbotToggleOn;
        s_sWas = sNow; s_aWas = aNow;
    }

    static bool magicInit=false; static float magicVal=0.f; static bool noClipOn=false;
    static bool iaA=false; static float oA=0.f;
    static bool nrA=false; static float oR=0.f;
    static bool nsA=false; static float oS=0.f;
    static bool nreA=false; static float oRe=0.f;
    static bool nraA=false; static float oRa=0.f;
    static bool ebA=false; static int   oEb=0;
    static bool rfA=false; static float oRf=0.f;
    static bool dbA=false; static float oDb=0.f;

    while (true) {
        if (!keepRunning || !g_gameConnected) {
            // Bağlantı kesildi — weapon hook'u temizle
            if (WS_WeaponHook::IsHooked()) WS_WeaponHook::Unhook();
            Sleep(50); continue;
        }
        if (!LocalPlayer.Pointer) { Sleep(10); continue; }

        // ── WaveShield Weapon Check Bypass — silent açıkken oto aktif ───
        if ((bool)Cheats::AimAssist::Silent::Enabled) {
            if (!WS_WeaponHook::IsHooked()) WS_WeaponHook::Hook();
        } else if (WS_WeaponHook::IsHooked()) {
            WS_WeaponHook::Unhook();
        }

        if (!magicInit) {
            uintptr_t mi=ReadMemory<uintptr_t>(LocalPlayer.Pointer+0x20);
            if(mi){ magicVal=ReadMemory<float>(mi+0x2C); magicInit=true; }
        }

        // NoClip
        if (Cheats::World::NoClip::Enabled) {
            noClipOn=true;
            uintptr_t cam=ReadMemory<uintptr_t>(ReadMemory<uintptr_t>(Offsets.GameBase+Offsets.BlipList)+0x3C0);
            Vector3 camPos=ReadMemory<Vector3>(cam+0x40);
            Vector3 plPos=ReadMemory<Vector3>(LocalPlayer.Pointer+0x90);
            uintptr_t nav=ReadMemory<uintptr_t>(LocalPlayer.Pointer+0x30);
            Vector3 fwd=Normalize({camPos.x-plPos.x,camPos.y-plPos.y,camPos.z-plPos.z});
            Vector3 right=Normalize(fwd.Cross({0,0,1}));
            float spd=Cheats::World::NoClip::MovementSpeed*0.1f;
            Vector3 np=plPos;
            if(GetAsyncKeyState(Cheats::World::NoClip::ForwardKey))  np=np+fwd*spd;
            if(GetAsyncKeyState(Cheats::World::NoClip::BackwardKey)) np=np-fwd*spd;
            if(GetAsyncKeyState(Cheats::World::NoClip::LeftKey))     np=np-right*spd;
            if(GetAsyncKeyState(Cheats::World::NoClip::RightKey))    np=np+right*spd;
            if(GetAsyncKeyState(Cheats::World::NoClip::UpKey))      np=np+Vector3(0,0,1)*spd;
            if(GetAsyncKeyState(Cheats::World::NoClip::DownKey))    np=np-Vector3(0,0,1)*spd;
            TeleportObject(LocalPlayer.Pointer,nav,0,np,false);
        } else if (noClipOn) {
            uintptr_t mi=ReadMemory<uintptr_t>(LocalPlayer.Pointer+0x20);
            if(mi) WriteMemory<float>(mi+0x2C,magicVal);
            noClipOn=false;
        }

        // Weapon mods
        uintptr_t wm=ReadMemory<uintptr_t>(LocalPlayer.Pointer+Offsets.WeaponManager);
        uintptr_t wi=ReadMemory<uintptr_t>(wm+0x20);
        if(wi){
            auto mod=[&](bool en,bool&ap,float&old,uintptr_t off,float val){
                if(en){if(!ap){old=ReadMemory<float>(wi+off);ap=true;}WriteMemory<float>(wi+off,val);}
                else if(ap){WriteMemory<float>(wi+off,old);ap=false;}
            };
            mod(Cheats::AimAssist::Settings::NoRecoil, nrA, oR, 0x2F4, 0.f);
            mod(Cheats::AimAssist::Settings::NoSpread, nsA, oS, 0x84,  0.f);
            mod(Cheats::AimAssist::Settings::NoReload, nreA,oRe,0x134,1000.f);
            mod(Cheats::AimAssist::Settings::NoRange,  nraA,oRa,0x28C,1000.f);

            if(Cheats::AimAssist::Settings::InfiniteAmmo){
                uintptr_t ai=ReadMemory<uintptr_t>(wi+0x60);
                uintptr_t ac=ReadMemory<uintptr_t>(ai+0x8);
                uintptr_t ac2=ReadMemory<uintptr_t>(ac+0x0);
                if(ac2){if(!iaA){oA=ReadMemory<float>(ac2+0x18);iaA=true;}WriteMemory<float>(ac2+0x18,30.f);}
            } else if(iaA){
                uintptr_t ai=ReadMemory<uintptr_t>(wi+0x60);
                uintptr_t ac=ReadMemory<uintptr_t>(ai+0x8);
                uintptr_t ac2=ReadMemory<uintptr_t>(ac+0x0);
                if(ac2) WriteMemory<float>(ac2+0x18,oA);
                iaA=false;
            }
        }

        // HealToggle — her basışta HP veya Armor dönüşümlü doldur
        if (Cheats::World::HealToggle::Enabled &&
            Cheats::World::HealToggle::HotKey &&
            (GetAsyncKeyState(Cheats::World::HealToggle::HotKey) & 1)) {
            if (Cheats::World::HealToggle::NextIsHP) {
                WriteMemory<float>(LocalPlayer.Pointer + Offsets.Health, 198.f);
            } else {
                WriteMemory<float>(LocalPlayer.Pointer + Offsets.Armor, 100.f);
            }
            Cheats::World::HealToggle::NextIsHP = !Cheats::World::HealToggle::NextIsHP;
        }

        // ArmorFill — tuşa basınca sadece zırhı 100'e doldur (can değişmez)
        if (Cheats::World::ArmorFill::Enabled &&
            Cheats::World::ArmorFill::HotKey &&
            (GetAsyncKeyState(Cheats::World::ArmorFill::HotKey) & 1)) {
            WriteMemory<float>(LocalPlayer.Pointer + Offsets.Armor, 100.f);
        }

        if(Cheats::Vehicles::VehicleFix::Enabled&&(GetAsyncKeyState(Cheats::Vehicles::VehicleFix::HotKey)&1)){
            uintptr_t veh=ReadMemory<uintptr_t>(LocalPlayer.Pointer+Offsets.Vehicle);
            if(veh) WriteMemory<uint8_t>(veh+0x972,0x17);
        }

        // ── Araç özellikleri — sadece araçtayken çalışır ──────────────────────
        {
            uintptr_t veh = ReadMemory<uintptr_t>(LocalPlayer.Pointer + Offsets.Vehicle);
            bool inVeh = (veh > 0x10000ULL && veh < 0x7FFFFFFFF000ULL);

            // Vehicle God Mode — aracın HP'sini sürekli 1000'de tut
            if (Cheats::Vehicles::VehicleGodMode::Enabled && inVeh)
                WriteMemory<float>(veh + Offsets.Health, 1000.f);

            // Vehicle Speed Boost — araç hareket ederken hızı hedef km/h'e çek
            // Velocity offset 0x2B0 = CVehicle velocity vector (GTA5 b2802–b3570 sabit)
            if (Cheats::Vehicles::SpeedBoost::Enabled && inVeh) {
                float vx = ReadMemory<float>(veh + 0x2B0);
                float vy = ReadMemory<float>(veh + 0x2B4);
                float curSpd = sqrtf(vx*vx + vy*vy);       // yatay hız m/s
                float target = Cheats::Vehicles::SpeedBoost::KmH / 3.6f;
                // Sadece araç ilerlerken ve hedef hızın altındayken boost yap
                if (curSpd > 1.5f && curSpd < target) {
                    float scale = target / curSpd;
                    WriteMemory<float>(veh + 0x2B0, vx * scale);
                    WriteMemory<float>(veh + 0x2B4, vy * scale);
                }
            }

            // Vehicle Flip — hotkey'e basınca devrilen aracı düzeltir
            if (Cheats::Vehicles::VehicleFlip::Enabled && inVeh &&
                Cheats::Vehicles::VehicleFlip::HotKey) {
                static bool s_flipWas = false;
                bool flipNow = (GetAsyncKeyState(Cheats::Vehicles::VehicleFlip::HotKey) & 0x8000) != 0;
                if (flipNow && !s_flipWas) {
                    // Forward vektörünü oku, XY düzlemine yansıt (pitch sıfırla)
                    Vector3 fwd = ReadMemory<Vector3>(veh + 0x70);
                    float len = sqrtf(fwd.x*fwd.x + fwd.y*fwd.y);
                    if (len > 0.001f) { fwd.x /= len; fwd.y /= len; }
                    else              { fwd = {0.f, 1.f, 0.f}; }
                    fwd.z = 0.f;
                    // GTA5 Z-up sağ-el: right = (fwd.y, -fwd.x, 0), up = (0,0,1)
                    Vector3 right = { fwd.y, -fwd.x, 0.f };
                    Vector3 up    = { 0.f,    0.f,   1.f };
                    WriteMemory<Vector3>(veh + 0x60, right);
                    WriteMemory<Vector3>(veh + 0x70, fwd);
                    WriteMemory<Vector3>(veh + 0x80, up);
                    // Zeminden biraz kaldır — doğrulduktan sonra içine gömülmesin
                    Vector3 pos = ReadMemory<Vector3>(veh + 0x90);
                    pos.z += 0.6f;
                    WriteMemory<Vector3>(veh + 0x90, pos);
                    uintptr_t nav = ReadMemory<uintptr_t>(veh + 0x30);
                    if (nav > 0x10000ULL && nav < 0x7FFFFFFFF000ULL)
                        WriteMemory<Vector3>(nav + 0x50, pos);
                }
                s_flipWas = flipNow;
            }
        }

        if(Cheats::Players::StatusBars::HealthBoost::Enabled&&
           (GetAsyncKeyState(Cheats::Players::StatusBars::HealthBoost::HotKey)&1)){
            float hp=ReadMemory<float>(LocalPlayer.Pointer+Offsets.Health);
            float nh=hp+(float)Cheats::Players::StatusBars::HealthBoost::Value;
            if(nh>199.f)nh=198.f;
            WriteMemory<float>(LocalPlayer.Pointer+Offsets.Health,nh);
        }
        if(Cheats::Players::StatusBars::ArmorBoost::Enabled&&
           (GetAsyncKeyState(Cheats::Players::StatusBars::ArmorBoost::HotKey)&1)){
            float ar=ReadMemory<float>(LocalPlayer.Pointer+Offsets.Armor);
            float na=ar+(float)Cheats::Players::StatusBars::ArmorBoost::Value;
            if(na>99.f)na=98.f;
            WriteMemory<float>(LocalPlayer.Pointer+Offsets.Armor,na);
        }

        if(Cheats::World::SemiGodMode::Enabled){
            float hp=ReadMemory<float>(LocalPlayer.Pointer+Offsets.Health);
            if(hp<170.f) WriteMemory<float>(LocalPlayer.Pointer+Offsets.Health,198.f);
        }

        // Player info features (SuperSprint / Stamina / NoWanted)
        uintptr_t pi2 = ReadMemory<uintptr_t>(LocalPlayer.Pointer + Offsets.PlayerInfo);
        if (pi2) {
                static bool s_spWasOn = false;
            if (Cheats::World::SuperSprint::Enabled) {
                // 0x1A4 ve 0x1A8 her ikisine de yaz — FiveM build'e göre biri aktif olur.
                float spMult = (float)Cheats::World::SuperSprint::Speed;
                WriteMemory<float>(pi2 + 0x1A4, spMult);
                WriteMemory<float>(pi2 + 0x1A8, spMult);
                s_spWasOn = true;
            } else {
                // Devre dışı → varsayılan hızı geri yaz (1.0f)
                if (s_spWasOn) {
                    WriteMemory<float>(pi2 + 0x1A4, 1.0f);
                    WriteMemory<float>(pi2 + 0x1A8, 1.0f);
                    s_spWasOn = false;
                }
            }
            if (Cheats::World::InfiniteStamina::Enabled)
                WriteMemory<float>(pi2 + 0x294, 100.f);
        }

        // Weapon-based features using already-fetched wi
        if (wi) {
            auto modI=[&](bool en,bool&ap,int&old2,uintptr_t off,int val){
                if(en){if(!ap){old2=ReadMemory<int>(wi+off);ap=true;}WriteMemory<int>(wi+off,val);}
                else if(ap){WriteMemory<int>(wi+off,old2);ap=false;}
            };
            auto modF2=[&](bool en,bool&ap,float&old2,uintptr_t off,float val){
                if(en){if(!ap){old2=ReadMemory<float>(wi+off);ap=true;}WriteMemory<float>(wi+off,val);}
                else if(ap){WriteMemory<float>(wi+off,old2);ap=false;}
            };
            modI (Cheats::World::ExplosiveBullets::Enabled, ebA, oEb, 0x120, 0x4000000);
            modF2(Cheats::World::RapidFire::Enabled,        rfA, oRf, 0x128, 0.001f);
            if (Cheats::World::FireBullets::Enabled) WriteMemory<BYTE>(wi+0x220, 1);
            else                                     WriteMemory<BYTE>(wi+0x220, 0);

            // DamageBoost: fDamage (0x9C) değerini çarpanla yaz, eski değeri sakla
            if (Cheats::World::DamageBoost::Enabled) {
                if (!dbA) { oDb = ReadMemory<float>(wi + 0x9C); dbA = true; }
                WriteMemory<float>(wi + 0x9C, oDb * Cheats::World::DamageBoost::Multiplier);
            } else if (dbA) {
                WriteMemory<float>(wi + 0x9C, oDb);
                dbA = false;
            }
        }

        // LowDamage: gelen hasarı çarpana bölerek azalt
        {
            static float s_ldPrevHp = -1.f;
            if (Cheats::World::LowDamage::Enabled) {
                float curHp = ReadMemory<float>(LocalPlayer.Pointer + Offsets.Health);
                if (s_ldPrevHp < 0.f) {
                    s_ldPrevHp = curHp;
                } else if (curHp < s_ldPrevHp) {
                    float dmg  = s_ldPrevHp - curHp;
                    float mult = std::max(1.01f, Cheats::World::LowDamage::Multiplier);
                    float newHp = s_ldPrevHp - dmg / mult;
                    if (newHp <= 0.f) newHp = 1.f;  // ölümü engelle
                    WriteMemory<float>(LocalPlayer.Pointer + Offsets.Health, newHp);
                    s_ldPrevHp = newHp;
                } else {
                    s_ldPrevHp = curHp;
                }
            } else {
                s_ldPrevHp = -1.f;  // devre dışı → state sıfırla
            }
        }

        // ── Invisible — GameBase + 0x6AE372 görünürlük baytını sıfırla ─────────
        // Orijinal değer ilk aktifleşmede kaydedilir; devre dışında geri yazılır.
        {
            static bool    s_invActive  = false;
            static uint8_t s_invOrig    = 0xFF;  // 0xFF = henüz okunmadı

            bool wantInv = Cheats::World::Invisible::Enabled;
            // Hotkey varsa rising-edge toggle
            if (Cheats::World::Invisible::HotKey) {
                static bool s_invKeyWas = false;
                bool keyNow = (GetAsyncKeyState(Cheats::World::Invisible::HotKey) & 0x8000) != 0;
                if (keyNow && !s_invKeyWas)
                    Cheats::World::Invisible::Enabled = !Cheats::World::Invisible::Enabled;
                s_invKeyWas = keyNow;
                wantInv = Cheats::World::Invisible::Enabled;
            }

            if (Offsets.Invisible && Offsets.GameBase) {
                uintptr_t addr = Offsets.GameBase + Offsets.Invisible;
                if (wantInv) {
                    if (!s_invActive) {
                        // Orijinal değeri bir kez oku ve sakla
                        s_invOrig   = ReadMemory<uint8_t>(addr);
                        s_invActive = true;
                    }
                    WriteMemory<uint8_t>(addr, 0);
                } else if (s_invActive) {
                    // Devre dışı → orijinal değeri geri yaz
                    if (s_invOrig != 0xFF)
                        WriteMemory<uint8_t>(addr, s_invOrig);
                    s_invActive = false;
                    s_invOrig   = 0xFF;
                }
            }
        }

        // Respawn: ölüm anında (HP ≤ 0) canı 198'e yaz
        if (Cheats::World::Respawn::Enabled) {
            if (!Cheats::World::Respawn::HotKey ||
                (GetAsyncKeyState(Cheats::World::Respawn::HotKey)&1)) {
                float hp = ReadMemory<float>(LocalPlayer.Pointer+Offsets.Health);
                if (hp <= 0.f)
                    WriteMemory<float>(LocalPlayer.Pointer+Offsets.Health, 198.f);
            }
        }

        // UnlockCar: ModKey + F ile en yakın aracı seç, 800ms boyunca her tick kilidini aç.
        // FiveM sunucuları ~30Hz lock yazar; biz ~100Hz unlock yazarak sürekli kazanırız.
        // Sorun 1 fix: vi2/vl2 null check eklendi.
        // Sorun 2 fix: uint8_t kullan — uint32_t yazınca 0x261–0x263 bozuluyordu.
        // Sorun 3 fix: hedef araç pointer saklanır, 800ms boyunca her tick yeniden yazılır.
        {
            static bool     s_fWasDown    = false;
            static uintptr_t s_lockTarget = 0;      // unlock uygulanacak araç
            static DWORD     s_lockUntil  = 0;      // bu tick'e kadar yaz

            bool fDown    = (GetAsyncKeyState(0x46) & 0x8000) != 0;
            bool fPressed = fDown && !s_fWasDown;
            s_fWasDown    = fDown;

            if (Cheats::World::UnlockCar::Enabled) {
                bool modHeld = !Cheats::World::UnlockCar::ModKey ||
                               (GetAsyncKeyState(Cheats::World::UnlockCar::ModKey) & 0x8000);

                uintptr_t curVeh = ReadMemory<uintptr_t>(LocalPlayer.Pointer + Offsets.Vehicle);
                bool inVehicle   = (curVeh > 0x10000ULL && curVeh < 0x7FFFFFFFF000ULL);

                // F'ye basılınca yakındaki aracı bul ve 800ms hedef olarak işaretle
                if (modHeld && fPressed && !inVehicle && GameReplayInterface) {
                    uintptr_t vi2 = ReadMemory<uintptr_t>(GameReplayInterface + 0x10);
                    // Fix 1: vi2 geçerlilik kontrolü — 0 ise vl2/cnt2 okuma crashlar
                    if (vi2 > 0x10000ULL && vi2 < 0x7FFFFFFFF000ULL) {
                        uintptr_t vl2 = ReadMemory<uintptr_t>(vi2 + 0x180);
                        int       cnt2 = ReadMemory<int>(vi2 + 0x188);
                        // Fix 2: vl2 geçerlilik kontrolü
                        if (vl2 > 0x10000ULL && cnt2 > 0 && cnt2 <= 500) {
                            Vector3   myPos       = ReadMemory<Vector3>(LocalPlayer.Pointer + 0x90);
                            uintptr_t nearestVeh  = 0;
                            float     nearestDist = 8.f;
                            for (int i = 0; i < cnt2; ++i) {
                                uintptr_t vp = ReadMemory<uintptr_t>(vl2 + i * 0x10);
                                if (!vp || vp < 0x10000ULL || vp > 0x7FFFFFFFF000ULL) continue;
                                Vector3 vpos = ReadMemory<Vector3>(vp + 0x90);
                                Vector3 diff = myPos - vpos;
                                float   dist = sqrtf(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
                                if (dist < nearestDist) { nearestDist = dist; nearestVeh = vp; }
                            }
                            if (nearestVeh) {
                                s_lockTarget = nearestVeh;
                                s_lockUntil  = GetTickCount() + 800; // 800ms boyunca yaz
                            }
                        }
                    }
                }

                // Her tick: hedef araç geçerliyse ve süre dolmadıysa unlock yaz
                // Fix 3: uint8_t — lock field 1 bayt; uint32_t yazınca 0x261–0x263 bozulurdu
                if (s_lockTarget && GetTickCount() < s_lockUntil) {
                    WriteMemory<uint8_t>(s_lockTarget + 0x260, 1); // VEHICLELOCK_UNLOCKED = 1
                }
            }

            // Özellik kapatılırsa veya süre dolduysa hedefi temizle
            if (!Cheats::World::UnlockCar::Enabled || GetTickCount() >= s_lockUntil)
                s_lockTarget = 0;
        }

        // ── Spectator Detection ────────────────────────────────────────────────
        if (Cheats::SpectatorDetect::Enabled) {
            Vector3 myPos = ReadMemory<Vector3>(LocalPlayer.Pointer + 0x90);
            static std::unordered_map<int,int> s_specTicks;
            std::vector<int> specs;
            {
                std::lock_guard<std::mutex> lk(g_pedMutex);
                std::unordered_map<int,int> newTicks;
                for (auto& p : PedList) {
                    float dx = p.Position.x - myPos.x;
                    float dy = p.Position.y - myPos.y;
                    float dz = p.Position.z - myPos.z;
                    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                    int id = p.GetId();
                    if (id > 0 && dist < 1.0f) {
                        int prev = s_specTicks.count(id) ? s_specTicks[id] : 0;
                        newTicks[id] = prev + 1;
                        if (newTicks[id] >= Cheats::SpectatorDetect::Threshold)
                            specs.push_back(id);
                    }
                }
                s_specTicks = newTicks;
            }
            {
                std::lock_guard<std::mutex> lk(g_spectatorMutex);
                g_spectatorIds = specs;
            }
        } else {
            std::lock_guard<std::mutex> lk(g_spectatorMutex);
            g_spectatorIds.clear();
        }

        // ── Low Gravity ────────────────────────────────────────────────────────
        if (Cheats::LowGravity::Enabled) {
            static float s_prevZ = 0.f;
            Vector3 pos = ReadMemory<Vector3>(LocalPlayer.Pointer + 0x90);
            float dz = pos.z - s_prevZ;
            s_prevZ = pos.z;
            // Düşüyorsa (Z azalıyorsa) yukarı it
            if (dz < -0.005f) {
                float str = std::max(0.f, std::min(1.f, Cheats::LowGravity::Strength));
                float cancelZ = fabsf(dz) * str;
                pos.z += cancelZ;
                WriteMemory<Vector3>(LocalPlayer.Pointer + 0x90, pos);
                uintptr_t navLG = ReadMemory<uintptr_t>(LocalPlayer.Pointer + 0x30);
                if (navLG > 0x10000ULL && navLG < 0x7FFFFFFFF000ULL)
                    WriteMemory<float>(navLG + 0x58, pos.z);
            }
        }

        // ── Moon Jump — Space basılıyken sürekli yukarı it ────────────────────
        if (Cheats::MoonJump::Enabled) {
            bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            if (space) {
                Vector3 pos = ReadMemory<Vector3>(LocalPlayer.Pointer + 0x90);
                pos.z += Cheats::MoonJump::Force;
                WriteMemory<Vector3>(LocalPlayer.Pointer + 0x90, pos);
                uintptr_t navMJ = ReadMemory<uintptr_t>(LocalPlayer.Pointer + 0x30);
                if (navMJ > 0x10000ULL && navMJ < 0x7FFFFFFFF000ULL)
                    WriteMemory<float>(navMJ + 0x58, pos.z);
            }
        }

        // PendingAction — web menu'den gelen eylemler (teleport / kıyafet kopyala)
        if (g_pendingAction.type != PA_NONE) {
            int targetId  = g_pendingAction.targetId;
            PendingActionType act = g_pendingAction.type;
            g_pendingAction.type = PA_NONE;  // tek seferlik

            // Pointer'ı mutex içinde kopyala — UpdatePeds thread'i aynı anda
            // PedList'i yenilerse dangling pointer oluşmasın.
            uintptr_t targetPtr = 0;
            {
                std::lock_guard<std::mutex> lk(g_pedMutex);
                for (auto& p : PedList) {
                    if (p.GetId() == targetId) { targetPtr = p.Pointer; break; }
                }
            }

            if (targetPtr) {
                if (act == PA_TELEPORT_TO) {
                    Vector3 tpos = ReadMemory<Vector3>(targetPtr + 0x90);

                    // Hata 1 düzeltme: ReadMemory başarısız → tpos = {0,0,0} → void'e düşme.
                    // Haritanın geçerli koordinat aralığı GTA V'de yaklaşık ±10000.
                    bool posValid = (fabsf(tpos.x) > 0.1f || fabsf(tpos.y) > 0.1f) &&
                                    fabsf(tpos.x) < 12000.f &&
                                    fabsf(tpos.y) < 12000.f &&
                                    fabsf(tpos.z) > -1500.f;
                    if (posValid) {
                        tpos.z += 2.0f;  // zemin üstüne çıkmak için biraz daha marj
                        PositionTeleport(tpos);
                    }
                }
                else if (act == PA_COPY_OUTFIT) {
                    // CPedVariationData pointer: ped + 0x10C0 → ptr → components[12]
                    // Her bileşen 0x10 stride: drawableId(2) + textureId(2) + paletteId(2) + pad(10)
                    static const uintptr_t kVarOff = 0x10C0;
                    uintptr_t srcVar = ReadMemory<uintptr_t>(targetPtr + kVarOff);
                    uintptr_t dstVar = ReadMemory<uintptr_t>(LocalPlayer.Pointer + kVarOff);

                    if (srcVar > 0x10000ULL && srcVar < 0x7FFFFFFFF000ULL &&
                        dstVar > 0x10000ULL && dstVar < 0x7FFFFFFFF000ULL)
                    {
                        struct SlotData { uint16_t draw, tex, pal; };
                        SlotData slots[12] = {};

                        // Hata 2 düzeltme: eski limit (draw>1024, tex>256) modifiyeli
                        // FiveM sunucularında binlerce drawable olduğundan hep false dönüyordu.
                        // Gerçek üst sınır uint16_t max = 65535; geçersiz değer için 0xFFFF kontrolü yeterli.
                        bool anyValid = false;
                        for (int comp = 0; comp < 12; ++comp) {
                            slots[comp].draw = ReadMemory<uint16_t>(srcVar + (uintptr_t)comp * 0x10 + 0x00);
                            slots[comp].tex  = ReadMemory<uint16_t>(srcVar + (uintptr_t)comp * 0x10 + 0x02);
                            slots[comp].pal  = ReadMemory<uint16_t>(srcVar + (uintptr_t)comp * 0x10 + 0x04);

                            // FiveM addon/DLC kıyafetleri 0x1000 (4096)'ı aşan drawable ID'lere
                            // sahip olabilir. Bunlar server-streamed — yerel streaming pool'da
                            // bulunmayabilir, kopyalanırsa CStreamingManager geçersiz model
                            // yüklemeye çalışır ve crash olur. Bu bileşenleri default'a (0) çek.
                            if (slots[comp].draw >= 0x1000 || slots[comp].draw == 0xFFFF) {
                                slots[comp].draw = 0;
                                slots[comp].tex  = 0;
                                slots[comp].pal  = 0;
                            }
                            anyValid = true;
                        }
                        if (anyValid) {
                            for (int comp = 0; comp < 12; ++comp) {
                                uintptr_t dstSlot = dstVar + (uintptr_t)comp * 0x10;
                                WriteMemory<uint16_t>(dstSlot + 0x00, slots[comp].draw);
                                WriteMemory<uint16_t>(dstSlot + 0x02, slots[comp].tex);
                                WriteMemory<uint16_t>(dstSlot + 0x04, slots[comp].pal);
                            }
                            // Dirty flag yazılmıyor — GTA render thread kendi döngüsünde
                            // variation değişikliğini algılar. Yanlış offset yazmak
                            // struct'ı bozar ve crash'e yol açar.
                        }
                    }
                }
            }
        }

        Sleep(10);
    }
}
