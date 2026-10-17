/*
 * score_ui.cpp —— 战绩页「符卡数据表」v9：自建表 + 自建渲染
 *
 * ======== 本次逆向确认的两件事（都有 exe 代码为证）========
 *
 * (A) 文字位置 API —— 不再依赖 ANM 脚本
 *   AsciiManager::create_string   0x408190
 *     __thiscall(ecx = [0x4B7678] ASCII_MANAGER_PTR)
 *     参数: (const zFloat3* pos, const char* text)   ret 8
 *     行为(逐条对照 0x408190..0x4082AB):
 *       idx = ++[ascii+0x19210]; str = ascii + 0xC10 + idx*0x138
 *       strcpy(str+0x00, text)                     ; text[0x100]
 *       str[0x100..0x10B] = *pos (3 个 float) ×[0x524718] 全局缩放
 *       str+0x10C = [ascii+0x19214]  color
 *       str+0x110 = [ascii+0x19218]  scale
 *       str+0x11C = [ascii+0x19220]
 *       str+0x120 = [ascii+0x1922C]  font_id
 *       str+0x124 = [ascii+0x19228]  draw_shadows
 *       str+0x128 = [ascii+0x19230]  render_group
 *       str+0x12C = [ascii+0x19234]  remaining_time(存活帧数)
 *       str+0x130 = [ascii+0x19238]  align_h
 *       str+0x134 = [ascii+0x1923C]  align_v
 *   条目上限 0x140；AsciiManager::on_tick(0x408050) 每帧减 remaining_time，
 *   减到 <0 就删掉并压缩数组。
 *   坐标是 640×480 虚拟坐标（证据：Gui::on_tick 里用 286/296/300/308/246/266 等，
 *   Spellcard::on_draw 里用 (360, 35, 0)）。zAsciiStr.__field_118(+0x118) 没人写，
 *   本模块拿它当"这是我加的条目"标记，实现自清理（每帧只画一份）。
 *
 * (B) 存档符卡条目字段（记录基址 = [0x4B77DC] + 槽位*记录大小；名字偏移 = 0x8D8 + 号*0x9C）
 *     +0x80 取得(主)  +0x84 取得(练习)  +0x88 遭遇(主)  +0x8C 遭遇(练习)
 *     +0x90 index     +0x94 难度        +0x98 分数
 *   证据:
 *     0x41AD6A-0x41AE03  遭遇时 strcpy 名字到记录 + 自增 [ecx+eax(+0x960/+0x964)]
 *                        （基址 [0x4B77DC] 无 +8 → 实际是 +0x88/+0x8C）
 *     0x41B235-0x41B274  取得时自增 [ecx*4+0x958]（→ +0x80/+0x84），之后发奖杯
 *     0x41AC2D-0x41AC5D  符卡横幅 >=100 显示 "MASTER" 否则 "%.2d/99+"，
 *                        读的正是 +0x80/+0x84（+0x88/+0x8C 那对是遭遇）
 *     0x44FED0-0x44FEEA  读档初始化把 index 写 [+0x960]（→+0x90）、难度写 [+0x964]（→+0x94）
 *     0x458A72/0x458A79  战绩页列表印 "No.%s%s%s %s %4d/%4d"，两个数字正是 +0x80/+0x88
 *
 * ======== 本模块做的事 ========
 *   v7（保留，可单独开关）：难度过滤表、上限跳板、读档 index/难度 初始化、行号/名字钩子
 *   v9（新）：SpellTableRender=1 时，直接接管 0x4587F1 的整段绘制，用自建表逐行输出
 *            "No.组内序号 名字  M 取得/遭遇 収率  P 取得/遭遇 収率"
 *            - 行号 = 组内序号（1 起）
 *            - 名字优先用 CSV 的显示名，空则用存档里的名字
 *            - 収率 = 取得/遭遇，主模式与练习模式分别显示
 *            - 行位置取游戏自己的行 VM 坐标（vm+0x2C），拿不到就用 ini 固定坐标
 *            - 存档里的"内部槽位号"只用于读数据，不显示（可写日志）
 *
 * CSV 格式（两种列序都支持，自动识别）：
 *     分组,内部槽位,显示名
 *     分组,显示名,内部槽位
 *   @PAGE_ROWS=n      每页行数（默认 10）
 *   @PAGES,分组=n     该分组的页数（只记录；页数上限由菜单逻辑决定，见说明文档）
 *   # 开头为注释
 *
 * 开关（ECLplus.ini [ECLplus]）：SpellTable / SpellTableRender / SpellTableInit /
 *   SpellTableFilter / SpellTableLimit / SpellTableNames / SpellTablePos / SpellTableX /
 *   SpellTableY0 / SpellTableDY / SpellTableXOff / SpellTableYOff / SpellTableAlignH /
 *   SpellTableAlignV / SpellTableFont / SpellTableShadow / SpellTableDuration
 */
#include "pch.h"
#include "score_ui.h"
#include "musiclock.h"
#include "savecheck.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#define SITE_FILTER_DISP   0x004588D7
#define SITE_FILTER_DISP2  0x00458833
#define ORIG_FILTER_TABLE  0x004A0CA0
#define SITE_LIMIT1        0x004588C9
#define SITE_LIMIT1_JT     0x004588EB
#define SITE_LIMIT2        0x004588E0
#define SITE_LIMIT2_JT     0x004588D4
#define SITE_ROWSET        0x004588F0      /* lea esi,[edx+0x1d3] （6 字节） */
#define SITE_ROWSET_BACK   0x004588F6
#define MENU_DIFF_OFF      0xFC
#define MENU_PAGE_OFF      0x1D4
#define MENU_SLOT_OFF      0x24
#define MENU_VMIDS_OFF     0x74C           /* int[10]，每行文字用的 VM id */
#define LOG_NAME           "ECLplusSpellTable.log"

/* 读档初始化（加载器把 index/难度 写进每条符卡条目） */
#define SITE_INIT_LIMIT      0x0044FEEA
#define SITE_INIT_LIMIT_JT   0x0044FED0
#define SITE_INIT_TABLE_DISP 0x0044FED3
#define ORIG_DIFF_TABLE      0x004A0CA0

/* 运行时立即数（512 补丁会改写它们，所以直接读现成的） */
#define IMM_RECSIZE        0x0044FBF9      /* 0x4820 / 0x18020 */
#define IMM_NAMEOFF        0x0041AD90      /* 0x8D8  / 0x4828  */

/* 全局指针 */
#define PTR_SCOREFILE      0x004B77DC      /* ScoreFile*（+8 = payload/记录区） */
#define PTR_ASCII_MGR      0x004B7678
#define PTR_ANM_MGR        0x00509A20
#define PTR_MAIN_MENU      0x004B77F0      /* zMainMenu*（菜单对象；+0x20 子页、+0x24 选择、+0x5DF0 模式快照） */

/* 菜单字段（本次逆向：0x457E52 用 [菜单+0x20] 查表跳转 0x4587E0，表项 0x457E65 把 [菜单+0x24] 存进 +0x5DF0，
   只有 +0x5DF0==1 时后面 4 处 `call 0x4587F0`（符卡列表绘制）才会执行） */
#define MENU_SUBPAGE_OFF   0x20
#define MENU_SEL_OFF       0x24
#define MENU_MODE_OFF      0x5DF0
#define MENU_SLOT2_OFF     0x5CF8
#define MENU_ROW2_OFF      0x5DF4
#define MENU_DIFF_OFF2     0x1DC

/* 游戏函数 */
#define FN_CREATE_STRING   0x00408190      /* __thiscall(ecx=ascii, pos, text) ret 8 */
#define FN_GET_VM          0x00476470      /* __thiscall(ecx=anm, id) → zAnmVm* ret 4 */
#define SITE_SPELL_DRAW    0x004587F1      /* 8 字节：mov ebp,esp / sub esp,0xF4 */
#define SPELL_DRAW_LEN     8

/* ANM VM 位置 */
#define ANM_VM_POS         0x2C            /* zFloat3 */

/* zAsciiManager 字段 */
#define AM_NUM_STRINGS     0x19210
#define AM_COLOR           0x19214
#define AM_SCALE           0x19218
#define AM_RENDER_GROUP    0x19230
#define AM_DRAW_SHADOWS    0x19228
#define AM_FONT_ID         0x1922C
#define AM_DURATION        0x19234
#define AM_ALIGN_H         0x19238
#define AM_ALIGN_V         0x1923C
#define ZSTR_BASE          0x0C10
#define ZSTR_SIZE          0x138
#define ZSTR_MAX           0x140
#define ZSTR_MARK_OFF      0x118           /* __field_118：create_string 不写 */
#define ZSTR_MARK_VAL      0x54483137u     /* 'TH17' */

/* 符卡条目字段（相对"名字偏移"） */
#define SF_NAME            0x00
#define SF_CAP_MAIN        0x80
#define SF_CAP_PRAC        0x84
#define SF_ENC_MAIN        0x88
#define SF_ENC_PRAC        0x8C
#define SF_INDEX           0x90
#define SF_DIFF            0x94
#define SF_SCORE           0x98
#define SF_SIZE            0x9C

/* v45: 収率显示的取数位置 —— 【记录 9 = 全局符卡历史表】。
   依据（th17符卡数据_全部信息.md §2「槽位 9 的特殊地位」）：
     "slot = 9 的记录不是角色×灵，而是游戏用来放「全局符卡历史/解锁表」的地方。
      Player Data 的符卡页读它：遭遇判断 0x458959（[entry+0x88] != 0）、名字拷贝 0x458967（0x291F8）。"
   实测（tools/check_slot9_theory.py，live 存档 scorethow.dat）：
     rec9 有 106 个槽位有数据，其它记录最多 7 个 —— rec9 就是全局历史表。
   ★ 口径分工（用户要求，也是游戏的实际行为）：
       名字是否打码 → 遍历【全部 10 条记录】的四个収率，任一非 0 即不打码
       収率显示     → 只读【记录 9】（全局历史表） */
#define SPELL_HISTORY_RECORD   9

#define GROUP_MAX   16
#define MAX_SLOTS   512
#define MAX_ROWS    2048
#define NAME_LEN    64
#define ROW_TEXT    192

static unsigned char g_filter[MAX_SLOTS];
static unsigned char g_emptyDiff[MAX_SLOTS];   /* v22：游戏读的"内部空表"（全 0xFF = 不属于任何难度） */
static int  g_rowCount = 0;
static int  g_maxSlot = -1;
static int  g_limit = MAX_SLOTS;
static int  g_pageRows = 10;
static int  g_pageStart[GROUP_MAX][32];   /* v34：每个分组的"分页断点"（@PAGE 标记的行号） */
static int  g_pageBreakCnt[GROUP_MAX];     /* 该分组的断点个数（= 额外页数） */
static int  g_pendingBreak = 0;
static int  g_optNames = 1;

/* 每个分组按「槽位升序」排好的显示名（v7 名字钩子用，保留） */
static char g_groupNames[GROUP_MAX][MAX_ROWS][NAME_LEN];
static int  g_groupNameCount[GROUP_MAX];

/* v41: slot of row N when a group is sorted by slot ascending (kept in sync with g_groupNames) */
static int  g_groupSlots[GROUP_MAX][MAX_ROWS];

/* 每个分组按「CSV 书写顺序」排好的行（v9 渲染用） */
struct GroupRow { int slot; char name[NAME_LEN]; };
static GroupRow* g_groupList[GROUP_MAX];
static int       g_groupListCount[GROUP_MAX];

/* v7 行号钩子写进来的当前状态 */
static volatile int g_curDiff = -1;
static volatile int g_curPage = 0;
static volatile int g_curRow = -1;

/* v9 */
static int   g_optRender = 0;
static int   g_optPos = 0;          /* 0=用行 VM 坐标 1=固定坐标 */
static float g_fx = 96.0f, g_fy0 = 152.0f, g_fdy = 16.0f;
static float g_fxoff = 0.0f, g_fyoff = 0.0f;
static int   g_alignH = 0, g_alignV = 1;
static int   g_fontId = 2, g_shadow = 1, g_duration = 1;
static volatile void* g_menuPtr = 0;
static volatile unsigned g_renderFrames = 0;
static int g_logRows = 1;           /* 头几帧把每行数据写日志 */

typedef void (__thiscall *tCreateString)(void* ascii, const float* pos, const char* text);
typedef void* (__thiscall *tGetVm)(void* anm, int id);

static char g_logPath[520] = {0};

/* 日志路径：优先放在 ECLPLUS.dll 旁边（和 ECLplus.ini / spell_table.csv 同一目录），
   这样不依赖进程的当前目录；拿不到就退回相对路径。 */
static const char* LogPath(void) {
    if (g_logPath[0]) return g_logPath;
    wchar_t wpath[MAX_PATH];
    HMODULE hm = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(const void*)&LogPath, &hm) && hm) {
        if (GetModuleFileNameW(hm, wpath, MAX_PATH)) {
            wchar_t* slash = wcsrchr(wpath, L'\\');
            if (slash) {
                slash[1] = 0;
                if (wcscat_s(wpath, MAX_PATH, L"ECLplusSpellTable.log") == 0) {
                    int n = WideCharToMultiByte(CP_ACP, 0, wpath, -1, g_logPath, (int)sizeof(g_logPath), NULL, NULL);
                    if (n > 0) return g_logPath;
                }
            }
        }
    }
    strcpy_s(g_logPath, sizeof(g_logPath), LOG_NAME);
    return g_logPath;
}

static int MiniFmtV(char* out, int cap, const char* fmt, va_list ap);
static void WatchAdd(unsigned site, int len, void* tramp, unsigned char origByte);   /* 看门狗登记（定义在后面） */

/* 日志句柄只开一次（在任何线程都能用）：thcrap 会挂钩 kernel32 的文件 API，
   在它没初始化好的线程里 CreateFileA 会静默失败 —— 实测监控线程就是这样哑掉的。
   所以这里在 init 时（主线程、thcrap 已就绪）开好句柄，之后只 WriteFile。 */
static volatile HANDLE g_logHandle = INVALID_HANDLE_VALUE;

static HANDLE LogHandle(void) {
    HANDLE h = (HANDLE)g_logHandle;
    if (h != INVALID_HANDLE_VALUE && h != NULL) return h;
    h = CreateFileA(LogPath(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (InterlockedCompareExchangePointer((PVOID volatile*)&g_logHandle, h, (PVOID)INVALID_HANDLE_VALUE) != (PVOID)INVALID_HANDLE_VALUE) {
            CloseHandle(h);            /* 别的线程先开了，用它的 */
            h = (HANDLE)g_logHandle;
        }
    }
    return h;
}

static void UiLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = MiniFmtV(buf, (int)sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) { n = (int)strlen(fmt); if (n > 1000) n = 1000; memcpy(buf, fmt, (size_t)n); }
    HANDLE h = LogHandle();
    if (h == INVALID_HANDLE_VALUE || h == NULL) return;
    DWORD w = 0;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
}

static int   g_maskUnknownUi = 1;      /* v30 ini: GuiMaskUnknown（未遭遇的名字打码） */
static int   g_optMaskAnyRecord = 1;   /* v42 ini: GuiMaskAnyRecord（1=任一自机遇到过就显示真名，默认开） */
static int   g_optMaskNeedCap = 0;     /* v58 ini: GuiMaskNeedCap（0=遭遇过就算解锁【默认】；1=必须取得≥1才算解锁） */
static int IniInt(const wchar_t* key, int def) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW((HMODULE)NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return def;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return def;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"ECLplus.ini");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return def;
    return GetPrivateProfileIntW(L"ECLplus", key, def, path);
}

/* ini 里的字符串设置（ANSI）。找不到 / 为空时保留默认值。
 * v59：让符卡表文件名可配置 —— 原来是硬编码 "spell_table.csv"，
 * 想用别的名字（例如 spell_table2.csv）就得手动改名。 */
static void IniStrA(const wchar_t* key, const char* def, char* out, int outCap) {
    if (!out || outCap <= 0) return;
    if (def) strcpy_s(out, (size_t)outCap, def); else out[0] = 0;

    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW((HMODULE)NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"ECLplus.ini");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;

    wchar_t wbuf[MAX_PATH] = { 0 };
    GetPrivateProfileStringW(L"ECLplus", key, L"", wbuf, MAX_PATH, path);
    if (wbuf[0] == 0) return;
    WideCharToMultiByte(CP_ACP, 0, wbuf, -1, out, outCap, NULL, NULL);
}

static char* ReadWholeFileA(const char* path, DWORD* outLen) {    if (outLen) *outLen = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > (1 << 20)) { CloseHandle(h); return NULL; }
    char* buf = (char*)malloc(size + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, size, &got, NULL);
    CloseHandle(h);
    if (!ok) { free(buf); return NULL; }
    buf[got] = 0;
    if (outLen) *outLen = got;
    return buf;
}

/* 分组名 → 编号：内置 E/N/H/L/EX，其余（如 EX2、A、B…）按出现顺序分配 >=5 的编号 */
static char g_groupTag[GROUP_MAX][16];
static int  g_groupUsed[GROUP_MAX];
static int  g_nextGroup = 5;

static int GroupId(const char* s, int createIfNew) {
    char t[16];
    size_t n = strlen(s);
    if (n == 0) return -1;
    if (n > 15) n = 15;
    for (size_t i = 0; i < n; i++) t[i] = (char)toupper((unsigned char)s[i]);
    t[n] = 0;
    if (!strcmp(t, "E")) return 0;
    if (!strcmp(t, "N")) return 1;
    if (!strcmp(t, "H")) return 2;
    if (!strcmp(t, "L")) return 3;
    if (!strcmp(t, "EX") || !strcmp(t, "X")) return 4;
    for (int i = 0; i < GROUP_MAX; i++) {
        if (g_groupUsed[i] && !strcmp(g_groupTag[i], t)) return i;
    }
    if (!createIfNew || g_nextGroup >= GROUP_MAX) return -1;
    int id = g_nextGroup++;
    g_groupUsed[id] = 1;
    strncpy_s(g_groupTag[id], sizeof(g_groupTag[id]), t, _TRUNCATE);
    UiLog("[score-ui] 新分组 '%s' → 编号 %d", t, id);
    return id;
}

/* 判定一个字段是不是"纯数字"（用于自动识别 CSV 列序） */
static int IsAllDigits(const char* s) {
    if (!s || !*s) return 0;
    while (*s) {
        if (*s == ' ' || *s == '\t') { s++; continue; }
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

struct RawRow { int group; int slot; int order; char name[NAME_LEN]; };
static RawRow g_raw[MAX_ROWS];
static int    g_rawCount = 0;

static void AddGroupRow(int grp, int slot, const char* name) {
    if (g_groupListCount[grp] >= MAX_ROWS) return;
    if (!g_groupList[grp]) {
        g_groupList[grp] = (GroupRow*)malloc(sizeof(GroupRow) * MAX_ROWS);
        if (!g_groupList[grp]) return;
    }
    GroupRow* r = &g_groupList[grp][g_groupListCount[grp]];
    r->slot = slot;
    memset(r->name, 0, NAME_LEN);
    if (name) strncpy_s(r->name, NAME_LEN, name, _TRUNCATE);
    if (g_pendingBreak) {   /* v34：这一行开始新的一页 */
        if (g_pageBreakCnt[grp] < 32) g_pageStart[grp][g_pageBreakCnt[grp]] = g_groupListCount[grp];
        g_pageBreakCnt[grp]++;
        g_pendingBreak = 0;
    }
    g_groupListCount[grp]++;
}

static void LoadTable(void) {
    memset(g_filter, 0xFF, sizeof(g_filter));
    for (int _g = 0; _g < GROUP_MAX; _g++) { g_pageBreakCnt[_g] = 0; }   /* v35：重新读表前清空分页断点 */
    memset(g_emptyDiff, 0xFF, sizeof(g_emptyDiff));
    DWORD len = 0;
    /* v59：表文件名可由 ini 的 SpellTableFile 指定，默认 spell_table.csv */
    char tableFile[MAX_PATH] = { 0 };
    IniStrA(L"SpellTableFile", "spell_table.csv", tableFile, MAX_PATH);
    char* buf = ReadWholeFileA(tableFile, &len);
    if (!buf) { UiLog("[score-ui] 读不到 %s", tableFile); return; }
    int bad = 0, swapped = 0;
    char* save = NULL;
    char* line = strtok_s(buf, "\r\n", &save);
    while (line) {
        char* p = line;
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3; /* UTF-8 BOM */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == 0) { line = strtok_s(NULL, "\r\n", &save); continue; }
        if (*p == '@') {                                  /* 全局设置行 */
            if (_strnicmp(p, "@PAGE", 5) == 0 && (p[5] == 0 || p[5] == ' ' || p[5] == '\t' || p[5] == ';')) {   /* v35：只认单独的 @PAGE（避免吞掉 @PAGE_ROWS=） */
                g_pendingBreak = 1;
                UiLog("[score-ui] @PAGE 分页标记（下一个条目开始新的一页）");
            } else if (_strnicmp(p, "@PAGE_ROWS=", 11) == 0) {
                int v = atoi(p + 11);
                if (v >= 1 && v <= 40) g_pageRows = v;
                UiLog("[score-ui] @PAGE_ROWS=%d", g_pageRows);
            } else {
                UiLog("[score-ui] 已记录设置行：%s", p);
            }
            line = strtok_s(NULL, "\r\n", &save);
            continue;
        }
        if (g_rawCount >= MAX_ROWS) break;
        char* c1 = strchr(p, ',');
        char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        if (!c1 || !c2) { bad++; line = strtok_s(NULL, "\r\n", &save); continue; }
        *c1 = 0;
        *c2 = 0;
        const char* f1 = p;            /* 分组 */
        const char* f2 = c1 + 1;       /* 第二列 */
        const char* f3 = c2 + 1;       /* 第三列 */
        int grp = GroupId(f1, 1);
        int slot; const char* nm;
        if (IsAllDigits(f2)) {         /* 分组,内部槽位,显示名 */
            slot = atoi(f2);
            nm = f3;
        } else if (IsAllDigits(f3)) {  /* 分组,显示名,内部槽位 */
            slot = atoi(f3);
            nm = f2;
            swapped++;
        } else {
            bad++;
            line = strtok_s(NULL, "\r\n", &save);
            continue;
        }
        char nameBuf[NAME_LEN];
        memset(nameBuf, 0, sizeof(nameBuf));
        strncpy_s(nameBuf, sizeof(nameBuf), nm, _TRUNCATE);
        {   /* 名字里允许用 ';' 或 " #" 起注释（有些表习惯这么写） */
            char* semi = strchr(nameBuf, ';');
            if (semi) *semi = 0;
            char* hash = strstr(nameBuf, " #");
            if (hash) *hash = 0;
        }
        size_t n = strlen(nameBuf);
        while (n && (nameBuf[n - 1] == ' ' || nameBuf[n - 1] == '\t')) nameBuf[--n] = 0;
        if (grp < 0 || slot < 0 || slot >= MAX_SLOTS) {
            bad++;
        } else {
            g_filter[slot] = (unsigned char)grp;
            g_raw[g_rawCount].group = grp;
            g_raw[g_rawCount].slot = slot;
            g_raw[g_rawCount].order = g_rawCount;
            memset(g_raw[g_rawCount].name, 0, NAME_LEN);
            memcpy(g_raw[g_rawCount].name, nameBuf, strlen(nameBuf) < NAME_LEN ? strlen(nameBuf) : NAME_LEN - 1);
            g_rawCount++;
            g_rowCount++;
            AddGroupRow(grp, slot, nameBuf);
            if (slot > g_maxSlot) g_maxSlot = slot;
        }
        line = strtok_s(NULL, "\r\n", &save);
    }
    free(buf);

    /* v7 用：每个分组按槽位升序的名字表（游戏按槽位从小到大遍历） */
    for (int g = 0; g < GROUP_MAX; g++) g_groupNameCount[g] = 0;
    for (int g = 0; g < GROUP_MAX; g++) {
        for (int i = 0; i < g_rawCount; i++) g_raw[i].order = i;
        for (int i = 0; i < g_rawCount; i++) {
            if (g_raw[i].group != g) continue;
            int best = i;
            for (int j = i + 1; j < g_rawCount; j++) {
                if (g_raw[j].group != g) continue;
                if (g_raw[j].slot < g_raw[best].slot) best = j;
            }
            if (best != i) { RawRow tmp = g_raw[i]; g_raw[i] = g_raw[best]; g_raw[best] = tmp; }
            int k = g_groupNameCount[g]++;
            if (k < MAX_ROWS) {
                memset(g_groupNames[g][k], 0, NAME_LEN);
                strncpy_s(g_groupNames[g][k], NAME_LEN, g_raw[i].name, _TRUNCATE);
                g_groupSlots[g][k] = g_raw[i].slot;    /* v41: slot kept in the same order as the name */
            }
        }
    }
    g_limit = (g_maxSlot + 1 > 0x65) ? (g_maxSlot + 1) : 0x65;
    if (g_limit > MAX_SLOTS) g_limit = MAX_SLOTS;
    UiLog("[score-ui] 表读取完成：%d 行，分组数 %d，最大槽位 %d，遍历上限 %d，每页 %d 行（列序颠倒的行 %d，坏行 %d）",
        g_rowCount, g_nextGroup, g_maxSlot, g_limit, g_pageRows, swapped, bad);
    for (int g = 0; g < g_nextGroup && g < GROUP_MAX; g++) {
        if (g_groupListCount[g])
            UiLog("[score-ui]   分组 %d：%d 条（组内序号即屏幕行号，槽位顺序=CSV 顺序）", g, g_groupListCount[g]);
    }
}

static BOOL PatchFilterAt(unsigned int site) {
    unsigned int* p = (unsigned int*)site;
    if (*p != ORIG_FILTER_TABLE) { UiLog("[score-ui] %#08X 读到 %#x（原版 %#x），跳过", site, *p, ORIG_FILTER_TABLE); return FALSE; }
    DWORD oldProt = 0;
    if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    unsigned int mine = (unsigned int)(DWORD_PTR)g_emptyDiff;   /* v22：同一张内部空表 */
    memcpy(p, &mine, 4);
    DWORD tmp = 0;
    VirtualProtect(p, 4, oldProt, &tmp);
    UiLog("[score-ui] 过滤表 %#08X → %#08X", site, mine);
    return TRUE;
}

static BOOL PatchLimit(unsigned int site, unsigned int jccTarget, int limit) {
    unsigned char* p = (unsigned char*)site;
    if (p[0] != 0x83 || p[1] != 0xFE || p[2] != 0x65) { UiLog("[score-ui] %#08X 不是 cmp esi,0x65", site); return FALSE; }
    unsigned char* tramp = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return FALSE;
    unsigned char* w = tramp;
    *w++ = 0x81; *w++ = 0xFE; memcpy(w, &limit, 4); w += 4;
    *w++ = 0x0F; *w++ = (p[3] == 0x7D) ? 0x8D : 0x8C;
    int rel = (int)(jccTarget - (unsigned int)(w + 4)); memcpy(w, &rel, 4); w += 4;
    *w++ = 0xE9;
    unsigned int back = site + 5;
    rel = (int)(back - (unsigned int)(w + 4)); memcpy(w, &rel, 4); w += 4;
    DWORD oldProt = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int jmpRel = (int)(tramp - (p + 5));
    p[0] = 0xE9; memcpy(p + 1, &jmpRel, 4);
    DWORD tmp = 0; VirtualProtect(p, 5, oldProt, &tmp);
    UiLog("[score-ui] 上限 %#08X → 跳板(cmp esi,%d)", site, limit);
    return TRUE;
}

/* ---------- v7：名字注入（最底层绘制前替换字符串） ---------- */

typedef int (__cdecl *DrawFn)(void* ascii, const char* str, int a3, int a4, int a5, int a6, const char* f7);
static DrawFn g_origDraw = (DrawFn)0x00475290;

static int __cdecl HookDraw(void* ascii, const char* str, int a3, int a4, int a5, int a6, const char* f7) {
    const char* use = str;
    int d = g_curDiff, row = g_curRow;
    if (g_optNames && d >= 0 && d < GROUP_MAX && row >= 0) {
        int idx = g_curPage * g_pageRows + row;
        if (idx >= 0 && idx < g_groupNameCount[d] && g_groupNames[d][idx][0])
            if (g_maskUnknownUi && SpellRowUnknownByIndex(d, idx))
                use = "No.？ ？？？";   /* mask: current player has all 4 counters zero */
            else
                use = g_groupNames[d][idx];   /* any counter >=1 -> show the real name */
    }
    return g_origDraw(ascii, use, a3, a4, a5, a6, f7);
}

static void __declspec(naked) HookRowSet(void) {
    __asm {
        push eax
        push edx
        mov eax, [ecx + 0x1D4]
        mov g_curPage, eax
        mov eax, [ecx + 0x0FC]
        mov g_curDiff, eax
        mov g_curRow, edx
        pop edx
        pop eax
        lea esi, [edx + 0x1d3]
        push 0x004588F6
        ret
    }
}

static BOOL PatchRowSet(void) {
    unsigned char* p = (unsigned char*)SITE_ROWSET;
    if (p[0] != 0x8D || p[1] != 0xB2 || p[2] != 0xD3 || p[3] != 0x01) {
        UiLog("[score-ui] %#08X 不是 lea esi,[edx+0x1d3]（读到 %02x %02x %02x %02x），跳过", SITE_ROWSET, p[0], p[1], p[2], p[3]);
        return FALSE;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int rel = (int)((unsigned char*)&HookRowSet - (p + 5));
    p[0] = 0xE9; memcpy(p + 1, &rel, 4); p[5] = 0x90;
    DWORD tmp = 0; VirtualProtect(p, 6, oldProt, &tmp);
    UiLog("[score-ui] 行号钩子 %#08X → %#08X", SITE_ROWSET, (unsigned)(DWORD_PTR)&HookRowSet);
    return TRUE;
}

static BOOL PatchDraw(void) {
    unsigned char* p = (unsigned char*)0x00458924;
    if (p[0] != 0xE8) { UiLog("[score-ui] %#08X 不是 call（读到 %02x），跳过", 0x00458924, p[0]); return FALSE; }
    int rel = *(int*)(p + 1);
    if ((unsigned int)(0x00458924 + 5 + rel) != 0x00475290) {
        UiLog("[score-ui] %#08X 已指向 %#08X（原版 %#08X），跳过", 0x00458924, 0x00458924 + 5 + rel, 0x00475290);
        return FALSE;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int newRel = (int)((unsigned char*)&HookDraw - (p + 5));
    memcpy(p + 1, &newRel, 4);
    DWORD tmp = 0; VirtualProtect(p, 5, oldProt, &tmp);
    WatchAdd(0x00458924, 5, (void*)&HookDraw, 0xE8);
    UiLog("[score-ui] 名字钩子 %#08X → %#08X（绕过 thcrap 字符串层）", 0x00458924, (unsigned)(DWORD_PTR)&HookDraw);
    return TRUE;
}

static unsigned char g_saveDiff[MAX_SLOTS];

static void BuildSaveDiffTable(void) {
    const unsigned char* orig = (const unsigned char*)ORIG_DIFF_TABLE;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (i < 101) g_saveDiff[i] = orig[i] & 0x07;
        else g_saveDiff[i] = 0;
    }
    int applied = 0;
    for (int i = 0; i < g_rawCount; i++) {
        int slot = g_raw[i].slot;
        if (slot < 0 || slot >= MAX_SLOTS) continue;
        int g = g_raw[i].group;
        g_saveDiff[slot] = (unsigned char)(g <= 4 ? g : 4);
        applied++;
    }
    UiLog("[score-ui] 读档难度表已生成：512 项（其中按表覆盖 %d 项，自定义分组按 EX 记）", applied);
}

static BOOL PatchInitLimit(int limit) {
    unsigned char* p = (unsigned char*)SITE_INIT_LIMIT;
    if (p[0] != 0x83 || p[1] != 0xF9 || p[2] != 0x65) {
        UiLog("[score-ui] %#08X 不是 cmp ecx,0x65（读到 %02x %02x %02x），跳过", SITE_INIT_LIMIT, p[0], p[1], p[2]);
        return FALSE;
    }
    unsigned char* tramp = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return FALSE;
    unsigned char* w = tramp;
    *w++ = 0x81; *w++ = 0xF9; memcpy(w, &limit, 4); w += 4;
    *w++ = 0x0F; *w++ = (p[3] == 0x7C) ? 0x8C : 0x8D;
    int rel = (int)(SITE_INIT_LIMIT_JT - (unsigned int)(w + 4)); memcpy(w, &rel, 4); w += 4;
    *w++ = 0xE9;
    unsigned int back = SITE_INIT_LIMIT + 5;
    rel = (int)(back - (unsigned int)(w + 4)); memcpy(w, &rel, 4); w += 4;

    DWORD oldProt = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int jmpRel = (int)(tramp - (p + 5));
    p[0] = 0xE9; memcpy(p + 1, &jmpRel, 4);
    DWORD tmp = 0; VirtualProtect(p, 5, oldProt, &tmp);
    UiLog("[score-ui] 读档初始化上限 %#08X → 跳板（cmp ecx,%d）", SITE_INIT_LIMIT, limit);
    return TRUE;
}

static BOOL PatchInitDiffTable(void) {
    unsigned int* p = (unsigned int*)SITE_INIT_TABLE_DISP;
    if (*p != ORIG_DIFF_TABLE) {
        UiLog("[score-ui] %#08X 处读到 %#x（原版 %#x），跳过", SITE_INIT_TABLE_DISP, *p, ORIG_DIFF_TABLE);
        return FALSE;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    unsigned int mine = (unsigned int)(DWORD_PTR)g_emptyDiff;   /* v22：游戏读内部空表（与外部 CSV 解耦） */
    memcpy(p, &mine, 4);
    DWORD tmp = 0; VirtualProtect(p, 4, oldProt, &tmp);
    UiLog("[score-ui] 读档难度表 %#08X → %#08X（512 项）", SITE_INIT_TABLE_DISP, mine);
    return TRUE;
}

/* ================= v9：自建渲染 ================= */

/* ---- 迷你格式化：渲染/日志路径完全不碰 CRT（避免 SSE 对齐/依赖问题） ----
   支持 %s %d %u %x %X %% ，以及标志 # 0 和十进制宽度。 */
static char* MfUnsigned(char* d, char* end, unsigned v, int base, int width, char pad, int alt) {
    char tmp[16];
    int n = 0;
    const char* digits = "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v && n < (int)sizeof(tmp)) { tmp[n++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
    if (alt) { if (base == 16) { if (d + 2 <= end) { *d++ = '0'; *d++ = 'x'; } width -= 2; } }
    { int padN = width - n; while (padN-- > 0 && d < end) *d++ = pad; }
    while (n > 0 && d < end) *d++ = tmp[--n];
    return d;
}
static char* MfSigned(char* d, char* end, int v, int width, char pad) {
    if (v < 0) { if (d < end) *d++ = '-'; width--; return MfUnsigned(d, end, (unsigned)(-(long)v), 10, width, pad, 0); }
    return MfUnsigned(d, end, (unsigned)v, 10, width, pad, 0);
}
static int MiniFmtV(char* out, int cap, const char* fmt, va_list ap) {
    if (cap <= 0) return 0;
    char* d = out; char* end = out + cap - 1;
    while (*fmt && d < end) {
        if (*fmt != '%') { *d++ = *fmt++; continue; }
        fmt++;
        int alt = 0, pad = ' ';
        while (*fmt == '#' || *fmt == '0' || *fmt == '-' || *fmt == ' ') {
            if (*fmt == '#') alt = 1;
            if (*fmt == '0') pad = '0';
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            while (len < width && d < end) { *d++ = ' '; width--; }
            while (*s && d < end) *d++ = *s++;
            fmt++;
        } else if (*fmt == 'd' || *fmt == 'i') {
            d = MfSigned(d, end, va_arg(ap, int), width, (char)pad); fmt++;
        } else if (*fmt == 'u') {
            d = MfUnsigned(d, end, va_arg(ap, unsigned), 10, width, (char)pad, 0); fmt++;
        } else if (*fmt == 'x' || *fmt == 'X') {
            d = MfUnsigned(d, end, va_arg(ap, unsigned), 16, width, (char)pad, alt); fmt++;
        } else if (*fmt == '%') {
            *d++ = '%'; fmt++;
        } else {
            if (d < end) *d++ = '%';
            if (*fmt && d < end) *d++ = *fmt++;
        }
    }
    *d = 0;
    return (int)(d - out);
}

static int MiniFmt(char* out, int cap, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = MiniFmtV(out, cap, fmt, ap);
    va_end(ap);
    return n;
}

static void* AsciiMgr(void) { return *(void**)PTR_ASCII_MGR; }
static void* AnmMgr(void)   { return *(void**)PTR_ANM_MGR; }
static unsigned char* ScoreFile(void) { return *(unsigned char**)PTR_SCOREFILE; }
static unsigned RecSize(void) { return *(unsigned int*)IMM_RECSIZE; }
static unsigned NameOff(void) { return *(unsigned int*)IMM_NAMEOFF; }

/* ===== v45r7：条目基准 = NameOff() - 8 = 0x4820 =====
   证据链（tools/solve_base_from_log.py）：把日志实测的 cnt 当约束反解基准，
   唯一同时满足【index==槽位号】和【计数对应游戏 History】的基准是 0x4820：
       BASE=0x4820  slot93 -> (取主0, 取练1, 遇主1, 遇练4, index93)  = History 01/04 ✔
       BASE=0x4818  slot93 -> (0,0,0,1, index1)                      = 观测到的 cnt，但 index 错
       BASE=0x4828  slot93 -> (1,4,93,0xFFFFFFFF, index0)            = 把 index 当遭遇（v41 的老 bug）
   即：名字字段在条目 +0x08（前 8 字节是上一条的尾巴），所以
       exe 的 IMM_NAMEOFF = 0x4820 + 0x08 = 0x4828（名字地址），
       而读整条条目要用基准 0x4820 = NameOff() - 8。
   历史轨迹：v41/v42 用 0x4828、v43/r4/r5/r6 用 0x4820/0x4818 之间反复，
   r7 定为 0x4820（ADJUST=8）。 */
#define NAME_OFF_ADJUST   8
static unsigned FileNameOff(void) {
    unsigned o = NameOff();
    return (o > NAME_OFF_ADJUST) ? (o - NAME_OFF_ADJUST) : o;
}
/* 条目指针：sf + 记录号*记录大小 + 文件布局的名字基准 + 槽位*0x9C */
static unsigned char* SpellEntry(unsigned char* sf, unsigned rec, unsigned recSize, int slot) {
    return sf + (size_t)rec * recSize + FileNameOff() + (size_t)slot * SF_SIZE;
}

/* v45r11：収率要读【当前自机】的那条记录，而不是全局历史表(记录9)。
   依据（th17符卡数据_全部信息.md）：
     - "显示只读 slot9，而运行时计数写在「当前 slot」(char*3+spirit)"（§未确认项3）
     - 多处 `imul eax, dword ptr [menu + 0x24], 0x4820` → menu+0x24 就是当前记录索引
       （0..8 = 角色×灵；记录9 = 全局符卡历史/解锁表）
   所以：名字解锁用记录9（全局，游戏 Player Data 的判定也用它），
        収率则按【当前自机】那条记录显示 —— 这样不同自机的収率就不会被合成一份。 */
static int CurrentSpellRecord(void) {
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    int rec = 0;
    if (menu) __try { rec = *(int*)(menu + MENU_SLOT_OFF); } __except (EXCEPTION_EXECUTE_HANDLER) { rec = 0; }
    if (rec < 0 || rec > 9) rec = 0;
    return rec;
}

/* ===== v45r8：运行时自动定位符卡条目（不再手工调偏移常数）=====
   起因：v41..v45r7 反复在 0x4818 / 0x4820 / 0x4828 之间试错，因为
   "保存文件里的条目起点"与 exe 里 IMM_NAMEOFF(=0x4828) 的关系有多种可能。
   本函数改用参考资料给的硬不变量【index == 槽位号】直接搜索：
     在 FileNameOff() ± 0x40 范围内找 p 使 [p + 0x90] == slot，
   找不到（旧档/未初始化）就退回 FileNameOff()。
   这样无论记录头/结构前导有 8 还是 16 字节，都能读到正确字段。 */
static unsigned char* FindSpellEntry(unsigned char* sf, unsigned rec, unsigned recSize, int slot) {
    unsigned base = FileNameOff();
    if (!sf || !recSize || slot < 0) return SpellEntry(sf, rec, recSize, slot);
    unsigned char* rb = sf + (size_t)rec * recSize;
    unsigned char* best = 0;
    int bestDist = 0x7FFFFFFF;
    for (int d = -0x40; d <= 0x40; d += 4) {
        int off = (int)base + d;
        if (off < 0) continue;
        unsigned char* p = rb + (size_t)off + (size_t)slot * SF_SIZE;
        unsigned idx = 0;
        __try { idx = *(unsigned*)(p + SF_INDEX); } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if ((int)idx == slot) {
            int dist = (d < 0) ? -d : d;
            /* 取离 FileNameOff() 最近的命中，避免 index==0 之类在别处也成立时选偏 */
            if (dist < bestDist) { bestDist = dist; best = p; }
        }
    }
    if (best) return best;
    return SpellEntry(sf, rec, recSize, slot);
}

/* 崩溃报告：把异常码/地址/EIP 和模块基准写进日志，只报一次，之后照旧让系统处理 */
static volatile LONG g_inCrash = 0;
static volatile int  g_crashLog = 1;
static unsigned      g_exeBase = 0x00400000;
static unsigned      g_dllBase = 0;

static void CrashReport(const char* tag, unsigned code, unsigned addr, unsigned eip) {
    if (InterlockedExchange(&g_inCrash, 1) != 0) return;
    const char* mod = "?";
    unsigned off = eip;
    if (eip >= g_exeBase && eip < g_exeBase + 0x200000) { mod = "th17.exe"; off = eip - g_exeBase; }
    else if (g_dllBase && eip >= g_dllBase) { mod = "ECLPLUS.dll"; off = eip - g_dllBase; }
    UiLog("[crash] %s code=%#08X 地址=%#08X EIP=%#08X (%s+%#08X)",
          tag, code, addr, eip, mod, off);
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (g_crashLog && ep && ep->ExceptionRecord) {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        unsigned eip = ep->ContextRecord ? (unsigned)ep->ContextRecord->Eip : 0;
        CrashReport("未处理异常", (unsigned)er->ExceptionCode, (unsigned)(DWORD_PTR)er->ExceptionAddress, eip);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI CrashVeh(EXCEPTION_POINTERS* ep) {
    if (g_crashLog && ep && ep->ExceptionRecord) {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        /* 只关心访问违例/非法指令/栈溢出，其它（如 C++ 异常、断点）不记录 */
        if (er->ExceptionCode == 0xC0000005u || er->ExceptionCode == 0xC000001Du || er->ExceptionCode == 0xC00000FDu) {
            unsigned eip = ep->ContextRecord ? (unsigned)ep->ContextRecord->Eip : 0;
            CrashReport("访问违例", (unsigned)er->ExceptionCode, (unsigned)(DWORD_PTR)er->ExceptionAddress, eip);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* 清掉上一帧我们自己加进去的字符串（用 +0x118 标记识别，不碰游戏自己的） */
static void ClearMyStrings(void* ascii) {
    if (!ascii) return;
    int* pn = (int*)((unsigned char*)ascii + AM_NUM_STRINGS);
    int n = *pn;
    if (n <= 0) return;
    if (n > ZSTR_MAX) n = ZSTR_MAX;
    unsigned char* arr = (unsigned char*)ascii + ZSTR_BASE;
    int keep = 0;
    for (int i = 0; i < n; i++) {
        unsigned char* s = arr + (size_t)i * ZSTR_SIZE;
        if (*(unsigned int*)(s + ZSTR_MARK_OFF) == ZSTR_MARK_VAL) continue;
        if (keep != i) memmove(arr + (size_t)keep * ZSTR_SIZE, s, ZSTR_SIZE);
        keep++;
    }
    *pn = keep;
}

static void AddMyString(void* ascii, const float* pos, const char* text) {
    if (!ascii || !pos || !text) return;
    int before = *(int*)((unsigned char*)ascii + AM_NUM_STRINGS);
    if (before < 0 || before >= ZSTR_MAX) return;
    ((tCreateString)FN_CREATE_STRING)(ascii, pos, text);
    int after = *(int*)((unsigned char*)ascii + AM_NUM_STRINGS);
    if (after == before + 1) {
        unsigned char* s = (unsigned char*)ascii + ZSTR_BASE + (size_t)(after - 1) * ZSTR_SIZE;
        *(unsigned int*)(s + ZSTR_MARK_OFF) = ZSTR_MARK_VAL;
    }
}

/* 把存档里的名字（Shift-JIS，最多 10 字节）拷进 buf；ptr 已保证可读 */
static void ReadSaveName(const unsigned char* p, char* buf, int cap) {
    int i = 0;
    while (i < 10 && i < cap - 1 && p[i]) { buf[i] = (char)p[i]; i++; }
    buf[i] = 0;
}

static int FmtRate(char* out, int cap, unsigned cap1, unsigned enc1) {
    if (enc1 == 0) return MiniFmt(out, cap, "  --");
    unsigned pct = (unsigned)((cap1 * 200u + enc1) / (2u * enc1));   /* 四舍五入 */
    return MiniFmt(out, cap, "%3u%%", pct);
}

/* 找本页第 row 行的坐标：优先用游戏自己的行 VM（vm+0x2C） */
static int VmRowPos(void* menu, int row, float* out) {
    if (!menu) return 0;
    int vid = *(int*)((unsigned char*)menu + MENU_VMIDS_OFF + (size_t)row * 4);
    if (!vid) return 0;
    void* anm = AnmMgr();
    if (!anm) return 0;
    void* vm = ((tGetVm)FN_GET_VM)(anm, vid);
    if (!vm) return 0;
    float* p = (float*)((unsigned char*)vm + ANM_VM_POS);
    out[0] = p[0] + g_fxoff;
    out[1] = p[1] + g_fyoff;
    out[2] = p[2];
    return 1;
}

/* 真正的绘制流程；外面由 RenderSpellTable 套 SEH 兜住任何异常。
   返回 1 = 我们自己画了（跳过游戏原绘制）；0 = 交回游戏自己画。 */
static int RenderSpellTableInner(void) {
    void* menu = (void*)g_menuPtr;
    if (!menu) return 0;
    void* ascii = AsciiMgr();
    if (!ascii) return 0;
    unsigned char* am = (unsigned char*)ascii;
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize();
    unsigned nameOff = NameOff();

    unsigned renderNo = g_renderFrames;
    int doLog = (renderNo < 3 && g_logRows);
    if (doLog) UiLog("[T1] 进入渲染：菜单=%#08X 帧=%u 存档=%#08X 记录=%#X 名字偏移=%#X",
                     (unsigned)menu, renderNo, (unsigned)sf, recSize, nameOff);
    if (!sf || !recSize || !nameOff) return 0;

    int group = *(int*)((unsigned char*)menu + MENU_DIFF_OFF);
    int page = *(int*)((unsigned char*)menu + MENU_PAGE_OFF);
    int slot = *(int*)((unsigned char*)menu + MENU_SLOT_OFF);
    if (group < 0 || group >= GROUP_MAX) return 0;
    if (page < 0) page = 0;
    if (slot < 0 || slot > 15) slot = -1;
    int total = g_groupListCount[group];
    GroupRow* list = g_groupList[group];
    /* 这一组没有写过任何行、或者翻到了表里没有的页 → 交回游戏自己画（安全） */
    if (total <= 0 || page * g_pageRows >= total) return 0;
    if (doLog) UiLog("[T2] 组=%d 页=%d 槽位=%d 每页=%d 组内共 %d 行", group, page, slot, g_pageRows, total);

    /* 行几何：先用行 0 定位，再按相邻行 VM 求行距 */
    float anchor[3];
    anchor[0] = g_fx + g_fxoff;
    anchor[1] = g_fy0 + g_fyoff;
    anchor[2] = 0.0f;
    float pitch = g_fdy;
    int haveAnchor = 0;
    if (!g_optPos) {
        float a[3], b[3];
        a[0] = a[1] = a[2] = 0.0f;
        b[0] = b[1] = b[2] = 0.0f;
        int aRow = -1, bRow = -1;
        for (int r = 0; r < g_pageRows && r < 10; r++) {
            if (VmRowPos(menu, r, a)) { aRow = r; anchor[0] = a[0]; anchor[1] = a[1]; anchor[2] = a[2]; haveAnchor = 1; break; }
        }
        if (haveAnchor) {
            for (int r = aRow + 1; r < g_pageRows && r < 10; r++) {
                if (VmRowPos(menu, r, b)) { bRow = r; break; }
            }
            if (bRow > aRow) {
                float d = (b[1] - anchor[1]) / (float)(bRow - aRow);
                if (d > 1.0f && d < 200.0f) pitch = d;
            }
        }
    }
    if (doLog) UiLog("[T3] 锚点=%d,%d 行距=%d 用VM坐标=%d", (int)anchor[0], (int)anchor[1], (int)pitch, haveAnchor ? 1 : 0);

    ClearMyStrings(ascii);
    if (doLog) UiLog("[T4] 已清掉自己上一帧的文字，当前条目数=%d", *(int*)(am + AM_NUM_STRINGS));


    int drawn = 0;
    for (int r = 0; r < g_pageRows; r++) {
        int idx = page * g_pageRows + r;
        if (idx < 0 || idx >= total) break;
        int id = list[idx].slot;
        float pos[3];
        pos[0] = anchor[0];
        pos[1] = anchor[1] + pitch * (float)r;
        pos[2] = anchor[2];
        if (!g_optPos) { float p2[3]; if (VmRowPos(menu, r, p2)) { pos[0] = p2[0]; pos[1] = p2[1]; pos[2] = p2[2]; } }

        char name[NAME_LEN];
        name[0] = 0;
        if (list[idx].name[0]) {
            int k = 0;
            while (k < NAME_LEN - 1 && list[idx].name[k]) { name[k] = list[idx].name[k]; k++; }
            name[k] = 0;
        }

        unsigned capM = 0, capP = 0, encM = 0, encP = 0, index = 0, diff = 0, score = 0;
        int readOk = 0;
        /* v45：収率固定读【记录 9 = 全局符卡历史表】（游戏 Player Data 符卡页读的就是它）。
           名字打码另走 SpellRowUnknownEx（它会遍历全部 10 条记录）—— 两者分工，互不干扰。 */
        {
            unsigned char* base = sf + (size_t)SPELL_HISTORY_RECORD * recSize;
            unsigned char* e = FindSpellEntry(sf, CurrentSpellRecord(), recSize, id);   /* v45r9: 参数是记录号！ */
            __try {
                if (!name[0]) ReadSaveName(e + SF_NAME, name, sizeof(name));
                capM = *(unsigned*)(e + SF_CAP_MAIN);
                capP = *(unsigned*)(e + SF_CAP_PRAC);
                encM = *(unsigned*)(e + SF_ENC_MAIN);
                encP = *(unsigned*)(e + SF_ENC_PRAC);
                index = *(unsigned*)(e + SF_INDEX);
                diff = *(unsigned*)(e + SF_DIFF);
                score = *(unsigned*)(e + SF_SCORE);
                readOk = 1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                readOk = 0;
            }
        }

        char rm[8], rp[8];
        FmtRate(rm, sizeof(rm), capM, encM);
        FmtRate(rp, sizeof(rp), capP, encP);

        /* v44：v9 渲染路径自己打码（原来这里完全没有打码，导致原版 101 坑位显示真名）。
           这样就不需要 v7 的名字钩子（SpellTableNames 可保持 0，避免 0x4588F0/0x458924
           那两处未验证的代码补丁）。判据与 ImGui 表一致：SpellRowUnknownEx。 */
        int masked = 0;
        if (g_maskUnknownUi && SpellRowUnknownEx(group, idx, capM, capP, encM, encP)) {
            lstrcpynA(name, "No.？ ？？？", (int)sizeof(name));
            lstrcpynA(rm, "--", (int)sizeof(rm));
            lstrcpynA(rp, "--", (int)sizeof(rp));
            capM = capP = encM = encP = 0;
            masked = 1;
        }

        unsigned color = (encM || encP) ? ((capM || capP) ? 0x00FFFF80u : 0x00EFEFEFu) : 0x00808080u;
        *(unsigned*)(am + AM_COLOR) = color;

        char text[ROW_TEXT];
        MiniFmt(text, (int)sizeof(text), "No.%02d %s  M %3u/%3u %s  P %3u/%3u %s",
                idx + 1, name[0] ? name : "----------",
                capM, encM, rm, capP, encP, rp);
        AddMyString(ascii, pos, text);
        drawn++;

        if (doLog) {
            UiLog("[row] 页=%d 行=%d 组内序号=%d 槽位(id)=%d 名字='%s' 主=%u/%u(%s) 练=%u/%u(%s) index=%u 难度=%u 分数=%u 读出=%d 打码=%d 坐标=%d,%d",
                  page, r, idx + 1, id, name, capM, encM, rm, capP, encP, rp, index, diff, score, readOk, masked,
                  (int)pos[0], (int)pos[1]);
        }
    }

    if (doLog) {
        UiLog("[T5] 本页画了 %d 行（组=%d 页=%d 槽位=%d）", drawn, group, page, slot);
    }
    return 1;
}

/* 外层：保存/恢复 AsciiManager 的"当前状态"，并用 SEH 兜住内部任何异常
   （万一出问题只是这一帧不画我们的表，绝不把游戏带崩）。 */
static int RenderSpellTable(void) {
    void* ascii = AsciiMgr();
    if (!ascii) return 0;
    unsigned char* am = (unsigned char*)ascii;
    unsigned renderNo = g_renderFrames;
    int oldFont = *(int*)(am + AM_FONT_ID), oldShadow = *(int*)(am + AM_DRAW_SHADOWS);
    int oldDur = *(int*)(am + AM_DURATION), oldAH = *(int*)(am + AM_ALIGN_H), oldAV = *(int*)(am + AM_ALIGN_V);
    unsigned oldColor = *(unsigned*)(am + AM_COLOR);
    float oldScale0 = *(float*)(am + AM_SCALE), oldScale1 = *(float*)(am + AM_SCALE + 4);
    int oldGroup = *(int*)(am + AM_RENDER_GROUP);
    if (g_fontId >= 0) *(int*)(am + AM_FONT_ID) = g_fontId;
    *(float*)(am + AM_SCALE) = 1.0f;            /* 缩放可能是 0 → 字看不见 */
    *(float*)(am + AM_SCALE + 4) = 1.0f;
    *(int*)(am + AM_RENDER_GROUP) = 0;          /* 组 0 一定会被画 */
    *(int*)(am + AM_DRAW_SHADOWS) = g_shadow;
    *(int*)(am + AM_DURATION) = g_duration;
    *(int*)(am + AM_ALIGN_H) = g_alignH;
    *(int*)(am + AM_ALIGN_V) = g_alignV;

    int r = 0;
    __try {
        r = RenderSpellTableInner();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = -1;
    }
    if (r < 0) {
        UiLog("[crash-ui] 自建渲染里出了异常，已拦住 → 本帧交回游戏绘制（连续出现请把 SpellTableRender 设 0）");
        r = 0;
    }

    if (g_fontId >= 0) *(int*)(am + AM_FONT_ID) = oldFont;
    *(float*)(am + AM_SCALE) = oldScale0;
    *(float*)(am + AM_SCALE + 4) = oldScale1;
    *(int*)(am + AM_RENDER_GROUP) = oldGroup;
    *(int*)(am + AM_DRAW_SHADOWS) = oldShadow;
    *(int*)(am + AM_DURATION) = oldDur;
    *(int*)(am + AM_ALIGN_H) = oldAH;
    *(int*)(am + AM_ALIGN_V) = oldAV;
    *(unsigned*)(am + AM_COLOR) = oldColor;
    g_renderFrames = renderNo + 1;
    return r;
}

/* 0x4587F0 才是真函数入口：55（push ebp）；紧接着 0x4587F1 是
   "mov ebp,esp / sub esp,0xF4"（共 8 字节），我们把这 8 字节换成 jmp。
   注意：进入我们钩子时 push ebp 已经执行过，所以
     · 自己画完直接返回 → 必须先 pop ebp 把栈弹回去（否则调用者 ret 到垃圾地址 → 崩溃）
     · 交回游戏画 → 补回 mov ebp,esp / sub esp,0xF4 后跳到 0x4587F9（栈与寄存器状态与原来一致） */
static int (*volatile g_renderFn)(void) = RenderSpellTable;
static volatile int g_renderResult = 0;
static volatile unsigned g_savedEsp = 0;
static volatile unsigned g_fallbackTarget = 0x004587F9;   /* 原函数 prologue 之后的续跑点（测试可替换） */

static void __declspec(naked) HookSpellListDraw(void) {
    __asm {
        mov dword ptr [g_menuPtr], ecx
        pushad
        mov dword ptr [g_savedEsp], esp
        and esp, 0FFFFFFF0h              /* 保证 16 字节对齐：游戏栈不保证，SSE 代码可能因此崩溃 */
        call dword ptr [g_renderFn]
        mov dword ptr [g_renderResult], eax
        mov esp, dword ptr [g_savedEsp]
        popad
        cmp dword ptr [g_renderResult], 0
        je fallback
        pop ebp                          /* 弹掉 0x4587F0 push 进来的 ebp */
        xor eax, eax
        ret
    fallback:
        mov ebp, esp                     /* = 原函数 0x4587F1 的第一条指令 */
        sub esp, 0F4h                    /* = 原函数 0x4587F3 的第二条指令 */
        push dword ptr [g_fallbackTarget]
        ret
    }
}

static BOOL PatchSpellListDraw(void) {
    unsigned char* p = (unsigned char*)SITE_SPELL_DRAW;
    unsigned char* fn = (unsigned char*)(SITE_SPELL_DRAW - 1);
    if (fn[0] != 0x55) {
        UiLog("[score-ui] %#08X 不是 push ebp（读到 %02x）→ 函数入口假设不成立，拒绝接管绘制",
              (unsigned)SITE_SPELL_DRAW - 1, fn[0]);
        return FALSE;
    }
    if (p[0] != 0x8B || p[1] != 0xEC || p[2] != 0x81 || p[3] != 0xEC) {
        UiLog("[score-ui] %#08X 不是 mov ebp,esp / sub esp,0xF4（读到 %02x %02x %02x %02x），跳过",
              SITE_SPELL_DRAW, p[0], p[1], p[2], p[3]);
        return FALSE;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(p, SPELL_DRAW_LEN, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int rel = (int)((unsigned char*)&HookSpellListDraw - (p + 5));
    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
    p[5] = 0x90; p[6] = 0x90; p[7] = 0x90;
    DWORD tmp = 0;
    VirtualProtect(p, SPELL_DRAW_LEN, oldProt, &tmp);
    UiLog("[score-ui] 符卡列表绘制接管 %#08X → %#08X（原 8 字节已覆盖）", SITE_SPELL_DRAW, (unsigned)(DWORD_PTR)&HookSpellListDraw);
    WatchAdd(SITE_SPELL_DRAW, SPELL_DRAW_LEN, (void*)&HookSpellListDraw, 0x8B);
    return TRUE;
}

/* ================= 诊断探针 =================
   1) 页面处理函数入口 0x457E40（战绩页/Player Data 的那个 handler，被 MainMenu::on_tick 0x4515C0 一带调用）
      —— 每次运行记一行菜单字段，用来确认"你选的到底是哪一项、字段是什么值"
   2) 0x457E5E 的 4 路跳转 `jmp [eax*4+0x4587E0]` —— 记录这次跑的是哪一个分支
   3) 符卡列表绘制的 4 个 call 点 —— 记录它到底有没有被调用

   所有探针都是"记日志 + 原样续跑"，不改游戏行为；每个点最多记 8 次，避免刷爆日志。 */
static volatile int g_probeId = -1;
static volatile unsigned g_probeEsp = 0;
static volatile unsigned g_probeMenu = 0;
static volatile int g_probeHits[16] = { 0 };

static const unsigned g_probeSites[6] = {
    0x00457E41,   /* page handler */
    0x00457E5E,   /* 4-way jump */
    0x00457FC9, 0x0045824A, 0x0045831D, 0x00458349,   /* spell list draw calls */
};
static const char* const g_probeNames[8] = {
    "页面handler", "4路跳转", "call绘制#1", "call绘制#2", "call绘制#3", "call绘制#4",
    "备用", "主菜单tick"
};

/* ---- 看门狗：把打过补丁的地方记下来，主菜单 tick 里定期检查，被改掉就重装 ----
   （thcrap 的 mod 会在 DLL 之后继续 patch 游戏代码：实测 512 槽补丁没事，
     但符卡列表那一带的钩子全部"装了却不触发"，所以需要这个自愈机制） */
struct WatchEntry { unsigned site; int len; unsigned char* tramp; unsigned char origByte; };
static WatchEntry g_watch[8];
static int g_watchCount = 0;
static volatile int g_optWatchdog = 1;
static volatile int g_watchFixes = 0;

static void WatchAdd(unsigned site, int len, void* tramp, unsigned char origByte) {
    if (g_watchCount >= 8) return;
    g_watch[g_watchCount].site = site;
    g_watch[g_watchCount].len = len;
    g_watch[g_watchCount].tramp = (unsigned char*)tramp;
    g_watch[g_watchCount].origByte = origByte;
    g_watchCount++;
}

static void WriteJmp(unsigned site, int len, const void* target) {
    unsigned char* p = (unsigned char*)site;
    DWORD oldProt = 0;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProt)) return;
    int rel = (int)((const unsigned char*)target - (p + 5));
    p[0] = 0xE9; memcpy(p + 1, &rel, 4);
    for (int i = 5; i < len; i++) p[i] = 0x90;
    DWORD tmp = 0;
    VirtualProtect(p, len, oldProt, &tmp);
}

static int WatchdogFix(void) {
    int fixed = 0;
    for (int i = 0; i < g_watchCount; i++) {
        unsigned char* p = (unsigned char*)g_watch[i].site;
        if (p[0] == 0xE9) continue;
        unsigned char was = p[0];
        WriteJmp(g_watch[i].site, g_watch[i].len, g_watch[i].tramp);
        fixed++;
        UiLog("[watch] %#08X 的钩子被改掉了（首字节 %02x）→ 已重装", g_watch[i].site, was);
    }
    if (fixed) g_watchFixes += fixed;
    return fixed;
}

static int WatchReport(char* buf, int cap) {
    int n = 0;
    if (g_watchCount == 0) return MiniFmt(buf, cap, "（未装代码钩子）");
    for (int i = 0; i < g_watchCount && n < cap - 16; i++) {
        unsigned char b = *(unsigned char*)g_watch[i].site;
        n += MiniFmt(buf + n, cap - n, "%s%#X=%s", i ? " " : "", g_watch[i].site, b == 0xE9 ? "在" : "掉了");
    }
    return n;
}

void SpellTableNoteListDraw(void);
static volatile int g_listDrawSeen = 0;   /* 见过"游戏在画符卡列表"就置位 */
/* v14：把第 0 行 VM 从 +0x2C 起的 12 个浮点打出来（×100），用于定位退出缩小的字段 */
void SpellTableVmDump(char* buf, int cap) {
    if (!buf || cap < 8) return;
    buf[0] = 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    if (!menu) return;
    __try {
        int vid = *(int*)(menu + MENU_VMIDS_OFF);
        if (vid <= 0 || vid > 0x4000) return;
        void* anm = AnmMgr();
        if (!anm) return;
        unsigned char* vm = (unsigned char*)((tGetVm)FN_GET_VM)(anm, vid);
        if (!vm) return;
        int n = 0;
        for (int i = 0; i < 12 && n < cap - 16; i++) {
            float f = *(float*)(vm + 0x2C + i * 4);
            n += MiniFmt(buf + n, cap - n, "%s%d", i ? "," : "", (int)(f * 100.0f));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

/* v17：把菜单对象上两段整数打出来（+0x1D0 起 16 个 / +0x5CE0 起 8 个），用于定位退出瞬间的字段 */
void SpellTableMenuDump(char* buf, int cap) {
    if (!buf || cap < 16) return;
    buf[0] = 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    if (!menu) return;
    __try {
        int n = 0;
        n += MiniFmt(buf + n, cap - n, "A:");
        for (int i = 0; i < 16 && n < cap - 16; i++) n += MiniFmt(buf + n, cap - n, "%s%d", i ? "," : "", *(int*)(menu + 0x1D0 + i * 4));
        n += MiniFmt(buf + n, cap - n, " B:");
        for (int i = 0; i < 8 && n < cap - 16; i++) n += MiniFmt(buf + n, cap - n, "%s%d", i ? "," : "", *(int*)(menu + 0x5CE0 + i * 4));
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static void ProbeLogC(void);   /* 前置声明（asm 里 call 它） */
static void __declspec(naked) ProbeTramp(void) {
    __asm {
        pushad
        pushfd
        mov dword ptr [g_probeEsp], esp
        and esp, 0FFFFFFF0h
        call ProbeLogC
        mov esp, dword ptr [g_probeEsp]
        popfd
        popad
        ret
    }
}

/* 建一个"记日志 + 原样续跑"的跳板：把 site 处 len 字节原样搬到跳板里执行，然后跳到 cont */
static BOOL PatchLogHook(int id, unsigned site, int len, unsigned cont, const unsigned char* orig) {
    unsigned char* p = (unsigned char*)site;
    for (int i = 0; i < len; i++) {
        if (p[i] != orig[i]) {
            UiLog("[probe] %s %#08X 字节不符（第%d字节 读到 %02x，期望 %02x），跳过",
                  g_probeNames[id], site, i, p[i], orig[i]);
            return FALSE;
        }
    }
    unsigned char* tramp = (unsigned char*)VirtualAlloc(NULL, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return FALSE;
    unsigned char* w = tramp;
    /* mov dword [g_probeId], id */
    *w++ = 0xC7; *w++ = 0x05; unsigned a = (unsigned)(DWORD_PTR)&g_probeId; memcpy(w, &a, 4); w += 4;
    memcpy(w, &id, 4); w += 4;
    /* mov dword [g_probeMenu], ecx */
    *w++ = 0x89; *w++ = 0x0D; a = (unsigned)(DWORD_PTR)&g_probeMenu; memcpy(w, &a, 4); w += 4;
    /* call ProbeTramp */
    *w++ = 0xE8; int r2 = (int)((unsigned char*)&ProbeTramp - (w + 4)); memcpy(w, &r2, 4); w += 4;
    /* 原样搬过来的指令 */
    memcpy(w, orig, (size_t)len); w += len;
    /* jmp cont */
    *w++ = 0xE9; r2 = (int)(cont - (unsigned)(w + 4)); memcpy(w, &r2, 4); w += 4;
    DWORD oldProt = 0;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int newRel = (int)(tramp - (p + 5));
    p[0] = 0xE9; memcpy(p + 1, &newRel, 4);
    for (int i = 5; i < len; i++) p[i] = 0x90;
    DWORD tmp = 0; VirtualProtect(p, len, oldProt, &tmp);
    WatchAdd(site, len, tramp, orig[0]);
    return TRUE;
}

/* 4 个 call 点：call 的目标是 0x4587F0，必须按新位置重算相对偏移（不能原样搬） */
static BOOL PatchProbeSite(int id) {
    unsigned site = g_probeSites[id];
    unsigned char* p = (unsigned char*)site;
    if (p[0] != 0xE8) { UiLog("[probe] %s %#08X 不是 call（读到 %02x），跳过", g_probeNames[id], site, p[0]); return FALSE; }
    int rel = *(int*)(p + 1);
    unsigned target = (unsigned)(site + 5 + rel);
    if (target != SITE_SPELL_DRAW - 1) {
        UiLog("[probe] %s %#08X 目标不是 %#08X（读到 %#08X），跳过", g_probeNames[id], site, SITE_SPELL_DRAW - 1, target);
        return FALSE;
    }
    unsigned char* tramp = (unsigned char*)VirtualAlloc(NULL, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return FALSE;
    unsigned char* w = tramp;
    *w++ = 0xC7; *w++ = 0x05; unsigned a = (unsigned)(DWORD_PTR)&g_probeId; memcpy(w, &a, 4); w += 4;
    memcpy(w, &id, 4); w += 4;
    *w++ = 0x89; *w++ = 0x0D; a = (unsigned)(DWORD_PTR)&g_probeMenu; memcpy(w, &a, 4); w += 4;
    *w++ = 0xE8; int r2 = (int)((unsigned char*)&ProbeTramp - (w + 4)); memcpy(w, &r2, 4); w += 4;
    *w++ = 0xE8; r2 = (int)(target - (unsigned)(w + 4)); memcpy(w, &r2, 4); w += 4;   /* 原调用 */
    *w++ = 0xE9; r2 = (int)((site + 5) - (unsigned)(w + 4)); memcpy(w, &r2, 4); w += 4;
    DWORD oldProt = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return FALSE;
    int newRel = (int)(tramp - (p + 5));
    p[0] = 0xE9; memcpy(p + 1, &newRel, 4);
    DWORD tmp = 0; VirtualProtect(p, 5, oldProt, &tmp);
    WatchAdd(site, 5, tramp, 0xE8);
    return TRUE;
}

static BOOL InstallProbes(void) {
    static const unsigned char origPage[8] = { 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x1C };
    static const unsigned char origJump[7] = { 0xFF, 0x24, 0x85, 0xE0, 0x87, 0x45, 0x00 };
    int ok = 0;
    ok += PatchLogHook(0, 0x00457E41, 8, 0x00457E49, origPage) ? 1 : 0;   /* 页面 handler 入口 */
    ok += PatchLogHook(1, 0x00457E5E, 7, 0x00000000, origJump) ? 1 : 0;   /* 4 路跳转（搬完直接跳走） */
    for (int i = 0; i < 4; i++) ok += PatchProbeSite(2 + i) ? 1 : 0;
    UiLog("[probe] 探针安装 %d/6（页面handler / 4路跳转 / 4个call点）", ok);
    return ok > 0;
}

/* ---- 看门狗的实现见上面（ProbeLogC 之前）---- */

/* 主菜单 tick（0x4515D0 的 push ebp 之后）——它一定每帧都在跑，适合当看门狗 */
static BOOL InstallTickWatchdog(void) {
    /* 注意：0x4515D0 是 push ebp，我们从 0x4515D1 开始替换；
       5 字节会切断后面的 mov esi,[0x4B769C]，所以搬 11 字节（到 push edi 为止），
       续跑点 0x4515DC（mov edi,ecx）。 */
    static const unsigned char orig[11] = { 0x8B, 0xEC, 0x51, 0x56, 0x8B, 0x35, 0x9C, 0x76, 0x4B, 0x00, 0x57 };
    unsigned char* tramp = (unsigned char*)VirtualAlloc(NULL, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return FALSE;
    unsigned char* w = tramp;
    int id = 7;
    *w++ = 0xC7; *w++ = 0x05; unsigned a = (unsigned)(DWORD_PTR)&g_probeId; memcpy(w, &a, 4); w += 4;
    memcpy(w, &id, 4); w += 4;
    *w++ = 0x89; *w++ = 0x0D; a = (unsigned)(DWORD_PTR)&g_probeMenu; memcpy(w, &a, 4); w += 4;
    *w++ = 0xE8; int r2 = (int)((unsigned char*)&ProbeTramp - (w + 4)); memcpy(w, &r2, 4); w += 4;
    memcpy(w, orig, 11); w += 11;
    *w++ = 0xE9; r2 = (int)(0x004515DC - (unsigned)(w + 4)); memcpy(w, &r2, 4); w += 4;
    unsigned char* p = (unsigned char*)0x004515D1;
    for (int i = 0; i < 11; i++) {
        if (p[i] != orig[i]) { UiLog("[watch] 0x4515D1 字节不符（第%d字节 %02x），看门狗未安装", i, p[i]); return FALSE; }
    }
    WriteJmp(0x004515D1, 11, tramp);
    WatchAdd(0x004515D1, 11, tramp, 0x8B);
    UiLog("[watch] 看门狗已挂在主菜单 tick 0x4515D1 上");
    return TRUE;
}

/* ---- 监控线程：完全不依赖任何游戏钩子 ----
   线程在游戏进程里跑，负责：
   1) 每 100ms 检查一次我所有补丁点的首字节，一旦被改掉就记下"什么时候、被改成了什么"
   2) 到 SpellTableReinstallMs 指定的时刻（默认 3000ms）把钩子**再装一遍**——
      这就是对付"thcrap 在我的 init 之后又改写这块代码"的办法（晚装，而不是硬刚）
   3) 定期记一行菜单状态（子页/选择/模式/难度/页） */
static int g_optCodeHooks = 0;    /* 是否允许修改游戏代码 */
static int g_optLateHooks = 1;   /* 1 = 不在 DllMain 装，等 thcrap 打完补丁后从 D3D9 帧钩子里装 */
static int g_lateHookFrames = 150;
static int g_hooksInstalled = 0;
static unsigned g_frameCount = 0;
static volatile int g_optFrameLog = 1;
static int g_optMonitor = 1;
static int g_optReinstallMs = 3000;
static int g_optRepair = 1;

struct MonitorState { int reportedLost; int reinstalled; };
static MonitorState g_mon[8];

static DWORD WINAPI MonitorThread(LPVOID) {
    unsigned iter = 0;
    unsigned lastMenuLog = 0;
    for (;;) {
        /* 前 3 秒 100ms 一次，之后 500ms 一次 */
        Sleep(iter < 30 ? 100 : 500);
        /* v10：字体改到 CreateDevice 钩子里创建（不能在其他线程碰 D3D 设备） */
        iter++;

        /* 1) 丢了就记（每个点只记第一次，并打印新字节） */
        for (int i = 0; i < g_watchCount && i < 8; i++) {
            unsigned char* p = (unsigned char*)g_watch[i].site;
            if (*p == 0xE9) continue;
            if (!g_mon[i].reportedLost) {
                g_mon[i].reportedLost = 1;
                unsigned char b[8];
                for (int k = 0; k < 8; k++) b[k] = p[k];
                UiLog("[mon] 第%u次检查发现 %#08X 的钩子没了：%02x %02x %02x %02x %02x %02x %02x %02x（%ums 左右）",
                      iter, g_watch[i].site, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], iter * 100);
            }
        }

        /* 2) 到点重装一遍（只装一次） */
        if (g_optCodeHooks && g_optReinstallMs > 0 && !g_mon[0].reinstalled && (int)(iter * 100) >= g_optReinstallMs) {
            g_mon[0].reinstalled = 1;
            /* 第一次安装（延迟）也要在这里做：DllMain 里装会被 thcrap 覆盖 */
            if (!g_hooksInstalled && g_optLateHooks) {
                /* 不再接管 0x4587F0 的绘制：跳过它会让每行 VM 的文字没被初始化 → ANM 字体绘制崩溃。
                   改成"游戏照常画 + 我们在帧钩子里另外加字"。 */
                if (IniInt(L"SpellTableProbe", 1)) InstallProbes();
                g_hooksInstalled = 1;
                UiLog("[late] 监控线程：%ums 后安装代码钩子（thcrap 应已打完补丁）", g_optReinstallMs);
            }
            int fixed = 0, already = 0;
            for (int i = 0; i < g_watchCount && i < 8; i++) {
                unsigned char* p = (unsigned char*)g_watch[i].site;
                if (*p == 0xE9) { already++; continue; }
                if (*p != g_watch[i].origByte) {
                    UiLog("[mon] %#08X 现在是 %02x（不是原版 %02x，可能是 thcrap 自己写的代码）→ 不碰它",
                          g_watch[i].site, *p, g_watch[i].origByte);
                    continue;
                }
                WriteJmp(g_watch[i].site, g_watch[i].len, g_watch[i].tramp);
                fixed++;
            }
            UiLog("[mon] 延迟重装（%ums）：本来就在的 %d 个，这次补装 %d 个", g_optReinstallMs, already, fixed);
        }

        /* 3) 之后如果还丢，就按开关继续补 */
        if (g_optCodeHooks && g_optRepair && g_mon[0].reinstalled) {
            for (int i = 0; i < g_watchCount && i < 8; i++) {
                unsigned char* p = (unsigned char*)g_watch[i].site;
                if (*p == 0xE9) continue;
                if (*p != g_watch[i].origByte) continue;
                WriteJmp(g_watch[i].site, g_watch[i].len, g_watch[i].tramp);
                if (g_mon[i].reportedLost < 2) {
                    g_mon[i].reportedLost = 2;
                    UiLog("[mon] 又补装了一次 %#08X", g_watch[i].site);
                }
            }
        }

        /* 4) 菜单状态心跳（每 2 秒左右） */
        if (iter - lastMenuLog >= 4) {
            lastMenuLog = iter;
            unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
            int ok = 0, sub = 0, sel = 0, mode = 0, diff = 0, page = 0, slot2 = 0, v1dc = 0;
            unsigned sf = *(unsigned*)PTR_SCOREFILE;
            if (menu) {
                __try {
                    sub = *(int*)(menu + MENU_SUBPAGE_OFF);
                    sel = *(int*)(menu + MENU_SEL_OFF);
                    mode = *(int*)(menu + MENU_MODE_OFF);
                    diff = *(int*)(menu + MENU_DIFF_OFF);
                    page = *(int*)(menu + MENU_PAGE_OFF);
                    slot2 = *(int*)(menu + MENU_SLOT2_OFF);
                    v1dc = *(int*)(menu + MENU_DIFF_OFF2);
                    ok = 1;
                } __except (EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
            }
            if (iter < 60 || (ok && mode == 1) || iter % 20 == 0) {
                UiLog("[mon] %us 菜单=%#08X 子页+20=%d 选择+24=%d 模式5DF0=%d 难度FC=%d 页1D4=%d 槽5CF8=%d 1DC=%d 存档=%#08X",
                      iter / 2, (unsigned)menu, sub, sel, mode, diff, page, slot2, v1dc, sf);
            }
        }
    }
    return 0;
}

/* 菜单状态心跳：每 20 帧记一次（菜单对象为空时也记，用来判断 Tick 有没有在跑） */
static int g_optStateLog = 1;

/* ============ 每帧回调（从 ECLplus 的 D3D9 EndScene 钩子里调）============
   为什么放这里：ECLPLUS.dll 是 ECLplusLoader 在 CreateProcess 之后"立刻"注入的，
   那时 thcrap 的 codecave/断点补丁还没写完，任何改游戏代码的补丁都会被它覆盖（实测三个版本的钩子全部消失）。
   D3D9 的 EndScene 是游戏自己的渲染线程每帧都调用的，等它开始跑（60 帧后）thcrap 早已打完补丁，
   这时再装钩子就稳了；而且这条路径完全不碰游戏代码。 */
/* 从日志实测：进符卡数据页时 子页+20=0 且 模式5DF0=1（探针 call绘制#1 那一行） */
static int IsSpellCardPage(void) {
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    if (!menu) return 0;
    __try {
        int mode = *(int*)(menu + MENU_MODE_OFF);   /* 游戏自己的门：+0x5DF0 == 1 就是符卡数据页 */
        int diff = *(int*)(menu + MENU_DIFF_OFF);
        return (mode == 1 && diff >= 0 && diff <= 4) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ===== v45r12：成就/解锁位运行时探针 =====
   目的：确定"成就状态"到底存在 ScoreFile 的哪个偏移，以及谁把它写成了 1。
   已确认：
     - 512 布局下尾块应在对象内 0xF0148（= 10*0x18020），成就读位在 +0x50（0xF0198）
     - 101 布局的新档该区域是全 0；512 布局的档 tail+0x26..0xA5 是 128 个 01
     - 全程序唯一写这些字节的指令是 award_trophy 里的 0x45D8BC，而它只有一个调用点
   本探针把对象附近 0x2D140/0xF0140 起始的 0x220 字节逐帧快照写日志，
   变化的字节会单独列出 —— 这样"谁在什么时候写了 1"就一目了然。 */
static unsigned g_probeLastHash = 0;
static int      g_probeUnlockOnce = 1;     /* 下一次无条件打一遍 */
static int      g_optProbeUnlock = 1;      /* ini: ProbeUnlock（1=开启成就位探针） */

/* 简单 hex（不调 MiniFmt，避免探针自己被 __try 吞掉） */
static void HexCat(char* out, int cap, const unsigned char* p, int n) {
    static const char* H = "0123456789ABCDEF";
    int w = 0;
    for (int i = 0; i < n && w < cap - 4; i++) {
        out[w++] = H[p[i] >> 4];
        out[w++] = H[p[i] & 15];
        out[w++] = ' ';
    }
    out[w] = 0;
}

static void ProbeUnlockArray(void) {
    if (!g_optProbeUnlock) return;
    unsigned char* sf = *(unsigned char**)PTR_SCOREFILE;
    if (!sf) return;
    unsigned recSize = *(unsigned int*)IMM_RECSIZE;
    if (!recSize) return;
    unsigned tailOff = 10u * recSize;                     /* 512 对象内: 0xF0148 */
    unsigned base = tailOff + 0x40;
    unsigned char* p = sf + base;                         /* 覆盖 tail+0x40..+0x140 */
    unsigned h = 2166136261u;
    char hex[3 * 0x100 + 8];
    HexCat(hex, (int)sizeof(hex), p, 0x100);
    for (int i = 0; i < 0x100; i++) h = (h ^ p[i]) * 16777619u;

    int changed = (h != g_probeLastHash);
    int first = g_probeUnlockOnce;
    if (!changed && !first) return;
    g_probeLastHash = h;
    g_probeUnlockOnce = 0;
    UiLog("[unlockprobe] frame=%u sf=%p recSize=0x%X tailOff=0x%X dumpOff=0x%X %s",
          g_frameCount, sf, recSize, tailOff, base, changed ? "CHANGED" : "first");
    UiLog("[unlockdump] %s", hex);
    /* 游戏读成就位 = [scorefile + idx + 0xF0198]（对象内）。相对 tail 起点就是 +0x50 */
    char hx2[3 * 0x90 + 8];
    HexCat(hx2, (int)sizeof(hx2), sf + tailOff + 0x50, 0x90);
    UiLog("[achbytes] objoff=0x%X %s", tailOff + 0x50, hx2);
}

void TickSpellTableFrame(void) {
    g_frameCount++;

    /* ===== v47：这些"每帧必须做的事"提到所有 return 之前 =====
       之前 MusicLockTick()/SaveCheckInit() 在 `if (!g_optFrameLog) return;` 之后，
       而 ini 里 SpellTableFrameLog=0 → 它们**从来没跑过**（音乐室解锁持久化实际是死的）。 */
    SaveCheckInit();    /* 存档完整性检查（内部只跑一次） */
    MusicLockTick();    /* 音乐室解锁持久化（内部自节流） */

    if (g_optRender && g_optProbeUnlock) ProbeUnlockArray();

    if (g_optCodeHooks && !g_hooksInstalled) {
        if ((int)g_frameCount >= g_lateHookFrames) {
            int ok = 0;
            if (IniInt(L"SpellTableProbe", 1)) InstallProbes();
            g_hooksInstalled = 1;
            UiLog("[late] 第 %u 帧：代码钩子已安装（不接管绘制，避免破坏 ANM 状态）", g_frameCount);
        }
        return;
    }

    /* 在符卡数据页：把菜单指针交给渲染器，每帧加一遍自己的行（游戏自己那份照常画） */
    if (g_optRender) {
        void* menu = *(void**)PTR_MAIN_MENU;
        g_menuPtr = menu;
        /* 关键：EndScene 在 AsciiManager 画完之后才调用，所以不能每帧"清掉再加"（那样它永远看不到）。
           改成每 8 帧加一次、每条活 10 帧，中途一直有字存在。 */
        if (menu && IsSpellCardPage() && (g_frameCount % 8) == 0) RenderSpellTable();
    }

    if (g_optFrameLog && (g_frameCount % 120) == 0) {
        void* am2 = AsciiMgr();
        if (am2) {
            unsigned char* a2 = (unsigned char*)am2;
            UiLog("[ascii] 帧=%u 条目数=%d 颜色=%#08X 缩放=%d,%d 字体=%d 阴影=%d 对齐=%d/%d 组=%d",
                  g_frameCount, *(int*)(a2 + AM_NUM_STRINGS), *(unsigned*)(a2 + AM_COLOR),
                  (int)(*(float*)(a2 + AM_SCALE) * 100), (int)(*(float*)(a2 + AM_SCALE + 4) * 100),
                  *(int*)(a2 + AM_FONT_ID), *(int*)(a2 + AM_DRAW_SHADOWS),
                  *(int*)(a2 + AM_ALIGN_H), *(int*)(a2 + AM_ALIGN_V), *(int*)(a2 + AM_RENDER_GROUP));
        }
    }

    if (!g_optFrameLog) return;

    /* 看门狗：每 30 帧检查一次，掉了就补 */    if (g_hooksInstalled && (g_frameCount % 30) == 0) {
        int fixed = 0;
        for (int i = 0; i < g_watchCount && i < 8; i++) {
            unsigned char* pp = (unsigned char*)g_watch[i].site;
            if (*pp == 0xE9) continue;
            if (*pp != g_watch[i].origByte) continue;
            WriteJmp(g_watch[i].site, g_watch[i].len, g_watch[i].tramp);
            fixed++;
        }
        if (fixed) UiLog("[late] 第 %u 帧：有 %d 个钩子被覆盖 → 已补装", g_frameCount, fixed);
    }

    /* 状态：进入菜单后每 120 帧记一行（SaveCheckInit/MusicLockTick 已在函数开头调用） */
    if ((g_frameCount % 15) != 0) return;   /* v14：15 帧一行的字段追踪 */
    int ok = 0, sub = 0, sel = 0, mode = 0, diff = 0, page = 0, slot2 = 0, v1dc = 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    __try {
        if (menu) {
            sub   = *(int*)(menu + MENU_SUBPAGE_OFF);
            sel   = *(int*)(menu + MENU_SEL_OFF);
            mode  = *(int*)(menu + MENU_MODE_OFF);
            diff  = *(int*)(menu + MENU_DIFF_OFF);
            page  = *(int*)(menu + MENU_PAGE_OFF);
            slot2 = *(int*)(menu + MENU_SLOT2_OFF);
            v1dc  = *(int*)(menu + MENU_DIFF_OFF2);
        }
        ok = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
    char rep[384];
    WatchReport(rep, (int)sizeof(rep));
    if (menu && ok) {
        { char vmd[220]; SpellTableVmDump(vmd, (int)sizeof(vmd)); if (vmd[0]) UiLog("[vm] %s", vmd); }
        UiLog("[frame] 帧=%u 菜单=%#08X 应显示=%d 行VM=%d 子页+20=%d 选择+24=%d 模式5DF0=%d 难度FC=%d 页1D4=%d 槽5CF8=%d 1DC=%d | 钩子：%s",
              g_frameCount, (unsigned)menu, SpellTableShouldShow(), SpellTableVmCount(), sub, sel, mode, diff, page, slot2, v1dc, rep);
    UiLog("[probe-hits] 帧=%u A=%d B=%d C=%d D=%d E=%d F=%d", g_frameCount,
        g_probeHits[0], g_probeHits[1], g_probeHits[2], g_probeHits[3], g_probeHits[4], g_probeHits[5]);
    } else {
        UiLog("[frame] 帧=%u 菜单=%#08X（未在菜单）| 钩子：%s", g_frameCount, (unsigned)menu, rep);
    }
}

/* ================= v10：自绘覆盖层用到的接口 ================= */
static int      g_optDraw = 1;
static int      g_drawRowX = 64, g_drawRowY0 = 172, g_drawRowDY = 21;
static int      g_drawBgX0 = 48, g_drawBgY0 = 162, g_drawBgX1 = 632, g_drawBgY1 = 238;
static int      g_drawFontH = 18, g_drawCharset = 134;
static unsigned g_drawColor = 0xFFFFE060u, g_drawBgColor = 0xFF000000u;   /* 已收取（金） */
static unsigned g_drawColorSeen = 0xFFFFFFFFu;    /* 已遭遇未收取（白） */
static unsigned g_drawColorNone = 0xFFC8C8C8u;    /* 未遭遇（浅灰） */
static unsigned g_drawColorOutline = 0xFF000000u; /* 描边（黑） */
static char     g_drawFace[64] = "SimHei";

void UiLogSafe(const char* fmt, ...) {
    char b[900];
    va_list ap; va_start(ap, fmt);
    MiniFmtV(b, (int)sizeof(b), fmt, ap);
    va_end(ap);
    UiLog("%s", b);
}

int SpellTableDrawEnabled(void) { return (g_optDraw && g_rowCount > 0) ? 1 : 0; }
void SpellTableSetDrawOff(void) { g_optDraw = 0; }

int SpellTableIsActivePage(void) {
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    if (!menu) return 0;
    __try {
        int mode = *(int*)(menu + MENU_MODE_OFF);
        int diff = *(int*)(menu + MENU_DIFF_OFF);
        return (mode == 1 && diff >= 0 && diff <= 4) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void SpellTableDrawParams(int* rowX, int* rowY0, int* rowDY, int* bgX0, int* bgY0, int* bgX1, int* bgY1,
                          int* fontH, char* face, int faceCap, int* charset, unsigned* color, unsigned* bgColor) {
    if (rowX) *rowX = g_drawRowX;
    if (rowY0) *rowY0 = g_drawRowY0;
    if (rowDY) *rowDY = g_drawRowDY;
    if (bgX0) *bgX0 = g_drawBgX0;
    if (bgY0) *bgY0 = g_drawBgY0;
    if (bgX1) *bgX1 = g_drawBgX1;
    if (bgY1) *bgY1 = g_drawBgY1;
    if (fontH) *fontH = g_drawFontH;
    if (charset) *charset = g_drawCharset;
    if (color) *color = g_drawColor;
    if (bgColor) *bgColor = g_drawBgColor;
    if (face && faceCap > 0) {
        int i = 0;
        while (i < faceCap - 1 && g_drawFace[i]) { face[i] = g_drawFace[i]; i++; }
        face[i] = 0;
    }
}


/* v13：ImGui 要 UTF-8；CSV 显示名是 GBK、存档里的符卡名是 SJIS */
/* 手写 UTF-8 合法性检查（不依赖 MultiByteToWideChar 的行为） */
static int IsValidUtf8(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (p && *p) {
        if (*p < 0x80) { p++; continue; }
        int n;
        if ((*p & 0xE0) == 0xC0) n = 1;
        else if ((*p & 0xF0) == 0xE0) n = 2;
        else if ((*p & 0xF8) == 0xF0) n = 3;
        else return 0;
        p++;
        for (int i = 0; i < n; i++) { if ((*p & 0xC0) != 0x80) return 0; p++; }
    }
    return 1;
}
static void NameToUtf8(const char* src, int cp, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!src || !src[0]) return;
    /* 若本来就是合法 UTF-8（编辑器存成 UTF-8 的情况）直接用，避免二次转换毁掉中文 */
    if (IsValidUtf8(src)) {
        lstrcpynA(out, src, cap);
        return;
    }
    /* v16：不信任单一码页 —— 依序尝试（指定码页 → 932 → 系统 ANSI），
       用"结果里汉字的数量 - 半角片假名的数量"来打分选最佳（GBK 会给出汉字，误按 CP932 会给出半角片假名） */
    const int cps[3] = { cp, 932, CP_ACP };
    int bestScore = -100000, bestIdx = -1;
    char best[400];
    best[0] = 0;
    for (int i = 0; i < 3; i++) {
        wchar_t w[128];
        int n = MultiByteToWideChar((UINT)cps[i], 0, src, -1, w, 128);
        if (n <= 0) continue;
        char tmp[400];
        if (WideCharToMultiByte(CP_UTF8, 0, w, -1, tmp, (int)sizeof(tmp), NULL, NULL) <= 0) continue;
        int score = 0;
        for (int k = 0; w[k]; k++) {
            if (w[k] >= 0x4E00 && w[k] <= 0x9FFF) score += 10;       /* 汉字 */
            else if (w[k] >= 0xFF61 && w[k] <= 0xFF9F) score -= 10;  /* 半角片假名：多半是解错了 */
            else if (w[k] >= 0x3040 && w[k] <= 0x30FF) score += 2;   /* 假名 */
        }
        if (score > bestScore) { bestScore = score; bestIdx = i; lstrcpynA(best, tmp, (int)sizeof(best)); }
    }
    if (bestIdx >= 0 && best[0]) {
        static int once = 0;
        if (!once) { once = 1; UiLog("[name] 转换选用 codepage=%d (得分=%d)", cps[bestIdx], bestScore); }
        lstrcpynA(out, best, cap);
        return;
    }
    lstrcpynA(out, src, cap);
}
/* v18：与 SpellTableFormatRow 相同的读取逻辑，但把各列分开返回（供固定列布局用）
   収率用紧凑格式："100%" / "--"（无多余空格） */
int SpellTableFormatRowCols(int rowInPage, char* name, int nameCap,
                            unsigned* capM, unsigned* encM, char* rateM, int rCapM,
                            unsigned* capP, unsigned* encP, char* rateP, int rCapP,
                            unsigned* colorOut) {
    if (name && nameCap > 0) name[0] = 0;
    if (rateM && rCapM > 0) rateM[0] = 0;
    if (rateP && rCapP > 0) rateP[0] = 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize(), nameOff = NameOff();
    if (!menu || !sf || !recSize || !nameOff) return 0;
    int group = 0, page = 0, slot = 0;
    __try {
        group = *(int*)(menu + MENU_DIFF_OFF);
        page  = *(int*)(menu + MENU_PAGE_OFF);
        slot  = *(int*)(menu + MENU_SEL_OFF);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (group < 0 || group >= GROUP_MAX) return 0;
    if (page < 0) page = 0;
    int total = g_groupListCount[group];
    if (total <= 0 || !g_groupList[group]) return 0;
    int idx;
    if (g_pageBreakCnt[group] > 0) {   /* v34：有 @PAGE 断点 → 按断点分页 */
        int st = (page <= 0) ? 0 : ((page - 1 < g_pageBreakCnt[group]) ? g_pageStart[group][page - 1] : total);
        int nx = (page < g_pageBreakCnt[group]) ? g_pageStart[group][page] : total;
        if (st < 0 || st > total) st = total;
        if (nx < st) nx = st;
        if (rowInPage < 0 || rowInPage >= (nx - st)) return 0;
        idx = st + rowInPage;
    } else {
        idx = page * g_pageRows + rowInPage;
    }
    if (idx < 0 || idx >= total) return 0;

    char nm[NAME_LEN];
    NameToUtf8(g_groupList[group][idx].name, 936, nm, (int)sizeof(nm));
    if (name) lstrcpynA(name, nm, nameCap);

    unsigned cM = 0, eM = 0, cP = 0, eP = 0;
    {   /* v45：収率读【记录 9 = 全局符卡历史表】（游戏 Player Data 符卡页读的也是它）*/
        const int id = g_groupList[group][idx].slot;
        unsigned char* e = FindSpellEntry(sf, CurrentSpellRecord(), recSize, id);
        __try {
            if (name && !name[0]) { char raw[64]; ReadSaveName(e + SF_NAME, raw, (int)sizeof(raw)); NameToUtf8(raw, 932, name, nameCap); }
            cM = *(unsigned*)(e + SF_CAP_MAIN);
            cP = *(unsigned*)(e + SF_CAP_PRAC);
            eM = *(unsigned*)(e + SF_ENC_MAIN);
            eP = *(unsigned*)(e + SF_ENC_PRAC);
        } __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    if (eM > 100000u || cM > eM) { cM = 0; eM = 0; }
    if (eP > 100000u || cP > eP) { cP = 0; eP = 0; }
    if (capM) *capM = cM;  if (encM) *encM = eM;
    if (capP) *capP = cP;  if (encP) *encP = eP;
    if (rateM && rCapM > 0) {
        if (eM) MiniFmt(rateM, rCapM, "%u%%", (unsigned)((cM * 100u) / eM));
        else    lstrcpynA(rateM, "--", rCapM);
    }
    if (rateP && rCapP > 0) {
        if (eP) MiniFmt(rateP, rCapP, "%u%%", (unsigned)((cP * 100u) / eP));
        else    lstrcpynA(rateP, "--", rCapP);
    }
    /* v41: mask decision goes through SpellRowUnknown (current record's 4 counters only).
       The old `&& !SpellTableAnyRecordMet(group, idx)` was removed: that function returned 1
       for the original-101 slots (save +0x88 holds a leftover slot number for id 0..149),
       which turned the whole mask condition false. MASKDIAG kept for one more test round. */
    if (g_maskUnknownUi) {
        /* v45r8：诊断直接 dump"自动定位到的那个条目"，因此日志与取数地址必然一致。 */
        static int seenSlot[512];
        int sid = g_groupList[group][idx].slot;
        if (sid >= 0 && sid < 512 && !seenSlot[sid]) {
            seenSlot[sid] = 1;
            unsigned char* _e = FindSpellEntry(sf, CurrentSpellRecord(), recSize, sid);
            unsigned offNow = (unsigned)((size_t)(_e - sf));
            char hex[3 * 0xA0 + 8];
            int hn = 0;
            for (int q = 0; q < 0xA0 && hn < (int)sizeof(hex) - 4; q++)
                hn += MiniFmt(hex + hn, (int)sizeof(hex) - hn, "%02X ", (unsigned)_e[q]);
            UiLog("[maskdiag] grp=%d idx=%d slot=%d rec=%d recSize=0x%X nameOff=0x%X off=0x%X cnt=%u/%u/%u/%u mask=%d oldAny=%d",
                  group, idx, sid, CurrentSpellRecord(), RecSize(), FileNameOff(), offNow,
                  (unsigned)cM, (unsigned)cP, (unsigned)eM, (unsigned)eP,
                  SpellRowUnknownEx(group, idx, cM, cP, eM, eP), (int)SpellTableAnyRecordMet(group, idx));
            /* v45r11：同时打印"当前自机"与"记录9(全局历史)"的四个计数，便于对比
               —— 切换自机时看哪个记录的数值在动，就能确认収率该取哪条。 */
            {
                unsigned char* _p9 = FindSpellEntry(sf, SPELL_HISTORY_RECORD, recSize, sid);
                unsigned char* _pc = FindSpellEntry(sf, CurrentSpellRecord(), recSize, sid);
                UiLog("[maskcmp] slot=%d rec_cur=%d 当前自机=取得%u/%u 遭遇%u/%u | rec9=取得%u/%u 遭遇%u/%u",
                      sid, CurrentSpellRecord(),
                      *(unsigned*)(_pc + SF_CAP_MAIN), *(unsigned*)(_pc + SF_CAP_PRAC),
                      *(unsigned*)(_pc + SF_ENC_MAIN), *(unsigned*)(_pc + SF_ENC_PRAC),
                      *(unsigned*)(_p9 + SF_CAP_MAIN), *(unsigned*)(_p9 + SF_CAP_PRAC),
                      *(unsigned*)(_p9 + SF_ENC_MAIN), *(unsigned*)(_p9 + SF_ENC_PRAC));
            }
            UiLog("[maskdump] slot=%d bytes=%s", sid, hex);
        }
    }
    if (g_maskUnknownUi && SpellRowUnknownEx(group, idx, cM, cP, eM, eP)) {   /* all 4 zero -> mask */
        if (name && nameCap > 0) lstrcpynA(name, "No.？ ？？？", nameCap);
        if (rateM && rCapM > 0) lstrcpynA(rateM, "--", rCapM);
        if (rateP && rCapP > 0) lstrcpynA(rateP, "--", rCapP);
    }
    if (colorOut) { if (!(eM || eP)) *colorOut = g_drawColorNone; else if (!(cM || cP)) *colorOut = g_drawColorSeen; else *colorOut = g_drawColor; }
    return 1;
}
int SpellTablePageRows(void) { return g_pageRows; }
int SpellTableMaxGroup(void) { int m = 0; for (int i = 0; i < GROUP_MAX; i++) if (g_groupListCount[i] > 0) m = i; return m; }
int SpellTableFormatRow(int rowInPage, char* out, int cap, unsigned* colorOut) {
    if (!out || cap < 16) return 0;
    out[0] = 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize(), nameOff = NameOff();
    if (!menu || !sf || !recSize || !nameOff) return 0;
    int group = 0, page = 0, slot = 0;
    __try {
        group = *(int*)(menu + MENU_DIFF_OFF);
        page  = *(int*)(menu + MENU_PAGE_OFF);
        slot  = *(int*)(menu + MENU_SEL_OFF);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (group < 0 || group >= GROUP_MAX) return 0;
    if (page < 0) page = 0;
    int total = g_groupListCount[group];
    if (total <= 0 || !g_groupList[group]) return 0;
    int idx;
    if (g_pageBreakCnt[group] > 0) {   /* v34：有 @PAGE 断点 → 按断点分页 */
        int st = (page <= 0) ? 0 : ((page - 1 < g_pageBreakCnt[group]) ? g_pageStart[group][page - 1] : total);
        int nx = (page < g_pageBreakCnt[group]) ? g_pageStart[group][page] : total;
        if (st < 0 || st > total) st = total;
        if (nx < st) nx = st;
        if (rowInPage < 0 || rowInPage >= (nx - st)) return 0;
        idx = st + rowInPage;
    } else {
        idx = page * g_pageRows + rowInPage;
    }
    if (idx < 0 || idx >= total) return 0;
    const int id = g_groupList[group][idx].slot;

    char name[NAME_LEN];
    name[0] = 0;
    {
        NameToUtf8(g_groupList[group][idx].name, 936, name, (int)sizeof(name));   /* CSV=GBK → UTF-8 */
    }

    unsigned capM = 0, capP = 0, encM = 0, encP = 0;
    {   /* v45：収率读【记录 9 = 全局符卡历史表】 */
        unsigned char* e = FindSpellEntry(sf, CurrentSpellRecord(), recSize, id);
        __try {
            if (!name[0]) ReadSaveName(e + SF_NAME, name, sizeof(name));
            capM = *(unsigned*)(e + SF_CAP_MAIN);
            capP = *(unsigned*)(e + SF_CAP_PRAC);
            encM = *(unsigned*)(e + SF_ENC_MAIN);
            encP = *(unsigned*)(e + SF_ENC_PRAC);
        } __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    char rm[8], rp[8];
    FmtRate(rm, sizeof(rm), capM, encM);
    FmtRate(rp, sizeof(rp), capP, encP);
    /* v15：无条件按 CSV 原文转换（NameToUtf8 内部先判是否已是合法 UTF-8，是则原样通过） */
    {
        char nm[NAME_LEN];
        NameToUtf8(g_groupList[group][idx].name, 936, nm, (int)sizeof(nm));
        if (nm[0]) lstrcpynA(name, nm, (int)sizeof(name));
        /* v41/v42: same single rule as SpellTableFormatRowCols (current record's 4 counters) */
        if (g_maskUnknownUi && SpellRowUnknownEx(group, idx, capM, capP, encM, encP)) {
            lstrcpynA(name, "No.？ ？？？", (int)sizeof(name));
            lstrcpynA(rm, "--", (int)sizeof(rm));
            lstrcpynA(rp, "--", (int)sizeof(rp));
        }
        static int once2 = 0;
        if (!once2) {
            once2 = 1;
            char h1[130], h2[130]; int n1 = 0, n2 = 0;
            const char* src = g_groupList[group][idx].name;
            for (int q = 0; src && src[q] && n1 < 120; q++) n1 += MiniFmt(h1 + n1, 130 - n1, "%02X ", (unsigned char)src[q]);
            for (int q = 0; name[q] && n2 < 120; q++) n2 += MiniFmt(h2 + n2, 130 - n2, "%02X ", (unsigned char)name[q]);
            UiLog("[name] CSV原文: %s| 转后: %s", h1, h2);
        }
    }
    {   /* 只记一次：确认送进 ImGui 的到底是合法 UTF-8 还是没转干净的字节 */
        static int once = 0;
        if (!once) {
            once = 1;
            char hex[200]; int hn = 0;
            for (int q = 0; name[q] && hn < 190; q++) hn += MiniFmt(hex + hn, 200 - hn, "%02X ", (unsigned char)name[q]);
            UiLog("[name] 第1行名字字节: %s", hex);
        }
    }
    if (colorOut) *colorOut = (encM || encP) ? ((capM || capP) ? g_drawColor : 0xFFFFFFFFu) : 0xFFA0A0A0u;
    MiniFmt(out, cap, "No.%02d %s  M %3u/%3u %s  P %3u/%3u %s",
            idx + 1, name[0] ? name : "----------", capM, encM, rm, capP, encP, rp);
    return 1;
}

/* ===== v11：ImGui 界面参数（相关的 ImGui 界面模块已全部封存到 quarantine/，保留接口以防将来复用） ===== */
static int      g_guiOn = 1;
static int      g_guiX = 120, g_guiY = 96, g_guiW = 430, g_guiH = 250;
static float    g_guiFontSize = 18.0f;
static unsigned g_guiText = 0xFFFFFFF0u;
static char     g_guiFontFile[260] = { 0 };

void SpellTableGuiParams(int* on, int* x, int* y, int* w, int* h, float* fontSize,
                         unsigned* text, char* file, int fileCap) {
    if (on) *on = g_guiOn;
    if (x) *x = g_guiX;
    if (y) *y = g_guiY;
    if (w) *w = g_guiW;
    if (h) *h = g_guiH;
    if (fontSize) *fontSize = g_guiFontSize;
    if (text) *text = g_guiText;
    if (file && fileCap > 0) {
        int i = 0;
        while (i < fileCap - 1 && g_guiFontFile[i]) { file[i] = g_guiFontFile[i]; i++; }
        file[i] = 0;
    }
}

int SpellTableGroupRows(int group) {
    return (group >= 0 && group < GROUP_MAX) ? g_groupListCount[group] : 0;
}

int SpellTableCurrentGroup(void) {
    unsigned char* m = *(unsigned char**)PTR_MAIN_MENU;
    if (!m) return -1;
    __try {
        int g = *(int*)(m + MENU_DIFF_OFF);
        return (g >= 0 && g < GROUP_MAX) ? g : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* v13：更严格的"当前是否停在符卡数据页"判定（用实测签名：子页+20==2 且 1DC!=0 且 模式5DF0==1） */
static volatile DWORD g_listDrawTick = 0;
void SpellTableNoteListDraw(void) {
    DWORD now = GetTickCount();
    static DWORD last = 0;
    static int logged = 0;
    if (last && logged < 20) {
        DWORD gap = now - last;
        if (gap > 900) { logged++; UiLog("[probe] 距上次绘制调用 %ums（用于确定页面信号窗口）", (unsigned)gap); }
    }
    last = now;
    g_listDrawTick = now;
}

int SpellTableVmCount(void) {   /* 行 VM 还活着几个（列表页=若干，离开页面=0） */
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    if (!menu) return 0;
    int n = 0;
    __try {
        for (int i = 0; i < 10; i++) { float tmp[3]; if (VmRowPos(menu, i, tmp)) n++; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return n;
}

static volatile int g_hideGameRows = 0;   /* ini: SpellTableHideGameRows（1=清空游戏内部表） */

int SpellTableShouldShow(void) {

    {   /* v39: log the gate inputs + decision (every 60 frames) so we can align fields with the log */
        static unsigned lastLog = 0;
        if (g_frameCount - lastLog >= 60) {
            lastLog = g_frameCount;
            __try {
                unsigned char* mn = *(unsigned char**)PTR_MAIN_MENU;
                if (mn) UiLog("[gate] 帧=%u 探针见过=%d 模式=%d 槽5CF8=%d 1DC=%d 子页20=%d 难度=%d 行VM=%d",
                    g_frameCount, g_listDrawSeen, *(int*)(mn + MENU_MODE_OFF), *(int*)(mn + MENU_SLOT2_OFF),
                    *(int*)(mn + MENU_DIFF_OFF2), *(int*)(mn + MENU_SUBPAGE_OFF), *(int*)(mn + MENU_DIFF_OFF), SpellTableVmCount());
            } __except (EXCEPTION_EXECUTE_HANDLER) { }
        }
    }
    /* v36: only on Player Data spell pages. field +0x1DC is 3 there, 2 on the
       spell-card practice page (verified from the frame log), so require 3. */
    if (g_listDrawSeen) {
        unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
        if (!menu) return 0;
        __try {
            int mode = *(int*)(menu + MENU_MODE_OFF);
            if (mode != 1) return 0;
            if (SpellTableVmCount() < 5) return 0;   /* v40: player-data spell page has 10 row VMs, practice list has 4 (measured at the gate) */
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        return (SpellTableVmCount() >= 1) ? 1 : 0;
    }
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    if (!menu) return 0;
    __try {
        int sub  = *(int*)(menu + MENU_SUBPAGE_OFF);
        int mode = *(int*)(menu + MENU_MODE_OFF);
        int diff = *(int*)(menu + MENU_DIFF_OFF);
        return (mode == 1 && diff >= 0 && diff <= 4 && sub == 2 && SpellTableVmCount() >= 5) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SpellTableIniInt(const wchar_t* key, int def) { return IniInt(key, def); }   /* v26：统一 ini 读取 */

/* v52：把"当前页号"从主表渲染里独立出来，供索引条(score_index.cpp)使用。
   来源就是主表一直在用的 [menu + 0x1D4]；不在符卡数据页返回 -1。 */
int SpellTableCurrentPage(void) {
    unsigned char* m = *(unsigned char**)PTR_MAIN_MENU;
    if (!m) return -1;
    __try {
        if (!SpellTableIsActivePage()) return -1;
        int p = *(int*)(m + MENU_PAGE_OFF);
        return (p >= 0) ? p : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}


/* 供日志/调试：把关键字段一次性拿出来 */
void SpellTablePageInfo(int* sub, int* mode, int* diff, int* v1dc, int* rows) {
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    *sub = *mode = *diff = *v1dc = -1; *rows = 0;
    if (!menu) return;
    __try {
        *sub  = *(int*)(menu + MENU_SUBPAGE_OFF);
        *mode = *(int*)(menu + MENU_MODE_OFF);
        *diff = *(int*)(menu + MENU_DIFF_OFF);
        *v1dc = *(int*)(menu + MENU_DIFF_OFF2);
        if (*diff >= 0 && *diff < GROUP_MAX) *rows = g_groupListCount[*diff];
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}
void TickSpellSlotsUI(void) {
    static unsigned tick = 0;
    static unsigned logged = 0;
    tick++;
    if (!g_optStateLog) return;
    if (tick % 20) return;
    if (logged > 240) return;
    logged++;
    int hookOk = ((unsigned char*)SITE_SPELL_DRAW)[0] == 0xE9 ? 1 : 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    int valid = 0, sub = 0, sel = 0, mode = 0, diff = 0, page = 0, slot2 = 0, row2 = 0;
    if (menu) {
        __try {
            sub = *(int*)(menu + MENU_SUBPAGE_OFF);
            sel = *(int*)(menu + MENU_SEL_OFF);
            mode = *(int*)(menu + MENU_MODE_OFF);
            diff = *(int*)(menu + MENU_DIFF_OFF);
            page = *(int*)(menu + MENU_PAGE_OFF);
            slot2 = *(int*)(menu + MENU_SLOT2_OFF);
            row2 = *(int*)(menu + MENU_ROW2_OFF);
            valid = (sub >= 0 && sub < 16 && sel >= 0 && sel < 64 && page >= 0 && page < 64) ? 1 : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            valid = 0;
        }
    }
    if (menu && valid) {
        UiLog("[state] 帧=%u 菜单=%#08X 子页=%d 选择=%d 模式5DF0=%d 难度FC=%d 页1D4=%d 槽5CF8=%d 行5DF4=%d 钩子=%s",
              tick, (unsigned)menu, sub, sel, mode, diff, page, slot2, row2, hookOk ? "在" : "**已被覆盖**");
    } else {
        UiLog("[state] 帧=%u 菜单=%#08X（无效/未进菜单）钩子=%s", tick, (unsigned)menu, hookOk ? "在" : "**已被覆盖**");
    }
}


void InitSpellSlotsUI(void) {
    if (!IniInt(L"SpellTable", 0)) return;
    UiLog("[score-ui] init：符卡数据表模块 v9 启动");
    LoadTable();
    if (g_rowCount == 0) { UiLog("[score-ui] 表为空，不安装"); return; }
    if (IniInt(L"SpellTableInit", 1)) {
        BuildSaveDiffTable();
        int ok = 0;
        ok += PatchInitDiffTable() ? 1 : 0;
        ok += PatchInitLimit(MAX_SLOTS) ? 1 : 0;
        UiLog("[score-ui] 读档初始化补丁 %d/2（目标 %d 项）", ok, MAX_SLOTS);
    }
    if (IniInt(L"SpellTableFilter", 1)) {
        int ok = 0;
        ok += PatchFilterAt(SITE_FILTER_DISP) ? 1 : 0;
        ok += PatchFilterAt(SITE_FILTER_DISP2) ? 1 : 0;
        UiLog("[score-ui] 过滤表补丁 %d/2", ok);
    }
    if (IniInt(L"SpellTableLimit", 1)) {
        int ok = 0;
        ok += PatchLimit(SITE_LIMIT1, SITE_LIMIT1_JT, g_limit) ? 1 : 0;
        ok += PatchLimit(SITE_LIMIT2, SITE_LIMIT2_JT, g_limit) ? 1 : 0;
        UiLog("[score-ui] 上限补丁 %d/2（目标 %d）", ok, g_limit);
    }

    /* 崩溃报告（诊断用，默认开）：把异常码/地址/EIP 写进日志，只报一次 */
    g_crashLog = IniInt(L"SpellTableCrashLog", 1);
    if (g_crashLog) {
        wchar_t me[MAX_PATH];
        if (GetModuleFileNameW(NULL, me, MAX_PATH)) { /* 只为填充，下面单独取 DLL 基址 */ }
        HMODULE hm = NULL;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)(const void*)&RenderSpellTable, &hm) && hm) {
            g_dllBase = (unsigned)(DWORD_PTR)hm;
        }
        SetUnhandledExceptionFilter(CrashFilter);
        AddVectoredExceptionHandler(1, CrashVeh);
        UiLog("[score-ui] 崩溃报告已开启：exe 基准=%#08X DLL 基准=%#08X", g_exeBase, g_dllBase);
    }

    /* v9 渲染 */
    g_optRender = IniInt(L"SpellTableRender", 0);
    g_optPos    = IniInt(L"SpellTablePos", 0);
    g_fx        = (float)IniInt(L"SpellTableX", 96);
    g_fy0       = (float)IniInt(L"SpellTableY0", 152);
    g_fdy       = (float)IniInt(L"SpellTableDY", 16);
    g_fxoff     = (float)IniInt(L"SpellTableXOff", 0);
    g_fyoff     = (float)IniInt(L"SpellTableYOff", 0);
    g_alignH    = IniInt(L"SpellTableAlignH", 0);
    g_alignV    = IniInt(L"SpellTableAlignV", 1);
    g_fontId    = IniInt(L"SpellTableFont", 2);
    g_shadow    = IniInt(L"SpellTableShadow", 1);
    g_duration  = IniInt(L"SpellTableDuration", 1);
    g_logRows   = IniInt(L"SpellTableRowLog", 1);
    /* v10 自绘覆盖层参数 */
    g_optDraw     = IniInt(L"SpellTableDraw", 1);
    g_drawRowX    = IniInt(L"DrawRowX", 64);
    g_drawRowY0   = IniInt(L"DrawRowY0", 172);
    g_drawRowDY   = IniInt(L"DrawRowDY", 21);
    g_drawBgX0    = IniInt(L"DrawBgX0", 48);
    g_drawBgY0    = IniInt(L"DrawBgY0", 162);
    g_drawBgX1    = IniInt(L"DrawBgX1", 632);
    g_drawBgY1    = IniInt(L"DrawBgY1", 238);
    g_drawFontH   = IniInt(L"DrawFontH", 18);
    g_drawCharset = IniInt(L"DrawCharset", 134);
    g_drawColor   = (unsigned)IniInt(L"DrawColor", (int)0xFFFFE060);
    g_drawColorSeen    = (unsigned)IniInt(L"DrawColorSeen", (int)0xFFFFFFFF);
    g_drawColorNone    = (unsigned)IniInt(L"DrawColorNone", (int)0xFFC8C8C8);
    g_drawColorOutline = (unsigned)IniInt(L"DrawColorOutline", (int)0xFF000000);
    g_drawBgColor = (unsigned)IniInt(L"DrawBgColor", (int)0xFF000000);
    {   /* 字体名（字符串型 ini） */
        wchar_t path[MAX_PATH], wf[64];
        DWORD n = GetModuleFileNameW((HMODULE)NULL, path, MAX_PATH);
        wchar_t* slash = (n && n < MAX_PATH) ? wcsrchr(path, L'\\') : 0;
        if (slash) {
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"ECLplus.ini");
            GetPrivateProfileStringW(L"ECLplus", L"DrawFontFace", L"SimHei", wf, 64, path);
            WideCharToMultiByte(CP_ACP, 0, wf, -1, g_drawFace, (int)sizeof(g_drawFace), NULL, NULL);
        }
    }
    g_guiOn       = IniInt(L"SpellTableGui", 1);
    g_guiX        = IniInt(L"GuiX", 120);
    g_guiY        = IniInt(L"GuiY", 96);
    g_guiW        = IniInt(L"GuiW", 430);
    g_guiH        = IniInt(L"GuiH", 250);
    g_guiFontSize = (float)IniInt(L"GuiFontSize", 18);
    g_guiText     = (unsigned)IniInt(L"GuiTextColor", (int)0xFFFFFFF0);
    {   /* 字体文件（可留空 = 自动找 Touhou_Simhei.ttf / simhei.ttf） */
        wchar_t path[MAX_PATH], wf[MAX_PATH];
        DWORD n = GetModuleFileNameW((HMODULE)NULL, path, MAX_PATH);
        wchar_t* sl = (n && n < MAX_PATH) ? wcsrchr(path, L'\\\\') : 0;
        if (sl) {
            wcscpy_s(sl + 1, MAX_PATH - (sl + 1 - path), L"ECLplus.ini");
            GetPrivateProfileStringW(L"ECLplus", L"GuiFontFile", L"", wf, MAX_PATH, path);
            WideCharToMultiByte(CP_ACP, 0, wf, -1, g_guiFontFile, (int)sizeof(g_guiFontFile), NULL, NULL);
        }
    }
    { void ScoreGuiInit(void); ScoreGuiInit(); }   /* 读取 ImGui 界面参数 */
    { void IndexBarInit(void); IndexBarInit(); }   /* v52：读取索引条的 ini 参数 */
    /* v21：把游戏内部那张符卡表清空（游戏不再画任何行），我们自己的表照旧从 CSV 读 */
    g_hideGameRows = IniInt(L"SpellTableHideGameRows", 1);
g_maskUnknownUi = IniInt(L"GuiMaskUnknown", 1);   /* v30：未遭遇名字打码 */   /* 默认 1：游戏表已是内部空表，不依赖它的行 VM */
g_optMaskAnyRecord = IniInt(L"GuiMaskAnyRecord", 1);   /* v42：1=任一存档记录遇到过就显示真名（默认开） */
g_optMaskNeedCap = IniInt(L"GuiMaskNeedCap", 0);       /* v58：0=遭遇过就算解锁【默认】；1=必须取得≥1 */
    if (g_hideGameRows) {
        /* v22：游戏读的是内部空表 g_emptyDiff，这里无需再动 */
        UiLog("[gui] 已清空游戏内部符卡表（HideGameRows=1）");
    }
    UiLog("[draw] 自绘参数：黑底 %d,%d-%d,%d 行 x=%d y0=%d dy=%d 字号=%d 字体=%s charset=%d 颜色=%#08X",
          g_drawBgX0, g_drawBgY0, g_drawBgX1, g_drawBgY1, g_drawRowX, g_drawRowY0, g_drawRowDY,
          g_drawFontH, g_drawFace, g_drawCharset, g_drawColor);
    g_optCodeHooks = IniInt(L"SpellTableCodeHooks", 0);
    g_optLateHooks   = IniInt(L"SpellTableLateHooks", 1);
    g_lateHookFrames = IniInt(L"SpellTableLateFrames", 150);
    if (g_optRender && g_optLateHooks) {
        UiLog("[score-ui] 代码钩子改为【延迟安装】：等 %d 帧（约 %d 毫秒）后再装，避开 thcrap 的补丁写入", g_lateHookFrames, g_lateHookFrames * 16);
    } else if (0) {
        int ok = 0;
        UiLog("[score-ui] 自建渲染补丁 %d/1（每页 %d 行，坐标=%s，对齐 %d/%d，字体 %d，阴影 %d，存活 %d 帧）",
              ok, g_pageRows, g_optPos ? "固定" : "行VM", g_alignH, g_alignV, g_fontId, g_shadow, g_duration);
    } else if (g_optRender) {
        UiLog("[score-ui] SpellTableCodeHooks=0 → 不改游戏代码，本次不安装绘制接管（避免和 thcrap 打架）");
    } else {
        UiLog("[score-ui] SpellTableRender=0：不做自建渲染");
    }

    g_optNames = IniInt(L"SpellTableNames", 1);
    if (g_optNames && !g_optRender) {
        int ok = 0;
        ok += PatchRowSet() ? 1 : 0;
        ok += PatchDraw() ? 1 : 0;
        UiLog("[score-ui] 名字覆盖补丁 %d/2", ok);
    } else if (g_optRender) {
        UiLog("[score-ui] 自建渲染开启 → 跳过 v7 的名字覆盖钩子");
    } else {
        UiLog("[score-ui] 按开关不做名字覆盖");
    }

    /* 诊断：探针 + 菜单状态心跳 */
    g_optStateLog = IniInt(L"SpellTableStateLog", 1);
    if (IniInt(L"SpellTableProbe", 1) && g_optCodeHooks && !g_optLateHooks) InstallProbes();
    else if (IniInt(L"SpellTableProbe", 1) && g_optCodeHooks) UiLog("[probe] 探针改为【延迟安装】（在 thcrap 打完补丁之后）");
    else UiLog("[probe] 不装探针（SpellTableCodeHooks=0 或 SpellTableProbe=0）");
    /* 看门狗改用独立的监控线程（见 MonitorThread），不再依赖游戏代码 */
    UiLog("[score-ui] 诊断心跳开关 SpellTableStateLog=%d（每 20 帧记一次菜单状态）", g_optStateLog);
    /* 监控线程：不依赖任何游戏钩子，负责发现覆盖 + 延迟重装 */
    g_optMonitor     = IniInt(L"SpellTableMonitor", 1);
    g_optReinstallMs = IniInt(L"SpellTableReinstallMs", 3000);
    g_optRepair      = IniInt(L"SpellTableRepair", 1);
    if (g_optMonitor) {
        HANDLE h = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        UiLog("[mon] 监控线程已启动（句柄=%#X，延迟重装=%dms，持续补装=%d，监控点 %d 个）",
              (unsigned)(DWORD_PTR)h, g_optReinstallMs, g_optRepair, g_watchCount);
    } else {
        UiLog("[mon] 监控线程关闭（SpellTableMonitor=0）");
    }
}
/* ★ 恢复：ProbeLogC（asm 探针调用的函数 ✓）
   原始实现包含：主菜单 tick 看门狗 ✓ + [tick] 状态日志 ✓ + 列表绘制钩子 ✓
   本次因误删重建，保留了【看门狗】与【列表绘制钩子】（[tick] 详细日志略去 ✓） */
static void ProbeLogC(void) {
    int id = g_probeId;
    if (id < 0 || id > 7) return;
    if (id == 7) {                       /* 主菜单 tick：看门狗 ✓ */
        static unsigned tc = 0;
        tc++;
        if (g_optWatchdog && (tc % 30) == 0 && g_watchCount > 1) WatchdogFix();
        return;
    }
    if (id >= 2 && id <= 5) { g_listDrawSeen = 1; SpellTableNoteListDraw(); }
}
/* ==================== REBUILD4：误删恢复（4 个函数 ✓） ==================== */

/* v41: the default mask decision. Looks at the current record's 4 counters; all zero -> mask.
   Why SpellTableAnyRecordMet is no longer ANDed in unconditionally: the save's +0x88 column
   holds a leftover slot number (0,1,2,...,149) for id 0..149 instead of an encounter count,
   so AnyRecordMet returned 1 for every original-101 slot and made the whole mask condition
   false. The "any save slot counts as met" behaviour is still available as an option:
   ini [ECLplus] GuiMaskAnyRecord=1  (see SpellRowUnknownEx below). */
int SpellRowUnknown(unsigned capM, unsigned capP, unsigned encM, unsigned encP) {
    return (capM == 0 && capP == 0 && encM == 0 && encP == 0) ? 1 : 0;
}

/* v45: 収率显示固定读 SPELL_HISTORY_RECORD（见文件顶部定义）—— 此处不再重复定义。 */

/* v44: aggregate the 4 counters over all 10 save records.
   取证（tools/ 的 live 存档 scorethow.dat，slot=93 = CSV「EX,93,No.220」）：
     rec0 取练=1 遇练=4 ；rec9 取练=1 遇练=4 遇主=1
     游戏横幅显示 History 01/04 —— 即"取得/遭遇"是【跨自机取最大值】后的结果。
   而 v9/ImGui 两条绘制路径原来只读【当前记录】，于是换过自机后表里就变成 0/x。
   这里改为：每个字段在 10 条记录里**取最大值**（不是求和 —— 求和会出现取得>遭遇
   把収率算成几百 %）。这样与游戏横幅口径一致，也和"任一自机解锁"的打码判定一致。
   返回 1 表示读到了。*/
int SpellTableAggCounters(int slot, unsigned* cm, unsigned* cp, unsigned* em, unsigned* ep) {
    if (cm) *cm = 0;
    if (cp) *cp = 0;
    if (em) *em = 0;
    if (ep) *ep = 0;
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize(), nameOff = FileNameOff();
    if (!sf || !recSize || !nameOff || slot < 0 || slot > 511) return 0;
    unsigned a = 0, b = 0, c = 0, d = 0;
    __try {
        for (int rec = 0; rec < 10; rec++) {
            unsigned char* e = sf + (size_t)rec * recSize + nameOff + (size_t)slot * SF_SIZE;
            unsigned v1 = *(unsigned*)(e + SF_CAP_MAIN);
            unsigned v2 = *(unsigned*)(e + SF_CAP_PRAC);
            unsigned v3 = *(unsigned*)(e + SF_ENC_MAIN);
            unsigned v4 = *(unsigned*)(e + SF_ENC_PRAC);
            if (v3 > 100000u || v1 > v3) { v1 = 0; v3 = 0; }     /* 合理性校验 */
            if (v4 > 100000u || v2 > v4) { v2 = 0; v4 = 0; }
            if (v1 > a) a = v1;
            if (v2 > b) b = v2;
            if (v3 > c) c = v3;
            if (v4 > d) d = v4;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (cm) *cm = a;
    if (cp) *cp = b;
    if (em) *em = c;
    if (ep) *ep = d;
    return 1;
}

/* v45r10：把"解锁"判据收敛到一个函数，避免开关写在死路径上。
/* v58：解锁判据 —— 按用户要求，【遭遇过就算解锁】。
   GuiMaskNeedCap=0（默认，遭遇口径）：四个计数任一 ≥1 即解锁（含 遭遇主/遭遇练）。
       注意 rate（収率显示）仍然只统计"取得"，所以"遭遇过但没收"的卡会显示真名 + 0% 収率。
   GuiMaskNeedCap=1（严格口径，可选）：必须 取得主/取得练 ≥1 才算解锁。
   历史：v45r9 曾把默认设成 1（只认取得），因为记录 9 里原版 101 多为 (取得0, 遭遇1)，
        全显示真名当年被当成回归。现按用户要求改回"遭遇即可"。 */
static int RowMet(unsigned cm, unsigned cp, unsigned em, unsigned ep) {
    if (g_optMaskNeedCap) return (cm != 0 || cp != 0) ? 1 : 0;              /* 只认取得 */
    return (cm != 0 || cp != 0 || em != 0 || ep != 0) ? 1 : 0;              /* 遭遇也算（默认） */
}

/* v42: optional "any playable character counts" mode.
   GuiMaskAnyRecord=0 -> only the record on screen decides (v41 behaviour).
   GuiMaskAnyRecord=1 -> if ANY of the 10 save records counts as met, show the real name.
   The (cm,cp,em,ep) passed in are the current record's counters. */
int SpellRowUnknownEx(int group, int idx, unsigned cm, unsigned cp, unsigned em, unsigned ep) {
    if (RowMet(cm, cp, em, ep)) return 0;             /* 当前记录已解锁 -> 真名 */
    if (!g_optMaskAnyRecord) return 1;                /* 只看当前自机 -> 打码 */
    return SpellTableAnyRecordMet(group, idx) ? 0 : 1;/* 任一自机解锁 -> 真名 */
}

/* Same rule for "row number inside group" (used by the v7 name hook; row order = slot ascending) */
int SpellRowUnknownByIndex(int group, int row) {
    if (group < 0 || group >= GROUP_MAX || row < 0 || row >= MAX_ROWS) return 1;
    if (row >= g_groupNameCount[group]) return 1;
    int slot = g_groupSlots[group][row];
    if (slot < 0 || slot >= MAX_SLOTS) return 1;
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize(), nameOff = NameOff();
    unsigned cm = 0, cp = 0, em = 0, ep = 0;
    unsigned char* menu = *(unsigned char**)PTR_MAIN_MENU;
    int cur = 0;
    __try { if (menu) cur = *(int*)(menu + MENU_SLOT_OFF); } __except (EXCEPTION_EXECUTE_HANDLER) { cur = 0; }
    if (cur < 0 || cur > 9) cur = 0;
    if (sf && recSize && nameOff && menu) {
        unsigned char* e = FindSpellEntry(sf, cur, recSize, slot);   /* v45r8: 自动定位 */
        __try {
            cm = *(unsigned*)(e + SF_CAP_MAIN);
            cp = *(unsigned*)(e + SF_CAP_PRAC);
            em = *(unsigned*)(e + SF_ENC_MAIN);
            ep = *(unsigned*)(e + SF_ENC_PRAC);
        } __except (EXCEPTION_EXECUTE_HANDLER) { cm = cp = em = ep = 0; }
        if (em > 100000u || cm > em) { cm = 0; em = 0; }
        if (ep > 100000u || cp > ep) { cp = 0; ep = 0; }
    }
    /* v45r10: 与 SpellRowUnknownEx 同一条判据（RowMet 收敛） */
    if (RowMet(cm, cp, em, ep)) return 0;
    if (!g_optMaskAnyRecord) return 1;
    return SpellTableAnyRecordMetSlot(g_groupSlots[group][row]) ? 0 : 1;
}

/* Any record (player) ever met this spell card: v41 also counts obtained counters now.
   `slot` is the spell card's internal slot number (not the CSV row index). */
int SpellTableAnyRecordMetSlot(int slot) {
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize(), nameOff = NameOff();
    if (!sf || !recSize || !nameOff) return 0;
    if (slot < 0 || slot > 510) return 0;
    int any = 0;
    __try {
        for (int rec = 0; rec < 10 && !any; rec++) {          /* 存档只有 10 条记录 ✓ */
            unsigned char* e = FindSpellEntry(sf, rec, recSize, slot);   /* v45r8: 自动定位 */
            unsigned em = *(unsigned*)(e + SF_ENC_MAIN), ep = *(unsigned*)(e + SF_ENC_PRAC);
            unsigned cm = *(unsigned*)(e + SF_CAP_MAIN), cp = *(unsigned*)(e + SF_CAP_PRAC);
            if (em > 100000u || cm > em) { em = 0; cm = 0; }   /* 合理性校验 ✓ */
            if (ep > 100000u || cp > ep) { ep = 0; cp = 0; }
            /* v58：GuiMaskNeedCap=0（默认）时【四个计数任一 ≥1 即算解锁】——
               也就是说"遭遇过"就够了（用户明确要求）。
               GuiMaskNeedCap=1 为严格口径，只认"取得"。 */
            if (g_optMaskNeedCap) {
                if (cm != 0 || cp != 0) any = 1;
            } else {
                if (cm != 0 || cp != 0 || em != 0 || ep != 0) any = 1;   /* 遭遇也算（默认） */
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { any = 0; }
    return any;
}

int SpellTableAnyRecordMet(int group, int idx) {
    if (group < 0 || group >= GROUP_MAX || idx < 0) return 0;
    if (idx >= g_groupListCount[group] || !g_groupList[group]) return 0;
    return SpellTableAnyRecordMetSlot(g_groupList[group][idx].slot);
}

/* 分组按钮的标签：E / N / H / L / EX …（超出则 G<n> ✓） */
void SpellTableGroupLabel(int group, char* lab, int cap) {
    static const char* kLab[5] = { "E", "N", "H", "L", "EX" };
    if (!lab || cap <= 0) return;
    if (group >= 0 && group < 5) lstrcpynA(lab, kLab[group], cap);
    else _snprintf_s(lab, (size_t)cap, _TRUNCATE, "G%d", group);
}

/* 收集表里用到的字形（GBK → Unicode ✓，去重 ✓） */
int SpellTableCollectChars(unsigned short* out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    for (int g = 0; g < GROUP_MAX; g++) {
        for (int i = 0; i < g_groupListCount[g]; i++) {
            const char* s = g_groupList[g][i].name;
            if (!s || !s[0]) continue;
            wchar_t wbuf[NAME_LEN];
            int wn = MultiByteToWideChar(936, 0, s, -1, wbuf, NAME_LEN);
            for (int q = 0; q < wn - 1 && n < cap; q++) {
                unsigned short cp = (unsigned short)wbuf[q];
                if (!cp) continue;
                int dup = 0;
                for (int z = 0; z < n; z++) if (out[z] == cp) { dup = 1; break; }
                if (!dup) out[n++] = cp;
            }
        }
    }
    return n;
}

/* 自绘表取第 rowIndex 行的显示文本（名字 / 収率 / 计数 ✓；含打码 ✓） */
int SpellTableFormatRowAt(int group, int rowIndex, char* name, int nameCap,
                          unsigned* capMOut, unsigned* encMOut, char* rateM, int rCapM,
                          unsigned* capPOut, unsigned* encPOut, char* rateP, int rCapP,
                          unsigned* colorOut) {
    (void)colorOut;
    if (group < 0 || group >= GROUP_MAX) return 0;
    int total = g_groupListCount[group];
    if (total <= 0 || !g_groupList[group]) return 0;
    int idx = rowIndex;
    if (idx < 0 || idx >= total) return 0;

    const int id = g_groupList[group][idx].slot;
    char nm[NAME_LEN];
    nm[0] = 0;
    NameToUtf8(g_groupList[group][idx].name, 936, nm, (int)sizeof(nm));
    if (name && nameCap > 0) lstrcpynA(name, nm, nameCap);

    unsigned capM = 0, capP = 0, encM = 0, encP = 0;
    unsigned char* sf = ScoreFile();
    unsigned recSize = RecSize(), nameOff = NameOff();
    if (sf && recSize && nameOff && id >= 0 && id < 512) {
        /* v45r11：収率读"当前自机"的记录（原注释写记录 9 是旧的）。
           与 SpellTableFormatRowCols / RenderSpellTableInner 保持一致，
           否则 v9 显示 1/4、ImGui 显示 0/0，两个渲染器数字不一样。 */
        unsigned char* e = FindSpellEntry(sf, CurrentSpellRecord(), recSize, id);
        __try {
            if (name && nameCap > 0 && !name[0]) ReadSaveName(e + SF_NAME, name, nameCap);
            capM = *(unsigned*)(e + SF_CAP_MAIN);
            capP = *(unsigned*)(e + SF_CAP_PRAC);
            encM = *(unsigned*)(e + SF_ENC_MAIN);
            encP = *(unsigned*)(e + SF_ENC_PRAC);
        } __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    if (capMOut) *capMOut = capM;
    if (encMOut) *encMOut = encM;
    if (capPOut) *capPOut = capP;
    if (encPOut) *encPOut = encP;
    if (rateM && rCapM > 0) FmtRate(rateM, rCapM, capM, encM);
    if (rateP && rCapP > 0) FmtRate(rateP, rCapP, capP, encP);

    /* v41/v42: same single rule as SpellTableFormatRowCols (current record's 4 counters) */
    if (g_maskUnknownUi && SpellRowUnknownEx(group, idx, capM, capP, encM, encP)) {
        if (name && nameCap > 0) lstrcpynA(name, "No.？ ？？？", nameCap);
        if (rateM && rCapM > 0) lstrcpynA(rateM, "--", rCapM);
        if (rateP && rCapP > 0) lstrcpynA(rateP, "--", rCapP);
    }
    return 1;
}
