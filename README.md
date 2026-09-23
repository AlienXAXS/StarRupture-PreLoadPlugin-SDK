# StarRupture PreLoad Plugin SDK

Everything you need to build **preload plugins** for the
[StarRupture ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader) — DLLs that run
**before the game executable's entry point**, so they can patch code the engine touches on its way
up.

> Looking for ordinary plugins — UI panels, engine events, config, networking? That's the
> [StarRupture Plugin SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK). This repo is the
> narrow, early-boot one.

---

## Preload plugin vs ordinary plugin

|  | Ordinary plugin | Preload plugin |
|---|---|---|
| Folder | `ModLoader\Plugins\` | `ModLoader\Preload\` |
| Runs at | After the engine is ready | Before the game's entry point has run |
| Can use | The whole engine, the whole plugin API | Pattern scanning, byte patching, detours |
| `self->hooks` | The full `IPluginHooks` | `nullptr` |
| `self->config` | `IPluginConfig` | `nullptr` |
| If it fails | Plugin does not load | Plugin does not load; **game starts anyway** |
| Reload at runtime | Yes | No — the window is gone |

Use a preload plugin when, and only when, you need to change something **before the engine reads
it**. Everything else belongs in an ordinary plugin, where you get the whole API and can hot-reload
while the game runs.

---

## What's in this repo

| Path | Description |
|---|---|
| `include/preload_interface.h` | The preload API — `PreloadInfo`, `IPreloadPatch`, the exports |
| `include/plugins/plugin_interface.h` | `IPluginSelf` and `IPluginHookScanner`, shared with the plugin API |
| `ExamplePreloadPlugin/` | A complete, commented starter — copy, rename, go |
| `Shared.props` | MSBuild target defines |

Both headers are copied verbatim from the mod loader by CI. Don't edit them here.

---

## Quick start

1. **Clone this repo.**
2. **Copy `ExamplePreloadPlugin\`** to a new folder, rename it, and give the `.vcxproj` a new name
   and a new `ProjectGuid`.
3. Open `StarRupture-PreLoadPlugin-SDK.sln` and build **`Client Release`** or **`Server Release`**.
4. Drop the `.dll` into `<game>\Binaries\Win64\ModLoader\Preload\` and launch.
5. Check `ModLoader\Logs\modloader.log` for the `[Preload]` lines, or run `preload` in the
   mod loader console.

There is no generic configuration. The client and the dedicated server are different executables
with different code at different addresses, so a preload plugin declares one or the other in
`PreloadInfo::target` and the loader skips a mismatch.

---

## The shape of a preload plugin

Four exports, two of them optional:

```cpp
PreloadInfo* GetPreloadInfo();                                   // required
void  PreloadScan(IPluginSelf* self, IPluginHookScanner* s);     // optional -- resolve only
bool  PreloadInit(IPluginSelf* self, IPreloadPatch* patch);      // required -- install
void  PreloadShutdown();                                         // optional
```

```cpp
#include "preload_interface.h"

static PreloadInfo s_info = {
    PRELOAD_INTERFACE_VERSION,
    "MyPreload", "1.0.0", "me",
    "Patches a thing before the engine starts",
    PRELOAD_TARGET_CLIENT,
    100                              // priority: lower runs first
};

static uintptr_t s_target   = 0;
static void*     s_original = nullptr;

extern "C" __declspec(dllexport) PreloadInfo* GetPreloadInfo() { return &s_info; }

extern "C" __declspec(dllexport)
void PreloadScan(IPluginSelf* self, IPluginHookScanner* scanner)
{
    PluginScanRequest req = PLUGIN_SCAN_REQUEST_INIT;
    req.hookName = "FEngineLoop::PreInit";
    req.pattern  = "48 89 5C 24 ?? 57 48 83 EC ??";
    req.kind     = PLUGIN_SCAN_FUNCTION_START;

    s_target = scanner->Resolve(self, &req);
}

extern "C" __declspec(dllexport)
bool PreloadInit(IPluginSelf* self, IPreloadPatch* patch)
{
    return patch->InstallHook(self, "FEngineLoop::PreInit", s_target, &MyDetour, &s_original);
}
```

### Resolve in `PreloadScan`, install in `PreloadInit`

The split is not decorative, and `PreloadScan` deliberately has no patch table.

`PreloadInit` is only called when **every** pattern in `PreloadScan` resolved cleanly. If a plugin
could install a detour while resolving and a later pattern then missed, the loader would have to
free a module that has already written a jump into game code — and it cannot undo that, or even
find out it happened. So the interface does not hand you the means.

---

## There is no engine yet

At preload time the game's main thread is parked and the executable's entry point has not run.

**Available:** the mapped game `.exe`, pattern scanning over it, byte patching, detours,
`self->logger`.

**Not available:** `GMalloc` (the CRT static initialisers have not run), `FEngineLoop::PreInit`,
`GEngine`, any world, `UObject`, `FName`, `FString`, `GConfig`, the ImGui overlay, the console,
plugin networking.

So: **resolve, patch, and return.** Don't touch the engine, don't allocate through it, don't start
threads that assume it exists, and don't block — the whole game is waiting on you, with a hard
180-second ceiling before the loader gives up and lets it boot regardless.

Your detours fire later, on the game thread, once the engine is running. That is normal and is what
they are for. `DllMain` runs under the loader lock at this point, so keep it empty too.

---

## Pattern rules

Both of these refuse the plugin. There is no flag to turn either off.

**1. A pattern must match exactly once.** Two matches is not an address, it is a coin flip.

**2. The address must be what you said it is.** Declare a `PluginScanKind` and the loader checks it
against the executable's structure before handing it back:

| Kind | Checked against |
|---|---|
| `PLUGIN_SCAN_FUNCTION_START` | The exception directory (`.pdata`): a function's primary entry, not a separated cold chunk, at least 14 bytes long so a detour fits |
| `PLUGIN_SCAN_IN_FUNCTION` | Inside some function with unwind info — for mid-function anchors |
| `PLUGIN_SCAN_CODE` | An executable section — for hand-written thunks with no unwind info |
| `PLUGIN_SCAN_DATA` | An initialised, non-executable section |
| `PLUGIN_SCAN_VTABLE` | Data, and the first `vtableSlots` pointers each point at a function start |
| `PLUGIN_SCAN_ANY` | Nothing. Uniqueness only, and the report says so |

`PLUGIN_SCAN_UNSPECIFIED` is `0` and is **refused** — a request that forgot to declare a kind does
not quietly get the weakest check. Always initialise with `PLUGIN_SCAN_REQUEST_INIT`.

When something fails, the loader names every match and what it landed on:

```
FEngineLoop::PreInit  [required]
    pattern is not unique -- it matched 3 times and an AOB must resolve to exactly one address.
    Pattern: 48 89 5C 24 ?? 57 48 83 EC ??
      #1  StarRupture-Win64-Shipping.exe+0x3F219B0  .text  function start (0x1A4 bytes)
      #2  StarRupture-Win64-Shipping.exe+0x41C2A37  .text  inside function +0x41C2A00 (+0x37)
      #3  StarRupture-Win64-Shipping.exe+0x52B1104  .rdata
```

Those RVAs paste straight into IDA. If it says your pattern landed `0x37` bytes inside a function,
`req.resultOffset = -0x37` is the fix.

`req.followRel32At` (with `PLUGIN_SCAN_FLAG_FOLLOW_REL32`) decodes an `E8`/`E9` at that offset and
resolves to its target instead — useful for reaching a function through a distinctive call site,
and unlike decoding it yourself, the target still gets the kind check.

---

## `IPreloadPatch`

```cpp
HMODULE     GetGameModule();
uintptr_t   GetGameModuleBase();
size_t      GetGameModuleSize();
const char* GetGameVersion();        // the game's ProductVersion
const char* GetLoaderBuildTag();     // "dev" on local builds

bool ReadBytes (uintptr_t address, void* dest, size_t size);
bool WriteBytes(uintptr_t address, const void* source, size_t size);
bool Nop       (uintptr_t address, size_t size);

bool InstallHook(const IPluginSelf* self, const char* name, uintptr_t target,
                 void* detour, void** outOriginal);
bool RemoveHook (const IPluginSelf* self, const char* name);
```

The memory functions handle page protection and refuse an address outside a loaded module, so a bad
offset returns `false` rather than faulting before the game has started. `InstallHook` refuses a
target of `0` for the same reason — passing through a pattern that did not resolve fails cleanly.

**Hooks are owned by the loader, not by your plugin.** They are registered under the owning plugin
and removed at shutdown, and if `PreloadInit` crashes part-way through, whatever it already
installed is taken back out before the module is freed.

---

## Failing is cheap, and the game never pays for it

A preload plugin that fails anything is unloaded, reported, and skipped — and the game starts
normally. "Anything" means a wrong interface version, the wrong build target, a pattern that
misses, a pattern that matches twice, a pattern that resolves to the wrong kind of thing, or a
crash in any entry point.

This is the opposite of the mod loader's own pattern preflight, which disables the whole loader
when one of *its* patterns breaks. A preload plugin is somebody's mod, and an outdated one has to
be safe to leave installed across a game update: it stops working, it says so, and nothing else
changes.

### Seeing what happened

The phase runs before anything that could display a result exists, so the loader keeps the records
for the session:

```
> preload
3 preload plugin(s), 2 running:
  EarlyPatch               1.2.0      running                      EarlyPatch.dll
      2 hook(s) installed
  OldMod                   1.0.0      pattern scan failed          OldMod.dll
      one or more patterns did not resolve -- run `hookfailures` for the detail
```

On the client, failures also appear in the plugin hook failure window at the main menu, tagged
`[preload]`. On a dedicated server (`-console`), the console is the only route.

### If a preload plugin stops the game starting

It shouldn't be able to — but a plugin that *hangs* is the one case the loader cannot catch, so
there are three ways out:

```
-NoPreload                              command line; skips the phase entirely
```

```ini
[Preload]
Enabled=0                               ; skip the phase
Disabled=BadPlugin.dll,Other.dll        ; skip named plugins
```

The loader also leaves a breadcrumb: `ModLoader\Preload\.state` holds the name of whichever DLL is
currently being touched and is deleted the moment it finishes. If it survives a launch, that plugin
is **skipped on the next one** and the log says which it was and how to undo it. That turns "the
game won't start and the log stops mid-sentence" into a second launch that works and names the
culprit.

---

## Interface versioning

`PreloadInfo::interfaceVersion` must be `PRELOAD_INTERFACE_VERSION` from `preload_interface.h`, and
the loader refuses anything outside its supported `[MIN, MAX]` range.

This is versioned **independently** of `PLUGIN_INTERFACE_VERSION`: a preload plugin uses a small
corner of the API and should not need rebuilding every time the much busier plugin interface gains
a field it never touches.

---

## Runtime DLL

The mod loader itself (`dwmapi.dll` plus `ModLoader\`) is **not** built from this repo. Get it from
the [mod loader releases](https://github.com/AlienXAXS/StarRupture-ModLoader/releases/latest).

Don't fork or build the main mod loader repo just to develop a preload plugin — this repo is the
intended starting point.

---

## Further reading

- [Preload.md](https://github.com/AlienXAXS/StarRupture-ModLoader/blob/main/Preload.md) — the
  loader-side design and why each rule exists
- [StarRupture Plugin SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK) — ordinary plugins
