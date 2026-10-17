#include "pch.h"
#include "ECLplus.h"
#include "ins2400.h"

extern IDirect3DDevice9* g_pDevice;

/* ============================================================
 * Line drawing, integrated into the game's render pipeline.
 *
 * ECL -> screen mapping (single source of truth: GetLineMap2400):
 *   screen = (ax*x + bx, ay*y + by)
 *   640x480 logical space: playfield = (32,16)-(416,464),
 *   ECL origin (0,0) = playfield top-center.  @1280x960:
 *     x=-192 -> 64    x=0 -> 448    x=+192 -> 832
 *     y=0    -> 32    y=224 -> 480  y=448  -> 928
 *
 * Channels:
 *   FRONT (default) : layer >= 0 -> above bullets/boss
 *   BACK            : layer <  0 -> below bullets/boss
 *
 * ROUTE_A_BACK_HOOK == 0 : both channels are drawn at the green-line
 *   render point (coordinates verified correct, but above the sprites).
 * ROUTE_A_BACK_HOOK == 1 : BACK goes back to its own hook, with the
 *   fixed x compensation measured there (384 px @ 2x = 192 units).
 * ============================================================ */

 /* ---------- switches (the only things you normally touch) ---------- */
#define LINE_SELFTEST     0     /* 1 = calibration geometry (magenta cross) */
#define LINE_CALIB_LOG    0      /* 1 = log rt/viewport/mapping every ~2 s */
#define SHOW_TOP_MARKER   0    /* 1 = green debug line at y=320 */
#define SHOW_BORDER_LINE  0      /* 1 = stage border line (yellow frame at the playfield edges) */

#define ROUTE_A_BACK_HOOK 0      /* 0 = working config (draw all at green-line point)
                                    1 = ROUTE A TEST (BACK at its own hook + comp) */
#define BACK_HOOK_COMP_X  192.0f /* logical units of x compensation (384 px @2x);
                                    used only when ROUTE_A_BACK_HOOK == 1 */

DWORD g_lineColor = 0xFFFFFFFF;
float g_lineLayer = 0.0f;

LineDrawItem g_lineBuffer[MAX_LINE_VERTEX_PAIRS];   /* FRONT channel */
int g_lineCount = 0;

LineDrawItem g_backBuffer[MAX_LINE_VERTEX_PAIRS];   /* BACK channel */
int g_backCount = 0;

void ResetLineBuffer() {
    g_lineCount = 0;
    g_backCount = 0;
}

/* ---------- shared mapping ---------- */

struct LineMap2400 {
    float ax, bx, ay, by;
    unsigned rtW, rtH;
};

static LineMap2400 GetLineMap2400(IDirect3DDevice9* pDevice) {
    LineMap2400 m = { 2.0f, 448.0f, 2.0f, 32.0f, 1280, 960 };
    IDirect3DSurface9* rt = nullptr;
    D3DSURFACE_DESC rd = {};
    pDevice->GetRenderTarget(0, &rt);
    if (rt) { rt->GetDesc(&rd); rt->Release(); }
    if (rd.Width == 0) { rd.Width = 640; rd.Height = 480; }
    m.rtW = rd.Width;
    m.rtH = rd.Height;
    m.ax = (float)rd.Width / 640.0f;   /* 2.0 @1280x960 */
    m.bx = 224.0f * m.ax;              /* x=0 -> 448 px  */
    m.ay = (float)rd.Height / 480.0f;  /* 2.0 @1280x960 */
    m.by = 16.0f * m.ay;               /* y=0 ->  32 px  */
    return m;
}

struct LineVertex2400 { float x, y, z, rhw; DWORD color; };

static void PushLine2400(IDirect3DDevice9* pDevice,
    float sx1, float sy1, float sx2, float sy2, DWORD color) {
    LineVertex2400 verts[2] = {
        { sx1, sy1, 0.5f, 1.0f, color },
        { sx2, sy2, 0.5f, 1.0f, color }
    };
    pDevice->DrawPrimitiveUP(D3DPT_LINELIST, 1, verts, sizeof(LineVertex2400));
}

static void DrawList2400(IDirect3DDevice9* pDevice, LineDrawItem* buf, int& count) {
    if (!pDevice) return;
#if LINE_SELFTEST
    /* self-test geometry is drawn even if the script submitted nothing */
#else
    if (count == 0) return;
#endif
    const bool isBack = (buf == g_backBuffer);

    const LineMap2400 m = GetLineMap2400(pDevice);

    /* x base actually used for drawing; Route A compensates the BACK hook */
    float bxDraw = m.bx;
#if ROUTE_A_BACK_HOOK
    if (isBack) bxDraw += BACK_HOOK_COMP_X * m.ax;
#endif

#if LINE_CALIB_LOG
    static int logBack = 0, logFront = 0;
    int& logN = isBack ? logBack : logFront;
    if (++logN % 120 == 1) {
        D3DVIEWPORT9 v = {};
        pDevice->GetViewport(&v);
        char b[220];
        snprintf(b, 220,
            "2400 %s rt=%ux%u ax=%.1f bx=%.1f compPx=%.0f n=%d vp0=(%u,%u,%u,%u)\n",
            isBack ? "BACK" : "FRONT", m.rtW, m.rtH, m.ax, bxDraw,
            bxDraw - m.bx, count,
            (unsigned)v.X, (unsigned)v.Y, (unsigned)v.Width, (unsigned)v.Height);
        EclPrint(b);
    }
#endif

    /* Force a fullscreen viewport WHILE WE DRAW, restore it at the end. */
    D3DVIEWPORT9 oldVp;
    pDevice->GetViewport(&oldVp);
    D3DVIEWPORT9 fullVp = { 0, 0, m.rtW, m.rtH, 0.0f, 1.0f };
    pDevice->SetViewport(&fullVp);

    /* insertion sort by layer (ascending: larger layer = drawn later/on top) */
    for (int i = 1; i < count; i++) {
        LineDrawItem key = buf[i];
        int j = i - 1;
        while (j >= 0 && buf[j].layer > key.layer) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = key;
    }

    /* save states */
    DWORD oldFVF;
    DWORD oldLighting, oldCull, oldAlphaBlend, oldSrcBlend, oldDestBlend, oldBlendOp;
    DWORD oldZEnable, oldZWrite, oldAlphaTest, oldScissor, oldStencil, oldFog;
    DWORD oldColorOp, oldColorArg1, oldAlphaOp, oldAlphaArg1;
    IDirect3DBaseTexture9* oldTex = nullptr;
    IDirect3DVertexShader9* oldVS = nullptr;
    IDirect3DPixelShader9* oldPS = nullptr;

    pDevice->GetFVF(&oldFVF);
    pDevice->GetRenderState(D3DRS_LIGHTING, &oldLighting);
    pDevice->GetRenderState(D3DRS_CULLMODE, &oldCull);
    pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
    pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
    pDevice->GetRenderState(D3DRS_BLENDOP, &oldBlendOp);
    pDevice->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
    pDevice->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    pDevice->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
    pDevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissor);
    pDevice->GetRenderState(D3DRS_STENCILENABLE, &oldStencil);
    pDevice->GetRenderState(D3DRS_FOGENABLE, &oldFog);
    pDevice->GetTexture(0, &oldTex);
    pDevice->GetTextureStageState(0, D3DTSS_COLOROP, &oldColorOp);
    pDevice->GetTextureStageState(0, D3DTSS_COLORARG1, &oldColorArg1);
    pDevice->GetTextureStageState(0, D3DTSS_ALPHAOP, &oldAlphaOp);
    pDevice->GetTextureStageState(0, D3DTSS_ALPHAARG1, &oldAlphaArg1);
    pDevice->GetVertexShader(&oldVS);
    pDevice->GetPixelShader(&oldPS);

    pDevice->SetVertexShader(NULL);
    pDevice->SetPixelShader(NULL);
    pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    pDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    pDevice->SetTexture(0, NULL);
    pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

#if LINE_SELFTEST
    /* ---- calibration geometry (set LINE_SELFTEST 0 to remove) ----
       FRONT : magenta center vertical + middle horizontal (448 / 480)
       (the cyan playfield border used to be drawn here; it now lives in
        DrawBorderLine2400 behind the SHOW_BORDER_LINE switch) */
    if (!isBack) {
        PushLine2400(pDevice, m.ax * 0.0f + bxDraw, m.ay * 0.0f + m.by,
            m.ax * 0.0f + bxDraw, m.ay * 448.0f + m.by, 0xFFFF00FF);
        PushLine2400(pDevice, m.ax * -192.0f + bxDraw, m.ay * 224.0f + m.by,
            m.ax * 192.0f + bxDraw, m.ay * 224.0f + m.by, 0xFFFF00FF);
    }
#endif

    for (int i = 0; i < count; i++) {
        float sx1 = m.ax * buf[i].x1 + bxDraw;
        float sy1 = m.ay * buf[i].y1 + m.by;
        float sx2 = m.ax * buf[i].x2 + bxDraw;
        float sy2 = m.ay * buf[i].y2 + m.by;
        PushLine2400(pDevice, sx1, sy1, sx2, sy2, buf[i].color);
    }

    /* restore states */
    pDevice->SetRenderState(D3DRS_LIGHTING, oldLighting);
    pDevice->SetRenderState(D3DRS_CULLMODE, oldCull);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
    pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
    pDevice->SetRenderState(D3DRS_BLENDOP, oldBlendOp);
    pDevice->SetRenderState(D3DRS_ZENABLE, oldZEnable);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
    pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissor);
    pDevice->SetRenderState(D3DRS_STENCILENABLE, oldStencil);
    pDevice->SetRenderState(D3DRS_FOGENABLE, oldFog);
    pDevice->SetTexture(0, oldTex);
    pDevice->SetTextureStageState(0, D3DTSS_COLOROP, oldColorOp);
    pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, oldColorArg1);
    pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, oldAlphaOp);
    pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, oldAlphaArg1);
    pDevice->SetFVF(oldFVF);
    pDevice->SetVertexShader(oldVS);
    pDevice->SetPixelShader(oldPS);

    if (oldTex) oldTex->Release();
    if (oldVS)  oldVS->Release();
    if (oldPS)  oldPS->Release();

    pDevice->SetViewport(&oldVp);

    count = 0;
}

void DrawAllLines2400(IDirect3DDevice9* pDevice) {
    DrawList2400(pDevice, g_lineBuffer, g_lineCount);
}

static void DrawBackLines2400(IDirect3DDevice9* pDevice) {
    DrawList2400(pDevice, g_backBuffer, g_backCount);
}

/* temporary: print viewport + render target once (English only, keep as is) */
static void PrintViewportOnce(const char* tag) {
    if (!g_pDevice) return;
    D3DVIEWPORT9 v = {};
    g_pDevice->GetViewport(&v);
    IDirect3DSurface9* rt = nullptr;
    g_pDevice->GetRenderTarget(0, &rt);
    D3DSURFACE_DESC d = {};
    if (rt) { rt->GetDesc(&d); rt->Release(); }
    char buf[160];
    snprintf(buf, 160, "%s vp=(%u,%u,%u,%u) rt=%ux%u\n",
        tag, v.X, v.Y, v.Width, v.Height, d.Width, d.Height);
    EclPrint(buf);
}

void FlushBackLines2400() {
#if ROUTE_A_BACK_HOOK
    /* Route A: BACK is drawn here, at its own (old) hook */
    DrawBackLines2400(g_pDevice);
#endif
    /* ROUTE_A_BACK_HOOK == 0: drawn at TopMarker2400 instead */
}

void FlushFrontLines2400() {
#if ROUTE_A_BACK_HOOK
    /* Route A: FRONT keeps its own hook */
    DrawAllLines2400(g_pDevice);
#endif
    /* ROUTE_A_BACK_HOOK == 0: drawn at TopMarker2400 instead */
}

/* ===== top debug marker (green) ===== */

static void DrawMarker2400(DWORD color, float gy) {
    IDirect3DDevice9* pDevice = g_pDevice;
    if (!pDevice) return;

    IDirect3DSurface9* rt = nullptr;
    D3DSURFACE_DESC rd = {};
    pDevice->GetRenderTarget(0, &rt);
    if (rt) { rt->GetDesc(&rd); rt->Release(); }
    if (rd.Width == 0) { rd.Width = 640; rd.Height = 480; }
    /* screen_px = A * coord + B  -- direct affine mapping */
    const float AX = (float)rd.Width / 640.0f;   /* 2.0 @1280x960 */
    const float BX = 224.0f * AX;                 /* x=0 -> 448 px */
    const float AY = (float)rd.Height / 480.0f;   /* 2.0 @1280x960 */
    const float BY = 16.0f * AY;                 /* y=0 ->  32 px */

    /* Force a fullscreen viewport WHILE WE DRAW, restore it at the end. */
    D3DVIEWPORT9 oldVp;
    pDevice->GetViewport(&oldVp);
    D3DVIEWPORT9 fullVp = { 0, 0, rd.Width, rd.Height, 0.0f, 1.0f };
    pDevice->SetViewport(&fullVp);

    DWORD oldFVF;
    DWORD oldLighting, oldCull, oldAlphaBlend, oldSrcBlend, oldDestBlend;
    DWORD oldZ, oldZWrite, oldScissor, oldAlphaTest;
    IDirect3DBaseTexture9* oldTex = nullptr;
    IDirect3DVertexShader9* oldVS = nullptr;
    IDirect3DPixelShader9* oldPS = nullptr;

    pDevice->GetFVF(&oldFVF);
    pDevice->GetRenderState(D3DRS_LIGHTING, &oldLighting);
    pDevice->GetRenderState(D3DRS_CULLMODE, &oldCull);
    pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
    pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
    pDevice->GetRenderState(D3DRS_ZENABLE, &oldZ);
    pDevice->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    pDevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissor);
    pDevice->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
    pDevice->GetTexture(0, &oldTex);
    pDevice->GetVertexShader(&oldVS);
    pDevice->GetPixelShader(&oldPS);

    pDevice->SetVertexShader(NULL);
    pDevice->SetPixelShader(NULL);
    pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    pDevice->SetTexture(0, NULL);

    struct LineVertex { float x, y, z, rhw; DWORD color; };
    LineVertex verts[2] = {
        { AX * -192.0f + BX, AY * gy + BY, 0.5f, 1.0f, color },
        { AX * 192.0f + BX, AY * gy + BY, 0.5f, 1.0f, color }
    };
    pDevice->DrawPrimitiveUP(D3DPT_LINELIST, 1, verts, sizeof(LineVertex));

    pDevice->SetRenderState(D3DRS_LIGHTING, oldLighting);
    pDevice->SetRenderState(D3DRS_CULLMODE, oldCull);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
    pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
    pDevice->SetRenderState(D3DRS_ZENABLE, oldZ);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissor);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
    pDevice->SetTexture(0, oldTex);
    pDevice->SetFVF(oldFVF);
    pDevice->SetVertexShader(oldVS);
    pDevice->SetPixelShader(oldPS);

    if (oldTex) oldTex->Release();
    if (oldVS)  oldVS->Release();
    if (oldPS)  oldPS->Release();

    pDevice->SetViewport(&oldVp);
}

#if SHOW_BORDER_LINE
/* ===== stage border line (own switch: SHOW_BORDER_LINE) =====
   Frame along the playfield edges: x = -192/+192, y = 0/448
   (-> 64/832 and 32/928 px @1280x960).
   Drawn at the same render point as the green debug marker, so it
   works in both ROUTE_A settings and stays visible during pause.
   To recolor, change the 0xFFFFFF00 below. */
static void DrawBorderLine2400() {
    IDirect3DDevice9* pDevice = g_pDevice;
    if (!pDevice) return;

    const LineMap2400 m = GetLineMap2400(pDevice);

    /* Force a fullscreen viewport WHILE WE DRAW, restore it at the end. */
    D3DVIEWPORT9 oldVp;
    pDevice->GetViewport(&oldVp);
    D3DVIEWPORT9 fullVp = { 0, 0, m.rtW, m.rtH, 0.0f, 1.0f };
    pDevice->SetViewport(&fullVp);

    DWORD oldFVF;
    DWORD oldLighting, oldCull, oldAlphaBlend, oldSrcBlend, oldDestBlend;
    DWORD oldZ, oldZWrite, oldScissor, oldAlphaTest;
    IDirect3DBaseTexture9* oldTex = nullptr;
    IDirect3DVertexShader9* oldVS = nullptr;
    IDirect3DPixelShader9* oldPS = nullptr;

    pDevice->GetFVF(&oldFVF);
    pDevice->GetRenderState(D3DRS_LIGHTING, &oldLighting);
    pDevice->GetRenderState(D3DRS_CULLMODE, &oldCull);
    pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
    pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
    pDevice->GetRenderState(D3DRS_ZENABLE, &oldZ);
    pDevice->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    pDevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissor);
    pDevice->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
    pDevice->GetTexture(0, &oldTex);
    pDevice->GetVertexShader(&oldVS);
    pDevice->GetPixelShader(&oldPS);

    pDevice->SetVertexShader(NULL);
    pDevice->SetPixelShader(NULL);
    pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    pDevice->SetTexture(0, NULL);

    const DWORD col = 0xFFFFFF00;             /* yellow, opaque */
    const float xl = m.ax * -192.0f + m.bx;   /* left   edge */
    const float xr = m.ax * 192.0f + m.bx;   /* right  edge */
    const float yt = m.ay * 0.0f + m.by;    /* top    edge */
    const float yb = m.ay * 448.0f + m.by;    /* bottom edge */

    PushLine2400(pDevice, xl, yt, xr, yt, col);
    PushLine2400(pDevice, xl, yb, xr, yb, col);
    PushLine2400(pDevice, xl, yt, xl, yb, col);
    PushLine2400(pDevice, xr, yt, xr, yb, col);

    pDevice->SetRenderState(D3DRS_LIGHTING, oldLighting);
    pDevice->SetRenderState(D3DRS_CULLMODE, oldCull);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
    pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
    pDevice->SetRenderState(D3DRS_ZENABLE, oldZ);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissor);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
    pDevice->SetTexture(0, oldTex);
    pDevice->SetFVF(oldFVF);
    pDevice->SetVertexShader(oldVS);
    pDevice->SetPixelShader(oldPS);

    if (oldTex) oldTex->Release();
    if (oldVS)  oldVS->Release();
    if (oldPS)  oldPS->Release();

    pDevice->SetViewport(&oldVp);
}
#endif

void TopMarker2400() {
#if SHOW_TOP_MARKER
    static bool printed = false;
    if (!printed) {
        printed = true;
        EclPrint("ECLplus: TOP MARKER (green) running\n");
    }
    DrawMarker2400(0xFF00FF00, 320.0f);
#endif

#if !ROUTE_A_BACK_HOOK
    /* working config: draw both channels in THIS context (same as the green line) */
    DrawBackLines2400(g_pDevice);
    DrawAllLines2400(g_pDevice);
#endif

#if SHOW_BORDER_LINE
    /* stage border line (own switch); drawn last so it stays visible on top */
    DrawBorderLine2400();
#endif
}

/* ===== ECL instruction ===== */

BOOL ins_2400(ENEMY* enm, INSTR* ins) {
    switch (ins->id) {
    case INS_LINE_SET: {
        int colorHex = GetIntArg(enm, 0);
        int alpha = GetIntArg(enm, 1);
        g_lineColor = ((alpha & 0xFF) << 24) |
            (((colorHex >> 16) & 0xFF) << 16) |
            (((colorHex >> 8) & 0xFF) << 8) |
            (colorHex & 0xFF);
        break;
    }
    case INS_LINE_ATTR: {
        g_lineLayer = GetFloatArg(enm, 0);
        break;
    }
    case INS_LINE_DRAW: {
        float x1 = GetFloatArg(enm, 0);
        float y1 = GetFloatArg(enm, 1);
        float x2 = GetFloatArg(enm, 2);
        float y2 = GetFloatArg(enm, 3);

        LineDrawItem* slot;
        if (g_lineLayer < 0.0f) {           /* BACK channel: below bullets/boss */
            if (g_backCount >= MAX_LINE_VERTEX_PAIRS) return FALSE;
            slot = &g_backBuffer[g_backCount++];
        }
        else {                              /* FRONT channel (default) */
            if (g_lineCount >= MAX_LINE_VERTEX_PAIRS) return FALSE;
            slot = &g_lineBuffer[g_lineCount++];
        }
        slot->x1 = x1;
        slot->y1 = y1;
        slot->x2 = x2;
        slot->y2 = y2;
        slot->color = g_lineColor;
        slot->layer = g_lineLayer;
        break;
    }
    default:
        return FALSE;
    }
    return TRUE;
}