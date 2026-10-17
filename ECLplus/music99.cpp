#include "pch.h"
#include "music99.h"
// TH17 1.00b Music Room 99, x86 in-process eclplus integration.
// Call InstallMusicRoom99() during startup, before the first menu allocation.
// This source modifies process memory only; it does not write game files.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "music99_payload.h"
namespace th17_music99 {
struct Patch { uintptr_t address; unsigned count; bool branch; unsigned char before[16], after[16]; };
static const Patch patches[] = {
{0x450c51,4,false,{0x18,0x5e,0x00,0x00},{0x1c,0x5e,0x01,0x00}},
{0x451218,4,false,{0x18,0x5e,0x00,0x00},{0x1c,0x5e,0x01,0x00}},
{0x451231,4,false,{0x18,0x5e,0x00,0x00},{0x1c,0x5e,0x01,0x00}},
{0x4515a9,4,false,{0x18,0x5e,0x00,0x00},{0x1c,0x5e,0x01,0x00}},
{0x45a3da,4,false,{0x10,0x10,0x00,0x00},{0x64,0x78,0x00,0x00}},
{0x45a3e4,4,false,{0x50,0x18,0x00,0x00},{0xea,0x91,0x00,0x00}},
{0x45a3ea,4,false,{0x10,0x08,0x00,0x00},{0xa4,0x5f,0x00,0x00}},
{0x45a618,4,false,{0x08,0x08,0x00,0x00},{0x32,0x3c,0x00,0x00}},
{0x45a627,4,false,{0xe3,0x01,0x00,0x00},{0x86,0x17,0x00,0x00}},
{0x45a95d,4,false,{0x28,0x0c,0x00,0x00},{0xf5,0x48,0x00,0x00}},
{0x45aa28,4,false,{0x28,0x0c,0x00,0x00},{0xf5,0x48,0x00,0x00}},
{0x45ab3c,4,false,{0x8c,0x07,0x00,0x00},{0x18,0x5e,0x00,0x00}},
{0x45ae43,4,false,{0x8c,0x07,0x00,0x00},{0x18,0x5e,0x00,0x00}},
{0x45adb3,4,false,{0x10,0x08,0x00,0x00},{0xa4,0x5f,0x00,0x00}},
{0x45a3f2,5,true,{0x8a,0x08,0x80,0xf9,0x23},{0xe9,0x09,0x2c,0x0d,0x00}},
{0x45a674,6,true,{0x81,0xc2,0xaa,0x00,0x00,0x00},{0xe9,0x87,0x2a,0x0d,0x00,0x90}},
/* v2：解锁检查保持原版 ✗ 不替换 */ /* {0x45a715,8,true,{0x80,0xbc,0x01,0x6e,0xd1,0x02,0x00,0x00},{0xe9,0xe6,0x2a,0x0d,0x00,0x90,0x90,0x90}}, */
/* v2：解锁检查保持原版 ✗ 不替换 */ /* {0x45a921,8,true,{0x80,0xbc,0x0a,0x6e,0xd1,0x02,0x00,0x00},{0xe9,0xda,0x29,0x0d,0x00,0x90,0x90,0x90}}, */
/* v2：解锁检查保持原版 ✗ 不替换 */ /* {0x45a9ec,8,true,{0x80,0xbc,0x0a,0x6e,0xd1,0x02,0x00,0x00},{0xe9,0x0f,0x2a,0x0d,0x00,0x90,0x90,0x90}}, */
/* v2：解锁检查保持原版 ✗ 不替换 */ /* {0x45ad67,8,true,{0x80,0xbc,0x08,0x6e,0xd1,0x02,0x00,0x00},{0xe9,0x94,0x27,0x0d,0x00,0x90,0x90,0x90}}, */
{0x45a3bb,5,true,{0xe8,0x10,0x7d,0xfa,0xff},{0xe8,0x40,0x32,0x0d,0x00}},
{0x44082c,5,true,{0xe8,0x9f,0x18,0xfc,0xff},{0xe8,0xcf,0xd0,0x0e,0x00}},
{0x4744c0,5,true,{0xe8,0x0b,0xdc,0xf8,0xff},{0xe8,0x3b,0x97,0x0b,0x00}}
,
{0x45a597,9,true,{0x83,0xf9,0x0a,0x0f,0x8d,0xfe,0x02,0x00,0x00},{0xe9,0x64,0x39,0x0d,0x00,0x90,0x90,0x90,0x90}},
{0x45a895,9,true,{0x83,0xf9,0x0a,0x0f,0x8c,0x68,0x06,0x00,0x00},{0xe9,0x66,0x37,0x0d,0x00,0x90,0x90,0x90,0x90}}
};
struct Fixup { unsigned offset; int direction; };
static const Fixup fixups[] = {
{0x7,-1},
{0x11,-1},
{0x10c,-1},
{0x117,-1},
{0x20e,-1},
{0x21c,-1},
{0x30e,-1},
{0x31c,-1},
{0x40e,-1},
{0x41c,-1},
{0x50e,-1},
{0x51c,-1},
{0x60f,-1},
{0x620,-1},
{0x90d,1},
{0x914,-1},
{0x925,-1},
{0xc19,-1},
{0xc7c,-1},
{0xc94,1},
{0xcb0,-1}
,
{0xf0f,-1},
{0xf14,-1},
{0x100f,-1},
{0x1014,-1}
};
static bool installed = false;
// Failure returns a diagnostic; do not continue with a partially installed patch.
const char* InstallMusicRoom99() {
    if (installed) return nullptr;
    if (sizeof(void*) != 4) return "Use a 32-bit build.";
    auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (module != 0x400000) return "Unsupported image base.";
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(module + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.AddressOfEntryPoint != 0x7bb91 ||
        nt->OptionalHeader.SizeOfImage != 0x12d000) return "Unsupported TH17 version / image layout.";
    if (*reinterpret_cast<uintptr_t*>(0x4b77f0)) return "Install before the first menu is created; restart the game.";
    const uint32_t unlockOffset = *reinterpret_cast<uint32_t*>(0x45adf7);
    if (unlockOffset != 0x2d16e && unlockOffset != 0xf016e)
        return "Unknown score music-unlock layout.";
    for (const auto& p : patches) {
        unsigned char expected[16]; std::memcpy(expected,p.before,p.count);
        if (p.address==0x45a715 || p.address==0x45a921 || p.address==0x45a9ec || p.address==0x45ad67)
            std::memcpy(expected+3,&unlockOffset,4);
        if (std::memcmp(reinterpret_cast<void*>(p.address),expected,p.count))
            return "Patch site differs: another mod or unsupported executable.";
    }
    auto block = static_cast<unsigned char*>(VirtualAlloc(nullptr, sizeof(payload), MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!block) return "Cannot allocate patch code.";
    std::memcpy(block, payload, sizeof(payload));
    auto delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block) - 0x52d000u);
    for (const auto& f : fixups) {
        uint32_t value;
        std::memcpy(&value, block + f.offset, 4);
        value += f.direction == 1 ? delta : (0u - delta);
        std::memcpy(block + f.offset, &value, 4);
    }
    // Follow the active 101-slot / 512-slot score layout for existing music.
    const unsigned unlockDisplacements[] = {0x208,0x308,0x408,0x508};
    for (unsigned offset : unlockDisplacements) std::memcpy(block+offset,&unlockOffset,4);
    // Protect the covering code pages once; overlapping sites must not restore RWX.
    uintptr_t lo=patches[0].address, hi=lo+patches[0].count;
    for (const auto& p : patches) { if(p.address<lo)lo=p.address; if(p.address+p.count>hi)hi=p.address+p.count; }
    DWORD originalProtection;
    if (!VirtualProtect(reinterpret_cast<void*>(lo), hi-lo, PAGE_EXECUTE_READWRITE, &originalProtection)) {
        VirtualFree(block,0,MEM_RELEASE); return "Cannot unlock patch sites.";
    }
    for (const auto& p : patches) {
        unsigned char bytes[16]; std::memcpy(bytes,p.after,p.count);
        if (p.branch) { uint32_t rel; std::memcpy(&rel,bytes+1,4); rel+=delta; std::memcpy(bytes+1,&rel,4); }
        std::memcpy(reinterpret_cast<void*>(p.address),bytes,p.count);
    }
    DWORD ignored;
    VirtualProtect(reinterpret_cast<void*>(lo),hi-lo,originalProtection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),block,sizeof(payload));
    for (const auto& p : patches) FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p.address),p.count);
    installed=true;
    return nullptr;
}
static LONG g_attempted = 0;
static void LogMusic99(const char* text) {
    FILE* file=nullptr;
    if (!fopen_s(&file,"ECLplusMusic99.log","a") && file) {
        fprintf(file,"[music99] %s\n",text); fclose(file);
    }
    OutputDebugStringA(text);
}
static void __cdecl OnMenuCreating() {
    if (InterlockedCompareExchange(&g_attempted,1,0)) return;
    const char* error=InstallMusicRoom99();
    LogMusic99(error ? error : "Installed; supports 99 entries, active score unlock layout detected.");
}
void InitMusicRoom99() {
    wchar_t path[MAX_PATH];
    DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if (!n || n>=MAX_PATH) return;
    wchar_t* slash=wcsrchr(path,L'\\');
    if (!slash) return;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ECLplus.ini");
    if (!GetPrivateProfileIntW(L"ECLplus",L"MusicRoom99",1,path)) return;
    if (sizeof(void*)!=4) { LogMusic99("Use Win32 build."); return; }
    // Never resize an already allocated Menu. Defer until its next constructor.
    const unsigned char original[] = {0x55,0x8b,0xec,0x51,0x53};
    auto site=reinterpret_cast<unsigned char*>(0x451210);
    if (std::memcmp(site,original,sizeof(original))) { LogMusic99("Menu constructor hook conflicts; not installed."); return; }
    auto cave=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (!cave) { LogMusic99("Cannot allocate constructor hook."); return; }
    unsigned at=0; cave[at++]=0x9c; cave[at++]=0x60; cave[at++]=0xe8;
    uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&OnMenuCreating)-reinterpret_cast<uintptr_t>(cave+at+4));
    std::memcpy(cave+at,&relative,4); at+=4;
    cave[at++]=0x61; cave[at++]=0x9d;
    std::memcpy(cave+at,original,sizeof(original)); at+=sizeof(original);
    cave[at++]=0xe9;
    relative=static_cast<uint32_t>(0x451215u-reinterpret_cast<uintptr_t>(cave+at+4));
    std::memcpy(cave+at,&relative,4); at+=4;
    FlushInstructionCache(GetCurrentProcess(),cave,at);
    DWORD protection;
    if (!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) { VirtualFree(cave,0,MEM_RELEASE); LogMusic99("Cannot write constructor hook."); return; }
    unsigned char jump[5]={0xe9};
    relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cave)-0x451215u);
    std::memcpy(jump+1,&relative,4); std::memcpy(site,jump,5);
    DWORD ignored; VirtualProtect(site,5,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    LogMusic99(*reinterpret_cast<uintptr_t*>(0x4b77f0)
        ? "Menu already exists; deferred until the next Menu construction. Restart with loader for predictable activation."
        : "Constructor hook ready; will install before Menu allocation.");
}
} // namespace th17_music99
