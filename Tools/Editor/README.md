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
- **Asset browser** — scans the project folder, filters by All/Images/Audio/Fonts/Scripts/Prefabs/Resources,
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
| `tags` | string[] | none | Free-form labels; `Scene.FindByTag`/`HasTag` read them. Omitted when empty. |
| `properties` | object | none | String key/value pairs a script reads and writes with `Scene.GetProp`/`SetProp`. |
| `spriteAnim` | object | none | Sprite-sheet animation: `asset`, `frameW`, `frameH`, `frameCount`, `fps`, `loop`, `playing`, `currentFrame`, `tint`. |
| `text` | object | none | `text`, `color`, `fontSize`, `style`, and `align` (`left`/`center`/`right`). |
| `audio` | object | none | `asset`, `volume` (0–1), `loop`, `playOnStart`. |
| `camera` | object | none | `active`, `zoom`, `followTarget` entity id. |
| `tilemap` | object | none | `tileW`, `tileH`, `cols`, `rows`, `tilesetCols`, `tilesetAsset` and a row-major `tiles` array (`-1` is an empty cell). |
| `particles` | object | none | `emitRate` (per second), `lifetime`, `speed`, `startColor`, `endColor`, `size`, `active`. |
| `prefabSource` | string | none | Path to the `.gprefab.json` this entity was instantiated from. Omitted when empty. |

Notes:

- **Transforms are local.** An entity's world transform is the composition of its parent chain
  (position, rotation and scale), exactly as the editor renders it. `parentId` gives free grouping.
- **Sprites are drawn centred** on the entity transform, scaled by `scaleX/scaleY` and rotated by
  `rotation`. An entity without a (loadable) sprite draws as a coloured placeholder rectangle, using
  the tint when it is not plain white.
- **Ids are stable**; new entities take the next free id. Duplicating or deleting entities keeps
  the rest of the scene's ids intact, so references in scripts/tools stay valid.
- **`prefabSource` is optional and additive.** It only records which prefab an entity came from;
  a scene written by an older editor loads unchanged, and scenes without prefabs never emit it.

### Expanded components

All of the following are optional and additive: a scene written before they existed loads
unchanged, and each one is only written back when an entity carries it.

| Component | Stepped by | What it does |
| --- | --- | --- |
| `spriteAnim` | `PlaySession::Tick` / `SceneRuntime::Update` | Splits the sheet into `frameW x frameH` cells, row-major, and advances `currentFrame` at `fps`. `loop = false` stops on the last frame (and clears `playing`). |
| `text` | renderer | Drawn at the entity origin. `SceneRuntime::SetTextPrinter()` takes the host's `StyledPrinter`; without one a placeholder quad is drawn (headless runs have no font). |
| `audio` | step | `playOnStart` fires the asset once per play session through the same path `Audio::PlaySound` uses, so it is recorded even without an audio device. |
| `camera` | step | The first active component owns the projection: `zoom`, centred on `followTarget` (or on the camera entity). It overrides a script's `SetCameraPos`. |
| `tilemap` | renderer | One quad per cell, textured from `tilesetAsset` (row-major with `tilesetCols` columns) or coloured per tile index when there is no tileset. |
| `particles` | step | CPU emitter: `emitRate` particles per second, each living `lifetime` seconds and moving `speed` units/s from the emitter origin, coloured from `startColor` to `endColor`. Live particles live in the runtime, never in the scene file. |

Tags and properties are plain entity metadata: the inspector edits tags as a comma-separated list,
and scripts read/write both through `using Scene`:

| Function | Returns | Meaning |
| --- | --- | --- |
| `FindByTag(tag)` | Int | first entity carrying the tag, `0` when there is none |
| `FindAllByTag(tag)` | Int | number of entities carrying the tag |
| `GetTaggedEntity(tag, index)` | Int | id of the index-th entity carrying the tag |
| `HasTag(id, tag)` | Int | `1` when the entity carries the tag |
| `AddTag(id, tag)` | Int | `1` when the tag was added |
| `GetProp(id, key)` | String | property value, empty when the key does not exist |
| `SetProp(id, key, value)` | Int | `1` when written |

The inspector adds and removes every component above from the selected entity, plus the tags field.
Scene files stay `schemaVersion: 1`.

### Prefabs (`.gprefab.json`)

A prefab is a reusable entity blueprint: one root entity plus its whole descendant subtree, stored
with the root at the local origin `(0, 0)` and ids normalized to `1..N`.

```json
{
  "schemaVersion": 1,
  "type": "gorgon.prefab",
  "name": "Turret",
  "rootId": 1,
  "entities": [
    { "id": 1, "parentId": 0, "name": "Turret", "transform": { "x": 0.0, "y": 0.0, "rotation": 30.0 } },
    { "id": 2, "parentId": 1, "name": "Barrel", "transform": { "x": 12.0, "y": 0.0 } }
  ]
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `schemaVersion` | int | Required, currently `1`. |
| `type` | string | `"gorgon.prefab"`; informational. |
| `name` | string | Prefab name; defaults to the root entity's name. |
| `rootId` | int | Local id of the root entity; defaults to the first root found. |
| `entities` | array | Root first, then descendants in DFS order. Same entity schema as a scene. |

Workflow:

- **Save as Prefab** — select an entity, then use `Save as Prefab` in the Inspector's *Prefab*
  section. The file goes to the scene's folder (or the asset browser root) as
  `<EntityName>.gprefab.json`, and the entity is linked to it via `prefabSource`.
- **Instantiate** — the Asset Browser lists `.gprefab.json` files under the **Prefabs** filter;
  double-click them or drag them into the viewport. The whole subtree appears at the drop point
  (grid-snapped) with fresh entity ids as one undo step.
- **Apply / Revert** — a linked instance shows its `prefabSource` plus `Apply` and `Revert`
  buttons. Both restore the instance from the file on disk as a single undo step, keeping the
  instance's own id, parent and position so placement and scene references survive.

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
- Optional lifecycle hooks next to `Update` (the runtime wires only the ones the script defines):
  `function Start(self)` runs **once** — when play starts, or when the entity is spawned — and
  `function OnDestroy(self)` runs **before** the entity is destroyed and before Play stops.
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

## Script input (keyboard and mouse)

Scripts reach the keyboard and mouse through the `Input` library. It is a separate library, so a
script opts in with a `using Input` line; nothing is implicit.

| Function | Returns | Meaning |
| --- | --- | --- |
| `IsKeyDown(name)` | Int (`1`/`0`) | Key is held down this frame. |
| `IsKeyPressed(name)` | Int (`1`/`0`) | Key went down on this frame only. |
| `IsKeyReleased(name)` | Int (`1`/`0`) | Key went up on this frame only. |
| `IsMouseDown(button)` | Int (`1`/`0`) | Mouse button is held. |
| `MouseX()`, `MouseY()` | Float | Mouse position in **world** units (not viewport pixels, no grid snap). |

Key names are matched case- and whitespace-insensitively, so `"W"`, `"w"` and `" w "` are the same
query, and `"Return"`/`"Enter"`, `"Esc"`/`"Escape"`, `"ArrowLeft"`/`"Left"` are folded onto one form.
The exposed names are `a`–`z`, `0`–`9`, `left`, `right`, `up`, `down`, `space`, `enter`, `escape`,
`tab`, `backspace`, `delete`, `insert`, `home`, `end`, `pageup`, `pagedown`, `shift`, `control`,
`alt`, `meta` and `f1`–`f12`. Buttons are `left`, `right` and `middle` (`LMB`/`RMB`/`MMB` also work).
An unknown name simply reports `0`; it never errors.

Pressed and released are **edge flags for exactly one frame**: `PlaySession::Tick()` and
`SceneRuntime::Update()` clear them at the end of the step, so every script sees a press once and a
held key keeps reporting `IsKeyDown` until the release arrives.

```
using Input

speed = 0.0

function Update(self, dt)
    # held key: continuous movement
    if IsKeyDown("d") then
        self.x = self.x + 200 * dt
    end if

    # edge: fire once per press
    if IsKeyPressed("Space") then
        speed = 600.0
    end if

    # mouse: follow the pointer while the left button is held
    if IsMouseDown("Left") then
        self.x = MouseX()
        self.y = MouseY()
    end if

    return 0
end function
```

### Feeding the input state

The engine never reads a device itself — whoever owns the frame loop fills an `InputState`:

- **Play-in-editor** does it for you: `EditorApp` mirrors every `KeyPreviewEvent` into the session,
  and the viewport reports the pointer position and button state, converted to world units with its
  own camera. Scripts therefore see the same coordinates the viewport draws.
- **`SceneRuntime`** exposes `GetInput()` and expects you to fill it before `Update(dt)`:

```cpp
window.KeyPreviewEvent.Register([&](Gorgon::Input::Key key, float state) {
    const auto name = Gorgon::Editor::KeyNameFromCode(key);   // "w", "left", "space", ...
    if(!name.empty())
        runtime.GetInput().SetKeyDown(name, state > 0.0f);
});

window.MouseEvent.Register([&](Gorgon::Input::Mouse::Button btn, float state) {
    const auto name = Gorgon::Editor::MouseButtonName(btn);   // "left", "right", "middle"
    if(!name.empty())
        runtime.GetInput().SetMouseButtonDown(name, state > 0.0f);
});

// per frame, before runtime.Update(dt)
const auto world = runtime.GetCamera().ScreenToWorld(mouseX, mouseY, size.Width, size.Height);
runtime.GetInput().SetMousePosition(world.X, world.Y);
runtime.Update(Gorgon::Time::DeltaTime() / 1000.0f);
```

Edge flags you set are consumed by the following `Update()`, so set them again for the next frame;
held state persists until you clear it. `InputState::Reset()` forgets everything, `ClearEdges()` only
drops the one-frame flags. Tests (and scripted demos) drive the same struct directly, which is how
`--selftest` and the `[Input]` Catch2 cases exercise a real `.gs` script without a keyboard.

## Collision and physics

Every entity can carry one `ColliderComponent` (inspector → Collider): an `AABB` (`W`/`H`) or a
`Circle` (`Radius`), placed relative to the entity with `Offset`. Two flags change how the solver
treats it:

| Flag | Serialized as | Meaning |
| --- | --- | --- |
| `Trigger` | `"isTrigger": true` | Reports overlaps but is never pushed apart. |
| `Static` | `"isStatic": true` | Never moves; dynamic colliders are pushed out of it instead. |

Both default to `false` and are omitted from the JSON when unset, so older scenes load unchanged.

Play mode runs one collision pass per `PlaySession::Tick()` (and therefore per
`SceneRuntime::Update()`), *after* the scripts have moved their entities:

1. every collider is turned into a world-space shape (`CollisionSystem::ComputeShape`), which
   accounts for the entity's composed world transform and its collider offset — the parent scale
   sizes the shape and the world rotation swings the offset;
2. overlapping pairs are collected and reported through `PlaySession::GetActiveOverlaps()` (or
   `SceneRuntime::GetActiveOverlaps()`, plus `IsOverlapping(a, b)`);
3. non-trigger pairs where at least one collider is dynamic are separated along the minimum
   translation vector — two dynamic bodies travel half of it each, a dynamic one takes all of it
   against a static one;
4. the pair set is diffed against the previous frame and the difference drives the script hooks.

The queries (`CollisionSystem::Overlaps` / `ComputeOverlap` / `ContainsPoint`) are pure geometry, so
they can be used without a play session at all. Colliders are axis aligned in world space: rotation
moves the offset but never tilts the box.

### Collision hooks

A script may define any of these next to `Update`; the runtime only wires the ones that exist:

```
function OnCollisionEnter(self, otherId)   # solid overlap started
function OnCollisionExit(self, otherId)    # solid overlap ended
function OnTriggerEnter(self, otherId)     # overlap with a trigger volume started
function OnTriggerExit(self, otherId)      # ... ended
```

`self` is the same writable transform `Update` receives (a hook may move or rotate its entity) and
`otherId` is the id of the other entity. A pair counts as a trigger pair as soon as either collider
is a trigger, so a trigger never fires `OnCollisionEnter`. Enter fires on the frame the pair appears,
exit on the frame it disappears; a pair that stays put does not repeat.

```
function OnTriggerEnter(self, otherId)
    self.rotation = self.rotation + 45
    return 0
end function

function Update(self, dt)
    return 0
end function
```

### Physics queries (`using Physics`)

| Function | Result |
| --- | --- |
| `Overlaps(idA, idB)` | `1` when the two colliders overlap right now, else `0` |
| `OverlapPoint(x, y)` | id of the topmost entity whose collider contains the world point, `0` when none |
| `IsOverlapping(id)` | `1` when the entity was part of an overlap in the last collision pass |

```
using Physics

function Update(self, dt)
    if(Overlaps(1, 2))
        self.x = self.x + 1
    end if
    if(IsOverlapping(self))
        self.rotation = self.rotation + OverlapPoint(0, 0)
    end if
    return 0
end function
```

`Overlaps` and `OverlapPoint` read the live transforms, so they see the moves a script made earlier
in the same frame; `IsOverlapping` reports the set the previous collision pass left behind. Note
GScript spells equality with a single `=` when one is needed inside a condition.

### Scene access (`using Scene`)

A script sees the whole scene through the `Scene` library: lookup, spawning, deferred destruction
and the camera. Every position in this API is **world space**, so `GetX` of a child entity returns
what `SetPos` wrote, not the child's local value.

| Function | Returns | Meaning |
| --- | --- | --- |
| `FindByName(name)` | Int | id of the first entity with that name, `0` when there is none |
| `Spawn(name, x, y)` | Int | id of a fresh entity. An existing entity with that name becomes the template: its whole subtree (components, colliders, scripts) is duplicated with new ids and the copy is named `<name> Copy`. With no such entity, an empty entity with that name is created. |
| `SpawnPrefab(path, x, y)` | Int | id of the root instantiated from a `.gprefab.json`, `0` when the file cannot be loaded |
| `Destroy(id)` | Int | `1` when the entity (and its children) was queued, `0` for an unknown id |
| `GetX(id)`, `GetY(id)` | Float | world position |
| `SetPos(id, x, y)` | Int | moves the entity to a world position (converted into its parent's space) |
| `SetVisible(id, visible)` | Int | `1` shows, `0` hides |
| `GetName(id)` | String | entity name, empty for an unknown id |
| `SetCameraPos(x, y)` | Int | centres the camera on a world position |
| `SetCameraZoom(zoom)` | Int | viewport pixels per world unit (clamped above zero) |
| `GetCameraX()`, `GetCameraY()` | Float | world position the camera is centred on |
| `FindByTag(tag)`, `FindAllByTag(tag)`, `GetTaggedEntity(tag, i)` | Int | tag lookups (see Expanded components) |
| `HasTag(id, tag)`, `AddTag(id, tag)` | Int | tag queries and mutation |
| `GetProp(id, key)`, `SetProp(id, key, value)` | String / Int | custom string properties |

`Destroy` never invalidates the frame it was called in: the entity is removed at the end of the tick,
after every `Update` has run, so a script may destroy entities other scripts still reference (or
itself) while it iterates. Its `OnDestroy` hook runs first. `Spawn` creates the entity immediately
but compiles and `Start`s its script at the end of the same tick, so a spawned entity's `Update` runs
from the next frame on.

```
using Scene

function Update(self, dt)
    if(IsKeyPressed("Space"))
        Spawn("Bullet", GetX(self), GetY(self))
    end if
    SetCameraPos(GetX(self), GetY(self))
    return 0
end function
```

The camera a script moves is the one `SceneRuntime::Render()` draws with, so
`SetCameraPos`/`SetCameraZoom` are all a follow-cam needs; play-in-editor copies the same camera into
the viewport whenever a script changed it.

### Sound (`using Audio`)

| Function | Returns | Meaning |
| --- | --- | --- |
| `PlaySound(path)` | Int | `1` when playback started, `0` when Gorgon::Audio has no device or the file cannot be decoded |

The path is resolved against the scene directory like any other asset, and every call is recorded in
`PlaySession::GetPlayedSounds()` / `SceneRuntime::GetPlayedSounds()` whether or not a device was
available — that is what makes the hook testable in a headless run.

```
using Audio

function OnCollisionEnter(self, otherId)
    PlaySound("audio/hit.wav")
    return 0
end function

function Update(self, dt)
    return 0
end function
```

**Call it as a statement.** A function call on the right-hand side of a `self.<member> = …`
assignment is evaluated *twice* by GScript (once while the assignment is resolved, once when the
result is converted), so `self.rotation = PlaySound("hit.wav")` plays the sound twice. Store the
result in a script variable first (`v = PlaySound(...)` then `self.rotation = v`) or just ignore the
return value.

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

GorgonEditor.exe --selftest      rem camera, hit-testing, drag/snap, copy/paste, prefabs, JSON round-trip, asset browser
GorgonEditor.exe --smoke         rem real window: hierarchy/inspector sync, undo/redo, play-in-editor
EditorDemo.exe --selftest        rem SceneRuntime: load, path resolution, script ticking
```

The Catch2 suite covers the script runtime too: `[Lifecycle]` (Start → Update → OnDestroy order,
including a destroyed entity's hook and the Play-stop hooks), `[Scene]` (name lookup, spawn, deferred
destroy, camera, SpawnPrefab) and `[Audio]` (sound calls are recorded with no device present).

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
