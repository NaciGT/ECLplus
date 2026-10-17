/*
 * score_ui.h —— 战绩页符卡列表：用自建表（spell_table.csv）替换显示内容
 * 详见 score_ui.cpp 顶部的逆向结论。
 */
#pragma once

#include "pch.h"

/* 在 init() 里调用（建议放在 InitSpellSlots() 之后，保证记录大小/数组起点已经确定） */
void InitSpellSlotsUI(void);

/* MainLoop()（每帧）里调用：第一帧才真正读表/装钩子 */
void TickSpellSlotsUI(void);

/* 每帧回调：从 ECLplus 的 D3D9 EndScene 钩子里调用（延迟安装代码钩子 + 看门狗 + 状态日志） */
void TickSpellTableFrame(void);

/* v10 自绘覆盖层接口 */
void UiLogSafe(const char* fmt, ...);
int  SpellTableDrawEnabled(void);
int  SpellTableIsActivePage(void);
void SpellTableDrawParams(int* rowX, int* rowY0, int* rowDY, int* bgX0, int* bgY0, int* bgX1, int* bgY1, int* fontH, char* face, int faceCap, int* charset, unsigned* color, unsigned* bgColor);
int  SpellTableFormatRow(int rowInPage, char* out, int cap, unsigned* colorOut);
void SpellTableSetDrawOff(void);

/* v11 ImGui 界面参数 */
void SpellTableGuiParams(int* on, int* x, int* y, int* w, int* h, float* fontSize, unsigned* text, char* file, int fileCap);
int  SpellTableGroupRows(int group);
int  SpellTableCurrentGroup(void);

int  SpellTableShouldShow(void);
void SpellTablePageInfo(int* sub, int* mode, int* diff, int* v1dc, int* rows);

int  SpellTableVmCount(void);

int  SpellTableFormatRowCols(int rowInPage, char* name, int nameCap, unsigned* capM, unsigned* encM, char* rateM, int rCapM, unsigned* capP, unsigned* encP, char* rateP, int rCapP, unsigned* colorOut);

int  SpellTableFormatRowAt(int group, int idx, char* name, int nameCap, unsigned* capM, unsigned* encM, char* rateM, int rCapM, unsigned* capP, unsigned* encP, char* rateP, int rCapP, unsigned* colorOut);
int  SpellTablePageRows(void);
int  SpellTableMaxGroup(void);

/* v52：当前页号（主表一直在用 [menu+0x1D4]；不在符卡数据页返回 -1）
   —— 从主表渲染逻辑里独立出来，专供"索引条"(score_index.cpp) 读取 */
int  SpellTableCurrentPage(void);

void SpellTableGroupLabel(int g, char* out, int cap);

int  SpellTableIniInt(const wchar_t* key, int def);

int  SpellTableAnyRecordMet(int group, int idx);

/* v41：打码的唯一判据（只看当前记录的四个収率数字；四个全 0 → 1） */
int  SpellRowUnknown(unsigned capM, unsigned capP, unsigned encM, unsigned encP);
int  SpellRowUnknownByIndex(int group, int row);
/* v42：带 GuiMaskAnyRecord 选项的版本（1=任一自机遇到过则不打码） */
int  SpellRowUnknownEx(int group, int idx, unsigned cm, unsigned cp, unsigned em, unsigned ep);
int  SpellTableAnyRecordMetSlot(int slot);
/* v44：把 10 条记录的四个计数累加（供"任一自机"口径的収率显示用） */
int  SpellTableAggCounters(int slot, unsigned* cm, unsigned* cp, unsigned* em, unsigned* ep);

int  SpellTableCollectChars(unsigned short* out, int cap);



