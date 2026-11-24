# Gorgon 2D Editor — Architecture & Implementation Plan

A standalone, Unreal/Godot-style 2D scene editor built **with Gorgon 4.2 itself** (`Tools/Editor/`), using Gorgon's native widgets, `Layerbox` viewport, built-in JSON encoder, and GScript VM—no new external dependencies.

## Branch & Build Conventions
- **Repo:** `C:/Users/EfendEEz/projects/Gorgon`
- **Branch:** Create `feat/2d-editor` from `fix/windows-tests` (which has the working Windows MSVC/vcpkg build). Leave untracked `Examples/Simple Games/3- Neon Vortex*` untouched.
- **Build:** `cmake --preset WindowsDefault` / Ninja Multi-Config with `BUILD_EDITOR=ON`.
- **Targets:**
  - `GorgonEditorCore` (static lib): scene model, components, JSON I/O, runtime loader (reusable by games).
  - `GorgonEditor` (executable): standalone 2D editor app linking `Gorgon::Gorgon` and `GorgonEditorCore`.

## UI Layout (Gorgon Native Widgets)
- **Top:** Toolbar (`Button`, `Checkbox`) — New, Open, Save, Play/Pause/Stop, Grid Snap, Undo/Redo.
- **Left:** Scene Hierarchy (`Panel` + `Listbox`/`ListItem`) — entity tree, add/duplicate/delete/reparent, visibility toggle.
- **Center:** Interactive 2D Viewport (`Widgets::Layerbox` + `Graphics::Layer`) — pan, zoom, grid, sprite rendering, selection outline, translate/scale gizmos.
- **Right:** Details / Inspector (`Panel` + `Textbox`, `Numberbox`, `GeometryBoxes`, `ColorPicker`, `Checkbox`, `Dropdown`) — live editing of entity properties & components.
- **Bottom:** Asset Browser (`Panel` + `TabPanel`/`Listbox`) — scans project directory (`Gorgon/Filesystem`) for images, audio, fonts, GScript, and `.gor` resources with image thumbnails.

## Scene Format (`.gscene.json`)
Serialized with `Gorgon/Encoding/JSON.h`:
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
      "collider": { "type": "aabb", "w": 32.0, "h": 32.0, "offsetX": 0.0, "offsetY": 0.0 },
      "script": { "path": "scripts/player.gs" }
    }
  ]
}
```

## Execution Phases (Kanban DAG)
1. **Phase 1 — Scene Data Model & JSON Serialization (`GorgonEditorCore`)**
   - `Scene`, `Entity`, `Transform`, `SpriteComponent`, `ColliderComponent`, `ScriptComponent`.
   - `Gorgon/Encoding/JSON.h` load/save + unit tests for CRUD, hierarchy, and lossless round-trip.
2. **Phase 2 — Editor Shell & Interactive 2D Viewport (`GorgonEditor`)**
   - 4-pane window shell + toolbar + `Layerbox` viewport with camera pan/zoom, grid, sprite drawing, click-select, drag-move, grid snap, and selection gizmo.
3. **Phase 3 — Hierarchy Tree & Details Inspector** *(parallel with Phase 4)*
   - Bidirectional selection sync, entity CRUD/reparenting, live property editing.
4. **Phase 4 — Asset Browser & Insert-to-Scene** *(parallel with Phase 3)*
   - Project folder scanner, image thumbnails, filter box, double-click/insert to spawn sprite entities.
5. **Phase 5 — Undo/Redo, Shortcuts & Play-in-Editor** *(depends on Phases 3 & 4)*
   - Command pattern undo/redo stack, keyboard shortcuts, dirty flag, Play/Pause/Stop snapshot + GScript execution.
6. **Phase 6 — Runtime Scene Loader & Demo Integration** *(depends on Phase 5)*
   - Clean C++ runtime loader API in `GorgonEditorCore`, `Examples/EditorDemo` loading a `.gscene.json` scene, and `Tools/Editor/README.md`.
