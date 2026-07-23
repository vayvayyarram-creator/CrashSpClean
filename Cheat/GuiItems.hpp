// ============================================================
//  GuiItems  —  Fonts + Theme (Silver + Dark Green)
// ============================================================

ImFont* Roboto      = nullptr;
ImFont* Roboto2     = nullptr;
ImFont* RobotoBold  = nullptr;
ImFont* RobotoSmall = nullptr;
ImFont* RobotoEsp   = nullptr;
ImFont* RobotoEsp2  = nullptr;

namespace Images {
    ID3D11ShaderResourceView* Logo      = nullptr;
    ID3D11ShaderResourceView* ArrowsIcon= nullptr;
}

// ============================================================
//  DarkenColor
// ============================================================
ImColor DarkenColor(const ImColor& color, float factor = 0.7f) {
    return ImColor(
        ImClamp(color.Value.x * factor, 0.f, 1.f),
        ImClamp(color.Value.y * factor, 0.f, 1.f),
        ImClamp(color.Value.z * factor, 0.f, 1.f),
        color.Value.w);
}

// ============================================================
//  LoadFonts
// ============================================================
void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar glyphRanges[] = { 0x0020, 0x024F, 0 };
    Roboto     = io.Fonts->AddFontFromMemoryTTF(&RobotoCondensedRegularData,
                    sizeof(RobotoCondensedRegularData), 17.f, NULL, glyphRanges);
    Roboto2    = io.Fonts->AddFontFromMemoryTTF(&RobotoCondensedRegularData,
                    sizeof(RobotoCondensedRegularData), 15.f, NULL, glyphRanges);
    RobotoBold = io.Fonts->AddFontFromMemoryTTF(&RobotoCondensedBoldData,
                    sizeof(RobotoCondensedBoldData),    16.f, NULL, glyphRanges);
    RobotoSmall= io.Fonts->AddFontFromMemoryTTF(&RobotoCondensedRegularData,
                    sizeof(RobotoCondensedRegularData), 13.f, NULL, glyphRanges);
    RobotoEsp  = io.Fonts->AddFontFromMemoryTTF(&RobotoCondensedRegularData,
                    sizeof(RobotoCondensedRegularData), 14.f, NULL, glyphRanges);
    RobotoEsp2 = io.Fonts->AddFontFromMemoryTTF(&RobotoCondensedRegularData,
                    sizeof(RobotoCondensedRegularData), 12.f, NULL, glyphRanges);
}

// ============================================================
//  UpdateTheme  —  Dark + Emerald Green palette
// ============================================================
static const ImVec4 kAccent    = ImColor(  5, 150, 105, 255);  // emerald-600
static const ImVec4 kAccentDim = ImColor(  4, 120,  87, 255);  // emerald-700
static const ImVec4 kWinBg     = ImColor( 18,  21,  28, 255);  // dark content bg
static const ImVec4 kPanelBg   = ImColor( 24,  28,  36, 255);  // dark panel bg
static const ImVec4 kChildBg   = ImColor(  0,   0,   0,   0);  // transparent child
static const ImVec4 kBorder    = ImColor( 36,  42,  58, 255);  // dark border
static const ImVec4 kCheckBg   = ImColor( 36,  42,  58, 255);  // dark control bg
static const ImVec4 kText      = ImColor(220, 228, 240, 255);  // light text
static const ImVec4 kMuted     = ImColor(100, 115, 135, 255);  // gray muted

void UpdateTheme() {
    ImGuiContext& ctx = *GImGui;
    float dt = ctx.IO.DeltaTime * 10.f;

    GuiSetting::Accent      = ImLerp(GuiSetting::Accent,      kAccent,    dt);
    GuiSetting::AccentDim   = ImLerp(GuiSetting::AccentDim,   kAccentDim, dt);
    GuiSetting::WindowBg    = ImLerp(GuiSetting::WindowBg,    kWinBg,     dt);
    GuiSetting::PanelBg     = ImLerp(GuiSetting::PanelBg,     kPanelBg,   dt);
    GuiSetting::ChildBg     = ImLerp(GuiSetting::ChildBg,     kChildBg,   dt);
    GuiSetting::Border      = ImLerp(GuiSetting::Border,      kBorder,    dt);
    GuiSetting::CheckboxBg  = ImLerp(GuiSetting::CheckboxBg,  kCheckBg,   dt);
    GuiSetting::CheckboxFill= ImLerp(GuiSetting::CheckboxFill, kAccent,   dt);
    GuiSetting::SwitchOff   = ImLerp(GuiSetting::SwitchOff,   kBorder,    dt);
    GuiSetting::SwitchOn    = ImLerp(GuiSetting::SwitchOn,    kAccent,    dt);
    GuiSetting::TextMuted   = ImLerp(GuiSetting::TextMuted,   kMuted,     dt);
    // Legacy fields
    GuiSetting::Color1      = GuiSetting::Accent;
    GuiSetting::Color2      = GuiSetting::PanelBg;
    GuiSetting::Color3      = GuiSetting::ChildBg;
    GuiSetting::Color4      = GuiSetting::WindowBg;
    GuiSetting::Black       = kText;
    GuiSetting::Black1      = kText;
    GuiSetting::BlackIn     = ImColor(185,195,190,127);
    GuiSetting::BorderChild = ImColor(0,0,0,0);
    GuiSetting::LineChild   = ImColor(0,0,0,0);  // transparent — qwe auto-header gizle
    GuiSetting::ShadowTab   = GuiSetting::AccentDim;
    GuiSetting::CheckboxBackground   = GuiSetting::CheckboxFill;
    GuiSetting::CheckboxInBackground = GuiSetting::CheckboxBg;
    GuiSetting::CircleCheckbox       = kText;
    GuiSetting::CircleCheckboxIn     = ImColor(185,195,190,127);
    GuiSetting::Separator            = GuiSetting::Border;

    ImGuiStyle& s = ImGui::GetStyle();
    // Dark-themed style (used for ColorEdit4 popups etc.)
    s.Colors[ImGuiCol_WindowBg]           = kWinBg;
    s.Colors[ImGuiCol_ChildBg]            = ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_PopupBg]            = kPanelBg;
    s.Colors[ImGuiCol_Border]             = kBorder;
    s.Colors[ImGuiCol_FrameBg]            = kCheckBg;
    s.Colors[ImGuiCol_FrameBgHovered]     = ImColor( 50, 58, 78,255);
    s.Colors[ImGuiCol_FrameBgActive]      = GuiSetting::AccentDim;
    s.Colors[ImGuiCol_SliderGrab]         = GuiSetting::Accent;
    s.Colors[ImGuiCol_SliderGrabActive]   = GuiSetting::AccentDim;
    s.Colors[ImGuiCol_Button]             = kCheckBg;
    s.Colors[ImGuiCol_ButtonHovered]      = ImColor( 50, 58, 78,255);
    s.Colors[ImGuiCol_ButtonActive]       = GuiSetting::AccentDim;
    s.Colors[ImGuiCol_Header]             = kCheckBg;
    s.Colors[ImGuiCol_HeaderHovered]      = ImColor( 50, 58, 78,255);
    s.Colors[ImGuiCol_HeaderActive]       = GuiSetting::Accent;
    s.Colors[ImGuiCol_CheckMark]          = GuiSetting::Accent;
    s.Colors[ImGuiCol_Text]               = kText;
    s.Colors[ImGuiCol_TextDisabled]       = kMuted;
    s.Colors[ImGuiCol_Separator]          = kBorder;
    s.Colors[ImGuiCol_SeparatorHovered]   = GuiSetting::Accent;
    s.Colors[ImGuiCol_TitleBg]            = kWinBg;
    s.Colors[ImGuiCol_TitleBgActive]      = kWinBg;
    s.Colors[ImGuiCol_ScrollbarBg]        = ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_ScrollbarGrab]      = ImColor( 50, 60, 80,255);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = GuiSetting::Accent;
    s.Colors[ImGuiCol_ScrollbarGrabActive]  = GuiSetting::AccentDim;

    s.ScrollbarSize    = 4.f;
    s.WindowRounding   = 8.f;
    s.ChildRounding    = 6.f;
    s.FrameRounding    = 6.f;
    s.GrabRounding     = 6.f;
    s.PopupRounding    = 6.f;
    s.WindowPadding    = {  8.f,  6.f };
    s.FramePadding     = {  5.f,  3.f };
    s.ItemSpacing      = {  6.f,  4.f };
    s.ScrollbarSize    = 0.f;
    s.WindowBorderSize = 0.f;
    s.FrameBorderSize  = 0.f;
}

// ============================================================
//  KeyName
// ============================================================
static const char* KeyName(int vk) {
    static char buf[32];
    switch (vk) {
        case 0:           return "Yok";
        case VK_LBUTTON:  return "Mouse Sol";
        case VK_RBUTTON:  return "Mouse Sag";
        case VK_MBUTTON:  return "Mouse Orta";
        case 0x05:        return "Mouse 4 (Geri)";
        case 0x06:        return "Mouse 5 (Ileri)";
        case VK_SHIFT:    return "Shift";
        case VK_LSHIFT:   return "L-Shift";
        case VK_RSHIFT:   return "R-Shift";
        case VK_CONTROL:  return "Ctrl";
        case VK_LCONTROL: return "L-Ctrl";
        case VK_RCONTROL: return "R-Ctrl";
        case VK_MENU:     return "Alt";
        case VK_LMENU:    return "L-Alt";
        case VK_RMENU:    return "R-Alt";
        case VK_CAPITAL:  return "CapsLock";
        case VK_SPACE:    return "Space";
        case VK_RETURN:   return "Enter";
        case VK_ESCAPE:   return "Esc";
        case VK_TAB:      return "Tab";
        case VK_BACK:     return "Backspace";
        case VK_DELETE:   return "Delete";
        case VK_INSERT:   return "Insert";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "PgUp";
        case VK_NEXT:     return "PgDn";
        case VK_UP:       return "Yukari";
        case VK_DOWN:     return "Asagi";
        case VK_LEFT:     return "Sol";
        case VK_RIGHT:    return "Sag";
        case VK_NUMPAD0:  return "Num0";
        case VK_NUMPAD1:  return "Num1";
        case VK_NUMPAD2:  return "Num2";
        case VK_NUMPAD3:  return "Num3";
        case VK_NUMPAD4:  return "Num4";
        case VK_NUMPAD5:  return "Num5";
        case VK_NUMPAD6:  return "Num6";
        case VK_NUMPAD7:  return "Num7";
        case VK_NUMPAD8:  return "Num8";
        case VK_NUMPAD9:  return "Num9";
    }
    if (vk >= 0x30 && vk <= 0x39) { snprintf(buf,sizeof(buf),"%c",(char)vk); return buf; }
    if (vk >= 0x41 && vk <= 0x5A) { snprintf(buf,sizeof(buf),"%c",(char)vk); return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) { snprintf(buf,sizeof(buf),"F%d",vk-VK_F1+1); return buf; }
    snprintf(buf,sizeof(buf),"0x%02X",vk);
    return buf;
}

// ---- Hotkey capture state ----
static bool g_waitingForKey = false;
static int* g_waitingKeyPtr = nullptr;

// ---- Dummy HotkeyButton (not used in new Gui) ----
void HotkeyButton(const char* label, int* keyPtr) {
    char id[128];
    snprintf(id, sizeof(id), "##hkbtn_%s", label);
    bool isWaiting = (g_waitingForKey && g_waitingKeyPtr == keyPtr);
    char btnLbl[64];
    snprintf(btnLbl, sizeof(btnLbl), isWaiting ? "[Press...]" : "[%s]", KeyName(*keyPtr));
    if (isWaiting) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.35f, 0.15f, 1.f));
    if (ImGui::Button(btnLbl)) { g_waitingForKey = true; g_waitingKeyPtr = keyPtr; }
    if (isWaiting) {
        ImGui::PopStyleColor();
        for (int vk = 1; vk < 256; ++vk) {
            if (vk == VK_LBUTTON || vk == VK_RBUTTON) continue;
            if (GetAsyncKeyState(vk) & 1) {
                *keyPtr = (vk == VK_ESCAPE) ? 0 : vk;
                g_waitingForKey = false; g_waitingKeyPtr = nullptr; break;
            }
        }
    }
}
