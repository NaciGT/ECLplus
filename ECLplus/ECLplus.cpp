#include "pch.h"
#include "score.h"
#include "score_ui.h"
#include "score_draw.h"
#include "score_gui.h"
#include "ECLplus.h"
#include "music99.h"
#include "ins2000.h"
#include "ins2100.h"
#include "ins2200.h"
#include "ins2300.h"
#include "ins2400.h"
#include "intvar.h"
#include "binhack.h"

/* NOTE: must NOT be static! ins2400.cpp references it via "extern". */
IDirect3DDevice9* g_pDevice = nullptr;
static BOOL g_DeviceHooked = FALSE;

typedef HRESULT(STDMETHODCALLTYPE* EndScene_t)(IDirect3DDevice9*);
static EndScene_t OriginalEndScene = nullptr;

HRESULT STDMETHODCALLTYPE MyEndScene(IDirect3DDevice9* pDevice) {
    /* TSTF_GUARD ★ 保险：界面读游戏数据异常时绝不让游戏崩（跳过本帧 ✓）*/
    __try { TickSpellTableFrame(); } __except (EXCEPTION_EXECUTE_HANDLER) { static int once = 0; if (!once) { once = 1; EclPrint("[guard] TickSpellTableFrame 访问异常，已跳过本帧\n"); } }   /* 延迟安装钩子/看门狗/状态日志 */
    ScoreGuiFrame(pDevice);   /* ImGui 界面（v45r11 起就有；实测移除后自建表不显示） */
    /* Line drawing moved out; binhack.cpp handles rendering for now. */
    /* DrawAllLines2400(pDevice); */
    return OriginalEndScene(pDevice);
}

/* Reserved stub (old Stage4 hook is removed). */
void MyStage4Draw() {
}

typedef HRESULT(STDMETHODCALLTYPE* BeginScene_t)(IDirect3DDevice9*);
static BeginScene_t OriginalBeginScene = nullptr;
HRESULT STDMETHODCALLTYPE MyBeginScene(IDirect3DDevice9* pDevice) {
    { void ScoreGuiBeginScene(void*); ScoreGuiBeginScene(pDevice); }   /* ImGui 初始化（未进场景） */
    return OriginalBeginScene(pDevice);
}static void HookEndScene(IDirect3DDevice9* pDevice) {
    if (g_DeviceHooked || !pDevice) return;

    DWORD* vtable = *(DWORD**)pDevice;
    EndScene_t* pEndScene = (EndScene_t*)&vtable[42];
    OriginalEndScene = *pEndScene;

    DWORD oldProtect;
    VirtualProtect(pEndScene, sizeof(DWORD), PAGE_READWRITE, &oldProtect);
    *pEndScene = MyEndScene;
    VirtualProtect(pEndScene, sizeof(DWORD), oldProtect, &oldProtect);

    g_DeviceHooked = TRUE;
    {   /* 同时挂钩 BeginScene：ImGui 的设备对象必须在未进场景时创建 */
        BeginScene_t* pBegin = (BeginScene_t*)&vtable[41];
        OriginalBeginScene = *pBegin;
        VirtualProtect(pBegin, sizeof(DWORD), PAGE_READWRITE, &oldProtect);
        *pBegin = MyBeginScene;
        VirtualProtect(pBegin, sizeof(DWORD), oldProtect, &oldProtect);
        EclPrint("BeginScene hook installed successfully!\n");
    }    EclPrint("EndScene hook installed successfully!\n");
}

static HRESULT(STDMETHODCALLTYPE* RealCreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = nullptr;

HRESULT STDMETHODCALLTYPE MyCreateDevice(IDirect3D9* pD3D, UINT Adapter, D3DDEVTYPE DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface)
{
    HRESULT hr = RealCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow,
        BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr)) {
        g_pDevice = *ppReturnedDeviceInterface;
        HookEndScene(g_pDevice);
        { void ScoreDrawPrepare(void); ScoreDrawPrepare(); }   /* v10：设备刚建好、还没进场景，这里建字体最安全 */
    }
    return hr;
}

static void HookD3D9(IDirect3D9* pD3D) {
    DWORD* vtable = *(DWORD**)pD3D;
    RealCreateDevice = (decltype(RealCreateDevice))vtable[16];

    DWORD oldProtect;
    VirtualProtect(&vtable[16], sizeof(DWORD), PAGE_READWRITE, &oldProtect);
    vtable[16] = (DWORD)MyCreateDevice;
    VirtualProtect(&vtable[16], sizeof(DWORD), oldProtect, &oldProtect);
}

typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT);
static Direct3DCreate9_t RealDirect3DCreate9 = nullptr;

static void HookD3D9ViaExisting() {
    HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
    if (!hD3D9) return;
    Direct3DCreate9_t pCreate = (Direct3DCreate9_t)GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pCreate) return;

    IDirect3D9* pD3D = pCreate(31);
    if (!pD3D) return;

    HookD3D9(pD3D);
    pD3D->Release();
}

/* Executes an extra ECL instruction, called by the modified game. */
VOID __stdcall InsSwitch(ENEMY* enm, INSTR* ins) {
#ifdef DEV
    CHAR buf[256];
#endif
    BOOL success;
    if (ins->id >= 2000 && ins->id < 2100) {
        success = ins_2000(enm, ins);
    }
    else if (ins->id >= 2100 && ins->id < 2200) {
        success = ins_2100(enm, ins);
    }
    else if (ins->id >= 2200 && ins->id < 2300) {
        success = ins_2200(enm, ins);
    }
    else if (ins->id >= 2300 && ins->id < 2400) {
        success = ins_2300(enm, ins);
    }
    else if (ins->id >= 2400 && ins->id < 2500) {
        success = ins_2400(enm, ins);
    }
#ifdef DEV
    else {
        success = TRUE;
        snprintf(buf, 256, "instruction out of range: %d", ins->id);
        EclMsg(buf);
    }
    if (!success) {
        snprintf(buf, 256, "bad instruction number: %d", ins->id);
        EclMsg(buf);
    }
#endif
}

DWORD __stdcall IntVarSwitch(ENEMY* enm, DWORD var, DWORD type) {
    if (type == 0)
        return IntVarGetVal(enm, var);
    else
        return (DWORD)IntVarGetAddr(enm, var);
}

LONG DECLSPEC_NOINLINE GetIntArg(ENEMY* enm, DWORD n) {
    /* Calling the __thiscall requires assembly */
    LONG res;
    __asm {
        mov ecx, enm
        push n
        mov eax, GameGetIntArg
        call eax
        mov res, eax
    }
    return res;
}

LONG DECLSPEC_NOINLINE GetIntArgEx(ENEMY* enm, DWORD n, DWORD val) {
    LONG res;
    _asm {
        mov ecx, enm
        push val
        push n
        mov eax, GameGetIntArgEx
        call eax
        mov res, eax
    }
    return res;
}

DECLSPEC_NOINLINE FLOAT GetFloatArg(ENEMY* enm, DWORD n) {
    FLOAT res;
    __asm {
        mov ecx, enm
        push n
        mov eax, GameGetFloatArg
        call eax
        movd res, xmm0
    }
    return res;
}

DECLSPEC_NOINLINE FLOAT GetFloatArgEx(ENEMY* enm, DWORD n, FLOAT val) {
    FLOAT res;
    __asm {
        mov ecx, enm
        push n
        movss xmm2, val
        mov eax, GameGetFloatArgEx
        call eax
        movd res, xmm0
    }
    return res;
}

const CHAR* GetStringArg(INSTR* ins, DWORD n) {
    return (const CHAR*)&(ins->data[n * 4 + 4]);
}

DECLSPEC_NOINLINE LONG* GetIntArgAddr(ENEMY* enm, DWORD n) {
    LONG* res;
    __asm {
        mov ecx, enm
        push n
        mov eax, GameGetIntArgAddr
        call eax
        mov res, eax
    }
    return res;
}

DECLSPEC_NOINLINE FLOAT* GetFloatArgAddr(ENEMY* enm, DWORD n) {
    FLOAT* res;
    __asm {
        mov ecx, enm
        push n
        mov eax, GameGetFloatArgAddr
        call eax
        mov res, eax
    }
    return res;
}

VOID InitConsole() {
    AllocConsole();
    EclPrint("ECLplus v0.4 by Priw8\n");
}

/* ===================================================================
 *  BOSS 位置标记（front.anm script 112，sprite81 的 "enemy" 贴图）
 *
 *  原版行为：整个舞台只有一个，而且只在舞台开局由 sub_42A400 创建一次
 *  （位置来自调用点残留的 xmm0，不跟随任何 BOSS）。
 *
 *  这里改成：每个已注册的 BOSS 槽（0/1，即 ins_512(n) 登记的第 n 号）
 *  各有一个独立 VM，每帧把 entity_pos 设成 { boss.x * 2, 896, 0 }：
 *    - x 跟随 BOSS 的 x
 *    - y 钉死在 playfield 底边（448 * 2，因为 front.anm 是 2 倍坐标空间）
 *  完全不占用血条的那 7 个 anm，所以不会少任何血条部件。
 * =================================================================== */

#define ADV_GUI_PTR          0x004B76AC     /* zGui**          */
#define ADV_ANM_MGR_PTR      0x00509A20     /* zAnmManager**   */
#define ADV_ENM_MGR_PTR      0x004B76A0     /* zEnemyManager** */
#define ADV_PLAYER_PTR       0x004B77D0     /* zPlayer**       */
#define ADV_PLAYER_POS_X     0x610          /* zPlayer + 0x610 = player.pos.x */
#define ADV_ENM_HP          0x3F74         /* zEnemyLife.current  */
#define ADV_ENM_HPMAX       0x3F78         /* zEnemyLife.maximum  */
#define ADV_GAME_THREAD_PTR 0x004B76B0     /* zGameThread** */
#define ADV_GT_FLAGS_OFF    0x8C           /* GameThread 标志字；bit 0x10 = 暂停中 */

#define ADV_F_ALLOCATE_VM    0x00476A60     /* AnmManager::allocate_vm()            -> VM* */
#define ADV_F_INIT_COPY      0x00407230     /* anm_init_copy_vm_from_loaded(front, vm, script) */
#define ADV_F_ANMVMRUN       0x00466DE0     /* AnmVm::run(vm)                              */
#define ADV_F_INSERT_WBACK   0x00475C80     /* insert_in_world_list_back(out, vm)          */
#define ADV_F_GET_VM_BY_ID   0x00476470     /* AnmManager::get_vm_with_id(id)              -> VM* */
#define ADV_F_STOP_VM        0x00476620     /* stop vm by anm id                           */
#define ADV_F_GET_BOSS       0x0041A5C0     /* EnemyManager::get_boss_enemy_full(slot)     -> ENEMYFULL* */

#define ADV_MARKER_SCRIPT    112            /* front.anm 的 "enemy" 标签脚本 */
#define ADV_FRONT_ANM_OFF    0x2D0          /* Gui + 0x2D0 = front.anm 的 AnmLoaded* */
#define ADV_ANM_REFCOUNT_OFF 0x134
#define ADV_VM_FLAGS_HI      0x534
#define ADV_VM_FLAGS_LO      0x530
#define ADV_VM_ENTITY_POS    0x5E4          /* entity_pos (zFloat3) */
#define ADV_VM_POS           0x2C           /* VM 自己的 pos (zFloat3) */
#define ADV_VM_CHILD_MODE    0x51C
#define ADV_VM_ID            0x538

/* 标记的纵向位置（front.anm 的 2 倍空间；playfield 底边 448 -> 896） */
#define ADV_MARKER_Y         896.0f
/* x 的缩放（同上，1 格 playfield = 2 格 front.anm） */
#define ADV_MARKER_XSCALE    2.0f
/* 支持几个 BOSS 槽（引擎的血条循环只认 0/1） */
#define ADV_MARKER_SLOTS     2

static DWORD g_markerVmId[ADV_MARKER_SLOTS] = { 0, 0 };
static void* g_markerVm[ADV_MARKER_SLOTS]   = { NULL, NULL };   /* 直接存 VM 指针，不走 get_vm_with_id */
static int   g_markerBuildLogged = 0;
static int   g_markerLogCount = 0;
static int   g_markerCfgDone = 0;
static int   g_markerEnable = 1;
static FLOAT g_markerY = 960.0f;      /* 实测：画布最底边 */
static FLOAT g_markerXScale = 2.0f;
static FLOAT g_markerScale = 1.0f;
static FLOAT g_markerXOff = 449.0f;    /* 实测校准值（引擎自身公式是 448） */
static FLOAT g_markerYOff = 0.0f;
static FLOAT g_markerYScale = 0.0f;      /* y 也跟随 BOSS（0=不跟，固定） */
static int   g_markerPosMode = 0;        /* 0=写 entity_pos 1=写 vm->pos 2=两边都写 */
static int   g_markerLogOn = 0;        /* 默认关：需要排查时 ini 里 BossMarkerLog=1 */
static int   g_markerAlpha = 1;          /* 复刻原版的透明度规则 */
static FLOAT g_markerAlphaNear = 64.0f;  /* 距离阈值 */
static int   g_markerBlink = 1;          /* 复刻原版的血量闪烁 */
static int   g_markerBlinkRatio = 30;    /* 血量百分比低于此值就一直闪 */
static int   g_markerBlinkPeriod = 4;    /* 闪烁周期（帧） */
static int   g_markerBlinkDark = 2;                            /* 每周期里"暗"的帧数（原版脚本是亮16帧+黑2帧） */
static int   g_markerBlinkHold = 20;                           /* 受击后持续闪多少帧（修"击中不闪"） */
static DWORD g_markerLastDmgFrame[ADV_MARKER_SLOTS] = { 0, 0 };
static DWORD g_markerPauseMask = 0x10;                         /* dword_4B5CDC 的哪一位表示暂停；0=不做暂停冻结 */
static int   g_markerLastPaused[ADV_MARKER_SLOTS] = { -1, -1 };
static DWORD g_markerFrame = 0;
static LONG  g_markerLastHp[ADV_MARKER_SLOTS] = { -1, -1 };
static int   g_markerDespawnDelay = 30;                        /* BOSS 注销后延迟多少帧再清 */
static int   g_markerDespawnCount[ADV_MARKER_SLOTS] = { 0, 0 };
static LONG  g_markerLogHp[ADV_MARKER_SLOTS] = { -1, -1 };
static int   g_markerHpLogCount = 0;

/* 简易文件日志：写在游戏目录下的 ECLplus_marker.log（用 Win32 API，避开 CRT 的 C4996） */
static VOID AdvLog(CONST CHAR* fmt, ...)
{
    CHAR buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;

    HANDLE h = CreateFileA("ECLplus_marker.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

/* 读 ECLplus.ini（和 score_ui.cpp 里的 IniInt 一样的找法） */
static int AdvIniInt(CONST WCHAR* key, int def)
{
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW((HMODULE)NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return def;
    WCHAR* slash = wcsrchr(path, L'\\');
    if (!slash) return def;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"ECLplus.ini");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return def;
    return GetPrivateProfileIntW(L"ECLplus", key, def, path);
}

static VOID AdvMarkerLoadCfg()
{
    if (g_markerCfgDone) return;
    g_markerCfgDone = 1;
    g_markerEnable  = AdvIniInt(L"BossMarker", 1);
    g_markerY       = (FLOAT)AdvIniInt(L"BossMarkerY", 960);
    g_markerXScale  = (FLOAT)AdvIniInt(L"BossMarkerXScale100", 100) / 100.0f;
    g_markerScale   = (FLOAT)AdvIniInt(L"BossMarkerScale100", 100) / 100.0f;
    g_markerXOff    = (FLOAT)AdvIniInt(L"BossMarkerXOffset", 449);
    g_markerYOff    = (FLOAT)AdvIniInt(L"BossMarkerYOffset", 0);
    g_markerYScale  = (FLOAT)AdvIniInt(L"BossMarkerYScale100", 0) / 100.0f;
    g_markerPosMode = AdvIniInt(L"BossMarkerPosMode", 0);
    g_markerLogOn   = AdvIniInt(L"BossMarkerLog", 0);
    g_markerAlpha   = AdvIniInt(L"BossMarkerAlpha", 1);
    g_markerAlphaNear = (FLOAT)AdvIniInt(L"BossMarkerAlphaNear", 64);
    g_markerBlink       = AdvIniInt(L"BossMarkerBlink", 1);
    g_markerBlinkRatio  = AdvIniInt(L"BossMarkerBlinkRatio", 30);
    g_markerBlinkPeriod = AdvIniInt(L"BossMarkerBlinkPeriod", 4);
    g_markerDespawnDelay = AdvIniInt(L"BossMarkerDespawnDelay", 30);
    g_markerBlinkDark    = AdvIniInt(L"BossMarkerBlinkDark", 2);
    g_markerBlinkHold    = AdvIniInt(L"BossMarkerBlinkHold", 20);
    g_markerPauseMask    = (DWORD)AdvIniInt(L"BossMarkerPauseBit", 16);
    AdvLog("[cfg] en=%d Y=%.1f Xs=%.2f Ys=%.2f S=%.2f Xo=%.1f Yo=%.1f PosMode=%d\n",
           g_markerEnable, g_markerY, g_markerXScale, g_markerYScale,
           g_markerScale, g_markerXOff, g_markerYOff, g_markerPosMode);
}

/* 把算好的坐标写到 VM。PosMode 决定写 entity_pos(+0x5E4) 还是 VM 自己的 pos(+0x2C)，
 * 因为 script112 的 layer 29 用哪套空间还没定论，两个字段都开放给 ini 调。 */
static VOID AdvMarkerSetPos(void* vm, ENEMY* boss)
{
    FLOAT px = boss->pos.x * g_markerXScale + g_markerXOff;
    FLOAT py = boss->pos.y * g_markerYScale + g_markerY + g_markerYOff;

    FLOAT* ep = (FLOAT*)((char*)vm + ADV_VM_ENTITY_POS);
    FLOAT* vp = (FLOAT*)((char*)vm + ADV_VM_POS);

    BOOL toEntity = (g_markerPosMode == 0) || (g_markerPosMode == 2);
    BOOL toVmPos  = (g_markerPosMode == 1) || (g_markerPosMode == 2);

    ep[0] = toEntity ? px : 0.0f;
    ep[1] = toEntity ? py : 0.0f;
    ep[2] = 0.0f;

    vp[0] = toVmPos ? px : 0.0f;
    vp[1] = toVmPos ? py : 0.0f;
    vp[2] = 0.0f;
}

/* 复刻原版 label 的透明度规则（Gui::on_tick 0x42C587..0x42C622）：
 *     dx = |boss.x - player.x|
 *     dx >= 64  -> alpha = 0xFF
 *     dx <  64  -> alpha = (int)(dx * 191 / 64) + 0x40      （0x40..0xFF）
 *     |boss.x| > 192 -> alpha = 0                            （走出执行域左右边界即隐藏）
 * vm+0x523 = color_1 的 alpha 字节。 */
static BYTE AdvMarkerCalcAlpha(ENEMY* boss)
{
    if (!g_markerAlpha) return 0xFF;
    void* player = *(void**)ADV_PLAYER_PTR;
    if (player == NULL) return 0xFF;

    FLOAT playerX = *(FLOAT*)((char*)player + ADV_PLAYER_POS_X);
    FLOAT dx = boss->pos.x - playerX;
    if (dx < 0.0f) dx = -dx;

    BYTE a;
    const FLOAT thr = (g_markerAlphaNear > 1.0f) ? g_markerAlphaNear : 64.0f;
    if (dx >= thr) {
        a = 0xFF;
    } else {
        a = (BYTE)((int)(dx * 191.0f / thr) + 0x40);
    }
    if (boss->pos.x < -192.0f || boss->pos.x > 192.0f)
        a = 0;

    return a;
}

/* ---- 绘制阶段的可见性写入（由 ins2400.cpp 的 Flush*Lines2400 每帧调用）----
 * 为什么必须放在绘制阶段：script112 的脚本每帧都会重写 flags/颜色，
 * 放在 MainLoop（逻辑阶段）写的值会在绘制前被它覆盖掉 —— 这正是
 * "改了 flags_hi / flags_lo / alpha 画面全都没反应" 的原因。 */
static BOOL g_markerBlinkState[ADV_MARKER_SLOTS] = { FALSE, FALSE };
static BOOL g_markerPausedState[ADV_MARKER_SLOTS] = { FALSE, FALSE };
static DWORD g_markerPhase[ADV_MARKER_SLOTS] = { 0, 0 };
static DWORD g_markerClock[ADV_MARKER_SLOTS] = { 0xFFFFFFFF, 0xFFFFFFFF };
static BYTE  g_markerAlphaVal[ADV_MARKER_SLOTS] = { 0xFF, 0xFF };

void AdvMarkerApply()
{
    for (DWORD slot = 0; slot < ADV_MARKER_SLOTS; ++slot) {
        void* vm = g_markerVm[slot];
        if (vm == NULL) continue;

        BOOL dark = g_markerBlinkState[slot] && !g_markerPausedState[slot];
        if (dark) {
            DWORD per = (g_markerBlinkPeriod > 0) ? (DWORD)g_markerBlinkPeriod : 4;
            DWORD dn  = (g_markerBlinkDark > 0) ? (DWORD)g_markerBlinkDark : 1;
            dark = ((g_markerPhase[slot] % per) < dn);
        } else {
            dark = FALSE;
        }

        if (dark) {
            *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI) |= 0x60u;    /* render_layer 的过滤位 */
            *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO) &= ~2u;
            *(BYTE*)((char*)vm + 0x523) = 0;                    /* alpha 归零 */
        } else {
            *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI) &= ~0x60u;
            *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO) |= 2u;
            *(BYTE*)((char*)vm + 0x523) = g_markerAlphaVal[slot];
        }
    }
}

/* 用和 sub_42A400 完全相同的方式创建一个 marker VM */
static VOID AdvMarkerSpawn(DWORD slot, ENEMY* boss)
{
    if (slot >= ADV_MARKER_SLOTS || boss == NULL) return;
    if (g_markerVmId[slot] != 0) return;

    void* gui = *(void**)ADV_GUI_PTR;
    if (gui == NULL) return;
    void* front = *(void**)((char*)gui + ADV_FRONT_ANM_OFF);
    if (front == NULL) return;

    /* front->ref_count++ */
    *(DWORD*)((char*)front + ADV_ANM_REFCOUNT_OFF) += 1;

    /* vm = allocate_vm() */
    void* vm = ((void* (__cdecl *)(void))ADV_F_ALLOCATE_VM)();
    if (vm == NULL) return;

    /* anm_init_copy_vm_from_loaded(front, vm, 112)  —— __thiscall */
    __asm {
        mov  ecx, front
        push ADV_MARKER_SCRIPT
        push vm
        mov  eax, ADV_F_INIT_COPY
        call eax
    }

    /* flags_hi |= 0x400（和原版一致） */
    *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI) |= 0x400;

    /* ★ flags_lo 的 bit1 (0x02) = 「可见」，不是「隐藏」！
     *   依据：Gui::on_tick 里 0x42C429 在"该槽 BOSS 存在"时做 or [vm+530h],2
     *   并递归 sub_476860；BOSS 不存在时走 0x42C624 做 and [vm+530h],~2
     *   并递归 sub_476890。所以新建的 VM 必须置上 bit1 才会被画出来。 */
    *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO) |= 2u;

    /* 坐标交给 AdvMarkerSetPos（受 PosMode / 偏移 控制） */
    AdvMarkerSetPos(vm, boss);
    g_markerAlphaVal[slot] = AdvMarkerCalcAlpha(boss);

    /* 缩放（0x50 = scale，zFloat2） */
    *(FLOAT*)((char*)vm + 0x50) = g_markerScale;
    *(FLOAT*)((char*)vm + 0x54) = g_markerScale;

    /* vm+0x40 = rotation.z，原版也清 0（script112 自己没有 ins_401，保险起见照做） */
    *(DWORD*)((char*)vm + 0x40) = 0;

    /* AnmVm::run(vm) */
    __asm {
        mov  ecx, vm
        mov  eax, ADV_F_ANMVMRUN
        call eax
    }

    *(DWORD*)((char*)vm + ADV_VM_CHILD_MODE) = 0;

    /* insert_in_world_list_back(&out, vm) */
    DWORD outId = 0;
    __asm {
        push vm
        lea  eax, outId
        push eax
        mov  ecx, dword ptr [ADV_ANM_MGR_PTR]
        mov  edx, ADV_F_INSERT_WBACK
        call edx
    }

    DWORD id = *(DWORD*)((char*)vm + ADV_VM_ID);
    if (id == 0) id = outId;
    g_markerVmId[slot] = id;
    g_markerVm[slot]   = vm;

    if (g_markerLogOn) AdvLog("[spawn] slot=%u vm=%p id=%u outId=%u\n", slot, vm, id, outId);

#ifdef DEV
    {
        CHAR buf[160];
        snprintf(buf, 160, "[marker] slot %u spawned, vmId=%u\n", slot, id);
        EclPrint(buf);
    }
#endif
}

/* 每帧：让每个 BOSS 的标记跟随它的 x；BOSS 没了就清掉标记 */
static VOID AdvMarkerTickImpl()
{
    AdvMarkerLoadCfg();

    /* 第一次调用时写一行构建标识，用来和旧 DLL 残留的日志区分 */
    if (!g_markerBuildLogged) {
        g_markerBuildLogged = 1;
        AdvLog("[build] %s %s v=marker3 rawptr=1\n", __DATE__, __TIME__);
    }

    if (!g_markerEnable) return;

    /* ★ 启动阶段这些全局指针还是 NULL：get_boss_enemy_full 内部会直接解引用
     *   ENEMY_MANAGER_PTR（mov esi, ds:4B76A0 / mov edx,[esi+eax*4+48h]），
     *   没有 NULL 检查，所以必须在这里先挡掉。 */
    if (*(void**)ADV_ANM_MGR_PTR == NULL) return;
    if (*(void**)ADV_ENM_MGR_PTR == NULL) return;
    if (*(void**)ADV_GUI_PTR     == NULL) return;

    ++g_markerFrame;

    for (DWORD slot = 0; slot < ADV_MARKER_SLOTS; ++slot) {
        ENEMYFULL* bossFull =
            ((ENEMYFULL* (__stdcall *)(DWORD))ADV_F_GET_BOSS)(slot);

        if (bossFull == NULL) {
            /* BOSS 不在了（ins_512 注销 / 换阶段）：
             * 延迟 BossMarkerDespawnDelay 帧再清，让标记在死亡演出期间还在，
             * 避免血条一消失标记就瞬断。延迟期内保持原样不动。 */
            if (g_markerVm[slot] != NULL) {
                int delay = (g_markerDespawnDelay > 0) ? g_markerDespawnDelay : 0;
                if (g_markerDespawnCount[slot] < delay) {
                    ++g_markerDespawnCount[slot];
                    if (g_markerLogOn && g_markerLogCount < 20 &&
                        g_markerDespawnCount[slot] == 1) {
                        ++g_markerLogCount;
                        AdvLog("[tick] slot=%u BOSS gone, holding marker for %d frames\n",
                               slot, delay);
                    }
                    continue;   /* 延迟期内：位置/可见性都不动 */
                }
                /* 延迟结束：清可见位并丢指针，等下次 ins_512 重新注册时重建 */
                *(DWORD*)((char*)g_markerVm[slot] + ADV_VM_FLAGS_LO) &= ~2u;
                g_markerVm[slot]   = NULL;
                g_markerVmId[slot] = 0;
                g_markerDespawnCount[slot] = 0;
                g_markerLastHp[slot] = -1;
                if (g_markerLogOn && g_markerLogCount < 20) {
                    ++g_markerLogCount;
                    AdvLog("[tick] slot=%u marker cleared (after %d frames)\n", slot, delay);
                }
            }
            continue;
        }
        g_markerDespawnCount[slot] = 0;   /* BOSS 回来了，重置延迟计数 */

        if (g_markerVm[slot] == NULL) {
            AdvMarkerSpawn(slot, &bossFull->enm);
            if (g_markerVm[slot] == NULL) {
                if (g_markerLogOn && g_markerLogCount < 20) {
                    ++g_markerLogCount;
                    AdvLog("[tick] slot=%u boss=%p but spawn FAILED\n", slot, bossFull);
                }
                continue;
            }
        }

        /* ★ 不再调 get_vm_with_id（那是上次抛异常的地方），直接用存下来的 VM 指针，
         *   并用 vm->id 自校验这块内存还是不是我们那个 VM。 */
        void* vm = g_markerVm[slot];
        DWORD nowId = *(DWORD*)((char*)vm + ADV_VM_ID);
        if (nowId != g_markerVmId[slot]) {
            if (g_markerLogOn && g_markerLogCount < 20) {
                ++g_markerLogCount;
                AdvLog("[tick] slot=%u VM 失效 (id %u -> %u)，丢弃重建\n",
                       slot, g_markerVmId[slot], nowId);
            }
            g_markerVm[slot]   = NULL;
            g_markerVmId[slot] = 0;
            continue;
        }

        /* 跟随 x（y 可选跟随），全部受 ini 控制 */
        AdvMarkerSetPos(vm, &bossFull->enm);
        /* 每帧也保持缩放（防止脚本或引擎改写） */
        *(FLOAT*)((char*)vm + 0x50) = g_markerScale;
        *(FLOAT*)((char*)vm + 0x54) = g_markerScale;

        /* 血量闪烁判定（对应原版第 2、3 条）：
         *   2) 血量正在下降（正在挨打）→ 闪
         *   3) 血量百分比低于 BossMarkerBlinkRatio → 一直闪
         */
        LONG hp = 0, hpMax = 0;
        BOOL blink = FALSE;
        BOOL dark = FALSE;
        if (g_markerBlink) {
            /* 硬偏移优先（不依赖 ECLplus 里 ENEMY 结构体的对齐） */
            hp    = *(LONG*)((char*)&bossFull->enm + ADV_ENM_HP);
            hpMax = *(LONG*)((char*)&bossFull->enm + ADV_ENM_HPMAX);
            if (hpMax > 0) {
                /* ② 受击闪烁：hp 下降只发生在"某一帧"，是边沿事件。
                 *    如果只让那一帧 blink=TRUE，再撞上暗帧判定就只剩 1 帧（16ms），肉眼看不见。
                 *    所以记下最后一次受伤的时刻，之后持续 hold 帧都闪 —— 这样"正在攻击时一直闪"。 */
                if (g_markerLastHp[slot] >= 0 && hp < g_markerLastHp[slot])
                    g_markerLastDmgFrame[slot] = g_markerFrame;

                DWORD hold = (g_markerBlinkHold > 0) ? (DWORD)g_markerBlinkHold : 1;
                if (g_markerFrame - g_markerLastDmgFrame[slot] < hold)
                    blink = TRUE;

                /* ③ 低血量：持续闪 */
                if ((LONGLONG)hp * 100 / (LONGLONG)hpMax < (LONGLONG)g_markerBlinkRatio)
                    blink = TRUE;
            }
        }

        /* ★ 闪烁：在 tick 里直接写（低血量闪烁一直有效，证明这个时机是生效的）。
         *   时钟用 VM 自己的脚本时间 vm+0x540：暂停时它不前进 → 暂停时不闪。 */
        {
            /* 暂停判定：dword_4B5CDC 的 bit 0x10。
             * 依据：sub_4447E0 同时读 PAUSE_MENU_PTR 并 test 这一位；音频线程、
             *       GuiMsgVm::run、LoadingThread 也都用同一位判断是否暂停。 */
            void* gt = *(void**)ADV_GAME_THREAD_PTR;
            DWORD gameFlags = (gt != NULL) ? *(DWORD*)((char*)gt + ADV_GT_FLAGS_OFF) : 0;
            BOOL paused = (g_markerPauseMask != 0) && ((gameFlags & g_markerPauseMask) != 0);

            /* 闪烁相位用帧计数（这是低血闪烁一直有效的那个时钟）；
             * 暂停时直接不闪，避免暂停画面里还在闪。 */
            DWORD per = (g_markerBlinkPeriod > 0) ? (DWORD)g_markerBlinkPeriod : 4;
            DWORD dn  = (g_markerBlinkDark > 0) ? (DWORD)g_markerBlinkDark : 1;
            if (g_markerLogOn && g_markerLastPaused[slot] != (int)paused) {
                g_markerLastPaused[slot] = (int)paused;
                AdvLog("[pause] slot=%u paused=%d gtFlags=%08X mask=%08X blink=%d hidden=%d\n",
                       slot, (int)paused, gameFlags, g_markerPauseMask, (int)blink,
                       (int)((*(DWORD*)((char*)vm + ADV_VM_FLAGS_HI) & 0x60u) != 0));
            }
            dark = blink && !paused && ((g_markerFrame % per) < dn);

            if (dark) {
                *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI) |= 0x60u;
                *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO) &= ~2u;
                *(BYTE*)((char*)vm + 0x523) = 0;
            } else {
                *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI) &= ~0x60u;
                *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO) |= 2u;
                *(BYTE*)((char*)vm + 0x523) = AdvMarkerCalcAlpha(&bossFull->enm);
            }
        }

        /* 位置与缩放每帧照写（这几个字段脚本不抢，实测有效） */
        AdvMarkerSetPos(vm, &bossFull->enm);
        *(FLOAT*)((char*)vm + 0x50) = g_markerScale;
        *(FLOAT*)((char*)vm + 0x54) = g_markerScale;

        /* 诊断日志：血量一变就记一行 */
        if (g_markerLogOn && g_markerHpLogCount < 40 && hp != g_markerLogHp[slot]) {
            ++g_markerHpLogCount;
            g_markerLogHp[slot] = hp;
            AdvLog("[hp] slot=%u hp=%ld hpMax=%ld pct=%ld blink=%d dark=%d lastHp=%ld flagsLo=%08X"
                   " alpha=%d flagsHi=%08X lastDmg=%u\n",
                   slot, hp, hpMax,
                   (hpMax > 0) ? (LONG)((LONGLONG)hp * 100 / (LONGLONG)hpMax) : -1,
                   (int)blink, (int)dark, g_markerLastHp[slot],
                   *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO),
                   (int)*(BYTE*)((char*)vm + 0x523),
                   *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI),
                   g_markerLastDmgFrame[slot]);
        }
        g_markerLastHp[slot] = hp;


        /* （alpha 已在上面按 dark/亮帧处理，这里不再重复写） */

        if (g_markerLogOn && g_markerLogCount < 12) {
            ++g_markerLogCount;
            AdvLog("[tick] slot=%u bossPos=(%.1f,%.1f)\n"
                   "       myPos=(%.1f,%.1f,%.1f) layer=%d sprite=%d script=%d scale=(%.2f,%.2f)\n"
                   "       flagsLo=%08X flagsHi=%08X next=%08X\n",
                   slot,
                   bossFull->enm.pos.x, bossFull->enm.pos.y,
                   *(FLOAT*)((char*)vm + ADV_VM_ENTITY_POS + 0),
                   *(FLOAT*)((char*)vm + ADV_VM_ENTITY_POS + 4),
                   *(FLOAT*)((char*)vm + ADV_VM_ENTITY_POS + 8),
                   *(int*)((char*)vm + 0x18),
                   *(int*)((char*)vm + 0x20),
                   *(int*)((char*)vm + 0x24),
                   *(FLOAT*)((char*)vm + 0x50),
                   *(FLOAT*)((char*)vm + 0x54),
                   *(DWORD*)((char*)vm + ADV_VM_FLAGS_LO),
                   *(DWORD*)((char*)vm + ADV_VM_FLAGS_HI),
                   *(DWORD*)((char*)vm + 0x56C));
        }
    }
}

/* 外层守卫：任何越界/空指针都不许把游戏带走（和 MyEndScene 里同样的做法） */
static VOID AdvMarkerTick()
{
    __try {
        AdvMarkerTickImpl();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int once = 0;
        if (!once) { once = 1; AdvLog("[guard] AdvMarkerTick 抛异常，后续帧继续尝试\n"); }
    }
}

VOID __stdcall MainLoop() {
    /* Called every frame. */
    TickSpellSlotsUI();     /* 符卡列表劫持：第一帧执行初始化（见 score_ui.cpp） */
    AdvMarkerTick();        /* BOSS 位置标记：每个已注册 BOSS 一个，跟随其 x */
}
VOID init() {
#ifdef DEV
    InitConsole();
#endif
    InitSpellSlots();       /* 符卡栏位 512 运行期扩容（见 score.cpp，开关在 ECLplus.ini） */
    th17_music99::InitMusicRoom99();   /* 音乐室 99：Menu 构造门 + 仅容量类补丁（解锁检查保持原版 ✓） */
    InitSpellSlotsUI();     /* 战绩页符卡列表改成自建表（见 score_ui.cpp + spell_table.csv） */
    InitBinhacks();
    HookD3D9ViaExisting();
}