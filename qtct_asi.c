/* qtct.asi - GTA IV fix for Wine's unimplemented QueryThreadCycleTime
 *
 * Wine's kernelbase returns FALSE / ERROR_CALL_NOT_IMPLEMENTED for
 * QueryThreadCycleTime (stub, confirmed in wine master 2026-11 and in the
 * GE-Proton11-11 runtime this game runs on). GTA IV CE's 2026 build calls it
 * on its own thread as the time base of its load-wait loop, which then never
 * terminates (observed: 215k getrusage + 108k sched_yield in 5 s, 940
 * QueryThreadCycleTime calls in one capture, all returning stub!).
 *
 * This plugin patches the IAT of the game (and every loaded module) so
 * QueryThreadCycleTime returns a real monotonic cycle count (RDTSC).
 *
 * Build (32-bit, because GTAIV.exe is WOW64):
 *   i686-w64-mingw32-gcc -O2 -shared -o qtct.asi qtct_asi.c -lkernel32
 * Install:
 *   copy qtct.asi into "<game>/GTAIV/plugins/" (loaded by Ultimate ASI Loader,
 *   which FusionFix ships as dinput8.dll).
 */

#include <windows.h>
#include <psapi.h>

typedef BOOL (WINAPI *pfn_qtct)(HANDLE, PULONG64);

static BOOL WINAPI hook_qtct(HANDLE thread, PULONG64 cycle)
{
    (void)thread;
    if (!cycle) return FALSE;
#if defined(__i386__) || defined(__x86_64__)
    *cycle = __builtin_ia32_rdtsc();
#else
    *cycle = (ULONG64)GetTickCount64() * 1000;
#endif
    return TRUE;
}

/* Patch the import table of one module so imports of QueryThreadCycleTime
 * resolve to hook_qtct. Works for 32-bit PE images (the game) and for
 * 32-bit DLLs in the same process. */
static void patch_module(HMODULE mod)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return;

    IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress) return;

    IMAGE_IMPORT_DESCRIPTOR *imp =
        (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);

    for (; imp->Name; imp++)
    {
        IMAGE_THUNK_DATA32 *thunk = (IMAGE_THUNK_DATA32 *)(base + (imp->OriginalFirstThunk
                                        ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA32 *iat = (IMAGE_THUNK_DATA32 *)(base + imp->FirstThunk);
        for (; thunk->u1.AddressOfData; thunk++, iat++)
        {
            if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
            IMAGE_IMPORT_BY_NAME *name =
                (IMAGE_IMPORT_BY_NAME *)(base + thunk->u1.AddressOfData);
            if (lstrcmpA((LPCSTR)name->Name, "QueryThreadCycleTime") == 0)
            {
                DWORD old;
                if (VirtualProtect(&iat->u1.Function, sizeof(DWORD),
                                   PAGE_READWRITE, &old))
                {
                    iat->u1.Function = (DWORD)(ULONG_PTR)hook_qtct;
                    VirtualProtect(&iat->u1.Function, sizeof(DWORD), old, &old);
                }
            }
        }
    }
}

static DWORD WINAPI repatch(LPVOID arg)
{
    (void)arg;
    for (int i = 0; i < 120; i++)   /* re-patch every 2 s for 4 minutes */
    {
        Sleep(2000);
        HMODULE mods[512]; DWORD needed = 0;
        if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        {
            DWORD n = needed / sizeof(HMODULE); if (n > 512) n = 512;
            for (DWORD k = 0; k < n; k++) patch_module(mods[k]);
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        /* Patch the main module and everything else already loaded. */
        HMODULE mods[512];
        DWORD needed = 0;
        if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        {
            DWORD n = needed / sizeof(HMODULE);
            if (n > 512) n = 512;
            for (DWORD i = 0; i < n; i++)
                patch_module(mods[i]);
        }
        else
        {
            patch_module(GetModuleHandle(NULL));
        }
        CreateThread(NULL, 0, repatch, NULL, 0, NULL);
    }
    return TRUE;
}
