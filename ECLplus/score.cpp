/*
 * score.cpp —— 符卡栏位 512 运行期扩容（DLL 内，不改 th17.exe 文件）
 *
 * 为什么需要：原版每条存档记录里只有 101 个符卡条目（记录 +0x8D0，0x9C 字节一条）。
 * 魔改后符卡号会超过 101，超出的数据会踩到记录里的其它字段。这里把「记录大小、符卡数组位置、
 * 尾块偏移、对象大小、saver 校验和长度、初始化循环的魔数除法」等 131 处常量在内存里改掉，
 * 让游戏使用 512 槽的新布局。补丁表见 score_table.h（由 tools/gen_loader_table.py 生成并交叉核对）。
 */
#include "pch.h"
#include "score.h"
#include "score_table.h"

#include <stdio.h>
#include <stdarg.h>

#define SCORE_TARGET_MODULE L"th17.exe"

static FILE* g_scoreLog = NULL;

/* ---------- 输出：控制台（UTF-16，不受代码页影响）+ 日志文件 ---------- */

static VOID ScoreLogOpen(VOID) {
    fopen_s(&g_scoreLog, "ECLplusSpellSlots.log", "a, ccs=UTF-8");
}

static VOID ScorePrint(CONST WCHAR* text) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != NULL && h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) {
            DWORD written = 0;
            WriteConsoleW(h, text, (DWORD)wcslen(text), &written, NULL);
        }
    }
    if (g_scoreLog != NULL) {
        fputws(text, g_scoreLog);
        fflush(g_scoreLog);
    }
}

static VOID ScorePrintf(CONST WCHAR* fmt, ...) {
    WCHAR buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    ScorePrint(buf);
}

/* ---------- 开关 ---------- */

static BOOL ScoreReadIni(CONST WCHAR* fileName, INT* out) {
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW((HMODULE)NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return FALSE;
    }
    WCHAR* slash = wcsrchr(path, L'\\');
    if (slash == NULL) {
        return FALSE;
    }
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), fileName);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return FALSE;
    }
    INT v = GetPrivateProfileIntW(L"ECLplus", L"SpellSlots", -1, path);
    if (v != 101 && v != 512) {
        return FALSE;
    }
    *out = v;
    return TRUE;
}

/* ---------- 打补丁 ---------- */

ScoreSlotResult InitSpellSlots(VOID) {
    ScoreSlotResult r;
    ZeroMemory(&r, sizeof(r));
    ScoreLogOpen();

    INT slots = ECLPLUS_DEFAULT_SPELL_SLOTS ? 512 : 101;
    r.source = L"编译期默认";
    INT ini = 0;
    if (ScoreReadIni(L"ECLplus.ini", &ini)) {
        slots = ini;
        r.source = L"ECLplus.ini";
    } else if (ScoreReadIni(L"ECLplusLoader.ini", &ini)) {
        slots = ini;
        r.source = L"ECLplusLoader.ini";
    }
    r.slots = slots;

    ScorePrintf(L"\n[ECLplus/score] 符卡栏位模式：%d 槽（开关来源：%ls）\n", slots, r.source);
    if (slots != 512) {
        ScorePrintf(L"[ECLplus/score] 原版 101 槽，未做任何内存修改。\n");
        r.ok = TRUE;
        return r;
    }

    /* 时序检查：ScoreFile 对象一旦创建，代码常量就不能再改（对象大小/布局已经按旧值分配） */
    {
        const DWORD SCOREFILE_PTR_VA = 0x004B77DC;   /* static ScoreFile* SCOREFILE_PTR */
        DWORD scoreFile = *(DWORD*)SCOREFILE_PTR_VA;
        BOOL force = FALSE;
        {
            WCHAR iniPath[MAX_PATH];
            DWORD n = GetModuleFileNameW((HMODULE)NULL, iniPath, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                WCHAR* slash = wcsrchr(iniPath, L'\\');
                if (slash) {
                    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - iniPath), L"ECLplus.ini");
                    if (GetFileAttributesW(iniPath) != INVALID_FILE_ATTRIBUTES) {
                        force = GetPrivateProfileIntW(L"ECLplus", L"Force", 0, iniPath) != 0;
                    }
                }
            }
        }
        if (scoreFile != 0 && !force) {
            ScorePrintf(L"[ECLplus/score] 检测到存档对象已经创建（SCOREFILE_PTR=0x%08X），说明注入太晚：\n", scoreFile);
            ScorePrintf(L"[ECLplus/score]   此时再改常量会让对象布局与代码不一致，所以本次不打补丁，游戏按 101 槽运行。\n");
            ScorePrintf(L"[ECLplus/score]   想强制打补丁可在 ECLplus.ini 里加 Force=1（不推荐）。\n");
            return r;
        }
        if (scoreFile != 0 && force) {
            ScorePrintf(L"[ECLplus/score] 警告：存档对象已存在，但 ini 里设了 Force=1，仍继续打补丁。\n");
        }
    }

    HMODULE module = GetModuleHandleW(SCORE_TARGET_MODULE);
    if (module == NULL) {
        module = GetModuleHandleW(NULL);
    }
    if (module == NULL) {
        ScorePrintf(L"[ECLplus/score] 拿不到主模块句柄，跳过扩容。\n");
        return r;
    }
    BYTE* base = (BYTE*)module;
    ScorePrintf(L"[ECLplus/score] 目标模块基址 0x%08X，共 %d 处补丁待检查。\n",
        (unsigned)(DWORD_PTR)base, ECL_SLOT512_PATCH_COUNT);

    /* 第一遍：全量校验，任何一处不符就整批放弃，绝不改到一半 */
    for (INT i = 0; i < ECL_SLOT512_PATCH_COUNT; i++) {
        DWORD cur = *(DWORD*)(base + kEclSlot512Patches[i].rva);
        if (cur == kEclSlot512Patches[i].oldValue) {
            r.matchedOld++;
        } else if (cur == kEclSlot512Patches[i].newValue) {
            r.alreadyNew++;
        } else {
            if (r.mismatched == 0) {
                r.firstMismatchRva = kEclSlot512Patches[i].rva;
            }
            r.mismatched++;
        }
    }

    if (r.mismatched > 0) {
        ScorePrintf(L"[ECLplus/score] 有 %d 处与预期不符（第一处 0x%08X），放弃打补丁，游戏按 101 槽运行。\n",
            r.mismatched, r.firstMismatchRva);
        ScorePrintf(L"[ECLplus/score]   —— 常见原因：游戏版本不是 1.00b，或已有别的补丁改过这些位置。\n");
        return r;
    }
    if (r.matchedOld == 0) {
        ScorePrintf(L"[ECLplus/score] %d 处已全部是新值，本进程已打过扩容补丁。\n", r.alreadyNew);
        r.ok = TRUE;
        return r;
    }
    if (r.alreadyNew > 0) {
        ScorePrintf(L"[ECLplus/score] 状态不一致（%d 处新值 / %d 处旧值），为避免损坏放弃打补丁。\n",
            r.alreadyNew, r.matchedOld);
        return r;
    }

    /* 第二遍：写入 */
    for (INT i = 0; i < ECL_SLOT512_PATCH_COUNT; i++) {
        DWORD* addr = (DWORD*)(base + kEclSlot512Patches[i].rva);
        DWORD oldProtect = 0;
        if (!VirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            ScorePrintf(L"[ECLplus/score] VirtualProtect 失败 @0x%08X（错误码 %lu）。\n",
                kEclSlot512Patches[i].rva, GetLastError());
            return r;
        }
        *addr = kEclSlot512Patches[i].newValue;
        DWORD tmp = 0;
        VirtualProtect(addr, 4, oldProtect, &tmp);
        r.written++;
    }

    r.ok = TRUE;
    ScorePrintf(L"[ECLplus/score] 扩容补丁已应用：%d/%d 处。记录大小 0x%X，符卡数组起点（记录内）0x%X，"
        L"尾块偏移 0x%X，对象大小 0x%X\n",
        r.written, ECL_SLOT512_PATCH_COUNT, ECL_SLOT512_RECORD_SIZE, ECL_SLOT512_ARRAY_BASE,
        ECL_SLOT512_TAIL_OFFSET, ECL_SLOT512_OBJECT_SIZE);
    ScorePrintf(L"[ECLplus/score] 注意：512 槽写出的 scoreth17.dat 是扩容格式，与原版不通用；"
        L"切回 101 槽前请先用存档工具把存档转回原版格式。\n");
    return r;
}
