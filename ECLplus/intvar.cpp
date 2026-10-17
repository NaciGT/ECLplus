/*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce this list of conditions and the following disclaimer
*    in the documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
* AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
* OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
* OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "pch.h"
#include "intvar.h"
#include "ins2100.h"
#include <cstdio>
#include <cstring>

#define DEBUG_VAR_DUMP 0   // 1 = 开启变量值文本生成，0 = 发布时关闭

/* Extra global integer variables */
static DWORD GI[4] = { 0, 0, 0, 0 };

/* ----- 自定义永久变量存储（50个） ----- */
static DWORD CustomVars[INTVAR_CUSTOM_COUNT] = { 0 };

/* ----- 输出目录：exe 所在目录向上 3 级（loader 所在位置） -----
 *
 * 注意这里有两套位置，是**故意分开**的，不是 bug：
 *
 *   1) thow.dat —— ECLplus 自己的 50 个永久变量存储（二进制，DWORD[50]）。
 *      用相对路径 "thow.dat" 打开，也就是跟着「当前工作目录」走（通常就是
 *      启动 loader 时所在的那个目录）。这样从不同位置启动也能读写同一份。
 *
 *   2) 生成的 .txt —— 给人看的文本（Last Word 描述之类），用下面的绝对路径，
 *      写到「th17.exe 目录向上 GAME_DIR_UP_LEVELS 级」，方便在游戏外层找到。
 *
 * 两者互不相干：删掉 .txt 不影响变量；删掉 thow.dat 只会让变量清零。
 *
 * 想让 .txt 换个位置，改 GAME_DIR_UP_LEVELS 即可（0 = 就在 exe 同目录，
 * 和 ECLplus.ini / spell_table.csv 放一起）。
 */
#define GAME_DIR_UP_LEVELS 3

// 获取 exe 目录向上 levelsUp 级后的目录（末尾带反斜杠）
static bool GetGameDirUpW(int levelsUp, wchar_t* out, size_t cch) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;

    wchar_t* p = wcsrchr(path, L'\\');
    if (!p) return false;
    *(p + 1) = L'\0';                          // 去掉文件名，得到 exe 目录

    for (int i = 0; i < levelsUp; i++) {
        size_t n = wcslen(path);
        if (n <= 1) return false;              // 已到根目录
        if (path[n - 1] == L'\\') { path[n - 1] = L'\0'; n--; }
        wchar_t* q = wcsrchr(path, L'\\');
        if (!q) return false;
        *(q + 1) = L'\0';
    }

    wcscpy_s(out, cch, path);
    return true;
}

void LoadCustomSave() {
    wchar_t path[MAX_PATH];
    if (!GetGameDirUpW(GAME_DIR_UP_LEVELS, path, MAX_PATH)) {
        memset(CustomVars, 0, sizeof(CustomVars));
        return;
    }
    wcscat_s(path, L"thow.dat");

    FILE* f = nullptr;
    errno_t err = fopen_s(&f, "thow.dat", "rb");
    if (err == 0 && f) {
        size_t read = fread(CustomVars, sizeof(DWORD), INTVAR_CUSTOM_COUNT, f);
        if (read != INTVAR_CUSTOM_COUNT) {
            memset(CustomVars + read, 0, (INTVAR_CUSTOM_COUNT - read) * sizeof(DWORD));
            SaveCustomSave();   // 立即扩展保存为新格式
        }
        fclose(f);
    }
    else {
        memset(CustomVars, 0, sizeof(CustomVars));
    }
}

void SaveCustomSave() {
    wchar_t path[MAX_PATH];
    if (!GetGameDirUpW(GAME_DIR_UP_LEVELS, path, MAX_PATH)) return;
    wcscat_s(path, L"thow.dat");

    FILE* f = nullptr;
    errno_t err = fopen_s(&f, "thow.dat", "wb");
    if (err == 0 && f) {
        fwrite(CustomVars, sizeof(DWORD), INTVAR_CUSTOM_COUNT, f);
        fclose(f);
    }
}

/* ----- 获取变量值 ----- */
DWORD IntVarGetVal(ENEMY* enm, DWORD var) {
    switch (var) {
    case INTVAR_INPUT:
        return *INTVAR_INPUT_LOC;
    case INTVAR_SCORE:
        return *INTVAR_SCORE_LOC;
    case INTVAR_HISCORE:
        return *INTVAR_HISCORE_LOC;
    case INTVAR_BOMBING:
        return INTVAR_BOMBING_VAL;
    case INTVAR_LIVES:
        return PlayerLives;
    case INTVAR_BOMBS:
        return PlayerBombs;
    case INTVAR_GRAZE:
        return *INTVAR_GRAZE_LOC;
    case INTVAR_PIV:
        return *INTVAR_PIV_LOC;
    case INTVAR_CONTINUES:
        return *INTVAR_CONTINUES_LOC;
    case INTVAR_CREDITS:
        return *INTVAR_CREDITS_LOC;
    case INTVAR_GI4:
    case INTVAR_GI5:
    case INTVAR_GI6:
    case INTVAR_GI7:
        return GI[var - INTVAR_GI4];
    case INTVAR_IFRAMES:
        return GetDwordField(Player, PlayerIframeIntField);
    case INTVAR_PLAYER_STATE:
        return GetDwordField(Player, PlayerStateField);
    case INTVAR_HYPERTIMER:
        return *INTVAR_HYPERTIMER_LOC;
    case INTVAR_ISDIALOG:
        return *INTVAR_ISDIALOG_LOC;
    case INTVAR_SPELLBONUS:
        return GameSpell ? GameSpell->bonus : 0;
    default:
        if (var >= INTVAR_CUSTOM_FIRST && var <= INTVAR_CUSTOM_LAST) {
            return CustomVars[var - INTVAR_CUSTOM_FIRST];
        }
        return 0;
    }
}

static DWORD invalidAddr; /* Return a pointer to a valid variable to avoid crashing the game. */

/* ----- 获取变量地址（用于写入） ----- */
DWORD* IntVarGetAddr(ENEMY* enm, DWORD var) {
    switch (var) {
    case INTVAR_INPUT:
        return INTVAR_INPUT_LOC;
    case INTVAR_SCORE:
        return INTVAR_SCORE_LOC;
    case INTVAR_HISCORE:
        return INTVAR_HISCORE_LOC;
    case INTVAR_BOMBING:
        EclPrint("Warning: variable BOMBING is read-only\n");
        return &invalidAddr;
    case INTVAR_LIVES:
    case INTVAR_BOMBS:
        EclPrint(
            "Warning: LIVES/BOMBS variables are read-only, due to issues with writing them directly. "
            "Use playerSetLives/playerSetBombs instructions instead.\n"
        );
        return &invalidAddr;
    case INTVAR_GRAZE:
        return INTVAR_GRAZE_LOC;
    case INTVAR_PIV:
        return INTVAR_PIV_LOC;
    case INTVAR_CONTINUES:
        return INTVAR_CONTINUES_LOC;
    case INTVAR_CREDITS:
        return INTVAR_CREDITS_LOC;
    case INTVAR_GI4:
    case INTVAR_GI5:
    case INTVAR_GI6:
    case INTVAR_GI7:
        return &GI[var - INTVAR_GI4];
    case INTVAR_IFRAMES:
        return &invalidAddr;
    case INTVAR_PLAYER_STATE:
        return &GetDwordField(Player, PlayerStateField);
    case INTVAR_HYPERTIMER:
    case INTVAR_ISDIALOG:
    case INTVAR_SPELLBONUS:
        return &invalidAddr;
    default:
        if (var >= INTVAR_CUSTOM_FIRST && var <= INTVAR_CUSTOM_LAST) {
            return &CustomVars[var - INTVAR_CUSTOM_FIRST];
        }
        return &invalidAddr;
    }
}

/* ============================================================================
 * Text export: custom vars -> text file (pure ASCII source, UTF-8 output)
 * ========================================================================== */

 /* ============================================================================
  * 自定义变量 -> 文本导出（输出 UTF-8 带 BOM）
  * ========================================================================== */

struct CustomTextEntry {
    int            index;         // 变量索引 0~29，对应特殊变量 -7000 + index
    DWORD          triggerValue;  // 变量"等于此值"时写入文本
    const wchar_t* text;        // 解锁后显示的文本
    const wchar_t* lockedText;  // 未解锁时的提示文本（新增）
};

struct CustomTextGroup {
    const wchar_t* filename;   // 输出文件名
    const wchar_t* headerText; // 固定说明文字：文件一生成就写在最前面（不受变量限制）
    const CustomTextEntry* entries;    // 组内条目
    int                    count;      // 条目数量
};

// ---- 组 A：spellcard_lastword（变量 0 / 1，即 -7000 / -6999）----
static const CustomTextEntry s_entries_LastWord[] = {
    { 0, 1,
        L"○示例条目 No.1\r\n"
        L" \r\n"
        L"这里写「变量 0 等于 1」时要显示的正文。\r\n"
        L"可以多行，每行都以 \\r\\n 结尾（纯文本，不是 RTF）。\r\n"
        L"========================================================================================================\r\n",
        L"○示例条目 No.1（未解锁）\r\n"
        L" 提示：把变量 0 设成 1 就能解锁这段文字\r\n"
        L"========================================================================================================\r\n"
    },
    { 1, 1,
        L"○示例条目 No.2\r\n"
        L" \r\n"
        L"这里写「变量 1 等于 1」时要显示的正文。\r\n"
        L"========================================================================================================\r\n",
        L"○示例条目 No.2（未解锁）\r\n"
        L" 提示：把变量 1 设成 1 就能解锁这段文字\r\n"
        L"========================================================================================================\r\n"
    },
};

// ---- 全部组汇总（要加新组，照这个格式在这里加）----
static const CustomTextGroup s_textGroups[] = {
    {
        L"example_text.txt",      /* filename   */
        L"========================================================================================================\r\n"    /* headerText */
        L"○示例文本文件\r\n"
        L"\r\n"
        L"○这里是对符卡的额外描述（示例）\r\n"
        L"  当任意一条示例条目被解锁时生成这个文件\r\n"
        L"  正文内容由 intvar.cpp 里 s_entries_LastWord[] 的条目决定\r\n"
        L"\r\n"
        L"○（在此写你的署名 / 日期）\r\n"
        L"\r\n",
        s_entries_LastWord,             /* entries    */
        2,                              /* count      */
    },
};
static const int s_textGroupCount =
(int)(sizeof(s_textGroups) / sizeof(s_textGroups[0]));

// 把宽字符文本转成 UTF-8 并写入文件（去掉结尾 '\0'）
static void WriteUtf8Line(FILE* f, const wchar_t* text) {
    if (!f || !text) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return;
    char* buf = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, len, NULL, NULL);
    fwrite(buf, 1, len - 1, f);   // len 含结尾 '\0'，少写 1 字节
    delete[] buf;
}

#if DEBUG_VAR_DUMP
static void GenerateDebugVarsText() {
    wchar_t baseDir[MAX_PATH];
    if (!GetGameDirUpW(GAME_DIR_UP_LEVELS, baseDir, MAX_PATH)) return;
    wchar_t fullPath[MAX_PATH];
    wcscpy_s(fullPath, baseDir);
    wcscat_s(fullPath, L"debug_vars.txt");   // 输出到 loader 目录

    FILE* f = nullptr;
    if (_wfopen_s(&f, fullPath, L"wb") != 0 || !f) return;

    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    fwrite(bom, 1, 3, f);

    char line[128];
    for (int i = 0; i < INTVAR_CUSTOM_COUNT; i++) {
        int len = sprintf_s(line, "CUSTOM_%02d (ID -%d) = %u\n", i, 7000 - i, CustomVars[i]);
        fwrite(line, 1, len, f);
    }
    fclose(f);
}
#endif

void GenerateCustomSaveTexts() {
    for (int g = 0; g < s_textGroupCount; g++) {
        const CustomTextGroup& group = s_textGroups[g];

        // 1) 组内是否有任意变量命中其触发值
        bool anyActive = false;
        for (int i = 0; i < group.count; i++) {
            const CustomTextEntry& e = group.entries[i];
            if (CustomVars[e.index] == e.triggerValue) {
                anyActive = true;
                break;
            }
        }
        if (!anyActive) continue;   // 组内没命中 → 不生成文件

        // 2) 打开输出文件（覆盖写）：路径 = exe 目录上三级 + 文件名
        wchar_t baseDir[MAX_PATH];
        if (!GetGameDirUpW(GAME_DIR_UP_LEVELS, baseDir, MAX_PATH)) continue;

        wchar_t fullPath[MAX_PATH];
        wcscpy_s(fullPath, baseDir);
        wcscat_s(fullPath, group.filename);

        FILE* f = nullptr;
        errno_t err = _wfopen_s(&f, fullPath, L"wb");
        if (err != 0 || !f) continue;

        // 3) 写 UTF-8 BOM，保证记事本/Notepad++ 自动识别
        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        fwrite(bom, 1, 3, f);

        // 4) 先写固定说明文字（不受变量限制，只要文件生成就有）
        if (group.headerText) {
            WriteUtf8Line(f, group.headerText);
        }

        // 5) 再按索引顺序，把所有命中触发值的变量的文本依次写入（追加效果）
        for (int i = 0; i < group.count; i++) {
            const CustomTextEntry& e = group.entries[i];
            if (CustomVars[e.index] == e.triggerValue) {
                WriteUtf8Line(f, e.text);            // 已解锁 → 正文
            }
            else if (e.lockedText) {
                WriteUtf8Line(f, e.lockedText);      // 该条目单独写了提示 → 用它
            }
            else {
                WriteUtf8Line(f, L"？？？\r\n");     // 没写提示 → 默认 ???
            }
        }
        fclose(f);
    }
#if DEBUG_VAR_DUMP
    GenerateDebugVarsText();
#endif
}