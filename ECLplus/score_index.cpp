#include "pch.h"
#include <windows.h>
#include "score_index.h"
#include "score_ui.h"          /* SpellTable* 查询接口 */
#include <imgui.h>

/* v54：主表用的缩放系数（score_gui.cpp 导出）。
   索引条必须用【同一个】缩放，否则同样的坐标数字落点不一样。 */
float ScoreGuiScale(void);
/* v55：索引条自己的字体。
   为什么需要单独一份：主表在【加载字体时】就把字号乘过缩放系数了
   （score_gui.cpp: g_guiFontSize *= g_guiScale），它的窗口缩放固定 1.0。
   索引条若复用那份字体、再调 SetWindowFontScale(s)，就会变成
   「已放大字号 × 再放大」= 双重缩放（实测字大到离谱）。
   所以这里单独加载一份【基准字号】的字体，窗口缩放保持 1.0 —— 与主表口径一致。 */
static ImFont* g_font = 0;
static char    g_fontFile[260] = { 0 };

/* ===== 符卡表「索引条」=====
   从 score_gui.cpp 的 DrawTable() 里摘出来的独立单元。

   位置：与 v9 自建表共用同一套坐标（ini 的 SpellTableX / SpellTableY0 / SpellTableDY），
         索引条占"首行上方一格"，即 (SpellTableX, SpellTableY0 - SpellTableDY)，
         这样和表格是无缝衔接的，也不会盖到别的东西。

   显隐：由调用方传入主表的 visible / 动画进度 t，
         主表收起时(t→0)索引条同步淡出，解决了"退出界面不消失"的问题。

   颜色：文字色 IndexBarTextColor、底色 IndexBarBgColor、底透明度 IndexBarBgAlpha(%)
         三个都可以在 ini 里单独调。 */

static int      g_on = 1;
static int      g_padX = 6, g_padY = 2;
static unsigned g_textCol = 0xFF6FD2FFu;   /* 0xAARRGGBB；默认浅蓝 */
static unsigned g_bgCol   = 0xC0000000u;   /* 0xAARRGGBB；默认半透明黑；RGB 有效，A 由 IndexBarBgAlpha 缩放 */
static int      g_bgAlpha = 75;            /* 底透明度 0..100（0=完全透明） */
static int      g_outline = 1;             /* 描边开关 */
static int      g_outlineW = 2;            /* v57：描边粗细（逻辑像素 1~3，与主表 GuiOutlineWidth 同口径） */
static unsigned g_outlineCol = 0xFF000000u;/* v57：描边颜色（0xAARRGGBB，与主表 DrawColorOutline 同口径） */
static int      g_rule    = 0;             /* 1=文字下方加一条横线 */
static float    g_fontSize = 15.0f;
static float    g_x = 0.0f, g_y = 0.0f;    /* 由 ini 坐标算出 */
static float    g_fontScaleSaved = 1.0f;

static ImVec4 ToVec4(unsigned abgr, float alphaMul) {
    return ImVec4((float)((abgr >> 16) & 0xFF) / 255.0f,
                  (float)((abgr >> 8) & 0xFF) / 255.0f,
                  (float)(abgr & 0xFF) / 255.0f,
                  (float)((abgr >> 24) & 0xFF) / 255.0f * alphaMul);
}

void IndexBarInit(void) {
    g_on       = SpellTableIniInt(L"IndexBar", 1);
    g_padX     = SpellTableIniInt(L"IndexBarPadX", 6);
    g_padY     = SpellTableIniInt(L"IndexBarPadY", 2);
    g_textCol  = (unsigned)SpellTableIniInt(L"IndexBarTextColor", (int)0xFF6FD2FFu);
    g_bgCol    = (unsigned)SpellTableIniInt(L"IndexBarBgColor", (int)0xC0000000u);
    g_bgAlpha  = SpellTableIniInt(L"IndexBarBgAlpha", 75);
    g_outline  = SpellTableIniInt(L"IndexBarOutline", 1);
    g_outlineW = SpellTableIniInt(L"IndexBarOutlineWidth", 2);
    g_outlineCol = (unsigned)SpellTableIniInt(L"IndexBarOutlineColor", (int)0xFF000000u);
    g_rule     = SpellTableIniInt(L"IndexBarRule", 0);
    g_fontSize = (float)SpellTableIniInt(L"IndexBarFontSize", 15);
    {   /* IndexBarFontFile：留空则按 msyh.ttc → simhei.ttf → simsun.ttc 顺序找 */
        wchar_t wf[260] = { 0 }, ini[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, ini, MAX_PATH)) {
            wchar_t* sl = wcsrchr(ini, L'\\');
            if (sl) {
                wcscpy_s(sl + 1, MAX_PATH - (sl + 1 - ini), L"ECLplus.ini");
                GetPrivateProfileStringW(L"ECLplus", L"IndexBarFontFile", L"", wf, 260, ini);
                if (wf[0]) WideCharToMultiByte(CP_ACP, 0, wf, -1, g_fontFile, (int)sizeof(g_fontFile), NULL, NULL);
            }
        }
    }
    if (g_padX < 0) g_padX = 0;
    if (g_padY < 0) g_padY = 0;
    if (g_bgAlpha < 0) g_bgAlpha = 0;
    if (g_bgAlpha > 100) g_bgAlpha = 100;
    if (g_outlineW < 1) g_outlineW = 1;
    if (g_outlineW > 3) g_outlineW = 3;
    if (g_fontSize < 8.0f) g_fontSize = 8.0f;
    if (g_fontSize > 60.0f) g_fontSize = 60.0f;

    /* 位置：用两个【独立坐标键】，和主表坐标互不影响（游戏 640x480 基准）。
       IndexBarX / IndexBarY 想放哪就填哪。
       默认 (320, 96)：SpellTableY0=150 那一格实际被游戏原生表头
       （截图实测「博丽灵梦(咏唱) / HARD」在 y≈150~170）占着，所以索引条要更靠上。 */
    g_x = (float)SpellTableIniInt(L"IndexBarX", 320);
    g_y = (float)SpellTableIniInt(L"IndexBarY", 96);
    if (g_x < 0.0f) g_x = 0.0f;
    if (g_y < 0.0f) g_y = 0.0f;
    UiLogSafe("[indexbar] 开关=%d 坐标=%d,%d 字号=%d 描边=%d(粗%d 色%08X) 横线=%d 字色=%08X 底色=%08X 底透明=%d%%",
              g_on, (int)g_x, (int)g_y, (int)g_fontSize, g_outline, g_outlineW, g_outlineCol,
              g_rule, g_textCol, g_bgCol, g_bgAlpha);
}

/* v55：加载索引条专用字体（基准字号，不预乘缩放）。
   字形集合只需：ASCII + 第/页/共。 */
void IndexBarLoadFont(void* ioPtr) {
    ImGuiIO& io = *(ImGuiIO*)ioPtr;
    static ImWchar ranges[32];
    int n = 0;
    ranges[n++] = 0x0020; ranges[n++] = 0x007E;   /* ASCII */
    ranges[n++] = 0x7B2C; ranges[n++] = 0x7B2C;   /* 第 */
    ranges[n++] = 0x9875; ranges[n++] = 0x9875;   /* 页 */
    ranges[n++] = 0x5171; ranges[n++] = 0x5171;   /* 共 */
    ranges[n] = 0;

    const char* cands[5];
    int cn = 0;
    if (g_fontFile[0]) cands[cn++] = g_fontFile;
    cands[cn++] = "C:\\Windows\\Fonts\\msyh.ttc";
    cands[cn++] = "C:\\Windows\\Fonts\\simhei.ttf";
    cands[cn++] = "C:\\Windows\\Fonts\\simsun.ttc";
    for (int i = 0; i < cn; i++) {
        if (GetFileAttributesA(cands[i]) == INVALID_FILE_ATTRIBUTES) continue;
        /* 与主表同口径：加载时就把字号乘上缩放系数（主表在 score_gui.cpp 里也是这么做的），
           窗口字体缩放固定 1.0 —— 这样 IndexBarFontSize 和 GuiFontSize 数值含义一致。 */
        float px = g_fontSize * ScoreGuiScale();
        if (px < 6.0f) px = 6.0f;
        ImFont* f = io.Fonts->AddFontFromFileTTF(cands[i], px, NULL, ranges);
        if (f) { g_font = f; UiLogSafe("[indexbar] 字体：%s 逻辑%.0f -> 实际%.0fpx（与主表同口径）", cands[i], g_fontSize, px); return; }
    }
    UiLogSafe("[indexbar] 找不到可用字体，用默认字体");
}

void IndexBarDraw(int visible, float t) {
    if (!g_on || !visible || t <= 0.0f) return;
    int page = SpellTableCurrentPage();
    if (page < 0) return;                       /* 不在符卡数据页 */
    int grp = SpellTableCurrentGroup();
    if (grp < 0) return;
    int rowsPerPage = SpellTablePageRows();
    if (rowsPerPage < 1) rowsPerPage = 10;
    int total = SpellTableGroupRows(grp);
    int pages = (total > 0) ? ((total + rowsPerPage - 1) / rowsPerPage) : 1;
    int from = page * rowsPerPage + 1;
    int to = from + rowsPerPage - 1;
    if (to > total) to = total;
    if (to < from) to = from;

    char label[96];
    _snprintf_s(label, sizeof(label), _TRUNCATE, "第 %d/%d 页   No.%d-%d / 共%d",
                page + 1, pages, from, to, total);

    /* 与主表同步淡出：t 从 1→0 时整体变透明 */
    float fade = t;
    if (fade > 1.0f) fade = 1.0f;
    float bgA = (float)g_bgAlpha / 100.0f;

    /* 位置/字号都要乘【主表同一个缩放】—— 之前这里没乘，所以同样的坐标数字落点不同 */
    const float s = ScoreGuiScale();
    if (s <= 0.0f) return;
    ImGui::SetNextWindowPos(ImVec2(g_x * s, g_y * s), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(bgA * fade);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((float)g_padX * s, (float)g_padY * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
    if (bgA > 0.001f) ImGui::PushStyleColor(ImGuiCol_WindowBg, ToVec4(g_bgCol, fade));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(g_textCol, 1.0f));

    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##spell_indexbar", NULL, fl)) {
        /* v55：不再用 SetWindowFontScale（会双重缩放）。
           改用本模块自己那份【基准字号】字体。 */
        if (g_font) ImGui::PushFont(g_font);
        ImVec4 tc = ToVec4(g_textCol, 1.0f);
        tc.w = 1.0f;
        if (g_outline) {
            /* v57：与主表 TextOutlined 同一套"实心圆盘"描边 ——
               半径 _r = IndexBarOutlineWidth（逻辑像素 1~3），
               在 (2r+1)² 个偏移里除中心外全部重画一遍，得到粗细均匀的描边。 */
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* fnt = ImGui::GetFont();
            const float fs = (fnt ? fnt->FontSize : 16.0f);
            const int _r = (g_outlineW >= 1 && g_outlineW <= 3) ? g_outlineW : 2;
            const int _side = 2 * _r + 1;
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImVec4 ocv = ToVec4(g_outlineCol, 1.0f);
            unsigned oc = ImGui::GetColorU32(ImVec4(ocv.x, ocv.y, ocv.z, ocv.w * fade));
            for (int k = 0; k < _side * _side; k++) {
                int _ox = (k % _side) - _r, _oy = (k / _side) - _r;
                if (!_ox && !_oy) continue;            /* 中心留给正文 */
                dl->AddText(fnt, fs, ImVec2(tp.x + (float)_ox, tp.y + (float)_oy), oc, label);
            }
        }
        ImGui::TextUnformatted(label);
        if (g_rule) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::CalcTextSize(label).x;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + w, p.y + 1.0f),
                IM_COL32(120, 160, 200, (int)(180.0f * fade)), 1.0f);
        }
    }
    if (g_font) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();                                  /* Text */
    if (bgA > 0.001f) ImGui::PopStyleColor();                /* WindowBg */
    ImGui::PopStyleVar(3);
}
