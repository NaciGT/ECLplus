#include "pch.h"
#include "ECLplus.h"
#include "binhack.h"
#include "ins2400.h"

struct CODE_WRITER {
    UCHAR* addr;
    std::vector<UCHAR> pending;

    CODE_WRITER(LPVOID ptr) : addr((UCHAR*)ptr), pending() {}

    // Perform all pending writes.
    // The purpose of this is to make it easier to ensure that codecaves are written before
    // the code that jumps to them.
    VOID Commit() {
        DWORD old;
        SIZE_T size = this->pending.size();
        UCHAR* start = this->addr - size;

        VirtualProtect(start, size, PAGE_EXECUTE_READWRITE, &old);
        CopyMemory(start, this->pending.data(), size);
        VirtualProtect(start, size, old, &old);

        this->pending.clear();
    }

    // Check the bytes at the current write cursor.
    template<SIZE_T SIZE>
    BOOL Expect(CONST CHAR(&arr)[SIZE]) {
        if (memcmp(this->addr, arr, SIZE)) {
            char buf[256];
            snprintf(buf, 256, "ECLplus binhack warning: Expected bytes not found at %#08x.", (DWORD)this->addr);
            EclMsg(buf);
            return FALSE;
        }
        return TRUE;
    }

    // Write bytes and advance the write cursor.
    // They will not actually be written to memory until Commit() is called.
    template<SIZE_T SIZE>
    CODE_WRITER& Write(CONST CHAR(&arr)[SIZE]) {
        this->pending.insert(this->pending.end(), arr, arr + SIZE);
        this->addr += SIZE;
        return *this;
    }

    CODE_WRITER& WriteDword(DWORD value) {
        CHAR littleEndianBytes[4];
        *(DWORD*)littleEndianBytes = value;
        return this->Write(littleEndianBytes);
    }
    CODE_WRITER& WriteAbs(LPCVOID ptr) {
        return this->WriteDword((DWORD)ptr);
    }
    CODE_WRITER& WriteRel(LPCVOID ptr) {
        return this->WriteDword((CONST UCHAR*)ptr - (this->addr + 4));
    }
    CODE_WRITER& WriteNop(SIZE_T size) {
        this->pending.resize(this->pending.size() + size, 0x90);
        this->addr += size;
        return *this;
    }
};

constexpr SIZE_T CAVES_MAXSIZE = 0x3000;
static UCHAR* cavesStart;

void InitBinhacks() {
    ECLPLUS_FUNCS funcs = {};
    cavesStart = new UCHAR[CAVES_MAXSIZE]();

    CODE_WRITER cave{ cavesStart };

    {
        // ECL instructions
        CODE_WRITER binhack = { (LPVOID)0x4211AB };

        binhack.Write({ '\x0F', '\x87' }); // ja CAVE
        binhack.WriteRel(cave.addr);

        cave.Write({ '\x52' }); // push edx
        cave.Write({ '\x57' }); // push edi
        cave.Write({ '\xE8' }); // call InsSwitch
        cave.WriteRel(funcs.insSwitch);
        cave.Write({ '\xe9' }); // jmp  FUNC_END
        cave.WriteRel((LPCVOID)0x4265f2);
        cave.Write({ '\xCC' }); // (unreachable)
        cave.Commit();
        binhack.Commit();
    }

    {
        // Int variables (by value)
        CODE_WRITER binhack = { (LPVOID)0x427524 };

        binhack.Write({ '\x0F', '\x87' }); // ja CAVE
        binhack.WriteRel(cave.addr);

        cave.Write({ '\x6a', '\x00' });         // push 0x0
        cave.Write({ '\x52' });                 // push edx
        cave.Write({ '\x50' });                 // push eax
        cave.Write({ '\xE8' });                 // call IntVarSwitch
        cave.WriteRel(funcs.intVarSwitch);
        cave.Write({ '\x8b', '\xe5' });         // mov esp, ebp
        cave.Write({ '\x5d' });                 // pop ebp
        cave.Write({ '\xc2', '\x04', '\x00' }); // ret 0x4
        cave.Write({ '\xCC' });                 // (unreachable)
        cave.Commit();
        binhack.Commit();
    }

    {
        // Int variables (by pointer)
        CODE_WRITER binhack = { (LPVOID)0x00427Cf1 };

        binhack.Write({ '\x0F', '\x87' }); // ja CAVE
        binhack.WriteRel(cave.addr);

        cave.Write({ '\x6a', '\x01' });         // push 0x1
        cave.Write({ '\x52' });                 // push edx
        cave.Write({ '\x83', '\xc0', '\x0f' }); // add eax, 0xf
        cave.Write({ '\x50' });                 // push eax
        cave.Write({ '\xE8' });                 // call IntVarSwitch
        cave.WriteRel(funcs.intVarSwitch);
        cave.Write({ '\x5e' });                 // pop esi
        cave.Write({ '\x5d' });                 // pop ebp
        cave.Write({ '\xc2', '\x04', '\x00' }); // ret 0x4
        cave.Write({ '\xCC' });                 // (unreachable)
        cave.Commit();
        binhack.Commit();
    }

    {
        // Enemy damage
        CODE_WRITER binhack = { (LPVOID)0x0041FA15 };

        binhack.Expect({ '\xe8', '\x76', '\xb6', '\x02', '\x00' });

        binhack.Write({ '\xE9' }); // jmp CAVE
        binhack.WriteRel(cave.addr);

        cave.Write({ '\xe8' }); // call TALLY_DAMAGE_FROM_PLAYER
        cave.WriteRel((LPCVOID)0x44b090);
        cave.Write({ '\x03', '\x83' });         // add  eax, DWORD PTR[ebx + pendingDmg]
        cave.WriteDword(offsetof(ENEMY, pendingDmg));
        cave.Write({ '\xc7', '\x83' });         // mov  DWORD PTR[ebx + pendingDmg], 0x0
        cave.WriteDword(offsetof(ENEMY, pendingDmg));
        cave.WriteDword(0);
        cave.Write({ '\xE9' });                 // jmp  0x41fa1a
        cave.WriteRel((LPCVOID)0x41fa1a);
        cave.Write({ '\xCC' });                 // (unreachable)
        cave.Commit();
        binhack.Commit();
    }

    {
        // Main loop
        CODE_WRITER binhack = { (LPVOID)0x4612DE };

        CONST CHAR original[] = { '\x89', '\x8d', '\x8c', '\xdd', '\xff', '\xff' };

        binhack.Expect(original);

        binhack.Write({ '\xE9' }); // jmp CAVE
        binhack.WriteRel(cave.addr);
        binhack.WriteNop(1);

        cave.Write(original);            // mov  [ebp-0x2274], ecx
        cave.Write({ '\x60' });          // pusha
        cave.Write({ '\xe8' });          // call MainLoop
        cave.WriteRel(funcs.mainLoop);
        cave.Write({ '\x61' });          // popa
        cave.Write({ '\xE9' });          // jmp  0x4612e4
        cave.WriteRel((LPCVOID)0x4612e4);
        cave.Write({ '\xCC' });          // (unreachable)
        cave.Commit();
        binhack.Commit();
    }

    {
        // ===== Top anchor: inside the FPS overlay task (sub_429F30) =====
        // Original instruction at 0x429FC3: mov eax, [0x004B7678]  (A1 78 76 4B 00)
        // Draws the green debug marker line (see SHOW_TOP_MARKER in ins2400.cpp).
        CONST CHAR want[] = { '\xA1', '\x78', '\x76', '\x4B', '\x00' };
        if (memcmp((void*)0x429FC3, want, 5) != 0) {
            EclPrint("ECLplus: top-anchor SKIPPED (0x429FC3 bytes are not original)\n");
        }
        else {
            CODE_WRITER binhack = { (LPVOID)0x429FC3 };
            UCHAR* tramp = cave.addr;

            CHAR dbg[128];
            snprintf(dbg, 128, "ECLplus top-anchor trampoline @ %#08x\n", (DWORD)tramp);
            EclPrint(dbg);

            cave.Write({ '\x60' });                                  // pushad
            cave.Write({ '\x9C' });                                  // pushfd
            cave.Write({ '\xE8' });                                  // call TopMarker2400
            cave.WriteRel((LPCVOID)&TopMarker2400);
            cave.Write({ '\x9D' });                                  // popfd
            cave.Write({ '\x61' });                                  // popad
            cave.Write({ '\xA1', '\x78', '\x76', '\x4B', '\x00' });  // original instruction
            cave.Write({ '\xE9' });                                  // jmp 0x00429FC8
            cave.WriteRel((LPCVOID)0x00429FC8);
            cave.Write({ '\xCC' });                                  // (unreachable)
            cave.Commit();

            binhack.Write({ '\xE9' });                               // jmp trampoline
            binhack.WriteRel(tramp);
            binhack.Commit();
        }
    }

    {
        // ===== Render-layer hooks (final integration) =====
        // Slot A (BACK) : before task 0x442ED0 runs -> lines below bullets/boss
        // Slot B (FRONT): before task 0x443020 runs -> lines above bullets/boss
        // Both slots are "push offset <task>" inside the task registrar (sub_441730);
        // we redirect the push to a trampoline that flushes our line channel,
        // then jumps to the original task.
        static const struct { DWORD task; LPCVOID flush; const char* name; } kHooks[] = {
            { 0x00442ED0, (LPCVOID)&FlushBackLines2400,  "BACK"  },
            { 0x00443020, (LPCVOID)&FlushFrontLines2400, "FRONT" },
        };

        for (int i = 0; i < sizeof(kHooks) / sizeof(kHooks[0]); i++) {
            DWORD va = kHooks[i].task;
            UCHAR need[5];
            need[0] = 0x68;
            need[1] = (UCHAR)(va & 0xFF);
            need[2] = (UCHAR)((va >> 8) & 0xFF);
            need[3] = (UCHAR)((va >> 16) & 0xFF);
            need[4] = (UCHAR)((va >> 24) & 0xFF);

            UCHAR* found = nullptr;
            for (UCHAR* p = (UCHAR*)0x00441730; p < (UCHAR*)0x004418B4; p++) {
                if (memcmp(p, need, 5) == 0) { found = p; break; }
            }

            CHAR buf[160];
            if (!found) {
                snprintf(buf, 160, "ECLplus layer-hook: push %06X NOT FOUND\n", (unsigned)va);
                EclPrint(buf);
                continue;
            }

            UCHAR* tramp = cave.addr;
            cave.Write({ '\x60' });            // pushad
            cave.Write({ '\x9C' });            // pushfd
            cave.Write({ '\xE8' });            // call flush
            cave.WriteRel(kHooks[i].flush);
            cave.Write({ '\x9D' });            // popfd
            cave.Write({ '\x61' });            // popad
            cave.Write({ '\xE9' });            // jmp original task
            cave.WriteRel((LPCVOID)va);
            cave.Write({ '\xCC' });            // (unreachable)
            cave.Commit();

            CODE_WRITER binhack = { (LPVOID)found };
            binhack.Write({ '\x68' });         // push imm32
            binhack.WriteAbs(tramp);           // ... <trampoline>
            binhack.Commit();

            snprintf(buf, 160, "ECLplus layer-hook %s: %06X -> tramp %08X\n",
                kHooks[i].name, (unsigned)found, (unsigned)tramp);
            EclPrint(buf);
        }
    }

    // Player damage multiplier nop
    CODE_WRITER{ (LPVOID)0x0041E94F }.WriteNop(10).Commit();

    // (note: not giving this write permission crashes for some reason...?)
    DWORD old;
    VirtualProtect(cavesStart, CAVES_MAXSIZE, PAGE_EXECUTE_READWRITE, &old);

    // Copy func pointers to a location in static memory where downstream patches can find them.
    VirtualProtect(FUNC_POINTERS_BEGIN, sizeof(funcs), PAGE_EXECUTE_READWRITE, &old);
    *FUNC_POINTERS_BEGIN = funcs;
    VirtualProtect(FUNC_POINTERS_BEGIN, sizeof(funcs), old, &old);
}