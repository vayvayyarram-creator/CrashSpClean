// ============================================================
//  GDIRenderer.hpp  —  Pure GDI overlay renderer
//  D3D11 / D2D1 / DWrite tamamen kaldırıldı.
//  CPU-only, BitBlt tabanlı rendering — anti-cheat safe.
//
//  Kritik tasarım kararları:
//  ▸ hDC (pencere DC) kalıcı tutulmaz.
//    EndFrame'de her frame GetDC/BitBlt/ReleaseDC yapılır;
//    DWM WS_EX_LAYERED compositing'i bu döngüde güncellenir.
//  ▸ CreateCompatibleBitmap her zaman GetDC(NULL) (ekran DC)
//    kullanır — renkli bitmap garantisi.
//  ▸ AlphaBlend geçici bitmap üzerinde çalışır, ekran DC ile.
// ============================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wingdi.h>
#include <cstring>
#pragma comment(lib, "msimg32.lib")   // AlphaBlend

// ── D2D1_COLOR_F compat (d2d1.h dahil edilmiyor) ─────────────
#ifndef D2D1_COLOR_F_DEFINED
#define D2D1_COLOR_F_DEFINED
typedef struct D2D1_COLOR_F { float r, g, b, a; } D2D1_COLOR_F;
#endif

// D2D1::ColorF helper — Cheat.hpp kullanımı değişmez
namespace D2D1 {
    inline D2D1_COLOR_F ColorF(float r, float g, float b, float a = 1.f) {
        return { r, g, b, a };
    }
    // 0xRRGGBB hex
    inline D2D1_COLOR_F ColorF(unsigned int rgb, float a = 1.f) {
        return {
            ((rgb >> 16) & 0xFF) / 255.f,
            ((rgb >>  8) & 0xFF) / 255.f,
            ( rgb        & 0xFF) / 255.f,
            a
        };
    }
}

// ── Renk yardımcıları ─────────────────────────────────────────
inline D2D1_COLOR_F D2DFromImU32(unsigned int col) {
    return {
        ((col >>  0) & 0xFF) / 255.f,
        ((col >>  8) & 0xFF) / 255.f,
        ((col >> 16) & 0xFF) / 255.f,
        ((col >> 24) & 0xFF) / 255.f
    };
}
inline D2D1_COLOR_F D2DFromRGBA(int r, int g, int b, int a = 255) {
    return { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
}
inline D2D1_COLOR_F D2DFromImColor(const ImColor& c) {
    return { c.Value.x, c.Value.y, c.Value.z, c.Value.w };
}

// ── GDI renk dönüşümü ─────────────────────────────────────────
static inline COLORREF CR(D2D1_COLOR_F c) {
    int ri = (int)(c.r * 255.f + 0.5f);
    int gi = (int)(c.g * 255.f + 0.5f);
    int bi = (int)(c.b * 255.f + 0.5f);
    if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    return RGB(ri, gi, bi);
}
static inline BYTE Alpha(D2D1_COLOR_F c) {
    int ai = (int)(c.a * 255.f + 0.5f);
    if (ai < 0) ai = 0; if (ai > 255) ai = 255;
    return (BYTE)ai;
}

// ── Ana renderer ──────────────────────────────────────────────
struct D3DRenderer {
    HWND    hWnd      = nullptr;  // overlay penceresi (EndFrame'de UpdateLayeredWindow hedefi)
    HDC     memDC     = nullptr;  // arka tampon (kalıcı)
    HBITMAP memBmp    = nullptr;
    HBITMAP oldBmp    = nullptr;
    HFONT   fontNorm  = nullptr;
    HFONT   fontBold  = nullptr;
    HFONT   fontSmall = nullptr;
    int     width     = 0;
    int     height    = 0;
    bool    ready     = false;
    bool    inFrame   = false;

    // ── Init ─────────────────────────────────────────────────
    bool Init(HWND hwnd) {
        hWnd = hwnd;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        width  = rc.right  ? rc.right  : GetSystemMetrics(SM_CXSCREEN);
        height = rc.bottom ? rc.bottom : GetSystemMetrics(SM_CYSCREEN);

        // Ekran DC → renk derinliği garantisi; geçici kullanım, ReleaseDC ile kapatılır
        HDC screenDC = GetDC(NULL);
        if (!screenDC) return false;

        memDC  = CreateCompatibleDC(screenDC);
        memBmp = CreateCompatibleBitmap(screenDC, width, height);
        ReleaseDC(NULL, screenDC);

        if (!memDC || !memBmp) return false;
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // ANTIALIASED_QUALITY: ClearType aksine arka plan rengi gerektirmez
        fontNorm  = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        fontBold  = CreateFontA(-13, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        fontSmall = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");

        SetBkMode(memDC, TRANSPARENT);
        ready = true;
        return true;
    }

    // Boyut değişikliğinde arka tamponu yenile
    void Resize(int w, int h) {
        if (w <= 0 || h <= 0) return;
        width = w; height = h;
        if (oldBmp) { SelectObject(memDC, oldBmp); oldBmp = nullptr; }
        if (memBmp) { DeleteObject(memBmp);        memBmp = nullptr; }
        if (memDC)  { DeleteDC(memDC);             memDC  = nullptr; }

        HDC screenDC = GetDC(NULL);
        if (!screenDC) return;
        memDC  = CreateCompatibleDC(screenDC);
        memBmp = CreateCompatibleBitmap(screenDC, w, h);
        ReleaseDC(NULL, screenDC);

        if (memDC && memBmp) {
            oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            SetBkMode(memDC, TRANSPARENT);
        }
    }

    // Compat stubs — Overlay.hpp çağırır, GDI'de RT kavramı yok
    void ReleaseRT() {}
    bool CreateRT(void* = nullptr) { return ready; }

    void Cleanup() {
        ready = false;
        if (memDC && oldBmp) { SelectObject(memDC, oldBmp); oldBmp = nullptr; }
        if (memBmp)  { DeleteObject(memBmp);  memBmp  = nullptr; }
        if (memDC)   { DeleteDC(memDC);       memDC   = nullptr; }
        if (fontNorm)  { DeleteObject(fontNorm);  fontNorm  = nullptr; }
        if (fontBold)  { DeleteObject(fontBold);  fontBold  = nullptr; }
        if (fontSmall) { DeleteObject(fontSmall); fontSmall = nullptr; }
        hWnd = nullptr;
    }

    // ── Frame ────────────────────────────────────────────────
    // BeginFrame: arka tamponu colorkey rengiyle doldur → DWM bu pikseli şeffaf yapar
    void BeginFrame() {
        if (!ready || !memDC) return;
        inFrame = true;
        HBRUSH br = CreateSolidBrush(RGB(0, 0, 2));
        RECT   rc = { 0, 0, width, height };
        FillRect(memDC, &rc, br);
        DeleteObject(br);
    }

    // EndFrame: arka tamponu pencereye kopyala, DC'yi hemen serbest bırak
    // DWM WS_EX_LAYERED compositing'i ReleaseDC'nin ardından güncellenir
    void EndFrame() {
        if (!ready || !inFrame || !memDC || !hWnd) return;
        inFrame = false;
        HDC wdc = GetDC(hWnd);
        if (wdc) {
            BitBlt(wdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            ReleaseDC(hWnd, wdc);
        }
    }

    // ── Çizim ────────────────────────────────────────────────
    void Line(float x1, float y1, float x2, float y2,
              D2D1_COLOR_F col, float w = 1.5f) {
        if (!memDC) return;
        int pw = (int)(w + 0.5f); if (pw < 1) pw = 1;
        HPEN pen = CreatePen(PS_SOLID, pw, CR(col));
        HPEN old = (HPEN)SelectObject(memDC, pen);
        MoveToEx(memDC, (int)x1, (int)y1, nullptr);
        LineTo(memDC,   (int)x2, (int)y2);
        SelectObject(memDC, old);
        DeleteObject(pen);
    }

    void Rect(float l, float t, float r2, float b2,
              D2D1_COLOR_F col, float w = 1.5f) {
        if (!memDC) return;
        int pw = (int)(w + 0.5f); if (pw < 1) pw = 1;
        HPEN   pen  = CreatePen(PS_SOLID, pw, CR(col));
        HPEN   oldP = (HPEN)  SelectObject(memDC, pen);
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        ::Rectangle(memDC, (int)l, (int)t, (int)r2, (int)b2);
        SelectObject(memDC, oldP);
        SelectObject(memDC, oldB);
        DeleteObject(pen);
    }

    void RectFilled(float l, float t, float r2, float b2, D2D1_COLOR_F col) {
        if (!memDC || col.a < 0.01f) return;
        int x = (int)l, y = (int)t, rr = (int)r2, bb = (int)b2;
        int ww = rr - x, hh = bb - y;
        if (ww <= 0 || hh <= 0) return;

        if (col.a >= 0.99f) {
            HBRUSH br = CreateSolidBrush(CR(col));
            RECT   rc = { x, y, rr, bb };
            FillRect(memDC, &rc, br);
            DeleteObject(br);
        } else {
            // AlphaBlend: geçici bitmap ekran DC ile oluşturulur
            HDC     screenDC = GetDC(NULL);
            HDC     tmp      = CreateCompatibleDC(screenDC);
            HBITMAP bmp      = CreateCompatibleBitmap(screenDC, ww, hh);
            ReleaseDC(NULL, screenDC);

            if (tmp && bmp) {
                HBITMAP oB  = (HBITMAP)SelectObject(tmp, bmp);
                HBRUSH  br  = CreateSolidBrush(CR(col));
                RECT    rc  = { 0, 0, ww, hh };
                FillRect(tmp, &rc, br);
                DeleteObject(br);
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, Alpha(col), 0 };
                AlphaBlend(memDC, x, y, ww, hh, tmp, 0, 0, ww, hh, bf);
                SelectObject(tmp, oB);
            }
            if (bmp) DeleteObject(bmp);
            if (tmp) DeleteDC(tmp);
        }
    }

    void RectFillAlpha(float l, float t, float r2, float b2, D2D1_COLOR_F col) {
        RectFilled(l, t, r2, b2, col);
    }

    void Circle(float cx, float cy, float radius,
                D2D1_COLOR_F col, float w = 1.5f) {
        if (!memDC || radius < 0.5f) return;
        int pw = (int)(w + 0.5f); if (pw < 1) pw = 1;
        HPEN   pen  = CreatePen(PS_SOLID, pw, CR(col));
        HPEN   oldP = (HPEN)  SelectObject(memDC, pen);
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Ellipse(memDC,
            (int)(cx - radius), (int)(cy - radius),
            (int)(cx + radius), (int)(cy + radius));
        SelectObject(memDC, oldP);
        SelectObject(memDC, oldB);
        DeleteObject(pen);
    }

    void CircleFilled(float cx, float cy, float radius, D2D1_COLOR_F col) {
        if (!memDC || radius < 0.5f) return;
        HBRUSH br   = CreateSolidBrush(CR(col));
        HPEN   oldP = (HPEN)  SelectObject(memDC, GetStockObject(NULL_PEN));
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, br);
        Ellipse(memDC,
            (int)(cx - radius), (int)(cy - radius),
            (int)(cx + radius), (int)(cy + radius));
        SelectObject(memDC, oldP);
        SelectObject(memDC, oldB);
        DeleteObject(br);
    }

    // L-şekilli köşe kutu
    void CornerRect(float l, float t, float r2, float b2,
                    D2D1_COLOR_F col, float cs = -1.f, float w = 2.f) {
        float W = r2 - l;
        float c = (cs > 0.f) ? cs : W * 0.3f;
        RectFilled(l, t, r2, b2, D2D1::ColorF(0, 0, 0, 0.20f));
        Rect(l - 1.f, t - 1.f, r2 + 1.f, b2 + 1.f, D2D1::ColorF(0, 0, 0, 1.f), 1.f);
        Line(l,  t,  l+c, t,   col, w); Line(l,  t,  l,  t+c, col, w);
        Line(r2, t,  r2-c,t,   col, w); Line(r2, t,  r2, t+c, col, w);
        Line(l,  b2, l+c, b2,  col, w); Line(l,  b2, l,  b2-c,col, w);
        Line(r2, b2, r2-c,b2,  col, w); Line(r2, b2, r2, b2-c,col, w);
    }

    // ── Metin ────────────────────────────────────────────────
    void Text(float x, float y, D2D1_COLOR_F col,
              const char* txt, bool bold = false) {
        if (!memDC || !txt || !*txt) return;
        HFONT font = bold ? fontBold : fontNorm;
        if (!font) return;
        HFONT oldF = (HFONT)SelectObject(memDC, font);
        SetTextColor(memDC, CR(col));
        RECT rc = { (int)x, (int)y, (int)x + 600, (int)y + 26 };
        DrawTextA(memDC, txt, -1, &rc, DT_LEFT | DT_TOP | DT_NOCLIP | DT_SINGLELINE);
        SelectObject(memDC, oldF);
    }

    void TextShadow(float x, float y, D2D1_COLOR_F col,
                    const char* txt, bool bold = false) {
        D2D1_COLOR_F sh = D2D1::ColorF(0, 0, 0, 1.f);
        Text(x + 1.5f, y + 1.5f, sh, txt, bold);
        Text(x - 1.5f, y + 1.5f, sh, txt, bold);
        Text(x + 1.5f, y - 1.5f, sh, txt, bold);
        Text(x - 1.5f, y - 1.5f, sh, txt, bold);
        Text(x, y, col, txt, bold);
    }

    void TextCentered(float cx, float cy, D2D1_COLOR_F col, const char* txt) {
        float est = (float)strlen(txt) * 7.2f;
        TextShadow(cx - est * 0.5f, cy - 6.5f, col, txt);
    }

    float TextWidth(const char* txt, bool bold = false) {
        if (!memDC || !txt || !*txt) return 0.f;
        HFONT font = bold ? fontBold : fontNorm;
        if (!font) return (float)(strlen(txt) * 7);
        HFONT oldF = (HFONT)SelectObject(memDC, font);
        SIZE  sz   = {};
        GetTextExtentPoint32A(memDC, txt, (int)strlen(txt), &sz);
        SelectObject(memDC, oldF);
        return (float)sz.cx;
    }

    float TextHeight() { return 14.f; }

} g_D3DR;
