// ============================================================
//  Overlay.hpp  —  Pure GDI layered-window overlay
//  D3D11 / DXGI tamamen kaldırıldı → anti-cheat safe.
//  BitBlt tabanlı CPU rendering, LWA_COLORKEY(0,0,2) şeffaflık.
// ============================================================

// "PresenterCoreHost" — runtime inşa (bellek taramasında görünmesin)
static char s_overlayClassName[20] = {};
static const char* GetOverlayClassName() {
    if (!s_overlayClassName[0]) {
        s_overlayClassName[0]='P'; s_overlayClassName[1]='r'; s_overlayClassName[2]='e';
        s_overlayClassName[3]='s'; s_overlayClassName[4]='e'; s_overlayClassName[5]='n';
        s_overlayClassName[6]='t'; s_overlayClassName[7]='e'; s_overlayClassName[8]='r';
        s_overlayClassName[9]='C'; s_overlayClassName[10]='o'; s_overlayClassName[11]='r';
        s_overlayClassName[12]='e'; s_overlayClassName[13]='H'; s_overlayClassName[14]='o';
        s_overlayClassName[15]='s'; s_overlayClassName[16]='t'; s_overlayClassName[17]=0;
    }
    return s_overlayClassName;
}

static struct OverlayStruct {
    HWND       hWnd         = nullptr;
    WNDCLASSEX wndClassEx   = {};
    LPCSTR     lpClassName  = nullptr;
    LPCSTR     lpWindowName = "";
    UINT       resizeWidth  = 0;
    UINT       resizeHeight = 0;
    POINT      WindowSize   = {};
    POINT      ScreenSize   = {};
} Overlay;

bool CreateOverlayWindow();
void UpdateOverlay();
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);
void CleanupOverlayWindow();

// ESP-only overlay: her zaman click-through + colorkey şeffaf.
static void ApplyOverlayAttributes() {
    if (!Overlay.hWnd) return;
    LONG ex = GetWindowLong(Overlay.hWnd, GWL_EXSTYLE);
    ex |= WS_EX_TRANSPARENT;
    SetWindowLong(Overlay.hWnd, GWL_EXSTYLE, ex);
    SetWindowPos(Overlay.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(Overlay.hWnd, RGB(0, 0, 2), 0, LWA_COLORKEY);
}

// Overlay display size (DrawCheat kullanır)
OverlayIO g_io;

int OverlayMain(int& errorId) {
    if (!CreateOverlayWindow()) { CleanupOverlayWindow(); errorId = 1; return 1; }

    // Pencereyi GDI init'ten ÖNCE göster:
    // WS_EX_LAYERED DWM backing store ShowWindow'dan sonra hazır olur.
    // GetDC bu noktadan sonra çağrılırsa geçerli yüzey DC'si alınır.
    ShowWindow(Overlay.hWnd, SW_SHOWNOACTIVATE);
    ApplyOverlayAttributes();

    if (!g_D3DR.Init(Overlay.hWnd)) {
        CleanupOverlayWindow(); errorId = 3; return 1;
    }

    std::thread([]() { UpdateOverlay(); }).detach();

    g_io.DisplaySize = ImVec2((float)Overlay.ScreenSize.x, (float)Overlay.ScreenSize.y);

    while (keepRunning) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) keepRunning = false;
        }
        if (!keepRunning) break;

        // Ekran/pencere boyutu değişti → her şeyi yeniden kur
        if (screenSizeChanged || sizeChanged) {
            g_D3DR.Cleanup();
            CleanupOverlayWindow();
            if (!CreateOverlayWindow()) { errorId = 1; break; }
            Sleep(500);
            if (!g_D3DR.Init(Overlay.hWnd)) { errorId = 3; break; }
            g_io.DisplaySize = ImVec2((float)Overlay.ScreenSize.x, (float)Overlay.ScreenSize.y);
            ApplyOverlayAttributes();
            screenSizeChanged = sizeChanged = false;
            continue;
        }

        // WM_SIZE geldi → arka tamponu yeniden boyutlandır
        if (Overlay.resizeWidth && Overlay.resizeHeight) {
            Overlay.ScreenSize = { (LONG)Overlay.resizeWidth, (LONG)Overlay.resizeHeight };
            g_D3DR.Resize((int)Overlay.resizeWidth, (int)Overlay.resizeHeight);
            Overlay.resizeWidth = Overlay.resizeHeight = 0;
            g_io.DisplaySize = ImVec2((float)Overlay.ScreenSize.x, (float)Overlay.ScreenSize.y);
        }

        if (GetAsyncKeyState(VK_END) & 0x8000)
            keepRunning = false;

        if (Cheats::Settings::PanicKey &&
            (GetAsyncKeyState(Cheats::Settings::PanicKey) & 1)) {
            extern void RestoreSilent();
            try { RestoreSilent(); } catch (...) {}
            if (Game.hProcess) { CloseHandle(Game.hProcess); Game.hProcess = nullptr; }
            HWND hCon = GetConsoleWindow();
            if (hCon) ShowWindow(hCon, SW_HIDE);
            TerminateProcess(GetCurrentProcess(), 0);
        }

        if (g_D3DR.ready) {
            g_D3DR.BeginFrame();
            DrawCheat();
            g_D3DR.EndFrame();
        }

        mainReady = true;

        // ~120 fps hedef — 8ms cap
        static DWORD tLast = GetTickCount();
        DWORD tNow = GetTickCount();
        if (tNow - tLast < 8) Sleep(8 - (tNow - tLast));
        tLast = GetTickCount();
    }

    g_D3DR.Cleanup();
    CleanupOverlayWindow();
    return 0;
}

bool CreateOverlayWindow() {
    Overlay.ScreenSize = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    Overlay.lpClassName = GetOverlayClassName();

    ZeroMemory(&Overlay.wndClassEx, sizeof(Overlay.wndClassEx));
    Overlay.wndClassEx.cbSize        = sizeof(WNDCLASSEX);
    Overlay.wndClassEx.lpfnWndProc   = WndProc;
    Overlay.wndClassEx.hInstance     = GetModuleHandle(NULL);
    Overlay.wndClassEx.lpszClassName = Overlay.lpClassName;
    Overlay.wndClassEx.lpszMenuName  = Overlay.lpWindowName;
    if (!RegisterClassEx(&Overlay.wndClassEx)) return false;

    Overlay.WindowSize = Overlay.ScreenSize;

    Overlay.hWnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        Overlay.lpClassName, Overlay.lpWindowName, WS_POPUP,
        0, 0, Overlay.ScreenSize.x, Overlay.ScreenSize.y,
        NULL, NULL, Overlay.wndClassEx.hInstance, NULL);
    if (!Overlay.hWnd) return false;
    SetLayeredWindowAttributes(Overlay.hWnd, RGB(0, 0, 2), 0, LWA_COLORKEY);
    return true;
}

void UpdateOverlay() {
    ShowWindow(Overlay.hWnd, SW_SHOWNOACTIVATE);
    SetWindowPos(Overlay.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(Overlay.hWnd);

    while (keepRunning) {
        Sleep(500);
        if (!mjLib::Process::Check(Game.pID) || !FindWindowA(BuildClassName(), NULL)) {
            keepRunning = false; break;
        }
        SetWindowPos(Overlay.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowDisplayAffinity(Overlay.hWnd,
            Cheats::Settings::StreamProof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            Overlay.resizeWidth  = LOWORD(lParam);
            Overlay.resizeHeight = HIWORD(lParam);
        }
        return 0;
    case WM_ERASEBKGND:
        // GDI kendi arka planını her frame zaten doldurur.
        // Windows'un default erase'ini bastır — colorkey rengini silmez.
        return 1;
    case WM_PAINT: {
        // UpdateLayeredWindow kullanıldığında WM_PAINT sadece dirty flag'i temizler.
        // Gerçek çizim EndFrame → UpdateLayeredWindow ile yapılır.
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        if ((wParam & 0xfff0) == SC_CLOSE)   return 0;
        break;
    case WM_DESTROY:
        if (!sizeChanged && !screenSizeChanged) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void CleanupOverlayWindow() {
    if (Overlay.hWnd && IsWindow(Overlay.hWnd)) {
        DestroyWindow(Overlay.hWnd); Overlay.hWnd = nullptr;
    }
    if (Overlay.wndClassEx.hInstance) {
        UnregisterClass(Overlay.wndClassEx.lpszClassName, Overlay.wndClassEx.hInstance);
        Overlay.wndClassEx.hInstance = nullptr;
    }
}
