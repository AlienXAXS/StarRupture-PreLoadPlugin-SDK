# ExamplePreloadPlugin

A complete, working preload plugin. Copy this folder, rename it, give the `.vcxproj` a new name and
a new `ProjectGuid`, add it to the solution, and replace the pattern in `preload.cpp` with one of
your own.

## Files

| File | What it is |
|---|---|
| `preload.cpp` | The four exports, commented. This is the part you edit |
| `dllmain.cpp` | Empty on purpose -- see the comment in it |
| `ExamplePreloadPlugin.vcxproj` | Client/Server x64 configurations, output to `bin\...\Preload\` |

## What it does

Resolves `FEngineLoop::PreInit` before the game's entry point has run, detours it, logs once when
the engine starts coming up, and calls the original.

That is not a useful mod. It is the smallest thing that exercises the whole shape: resolve in
`PreloadScan`, install in `PreloadInit`, do the actual work later on the game thread from inside
the detour. The interesting part of a preload plugin is *when* it runs, not what it does.

## The pattern will not match

The AOB in `preload.cpp` is a placeholder and will be refused -- which is the point of trying it
first. Build it, drop it in `ModLoader\Preload\`, launch, and look at what the loader says: it will
name the pattern, say it was not found (or that it matched more than once), and the game will start
normally without it.

That is the failure path you want to be familiar with before you depend on one.

See the [repo README](../README.md) for the pattern rules and the scan kinds.
