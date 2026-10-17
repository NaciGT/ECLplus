/*
 * score_gui.cpp —— v20
 *  展开 5 帧"一条线上下展开" / 内部切换不重播 / 退出 1 帧直接消失
 *  行布局：No.+名字 贴左，M x/y 収率 P x/y 収率 紧凑打包贴右（GuiCols=0 时用整行字符串保底）
 */
#include "pch.h"
#include <d3d9.h>
#include "score_gui.h"
#include "score_ui.h"
#include "musiclock.h"
#include "snap.h"
#include "savecheck.h"
#include "imgui/imgui.h"
#include "imgui/score_imgui_dx9.h"

#define PTR_MAIN_MENU_G 0x004B77F0
#define WARMUP_FRAMES   90
#define ANIM_FRAMES     5
#define ANIM_FRAMES_OUT 1
#define ROW_MAX         64

static int   g_guiOn = 1;
static int   g_guiInited = 0;
static int   g_guiWarm = 0;
static int   g_guiFail = 0;
static int   g_guiX = 120, g_guiY = 96, g_guiW = 430, g_guiH = 250;
static float g_guiFontSize = 18.0f;
static float g_guiScale = 1.0f;
static unsigned g_guiText = 0xFFFFFFF0u;
static char  g_guiFontFile[260] = { 0 };
static unsigned g_guiFrames = 0;
static int   g_bgAlpha = 100, g_maxRows = 32, g_rowSpacing = 0, g_useCols = 1, g_rowCount = 0;
static unsigned g_outlineCol = 0xFF000000u;
static int      g_scroll = 0;      /* ini: GuiScroll（1=窗口可滚动） */
static int      g_ownView = 0;     /* ini: GuiOwnView（1=分组/分页自控；0=跟随游戏） */
static int      g_ourGroup = 0;
static int      g_ourPage = 0;
static float    g_mouseScale = 1.0f;
static int      g_wndHook = 0;
static volatile int g_wheelMsgs = 0;   /* 收到的 WM_MOUSEWHEEL 次数（诊断） */
static volatile int g_mouseMsgs = 0;   /* 收到的 WM_MOUSEMOVE 次数（诊断） */
static int      g_maskUnknown = 1;   /* ini: GuiMaskUnknown（1=未遭遇的符卡名字打码） */
static volatile int g_forceHideFrames = 0;   /* v28：按键触发"立即隐藏"的剩余帧数 */     /* ini: GuiWndHook（1=接管窗口消息，滚轮/点击可用；默认 0=不接管） */
static int      g_outlineOn = 1;
static int      g_outlineW = 2;   /* 描边粗细（像素） */
static void TextOutlined(ImVec4 col, const char* text) {
    if (!g_outlineOn) { ImGui::TextColored(col, "%s", text); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec4 oc(((g_outlineCol >> 16) & 0xFF) / 255.0f, ((g_outlineCol >> 8) & 0xFF) / 255.0f, (g_outlineCol & 0xFF) / 255.0f, 1.0f);
    const float o = (float)(g_outlineW > 0 ? g_outlineW : 1) * (g_guiScale > 0.1f ? g_guiScale : 1.0f);   /* 逻辑像素 → 跟随缩放 */   /* 描边粗细 */
    const int _r = (g_outlineW >= 1 && g_outlineW <= 3) ? g_outlineW : 2;
    const int _side = 2 * _r + 1;
    for (int k = 0; k < _side * _side; k++) {   /* 实心圆盘：半径内每个偏移都画 → 均匀实心描边 */
        static const float dx8[8] = { -1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 1.0f, 1.0f };
        int _ox = (k % _side) - _r, _oy = (k / _side) - _r;
        if (!_ox && !_oy) continue;
        float dx = (float)_ox;
        static const float dy8[8] = { 0.0f, 0.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };
        float dy = (float)_oy;
        ImGui::SetCursorScreenPos(ImVec2(p.x + dx, p.y + dy));
        ImGui::PushStyleColor(ImGuiCol_Text, oc);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::TextColored(col, "%s", text);
}
static int   g_animPhase = 0;
static float g_animT = 0.0f;

struct RowCache { char name[128]; char rateM[8]; char rateP[8]; unsigned capM, encM, capP, encP, color; };
static RowCache g_rc[ROW_MAX];
static int g_rcCount = 0;

/* v23：接管游戏窗口消息，把鼠标喂给 ImGui（thprac 同款做法）。
   游戏本身不使用鼠标，所以不会有冲突；用 CallWindowProc 原样转发，不影响游戏。 */
static WNDPROC g_oldWndProc = 0;
static HWND    g_hwnd = 0;
static LRESULT CALLBACK MyWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_oldWndProc) return DefWindowProcW(h, msg, wp, lp);   /* 断链保护 */
    if (g_guiInited && ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        short mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
        switch (msg) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            int vk = (int)wp;
#ifdef DEV
            /* ---- 调试热键：只在 DEV 构建里编译（Debug-dev / Release-dev）。
               正式 Release 构建里这些按键完全不存在，不会误触。
               想在所有构建里都有，把 DEV 换成 1 即可。 ---- */
            if (vk == VK_OEM_7) { SaveCheckFreeze(); UiLogSafe("[savecheck] hotkey ' -> frozen"); }   /* '键：冻结当前存档 ✓ */
            if (vk == VK_OEM_1) { SaveCheckRun(); SaveCheckMem(); UiLogSafe("[savecheck] hotkey ; -> checked"); }   /* ；键：随时查存档 ✓ */
            if (vk == VK_F7) { SnapNow(); UiLogSafe("[snap] F7 manual shot"); }
            if (vk == VK_F9)  { MusicLockUnlockRange(21, 28); UiLogSafe("[musiclock] F9 unlock 21..28 OK"); }
            if (vk == VK_F10) { MusicLockUnlockRange(1, 128); UiLogSafe("[musiclock] F10 unlock all OK"); }
            if (vk == VK_F8) { MusicLockApplyNow(); UiLogSafe("[musiclock] F8 manual rewrite OK"); }
#endif
            if (vk == 0x58 /*X*/ || vk == VK_ESCAPE) {
                g_forceHideFrames = 30;   /* 立刻收起（约 0.5 秒内保持隐藏） */
                UiLogSafe("[key] 退出键 VK=%#04X → 立即隐藏（帧=%u）", vk, g_guiFrames);
            } else if (vk == 0x5A || vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_RETURN) {
                UiLogSafe("[key] 记录 VK=%#04X（帧=%u）", vk, g_guiFrames);
            }
            break;
        }
        case WM_MOUSEMOVE:   g_mouseMsgs++; io.MousePos = ImVec2((float)mx * g_mouseScale, (float)my * g_mouseScale); break;
        case WM_LBUTTONDOWN: io.MouseDown[0] = true;  break;
        case WM_LBUTTONUP:   io.MouseDown[0] = false; break;
        case WM_RBUTTONDOWN: io.MouseDown[1] = true;  break;
        case WM_RBUTTONUP:   io.MouseDown[1] = false; break;
        case WM_MOUSEWHEEL: {
            g_wheelMsgs++; io.MouseWheel += (float)((short)HIWORD(wp)) / 120.0f;
            POINT pt; pt.x = mx; pt.y = my; ScreenToClient(h, &pt);
            io.MousePos = ImVec2((float)pt.x * g_mouseScale, (float)pt.y * g_mouseScale);
            break;
        }
        default: break;
        }
    }
    return CallWindowProcW(g_oldWndProc, h, msg, wp, lp);
}
static void HookWndProc(void) {
    if (g_oldWndProc) return;
    g_hwnd = 0;
    EnumWindows([](HWND h, LPARAM)->BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(h)) return TRUE;
        char cls[64] = { 0 }; GetClassNameA(h, cls, (int)sizeof(cls));
        if (lstrcmpA(cls, "ConsoleWindowClass") == 0) return TRUE;   /* 跳过控制台 */
        if (!GetWindow(h, GW_OWNER) && GetWindowTextLengthA(h) > 0) { g_hwnd = h; return FALSE; }
        return TRUE;
    }, 0);
    HWND h = g_hwnd;
    if (!h) return;
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)MyWndProc);
    if (old) {
        g_oldWndProc = old; g_hwnd = h;
        RECT rc = { 0, 0, 0, 0 }; GetClientRect(h, &rc);
        float cw = (float)(rc.right - rc.left), chh = (float)(rc.bottom - rc.top);
        ImGuiIO& io2 = ImGui::GetIO();
        if (cw > 1.0f && chh > 1.0f && io2.DisplaySize.y > 1.0f) g_mouseScale = io2.DisplaySize.y / chh;
        UiLogSafe("[gui] 已接管窗口消息（滚轮/拖动可用），鼠标缩放=%d%%", (int)(g_mouseScale * 100));
    }
}
static void ReadIni(void) {
    wchar_t ipath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, ipath, MAX_PATH);
    wchar_t* sl = (n && n < MAX_PATH) ? wcsrchr(ipath, L'\\') : 0;
    if (!sl) return;
    wcscpy_s(sl + 1, MAX_PATH - (sl + 1 - ipath), L"ECLplus.ini");
    g_bgAlpha    = SpellTableIniInt(L"GuiBgAlpha", 100);
    g_maxRows    = SpellTableIniInt(L"GuiMaxRows", 32);
    g_rowSpacing = SpellTableIniInt(L"GuiRowSpacing", 0);
    g_useCols    = SpellTableIniInt(L"GuiCols", 1);
    g_scroll     = SpellTableIniInt(L"GuiScroll", 0);
    g_wndHook    = SpellTableIniInt(L"GuiWndHook", 0);
    g_ownView    = SpellTableIniInt(L"GuiOwnView", 0);
    g_maskUnknown = SpellTableIniInt(L"GuiMaskUnknown", 1);
    g_outlineOn  = SpellTableIniInt(L"GuiOutline", 1);
    g_outlineW   = SpellTableIniInt(L"GuiOutlineWidth", 2);
    g_outlineCol = (unsigned)SpellTableIniInt(L"DrawColorOutline", (int)0xFF000000);
    if (g_outlineW < 1) g_outlineW = 1; if (g_outlineW > 6) g_outlineW = 6;
    if (g_bgAlpha < 0) g_bgAlpha = 0; if (g_bgAlpha > 100) g_bgAlpha = 100;
    if (g_maxRows < 1) g_maxRows = 1; if (g_maxRows > ROW_MAX) g_maxRows = ROW_MAX;
}

void ScoreGuiInit(void) {
    SpellTableGuiParams(&g_guiOn, &g_guiX, &g_guiY, &g_guiW, &g_guiH,
                        &g_guiFontSize, &g_guiText, g_guiFontFile, (int)sizeof(g_guiFontFile));
    ReadIni();
    if (g_guiOn) UiLogSafe("[gui] 开关=%d 窗口=%d,%d %dx%d 字号=%d 列=%d 最大行=%d 行距=%d 底色=%d 滚动=%d 钩子=%d 自控=%d",
                           g_guiOn, g_guiX, g_guiY, g_guiW, g_guiH, (int)g_guiFontSize,
                           g_useCols, g_maxRows, g_rowSpacing, g_bgAlpha, g_scroll, g_wndHook, g_ownView);
}

/* v54：把主表用的缩放系数暴露出去 —— 索引条(score_index.cpp)必须用【同一个】缩放，
   否则同样的坐标数字落点不一样（这就是之前"同样数据结果不同"的原因）。 */
float ScoreGuiScale(void) { return g_guiScale; }

static ImFont* LoadFont(ImGuiIO& io) {
    /* v33：图集只放「ASCII + 常用符号 + 本表实际用到的字」——内容小，D3D9 图集才能建成功
       （之前塞了 3500 个常用汉字 → 图集过大 → 构建/上传失败 → 汉字全丢 ✗） */
    static ImWchar g_glyphRanges[2048];
    {
        int n = 0;
        g_glyphRanges[n++] = 0x0020; g_glyphRanges[n++] = 0x007E;   /* ASCII（数字/字母/No. 等） */
        g_glyphRanges[n++] = 0x2010; g_glyphRanges[n++] = 0x203B;   /* 破折号等 */
        g_glyphRanges[n++] = 0x2600; g_glyphRanges[n++] = 0x26FF;   /* 符号（如 ☄） */
        g_glyphRanges[n++] = 0x3000; g_glyphRanges[n++] = 0x303F;   /* 中文标点 */
        g_glyphRanges[n++] = 0xFF00; g_glyphRanges[n++] = 0xFFEF;   /* 全角字符 */
        g_glyphRanges[n++] = 0x7B2C; g_glyphRanges[n++] = 0x7B2C;   /* 第 */
        g_glyphRanges[n++] = 0x9875; g_glyphRanges[n++] = 0x9875;   /* 页 */
        g_glyphRanges[n++] = 0x4E2D; g_glyphRanges[n++] = 0x4E2D;   /* 中 */
        g_glyphRanges[n++] = 0x5171; g_glyphRanges[n++] = 0x5171;   /* 共（索引条 score_index.cpp 用） */
        static unsigned short used[1024];
        int un = SpellTableCollectChars(used, 700);
        for (int i = 0; i < un && n + 3 < 2048; i++) {
            g_glyphRanges[n++] = (ImWchar)used[i];
            g_glyphRanges[n++] = (ImWchar)used[i];
        }
        g_glyphRanges[n] = 0;
    }
    io.Fonts->TexDesiredWidth = 2048;   /* 2048 对 D3D9 安全，且内容变小后足够装下 */
    const ImWchar* ranges = g_glyphRanges;
    if (g_guiFontFile[0]) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(g_guiFontFile, g_guiFontSize, NULL, ranges);
        if (f) { UiLogSafe("[gui] 字体：%s", g_guiFontFile); return f; }
    }
    wchar_t dir[MAX_PATH] = { 0 }, wpath[MAX_PATH];
    char ansi[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    const wchar_t* names[] = { L"\\Touhou_Simhei.ttf", L"\\..\\..\\bin\\Touhou_Simhei.ttf", L"\\simhei.ttf" };
    for (int i = 0; i < 3; i++) {
        wcscpy_s(wpath, MAX_PATH, dir);
        wcscat_s(wpath, MAX_PATH, names[i]);
        if (GetFileAttributesW(wpath) == INVALID_FILE_ATTRIBUTES) continue;
        WideCharToMultiByte(CP_ACP, 0, wpath, -1, ansi, MAX_PATH, NULL, NULL);
        ImFont* f = io.Fonts->AddFontFromFileTTF(ansi, g_guiFontSize, NULL, ranges);
        if (f) { UiLogSafe("[gui] 字体：%s", ansi); return f; }
    }
    const char* sys = "C:\\Windows\\Fonts\\simhei.ttf";
    if (GetFileAttributesA(sys) != INVALID_FILE_ATTRIBUTES) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(sys, g_guiFontSize, NULL, ranges);
        if (f) { UiLogSafe("[gui] 字体：%s", sys); return f; }
    }
    UiLogSafe("[gui] 找不到中文字体，用内置字体");
    io.Fonts->AddFontDefault();
    return io.Fonts->Fonts[0];
}

void ScoreGuiBeginScene(void* device) {
    if (!g_guiOn || g_guiFail || g_guiInited || !device) return;
    __try {
        if (!*(void**)PTR_MAIN_MENU_G) return;
        if (++g_guiWarm < WARMUP_FRAMES) return;
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = NULL;
        io.LogFilename = NULL;
        if (!THPrac::Gui::ImplDX9Init((IDirect3DDevice9*)device)) {
            g_guiFail = 1; UiLogSafe("[gui] ImplDX9Init 失败 → 关闭"); return;
        }
        THPrac::Gui::ImplDX9AdjustDispSize();
        g_guiScale = io.DisplaySize.y / 480.0f;
        if (g_guiScale < 0.5f || g_guiScale > 8.0f) g_guiScale = 1.0f;
        g_guiFontSize *= g_guiScale;
        UiLogSafe("[gui] 后备缓冲 %dx%d 缩放=%d%% 字号=%dpx",
                  (int)io.DisplaySize.x, (int)io.DisplaySize.y, (int)(g_guiScale * 100), (int)g_guiFontSize);
        LoadFont(io);
        {   /* v55：索引条自己的字体（基准字号，不预乘缩放），必须在 Build 之前加入图集 */
            void IndexBarLoadFont(void*);
            IndexBarLoadFont(&io);
        }
        io.Fonts->Build();
        THPrac::Gui::ImplDX9InvalidateDeviceObjects();
        if (g_wndHook) HookWndProc();   /* v23：把鼠标喂给 ImGui（默认关，需要滚轮/点击时再开） */
        g_guiInited = 1;
        UiLogSafe("[gui] ImGui 初始化完成（BeginScene 阶段）");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_guiFail = 1; UiLogSafe("[gui] 初始化异常 → 已关闭自绘界面");
    }
}

static void CollectRows(void) {
    g_rcCount = 0;
    /* 自愈：当前分组若没有数据，自动切到第一个有数据的分组（否则表是空的看不见） */
    if (g_ownView) {   /* 页码夹紧：不存在"空页"（越界就回到最后一页） */
        int _pr0 = SpellTablePageRows(); if (_pr0 < 1) _pr0 = 10;
        int _tot0 = SpellTableGroupRows(g_ourGroup);
        int _maxPage = (_tot0 > 0) ? (_tot0 - 1) / _pr0 : 0;
        if (g_ourPage > _maxPage) g_ourPage = _maxPage;
        if (g_ourPage < 0) g_ourPage = 0;
    }
    if (g_ownView && SpellTableGroupRows(g_ourGroup) <= 0) {
        for (int gi = 0; gi < 16; gi++) { if (SpellTableGroupRows(gi) > 0) { g_ourGroup = gi; g_ourPage = 0; break; } }
    }
    for (int i = 0; i < ROW_MAX; i++) {
        unsigned col = 0;
        int _ok;
        if (g_ownView) {
            int _pr = SpellTablePageRows(); if (_pr < 1) _pr = 10;
            _ok = SpellTableFormatRowAt(g_ourGroup, g_ourPage * _pr + i, g_rc[i].name, (int)sizeof(g_rc[i].name),
                     &g_rc[i].capM, &g_rc[i].encM, g_rc[i].rateM, (int)sizeof(g_rc[i].rateM),
                     &g_rc[i].capP, &g_rc[i].encP, g_rc[i].rateP, (int)sizeof(g_rc[i].rateP), &col);
        } else {
            _ok = SpellTableFormatRowCols(i, g_rc[i].name, (int)sizeof(g_rc[i].name),
                     &g_rc[i].capM, &g_rc[i].encM, g_rc[i].rateM, (int)sizeof(g_rc[i].rateM),
                     &g_rc[i].capP, &g_rc[i].encP, g_rc[i].rateP, (int)sizeof(g_rc[i].rateP), &col);
        }
        if (!_ok) break;
        g_rc[i].color = col;
        g_rcCount++;
    }
    g_rowCount = g_rcCount;
}

static void DrawTable(float t) {
    const float s = g_guiScale;
    const float fullW = g_guiW * s, fullH = g_guiH * s;
    const float cx = g_guiX * s + fullW * 0.5f;
    const float cy = g_guiY * s + fullH * 0.5f;
    float h = fullH * t;
    if (h < 2.0f) h = 2.0f;
    const float alpha = 1.0f;

    ImGui::SetNextWindowPos(ImVec2(cx - fullW * 0.5f, cy - h * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(fullW, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(alpha * ((float)g_bgAlpha / 100.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);   /* 去掉窗口边框（虚线框） */
    ImGuiWindowFlags _fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!g_scroll) _fl |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##spelltable", NULL, _fl);
    ImGui::SetWindowFontScale(1.0f);
    /* v52：原来这里内嵌着"分组按钮 + < > 翻页 + 第 %d 页"的索引代码，
       已摘出到 score_index.cpp（IndexBarDraw），本函数只管主表内容。 */
    if (g_ownView) {
        CollectRows();
    }


    int lim = g_rcCount < g_maxRows ? g_rcCount : g_maxRows;
    const float gap = 10.0f * s, edge = 8.0f * s, minX = 6.0f * s;
    const float winW = ImGui::GetWindowWidth();

    for (int i = 0; i < lim; i++) {
        if (!g_useCols) {
            char line[256]; unsigned col = g_rc[i].color;
            if (!SpellTableFormatRow(i, line, (int)sizeof(line), &col)) break;
            TextOutlined(ImVec4(((col >> 16) & 0xFF) / 255.0f, ((col >> 8) & 0xFF) / 255.0f, (col & 0xFF) / 255.0f, alpha), line);
            if (g_rowSpacing > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + g_rowSpacing * s);
            continue;
        }
        RowCache& rc = g_rc[i];
        ImVec4 c(((rc.color >> 16) & 0xFF) / 255.0f, ((rc.color >> 8) & 0xFF) / 255.0f, (rc.color & 0xFF) / 255.0f, alpha);
        char sM[24], sP[24];
        sprintf_s(sM, "M %u/%u", rc.capM, rc.encM);
        sprintf_s(sP, "P %u/%u", rc.capP, rc.encP);
            float wNm = ImGui::CalcTextSize(rc.name).x;
        float wM = ImGui::CalcTextSize(sM).x, wRM = ImGui::CalcTextSize(rc.rateM).x;
        float wP = ImGui::CalcTextSize(sP).x, wRP = ImGui::CalcTextSize(rc.rateP).x;
        float xL = minX;
            ImGui::SetCursorPosX(xL);  TextOutlined(c, rc.name);   /* 名字顶到最左（编号写进名字里） */
        float totalR = wM + wRM + wP + wRP + gap * 3.0f;
        float x = winW - edge - totalR;
        if (x < xL + wNm + gap) x = xL + wNm + gap;
        ImGui::SameLine(x);        TextOutlined(c, sM);       x += wM + gap;
        ImGui::SameLine(x);        TextOutlined(c, rc.rateM); x += wRM + gap;
        ImGui::SameLine(x);        TextOutlined(c, sP);       x += wP + gap;
        ImGui::SameLine(x);        TextOutlined(c, rc.rateP);
        if (g_rowSpacing > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + g_rowSpacing * s);
    }
    ImGui::End();
    if ((++g_guiFrames % 300) == 0) {
        UiLogSafe("[gui] 阶段=%d t=%d%% 行=%d 行VM=%d 窗口高=%d 可滚=%d 滚轮消息=%d 鼠标消息=%d", g_animPhase, (int)(g_animT * 100), g_rcCount, SpellTableVmCount(), (int)ImGui::GetWindowHeight(), (int)ImGui::GetScrollMaxY(), g_wheelMsgs, g_mouseMsgs);
    }
}

void ScoreGuiFrame(void* device) {
    SnapTick(device);   /* 随机截屏：必须在画 UI 之前 ✓ */
    if (!g_guiOn || g_guiFail || !device) return;
    __try {
        if (!g_guiInited) return;
        THPrac::Gui::ImplDX9Check((IDirect3DDevice9*)device);
        THPrac::Gui::ImplDX9AdjustDispSize();
        THPrac::Gui::ImplDX9NewFrame();
        ImGui::NewFrame();

        int vis = SpellTableShouldShow();
    if (g_forceHideFrames > 0) { g_forceHideFrames--; vis = 0; }   /* v28：按了退出键 → 立即收起，不等游戏 VM */
        const float step = 1.0f / (float)ANIM_FRAMES;
        if (vis && g_animPhase != 1) {
            CollectRows();
            if (g_animPhase == 0) g_animT = 0.0f;
            g_animPhase = 1;
        } else if (!vis && g_animPhase == 1) {
            g_animPhase = 2; g_animT = 1.0f;
        } else if (vis && g_animPhase == 1) {
            CollectRows();
        }
        if (g_animPhase == 1 && g_animT < 1.0f) { g_animT += step; if (g_animT > 1.0f) g_animT = 1.0f; }
        if (g_animPhase == 2) { g_animT -= 1.0f / (float)ANIM_FRAMES_OUT; if (g_animT <= 0.0f) { g_animT = 0.0f; g_animPhase = 0; } }

        /* v60：自建表为空（读不到 CSV / 该分组未接管）时不画窗口。
           原来这里是"空表也画，便于确认 UI 是否出现"（调试期遗留）：
           DrawTable 会 Begin 一个 GuiBgAlpha=100（全不透明）的空窗口，
           结果就是一个纯黑空框盖住游戏自己的符卡表。没有行就完全不画。 */
        const int haveRows = (g_rowCount > 0) ? 1 : 0;
        if (g_animPhase != 0 && !haveRows) {
            static int g_emptyLogged = 0;
            if (!g_emptyLogged) {
                g_emptyLogged = 1;
                UiLogSafe("[gui] 自建表为空（没读到 CSV 或该分组未接管）→ 不绘制窗口，显示游戏原表");
            }
        }
        if (g_animPhase != 0 && haveRows) DrawTable(g_animT);
        /* v52：索引条（从 DrawTable 里摘出来的独立单元）。
           把主表的显隐状态与动画进度一起传进去 —— 这样主表收起时索引条同步淡出，
           不会再出现"退出界面索引条还在"的问题。 */
        /* v60：索引条也跟着"有没有行"走，空表时不画（否则会留一个半透明黑块）。 */
        { void IndexBarDraw(int, float); IndexBarDraw((g_animPhase != 0 && haveRows) ? 1 : 0, g_animT); }
        ImGui::Render();
        THPrac::Gui::ImplDX9RenderDrawData(ImGui::GetDrawData());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_guiFail = 1;
        UiLogSafe("[gui] 绘制异常 → 已自动关闭自绘界面");
    }
}
