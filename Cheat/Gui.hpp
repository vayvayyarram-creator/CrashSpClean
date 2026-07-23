// ============================================================
//  Gui.hpp  —  WinscreW  (White Theme, Clean Rows)
//  All widgets drawn with DrawList + InvisibleButton.
//  Zero qwe ButtonEx / SliderInt / BeginCombo calls in UI.
// ============================================================

static int g_page   = 0;   // 0=Aim 1=Players 2=Vehicles 3=World 4=Settings 5=Friends
static int g_aimSub = 0;   // 0=Silent 1=Aimbot 2=Triggerbot 3=Weapons
static int g_plrSub = 0;   // 0=Markers 1=Info 2=Status
static bool g_waitHK     = false;
static int* g_waitPtr    = nullptr;
static int  g_waitFrames = 0;   // skip initial click frame

// ---- Harmonious dark-navy palette ----
#define C_BG     IM_COL32( 13, 16, 23,255)   // content bg (deep navy)
#define C_BG2    IM_COL32( 19, 23, 33,255)   // section header
#define C_TXT1   IM_COL32(230,236,248,255)   // primary text
#define C_TXT2   IM_COL32(120,135,160,255)   // secondary
#define C_TXT3   IM_COL32( 60, 72, 95,255)   // muted
#define C_BDR    IM_COL32( 28, 35, 52,255)   // divider
#define C_HOV    IM_COL32( 22, 28, 42,255)   // row hover
#define C_SB     IM_COL32(  8, 11, 16,255)   // sidebar near-black
// Theme-aware accent colors — updated each frame by ApplyTheme()
static ImU32 C_ACT  = IM_COL32( 99,179,130,255);
static ImU32 C_ACTH = IM_COL32( 72,150,103,255);
static ImU32 C_GLOW = IM_COL32( 99,179,130, 30);
#define C_PILL   IM_COL32( 26, 33, 50,255)
#define C_TOFF   IM_COL32( 38, 46, 68,255)
#define C_WHITE  IM_COL32(255,255,255,255)

static void ApplyTheme(int idx) {
    switch (idx) {
        default:
        case 0: // Emerald
            C_ACT  = IM_COL32( 99,179,130,255);
            C_ACTH = IM_COL32( 72,150,103,255);
            C_GLOW = IM_COL32( 99,179,130, 30);
            break;
        case 1: // Ocean (logo-matching deep blue)
            C_ACT  = IM_COL32( 60,140,255,255);
            C_ACTH = IM_COL32( 40,105,210,255);
            C_GLOW = IM_COL32( 60,140,255, 30);
            break;
        case 2: // Rose
            C_ACT  = IM_COL32(220, 80,120,255);
            C_ACTH = IM_COL32(180, 55, 90,255);
            C_GLOW = IM_COL32(220, 80,120, 30);
            break;
    }
}

// Font helpers
#define F14 (Roboto      ? Roboto      : ImGui::GetDefaultFont())
#define F11 (RobotoSmall ? RobotoSmall : ImGui::GetDefaultFont())
#define F12 (Roboto2     ? Roboto2     : (Roboto ? Roboto : ImGui::GetDefaultFont()))

// ============================================================
//  PNG Texture Loader  (WIC from memory — no external files)
// ============================================================
#include <wincodec.h>
#include <shlwapi.h>
#include "Assets.hpp"

static ID3D11ShaderResourceView* g_texLogo     = nullptr;
static ID3D11ShaderResourceView* g_texCombat   = nullptr;
static ID3D11ShaderResourceView* g_texPvp      = nullptr;
static ID3D11ShaderResourceView* g_texVisuals  = nullptr;
static ID3D11ShaderResourceView* g_texSettings = nullptr;
static int g_texLogoW = 0, g_texLogoH = 0;
// one entry per nav page  (Aim, Players, Vehicles, World, Settings, Friends)
static ID3D11ShaderResourceView* g_navIcons[6] = {};

// Load PNG from in-memory byte array using WIC IStream
static ID3D11ShaderResourceView* LoadTextureMem(
    ID3D11Device* pDev,
    const unsigned char* data, int dataLen,
    int* outW = nullptr, int* outH = nullptr)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&fac)))
        return nullptr;

    // Wrap raw bytes in a COM IStream
    IStream* stream = SHCreateMemStream(data, (UINT)dataLen);
    if (!stream) { fac->Release(); return nullptr; }

    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(fac->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnLoad, &dec)))
    { stream->Release(); fac->Release(); return nullptr; }

    IWICBitmapFrameDecode* frame = nullptr;
    dec->GetFrame(0, &frame);

    IWICFormatConverter* conv = nullptr;
    fac->CreateFormatConverter(&conv);
    conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut);

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (outW) *outW = (int)w;
    if (outH) *outH = (int)h;

    std::vector<BYTE> px(w * h * 4);
    conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data());

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = { px.data(), w * 4, 0 };

    ID3D11Texture2D* tex = nullptr;
    pDev->CreateTexture2D(&td, &sd, &tex);
    ID3D11ShaderResourceView* srv = nullptr;
    if (tex) { pDev->CreateShaderResourceView(tex, nullptr, &srv); tex->Release(); }

    conv->Release(); frame->Release(); dec->Release();
    stream->Release(); fac->Release();
    return srv;
}

// ============================================================
//  Widget Helpers — all return void, advance cursor by rh
// ============================================================

static void W_Toggle(const char* lbl, const char* /*desc*/, bool* v) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;
    float rh = 40.f;
    ImVec2 p  = ImGui::GetCursorScreenPos();

    bool hov = ImGui::IsMouseHoveringRect(p, {p.x+rw, p.y+rh});
    if (hov) dl->AddRectFilled(p, {p.x+rw, p.y+rh}, C_HOV);
    dl->AddLine({p.x+12, p.y+rh-1}, {p.x+rw-12, p.y+rh-1}, C_BDR);

    dl->AddText(F14, 14.f, {p.x+16, p.y+13}, C_TXT1, lbl);

    // Pill toggle — animated knob
    float TW=40.f, TH=22.f;
    float tx=p.x+rw-TW-16.f, ty=p.y+(rh-TH)*0.5f;
    // Lerp anim state per-pointer using ImGui storage
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID sid = ImGui::GetID((void*)v);
    float animT = st->GetFloat(sid, *v ? 1.f : 0.f);
    float target = *v ? 1.f : 0.f;
    animT = animT + (target - animT) * ImMin(ImGui::GetIO().DeltaTime * 12.f, 1.f);
    st->SetFloat(sid, animT);
    auto lerpU32 = [](ImU32 a, ImU32 b, float t) -> ImU32 {
        int ar=(a)&0xFF, ag=(a>>8)&0xFF, ab2=(a>>16)&0xFF, aa=(a>>24)&0xFF;
        int br=(b)&0xFF, bg=(b>>8)&0xFF, bb2=(b>>16)&0xFF, ba=(b>>24)&0xFF;
        return IM_COL32(ar+(int)((br-ar)*t),ag+(int)((bg-ag)*t),ab2+(int)((bb2-ab2)*t),aa+(int)((ba-aa)*t));
    };
    ImU32 pillCol = lerpU32(C_TOFF, C_ACT, animT);
    dl->AddRectFilled({tx,ty},{tx+TW,ty+TH}, pillCol, TH*0.5f);
    float kx = tx + TH*0.5f + animT*(TW-TH);
    dl->AddCircleFilled({kx, ty+TH*0.5f}, TH*0.5f-2.f, C_WHITE, 16);

    ImGui::SetCursorScreenPos(p);
    ImGui::PushID((void*)v);
    if (ImGui::InvisibleButton("##t", {rw, rh})) *v = !*v;
    ImGui::PopID();
}

static void W_Slider(const char* lbl, const char* /*desc*/, int* v, int mn, int mx, const char* fmt="%d") {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;
    float rh = 54.f;
    ImVec2 p  = ImGui::GetCursorScreenPos();

    bool hov = ImGui::IsMouseHoveringRect(p, {p.x+rw, p.y+rh});
    if (hov) dl->AddRectFilled(p, {p.x+rw, p.y+rh}, C_HOV);
    dl->AddLine({p.x+12, p.y+rh-1}, {p.x+rw-12, p.y+rh-1}, C_BDR);

    char val[32]; snprintf(val, 32, fmt, *v);
    dl->AddText(F14, 14.f, {p.x+16, p.y+8}, C_TXT1, lbl);
    float vw = F14->CalcTextSizeA(13.f, FLT_MAX, 0.f, val).x;
    dl->AddText(F14, 13.f, {p.x+rw-vw-16, p.y+8}, C_TXT1, val);

    float slx=p.x+16, sly=p.y+38, slw=rw-32;
    float ratio = (mx>mn) ? ImClamp((float)(*v-mn)/(float)(mx-mn), 0.f, 1.f) : 0.f;
    dl->AddRectFilled({slx,sly-2},{slx+slw,sly+2}, C_TOFF, 2.f);
    if (ratio > 0.f) dl->AddRectFilled({slx,sly-2},{slx+ratio*slw,sly+2}, C_ACT, 2.f);
    float kx = slx + ratio*slw;
    dl->AddCircleFilled({kx,sly}, 8.f, C_WHITE, 16);
    dl->AddCircle({kx,sly}, 8.f, C_ACT, 16, 1.5f);

    ImGui::SetCursorScreenPos(p);
    ImGui::PushID((void*)v);
    ImGui::InvisibleButton("##sl", {rw, rh});
    if (ImGui::IsItemActive()) {
        float mx2 = ImGui::GetIO().MousePos.x;
        float r = ImClamp((mx2-slx)/slw, 0.f, 1.f);
        *v = mn + (int)((float)(mx-mn)*r + 0.5f);
    }
    ImGui::PopID();
}

static void W_Buttons(const char* lbl, const char* /*desc*/, int* v,
                      const char* const* btns, int n) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;
    float rh = 42.f;
    ImVec2 p  = ImGui::GetCursorScreenPos();

    bool hov = ImGui::IsMouseHoveringRect(p, {p.x+rw, p.y+rh});
    if (hov) dl->AddRectFilled(p, {p.x+rw, p.y+rh}, C_HOV);
    dl->AddLine({p.x+12, p.y+rh-1}, {p.x+rw-12, p.y+rh-1}, C_BDR);

    dl->AddText(F14, 14.f, {p.x+16, p.y+13}, C_TXT1, lbl);

    const float BH=24.f, GAP=3.f;
    // Pre-compute widths
    float bw[16]; float totalW=0.f;
    for (int i=0;i<n&&i<16;++i) {
        bw[i] = F12->CalcTextSizeA(12.f,FLT_MAX,0.f,btns[i]).x + 20.f;
        totalW += bw[i] + (i<n-1?GAP:0.f);
    }
    float bx=p.x+rw-totalW-16.f, by=p.y+(rh-BH)*0.5f;
    float startX=bx;
    for (int i=0;i<n&&i<16;++i) {
        bool sel=(*v==i);
        dl->AddRectFilled({bx,by},{bx+bw[i],by+BH}, sel?C_ACT:C_PILL, 12.f);
        float tw2=F12->CalcTextSizeA(12.f,FLT_MAX,0.f,btns[i]).x;
        dl->AddText(F12, 12.f, {bx+(bw[i]-tw2)*0.5f, by+(BH-12.f)*0.5f},
                    sel?C_WHITE:C_TXT1, btns[i]);
        bx += bw[i]+GAP;
    }

    ImGui::SetCursorScreenPos(p);
    ImGui::PushID((void*)v);
    ImGui::InvisibleButton("##bg", {rw, rh});
    if (ImGui::IsItemClicked()) {
        float mx2=ImGui::GetIO().MousePos.x;
        float bx2=startX;
        for (int i=0;i<n&&i<16;++i) {
            if (mx2>=bx2 && mx2<bx2+bw[i]) { *v=i; break; }
            bx2+=bw[i]+GAP;
        }
    }
    ImGui::PopID();
}

static void W_Hotkey(const char* lbl, const char* /*desc*/, int* key) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;
    float rh = 40.f;
    ImVec2 p  = ImGui::GetCursorScreenPos();
    bool waiting = (g_waitHK && g_waitPtr==key);

    bool hov = ImGui::IsMouseHoveringRect(p, {p.x+rw, p.y+rh});
    if (hov) dl->AddRectFilled(p, {p.x+rw, p.y+rh}, C_HOV);
    dl->AddLine({p.x+12, p.y+rh-1}, {p.x+rw-12, p.y+rh-1}, C_BDR);

    dl->AddText(F14, 14.f, {p.x+16, p.y+13}, C_TXT1, lbl);

    const char* kl = waiting ? "..." : KeyName(*key);
    float kw = F12->CalcTextSizeA(12.f,FLT_MAX,0.f,kl).x;
    float bw2=kw+24.f, bh2=24.f;
    float bx=p.x+rw-bw2-16.f, by=p.y+(rh-bh2)*0.5f;
    dl->AddRectFilled({bx,by},{bx+bw2,by+bh2}, waiting?C_ACT:C_PILL, 12.f);
    dl->AddText(F12, 12.f, {bx+(bw2-kw)*0.5f,by+(bh2-12.f)*0.5f},
                waiting?C_WHITE:C_TXT1, kl);

    ImGui::SetCursorScreenPos(p);
    ImGui::PushID((void*)key);
    ImGui::InvisibleButton("##hk", {rw, rh});
    if (ImGui::IsItemClicked()) { g_waitHK=true; g_waitPtr=key; g_waitFrames=3; }
    ImGui::PopID();

    if (waiting) {
        if (g_waitFrames > 0) { --g_waitFrames; return; }
        // Mouse buttons — check release to avoid self-capture
        if (ImGui::GetIO().MouseReleased[0]) { *key=VK_LBUTTON; g_waitHK=false; g_waitPtr=nullptr; return; }
        if (ImGui::GetIO().MouseReleased[1]) { *key=VK_RBUTTON; g_waitHK=false; g_waitPtr=nullptr; return; }
        if (ImGui::GetIO().MouseReleased[2]) { *key=VK_MBUTTON; g_waitHK=false; g_waitPtr=nullptr; return; }
        // Mouse 4/5 (X buttons)
        if (GetAsyncKeyState(0x05)&1) { *key=0x05; g_waitHK=false; g_waitPtr=nullptr; return; }
        if (GetAsyncKeyState(0x06)&1) { *key=0x06; g_waitHK=false; g_waitPtr=nullptr; return; }
        // Keyboard
        for (int vk=7;vk<256;++vk) {
            if (GetAsyncKeyState(vk)&1) {
                *key=(vk==VK_ESCAPE)?0:vk;
                g_waitHK=false; g_waitPtr=nullptr; break;
            }
        }
    }
}

static void W_Color(const char* lbl, const char* desc, ImColor* c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;
    float rh = 50.f;
    ImVec2 p  = ImGui::GetCursorScreenPos();

    bool hov = ImGui::IsMouseHoveringRect(p, {p.x+rw, p.y+rh});
    if (hov) dl->AddRectFilled(p, {p.x+rw, p.y+rh}, C_HOV);
    dl->AddLine({p.x+12, p.y+rh-1}, {p.x+rw-12, p.y+rh-1}, C_BDR);

    dl->AddText(F14, 14.f, {p.x+16, p.y+(rh-14.f)*0.5f}, C_TXT1, lbl);

    // Manuel renk swatch — DrawList ile ciz
    const float SW=28.f, SH=18.f;
    float sx = p.x+rw-SW-16.f, sy = p.y+(rh-SH)*0.5f;
    dl->AddRectFilled({sx,sy},{sx+SW,sy+SH}, (ImU32)*c, 4.f);
    dl->AddRect({sx,sy},{sx+SW,sy+SH}, IM_COL32(80,90,110,255), 4.f, 0, 1.f);

    // Satira invisible button — tiklama algilama
    ImGui::SetCursorScreenPos(p);
    ImGui::PushID((void*)c);
    if (ImGui::InvisibleButton("##col", {rw, rh})) {
        ImGui::OpenPopup("##cpop");
    }
    // Renk secici popup
    ImGui::SetNextWindowPos({sx - 200.f, sy + SH + 4.f}, ImGuiCond_Always);
    if (ImGui::BeginPopup("##cpop",
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::ColorPicker4("##pk", (float*)c,
            ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoSmallPreview);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void W_Section(const char* lbl) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;
    float rh = 26.f;
    ImVec2 p  = ImGui::GetCursorScreenPos();

    // Background with subtle gradient feel
    dl->AddRectFilled(p, {p.x+rw, p.y+rh}, IM_COL32(16,20,30,255));
    // Left accent bar
    dl->AddRectFilled(p, {p.x+3.f, p.y+rh}, C_ACT);
    // Glow from left bar
    {
        ImU32 ca = C_ACT;
        ImU32 g1 = (ca & 0x00FFFFFFu) | 0x1A000000u;
        ImU32 g2 = (ca & 0x00FFFFFFu) | 0x0A000000u;
        dl->AddRectFilled({p.x+3.f,p.y},{p.x+60.f,p.y+rh}, g1);
        dl->AddRectFilled({p.x+3.f,p.y},{p.x+30.f,p.y+rh}, g2);
    }
    // Bottom divider
    dl->AddLine({p.x, p.y+rh-1.f}, {p.x+rw, p.y+rh-1.f}, IM_COL32(255,255,255,6));

    // Label
    ImFont* fs = F11 ? F11 : ImGui::GetDefaultFont();
    float ty = p.y + (rh - 11.f) * 0.5f;
    dl->AddText(fs, 11.f, {p.x + 12.f, ty},
                IM_COL32(140,160,150,255), lbl);

    ImGui::SetCursorScreenPos({p.x, p.y+rh});
    ImGui::Dummy({rw, 0.f});
}

// ============================================================
//  Tab bar — draws pill tabs, returns true if switched
// ============================================================
static void TabRow(const char* const* tabs, int n, int& cur,
                   ImVec2 basePos, float totalW) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float TH=28.f, TPAD=20.f, GAP=4.f;
    float x = basePos.x + 12.f;
    float y = basePos.y + 11.f;

    for (int i=0;i<n;++i) {
        float tw = F12->CalcTextSizeA(12.f,FLT_MAX,0.f,tabs[i]).x;
        float bw2 = tw + TPAD;
        bool sel = (cur==i);
        ImU32 bg = sel ? C_ACT : C_PILL;
        ImU32 tc = sel ? C_WHITE : C_TXT2;
        dl->AddRectFilled({x,y},{x+bw2,y+TH}, bg, 14.f);
        dl->AddText(F12, 12.f, {x+(bw2-tw)*0.5f, y+(TH-12.f)*0.5f}, tc, tabs[i]);

        // InvisibleButton for click (must be inside a window)
        ImGui::SetCursorScreenPos({x, y});
        char id[32]; snprintf(id,32,"##tab%d",i);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton(id, {bw2, TH})) cur=i;
        ImGui::PopID();

        x += bw2 + GAP;
    }
}

// ============================================================
//  Aim Assist page
// ============================================================
static void PageAim(float cw, float ch) {
    ImVec2 ctPos = ImGui::GetWindowPos();

    static const char* tabs[] = {"Silent Aim","Magic Bullet","Aimbot","Triggerbot","Weapons"};
    TabRow(tabs, 5, g_aimSub, {ctPos.x, ctPos.y}, cw);

    static const char* boneBtns[] = {"Head","Torso","Legs","Random"};
    static const char* aimModeBtns[] = {"Camera","Mouse"};
    static const char* prioBtns[] = {"Crosshair","Distance","Low HP"};

    ImGui::PushID(0xA1);
    ImGui::SetCursorScreenPos({ctPos.x, ctPos.y + 52.f});
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch - 52.f}, false, ImGuiWindowFlags_NoScrollbar);

    if (g_aimSub == 0) {  // ---- Silent Aim ----
        W_Toggle  ("Silent Aim", "", &Cheats::AimAssist::Silent::Enabled);
        W_Hotkey  ("Hotkey",     "", &Cheats::AimAssist::Silent::HotKey);
        W_Slider  ("FOV",           "", &Cheats::AimAssist::Silent::Fov, 1, 1000, "%dpx");
        W_Slider  ("Max Distance",  "", &Cheats::AimAssist::Silent::MaxDistance, 10, 600, "%dm");
        W_Slider  ("Miss Chance",   "", &Cheats::AimAssist::Silent::MissChance, 0, 100, "%d%%");
        W_Section ("HEDEF BOLGESI");
        W_Buttons ("Target Part", "", &Cheats::AimAssist::Silent::BoneMode, boneBtns, 4);
        W_Section ("FILTRE");
        W_Toggle  ("Duvar Arkasini Atla", "", &Cheats::AimAssist::Silent::VisCheck);
        W_Toggle  ("Arkadas Koruma",      "", &Cheats::AimAssist::Silent::SkipFriends);
        W_Section ("FOV CEMBERI");
        W_Toggle  ("Show FOV",   "", &Cheats::AimAssist::Silent::DrawFov);
        W_Color   ("FOV Color",  "", &Cheats::AimAssist::Silent::FovColor);

    } else if (g_aimSub == 1) {  // ---- Magic Bullet ----
        W_Toggle  ("Magic Bullet",   "", &Cheats::AimAssist::MagicBullet::Enabled);
        W_Slider  ("Max Distance",   "", &Cheats::AimAssist::MagicBullet::MaxDistance, 10, 800, "%dm");
        W_Section ("HEDEF BOLGESI");
        W_Buttons ("Target Part",    "", &Cheats::AimAssist::MagicBullet::BoneMode, boneBtns, 4);
        W_Section ("FILTRE");
        W_Toggle  ("Arkadas Koruma", "", &Cheats::AimAssist::MagicBullet::SkipFriends);
        W_Toggle  ("NPC Atla",       "", &Cheats::AimAssist::MagicBullet::SkipNPC);

    } else if (g_aimSub == 2) {  // ---- Aimbot ----
        W_Toggle  ("Aimbot",     "", &Cheats::AimAssist::Aimbot::Enabled);
        W_Hotkey  ("Hotkey",     "", &Cheats::AimAssist::Aimbot::HotKey);
        W_Toggle  ("Sticky Aim", "", &Cheats::AimAssist::Aimbot::StickyAim);
        W_Slider  ("FOV",        "", &Cheats::AimAssist::Aimbot::Fov, 1, 500, "%dpx");
        W_Slider  ("Smoothing",  "", &Cheats::AimAssist::Aimbot::Smooth, 1, 30, "%.0f");
        W_Buttons ("Target Part","", &Cheats::AimAssist::Aimbot::BoneMode, boneBtns, 4);
        W_Buttons ("Aim Mode",   "", &Cheats::AimAssist::Aimbot::AimMode, aimModeBtns, 2);
        W_Buttons ("Priority",   "", &Cheats::AimAssist::Aimbot::Priority, prioBtns, 3);
        W_Section ("FILTRE");
        W_Toggle  ("Duvar Arkasini Atla", "", &Cheats::AimAssist::Aimbot::VisCheck);
        W_Toggle  ("Arkadas Koruma",      "", &Cheats::AimAssist::Aimbot::SkipFriends);
        W_Section ("FOV CEMBERI");
        W_Toggle  ("Show FOV",   "", &Cheats::AimAssist::Aimbot::DrawFov);
        W_Color   ("FOV Color",  "", &Cheats::AimAssist::Aimbot::FovColor);

    } else if (g_aimSub == 3) {  // ---- Triggerbot ----
        W_Toggle  ("Triggerbot",     "", &Cheats::AimAssist::Triggerbot::Enabled);
        W_Hotkey  ("Hotkey",         "", &Cheats::AimAssist::Triggerbot::HotKey);
        W_Slider  ("Delay (ms)",     "", &Cheats::AimAssist::Triggerbot::Delay, 1, 300, "%dms");
        W_Slider  ("Max Distance",   "", &Cheats::AimAssist::Triggerbot::MaxDistance, 10, 600, "%dm");
        W_Section ("FILTRE");
        W_Toggle  ("Arkadas Koruma", "", &Cheats::AimAssist::Triggerbot::SkipFriends);

    } else if (g_aimSub == 4) {  // ---- Weapons ----
        W_Section ("SILAH MODLARI");
        W_Toggle  ("Infinite Ammo",  "", &Cheats::AimAssist::Settings::InfiniteAmmo);
        W_Toggle  ("No Recoil",      "", &Cheats::AimAssist::Settings::NoRecoil);
        W_Toggle  ("No Spread",      "", &Cheats::AimAssist::Settings::NoSpread);
        W_Toggle  ("No Reload",      "", &Cheats::AimAssist::Settings::NoReload);
        W_Section ("NISAN");
        W_Toggle  ("Crosshair",            "", &Cheats::AimAssist::Settings::Crosshair);
        if (Cheats::AimAssist::Settings::Crosshair) {
            static const char* chBtns[] = {"Cizgi","Nokta","Cerceve","Cember","Artı","X","Kare","Ok","Yildiz","Elmas"};
            W_Buttons ("Tip",              "", &Cheats::AimAssist::Settings::CrosshairSelectedType, chBtns, 10);
            W_Color   ("Renk",             "", &Cheats::AimAssist::Settings::CrosshairColor);
            W_Slider  ("Boyut",            "", &Cheats::AimAssist::Settings::CrosshairSize, 5, 40);
            W_Toggle  ("Dinamik Renk",     "", &Cheats::AimAssist::Settings::DynamicCrosshairColor);
        }
    }

    ImGui::EndChild();
    ImGui::PopID();
}

// ============================================================
//  Players page
// ============================================================
static void PagePlayers(float cw, float ch) {
    ImVec2 ctPos = ImGui::GetWindowPos();
    static const char* tabs[] = {"ESP Markers","Player Info","Status Bars"};
    TabRow(tabs, 3, g_plrSub, {ctPos.x, ctPos.y}, cw);

    static const char* locBtns[] = {"Above","Below"};
    static const char* boxBtns[]  = {"Corner","Full","3D"};
    static const char* lineBtns[] = {"Top","Center","Bottom"};

    ImGui::PushID(0xA2);
    ImGui::SetCursorScreenPos({ctPos.x, ctPos.y + 52.f});
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch - 52.f}, false, ImGuiWindowFlags_NoScrollbar);

    if (g_plrSub == 0) {  // ---- ESP Markers ----
        W_Section ("DUVAR ARKASI KONTROLU");
        W_Toggle  ("Wall Check",      "", &Cheats::Players::VisCheck::Enabled);
        W_Color   ("Gorunen Renk",    "", &Cheats::Players::VisCheck::VisibleColor);
        W_Color   ("Duvar Arkasi",    "", &Cheats::Players::VisCheck::HiddenColor);
        W_Section ("ISKELET & KUTU");
        W_Toggle  ("Draw Skeleton",   "", &Cheats::Players::VisualMarkers::DrawSkeleton::Enabled);
        W_Color   ("Skeleton Color",  "", &Cheats::Players::VisualMarkers::DrawSkeleton::Color);
        W_Toggle  ("Draw Box",        "", &Cheats::Players::VisualMarkers::DrawBox::Enabled);
        W_Color   ("Box Color",       "", &Cheats::Players::VisualMarkers::DrawBox::Color);
        W_Buttons ("Box Type",        "", &Cheats::Players::VisualMarkers::DrawBox::SelectedType, boxBtns, 3);
        W_Section ("SNAP LINE");
        W_Toggle  ("Draw Snap Line",  "", &Cheats::Players::VisualMarkers::DrawLine::Enabled);
        W_Color   ("Line Color",      "", &Cheats::Players::VisualMarkers::DrawLine::Color);
        W_Buttons ("Line Origin",     "", &Cheats::Players::VisualMarkers::DrawLine::SelectedLocation, lineBtns, 3);
        W_Section ("KEMIK NOKTALARI");
        W_Toggle  ("Bone Points",     "", &Cheats::Players::VisualMarkers::DrawBonePoints::Enabled);
        W_Color   ("Nokta Rengi",     "", &Cheats::Players::VisualMarkers::DrawBonePoints::Color);
        W_Slider  ("Nokta Boyutu",    "", &Cheats::Players::VisualMarkers::DrawBonePoints::Radius, 1, 8);
        W_Section ("FILTRE");
        W_Toggle  ("Ignore NPCs",     "", &Cheats::Players::Settings::IgnorePed);
        W_Toggle  ("Ignore Dead",     "", &Cheats::Players::Settings::IgnoreDeath);
        W_Slider  ("Max Distance",    "", &Cheats::Players::Settings::MaxDistance, 10, 600, "%dm");

    } else if (g_plrSub == 1) {  // ---- Player Info ----
        W_Section ("ISIM & ID");
        W_Toggle  ("Show Name",       "", &Cheats::Players::PlayerInfo::DrawName::Enabled);
        W_Color   ("Name Color",      "", &Cheats::Players::PlayerInfo::DrawName::Color);
        W_Toggle  ("Show ID",         "", &Cheats::Players::PlayerInfo::DrawId::Enabled);
        W_Color   ("ID Color",        "", &Cheats::Players::PlayerInfo::DrawId::Color);
        W_Section ("MESAFE & SILAH");
        W_Toggle  ("Show Distance",   "", &Cheats::Players::PlayerInfo::DrawDistance::Enabled);
        W_Color   ("Distance Color",  "", &Cheats::Players::PlayerInfo::DrawDistance::Color);
        W_Toggle  ("Show Weapon",     "", &Cheats::Players::PlayerInfo::DrawWeaponName::Enabled);
        W_Color   ("Weapon Color",    "", &Cheats::Players::PlayerInfo::DrawWeaponName::Color);
        W_Slider  ("Max Info Dist",   "", &Cheats::Players::PlayerInfo::GlobalSettings::MaxDistance, 10, 300, "%dm");

    } else {  // ---- Status Bars ----
        W_Section ("CANLILIK CUBUGU");
        W_Toggle  ("Health Bar",      "", &Cheats::Players::StatusBars::DrawHealthBar::Enabled);
        W_Buttons ("HP Location",     "", &Cheats::Players::StatusBars::DrawHealthBar::SelectedLocation, locBtns, 2);
        W_Section ("ZIRH CUBUGU");
        W_Toggle  ("Armor Bar",       "", &Cheats::Players::StatusBars::DrawArmorBar::Enabled);
        W_Buttons ("Armor Location",  "", &Cheats::Players::StatusBars::DrawArmorBar::SelectedLocation, locBtns, 2);
    }

    ImGui::EndChild();
    ImGui::PopID();
}

// ============================================================
//  Vehicles page
// ============================================================
static void PageVehicles(float cw, float ch) {
    ImVec2 ctPos = ImGui::GetWindowPos();
    ImGui::PushID(0xA3);
    ImGui::SetCursorScreenPos(ctPos);
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch}, false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy({0, 4.f});
    W_Section ("ARAC ESP");
    W_Toggle  ("Draw Point",    "", &Cheats::Vehicles::DrawPoint::Enabled);
    W_Color   ("Point Color",   "", &Cheats::Vehicles::DrawPoint::Color);
    W_Toggle  ("Draw Snap Line","", &Cheats::Vehicles::DrawLine::Enabled);
    W_Color   ("Line Color",    "", &Cheats::Vehicles::DrawLine::Color);
    W_Toggle  ("Show Distance", "", &Cheats::Vehicles::DrawDistance::Enabled);
    W_Toggle  ("Health Bar",    "", &Cheats::Vehicles::DrawHealthBar::Enabled);
    W_Section ("FILTRE");
    W_Toggle  ("Ignore Local",  "", &Cheats::Vehicles::Settings::IgnoreLocalVehicle);
    W_Slider  ("Max Count",     "", &Cheats::Vehicles::Settings::MaxVehicleCount, 10, 200);
    W_Slider  ("Max Distance",  "", &Cheats::Vehicles::Settings::MaxDistance, 50, 1000, "%dm");
    W_Section ("ARAC TAMIRI");
    W_Toggle  ("Vehicle Fix",   "", &Cheats::Vehicles::VehicleFix::Enabled);
    W_Hotkey  ("Fix Key",       "", &Cheats::Vehicles::VehicleFix::HotKey);
    ImGui::EndChild();
    ImGui::PopID();
}

// ============================================================
//  World page
// ============================================================
static void PageWorld(float cw, float ch) {
    ImVec2 ctPos = ImGui::GetWindowPos();
    ImGui::PushID(0xA4);
    ImGui::SetCursorScreenPos(ctPos);
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch}, false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy({0, 4.f});
    W_Section ("NOCLIP");
    W_Toggle  ("Enable NoClip", "", &Cheats::World::NoClip::Enabled);
    W_Slider  ("Speed",         "", &Cheats::World::NoClip::MovementSpeed, 1, 20);
    W_Section ("NOCLIP TUSLAR");
    W_Hotkey  ("Forward",  "", &Cheats::World::NoClip::ForwardKey);
    W_Hotkey  ("Backward", "", &Cheats::World::NoClip::BackwardKey);
    W_Hotkey  ("Up",       "", &Cheats::World::NoClip::UpKey);
    W_Hotkey  ("Down",     "", &Cheats::World::NoClip::DownKey);
    W_Section ("OYUNCU");
    W_Toggle  ("Semi God Mode",      "", &Cheats::World::SemiGodMode::Enabled);
    W_Toggle  ("Super Sprint",       "", &Cheats::World::SuperSprint::Enabled);
    W_Slider  ("Sprint Speed",       "", &Cheats::World::SuperSprint::Speed, 1, 20);
    W_Toggle  ("Infinite Stamina",   "", &Cheats::World::InfiniteStamina::Enabled);
    W_Section ("SILAH");
    W_Toggle  ("Explosive Bullets",  "", &Cheats::World::ExplosiveBullets::Enabled);
    W_Toggle  ("Fire Bullets",       "", &Cheats::World::FireBullets::Enabled);
    W_Toggle  ("Rapid Fire",         "", &Cheats::World::RapidFire::Enabled);
    ImGui::EndChild();
    ImGui::PopID();
}

// ============================================================
//  Settings page
// ============================================================
static void PageSettings(float cw, float ch) {
    ImVec2 ctPos = ImGui::GetWindowPos();
    ImGui::PushID(0xA5);
    ImGui::SetCursorScreenPos(ctPos);
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch}, false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy({0, 4.f});
    W_Section ("TEMA");
    {
        // 3 theme color swatches drawn inline
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        float trw = ImGui::GetContentRegionAvail().x;
        ImVec2 tp  = ImGui::GetCursorScreenPos();
        float  sw  = 36.f, sh = 28.f, gap = 10.f;
        float  startX = tp.x + (trw - (sw*3.f + gap*2.f)) * 0.5f;
        struct { ImU32 col; const char* name; } themes[] = {
            { IM_COL32( 99,179,130,255), "Emerald" },
            { IM_COL32( 60,140,255,255), "Ocean"   },
            { IM_COL32(220, 80,120,255), "Rose"    },
        };
        for (int ti = 0; ti < 3; ++ti) {
            float bx = startX + ti * (sw + gap);
            float by = tp.y + 6.f;
            bool sel = (Cheats::Settings::SelectedTheme == ti);
            tdl->AddRectFilled({bx,by},{bx+sw,by+sh}, themes[ti].col, 6.f);
            if (sel) tdl->AddRect({bx-2.f,by-2.f},{bx+sw+2.f,by+sh+2.f},
                                  IM_COL32(255,255,255,200), 7.f, 0, 2.f);
            ImGui::SetCursorScreenPos({bx,by});
            char tid[16]; snprintf(tid,16,"##thm%d",ti);
            if (ImGui::InvisibleButton(tid,{sw,sh}))
                Cheats::Settings::SelectedTheme = ti;
        }
        // Theme name label
        ImVec2 lp = {tp.x + (trw - 50.f)*0.5f, tp.y + sh + 10.f};
        tdl->AddText(F11, 11.f, lp, C_TXT2, themes[Cheats::Settings::SelectedTheme].name);
        ImGui::SetCursorScreenPos({tp.x, tp.y + sh + 26.f});
        ImGui::Dummy({trw, 0.f});
    }
    W_Section ("GENEL");
    W_Toggle  ("Stream Proof", "", &Cheats::Settings::StreamProof);
    W_Hotkey  ("Menu Key",     "", &Cheats::Settings::MenuKey);
    W_Hotkey  ("Panic Key",    "", &Cheats::Settings::PanicKey);
    W_Slider  ("Max Players",  "", &Cheats::Settings::MaxPlayerCount, 10, 1024);
    W_Section ("DURUM");
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float rw = ImGui::GetContentRegionAvail().x;
        ImVec2 p  = ImGui::GetCursorScreenPos();
        dl->AddText(F12, 12.f, {p.x+16, p.y+8},  C_TXT2, "Process:");
        bool ok = (Game.hProcess != nullptr);
        dl->AddText(F12, 12.f, {p.x+90, p.y+8},
                    ok ? C_ACT : IM_COL32(220,50,50,255),
                    ok ? "Attached" : "Not found");
        dl->AddText(F12, 12.f, {p.x+16, p.y+26}, C_TXT2, "Build:");
        dl->AddText(F12, 12.f, {p.x+90, p.y+26}, C_TXT1,
                    Game.Version.empty() ? "Detecting..." : Game.Version.c_str());
        dl->AddText(F12, 12.f, {p.x+16, p.y+44}, C_TXT2, "Players:");
        char pbuf[16]; snprintf(pbuf,16,"%d",(int)PedList.size());
        dl->AddText(F12, 12.f, {p.x+90, p.y+44}, C_TXT1, pbuf);
        ImGui::SetCursorScreenPos({p.x, p.y+64});
        ImGui::Dummy({rw, 0.f});
    }
    W_Section ("KONTROLLER");
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float rw = ImGui::GetContentRegionAvail().x;
        ImVec2 p  = ImGui::GetCursorScreenPos();
        dl->AddText(F11, 11.f, {p.x+16, p.y+8},  C_TXT3, "INSERT = Menuyu ac / kapat");
        dl->AddText(F11, 11.f, {p.x+16, p.y+22}, C_TXT3, "END    = Programdan cik");
        ImGui::SetCursorScreenPos({p.x, p.y+36});
        ImGui::Dummy({rw, 0.f});
    }
    W_Section ("CONFIG");
    {
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        float rw        = ImGui::GetContentRegionAvail().x;

        // ---- Active config header ----
        {
            ImVec2 hp = ImGui::GetCursorScreenPos();
            dl->AddText(F11, 11.f, {hp.x+16.f, hp.y+4.f}, C_TXT3, "Aktif:");
            dl->AddText(F12, 12.f, {hp.x+52.f, hp.y+3.f}, C_ACT,
                        ActiveConfigName().c_str());
            ImGui::SetCursorScreenPos({hp.x, hp.y+20.f});
            ImGui::Dummy({rw, 0.f});
        }

        // ---- New config: name input + save button ----
        static char s_cfgNewName[32] = "settings";
        {
            ImVec2 ip = ImGui::GetCursorScreenPos();
            float  iw = rw - 100.f;

            // Text input background
            bool ifoc = ImGui::IsMouseHoveringRect(ip, {ip.x+iw, ip.y+28.f});
            dl->AddRectFilled(ip, {ip.x+iw, ip.y+28.f},
                              IM_COL32(16,20,28,230), 5.f);
            dl->AddRect(ip, {ip.x+iw, ip.y+28.f},
                        ifoc ? C_ACT : IM_COL32(35,45,55,200), 5.f, 0, 1.f);
            ImGui::SetCursorScreenPos({ip.x+8.f, ip.y+6.f});
            ImGui::SetNextItemWidth(iw-12.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,  ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Text,      ImVec4(0.9f,0.92f,0.88f,1.f));
            ImGui::InputText("##cfgname", (ImTextureID)0, s_cfgNewName, sizeof(s_cfgNewName));
            ImGui::PopStyleColor(3);

            // Save button
            ImVec2 bp = {ip.x+iw+8.f, ip.y};
            float  bw2 = rw - iw - 10.f;
            bool   bhov = ImGui::IsMouseHoveringRect(bp, {bp.x+bw2, bp.y+28.f});
            dl->AddRectFilled(bp, {bp.x+bw2, bp.y+28.f},
                              bhov ? C_ACTH : IM_COL32(20,30,45,230), 5.f);
            dl->AddRect(bp, {bp.x+bw2, bp.y+28.f}, C_ACT, 5.f, 0, 1.f);
            {
                const char* lbl = "Kaydet";
                float tw = (F11?F11:ImGui::GetDefaultFont())->CalcTextSizeA(11.f,FLT_MAX,0.f,lbl).x;
                dl->AddText(F11, 11.f, {bp.x+(bw2-tw)*0.5f, bp.y+8.f},
                            bhov ? IM_COL32(255,255,255,255) : C_ACT, lbl);
            }
            ImGui::SetCursorScreenPos(bp);
            if (ImGui::InvisibleButton("##cfgSaveNew", {bw2, 28.f})) {
                std::string nm = s_cfgNewName;
                if (!nm.empty()) {
                    SaveConfig(CfgNamedPath(nm));
                    ActiveConfigName() = nm;
                    PushToast(("Kaydedildi: " + nm).c_str(), C_ACT, 2.f);
                }
            }
            ImGui::SetCursorScreenPos({ip.x, ip.y+34.f});
            ImGui::Dummy({rw, 0.f});
        }

        // ---- Config list ----
        static std::vector<std::string> s_cfgList;
        static DWORD s_cfgListTime = 0;
        DWORD now2 = GetTickCount();
        if (now2 - s_cfgListTime > 2000) { s_cfgList = ListConfigs(); s_cfgListTime = now2; }

        static std::string s_confirmCfg;  // which config is pending confirmation
        static bool s_showConfirm = false;

        float listH = ImMin(160.f, (float)s_cfgList.size() * 32.f + 8.f);
        if (s_cfgList.empty()) {
            ImVec2 ep = ImGui::GetCursorScreenPos();
            dl->AddText(F11, 11.f, {ep.x+16.f, ep.y+6.f}, C_TXT3, "Kayitli config yok.");
            ImGui::Dummy({rw, 24.f});
        } else {
            ImGui::BeginChild("", (ImTextureID)0, {rw, listH}, false, ImGuiWindowFlags_None);
            ImDrawList* dlL = ImGui::GetWindowDrawList();
            float lrw = ImGui::GetContentRegionAvail().x;

            for (auto& cfgName : s_cfgList) {
                ImVec2 rowP = ImGui::GetCursorScreenPos();
                float  rh   = 30.f;
                bool   isActive = (cfgName == ActiveConfigName());
                bool   rowHov   = ImGui::IsMouseHoveringRect(rowP, {rowP.x+lrw, rowP.y+rh});

                ImU32 rowBg = isActive ? IM_COL32(14,22,36,220) :
                              rowHov   ? IM_COL32(18,24,36,200) : IM_COL32(11,14,20,170);
                dlL->AddRectFilled(rowP, {rowP.x+lrw, rowP.y+rh}, rowBg, 4.f);

                // Active indicator
                if (isActive)
                    dlL->AddRectFilled(rowP, {rowP.x+3.f, rowP.y+rh}, C_ACT, 2.f);

                // File icon pill
                dlL->AddRectFilled({rowP.x+8.f,rowP.y+7.f},{rowP.x+22.f,rowP.y+23.f},
                                   IM_COL32(22,30,45,200), 3.f);
                dlL->AddText(F11, 10.f, {rowP.x+10.f,rowP.y+10.f}, C_TXT3, "cfg");

                // Config name
                ImU32 ncol = isActive ? C_ACT : (rowHov ? C_TXT1 : C_TXT2);
                dlL->AddText(F12, 12.f, {rowP.x+28.f, rowP.y+9.f}, ncol, cfgName.c_str());

                dlL->AddLine({rowP.x+6.f,rowP.y+rh-1.f},{rowP.x+lrw-6.f,rowP.y+rh-1.f},
                             IM_COL32(255,255,255,6));

                ImGui::SetCursorScreenPos(rowP);
                std::string btnId = "##cfgrow" + cfgName;
                ImGui::InvisibleButton(btnId.c_str(), {lrw, rh});
                if (ImGui::IsItemClicked()) {
                    s_confirmCfg  = cfgName;
                    s_showConfirm = true;
                    ImGui::OpenPopup("##cfgConfirm");
                }
            }
            ImGui::EndChild();
        }

        // ---- Load confirmation popup ----
        ImGui::SetNextWindowSize({220.f, 90.f});
        if (ImGui::BeginPopup("##cfgConfirm")) {
            ImGui::PushFont(F12);
            char confirmMsg[64];
            snprintf(confirmMsg, sizeof(confirmMsg), "\"%s\" yuklensin mi?", s_confirmCfg.c_str());
            ImGui::TextWrapped("%s", confirmMsg);
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetWindowWidth()-110.f)*0.5f);
            if (ImGui::Button("Evet", {50.f, 22.f})) {
                LoadConfig(CfgNamedPath(s_confirmCfg));
                ActiveConfigName() = s_confirmCfg;
                PushToast(("Yuklendi: " + s_confirmCfg).c_str(),
                          IM_COL32(80,180,255,255), 2.f);
                s_showConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 10.f);
            if (ImGui::Button("Iptal", {50.f, 22.f})) {
                s_showConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopFont();
            ImGui::EndPopup();
        }

        ImGui::Dummy({rw, 4.f});
    }
    ImGui::EndChild();
    ImGui::PopID();
}

// ============================================================
//  Friends / Player list page
// ============================================================
static void PageFriends(float cw, float ch) {
    ImVec2 ctPos = ImGui::GetWindowPos();
    ImGui::PushID(0xA6);
    ImGui::SetCursorScreenPos(ctPos);
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch}, false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy({0, 4.f});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rw = ImGui::GetContentRegionAvail().x;

    // ---- Search bar ----
    static char s_search[48] = {};
    {
        ImVec2 sp = ImGui::GetCursorScreenPos();
        float  sw2 = rw - 24.f;
        float  sh2 = 28.f;
        float  sx  = sp.x + 12.f;
        bool   sfoc = ImGui::IsMouseHoveringRect({sx,sp.y},{sx+sw2,sp.y+sh2});
        dl->AddRectFilled({sx,sp.y},{sx+sw2,sp.y+sh2}, IM_COL32(16,20,28,230), 6.f);
        dl->AddRect      ({sx,sp.y},{sx+sw2,sp.y+sh2},
                          sfoc ? C_ACT : IM_COL32(35,45,55,200), 6.f, 0, 1.f);
        // magnifier icon
        dl->AddCircle({sx+13.f,sp.y+14.f}, 5.f, C_TXT3, 12, 1.f);
        dl->AddLine({sx+17.f,sp.y+18.f},{sx+20.f,sp.y+21.f}, C_TXT3, 1.5f);
        // placeholder / input text
        ImGui::SetCursorScreenPos({sx+26.f, sp.y+5.f});
        ImGui::SetNextItemWidth(sw2-30.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,      ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.88f,0.9f,0.85f,1.f));
        ImGui::InputText("##plrsearch", (ImTextureID)0, s_search, sizeof(s_search));
        ImGui::PopStyleColor(3);
        if (s_search[0] == '\0') {
            ImVec2 hint = {sx+28.f, sp.y+8.f};
            dl->AddText(F11, 11.f, hint, C_TXT3, "ID veya isim ara...");
        }
        ImGui::SetCursorScreenPos({sp.x, sp.y+sh2+4.f});
        ImGui::Dummy({rw, 0.f});
    }

    W_Section("OYUNCULAR");

    // Stats header
    {
        ImVec2 hp = ImGui::GetCursorScreenPos();
        char stat[48];
        int total  = (int)PedList.size();
        int fcount = (int)std::count_if(friendList.begin(), friendList.end(),
                       [](const Friend& f){ return f.id > 0; });
        snprintf(stat, sizeof(stat), "%d oyuncu  |  %d arkadas", total, fcount);
        dl->AddText(F11, 11.f, {hp.x+16.f, hp.y+2.f}, C_TXT3, stat);
        ImGui::SetCursorScreenPos({hp.x, hp.y+18.f});
        ImGui::Dummy({rw, 0.f});
    }

    // ---- Player list (scrollable) ----
    float listH = ch - ImGui::GetCursorScreenPos().y + ctPos.y - 4.f;
    ImGui::BeginChild("", (ImTextureID)0, {rw, listH}, false, ImGuiWindowFlags_None);
    ImDrawList* dlL = ImGui::GetWindowDrawList();
    float lrw = ImGui::GetContentRegionAvail().x;

    static int  s_ctxId     = -1;   // right-click target
    static int  s_renameId  = -1;
    static char s_renameBuf[32] = {};

    // Build filtered list
    std::string srch = s_search;
    std::transform(srch.begin(), srch.end(), srch.begin(), ::tolower);

    bool anyShown = false;
    for (auto& ped : PedList) {
        int         pedId    = ped.GetId();
        std::string nm       = ped.GetName();
        bool        isFriend = IsFriend(pedId);
        bool        hasAlias = g_playerAliases.count(pedId) && !g_playerAliases[pedId].empty();
        std::string alias    = hasAlias ? g_playerAliases[pedId] : "";
        std::string dispName = !alias.empty() ? alias : (nm.empty() ? ("P#"+std::to_string(pedId)) : nm);

        // Filter
        if (!srch.empty()) {
            std::string idStr  = std::to_string(pedId);
            std::string nmLow  = dispName; std::transform(nmLow.begin(),nmLow.end(),nmLow.begin(),::tolower);
            if (idStr.find(srch) == std::string::npos && nmLow.find(srch) == std::string::npos)
                continue;
        }
        anyShown = true;

        ImVec2 rowP = ImGui::GetCursorScreenPos();
        float  rh   = 36.f;

        // Row bg
        bool rowHov = ImGui::IsMouseHoveringRect(rowP, {rowP.x+lrw, rowP.y+rh});
        ImU32 friendAccent = (C_ACT & 0x00FFFFFFu) | 0x28000000u;
        ImU32 rowBg = isFriend ? IM_COL32(14,26,18,230) :
                      (rowHov ? IM_COL32(20,26,34,200) : IM_COL32(13,16,22,170));
        dlL->AddRectFilled(rowP, {rowP.x+lrw, rowP.y+rh}, rowBg, 4.f);
        if (isFriend) {
            // Green left stripe + border for friends
            ImU32 fc = (C_ACT & 0x00FFFFFFu) | 0x60000000u;
            dlL->AddRectFilled(rowP, {rowP.x+3.f, rowP.y+rh}, C_ACT, 2.f);
            dlL->AddRect(rowP, {rowP.x+lrw, rowP.y+rh}, fc, 4.f, 0, 1.f);
        }

        // ID badge (pill)
        char idBuf[10]; snprintf(idBuf, 10, "#%d", pedId);
        float idW = (F11?F11:ImGui::GetDefaultFont())->CalcTextSizeA(10.f,FLT_MAX,0.f,idBuf).x + 8.f;
        dlL->AddRectFilled({rowP.x+6.f, rowP.y+9.f},{rowP.x+6.f+idW, rowP.y+9.f+16.f},
                           IM_COL32(22,28,38,200), 4.f);
        dlL->AddText(F11, 10.f, {rowP.x+10.f, rowP.y+12.f}, C_TXT3, idBuf);

        // Name / alias
        ImU32 nameCol = hasAlias ? IM_COL32(255,220,80,255) :
                        isFriend ? IM_COL32(100,220,140,255) : C_TXT1;
        dlL->AddText(F12, 12.f, {rowP.x+idW+12.f, rowP.y+12.f}, nameCol, dispName.c_str());

        // Friend star badge (right side)
        if (isFriend) {
            float starX = rowP.x+lrw-20.f;
            dlL->AddText(F12, 11.f, {starX, rowP.y+12.f}, C_ACT, "*");
        }

        // Invisible button covering whole row
        ImGui::SetCursorScreenPos(rowP);
        char rowId[32]; snprintf(rowId, 32, "##plr%d", pedId);
        ImGui::InvisibleButton(rowId, {lrw, rh});

        // Right-click → context popup
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            s_ctxId = pedId;
            s_renameId = -1;
            ImGui::OpenPopup("##plrCtx");
        }

        dlL->AddLine({rowP.x+6.f, rowP.y+rh-1.f},{rowP.x+lrw-6.f, rowP.y+rh-1.f},
                     IM_COL32(255,255,255,6));
    }

    if (!anyShown) {
        ImVec2 ep = ImGui::GetCursorScreenPos();
        const char* msg = PedList.empty() ? "Aktif oyuncu bulunamadi." : "Arama sonucu yok.";
        dlL->AddText(F12, 12.f, {ep.x+16.f, ep.y+10.f}, C_TXT3, msg);
        ImGui::Dummy({lrw, 28.f});
    }

    // ---- Context popup (right-click menu) ----
    if (ImGui::BeginPopup("##plrCtx")) {
        ImGui::PushFont(F12);
        bool ctxFriend = IsFriend(s_ctxId);
        char popTitle[24]; snprintf(popTitle, 24, "Oyuncu #%d", s_ctxId);
        ImGui::TextDisabled("%s", popTitle);
        ImGui::Separator();

        if (ctxFriend) {
            if (ImGui::MenuItem("  Arkadastan Cikar")) { RemoveFriend(s_ctxId); }
        } else {
            if (ImGui::MenuItem("  Arkadas Ekle"))     { friendList.push_back({s_ctxId,""}); }
        }

        if (ImGui::MenuItem("  Isim Degistir")) {
            s_renameId = s_ctxId;
            auto ait = g_playerAliases.find(s_ctxId);
            if (ait != g_playerAliases.end())
                strncpy_s(s_renameBuf, sizeof(s_renameBuf), ait->second.c_str(), _TRUNCATE);
            else s_renameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
            ImGui::OpenPopup("##plrRename");
        }

        bool hasAl = g_playerAliases.count(s_ctxId) && !g_playerAliases[s_ctxId].empty();
        if (hasAl && ImGui::MenuItem("  Alias Sil")) { g_playerAliases.erase(s_ctxId); }

        ImGui::PopFont();
        ImGui::EndPopup();
    }

    // ---- Rename popup ----
    ImGui::SetNextWindowSize({180.f, 68.f});
    if (ImGui::BeginPopup("##plrRename")) {
        ImGui::PushFont(F12);
        ImGui::Text("Yeni isim:");
        ImGui::SetNextItemWidth(165.f);
        if (ImGui::InputText("##rnI", (ImTextureID)0, s_renameBuf, sizeof(s_renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (s_renameId > 0) {
                if (s_renameBuf[0] == '\0') g_playerAliases.erase(s_renameId);
                else g_playerAliases[s_renameId] = s_renameBuf;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopID();
}

// ============================================================
//  Sidebar nav item
// ============================================================
static bool SideNavItem(const char* lbl, bool active, ImVec2 pos, float w,
                        ID3D11ShaderResourceView* iconSrv = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = 46.f;
    ImVec2 p = pos;

    bool hov = ImGui::IsMouseHoveringRect(p, {p.x+w, p.y+h});

    // Row background
    ImU32 bg = active ? IM_COL32(22,32,27,255) : IM_COL32(0,0,0,0);
    if (hov && !active) bg = IM_COL32(16,24,20,255);
    dl->AddRectFilled(p, {p.x+w, p.y+h}, bg);

    // Active: left accent bar + multi-layer glow
    if (active) {
        dl->AddRectFilled(p, {p.x+3.f, p.y+h}, C_ACT);
        // Horizontal glow layers radiating right from accent bar
        ImU32 cag = C_ACT;
        dl->AddRectFilled({p.x+3.f,p.y},{p.x+90.f,p.y+h}, (cag&0x00FFFFFFu)|0x0E000000u);
        dl->AddRectFilled({p.x+3.f,p.y},{p.x+55.f,p.y+h}, (cag&0x00FFFFFFu)|0x0C000000u);
        dl->AddRectFilled({p.x+3.f,p.y},{p.x+28.f,p.y+h}, (cag&0x00FFFFFFu)|0x0A000000u);
    } else if (hov) {
        dl->AddRectFilled(p, {p.x+3.f, p.y+h}, (C_ACT & 0x00FFFFFFu) | 0x3C000000u);
    }

    // Icon area
    const float IS = 22.f;
    float ix = p.x + 14.f, iy = p.y + (h - IS) * 0.5f;
    ImU32 iconBg = active ? C_ACT : IM_COL32(28,36,31,255);
    dl->AddRectFilled({ix,iy},{ix+IS,iy+IS}, iconBg, 5.f);

    if (iconSrv) {
        // Use PNG icon — tint white when active, muted when inactive
        ImU32 tint = active ? IM_COL32(255,255,255,230) : IM_COL32(180,200,190,160);
        dl->AddImage((ImTextureID)iconSrv, {ix+3.f,iy+3.f}, {ix+IS-3.f,iy+IS-3.f},
                     {0,0}, {1,1}, tint);
    } else {
        char icon[2] = {lbl[0], 0};
        ImFont* fb = RobotoBold ? RobotoBold : ImGui::GetDefaultFont();
        float iw2 = fb->CalcTextSizeA(11.f,FLT_MAX,0.f,icon).x;
        dl->AddText(fb, 11.f, {ix+(IS-iw2)*0.5f, iy+5.f},
                    active ? IM_COL32(8,12,10,255) : IM_COL32(100,120,110,255), icon);
    }

    // Label  (glow behind text when active)
    float lx = p.x + 44.f, ly = p.y + (h - 12.f) * 0.5f;
    ImU32 lblCol = active ? IM_COL32(230,240,235,255) : IM_COL32(90,110,100,255);
    if (active) {
        ImFont* fb2 = F12 ? F12 : ImGui::GetDefaultFont();
        float tw = fb2->CalcTextSizeA(12.f, FLT_MAX, 0.f, lbl).x;
        // Glow layers behind label text
        for (int gi = 4; gi >= 1; --gi) {
            float e = gi * 2.5f;
            dl->AddRectFilled({lx-e, ly-e*0.4f}, {lx+tw+e, ly+12.f+e*0.4f},
                              (C_ACT & 0x00FFFFFFu) | ((ImU32)(gi*6) << 24), e);
        }
    }
    dl->AddText(F12, 12.f, {lx, ly}, lblCol, lbl);

    // Chevron
    if (active) {
        ImFont* fb2 = RobotoBold ? RobotoBold : ImGui::GetDefaultFont();
        dl->AddText(fb2, 10.f, {p.x+w-14.f, p.y+(h-10.f)*0.5f}, C_ACT, "›");
    }

    ImGui::SetCursorScreenPos(p);
    char id[32]; snprintf(id, 32, "##nav%s", lbl);
    ImGui::InvisibleButton(id, {w, h});
    return ImGui::IsItemClicked();
}

// ============================================================
//  ImGuiPlugins
// ============================================================
void ImGuiPlugins(ID3D11Device* pDevice) {
    LoadFonts();

    // Load all icons from embedded byte arrays (no external files needed)
    g_texLogo     = LoadTextureMem(pDevice, g_logo_png,     g_logo_png_len,     &g_texLogoW, &g_texLogoH);
    g_texCombat   = LoadTextureMem(pDevice, g_combat_png,   g_combat_png_len);
    g_texPvp      = LoadTextureMem(pDevice, g_pvp_png,      g_pvp_png_len);
    g_texVisuals  = LoadTextureMem(pDevice, g_visuals_png,  g_visuals_png_len);
    g_texSettings = LoadTextureMem(pDevice, g_settings_png, g_settings_png_len);

    g_navIcons[0] = g_texCombat;    // Aim Assist
    g_navIcons[1] = g_texPvp;       // Players
    g_navIcons[2] = g_texVisuals;   // Vehicles
    g_navIcons[3] = g_texCombat;    // World
    g_navIcons[4] = g_texSettings;  // Settings
    g_navIcons[5] = g_texPvp;       // Friends
}

// ============================================================
//  RenderBackground — ESP overlay
// ============================================================
// ---- Toast render helper ----
static void RenderToasts() {
    ImDrawList* fdl = ImGui::GetForegroundDrawList();
    ImGuiIO&    io  = ImGui::GetIO();
    float dt  = io.DeltaTime;
    float sw  = io.DisplaySize.x, sh = io.DisplaySize.y;
    float y   = sh * 0.08f;  // top-center start

    for (int i = (int)g_toasts.size()-1; i >= 0; --i) {
        auto& t = g_toasts[i];
        t.timer -= dt;
        if (t.timer <= 0.f) { g_toasts.erase(g_toasts.begin()+i); continue; }

        float alpha = ImMin(1.f, t.timer * 4.f);  // fade out last 0.25s
        float slideY = ImMax(0.f, (1.f - ImMin(1.f, t.timer * 6.f)));  // slide in from top

        ImFont* f = F12 ? F12 : ImGui::GetDefaultFont();
        float tw = f->CalcTextSizeA(13.f, FLT_MAX, 0.f, t.msg.c_str()).x;
        float pw = tw + 28.f, ph = 28.f;
        float px = (sw - pw) * 0.5f;
        float py = y + slideY * (-ph - 6.f);

        // Background pill
        ImU32 bg  = IM_COL32(12,16,22, (int)(200*alpha));
        ImU32 bdr = (t.color & 0x00FFFFFFu) | ((ImU32)(int)(200*alpha) << 24);
        fdl->AddRectFilled({px,py},{px+pw,py+ph}, bg, ph*0.5f);
        fdl->AddRect       ({px,py},{px+pw,py+ph}, bdr, ph*0.5f, 0, 1.5f);

        // Colored left accent
        fdl->AddRectFilled({px,py},{px+3.f,py+ph}, bdr, ph*0.5f);

        // Text
        ImU32 tc = (t.color & 0x00FFFFFF) | ((ImU32)(int)(255*alpha) << 24);
        fdl->AddText(f, 13.f, {px+14.f, py+(ph-13.f)*0.5f}, tc, t.msg.c_str());

        y += ph + 6.f;
    }
}

// ---- Keybind overlay helper ----
static void RenderKeybindOverlay() {
    if (isMenuVisible) return;  // Menü açıkken sol alt yazıyı gizle
    ImDrawList* fdl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float x = 12.f, y = io.DisplaySize.y - 14.f;

    struct { bool en; int key; const char* label; ImU32 color; } items[] = {
        { Cheats::AimAssist::Silent::Enabled,      Cheats::AimAssist::Silent::HotKey,
          "SILENT",   IM_COL32(100,220,140,255) },
        { Cheats::AimAssist::MagicBullet::Enabled, 0,
          "MAGIC",    IM_COL32(255,180,  0,255) },
        { Cheats::AimAssist::Aimbot::Enabled,      Cheats::AimAssist::Aimbot::HotKey,
          "AIMBOT",   IM_COL32( 80,180,255,255) },
        { Cheats::AimAssist::Triggerbot::Enabled,  Cheats::AimAssist::Triggerbot::HotKey,
          "TRIGGER",  IM_COL32(255,200, 60,255) },
    };
    // Triggerbot has no hotkey field - skip key display for it
    for (auto& it : items) {
        if (!it.en) continue;
        char buf[32];
        if (it.key)
            snprintf(buf,32,"%s [%s]", it.label, KeyName(it.key));
        else
            snprintf(buf,32,"%s", it.label);

        ImFont* f = F11 ? F11 : ImGui::GetDefaultFont();
        float tw = f->CalcTextSizeA(11.f, FLT_MAX, 0.f, buf).x;
        float ph = 16.f, pw = tw + 12.f;
        y -= ph + 4.f;
        fdl->AddRectFilled({x,y},{x+pw,y+ph}, IM_COL32(10,14,20,200), 4.f);
        fdl->AddRect       ({x,y},{x+pw,y+ph}, (it.color & 0x00FFFFFFu) | 0x80000000u, 4.f, 0, 1.f);
        fdl->AddText(f, 11.f, {x+6.f, y+(ph-11.f)*0.5f}, it.color, buf);
    }
}

void RenderBackground() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::Begin("##espbg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
    DrawCheat();
    ImGui::End();
    RenderToasts();
    RenderKeybindOverlay();
}

// ============================================================
//  RenderMenu — main menu window
// ============================================================
void RenderMenu() {
    ApplyTheme(Cheats::Settings::SelectedTheme);
    UpdateTheme();
    if (Roboto) ImGui::PushFont(Roboto);

    static const float SBW = 170.f;
    static const float WW  = 820.f;
    static const float WH  = 530.f;

    GuiSetting::Size = ImVec2(WW, WH);

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({(ds.x-WW)*0.5f, (ds.y-WH)*0.5f}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({WW, WH}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);

    ImGui::Begin("##winscrew", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      wpos = ImGui::GetWindowPos();

    // ---- Deep shadow (outer drop) ----
    for (int sh=6;sh>=1;--sh) {
        int sa = 12 - sh*1;
        dl->AddRectFilled({wpos.x-(float)sh,wpos.y-(float)sh},
                          {wpos.x+WW+(float)sh,wpos.y+WH+(float)sh},
                          IM_COL32(0,0,0,sa), 12.f);
    }
    // ---- Animated pulsing border glow ----
    {
        float pulse = (float)(sin(ImGui::GetTime() * 1.6) * 0.5 + 0.5);
        ImU32 ca = C_ACT;
        int r=(ca)&0xFF, g=(ca>>8)&0xFF, b=(ca>>16)&0xFF;
        int ga = (int)(25 + pulse * 45);
        dl->AddRect({wpos.x-2,wpos.y-2},{wpos.x+WW+2,wpos.y+WH+2},
                    IM_COL32(r,g,b,ga), 10.f, 0, 2.f);
        dl->AddRect({wpos.x,wpos.y},{wpos.x+WW,wpos.y+WH},
                    IM_COL32(r,g,b,ga/3), 9.f, 0, 1.f);
    }
    // ---- Dark sidebar background ----
    dl->AddRectFilled(wpos, {wpos.x+SBW, wpos.y+WH}, C_SB, 9.f,
                      ImDrawFlags_RoundCornersLeft);
    // ---- Dark content background ----
    dl->AddRectFilled({wpos.x+SBW, wpos.y},{wpos.x+WW,wpos.y+WH},
                      C_BG, 9.f, ImDrawFlags_RoundCornersRight);
    // ---- Top gradient shimmer ----
    dl->AddRectFilled({wpos.x+SBW,wpos.y},{wpos.x+WW,wpos.y+1.f},
                      IM_COL32(255,255,255,8));
    // ---- Sidebar separator ----
    dl->AddRectFilled({wpos.x+SBW-1,wpos.y+8},{wpos.x+SBW,wpos.y+WH-8},
                      IM_COL32(40,55,45,255));
    // ---- Outer border ----
    dl->AddRect(wpos, {wpos.x+WW,wpos.y+WH}, IM_COL32(35,45,60,255), 9.f, 0, 1.f);
    // ---- Tab row background ----
    dl->AddRectFilled({wpos.x+SBW, wpos.y},{wpos.x+WW, wpos.y+50},
                      IM_COL32(14, 17, 23, 255));
    dl->AddLine({wpos.x+SBW, wpos.y+50},{wpos.x+WW, wpos.y+50}, C_BDR, 1.f);

    // ============================================================
    //  Sidebar child (placed at y=0 → auto-header off-screen)
    // ============================================================
    ImGui::PushID(0xC1);
    ImGui::SetCursorScreenPos(wpos);
    ImGui::BeginChild("", (ImTextureID)0, {SBW, WH}, false,
                       ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* sbDL = ImGui::GetWindowDrawList();
        ImVec2 sbPos = ImGui::GetWindowPos();

        // ---- Matrix / code rain animation ----
        {
            static const char kChars[] = "01{}[]()<>;#%$!|/\\~^&*+-=@0x10110100";
            static const int  COLS     = 8;
            static float      colY[COLS]   = {};
            static int        colChar[COLS]= {};
            static float      colSpeed[COLS]= {};
            static bool       colInit = false;
            if (!colInit) {
                for (int i=0;i<COLS;++i) {
                    colY[i]     = (float)(rand()%530);
                    colChar[i]  = rand()%((int)sizeof(kChars)-1);
                    colSpeed[i] = 60.f + (rand()%80);
                }
                colInit = true;
            }
            float dt = ImGui::GetIO().DeltaTime;
            float cw2 = SBW / (float)COLS;
            for (int i=0;i<COLS;++i) {
                colY[i] += colSpeed[i] * dt;
                if (colY[i] > WH + 16.f) {
                    colY[i]    = -14.f;
                    colChar[i] = rand()%((int)sizeof(kChars)-1);
                }
                if ((int)(colY[i] / 18.f) % 3 == 0)
                    colChar[i] = rand()%((int)sizeof(kChars)-1);
                // Draw 4 trailing chars with fading alpha
                for (int j=0;j<4;++j) {
                    float cy = colY[i] - j*14.f;
                    if (cy < 0 || cy > WH) continue;
                    int alpha = j==0 ? 55 : j==1 ? 35 : j==2 ? 20 : 10;
                    char ch[2] = {kChars[colChar[i]], 0};
                    // Extract RGB from C_ACT
                    ImU32 ca = C_ACT;
                    ImU32 cc = (ca & 0x00FFFFFFu) | ((ImU32)alpha << 24);
                    sbDL->AddText(F11, 10.f,
                        {sbPos.x + cw2*i + 4.f, sbPos.y + cy}, cc, ch);
                }
            }
        }

        // ---- Header: logo image or text fallback ----
        if (g_texLogo && g_texLogoW > 0 && g_texLogoH > 0) {
            float lh2 = 36.f;
            float lw2 = lh2 * ((float)g_texLogoW / (float)g_texLogoH);
            if (lw2 > SBW - 28.f) { lw2 = SBW - 28.f; lh2 = lw2 * ((float)g_texLogoH / (float)g_texLogoW); }
            float lx2 = sbPos.x + (SBW - lw2) * 0.5f;
            float ly2 = sbPos.y + 10.f;
            sbDL->AddImage((ImTextureID)g_texLogo, {lx2, ly2}, {lx2+lw2, ly2+lh2});
        } else {
            ImFont* fb = RobotoBold ? RobotoBold : ImGui::GetDefaultFont();
            sbDL->AddText(fb, 18.f, {sbPos.x+16, sbPos.y+16}, C_ACT, "Win");
            float winW = fb->CalcTextSizeA(18.f, FLT_MAX, 0.f, "Win").x;
            sbDL->AddText(fb, 18.f, {sbPos.x+16+winW, sbPos.y+16}, IM_COL32(200,215,208,255), "screW");
        }
        sbDL->AddLine({sbPos.x+12, sbPos.y+52},{sbPos.x+SBW-12, sbPos.y+52},
                      (C_ACT & 0x00FFFFFFu) | 0x28000000u, 1.f);

        // Nav items
        static const char* navLabels[] = {"Aim Assist","Players","Vehicles","World","Settings","Oyuncular"};
        ImGui::SetCursorScreenPos({sbPos.x, sbPos.y + 58.f});
        for (int i = 0; i < 6; ++i)
            if (SideNavItem(navLabels[i], g_page==i, ImGui::GetCursorScreenPos(), SBW, g_navIcons[i]))
                g_page = i;

        // Bottom key badge
        {
            float by = sbPos.y + WH - 38.f;
            sbDL->AddText(F11, 10.f, {sbPos.x+12, by}, IM_COL32(50,70,60,255), "MENU KEY");
            float bx = sbPos.x + 84.f;
            sbDL->AddRectFilled({bx,by-2},{bx+60,by+16}, C_ACT, 4.f);
            const char* kl = KeyName(Cheats::Settings::MenuKey);
            float kw = F11->CalcTextSizeA(10.f,FLT_MAX,0.f,kl).x;
            sbDL->AddText(F11, 10.f, {bx+(60-kw)*0.5f, by+3.f},
                          IM_COL32(8,12,10,255), kl);
        }
    }
    ImGui::EndChild();
    ImGui::PopID();

    // ============================================================
    //  Content child
    // ============================================================
    float cw = WW - SBW;
    float ch = WH;

    // ---- Tab fade animation ----
    static int   g_prevPage  = -1;
    static float g_fadeAlpha = 1.f;
    if (g_page != g_prevPage) { g_fadeAlpha = 0.f; g_prevPage = g_page; }
    g_fadeAlpha = ImMin(g_fadeAlpha + ImGui::GetIO().DeltaTime * 7.f, 1.f);

    ImGui::PushID(0xC2);
    ImGui::SetCursorScreenPos({wpos.x + SBW, wpos.y});
    ImGui::BeginChild("", (ImTextureID)0, {cw, ch}, false,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        // Subtle hex drift pattern in content background
        static const char* hexTokens[] = {"0x","FF","7F","A5","DE","AD","BE","EF","1A","2B","3C","4D"};
        static float htX[10]={},htY[10]={},htS[10]={};
        static bool htInit=false;
        if (!htInit){ for(int i=0;i<10;++i){htX[i]=(float)(rand()%500+20);htY[i]=(float)(rand()%530);htS[i]=8.f+(rand()%8);} htInit=true; }
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        ImVec2 cBase = {wpos.x+SBW, wpos.y};
        float dt2 = ImGui::GetIO().DeltaTime;
        for(int i=0;i<10;++i){
            htY[i] -= htS[i]*dt2;
            if(htY[i]<-14.f){htY[i]=WH+4.f;htX[i]=(float)(rand()%490+10);}
            ImU32 ca2 = C_ACT;
            ImU32 cc2 = (ca2 & 0x00FFFFFFu) | 0x0C000000u;  // very faint alpha
            cdl->AddText(F11,9.f,{cBase.x+htX[i],cBase.y+htY[i]},cc2,hexTokens[i%12]);
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_fadeAlpha);
    switch (g_page) {
    case 0: PageAim(cw, ch);      break;
    case 1: PagePlayers(cw, ch);  break;
    case 2: PageVehicles(cw, ch); break;
    case 3: PageWorld(cw, ch);    break;
    case 4: PageSettings(cw, ch); break;
    case 5: PageFriends(cw, ch);  break;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopID();

    ImGui::End();
    if (Roboto) ImGui::PopFont();
}

// ============================================================
//  RenderImGui
// ============================================================
void RenderImGui(HWND /*hWnd*/, bool menuVisible) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderBackground();
    if (menuVisible) {
        RenderMenu();

        // Rotating spiral cursor
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        ImVec2 mp  = ImGui::GetIO().MousePos;
        double  t  = ImGui::GetTime();
        ImU32 cMain = C_ACT;

        // Outer ring: 4 arc segments rotating clockwise
        {
            float rot = (float)(t * 2.2);
            for (int i = 0; i < 4; ++i) {
                float a0 = rot + i * IM_PI * 0.5f;
                float a1 = a0 + IM_PI * 0.32f;
                fdl->PathArcTo(mp, 11.f, a0, a1, 16);
                ImU32 c = (cMain & 0x00FFFFFFu) | IM_COL32(0,0,0,200);
                fdl->PathStroke(IM_COL32(0,0,0,100), false, 3.5f); // shadow
                fdl->PathArcTo(mp, 11.f, a0, a1, 16);
                fdl->PathStroke(cMain, false, 2.f);
            }
        }
        // Inner ring: 3 dots orbiting counter-clockwise
        {
            float rot2 = (float)(-t * 3.5);
            for (int i = 0; i < 3; ++i) {
                float a = rot2 + i * (IM_PI * 2.f / 3.f);
                ImVec2 pt = { mp.x + cosf(a)*5.5f, mp.y + sinf(a)*5.5f };
                fdl->AddCircleFilled(pt, 1.6f, IM_COL32(0,0,0,120), 6);
                fdl->AddCircleFilled(pt, 1.2f, cMain, 6);
            }
        }
        // Center dot
        fdl->AddCircleFilled(mp, 1.8f, IM_COL32(255,255,255,240), 8);
    }

    ImGui::Render();
}
