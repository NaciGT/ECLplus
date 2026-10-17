#include "pch.h"
#include "ins2300.h"
#include "ECLplus.h"
#include "intvar.h"          // INTVAR_CUSTOM_FIRST, IntVarGetAddr, SaveCustomSave, GenerateCustomSaveTexts

/* ============================================================
 * INS_2300 series
 *
 *   2300 setDifficulty(d)            —— 直接修改当前难度
 *   2301 setCustomVar(index, value)  —— 写 ECLplus 自带的永久变量 0..49
 *
 * 2301 的变量存在 ECLplus 自己的文件里（不是游戏存档），
 * 所以不会影响游戏自身的变量、成就与存档数据。
 * ============================================================ */

/* 当前难度的内存地址。
 *
 * th17.exe 的 VA = 0x4B5A00，也就是 ECL 整数全局 -9959。依据：
 *   Enemy__ecl_get_int_global  (0x4276D0) 的 -9959 分支 → mov eax, dword_4B5A00
 *   Enemy__ecl_get_float_global(0x428357) 的 -9959 分支 → fild    dword_4B5A00
 * 全 exe 里大量 `cmp dword_4B5A00, 1/2/3/4`，取值 0=Easy 1=Normal 2=Hard
 * 3=Lunatic 4=Extra。
 *
 * 注意用 GetModuleHandle(NULL) 取基址而不是写死 0x400000，这样在带 ASLR 的
 * 情况下也能算对（相对偏移固定为 0xB5A00）。 */
static int* GetDifficultyPtr() {
    static int* pDiff = nullptr;
    if (!pDiff) {
        DWORD base = (DWORD)GetModuleHandle(NULL);
        pDiff = (int*)(base + 0xB5A00);
    }
    return pDiff;
}

BOOL ins_2300(ENEMY* enm, INSTR* ins) {
    switch (ins->id) {

    case INS_SET_DIFFICULTY: {
        // 读取第一个整数参数作为新难度值
        int newDiff = GetIntArg(enm, 0);
        int* pDiff = GetDifficultyPtr();
        if (pDiff && newDiff >= 0 && newDiff <= 7) {
            *pDiff = newDiff;
        }
        break;
    }

    // ==============================
    // 2301 设置自定义永久变量
    // ==============================
    case INS_SET_CUSTOM_VAR: {
        int index = GetIntArg(enm, 0);   // 变量索引（0 ~ INTVAR_CUSTOM_COUNT-1）
        int value = GetIntArg(enm, 1);   // 要赋的值

        if (index < 0 || index >= INTVAR_CUSTOM_COUNT) {
            char msg[64];
            snprintf(msg, sizeof(msg), "ins_2301: index out of range (0~%d)\n",
                     INTVAR_CUSTOM_COUNT - 1);
            EclPrint(msg);
            break;
        }

        DWORD varID = INTVAR_CUSTOM_FIRST + index;
        DWORD* pVar = IntVarGetAddr(enm, varID);
        if (pVar) {
            *pVar = (DWORD)value;
            SaveCustomSave();            // 写入后立即保存
            GenerateCustomSaveTexts();   // 写入后立即刷新文本文件
        }
        break;
    }

    default:
        return FALSE;   // 未识别的指令
    }
    return TRUE;
}
