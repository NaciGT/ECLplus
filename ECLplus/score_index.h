#pragma once
/*
 * score_index.h —— 符卡表「索引条」（从主表绘制里摘出来的独立单元）
 *
 * 这段索引代码原本内嵌在 score_gui.cpp 的 DrawTable()（主表绘制函数）里
 * （分组按钮 + < > 翻页 + 「第 %d 页」），与主表的窗口/翻页状态缠在一起。
 * 现已摘出到本单元，只保留"显示索引"这一件事：
 *
 *   · 位置与 v9 主表对齐：X = SpellTableX，Y = SpellTableY0 - SpellTableDY
 *     （正好占表格首行上方一格，与表格无缝衔接）
 *   · 显隐复用主表的动画状态：只在 g_animPhase != 0 时画，并乘以 g_animT 淡出，
 *     所以"退出界面"时会和主表一起消失
 *   · 复用 ScoreGuiFrame() 每帧已经建好的 ImGui 上下文（不另起 ImGui）
 *   · 颜色/底透明度由 ini 独立控制（IndexBar* 系列）
 */
void IndexBarInit(void);                  /* 读 ini（在 InitSpellSlotsUI 末尾调用） */
void IndexBarDraw(int visible, float t);  /* 在 ScoreGuiFrame 里 DrawTable 之后调用 */
void IndexBarLoadFont(void* io);           /* 在 ImGui io.Fonts->Build() 之前调用（v55：本模块专用字体） */
