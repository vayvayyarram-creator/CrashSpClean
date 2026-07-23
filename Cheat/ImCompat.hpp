#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

// ── Temel vektör tipleri ──────────────────────────────────────
struct ImVec2 {
    float x = 0, y = 0;
    ImVec2() = default;
    ImVec2(float _x, float _y) : x(_x), y(_y) {}
    ImVec2 operator+(ImVec2 o) const { return {x+o.x, y+o.y}; }
    ImVec2 operator-(ImVec2 o) const { return {x-o.x, y-o.y}; }
    ImVec2 operator*(float s) const  { return {x*s, y*s}; }
    float  Length() const { return sqrtf(x*x + y*y); }
};
struct ImVec4 {
    float x = 0, y = 0, z = 0, w = 1;
    ImVec4() = default;
    ImVec4(float _x,float _y,float _z,float _w) : x(_x),y(_y),z(_z),w(_w) {}
};

// ── Renk tipleri ─────────────────────────────────────────────
typedef unsigned int ImU32;

// ImU32 format: 0xAABBGGRR
inline ImU32 IM_COL32(int r, int g, int b, int a=255) {
    return ((ImU32)a << 24) | ((ImU32)b << 16) | ((ImU32)g << 8) | (ImU32)r;
}

struct ImColor {
    ImVec4 Value;
    ImColor() : Value(1,1,1,1) {}
    ImColor(int r, int g, int b, int a=255)
        : Value(r/255.f, g/255.f, b/255.f, a/255.f) {}
    ImColor(float r, float g, float b, float a=1.f)
        : Value(r, g, b, a) {}
    explicit ImColor(ImU32 u)
        : Value(((u>>0)&0xFF)/255.f, ((u>>8)&0xFF)/255.f,
                ((u>>16)&0xFF)/255.f, ((u>>24)&0xFF)/255.f) {}

    // ImU32 dönüşümü
    operator ImU32() const {
        int r=(int)(Value.x*255+.5f), g=(int)(Value.y*255+.5f),
            b=(int)(Value.z*255+.5f), a=(int)(Value.w*255+.5f);
        r=std::max(0,std::min(255,r)); g=std::max(0,std::min(255,g));
        b=std::max(0,std::min(255,b)); a=std::max(0,std::min(255,a));
        return IM_COL32(r,g,b,a);
    }
    operator ImVec4() const { return Value; }
};

// ── ImMin / ImMax yardımcıları ────────────────────────────────
template<class T> inline T ImMin(T a, T b) { return a < b ? a : b; }
template<class T> inline T ImMax(T a, T b) { return a > b ? a : b; }

// ── Overlay IO — yalnızca DisplaySize kullanılır ─────────────
struct OverlayIO {
    ImVec2 DisplaySize;
    float  DeltaTime = 0.016f;
};

// ── Bağımsız zaman fonksiyonu ─────────────────────────────────
inline double FudGetTime() {
    static ULONGLONG start = 0;
    if (!start) start = GetTickCount64();
    return (double)(GetTickCount64() - start) / 1000.0;
}

// ── ImDrawList yardımcı sabitleri ─────────────────────────────
static constexpr float IM_PI = 3.14159265358979f;
