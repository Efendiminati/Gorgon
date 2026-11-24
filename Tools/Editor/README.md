# Gorgon 2D Editor

A standalone 2D scene editor built **with Gorgon itself** (`Gorgon::UI` widgets, `Layerbox`
viewport, `Gorgon/Encoding/JSON` and the GScript `VirtualMachine`), plus a small runtime library
so the scenes it saves can be loaded by an ordinary Gorgon game. No external dependencies beyond
what Gorgon already needs.

| Target | Kind | Purpose |
| --- | --- | --- |
| `GorgonEditorCore` | static library (`Gorgon::EditorCore`) | Scene data model, `.gscene.json` serializer, undo stack, play-in-editor session and the `SceneRuntime` loader. Reusable from games. |
| `GorgonEditor` | executable | The editor: toolbar, hierarchy, viewport, inspector, asset browser. |
| `EditorDemo` | executable (example) | ~20 lines of `SceneRuntime` integration that loads `Examples/EditorDemo/Scene/DemoScene.gscene.json` through `SceneRuntime` and runs it. |
| `UnitTest-EditorCore` | test executable | Catch2 tests for the scene model, serializer, undo stack, scripts and runtime loader. |

## Building

The editor is off by default. Configure with the `WindowsDefault` preset (or any preset that
produces executables, e.g. `WindowsTesting`) and turn the editor on:

```bat
cmake --preset WindowsDefault -DBUILD_EDITOR=ON

rem Ninja Multi-Config: pick a configuration
cmake --build build/WindowsDefault --config Release --target GorgonEditor
```

Assets and examples need `BUILD_EXAMPLES=ON` (`WindowsTesting` sets it). `EditorDemo` is only
added to `Examples/` when `Gorgon::EditorCore` exists, i.e. when `BUILD_EDITOR=ON`:

```bat
cmake --preset WindowsTesting -DBUILD_EDITOR=ON
cmake --build build/WindowsTesting --config Release --target GorgonEditor EditorDemo UnitTest-EditorCore
```

Outputs:

- `build/<preset>/Bin/Release/GorgonEditor.exe`
- `Examples/EditorDemo/Bin/EditorDemo.exe` (the `Scene/` folder is copied next to it on every build)

Because MSVC needs its environment, build from a Developer Command Prompt (or run
`"...\VsDevCmd.bat" -arch=amd64` first).

## Using the editor

Start `GorgonEditor.exe` for an empty sample scene, or pass a scene file to open it directly:

```bat
GorgonEditor.exe Examples\EditorDemo\Scene\DemoScene.gscene.json
```

The window is a four-pane layout: toolbar on top, scene hierarchy on the left, viewport in the
centre, details inspector on the right, asset browser along the bottom. The window title shows the
scene path and a `*` while there are unsaved changes.

### Controls

| Input | Action |
| --- | --- |
| Left click on entity | Select it (topmost visible, unlocked entity under the cursor) |
| Left drag on entity | Move it; snaps to `settings.gridSize` while *Grid Snap* is checked |
| Drag a corner handle | Scale the selected entity |
| Drag the red / green arrow | Translate on that axis only |
| Middle drag, or Space + left drag | Pan the camera |
| Mouse wheel | Zoom around the cursor (10%–1600%) |
| `Ctrl+N` | New scene (asks before discarding unsaved changes) |
| `Ctrl+O` | Open scene file |
| `Ctrl+S` / `Ctrl+Shift+S` | Save / Save As |
| `Ctrl+Z` / `Ctrl+Y` / `Ctrl+Shift+Z` | Undo / Redo |
| `Ctrl+D` | Duplicate the selected entity (with its subtree) |
| `Delete` | Delete the selected entity (with its subtree) |
| `F` | Focus the selected entity (centres the camera on it) |

`Ctrl+D`, `Delete` and `F` are ignored while a text field has focus, so typing in the inspector
cannot delete or move anything.

Toolbar buttons: **New**, **Open**, **Save**, **Save As**, **Play**, **Pause**, **Stop**, **Undo**,
**Redo**, **Reset View**, plus the **Grid Snap** toggle and a status line showing the world position
under the cursor and the current zoom.

Panels:

- **Hierarchy** — tree of entities with `#id`, visibility and lock markers. Buttons for *Add*,
  *Add Child*, *Duplicate*, *Delete*, *Rename*, *Visible*, *Locked*, and a parent dropdown that
  reparents the selection (cycles are refused).
- **Details** — name, visibility, lock, z-order, transform (x, y, scale, rotation), sprite
  asset + tint, collider (type, width/height or radius, offset) and script path. The `...` buttons
  open the asset browser as a picker for sprite and script fields.
- **Asset browser** — scans the project folder, filters by All/Images/Audio/Fonts/Scripts/Resources,
  has a search box and image thumbnails. Single click selects, double click (or *Add to Scene*)
  inserts: images become sprite entities, `.gs` files attach a `ScriptComponent`.

### Play mode

**Play** snapshots the current scene, compiles every attached script and runs the frame loop with
real delta times; **Pause** freezes the scripts, **Stop** tears the scripts down and restores the
exact pre-play scene. Editing is disabled while playing, so playing never dirties the scene.

## Scene format (`.gscene.json`)

Scenes are plain JSON written by `Gorgon::Encoding::JSON`. Sprite and script paths are stored
**relative to the scene file's folder**, so a project folder can be moved or zipped as a whole.

```json
{
  "schemaVersion": 1,
  "name": "Level01",
  "settings": { "gridSize": 32, "backgroundColor": [24, 24, 30, 255] },
  "entities": [
    {
      "id": 1,
      "parentId": 0,
      "name": "Player",
      "visible": true,
      "locked": false,
      "zOrder": 0,
      "transform": { "x": 120.0, "y": 80.0, "scaleX": 1.0, "scaleY": 1.0, "rotation": 0.0 },
      "sprite": { "asset": "assets/player.png", "tint": [255, 255, 255, 255] },
      "collider": { "type": "aabb", "w": 32.0, "h": 32.0, "radius": 16.0, "offsetX": 0.0, "offsetY": 0.0 },
      "script": { "path": "scripts/player.gs" }
    }
  ]
}
```

Root fields:

| Field | Type | Notes |
| --- | --- | --- |
| `schemaVersion` | int | Required. Only `1` is accepted; anything else is rejected on load. |
| `name` | string | Scene name shown in the editor title/status. Defaults to `Untitled`. |
| `settings.gridSize` | int | Grid step in world units (also the drag-snap step, minimum 1). |
| `settings.backgroundColor` | `[r,g,b,a]` | 0–255 per channel; also the runtime's clear colour. |
| `entities` | array | Entity objects, see below. |

Entity fields (`id` is required, everything else optional):

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `id` | int > 0 | — | Required and unique within the scene. `0` means "no entity". |
| `parentId` | int | `0` | Parent entity id, `0` for a root entity. Must exist (or be `0`) and must not form a cycle. |
| `name` | string | `Entity` | Display name in the hierarchy. |
| `visible` | bool | `true` | Hidden entities are neither drawn nor selectable in the viewport. |
| `locked` | bool | `false` | Locked entities can be selected in the hierarchy but not dragged/scaled. |
| `zOrder` | int | `0` | Draw order, ascending; ties keep storage order. |
| `transform` | object | identity | `x`, `y`, `scaleX`, `scaleY`, `rotation` (degrees, around the entity centre). |
| `sprite` | object | none | `asset` (path to a png/jpg/bmp) and `tint` (`[r,g,b,a]`, multiplied into the sprite). |
| `collider` | object | none | `type`: `"none"`, `"aabb"` or `"circle"`; `w`/`h` for AABBs, `radius` for circles, `offsetX`/`offsetY` relative to the entity centre. |
| `script` | object | none | `path` to a `.gs` file with an `Update(self, dt)` function. |

Notes:

- **Transforms are local.** An entity's world transform is the composition of its parent chain
  (position, rotation and scale), exactly as the editor renders it. `parentId` gives free grouping.
- **Sprites are drawn centred** on the entity transform, scaled by `scaleX/scaleY` and rotated by
  `rotation`. An entity without a (loadable) sprite draws as a coloured placeholder rectangle, using
  the tint when it is not plain white.
- **Ids are stable**; new entities take the next free id. Duplicating or deleting entities keeps
  the rest of the scene's ids intact, so references in scripts/tools stay valid.

## Scripting (GScript)

Any entity can carry one `.gs` script. The runtime compiles it in a `Gorgon::Scripting::VirtualMachine`
and calls

```
function Update(self, dt)
    ...
    return 0
end function
```

- `self` exposes the entity transform with **writable** `x`, `y`, `rotation` (degrees), `scaleX`
  and `scaleY`. Values written are copied back into the scene after every call.
- `dt` is the frame delta in seconds, clamped to `[0, 0.25]` so a stalled frame cannot teleport an
  entity.
- Top-level assignments (`phase = 0.0`) are script globals: they persist between `Update` calls
  because the VM keeps the script scope alive.
- `using Math` brings in `Sin`, `Cos`, `Tan`, `Log`, … (they take and return doubles; assignments to
  `self.*` are converted to float).
- Numbers are converted on assignment to `self.*`, so `self.y = 250`, `self.y = 250.0` and
  `self.y = 50 + 10 * Sin(t)` all behave as expected. GScript has no implicit conversion in
  arithmetic expressions though — keep a value's type consistent within one expression.
- A script that fails to compile, does not define `Update`, or throws while running is reported by
  `SceneRuntime::GetLastError()`; a throwing script stops being ticked instead of being retried
  every frame (the entity keeps its last good transform).
- Comments start with `#`. Paths are resolved relative to the scene file.

Example (`Examples/EditorDemo/Scene/scripts/orbit.gs`):

```
using Math

phase = 0.0

function Update(self, dt)
    phase = phase + dt
    self.x = 240 + 90 * Cos(phase * 1.6)
    self.y = 200 + 26 * Sin(phase * 3.2)
    self.rotation = phase * 90
    return 0
end function
```

## Loading a scene in a Gorgon game

`SceneRuntime` is the only class a game needs; it lives in `GorgonEditorCore`
(`Tools/Editor/Core/SceneRuntime.h`). Link `Gorgon::EditorCore` (which brings `Gorgon::Gorgon`
along):

```cpp
#include "Core/SceneRuntime.h"

Gorgon::Editor::SceneRuntime runtime;
if(!runtime.Load("Scene/DemoScene.gscene.json"))
    std::cerr << runtime.GetLastError();          // relative sprite/script paths
                                                  // resolve against the scene's folder

Gorgon::Graphics::Layer layer;
window.Add(layer);
runtime.CenterCamera(960, 600);                   // frame the whole scene (optional)

while(running) {
    runtime.Update(Gorgon::Time::DeltaTime() / 1000.0f);   // steps every Update(self, dt)
    layer.Clear();                                          // Render() does not clear
    runtime.Render(layer);
    Gorgon::NextFrame();
}
```

API summary:

| Member | Purpose |
| --- | --- |
| `Load(path, &error)` | Load a `.gscene.json`; sets the base dir to the file's folder. |
| `LoadFromScene(scene, baseDir, &error)` | Use an in-memory `Scene` instead of a file. |
| `Update(dt)` | Tick every attached script by `dt` seconds. |
| `Render(layer)` / `Render(layer, w, h)` | Draw background, visible entities and (optionally) collider wireframes into a layer you own. Uses the layer's target size for the camera unless a size is given. |
| `GetScene()` | The live `Scene` (usable with the editor's model API, e.g. `FindEntity`, `GetWorldTransform`). |
| `GetCamera()` / `SetCamera()` / `CenterCamera(w, h)` | `RuntimeCamera` with `panX`, `panY` (world point at the viewport centre) and `zoom` (pixels per world unit). |
| `GetRenderOptions()` | `drawBackground`, `drawEntities`, `drawColliders` (debug wireframes). |
| `ResolveAssetPath(path)` | Resolve a scene-relative sprite/script path against the base dir. |
| `SetBaseDir(dir)` | Re-root relative paths (re-resolves sprites and restarts scripts). |
| `GetScriptCount()`, `GetLoadedSpriteCount()`, `GetLastError()`, `IsLoaded()`, `Unload()` | Diagnostics. |

Rendering orders entities by `zOrder`, skips invisible ones, composes the parent hierarchy, honours
sprite tint, and falls back to a deterministic coloured rectangle when an image is missing or
unreadable (the failure is reported through `GetLastError()`).

## The bundled demo

`Examples/EditorDemo/` is a self-contained scene:

```
Scene/DemoScene.gscene.json              6 entities: ground strip, tree, a crate stack with a
                                           parented crate, a scripted lantern and a scripted orb
Scene/assets/*.png                       the sprites those entities reference
Scene/scripts/orbit.gs                   orbits + spins the orb around its spawn point
Scene/scripts/bob.gs                     bobs the lantern up and down
```

It is the same file the editor opens, so it exercises both paths: open
`Scene/DemoScene.gscene.json` in `GorgonEditor.exe` to author it, run `EditorDemo.exe` to see it
play. The demo accepts:

```bat
EditorDemo.exe                                   REM Scene\DemoScene.gscene.json next to the exe
EditorDemo.exe path\to\Other.gscene.json         REM any scene
EditorDemo.exe --selftest                        REM headless: load + tick + report, exit code 0
EditorDemo.exe --frames 120                      REM render 120 frames then exit (CI friendly)
```

`--selftest` needs no window: it loads the scene, verifies every sprite path resolves, ticks the
scripts and writes `%TEMP%\gorgon_editor_demo_report.txt` with the resulting transforms.

## Tests

```bat
ctest --test-dir build/WindowsTesting -C Release -R EditorCore --output-on-failure

GorgonEditor.exe --selftest      rem camera, hit-testing, drag/snap, JSON round-trip, asset browser
GorgonEditor.exe --smoke         rem real window: hierarchy/inspector sync, undo/redo, play-in-editor
EditorDemo.exe --selftest        rem SceneRuntime: load, path resolution, script ticking
```

## Source layout

```
Tools/Editor/
  PLAN.md                architecture and phase plan
  CMakeLists.txt         GorgonEditorCore + app + tests
  Core/                  Scene, Entity, Components, SceneSerializer,
                         CommandStack, PlaySession, SceneRuntime
  App/                   EditorApp (window shell), Viewport, HierarchyPanel,
                         InspectorPanel, AssetBrowserPanel, EditorTheme, Main
  Tests/                 Catch2 tests for Core
Examples/EditorDemo/     demo scene, assets, scripts and the small runtime example
```
