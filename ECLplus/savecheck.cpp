#include "pch.h"
#include <windows.h>
#include <stdarg.h>
#include "savecheck.h"
#include "score_ui.h"
#include "ECLplus.h"     /* EclPrint ✓ */

static int g_done = 0, g_show = 1;
static char g_summary[384] = { 0 };

void EclPlusDiag(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    EclPrint("[ECLplus] ");      /* DEV 构建才会真正输出 ✓ */
    EclPrint(buf);
    EclPrint("\n");
    UiLogSafe("%s", buf);
}
static unsigned Fnv(const unsigned char* p, size_t n) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static void CheckOne(const wchar_t* file, const char* tag) {
    wchar_t path[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    char line[256];
    if (!n || n >= MAX_PATH) return;
    wcscat_s(path, MAX_PATH, L"\\ShanghaiAlice\\th17\\");
    wcscat_s(path, MAX_PATH, file);
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) {
        sprintf_s(line, "[SaveCheck] %s: 不存在（全新存档 ✓）", tag);
        EclPlusDiag("%s", line);
        if (!g_summary[0]) strcpy_s(g_summary, line);
        return;
    }
    static unsigned char buf[4096];
    size_t rd = fread(buf, 1, sizeof(buf), f);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fclose(f);
    if (rd < 24) { sprintf_s(line, "[SaveCheck] %s: 文件过小 (%ld 字节) ✗", tag, sz); EclPlusDiag("%s", line); return; }
    int magic = (buf[0] == 'T' && buf[1] == 'H' && buf[2] == '7' && buf[3] == '1');
    unsigned sizeField = *(unsigned*)(buf + 4);
    unsigned tail = *(unsigned*)(buf + 16), mark = *(unsigned*)(buf + 20);
    unsigned h = Fnv(buf, rd);
    if (!magic)                          sprintf_s(line, "[SaveCheck] %s ✗ 魔数异常 (%02X %02X %02X %02X)", tag, buf[0], buf[1], buf[2], buf[3]);
    else if (sizeField != (unsigned)sz)  sprintf_s(line, "[SaveCheck] %s ✗ 长度字段不符 (%u vs %ld)", tag, sizeField, sz);
    else if (mark < 0x000F0000u || mark > 0x00100000u) sprintf_s(line, "[SaveCheck] %s ✗ 布局标记异常 (%X)", tag, mark);
    else                                 sprintf_s(line, "[SaveCheck] %s ✓ 结构正常  %ld 字节  布局标记=%X", tag, sz, mark);
    EclPlusDiag("%s", line);
    EclPlusDiag("[SaveCheck] %s 尾部=%X 头哈希=%08X", tag, tail, h);
    if (!g_summary[0]) sprintf_s(g_summary, "%s (%ld 字节, hash=%08X)", tag, sz, h);
}
void SaveCheckRun(void) {
    EclPlusDiag("[SaveCheck] ==== 手动检查（热键）====");
    CheckOne(L"scorethow.dat", "scorethow");
    CheckOne(L"scoreth17.dat", "scoreth17");
    EclPlusDiag("[SaveCheck] ==== 结束（哈希变化 = 自上次以来存过档 ✓）====");
}
void SaveCheckInit(void) {
    if (g_done) return;
    g_done = 1;
    g_show = SpellTableIniInt(L"SaveCheckShow", 1);
    if (!g_show) return;
    EclPlusDiag("[SaveCheck] ==== 启动存档检查 ====");
    CheckOne(L"scorethow.dat", "scorethow");
    CheckOne(L"scoreth17.dat", "scoreth17");
    EclPlusDiag("[SaveCheck] ==== 结束（哈希变化 = 上一局存过档 ✓）====");
}
/* 冻结存档：把当前 scoreth*.dat 原样复制到 <游戏目录>\snapshot\savefreeze_NNN\ ✓
   用途：一旦怀疑存档坏了，立刻按 '' 键把当时那份留下 ✓（之后游戏怎么写都影响不到它 ✓） */
void SaveCheckFreeze(void) {
    wchar_t exe[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) { EclPlusDiag("[SaveFreeze] 取自身路径失败"); return; }
    wchar_t* s = wcsrchr(exe, L'\\');
    if (!s) return;
    *(s + 1) = 0;                                  /* exe 变成"游戏目录\" ✓ */
    wchar_t base[MAX_PATH] = { 0 };
    swprintf_s(base, MAX_PATH, L"%s%s", exe, L"snapshot");
    CreateDirectoryW(base, NULL);
    wchar_t dir[MAX_PATH] = { 0 };
    int idx = 0;
    for (int i = 1; i <= 999; i++) {
        swprintf_s(dir, MAX_PATH, L"%s\\savefreeze_%03d", base, i);
        if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) { idx = i; break; }
    }
    if (!idx) { EclPlusDiag("[SaveFreeze] 目录数已满(999)"); return; }
    CreateDirectoryW(dir, NULL);
    wchar_t appdata[MAX_PATH] = { 0 };
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) { EclPlusDiag("[SaveFreeze] 取 APPDATA 失败"); return; }
    const wchar_t* names[2] = { L"scorethow.dat", L"scoreth17.dat" };
    int copied = 0;
    for (int k = 0; k < 2; k++) {
        wchar_t from[MAX_PATH] = { 0 }, to[MAX_PATH] = { 0 };
        swprintf_s(from, MAX_PATH, L"%s\\ShanghaiAlice\\th17\\%s", appdata, names[k]);
        swprintf_s(to,   MAX_PATH, L"%s\\%s", dir, names[k]);
        if (CopyFileW(from, to, FALSE)) copied++;
    }
    EclPlusDiag("[SaveFreeze] 已冻结 %d 个文件 -> snapshot\\savefreeze_%03d\\ ✓", copied, idx);
}
/* ===== 内存结构检查（关键 ✓：文件是加密的 ✗，内存是明文 ✓）=====
   已知（来自我们自己的补丁表与之前的反汇编 ✓）：
     ScoreFile 指针 = [0x4B77DC] ✓
     slot 记录基址   = ScoreFile + slot * 0x18020（ECL_SLOT512_RECORD_SIZE ✓）
     记录内名字区    = 记录 + 0x8D8 + id * 0x9C ✓
     崩溃点用的块步长 = 0x241（写死在游戏代码里 ✓）
   本函数只做【只读转储 + 明显异常标注】，全部在 __try 内 ✓，不会影响游戏 ✓ */
static unsigned char* SF(void) { return *(unsigned char**)0x004B77DC; }
static void DumpHex(const char* tag, const unsigned char* p, int n) {
    char line[200];
    int o = sprintf_s(line, "[SaveCheck/MEM] %s:", tag);
    for (int i = 0; i < n && o < (int)sizeof(line) - 4; i++) o += sprintf_s(line + o, sizeof(line) - o, "%02X", p[i]);
    EclPlusDiag("%s", line);
}
void SaveCheckMem(void) {
    unsigned char* sf = SF();
    if (!sf) { EclPlusDiag("[SaveCheck/MEM] ScoreFile 未加载（还没读档 ✓）"); return; }
    EclPlusDiag("[SaveCheck/MEM] ScoreFile=%08X", (unsigned)sf);
    __try {
        DumpHex("SF+0x00", sf, 32);
        DumpHex("SF+0x20", sf + 0x20, 32);
        DumpHex("SF+0x4820", sf + 0x4820, 32);          /* 512 数组基址（我们自己表里的常量 ✓） */
        DumpHex("SF+0x8D8", sf + 0x8D8, 32);            /* 名字区起点 ✓ */
        /* 扫描名字区里的可见文本（SJIS ✓）—— 找那个可疑的 '/' ✓ */
        char txt[96]; int k = 0;
        for (int i = 0; i < 0x400 && k < 90; i++) {
            unsigned char ch = sf[0x8D8 + i];
            txt[k++] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
        }
        txt[k] = 0;
        EclPlusDiag("[SaveCheck/MEM] 名字区前 0x400: %s", txt);
        /* 记录头抽查（带保护 ✓ 记录可能不止 1 个 ✓） */
        for (int slot = 0; slot < 4; slot++) {
            unsigned char* r = sf + (size_t)slot * 0x18020;
            unsigned f0 = *(unsigned*)(r + 0x00), f4 = *(unsigned*)(r + 0x04);
            EclPlusDiag("[SaveCheck/MEM] rec%d @%08X  f0=%08X f4=%08X", slot, (unsigned)r, f0, f4);
        }
        /* ===== 扫描整个对象，找出非零区间（定位真正的角色/记录数据区 ✓）===== */
        {
            int i = 0, segs = 0;
            while (i < 0x8000 && segs < 14) {
                while (i < 0x8000 && sf[i] == 0) i++;
                if (i >= 0x8000) break;
                int s = i;
                while (i < 0x8000) {
                    if (sf[i] == 0 && sf[i+1] == 0 && sf[i+2] == 0 && (i - s) > 12) break;
                    i++;
                }
                int len = i - s;
                int printable = 0;
                for (int k = s; k < i; k++) if (sf[k] >= 0x20 && sf[k] < 0x7F) printable++;
                EclPlusDiag("[SaveCheck/MEM] seg%d @+0x%X len=%d 可打印=%d", segs, s, len, printable);
                DumpHex("   ", sf + s, 24);
                segs++;
            }
            EclPlusDiag("[SaveCheck/MEM] 扫描结束：共 %d 段（0x8000 范围内 ✓）", segs);
        }    } __except (EXCEPTION_EXECUTE_HANDLER) {
        EclPlusDiag("[SaveCheck/MEM] 读取时发生异常（结构严重异常 ✗ 或越界 ✓）");
    }
}