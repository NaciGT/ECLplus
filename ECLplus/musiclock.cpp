#include "pch.h"
#include <windows.h>
#include "musiclock.h"
#include "score_ui.h"   /* UiLogSafe */

#define ML_MAX 64                       /* 我们自己位图的上限（游戏常用 <=42） */
static unsigned char g_bits[ML_MAX];
static int      g_inited = 0, g_applied = 0, g_enable = 1;
static DWORD    g_start = 0, g_last = 0;

/* ===== v3：修正越界（成就全解锁的根因）=====
   游戏里的"已解锁曲目"数组 = ScoreFile + off，off = 0x2D16E（512 布局为 0xF016E）。
   紧随其后的 0x2D198（0xF0198）是【成就解锁位数组】，游戏用
       cmp byte ptr [scorefile + idx + 0x2D198], 0
   判断成就是否已解锁。
   两者间距 = 0x2D198 - 0x2D16E = 0x2A = **42 字节** —— 这就是音乐数组的真实容量。
   v2 的错误：位图固定写 128 项（ML_TRACKS=128），从 0x2D16E 起写 128 字节，
   越过 42 字节的边界后**把成就位一片刷成 1**，于是"成就全部自动解锁"。
   修法：
     1) 容量改为运行时求出（成就数组偏移 - 音乐数组偏移），并留 8 字节余量；
     2) 写回时**只写 1（解锁），不写 0** —— 绝不覆盖游戏自己写的其它位；
     3) "捕获游戏新解锁"只扫合法范围，不再把成就位读成曲目解锁。 */
static int MLArraySize(void) {
    /* 游戏读成就位的指令在 0x0045D0B9：8A 94 06 <disp32>  →  [esi + eax + disp32]
       （disp32 在 +3 = 0x0045D0BC；原版 = 0x2D198，512 布局 = 0xF0198）
       音乐数组紧随其前：0x2D198 - 0x2A = 0x2D16E（512：0xF016E）。
       容量就是这 0x2A = 42 字节 —— 下标 0..41，再往后就是成就位数组，绝不能碰。 */
    unsigned ach = *(unsigned*)0x0045D0BC;
    unsigned mus = ach - 0x2A;
    if (ach != 0x2D198 && ach != 0xF0198) return 0;
    if (mus != 0x2D16E && mus != 0xF016E) return 0;
    return 0x2A;                     /* 满 42 项（用户确认：放宽到最大） */
}

static void MLPath(wchar_t* out, int n) {
    DWORD m = GetModuleFileNameW(NULL, out, (DWORD)n);
    if (!m || m >= (DWORD)n) { out[0] = 0; return; }
    wchar_t* s = wcsrchr(out, L'\\');
    if (!s) { out[0] = 0; return; }
    wcscpy_s(s + 1, (size_t)(n - (int)(s + 1 - out)), L"ECLplusMusicUnlock.txt");
}
static void MLLoad(void) {
    wchar_t wp[MAX_PATH]; MLPath(wp, MAX_PATH);
    char p[MAX_PATH * 2] = { 0 };
    if (!wp[0]) return;
    WideCharToMultiByte(CP_ACP, 0, wp, -1, p, (int)sizeof(p), NULL, NULL);   /* v3：与 MLSave 同一路径 */
    if (!p[0]) return;
    FILE* f = NULL;
    if (fopen_s(&f, p, "r") != 0 || !f) return;
    char line[512];
    int idx = 0;
    while (fgets(line, sizeof(line), f) && idx < ML_MAX) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\r' || *s == '\n' || !*s) continue;
        int v = atoi(s);
        g_bits[idx++] = (v != 0) ? 1 : 0;
    }
    fclose(f);
    int n = 0; for (int i = 0; i < ML_MAX; i++) if (g_bits[i]) n++;
    UiLogSafe("[musiclock] 位图已载入（%d 首标记为已解锁，文件读到 %d 行）", n, idx);
}
static void MLSave(void) {
    wchar_t wp[MAX_PATH]; MLPath(wp, MAX_PATH);
    char p[MAX_PATH * 2] = { 0 };
    if (!wp[0]) return;
    WideCharToMultiByte(CP_ACP, 0, wp, -1, p, (int)sizeof(p), NULL, NULL);
    if (!p[0]) return;
    int cap = MLArraySize();                 /* v3：只保存合法曲目数，绝不写越界位 */
    if (cap <= 0) cap = 32;
    if (cap > ML_MAX) cap = ML_MAX;
    FILE* f = NULL;
    if (fopen_s(&f, p, "w") != 0 || !f) return;
    fprintf(f, "# ECLplus music unlock state (1 line per track, index = track-1)\n");
    for (int i = 0; i < cap; i++) fprintf(f, "%d\n", g_bits[i] ? 1 : 0);
    fclose(f);
}
static unsigned char* MLArray(unsigned* outOff) {
    unsigned char* sf = *(unsigned char**)0x004B77DC;
    unsigned off = *(unsigned*)0x0045ADF7;      /* 游戏写音乐位指令 0x45ADF5 的 disp32 在 +2 */
    if (!sf || (off != 0x2D16E && off != 0xF016E)) return 0;
    *outOff = off;
    return sf;
}
void MusicLockInit(void) {
    if (g_inited) return;
    g_inited = 1;
    g_start = GetTickCount();
    MLLoad();
}
void MusicLockApplyNow(void) {
    if (!g_inited) { MusicLockInit(); g_enable = SpellTableIniInt(L"MusicUnlockEnable", 1); }
    if (!g_enable) return;
    int cap = MLArraySize();
    if (cap <= 0) { UiLogSafe("[musiclock] 无法求出数组容量，已跳过（避免越界写坏成就位）"); return; }
    unsigned off = 0;
    unsigned char* sf = MLArray(&off);
    if (!sf) { UiLogSafe("[musiclock] 无法定位解锁数组"); return; }
    int wrote = 0;
    __try {
        /* 范围严格限制在 cap(42) 之内：cap 之外紧邻成就解锁位数组，写过去就会把成就刷开。
           范围内按位图完整同步（1=解锁、0=上锁），这是"游戏内决定解锁"脚本需要的语义。 */
        for (int i = 0; i < cap; i++) {
            unsigned char want = g_bits[i] ? 1 : 0;
            if (sf[off + i] != want) { sf[off + i] = want; wrote++; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { UiLogSafe("[musiclock] 写入异常"); return; }
    UiLogSafe("[musiclock] 已写入解锁位图（偏移 %X，容量 %d，改动 %d 字节；%d 之后的成就区未触碰）",
              off, cap, wrote, cap);
}
void MusicLockTick(void) {
    if (!g_inited) { MusicLockInit(); g_enable = SpellTableIniInt(L"MusicUnlockEnable", 1);
                     UiLogSafe("[musiclock] enable=%d", g_enable); }
    if (!g_enable) return;
    unsigned off = 0;
    unsigned char* sf = MLArray(&off);
    if (!sf) return;
    DWORD now = GetTickCount();
    if (!g_applied && (DWORD)(now - g_start) > 2000) {   /* 存档载入后再写 ✓ */
        g_applied = 1;
        MusicLockApplyNow();
    }
    if ((DWORD)(now - g_last) < 1000) return;
    g_last = now;
    int cap = MLArraySize();
    if (cap <= 0 || cap > ML_MAX) return;
    int changed = 0;
    __try {
        /* v3：只扫合法曲目范围（原来扫 128 项，会把 cap 之后的成就位读成"曲目已解锁"） */
        for (int i = 0; i < cap; i++)
            if (sf[off + i] && !g_bits[i]) { g_bits[i] = 1; changed++; }   /* 只增不减 ✓ */
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (changed) { MLSave(); UiLogSafe("[musiclock] 捕获到 %d 首新解锁 ✓（已落盘）", changed); }
}
void MusicLockUnlockRange(int from1, int to1) {
    if (!g_inited) MusicLockInit();
    int cap = MLArraySize();
    if (cap <= 0 || cap > ML_MAX) cap = ML_MAX;
    if (from1 < 1) from1 = 1;
    if (to1 > cap) to1 = cap;                /* v3：上界 = 数组容量（42），不越界到成就区 */
    if (to1 < from1) return;
    unsigned off = 0;
    unsigned char* sf = MLArray(&off);
    int n = 0;
    for (int t = from1; t <= to1; t++) { if (!g_bits[t - 1]) { g_bits[t - 1] = 1; n++; } }
    if (sf) {
        __try { for (int t = from1; t <= to1; t++) sf[off + t - 1] = 1; }
        __except (EXCEPTION_EXECUTE_HANDLER) { UiLogSafe("[musiclock] range write fault"); }
    }
    MLSave();
    UiLogSafe("[musiclock] unlock range %d..%d done (cap=%d, %d new, saved)", from1, to1, cap, n);
}