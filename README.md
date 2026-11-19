# GTA IV CE infinite loading fix for Wine/Proton (QueryThreadCycleTime)

Fixes Grand Theft Auto IV: The Complete Edition hanging forever on "STARTING A NEW GAME..."
under Wine and Proton.

## Cause

The current GTA IV build polls `QueryThreadCycleTime` and uses the returned cycle count as the
time base of its loading loop. Wine does not implement this function - `dlls/kernelbase/thread.c`
returns FALSE with ERROR_CALL_NOT_IMPLEMENTED - so the loop never terminates while one CPU thread
spins on `getrusage(RUSAGE_THREAD)` + `sched_yield` (measured: 215,530 + 107,764 syscalls in 5 s,
940 `QueryThreadCycleTime` calls returning `stub!` in one +relay capture).

This affects every current Proton build (experimental-11.0, GE-Proton 11-x, proton-cachyos 11.0,
UMU 10.0-4 reaches the window and exits instead). ProtonDB ratings predate the 2026 game update.

## Fix

`qtct.asi` is a 32-bit ASI plugin that patches the `QueryThreadCycleTime` IAT of the game and of
late-loaded modules, replacing the stub with a real cycle count from RDTSC.

Verified working: Fedora, flatpak Steam, GE-Proton11-11, NVIDIA RTX 3090 + AMD Radeon 610M iGPU -
from a 30+ minute infinite load (reproduced on both GPUs, fresh prefixes, verified game files,
every Proton 11-era build) to the main menu and gameplay on the first launch with the plugin.

## Install

1. Download `prebuilt/qtct.asi` (or build it, below).
2. Copy it into `<Steam>/steamapps/common/Grand Theft Auto IV/GTAIV/plugins/`.
   The `plugins` folder is scanned by the Ultimate ASI Loader that ships with FusionFix
   (`dinput8.dll`) - install FusionFix first if you do not have it.
3. Launch the game normally. Keep using your preferred Proton.

To uninstall, delete `qtct.asi`. Once Wine/Proton implements `QueryThreadCycleTime` upstream,
the plugin becomes unnecessary.

## Build

Fedora:

    sudo dnf install mingw32-gcc
    i686-w64-mingw32-gcc -O2 -shared -o qtct.asi src/qtct_asi.c -lpsapi -lkernel32

## Upstream fix

`patches/0001-kernelbase-implement-QueryThreadCycleTime.patch` is a draft implementation for
Wine (`dlls/kernelbase/thread.c`): RDTSC for the calling thread, a runtime-based approximation
for other threads. Needs tightening before submission to wine-devel.

## License

MIT - see LICENSE.
