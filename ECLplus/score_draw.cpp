/*
 * score_draw.cpp —— v10：战绩页符卡表【自绘覆盖层】
 *
 * 为什么不再借游戏自己的文字系统：
 *   v9 用 AsciiManager::create_string / 接管 0x4587F0 全部失败 —— thcrap 用"断点 + codecave"
 *   改写那一带的代码（它要翻译 0x4A2934 那个格式串），任何改代码的补丁都会被覆盖或冲突；
 *   而接管绘制又会让每行 VM 的文字初始化被跳过 → AnmManager::draw_vm 崩溃。
 *
 * 现在这条路线（thprac 式）：
 *   · 只用 ECLplus 自己的 D3D9 EndScene 钩子（每帧都在跑，已验证）
 *   · 黑底 = SetFVF(XYZRHW|DIFFUSE) + DrawPrimitiveUP（2 个三角形）；文字 = d3dx9 的 ID3DXFont
 *   · 坐标写在 ini 里，按"逻辑 640x480"给，运行时乘 (后备缓冲宽/640) → 分辨率无关
 *   · 零游戏代码补丁 → 不需要屏蔽 thcrap，也不会崩
 */
#include "pch.h"
#include <d3d9.h>   /* 只用类型/常量，不用 d3dx9 头（字体走 GetProcAddress 动态取） */
#include "score_draw.h"
#include "score_ui.h"

/* ---------------- d3dx9_43.dll 动态加载（不引入 d3dx9 头/库） ---------------- */
typedef HRESULT(WINAPI* tD3DXCreateFontA)(void* dev, INT height, UINT width, UINT weight, UINT mipLevels,
    BOOL italic, DWORD charSet, DWORD outPrecision, DWORD quality, DWORD pitchAndFamily,
    LPCSTR faceName, void** font);
typedef HRESULT(STDMETHODCALLTYPE* tFontDrawTextA)(void* self, LPCSTR str, INT count, RECT* rect, DWORD format, D3DCOLOR color);
typedef HRESULT(STDMETHODCALLTYPE* tFontOnLostDevice)(void* self);
typedef HRESULT(STDMETHODCALLTYPE* tFontOnResetDevice)(void* self);
typedef ULONG(STDMETHODCALLTYPE* tRelease)(void* self);

extern IDirect3DDevice9* g_pDevice;   /* ECLplus.cpp 里的设备指针 */
static tD3DXCreateFontA g_createFont = 0;
static void*  g_font = 0;
static int    g_fontPx = 0;
static int    g_fontCharset = -1;
static char   g_fontFace[64] = { 0 };
static int    g_fontLost = 0;

/* ---------------- 顶点（屏幕空间） ---------------- */
struct TLVert { float x, y, z, rhw; D3DCOLOR color; };
#define TL_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

/* ---------------- 设备 vtable 手动调用 ---------------- */
static HRESULT DevGetViewport(void* dev, D3DVIEWPORT9* vp) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, D3DVIEWPORT9*);
    return ((t)((void**)dev)[48])(dev, vp);   /* GetViewport=48（47 是 SetViewport） */
}
static HRESULT DevSetFVF(void* dev, DWORD fvf) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, DWORD);
    return ((t)((void**)dev)[89])(dev, fvf);
}
static HRESULT DevDrawPrimitiveUP(void* dev, D3DPRIMITIVETYPE pt, UINT count, const void* data, UINT stride) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    return ((t)((void**)dev)[83])(dev, pt, count, data, stride);
}
static DWORD DevGetRenderState(void* dev, D3DRENDERSTATETYPE rs) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, D3DRENDERSTATETYPE, DWORD*);
    DWORD v = 0;
    ((t)((void**)dev)[58])(dev, rs, &v);
    return v;
}
static void DevSetRenderState(void* dev, D3DRENDERSTATETYPE rs, DWORD v) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, D3DRENDERSTATETYPE, DWORD);
    ((t)((void**)dev)[57])(dev, rs, v);
}
static void* DevGetTexture(void* dev, DWORD stage) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, DWORD, void**);
    void* tex = 0;
    ((t)((void**)dev)[64])(dev, stage, &tex);
    return tex;
}
static void DevSetTexture(void* dev, DWORD stage, void* tex) {
    typedef HRESULT(STDMETHODCALLTYPE* t)(void*, DWORD, void*);
    ((t)((void**)dev)[65])(dev, stage, tex);
}

/* ---------------- 字体 ---------------- */
static void FontRelease(void) {
    if (g_font) {
        ((tRelease)((void**)g_font)[2])(g_font);
        g_font = 0;
    }
}

void ScoreDrawReset(void) {
    if (g_font) {
        ((tFontOnLostDevice)((void**)g_font)[16])(g_font);
        FontRelease();
    }
    g_fontPx = 0;
    g_fontCharset = -1;
}

static int EnsureFont(void* dev, int px, const char* face, int charset) {
    if (g_font && g_fontPx == px && g_fontCharset == charset && !strcmp(g_fontFace, face)) return 1;
    FontRelease();
    g_fontPx = 0;
    if (!g_createFont) {
        HMODULE h = LoadLibraryA("d3dx9_43.dll");
        if (!h) { UiLogSafe("[draw] 找不到 d3dx9_43.dll，无法自绘文字"); return 0; }
        g_createFont = (tD3DXCreateFontA)GetProcAddress(h, "D3DXCreateFontA");
        if (!g_createFont) { UiLogSafe("[draw] d3dx9_43.dll 里没有 D3DXCreateFontA"); return 0; }
    }
    if (px < 6) px = 6;
    if (px > 200) px = 200;
    void* f = 0;
    /* height, width, weight, miplevels, italic, charset, outprec, quality, pitchfamily, face */
    HRESULT hr = g_createFont(dev, px, 0, FW_NORMAL, 1, FALSE, (DWORD)charset, 0 /*DEFAULT*/,
                              0 /*DEFAULT*/, 0 /*DEFAULT_PITCH|FF_DONTCARE*/, face, &f);
    if (FAILED(hr) || !f) { UiLogSafe("[draw] D3DXCreateFontA 失败 hr=%#08X face=%s", (unsigned)hr, face); return 0; }
    g_font = f;
    g_fontPx = px;
    g_fontCharset = charset;
    lstrcpynA(g_fontFace, face, sizeof(g_fontFace));
    UiLogSafe("[draw] 字体已创建：%s 高=%dpx charset=%d", face, px, charset);
    return 1;
}

/* ---------------- 黑底 ---------------- */
static void FillRect(void* dev, float x0, float y0, float x1, float y1, D3DCOLOR c) {
    if (x1 <= x0 || y1 <= y0) return;
    TLVert v[4];
    v[0].x = x0 - 0.5f; v[0].y = y0 - 0.5f; v[0].z = 0.0f; v[0].rhw = 1.0f; v[0].color = c;
    v[1].x = x1 - 0.5f; v[1].y = y0 - 0.5f; v[1].z = 0.0f; v[1].rhw = 1.0f; v[1].color = c;
    v[2].x = x0 - 0.5f; v[2].y = y1 - 0.5f; v[2].z = 0.0f; v[2].rhw = 1.0f; v[2].color = c;
    v[3].x = x1 - 0.5f; v[3].y = y1 - 0.5f; v[3].z = 0.0f; v[3].rhw = 1.0f; v[3].color = c;
    DevSetFVF(dev, TL_FVF);
    DevDrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLVert));
}

/* 由监控线程调用（不在 BeginScene/EndScene 中间，建字体更安全） */
void ScoreDrawPrepare(void) {
    if (g_font || !g_pDevice) return;
    if (!SpellTableDrawEnabled()) return;
    int fh = 18, cs = 134; char face[64] = { 0 };
    int a=0,b=0,c=0,d=0,e=0,f=0,g=0; unsigned col=0, bg=0;
    SpellTableDrawParams(&a,&b,&c,&d,&e,&f,&g,&fh, face, (int)sizeof(face), &cs, &col, &bg);
    D3DVIEWPORT9 vp;
    int px = fh;
    if (SUCCEEDED(DevGetViewport(g_pDevice, &vp)) && vp.Width >= 64) px = (int)(fh * ((float)vp.Width / 640.0f) + 0.5f);
    EnsureFont(g_pDevice, px, face, cs);
}

/* ---------------- 每帧入口（内部实现） ---------------- */
static void ScoreDrawFrameInner(void* device) {
    void* dev = device;
    static unsigned frame = 0;
    frame++;
    if (!dev) return;
    if (!SpellTableDrawEnabled()) return;
    if (!SpellTableIsActivePage()) return;

    D3DVIEWPORT9 vp;
    if (FAILED(DevGetViewport(dev, &vp)) || vp.Width < 64 || vp.Height < 64) return;
    const float s = (float)vp.Width / 640.0f;      /* 逻辑 640x480 → 后备缓冲 */

    int rx = 0, ry0 = 0, rdy = 0, bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, fh = 0;
    char face[64] = { 0 };
    int charset = 134;
    unsigned color = 0xFFFFF0A0u, bg = 0xFF000000u;
    SpellTableDrawParams(&rx, &ry0, &rdy, &bx0, &by0, &bx1, &by1, &fh,
                         face, (int)sizeof(face), &charset, &color, &bg);

    if (!g_font) return;                      /* 字体由监控线程准备好（不在场景里创建资源） */
    {   /* 校验 DrawTextA 这个 vtable 槽非空，避免跳到 0 地址 */
        void* fn = ((void**)g_font)[14];
        if (!fn) { if (!g_fontLost) { g_fontLost = 1; UiLogSafe("[draw] ID3DXFont::DrawTextA 槽为空（索引猜错），跳过绘制"); } return; }
    }

    /* 保存状态 */
    DWORD oldZ = DevGetRenderState(dev, D3DRS_ZENABLE);
    DWORD oldAB = DevGetRenderState(dev, D3DRS_ALPHABLENDENABLE);
    DWORD oldLT = DevGetRenderState(dev, D3DRS_LIGHTING);
    DWORD oldCull = DevGetRenderState(dev, D3DRS_CULLMODE);
    void* oldTex = DevGetTexture(dev, 0);

    DevSetRenderState(dev, D3DRS_ZENABLE, D3DZB_FALSE);
    DevSetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    DevSetRenderState(dev, D3DRS_LIGHTING, FALSE);
    DevSetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    DevSetTexture(dev, 0, 0);

    /* 不透明黑底（盖掉游戏自己那份列表） */
    FillRect(dev, bx0 * s, by0 * s, bx1 * s, by1 * s, (D3DCOLOR)bg);

    /* 逐行文字 */
    int drawn = 0;
    for (int i = 0; i < 32; i++) {
        char text[192];
        unsigned rowColor = color;
        if (!SpellTableFormatRow(i, text, (int)sizeof(text), &rowColor)) break;
        RECT r;
        r.left = (LONG)(rx * s);
        r.top = (LONG)((ry0 + i * rdy) * s);
        r.right = (LONG)vp.Width;
        r.bottom = r.top + (LONG)(fh * s) + 8;
        HRESULT hr = ((tFontDrawTextA)((void**)g_font)[14])(g_font, text, -1, &r,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP, (D3DCOLOR)rowColor);
        if (hr == D3DERR_DEVICELOST) { g_fontLost = 1; break; }
        drawn++;
    }

    /* 还原状态 */
    DevSetTexture(dev, 0, oldTex);
    DevSetRenderState(dev, D3DRS_CULLMODE, oldCull);
    DevSetRenderState(dev, D3DRS_LIGHTING, oldLT);
    DevSetRenderState(dev, D3DRS_ALPHABLENDENABLE, oldAB);
    DevSetRenderState(dev, D3DRS_ZENABLE, oldZ);

    if (frame <= 3 || (frame % 300) == 0) {
        UiLogSafe("[draw] 帧=%u 视口=%dx%d 缩放=%d%% 画了 %d 行 底=%d,%d-%d,%d 字高=%d 丢失=%d",
                  frame, (int)vp.Width, (int)vp.Height, (int)(s * 100), drawn,
                  bx0, by0, bx1, by1, (int)(fh * s), g_fontLost);
    }
}

/* 外层：任何异常都拦住，绝不让游戏崩 */
void ScoreDrawFrame(void* device) {
    __try {
        ScoreDrawFrameInner(device);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int once = 0;
        if (!once) { once = 1; UiLogSafe("[draw] 自绘里出了异常，已拦住（后续不再尝试绘制）"); }
        SpellTableSetDrawOff();
    }
}
