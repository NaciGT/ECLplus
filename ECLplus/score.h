/*
 * score.cpp 的接口：符卡栏位（存档 scoreth17.dat 相关）运行期扩容。
 *
 * 单独成文件，方便和其它 DLL 编辑部分一起管理。
 * 开关（优先级从高到低）：
 *   1. ECLplus.ini        （与本 DLL 同目录）  [ECLplus]  SpellSlots=512    ; 512=开 101=关
 *   2. ECLplusLoader.ini  （与本 DLL 同目录）  同上，可和启动器共用一份配置
 *   3. 编译期默认 ECLPLUS_DEFAULT_SPELL_SLOTS（1=512，0=101）
 * 输出：控制台（DEV 版有控制台；用 WriteConsoleW，不受代码页影响）+ ECLplusSpellSlots.log
 */
#pragma once

#include "pch.h"

#ifndef ECLPLUS_DEFAULT_SPELL_SLOTS
#define ECLPLUS_DEFAULT_SPELL_SLOTS 1
#endif

struct ScoreSlotResult {
    int  slots;              /* 101 / 512 */
    int  matchedOld;         /* 读到旧值、需要打补丁的处数 */
    int  alreadyNew;         /* 已经是新值（本进程已打过） */
    int  mismatched;         /* 既非旧值也非新值 */
    int  written;            /* 实际写入处数 */
    DWORD firstMismatchRva;
    BOOL ok;
    const wchar_t* source;   /* 开关来自哪里 */
};

/* 在 init() 里调用：读开关 -> 需要就打扩容补丁 -> 打印/写日志 */
ScoreSlotResult InitSpellSlots(VOID);
