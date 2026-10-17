#pragma once
/* 随机截屏（v1）
   - 只在"不在菜单"时截（= 游玩中 ✓，避免标题/菜单/normal UI 截图 ✓）
   - 随机间隔（ini: SnapMinSec / SnapMaxSec，默认 20~60 秒 ✓）
   - 存到游戏目录的 snapshot\thow_XXX.bmp（BMP ✓，无需 D3DX ✓）
   - F7 手动立即截一张 ✓
   - 为保证截到的是【纯游戏画面】，必须在画 ImGui 界面【之前】调用 SnapTick ✓
   ini: SnapEnable=1 开关 ✓ */
void SnapInit(void);
void SnapTick(void* d3d9Device);   /* 每帧调用（在 UI 之前 ✓） */
void SnapNow(void);                /* 手动截一张（F7 ✓） */