#include "pch.h"
#include <windows.h>
#include <d3d9.h>
#include "snap.h"
#include "score_ui.h"    /* UiLogSafe / SpellTableIniInt */

#define PTR_MAIN_MENU 0x004B77F0   /* ==0 → 不在菜单（= 游玩中 ✓） */
#define PTR_GAMEMODE  0x004B61D0
#define PTR_GAMEMODE_NEXT 0x004B61D4

/* 默认关闭：自动截图每 20~60 秒一张（1280x960 约 4.7MB/张），
   长时间游玩会占满磁盘。该功能留给后续内容，需要时在 ini 里
   设 SnapEnable=1 打开。手动 F7 不受此开关影响（且仅 DEV 构建有）。 */
static int   g_on = 0, g_min = 20, g_max = 60;
static DWORD g_next = 0;
static int   g_inited = 0;
static IDirect3DDevice9* g_dev = 0;

static unsigned NextRand(void) {
    static unsigned s = 0;
    if (!s) s = GetTickCount() | 1u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}
static void SnapDir(wchar_t* out, int n) {
    DWORD m = GetModuleFileNameW(NULL, out, (DWORD)n);
    if (!m || m >= (DWORD)n) { out[0] = 0; return; }
    wchar_t* s = wcsrchr(out, L'\\');
    if (!s) { out[0] = 0; return; }
    wcscpy_s(s + 1, (size_t)(n - (int)(s + 1 - out)), L"snapshot");
}
void SnapInit(void) {
    if (g_inited) return;
    g_inited = 1;
    g_on  = SpellTableIniInt(L"SnapEnable", 0);   /* 默认 0 = 不自动截图 */
    g_min = SpellTableIniInt(L"SnapMinSec", 20);
    g_max = SpellTableIniInt(L"SnapMaxSec", 60);
    if (g_min < 1) g_min = 20;
    if (g_max < g_min) g_max = g_min + 10;
    g_next = GetTickCount() + (DWORD)(g_min + (NextRand() % (unsigned)(g_max - g_min + 1))) * 1000u;
    UiLogSafe("[snap] init: on=%d interval=%d..%d sec", g_on, g_min, g_max);
}
/* BMP 写出（32bpp BI_RGB，自底向上 ✓，不依赖 D3DX ✓） */
static int SaveBMP(const wchar_t* path, int w, int h, const unsigned char* bits, int pitch) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    const unsigned datasize = (unsigned)w * 4u * (unsigned)h;
    BITMAPFILEHEADER fh; ZeroMemory(&fh, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + datasize;
    BITMAPINFOHEADER ih; ZeroMemory(&ih, sizeof(ih));
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1;
    ih.biBitCount = 32; ih.biCompression = BI_RGB; ih.biSizeImage = datasize;
    DWORD wr = 0; int ok = 1;
    ok &= WriteFile(f, &fh, sizeof(fh), &wr, NULL);
    ok &= WriteFile(f, &ih, sizeof(ih), &wr, NULL);
    for (int y = h - 1; y >= 0 && ok; y--)
        ok &= WriteFile(f, bits + (size_t)y * pitch, (DWORD)w * 4u, &wr, NULL);
    CloseHandle(f);
    return ok;
}
static int DoCapture(void) {
    if (!g_dev) return 0;
    IDirect3DSurface9* bb = NULL;
    IDirect3DSurface9* sys = NULL;
    int done = 0, index = 0;
    if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return 0;
    D3DSURFACE_DESC d; ZeroMemory(&d, sizeof(d));
    bb->GetDesc(&d);
    if (SUCCEEDED(g_dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, NULL)) && sys) {
        if (SUCCEEDED(g_dev->GetRenderTargetData(bb, sys))) {
            D3DLOCKED_RECT lr; ZeroMemory(&lr, sizeof(lr));
            if (SUCCEEDED(sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
                wchar_t dir[MAX_PATH] = { 0 }, path[MAX_PATH] = { 0 };
                SnapDir(dir, MAX_PATH);
                if (dir[0]) {
                    CreateDirectoryW(dir, NULL);
                    for (int i = 1; i <= 9999; i++) {
                        swprintf_s(path, MAX_PATH, L"%s\\thow_%03d.bmp", dir, i);
                        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { index = i; break; }
                    }
                    if (path[0]) {
                        done = SaveBMP(path, (int)d.Width, (int)d.Height, (const unsigned char*)lr.pBits, (int)lr.Pitch);
                        if (done) UiLogSafe("[snap] saved thow_%03d.bmp  %dx%d  gamemode=%u,%u", index, (int)d.Width, (int)d.Height,
                                            *(unsigned*)PTR_GAMEMODE, *(unsigned*)PTR_GAMEMODE_NEXT);
                    }
                }
                sys->UnlockRect();
            }
        }
        sys->Release();
    }
    bb->Release();
    return done;
}
void SnapNow(void) { if (DoCapture()) UiLogSafe("[snap] manual capture OK (F7)"); else UiLogSafe("[snap] manual capture FAILED"); }
void SnapTick(void* d3d9Device) {
    if (!g_inited) SnapInit();
    if (!d3d9Device) return;
    g_dev = (IDirect3DDevice9*)d3d9Device;
    if (!g_on) return;
    if (*(unsigned*)PTR_MAIN_MENU) return;      /* 在菜单 → 不截 ✓ */
    if (*(unsigned*)PTR_GAMEMODE == 0) return;  /* 未开始（标题/加载）→ 不截 ✓ */
    DWORD now = GetTickCount();
    if ((int)(now - g_next) < 0) return;
    g_next = now + (DWORD)(g_min + (NextRand() % (unsigned)(g_max - g_min + 1))) * 1000u;
    DoCapture();
}