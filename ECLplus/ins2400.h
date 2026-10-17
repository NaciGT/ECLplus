#pragma once
#include <d3d9.h>
#include "ECLplus.h"    // 提供 ENEMY, INSTR 等类型

#define INS_LINE_SET      2400   // lineSet(color, alpha)
#define INS_LINE_ATTR     2401   // lineSetAttr(layer)
#define INS_LINE_DRAW     2402   // lineDraw(x1, y1, x2, y2)

#define MAX_LINE_VERTEX_PAIRS 256

struct LineDrawItem {
    float x1, y1, x2, y2;
    DWORD color;
    float layer;
};

extern LineDrawItem g_lineBuffer[MAX_LINE_VERTEX_PAIRS];
extern int g_lineCount;

extern DWORD g_lineColor;
extern float g_lineLayer;

BOOL ins_2400(ENEMY* enm, INSTR* ins);
void DrawAllLines2400(IDirect3DDevice9* pDevice);
void ResetLineBuffer(); 
void Stage4Diag2400();
/* --- rendering integration (final hooks) --- */
void TopMarker2400();
void FlushBackLines2400();
void FlushFrontLines2400();