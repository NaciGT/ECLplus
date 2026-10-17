/*
 * score_draw.h —— v10：自绘覆盖层（thprac 式）
 * 完全用 D3D9 设备自己画：不透明黑底 + d3dx9 字体文字。
 * 不依赖游戏的 AsciiManager / ANM / 字体，也不修改任何游戏代码（因此和 thcrap 零冲突）。
 */
#pragma once

#include "pch.h"

/* 从 ECLplus 的 D3D9 EndScene 钩子里每帧调用（传 IDirect3DDevice9*） */
void ScoreDrawFrame(void* device);

/* 设备丢失/重置时调一下（可选） */
void ScoreDrawReset(void);
