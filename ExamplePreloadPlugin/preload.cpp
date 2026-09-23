// ---------------------------------------------------------------------------
// ExamplePreloadPlugin
//
// A complete, working preload plugin. Copy the folder, rename it, and replace
// the pattern below with one of your own.
//
// What it does: resolves FEngineLoop::PreInit before the game's entry point has
// run, detours it, logs once when the engine starts coming up, and calls the
// original. That is the whole shape of a preload plugin -- the interesting part
// is WHEN it happens, not what it does.
//
// Read Preload.md in the mod loader repo for the rules. The three that catch
// people out:
//
//   1. There is no engine yet. No GMalloc, no UObject, no FName, no GConfig.
//      Touching any of them here is a crash before WinMain, with no crash
//      handler to report it.
//
//   2. Resolve in PreloadScan, install in PreloadInit. The scan has no patch
//      table on purpose -- see the header.
//
//   3. Do not block. The game's main thread is parked waiting for you.
// ---------------------------------------------------------------------------

#include "preload_interface.h"

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

static PreloadInfo s_info = {
    PRELOAD_INTERFACE_VERSION,
    "ExamplePreloadPlugin",
    "1.0.0",
    "your name here",
    "Demonstrates resolving and hooking before the engine starts",

    // The client and the dedicated server are different executables with
    // different code at different addresses. A mismatch is skipped, not
    // guessed at.
    PRELOAD_TARGET_CLIENT,

    // Lower runs first; ties break by file name. Matters only when two preload
    // plugins patch the same function.
    100
};

// ---------------------------------------------------------------------------
// Resolved in PreloadScan, used in PreloadInit
// ---------------------------------------------------------------------------

static uintptr_t s_preInitAddress = 0;

typedef int(__fastcall* FEngineLoop_PreInit_t)(void* self, const wchar_t* cmdLine);
static FEngineLoop_PreInit_t s_originalPreInit = nullptr;

static IPluginSelf* s_self = nullptr;

// The detour. Runs on the game's main thread, once, as the engine starts --
// which is long after this file's other functions have finished. By this point
// the engine exists, so this is where engine work belongs.
static int __fastcall PreInitDetour(void* self, const wchar_t* cmdLine)
{
    if (s_self && s_self->logger)
        s_self->logger->Info(s_self, "FEngineLoop::PreInit reached -- the engine is starting");

    return s_originalPreInit(self, cmdLine);
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) PreloadInfo* GetPreloadInfo()
{
    return &s_info;
}

// Optional export. Resolve every address you need, and nothing else.
//
// Any failure here -- a pattern that misses, matches twice, or lands on the
// wrong kind of address -- refuses this plugin: PreloadInit is never called,
// the DLL is unloaded, and the game boots without it. There is nothing to
// check and no error to handle; the loader has already reported it.
extern "C" __declspec(dllexport)
void PreloadScan(IPluginSelf* self, IPluginHookScanner* scanner)
{
    PluginScanRequest request = PLUGIN_SCAN_REQUEST_INIT;

    // Name the thing, not the pattern: this is what the log, the failure
    // window and the clipboard text show under your plugin's name.
    request.hookName = "FEngineLoop::PreInit";

    // Replace with your own. IDA-style, ?? is any byte.
    request.pattern  = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ??";

    // Declaring the kind is what turns "the pattern matched" into "the pattern
    // matched the right thing". FUNCTION_START is checked against the
    // executable's exception directory: it must be a function's primary entry,
    // not a cold chunk, and long enough to hold a detour.
    request.kind     = PLUGIN_SCAN_FUNCTION_START;

    s_preInitAddress = scanner->Resolve(self, &request);
}

// Called only when every pattern above resolved cleanly. Install here.
extern "C" __declspec(dllexport)
bool PreloadInit(IPluginSelf* self, IPreloadPatch* patch)
{
    s_self = self;

    self->logger->Info(self, "preload starting -- game build %s, loader %s",
                       patch->GetGameVersion(), patch->GetLoaderBuildTag());

    if (!patch->InstallHook(self, "FEngineLoop::PreInit", s_preInitAddress,
                            &PreInitDetour, reinterpret_cast<void**>(&s_originalPreInit)))
    {
        // Returning false unloads this DLL and lets the game carry on. The
        // loader takes back out any hook that did install before this point.
        self->logger->Error(self, "could not install the PreInit hook -- standing down");
        return false;
    }

    return true;
}

// Optional export. The loader has already removed every hook this plugin
// installed by the time this runs, so there is usually nothing left to do.
extern "C" __declspec(dllexport)
void PreloadShutdown()
{
    s_self = nullptr;
}
