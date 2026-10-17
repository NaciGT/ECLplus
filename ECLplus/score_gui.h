#pragma once
#include "pch.h"

/* v11：ImGui 自绘界面（thprac 那套）
   · 从 ECLplus 的 EndScene 钩子里每帧调用
   · 初始化推迟到"菜单对象出现 + 若干帧之后"，开局阶段完全不碰设备
   · 全程 SEH，异常自动关闭 */
void ScoreGuiInit(void);
void ScoreGuiFrame(void* device);
