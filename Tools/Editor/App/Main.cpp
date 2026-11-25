#include "Core/Scene.h"
#include "Core/SceneRuntime.h"
#include "Core/SceneSerializer.h"
#include "EditorApp.h"
#include "EditorTheme.h"
#include "Viewport.h"

#include <Gorgon/Main.h>
#include <Gorgon/UI.h>
#include <Gorgon/UI/Window.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

    bool Approx(float a, float b, float eps = 1e-2f) {
        return std::abs(a - b) <= eps;
    }

    /// Smallest possible PNG (1x1, transparent) used to give the asset browser a real image
    /// to thumbnail without depending on repository resources being present.
    const unsigned char kTinyPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x08, 0x58, 0xF0, 0xE1,
        0x3F, 0x00, 0x06, 0x04, 0x02, 0xE0, 0x18, 0x2E, 0x14, 0x56, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };

    bool WriteTinyPng(const std::filesystem::path &path) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if(!out) return false;
        out.write(reinterpret_cast<const char *>(kTinyPng), sizeof(kTinyPng));
        return static_cast<bool>(out);
    }

    bool WriteEmptyFile(const std::filesystem::path &path) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        return static_cast<bool>(out);
    }

    int RunAssetBrowserSelfTest();
    int RunMultiSelectSelfTest();
    int RunCopyPasteSelfTest();
    int RunPrefabSelfTest();
    int RunInputSelfTest();

    int RunSelfTest() {
        using namespace Gorgon::Editor;

        // 1. Camera transforms, pan, cursor-anchored zoom, reset
        ViewportCamera cam;
        const float W = 800.0f, H = 600.0f;
        auto sc = cam.WorldToScreen(0.0f, 0.0f, W, H);
        if(!Approx(sc.X, 400.0f) || !Approx(sc.Y, 300.0f)) {
            std::cerr << "FAIL: origin WorldToScreen mismatch\n";
            return 1;
        }

        const float anchorX = 600.0f, anchorY = 200.0f;
        auto worldBefore = cam.ScreenToWorld(anchorX, anchorY, W, H);
        cam.ZoomAt(anchorX, anchorY, W, H, 2.0f);
        auto worldAfter = cam.ScreenToWorld(anchorX, anchorY, W, H);
        if(!Approx(cam.zoom, 2.0f) || !Approx(worldBefore.X, worldAfter.X) || !Approx(worldBefore.Y, worldAfter.Y)) {
            std::cerr << "FAIL: cursor-anchored zoom shifted anchor point\n";
            return 1;
        }

        cam.PanByScreenDelta(40.0f, -20.0f);
        cam.Reset();
        if(!Approx(cam.panX, 0.0f) || !Approx(cam.panY, 0.0f) || !Approx(cam.zoom, 1.0f)) {
            std::cerr << "FAIL: camera reset mismatch\n";
            return 1;
        }

        // 2. Hit-testing (zOrder, visible, locked) & drag-move with grid snap
        Scene scene;
        scene.settings.gridSize = 32;

        {
            Entity &back = scene.CreateEntity("Back");
            back.zOrder = 1;
            back.transform.x = 0.0f;
            back.transform.y = 0.0f;
        }

        EntityId frontId = InvalidEntityId;
        {
            Entity &front = scene.CreateEntity("Front");
            front.zOrder = 5;
            front.transform.x = 0.0f;
            front.transform.y = 0.0f;
            frontId = front.id;
        }

        {
            Entity &lockedTop = scene.CreateEntity("LockedTop");
            lockedTop.zOrder = 10;
            lockedTop.locked = true;
            lockedTop.transform.x = 0.0f;
            lockedTop.transform.y = 0.0f;
        }

        {
            Entity &hiddenTop = scene.CreateEntity("HiddenTop");
            hiddenTop.zOrder = 20;
            hiddenTop.visible = false;
            hiddenTop.transform.x = 0.0f;
            hiddenTop.transform.y = 0.0f;
        }

        EditorViewport vp(scene);
        vp.SetViewportSizeOverride({800, 600});
        vp.SetGridSnap(true);

        // Center of viewport (400, 300) maps to world (0, 0); should pick Front (ignoring locked/hidden)
        if(vp.HitTestEntity(400.0f, 300.0f) != frontId) {
            std::cerr << "FAIL: HitTestEntity did not select highest unlocked visible zOrder\n";
            return 1;
        }

        // Click + drag Front by (45, 18) -> snaps to (32, 32)
        vp.HandleMouseDown({400, 300}, Gorgon::Input::Mouse::Button::Left);
        if(vp.GetSelectedEntity() != frontId) {
            std::cerr << "FAIL: HandleMouseDown did not select Front\n";
            return 1;
        }
        vp.HandleMouseMove({445, 318});
        vp.HandleMouseUp({445, 318}, Gorgon::Input::Mouse::Button::Left);
        const Entity *frontAfter = scene.FindEntity(frontId);
        if(!frontAfter || !Approx(frontAfter->transform.x, 32.0f) || !Approx(frontAfter->transform.y, 32.0f)) {
            std::cerr << "FAIL: drag-move with grid snap got ("
                      << (frontAfter ? frontAfter->transform.x : -999.0f) << ", "
                      << (frontAfter ? frontAfter->transform.y : -999.0f) << ")\n";
            return 1;
        }

        // 2b. Rotation gizmo hit-test, 15-deg snapped drag, and free drag
        {
            int interactionStarts = 0;
            int interactionEnds = 0;
            vp.OnInteractionChanged = [&](bool active) {
                if(active) ++interactionStarts;
                else ++interactionEnds;
            };

            const auto rotHandle = vp.ComputeRotateHandleScreenPos(*frontAfter, W, H);
            if(vp.HitTestGizmo(rotHandle.X, rotHandle.Y) != GizmoHandle::Rotate) {
                std::cerr << "FAIL: HitTestGizmo did not detect Rotate handle\n";
                return 1;
            }
            // Drag from top (12 o'clock) to right (3 o'clock) -> +90 deg clockwise
            vp.HandleMouseDown({static_cast<int>(std::round(rotHandle.X)), static_cast<int>(std::round(rotHandle.Y))},
                               Gorgon::Input::Mouse::Button::Left);
            vp.HandleMouseMove({492, 332});
            vp.HandleMouseUp({492, 332}, Gorgon::Input::Mouse::Button::Left);
            if(!Approx(frontAfter->transform.rotation, 90.0f) || interactionStarts != 1 || interactionEnds != 1) {
                std::cerr << "FAIL: snapped rotate gizmo got " << frontAfter->transform.rotation
                          << " deg (starts=" << interactionStarts << ", ends=" << interactionEnds << ")\n";
                return 1;
            }

            // Free rotation without 15-deg snapping
            vp.SetGridSnap(false);
            const auto rotHandle90 = vp.ComputeRotateHandleScreenPos(*frontAfter, W, H);
            if(vp.HitTestGizmo(rotHandle90.X, rotHandle90.Y) != GizmoHandle::Rotate) {
                std::cerr << "FAIL: rotated Rotate handle not hit-tested at 90 deg\n";
                return 1;
            }
            vp.HandleMouseDown({static_cast<int>(std::round(rotHandle90.X)), static_cast<int>(std::round(rotHandle90.Y))},
                               Gorgon::Input::Mouse::Button::Left);
            // Center is (432, 332); (472, 362) is atan2(30, 40) ~= 36.87 deg -> 90 + 36.87 = 126.87 deg
            vp.HandleMouseMove({472, 362});
            vp.HandleMouseUp({472, 362}, Gorgon::Input::Mouse::Button::Left);
            if(std::abs(frontAfter->transform.rotation - 126.87f) > 0.2f) {
                std::cerr << "FAIL: free rotate gizmo got " << frontAfter->transform.rotation << " deg\n";
                return 1;
            }
            vp.SetGridSnap(true);
            vp.OnInteractionChanged = nullptr;
        }

        // 3. Space+left-drag pan
        vp.SetSpaceHeld(true);
        vp.HandleMouseDown({100, 100}, Gorgon::Input::Mouse::Button::Left);
        vp.HandleMouseMove({150, 125});
        vp.HandleMouseUp({150, 125}, Gorgon::Input::Mouse::Button::Left);
        vp.SetSpaceHeld(false);
        if(!Approx(vp.GetCamera().panX, -50.0f) || !Approx(vp.GetCamera().panY, -25.0f)) {
            std::cerr << "FAIL: Space+left-drag pan got (" << vp.GetCamera().panX << ", " << vp.GetCamera().panY << ")\n";
            return 1;
        }

        // 4. Scene round-trip via SceneSerializer
        const auto tmpPath = (std::filesystem::temp_directory_path() / "gorgon_editor_phase2_selftest.gscene.json").string();
        std::string err;
        if(!SceneSerializer::SaveToFile(scene, tmpPath, &err)) {
            std::cerr << "FAIL: SaveToFile: " << err << "\n";
            return 1;
        }
        Scene reloaded;
        if(!SceneSerializer::LoadFromFile(tmpPath, reloaded, &err)) {
            std::cerr << "FAIL: LoadFromFile: " << err << "\n";
            return 1;
        }
        std::filesystem::remove(tmpPath);
        if(reloaded != scene) {
            std::cerr << "FAIL: reloaded scene mismatch\n";
            return 1;
        }

        if(const int multiResult = RunMultiSelectSelfTest()) {
            return multiResult;
        }

        if(const int copyPasteResult = RunCopyPasteSelfTest()) {
            return copyPasteResult;
        }

        if(const int assetResult = RunAssetBrowserSelfTest()) {
            return assetResult;
        }

        if(const int prefabResult = RunPrefabSelfTest()) {
            return prefabResult;
        }

        if(const int inputResult = RunInputSelfTest()) {
            return inputResult;
        }

        std::cout << "GorgonEditor --selftest PASSED (camera, hit-test, grid-snap drag, pan, multi-select, copy/paste, prefabs, script input, round-trip, asset browser)\n";
        return 0;
    }

    /// Multi-select: Shift-click, marquee box select, group drag + single undo,
    /// group duplicate + single undo, and group delete + single undo.
    int RunMultiSelectSelfTest() {
        using namespace Gorgon::Editor;
        using Btn = Gorgon::Input::Mouse::Button;

        Scene scene;
        scene.settings.gridSize = 32;

        const EntityId idA = [&]() {
            Entity &e = scene.CreateEntity("A");
            e.zOrder = 1;
            e.transform.x = -128.0f;
            e.transform.y = 0.0f;
            return e.id;
        }();
        const EntityId idB = [&]() {
            Entity &e = scene.CreateEntity("B");
            e.zOrder = 2;
            e.transform.x = 0.0f;
            e.transform.y = 0.0f;
            return e.id;
        }();
        const EntityId idC = [&]() {
            Entity &e = scene.CreateEntity("C");
            e.zOrder = 3;
            e.transform.x = 128.0f;
            e.transform.y = 0.0f;
            return e.id;
        }();
        const EntityId idLocked = [&]() {
            Entity &e = scene.CreateEntity("Locked");
            e.zOrder = 10;
            e.locked = true;
            e.transform.x = -128.0f;
            e.transform.y = 0.0f;
            return e.id;
        }();
        const EntityId idHidden = [&]() {
            Entity &e = scene.CreateEntity("Hidden");
            e.zOrder = 20;
            e.visible = false;
            e.transform.x = 0.0f;
            e.transform.y = 0.0f;
            return e.id;
        }();

        EditorViewport vp(scene);
        vp.SetViewportSizeOverride({800, 600});
        vp.SetGridSnap(true);

        CommandStack commands;
        Scene lastCommitted = scene;
        bool gestureActive = false;
        bool gesturePushed = false;
        auto recordEdit = [&](const std::string &name, bool merge = false) {
            if(scene == lastCommitted) return;
            commands.Push(std::unique_ptr<Command>(
                new SceneSnapshotCommand(name, lastCommitted, scene)), merge);
            lastCommitted = scene;
        };
        vp.OnInteractionChanged = [&](bool active) {
            gestureActive = active;
            if(active) gesturePushed = false;
        };
        vp.OnSceneModified = [&]() {
            recordEdit(gestureActive ? "Move Entity" : "Viewport Edit", gestureActive && gesturePushed);
            if(gestureActive) gesturePushed = true;
        };

        const float W = 800.0f, H = 600.0f;
        auto toScreen = [&](float wx, float wy) {
            const auto p = vp.GetCamera().WorldToScreen(wx, wy, W, H);
            return Gorgon::Geometry::Point{static_cast<int>(std::round(p.X)), static_cast<int>(std::round(p.Y))};
        };

        // 1. Shift-click toggles selection; plain click replaces; empty click clears
        const auto ptA = toScreen(-128.0f, 0.0f);
        const auto ptB = toScreen(0.0f, 0.0f);
        const auto ptC = toScreen(128.0f, 0.0f);

        vp.HandleMouseDown(ptA, Btn::Left);
        vp.HandleMouseUp(ptA, Btn::Left);
        if(vp.GetSelectionCount() != 1 || vp.GetSelectedEntity() != idA) {
            std::cerr << "FAIL: plain click did not select A\n";
            return 1;
        }

        vp.SetShiftHeld(true);
        vp.HandleMouseDown(ptB, Btn::Left);
        vp.HandleMouseUp(ptB, Btn::Left);
        if(vp.GetSelectionCount() != 2 || !vp.IsEntitySelected(idA) || !vp.IsEntitySelected(idB)) {
            std::cerr << "FAIL: Shift-click did not add B to selection\n";
            return 1;
        }

        vp.HandleMouseDown(ptA, Btn::Left);
        vp.HandleMouseUp(ptA, Btn::Left);
        vp.SetShiftHeld(false);
        if(vp.GetSelectionCount() != 1 || vp.IsEntitySelected(idA) || !vp.IsEntitySelected(idB)) {
            std::cerr << "FAIL: Shift-click did not toggle A out of selection\n";
            return 1;
        }

        // Plain click on empty space clears selection
        vp.HandleMouseDown({20, 20}, Btn::Left);
        vp.HandleMouseUp({20, 20}, Btn::Left);
        if(vp.GetSelectionCount() != 0 || vp.GetSelectedEntity() != InvalidEntityId) {
            std::cerr << "FAIL: empty space click did not clear selection\n";
            return 1;
        }

        // 2. Marquee box select covers A & B (ignoring Locked & Hidden, excluding C)
        const auto mStart = toScreen(-180.0f, -60.0f);
        const auto mEnd   = toScreen(50.0f, 60.0f);
        vp.HandleMouseDown(mStart, Btn::Left);
        vp.HandleMouseMove(mEnd);
        vp.HandleMouseUp(mEnd, Btn::Left);
        if(vp.GetSelectionCount() != 2 || !vp.IsEntitySelected(idA) || !vp.IsEntitySelected(idB) ||
           vp.IsEntitySelected(idC) || vp.IsEntitySelected(idLocked) || vp.IsEntitySelected(idHidden)) {
            std::cerr << "FAIL: marquee select expected {A, B}, got count=" << vp.GetSelectionCount() << "\n";
            return 1;
        }

        // 2b. Shift+marquee adds C to existing selection
        vp.SetShiftHeld(true);
        const auto mcStart = toScreen(80.0f, -60.0f);
        const auto mcEnd   = toScreen(180.0f, 60.0f);
        vp.HandleMouseDown(mcStart, Btn::Left);
        vp.HandleMouseMove(mcEnd);
        vp.HandleMouseUp(mcEnd, Btn::Left);
        vp.SetShiftHeld(false);
        if(vp.GetSelectionCount() != 3 || !vp.IsEntitySelected(idA) || !vp.IsEntitySelected(idB) || !vp.IsEntitySelected(idC)) {
            std::cerr << "FAIL: Shift+marquee did not add C to selection\n";
            return 1;
        }

        // 3. Group drag moves all selected entities together (with grid snap) in 1 undo step
        vp.HandleMouseDown(ptA, Btn::Left);
        vp.HandleMouseMove({ptA.X + 30, ptA.Y + 14});
        vp.HandleMouseMove({ptA.X + 62, ptA.Y + 34});
        vp.HandleMouseUp({ptA.X + 62, ptA.Y + 34}, Btn::Left);
        const Entity *eA = scene.FindEntity(idA);
        const Entity *eB = scene.FindEntity(idB);
        const Entity *eC = scene.FindEntity(idC);
        if(!eA || !eB || !eC ||
           !Approx(eA->transform.x, -64.0f) || !Approx(eA->transform.y, 32.0f) ||
           !Approx(eB->transform.x,  64.0f) || !Approx(eB->transform.y, 32.0f) ||
           !Approx(eC->transform.x, 192.0f) || !Approx(eC->transform.y, 32.0f)) {
            std::cerr << "FAIL: group drag positions mismatch\n";
            return 1;
        }
        if(commands.GetUndoCount() != 1) {
            std::cerr << "FAIL: group drag should collapse to 1 undo step, got " << commands.GetUndoCount() << "\n";
            return 1;
        }
        if(!commands.Undo(scene)) {
            std::cerr << "FAIL: group drag undo failed\n";
            return 1;
        }
        lastCommitted = scene;
        eA = scene.FindEntity(idA);
        eB = scene.FindEntity(idB);
        eC = scene.FindEntity(idC);
        if(!eA || !eB || !eC ||
           !Approx(eA->transform.x, -128.0f) || !Approx(eA->transform.y, 0.0f) ||
           !Approx(eB->transform.x,    0.0f) || !Approx(eB->transform.y, 0.0f) ||
           !Approx(eC->transform.x,  128.0f) || !Approx(eC->transform.y, 0.0f)) {
            std::cerr << "FAIL: group drag undo did not restore all entities\n";
            return 1;
        }

        // 4. Group duplicate creates copies for all selected entities in 1 undo step
        vp.SetSelectedEntities({idA, idB}, false);
        const std::size_t countBeforeDup = scene.GetEntityCount();
        const auto dupIds = vp.DuplicateSelectedEntities(false);
        recordEdit("Duplicate Entities");
        if(dupIds.size() != 2 || scene.GetEntityCount() != countBeforeDup + 2 ||
           vp.GetSelectionCount() != 2 || vp.GetSelectedEntities() != dupIds) {
            std::cerr << "FAIL: group duplicate mismatch\n";
            return 1;
        }
        if(commands.GetUndoCount() != 1) {
            std::cerr << "FAIL: group duplicate expected 1 undo step, got " << commands.GetUndoCount() << "\n";
            return 1;
        }
        if(!commands.Undo(scene) || scene.GetEntityCount() != countBeforeDup) {
            std::cerr << "FAIL: group duplicate undo did not remove copies\n";
            return 1;
        }
        if(!commands.Redo(scene) || scene.GetEntityCount() != countBeforeDup + 2) {
            std::cerr << "FAIL: group duplicate redo did not restore copies\n";
            return 1;
        }
        lastCommitted = scene;

        // 5. Group delete removes all selected entities in 1 undo step
        vp.SetSelectedEntities(dupIds, false);
        if(!vp.DeleteSelectedEntities(false)) {
            std::cerr << "FAIL: group delete returned false\n";
            return 1;
        }
        recordEdit("Delete Entities");
        if(scene.GetEntityCount() != countBeforeDup || vp.GetSelectionCount() != 0) {
            std::cerr << "FAIL: group delete did not remove selected entities and clear selection\n";
            return 1;
        }
        if(commands.GetUndoCount() != 2) {
            std::cerr << "FAIL: group delete expected 2 total undo steps, got " << commands.GetUndoCount() << "\n";
            return 1;
        }
        if(!commands.Undo(scene) || scene.GetEntityCount() != countBeforeDup + 2) {
            std::cerr << "FAIL: group delete undo did not restore deleted entities\n";
            return 1;
        }

        return 0;
    }

    /// Copy/paste (Ctrl+C / Ctrl+V): single + multi-select subtree JSON clipboard,
    /// remapped parentIds, unique names, root offset, selection update, and single-step undo.
    int RunCopyPasteSelfTest() {
        using namespace Gorgon::Editor;

        Scene scene;
        scene.settings.gridSize = 32;

        Entity &hero = scene.CreateEntity("Hero");
        const EntityId heroId = hero.id;
        hero.transform = {64.0f, 32.0f, 1.0f, 1.0f, 0.0f};
        hero.sprite = SpriteComponent{"hero.png", {255, 255, 255, 255}};

        Entity &shield = scene.CreateEntity("Shield", heroId);
        const EntityId shieldId = shield.id;
        shield.transform = {16.0f, 0.0f, 1.0f, 1.0f, 0.0f};
        shield.collider = ColliderComponent{ColliderType::Circle, 0.0f, 0.0f, 12.0f, 0.0f, 0.0f};

        Entity &crate = scene.CreateEntity("Crate");
        const EntityId crateId = crate.id;
        crate.transform = {-64.0f, 0.0f, 1.0f, 1.0f, 0.0f};

        EditorViewport vp(scene);
        vp.SetViewportSizeOverride({800, 600});
        vp.SetGridSnap(true);

        CommandStack commands;
        Scene lastCommitted = scene;
        auto recordEdit = [&](const std::string &name) {
            if(scene == lastCommitted) return;
            commands.Push(std::unique_ptr<Command>(
                new SceneSnapshotCommand(name, lastCommitted, scene)), false);
            lastCommitted = scene;
        };

        // 1. Copy multi-select (Hero + Crate, where Hero owns Shield) and paste in 1 undo step
        vp.SetSelectedEntities({heroId, crateId}, false);
        const std::string clip = SceneSerializer::EntitiesToJsonString(scene, vp.GetSelectedEntities(), 2);
        const std::size_t countBefore = scene.GetEntityCount();
        std::vector<EntityId> pastedRoots;
        std::string err;
        const float offset = static_cast<float>(scene.settings.gridSize);
        if(!SceneSerializer::PasteEntitiesFromJsonString(clip, scene, &pastedRoots, offset, offset, &err) ||
           pastedRoots.size() != 2 || scene.GetEntityCount() != countBefore + 3) {
            std::cerr << "FAIL: copy/paste subtree did not add expected entities: " << err << "\n";
            return 1;
        }
        vp.SetSelectedEntities(pastedRoots, false);
        recordEdit("Paste Entities");

        const Entity *pHero = scene.FindEntity(pastedRoots[0]);
        const Entity *pCrate = scene.FindEntity(pastedRoots[1]);
        if(!pHero || !pCrate || pHero->id == heroId || pCrate->id == crateId ||
           pHero->name != "Hero Copy" || pCrate->name != "Crate Copy" ||
           !Approx(pHero->transform.x, 96.0f) || !Approx(pHero->transform.y, 64.0f) ||
           !Approx(pCrate->transform.x, -32.0f) || !Approx(pCrate->transform.y, 32.0f)) {
            std::cerr << "FAIL: pasted root names, IDs, or offsets mismatch\n";
            return 1;
        }
        const auto pHeroChildren = scene.GetChildIds(pHero->id);
        if(pHeroChildren.size() != 1 || pHeroChildren[0] == shieldId) {
            std::cerr << "FAIL: pasted subtree child was not remapped to pasted root\n";
            return 1;
        }
        const Entity *pShield = scene.FindEntity(pHeroChildren[0]);
        if(!pShield || pShield->parentId != pHero->id ||
           !Approx(pShield->transform.x, 16.0f) || !Approx(pShield->transform.y, 0.0f)) {
            std::cerr << "FAIL: pasted child parentId or local transform mismatch\n";
            return 1;
        }
        if(vp.GetSelectionCount() != 2 || vp.GetSelectedEntities() != pastedRoots) {
            std::cerr << "FAIL: paste did not select newly pasted root entities\n";
            return 1;
        }
        if(commands.GetUndoCount() != 1) {
            std::cerr << "FAIL: paste should be 1 undo step, got " << commands.GetUndoCount() << "\n";
            return 1;
        }
        if(!commands.Undo(scene) || scene.GetEntityCount() != countBefore) {
            std::cerr << "FAIL: paste undo did not remove all pasted entities\n";
            return 1;
        }
        if(!commands.Redo(scene) || scene.GetEntityCount() != countBefore + 3) {
            std::cerr << "FAIL: paste redo did not restore pasted entities\n";
            return 1;
        }

        return 0;
    }

    /// Prefabs: save a subtree to .gprefab.json, reload it, instantiate with fresh IDs at a
    /// target position, verify the prefab link survives a scene round-trip, and revert.
    int RunPrefabSelfTest() {
        using namespace Gorgon::Editor;

        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_prefab_selftest";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);

        Scene scene;
        scene.settings.gridSize = 32;

        Entity &ship = scene.CreateEntity("Ship");
        ship.transform.x = 300.0f;
        ship.transform.y = 200.0f;
        ship.transform.rotation = 45.0f;
        ship.sprite = SpriteComponent{"ship.png", {120, 200, 255, 255}};
        const EntityId shipId = ship.id;

        Entity &wing = scene.CreateEntity("WingL", shipId);
        wing.transform.x = -20.0f;
        const EntityId wingId = wing.id;

        Entity &gun = scene.CreateEntity("Gun", wingId);
        gun.transform.x = -8.0f;
        gun.transform.y = 6.0f;
        const EntityId gunId = gun.id;

        scene.CreateEntity("Bystander");

        const auto prefabPath = (dir / "ship.gprefab.json").string();
        std::string error;
        if(!SceneSerializer::SavePrefabToFile(scene, shipId, prefabPath, &error)) {
            std::cerr << "FAIL: prefab save: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        PrefabData prefab;
        if(!SceneSerializer::LoadPrefabFromFile(prefabPath, prefab, &error)) {
            std::cerr << "FAIL: prefab load: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(!prefab.IsValid() || prefab.entities.size() != 3 || prefab.name != "Ship") {
            std::cerr << "FAIL: prefab contents wrong (" << prefab.entities.size() << " entities)\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // Root is stored at the local origin; children keep their offsets.
        const Entity *prefabRoot = nullptr;
        for(const auto &e : prefab.entities) {
            if(e.id == prefab.rootId) prefabRoot = &e;
        }
        if(!prefabRoot || !Approx(prefabRoot->transform.x, 0.0f) || !Approx(prefabRoot->transform.y, 0.0f) ||
           !Approx(prefabRoot->transform.rotation, 45.0f)) {
            std::cerr << "FAIL: prefab root transform not normalized\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // Instantiate into a fresh scene at a target position.
        Scene target;
        EntityId instRoot = InvalidEntityId;
        std::vector<EntityId> created;
        if(!SceneSerializer::InstantiatePrefab(prefab, target, 128.0f, -64.0f, "ship.gprefab.json",
                                               &instRoot, &created, &error)) {
            std::cerr << "FAIL: prefab instantiate: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        const Entity *instRootEntity = target.FindEntity(instRoot);
        if(!instRootEntity || created.size() != 3 || target.GetEntityCount() != 3) {
            std::cerr << "FAIL: prefab instance did not create the full subtree\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(!Approx(instRootEntity->transform.x, 128.0f) || !Approx(instRootEntity->transform.y, -64.0f)) {
            std::cerr << "FAIL: prefab instance placed at ("
                      << instRootEntity->transform.x << ", " << instRootEntity->transform.y << ")\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(instRootEntity->prefabSource != "ship.gprefab.json") {
            std::cerr << "FAIL: prefab instance missing prefabSource (got '"
                      << instRootEntity->prefabSource << "')\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // Subtree links are remapped onto the new IDs.
        const auto instChildren = target.GetChildIds(instRoot);
        if(instChildren.size() != 1) {
            std::cerr << "FAIL: prefab instance root has " << instChildren.size() << " children\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        const auto instGrandchildren = target.GetChildIds(instChildren.front());
        if(instGrandchildren.size() != 1) {
            std::cerr << "FAIL: prefab instance child has " << instGrandchildren.size() << " children\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        const Entity *instGun = target.FindEntity(instGrandchildren.front());
        if(!instGun || instGun->name != "Gun" || !Approx(instGun->transform.y, 6.0f)) {
            std::cerr << "FAIL: prefab descendant transform not preserved\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // The prefab link survives a scene save/load round-trip.
        const auto scenePath = (dir / "prefab_scene.gscene.json").string();
        if(!SceneSerializer::SaveToFile(target, scenePath, &error) ||
           !SceneSerializer::LoadFromFile(scenePath, target, &error)) {
            std::cerr << "FAIL: prefab scene round-trip: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        Entity *roundTripped = target.FindEntity(instRoot);
        if(!roundTripped || roundTripped->prefabSource != "ship.gprefab.json") {
            std::cerr << "FAIL: prefabSource lost in scene round-trip\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // Revert restores the prefab contents but keeps placement, ID and link.
        roundTripped->sprite->tint = {0, 0, 0, 255};
        roundTripped->transform.y = 777.0f;
        if(!SceneSerializer::RevertPrefabInstance(target, instRoot, prefab, &error)) {
            std::cerr << "FAIL: prefab revert: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        const Entity *reverted = target.FindEntity(instRoot);
        if(!reverted || !Approx(reverted->transform.y, 777.0f) ||
           reverted->prefabSource != "ship.gprefab.json" ||
           reverted->sprite->tint != (ColorRGBA{120, 200, 255, 255})) {
            std::cerr << "FAIL: prefab revert did not restore contents while keeping placement\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // The asset browser classifies .gprefab.json as a prefab asset.
        if(GetAssetTypeForExtension("gprefab.json") != AssetType::Prefabs ||
           GetAssetTypeForExtension("gprefab") != AssetType::Prefabs) {
            std::cerr << "FAIL: .gprefab.json is not classified as a prefab asset\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        AssetLibrary library;
        if(!library.SetRoot(dir.generic_string(), &error) ||
           library.GetVisibleCount() != 1) {
            std::cerr << "FAIL: asset browser did not list the prefab file ("
                      << library.GetVisibleCount() << " entries)\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        library.SetFilter(AssetType::Prefabs);
        if(library.GetVisibleCount() != 1 || library.GetVisible()[0].type != AssetType::Prefabs) {
            std::cerr << "FAIL: Prefabs filter did not isolate the .gprefab.json file\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        std::filesystem::remove_all(dir, ec);
        return 0;
    }

    /// Script input: a real .gs script driven by an InputState the test fills by hand, through both
    /// PlaySession (play-in-editor) and SceneRuntime (game side).
    int RunInputSelfTest() {
        using namespace Gorgon::Editor;

        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_input_selftest";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);

        {
            std::ofstream out(dir / "input_move.gs", std::ios::binary | std::ios::trunc);
            if(!out.good()) {
                std::cerr << "FAIL: could not write the input test script\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            out << "using Input\n"
                   "function Update(self, dt)\n"
                   "    if(IsKeyDown(\"d\"))\n"
                   "        self.x = self.x + 200 * dt\n"
                   "    end if\n"
                   "    if(IsKeyPressed(\"Space\"))\n"
                   "        self.y = self.y + 10\n"
                   "    end if\n"
                   "    if(IsKeyReleased(\"A\"))\n"
                   "        self.y = self.y - 5\n"
                   "    end if\n"
                   "    if(IsMouseDown(\"Left\"))\n"
                   "        self.x = MouseX()\n"
                   "        self.y = MouseY()\n"
                   "    end if\n"
                   "    return 0\n"
                   "end function\n";
        }

        // ---- PlaySession -----------------------------------------------------
        {
            Scene scene;
            Entity &entity = scene.CreateEntity("Mover");
            entity.transform.x = 10.0f;
            entity.transform.y = 20.0f;
            entity.script = ScriptComponent{"input_move.gs"};
            const EntityId id = entity.id;

            PlaySession session;
            std::string error;
            if(!session.Start(scene, dir.generic_string(), &error)) {
                std::cerr << "FAIL: script using Input did not start: " << error << "\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            if(session.GetScriptCount() != 1) {
                std::cerr << "FAIL: input selftest compiled " << session.GetScriptCount() << " scripts\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            const auto x = [&]() { return scene.FindEntity(id)->transform.x; };
            const auto y = [&]() { return scene.FindEntity(id)->transform.y; };

            session.Tick(scene, 0.25f);
            if(!Approx(x(), 10.0f) || !Approx(y(), 20.0f)) {
                std::cerr << "FAIL: script moved the entity without input\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            // Held key: "D" is fed, the script asks for "d".
            session.GetInput().SetKeyDown("D", true);
            session.Tick(scene, 0.25f);
            if(!Approx(x(), 60.0f)) {
                std::cerr << "FAIL: IsKeyDown did not move the entity (x=" << x() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            session.Tick(scene, 0.25f);
            if(!Approx(x(), 110.0f)) {
                std::cerr << "FAIL: held key did not keep moving the entity (x=" << x() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            // Pressed is a one-frame edge.
            session.GetInput().SetKeyDown("Space", true);
            session.Tick(scene, 0.0f);
            if(!Approx(y(), 30.0f)) {
                std::cerr << "FAIL: IsKeyPressed did not fire (y=" << y() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            if(session.GetInput().IsKeyPressed("Space")) {
                std::cerr << "FAIL: pressed edge was not cleared by Tick\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            session.Tick(scene, 0.0f);
            if(!Approx(y(), 30.0f)) {
                std::cerr << "FAIL: IsKeyPressed fired twice (y=" << y() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            // Released is an edge too, and an unmatched release is not one.
            session.GetInput().SetKeyDown("A", false);
            session.Tick(scene, 0.0f);
            if(!Approx(y(), 30.0f)) {
                std::cerr << "FAIL: unmatched release produced an edge (y=" << y() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            session.GetInput().SetKeyDown("A", true);
            session.Tick(scene, 0.0f);
            session.GetInput().SetKeyDown("A", false);
            session.Tick(scene, 0.0f);
            if(!Approx(y(), 25.0f)) {
                std::cerr << "FAIL: IsKeyReleased did not fire (y=" << y() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            // Mouse: world position plus button state.
            session.GetInput().SetMousePosition(320.0f, -64.0f);
            session.GetInput().SetMouseButtonDown("Left", true);
            session.Tick(scene, 0.0f);
            if(!Approx(x(), 320.0f) || !Approx(y(), -64.0f)) {
                std::cerr << "FAIL: MouseX/MouseY did not reach the script ("
                          << x() << ", " << y() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            session.GetInput().SetMouseButtonDown("Left", false);
            session.Tick(scene, 0.25f);
            if(!Approx(x(), 370.0f)) {
                std::cerr << "FAIL: IsMouseDown stayed true after release (x=" << x() << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            session.Stop(scene);
            if(!Approx(scene.FindEntity(id)->transform.x, 10.0f)) {
                std::cerr << "FAIL: stop did not restore the scene\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
        }

        // ---- SceneRuntime ----------------------------------------------------
        {
            Scene scene;
            Entity &hero = scene.CreateEntity("Hero");
            hero.script = ScriptComponent{"input_move.gs"};
            const EntityId id = hero.id;

            SceneRuntime runtime;
            std::string error;
            if(!runtime.LoadFromScene(scene, dir.generic_string(), &error) || runtime.GetScriptCount() != 1) {
                std::cerr << "FAIL: runtime did not start the input script: " << error << "\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            runtime.GetInput().SetKeyDown("Right", true);   // not used by the script
            runtime.GetInput().SetKeyDown("D", true);
            runtime.Update(0.25f);
            runtime.Update(0.25f);
            if(!Approx(runtime.GetScene().FindEntity(id)->transform.x, 100.0f)) {
                std::cerr << "FAIL: runtime held key did not move the entity (x="
                          << runtime.GetScene().FindEntity(id)->transform.x << ")\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            runtime.GetInput().SetKeyDown("Space", true);
            runtime.Update(0.0f);
            if(!Approx(runtime.GetScene().FindEntity(id)->transform.y, 10.0f)) {
                std::cerr << "FAIL: runtime press edge did not reach the script\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            if(runtime.GetInput().IsKeyPressed("Space")) {
                std::cerr << "FAIL: runtime did not consume the press edge\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            runtime.Update(0.0f);
            if(!Approx(runtime.GetScene().FindEntity(id)->transform.y, 10.0f)) {
                std::cerr << "FAIL: runtime press edge fired twice\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
        }

        // ---- Name translation ------------------------------------------------
        namespace Keys = Gorgon::Input::Keyboard::Keycodes;
        if(KeyNameFromCode(Keys::W) != "w" || KeyNameFromCode(Keys::Left) != "left" ||
           KeyNameFromCode(Keys::Down) != "down" || KeyNameFromCode(Keys::Enter) != "enter" ||
           KeyNameFromCode(Keys::Escape) != "escape") {
            std::cerr << "FAIL: keycode to name translation is wrong\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(MouseButtonName(Gorgon::Input::Mouse::Button::Left) != "left" ||
           MouseButtonName(Gorgon::Input::Mouse::Button::Right) != "right" ||
           MouseButtonName(Gorgon::Input::Mouse::Button::Middle) != "middle") {
            std::cerr << "FAIL: mouse button to name translation is wrong\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(NormalizeInputName("Return") != "enter" || NormalizeInputName(" D ") != "d" ||
           NormalizeInputName("ArrowUp") != "up") {
            std::cerr << "FAIL: input name normalization is wrong\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(KeyNameFromCode(0x7FFFFF).empty() == false) {
            std::cerr << "FAIL: unknown keycode produced a name\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        std::filesystem::remove_all(dir, ec);
        return 0;
    }

    /// Phase 4: asset scanning, filtering, search, path handling and insert-to-scene,
    /// all without a window so it runs before Gorgon::Initialize.
    int RunAssetBrowserSelfTest() {
        using namespace Gorgon::Editor;

        const auto root = std::filesystem::temp_directory_path() / "gorgon_editor_phase4_selftest";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "images", ec);
        std::filesystem::create_directories(root / "audio", ec);
        std::filesystem::create_directories(root / "scripts", ec);

        const bool wroteAssets =
            WriteTinyPng(root / "root.png") &&
            WriteTinyPng(root / "images" / "hero.png") &&
            WriteTinyPng(root / "images" / "UPPER.PNG") &&
            WriteEmptyFile(root / "audio" / "beep.wav") &&
            WriteEmptyFile(root / "scripts" / "spin.gs") &&
            WriteEmptyFile(root / "scripts" / "move.gscript") &&
            WriteEmptyFile(root / "scripts" / "notes.txt");

        if(!wroteAssets) {
            std::cerr << "FAIL: could not create the Phase 4 test asset tree\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        AssetLibrary library;
        std::string error;
        if(!library.SetRoot(root.generic_string(), &error)) {
            std::cerr << "FAIL: AssetLibrary::SetRoot: " << error << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Root listing: 3 folders + root.png; notes.txt is not a supported asset type.
        if(library.GetVisibleCount() != 4) {
            std::cerr << "FAIL: root listing expected 4 entries, got " << library.GetVisibleCount() << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        const auto &rootEntries = library.GetVisible();
        const int directoryCount = static_cast<int>(std::count_if(rootEntries.begin(), rootEntries.end(),
            [](const AssetEntry &e) { return e.isDirectory; }));
        if(directoryCount != 3) {
            std::cerr << "FAIL: expected 3 folders in the root listing, got " << directoryCount << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(rootEntries[0].name != "audio") {
            std::cerr << "FAIL: folders must be listed first and alphabetically, got " << rootEntries[0].name << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Type classification, including case insensitive extensions.
        if(GetAssetTypeForExtension("png") != AssetType::Images ||
           GetAssetTypeForExtension("PNG") != AssetType::Images ||
           GetAssetTypeForExtension("ogg") != AssetType::Audio ||
           GetAssetTypeForExtension("otf") != AssetType::Fonts ||
           GetAssetTypeForExtension("gscript") != AssetType::Scripts ||
           GetAssetTypeForExtension("gor") != AssetType::Resources ||
           GetAssetTypeForExtension("txt") != AssetType::All ||
           IsSupportedAssetExtension("txt")) {
            std::cerr << "FAIL: extension classification mismatch\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Folder navigation + breadcrumb
        if(!library.NavigateTo("images")) {
            std::cerr << "FAIL: NavigateTo(images) failed\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(library.IsAtRoot() || library.GetVisibleCount() != 2) {
            std::cerr << "FAIL: images folder should list 2 assets, got " << library.GetVisibleCount() << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(library.GetBreadcrumb().find("images") == std::string::npos) {
            std::cerr << "FAIL: breadcrumb missing folder name: " << library.GetBreadcrumb() << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Type filter + text search
        library.SetFilter(AssetType::Images);
        if(library.GetVisibleCount() != 2) {
            std::cerr << "FAIL: images filter lost an entry\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        library.SetFilter(AssetType::Audio);
        if(library.GetVisibleCount() != 0) {
            std::cerr << "FAIL: audio filter matched image assets\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        library.SetFilter(AssetType::Images);
        library.SetSearch("HERO");
        if(library.GetVisibleCount() != 1 || library.GetVisible()[0].name != "hero.png") {
            std::cerr << "FAIL: case insensitive search did not isolate hero.png\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        library.SetSearch("zzz");
        if(library.GetVisibleCount() != 0) {
            std::cerr << "FAIL: search for a missing name returned entries\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        library.SetSearch("");
        library.SetFilter(AssetType::All);

        AssetEntry heroEntry;
        bool hasHero = false;
        for(const auto &entry : library.GetVisible()) {
            if(entry.name == "hero.png") {
                heroEntry = entry;
                hasHero = true;
            }
        }
        if(!hasHero) {
            std::cerr << "FAIL: hero.png missing from the images folder\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(heroEntry.relativePath != "images/hero.png") {
            std::cerr << "FAIL: relative path should be images/hero.png, got " << heroEntry.relativePath << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        if(!library.NavigateUp() || !library.IsAtRoot()) {
            std::cerr << "FAIL: NavigateUp did not return to the root\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Insert to scene: image -> sprite entity at the requested world position
        Scene scene;
        scene.settings.gridSize = 32;
        const EntityId imageEntity = InsertAssetAsEntity(scene, heroEntry, root.generic_string(), 96.0f, -32.0f);
        const Entity *inserted = scene.FindEntity(imageEntity);
        if(!inserted || !inserted->sprite) {
            std::cerr << "FAIL: image asset did not create a sprite entity\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(inserted->sprite->asset != "images/hero.png") {
            std::cerr << "FAIL: inserted sprite path should be relative, got " << inserted->sprite->asset << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(!Approx(inserted->transform.x, 96.0f) || !Approx(inserted->transform.y, -32.0f)) {
            std::cerr << "FAIL: inserted entity landed at the wrong position\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Script assets insert a ScriptComponent; folders and audio are rejected.
        AssetEntry scriptEntry;
        scriptEntry.name = "spin.gs";
        scriptEntry.fullPath = (root / "scripts" / "spin.gs").generic_string();
        scriptEntry.extension = "gs";
        scriptEntry.type = AssetType::Scripts;
        const EntityId scriptEntity = InsertAssetAsEntity(scene, scriptEntry, root.generic_string(), 0.0f, 0.0f);
        const Entity *scriptInserted = scene.FindEntity(scriptEntity);
        if(!scriptInserted || !scriptInserted->script || scriptInserted->script->path != "scripts/spin.gs") {
            std::cerr << "FAIL: script asset did not create a ScriptComponent entity\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        AssetEntry folderEntry;
        folderEntry.name = "images";
        folderEntry.isDirectory = true;
        AssetEntry audioEntry;
        audioEntry.name = "beep.wav";
        audioEntry.extension = "wav";
        audioEntry.type = AssetType::Audio;
        if(InsertAssetAsEntity(scene, folderEntry, root.generic_string(), 0.0f, 0.0f) != InvalidEntityId ||
           InsertAssetAsEntity(scene, audioEntry, root.generic_string(), 0.0f, 0.0f) != InvalidEntityId) {
            std::cerr << "FAIL: folders/audio must not be inserted into the scene\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Inserted sprite path must survive save/load
        const auto scenePath = (root / "phase4.gscene.json").string();
        if(!SceneSerializer::SaveToFile(scene, scenePath, &error)) {
            std::cerr << "FAIL: saving the phase 4 scene: " << error << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        Scene reloaded;
        if(!SceneSerializer::LoadFromFile(scenePath, reloaded, &error)) {
            std::cerr << "FAIL: loading the phase 4 scene: " << error << "\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        const Entity *reloadedImage = reloaded.FindEntity(imageEntity);
        if(!reloadedImage || !reloadedImage->sprite || reloadedImage->sprite->asset != "images/hero.png") {
            std::cerr << "FAIL: sprite asset path did not survive save/load\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // Grid math: tiles are laid out left to right, hit testing respects the padding gaps
        const AssetGridMetrics metrics = ComputeAssetGridMetrics(400, 200, 5);
        if(metrics.columns < 2 || metrics.tileWidth <= 0 || metrics.tileHeight <= 0) {
            std::cerr << "FAIL: grid metrics are degenerate\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        const auto first = GetAssetTileBounds(metrics, 0, 0);
        const auto second = GetAssetTileBounds(metrics, 1, 0);
        if(first.X != metrics.padding || first.Y != metrics.padding || second.X <= first.X) {
            std::cerr << "FAIL: tile layout mismatch\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        if(GetAssetTileAt(metrics, 0, first.X + 2, first.Y + 2) != 0 ||
           GetAssetTileAt(metrics, 0, second.X + 2, second.Y + 2) != 1 ||
           GetAssetTileAt(metrics, 0, 0, 0) != -1) {
            std::cerr << "FAIL: tile hit testing mismatch\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        const auto scrolled = GetAssetTileBounds(metrics, 1, metrics.tileHeight);
        if(scrolled.Y != second.Y - metrics.tileHeight) {
            std::cerr << "FAIL: scroll offset not applied to tile bounds\n";
            std::filesystem::remove_all(root, ec);
            return 1;
        }

        // ---- Drag-and-drop: Asset -> Viewport (grid snap, script attach, preview, single undo) ----
        {
            Scene dragScene;
            dragScene.settings.gridSize = 32;
            EditorViewport dragVp(dragScene);
            dragVp.SetViewportSizeOverride({800, 600});
            dragVp.SetGridSnap(true);
            dragVp.SetAssetBaseDir(root.generic_string());
            CommandStack dragCmds;

            // 1. Image drop with grid snap enabled: screen (420, 315) -> world (20, 15) -> snapped (32, 0)
            dragVp.SetDropPreview(420.0f, 315.0f, heroEntry.name, false);
            if(!dragVp.GetDropPreview().active || dragVp.GetDropPreview().isScript ||
               !Approx(dragVp.GetDropPreview().worldX, 32.0f) ||
               !Approx(dragVp.GetDropPreview().worldY, 0.0f)) {
                std::cerr << "FAIL: asset drop preview did not snap to grid\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            const Scene beforeImgDrop = dragScene;
            const AssetDropResult imgDrop = DropAssetOntoViewport(
                dragScene, dragVp, heroEntry, root.generic_string(), 420.0f, 315.0f);
            dragVp.ClearDropPreview();
            if(dragVp.GetDropPreview().active) {
                std::cerr << "FAIL: ClearDropPreview did not clear preview state\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            if(!imgDrop.succeeded || imgDrop.attachedScript || imgDrop.entityId == InvalidEntityId) {
                std::cerr << "FAIL: image asset drop onto viewport failed\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            const Entity *droppedImg = dragScene.FindEntity(imgDrop.entityId);
            if(!droppedImg || !droppedImg->sprite || droppedImg->sprite->asset != "images/hero.png" ||
               !Approx(droppedImg->transform.x, 32.0f) || !Approx(droppedImg->transform.y, 0.0f)) {
                std::cerr << "FAIL: dropped image entity sprite or snapped coordinates mismatch\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            dragCmds.Push(std::make_unique<SceneSnapshotCommand>("Drop Asset", beforeImgDrop, dragScene));
            if(!dragCmds.Undo(dragScene) || dragScene.GetEntityCount() != 0) {
                std::cerr << "FAIL: single undo did not revert asset drop\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            if(!dragCmds.Redo(dragScene) || dragScene.GetEntityCount() != 1) {
                std::cerr << "FAIL: redo did not restore asset drop\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            // 2. Script drop onto existing entity attaches ScriptComponent instead of spawning a new entity
            const auto heroScreen = dragVp.GetCamera().WorldToScreen(32.0f, 0.0f, 800.0f, 600.0f);
            dragVp.SetDropPreview(heroScreen.X, heroScreen.Y, scriptEntry.name, true);
            if(!dragVp.GetDropPreview().active || !dragVp.GetDropPreview().isScript ||
               dragVp.GetDropPreview().targetEntityId != imgDrop.entityId) {
                std::cerr << "FAIL: script drop preview did not target hovered entity\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            const Scene beforeScriptDrop = dragScene;
            const AssetDropResult scriptAttach = DropAssetOntoViewport(
                dragScene, dragVp, scriptEntry, root.generic_string(), heroScreen.X, heroScreen.Y);
            dragVp.ClearDropPreview();
            if(!scriptAttach.succeeded || !scriptAttach.attachedScript ||
               scriptAttach.entityId != imgDrop.entityId || dragScene.GetEntityCount() != 1) {
                std::cerr << "FAIL: script drop onto entity did not attach in place\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            const Entity *withScript = dragScene.FindEntity(imgDrop.entityId);
            if(!withScript || !withScript->script || withScript->script->path != "scripts/spin.gs") {
                std::cerr << "FAIL: attached script path mismatch\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            dragCmds.Push(std::make_unique<SceneSnapshotCommand>("Attach Script", beforeScriptDrop, dragScene));
            if(!dragCmds.Undo(dragScene) || dragScene.FindEntity(imgDrop.entityId)->script.has_value()) {
                std::cerr << "FAIL: single undo did not revert script attach\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            // 3. Unsnapped drop when grid snap is disabled
            dragVp.SetGridSnap(false);
            const AssetDropResult unsnappedDrop = DropAssetOntoViewport(
                dragScene, dragVp, scriptEntry, root.generic_string(), 620.0f, 415.0f);
            const Entity *unsnappedEnt = dragScene.FindEntity(unsnappedDrop.entityId);
            if(!unsnappedDrop.succeeded || unsnappedDrop.attachedScript || !unsnappedEnt ||
               !Approx(unsnappedEnt->transform.x, 220.0f) || !Approx(unsnappedEnt->transform.y, 115.0f)) {
                std::cerr << "FAIL: unsnapped script drop onto empty viewport mismatch\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
        }

        // ---- Drag-and-drop: HierarchyPanel reparenting + cycle guard + unparent to root + undo ----
        {
            Scene hierScene;
            const EntityId idA = hierScene.CreateEntity("Alpha").id;
            const EntityId idB = hierScene.CreateEntity("Beta").id;
            const EntityId idC = hierScene.CreateEntity("Gamma").id;
            CommandStack hierCmds;

            auto rows = BuildHierarchyRows(hierScene);
            if(rows.size() != 3 || rows[0].id != idA || rows[1].id != idB || rows[2].id != idC) {
                std::cerr << "FAIL: initial hierarchy row order mismatch\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            // Drag Beta onto Alpha (row 0)
            const HierarchyDropTarget ontoA = ResolveHierarchyDropTarget(
                hierScene, rows, idB, kHierarchyRowHeight / 2);
            if(ontoA.isRoot || ontoA.parentId != idA || !ontoA.valid) {
                std::cerr << "FAIL: dragging Beta onto Alpha should resolve as valid child drop\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            const Scene beforeReparentB = hierScene;
            if(!ApplyHierarchyDragReparent(hierScene, idB, ontoA.parentId) ||
               hierScene.FindEntity(idB)->parentId != idA) {
                std::cerr << "FAIL: drag-reparent Beta -> Alpha failed\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            hierCmds.Push(std::make_unique<SceneSnapshotCommand>("Edit Hierarchy", beforeReparentB, hierScene));

            // Drag Gamma onto Beta -> chain Alpha -> Beta -> Gamma
            rows = BuildHierarchyRows(hierScene);
            const HierarchyDropTarget ontoB = ResolveHierarchyDropTarget(
                hierScene, rows, idC, kHierarchyRowHeight + kHierarchyRowHeight / 2);
            if(ontoB.isRoot || ontoB.parentId != idB || !ontoB.valid ||
               !ApplyHierarchyDragReparent(hierScene, idC, ontoB.parentId) ||
               hierScene.FindEntity(idC)->parentId != idB) {
                std::cerr << "FAIL: drag-reparent Gamma -> Beta failed\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            // Cycle guard: dragging Alpha onto itself (row 0) or descendant Gamma (row 2) must be rejected
            rows = BuildHierarchyRows(hierScene);
            const HierarchyDropTarget selfDrop = ResolveHierarchyDropTarget(
                hierScene, rows, idA, kHierarchyRowHeight / 2);
            const HierarchyDropTarget descDrop = ResolveHierarchyDropTarget(
                hierScene, rows, idA, kHierarchyRowHeight * 2 + kHierarchyRowHeight / 2);
            if(selfDrop.valid || descDrop.valid ||
               ApplyHierarchyDragReparent(hierScene, idA, idA) ||
               ApplyHierarchyDragReparent(hierScene, idA, idC) ||
               hierScene.FindEntity(idA)->parentId != InvalidEntityId) {
                std::cerr << "FAIL: hierarchy drag cycle guard did not reject self/descendant reparent\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            // Unparent to root via panel header (y < 0) and via empty area below rows
            const HierarchyDropTarget headerDrop = ResolveHierarchyDropTarget(hierScene, rows, idC, -10);
            const HierarchyDropTarget emptyDrop = ResolveHierarchyDropTarget(
                hierScene, rows, idC, kHierarchyRowHeight * 5);
            if(!headerDrop.isRoot || headerDrop.parentId != InvalidEntityId || !headerDrop.valid ||
               !emptyDrop.isRoot || emptyDrop.parentId != InvalidEntityId || !emptyDrop.valid) {
                std::cerr << "FAIL: dragging to header or empty area did not resolve to root unparent\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }

            const Scene beforeUnparentC = hierScene;
            if(!ApplyHierarchyDragReparent(hierScene, idC, headerDrop.parentId) ||
               hierScene.FindEntity(idC)->parentId != InvalidEntityId) {
                std::cerr << "FAIL: unparenting Gamma to root failed\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            hierCmds.Push(std::make_unique<SceneSnapshotCommand>("Edit Hierarchy", beforeUnparentC, hierScene));

            // Single-step undo restores Gamma under Beta, redo unparents it again
            if(!hierCmds.Undo(hierScene) || hierScene.FindEntity(idC)->parentId != idB) {
                std::cerr << "FAIL: single undo did not revert hierarchy unparent\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
            if(!hierCmds.Redo(hierScene) || hierScene.FindEntity(idC)->parentId != InvalidEntityId) {
                std::cerr << "FAIL: redo did not reapply hierarchy unparent\n";
                std::filesystem::remove_all(root, ec);
                return 1;
            }
        }

        std::filesystem::remove_all(root, ec);
        return 0;
    }

    /// Diagnostic: isolates which component's construction/teardown corrupts the heap.
    /// Run as `GorgonEditor --diag <variant>`; the process exit code is the verdict.
    int RunDiag(const std::string &variant) {
        using namespace Gorgon::Editor;

        Gorgon::Initialize("GorgonEditor");
        int result = 0;
        {
            // Same lifetime rule as main(): the theme must outlive the window.
            std::unique_ptr<Gorgon::Widgets::SimpleGenerator> theme;
            Gorgon::UI::Window window({1280, 800}, "Gorgon 2D Editor diag");
            theme = CreateDarkEditorTheme(9.5f);

            const auto frames = [](int count) {
                for(int i = 0; i < count; ++i)
                    Gorgon::NextFrame();
            };

            if(variant == "panels4") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::Widgets::Panel left{Gorgon::Widgets::Registry::Panel_Left};
                    Gorgon::Widgets::Panel right{Gorgon::Widgets::Registry::Panel_Right};
                    Gorgon::Widgets::Layerbox box{Gorgon::Widgets::Registry::Layerbox_Blank};
                    window.Add(left);
                    window.Add(right);
                    window.Add(top);
                    window.Add(box);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::UI::Organizers::Flow flow;
                    Gorgon::Widgets::Button b1{"New"}, b2{"Open"}, b3{"Save"};
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    Gorgon::Widgets::Label lbl{"Ready"};
                    top.AttachOrganizer(flow);
                    flow << b1 << b2 << b3 << chk << lbl;
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "viewportobj") {
                {
                    Scene scene;
                    EditorViewport vp(scene);
                    vp.SetViewportSizeOverride({800, 600});
                    vp.Redraw();
                    frames(1);
                }
                frames(1);
            }
            else if(variant == "detached-layerbox") {
                {
                    Gorgon::Widgets::Layerbox box{Gorgon::Widgets::Registry::Layerbox_Blank};
                    Gorgon::Graphics::Layer gfx;
                    Gorgon::Input::Layer input;
                    box.GetLayer().Add(gfx);
                    box.GetLayer().Add(input);
                }
                frames(1);
            }
            else if(variant == "controls-noorg") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::Widgets::Button b1{"New"}, b2{"Open"}, b3{"Save"};
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    Gorgon::Widgets::Label lbl{"Ready"};
                    top.Add(b1); top.Add(b2); top.Add(b3); top.Add(chk); top.Add(lbl);
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls-flowonly") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::UI::Organizers::Flow flow;
                    top.AttachOrganizer(flow);
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls-orgfirst") {
                {
                    Gorgon::UI::Organizers::Flow flow;
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    top.AttachOrganizer(flow);
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls-noframes") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::UI::Organizers::Flow flow;
                    Gorgon::Widgets::Button b1{"New"}, b2{"Open"}, b3{"Save"};
                    top.AttachOrganizer(flow);
                    flow << b1 << b2 << b3;
                    window.Add(top);
                }
                frames(1);
            }
            else if(variant == "controls-scrolloff") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    top.EnableScroll(false, false);
                    Gorgon::Widgets::Button b1{"New"}, b2{"Open"}, b3{"Save"};
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    Gorgon::Widgets::Label lbl{"Ready"};
                    top.Add(b1); top.Add(b2); top.Add(b3); top.Add(chk); top.Add(lbl);
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls-vscroll") {
                {
                    Gorgon::Widgets::Panel right{Gorgon::Widgets::Registry::Panel_Right};
                    right.EnableScroll(true, false);
                    Gorgon::Widgets::Button b1{"A"}, b2{"B"};
                    right.Add(b1); right.Add(b2);
                    window.Add(right);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls-onewidget") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::Widgets::Button b1{"New"};
                    top.Add(b1);
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "controls-checkboxonly") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    top.Add(chk);
                    window.Add(top);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "checkbox-window") {
                {
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    window.Add(chk);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "checkbox-noframes") {
                {
                    Gorgon::Widgets::Panel top{Gorgon::Widgets::Registry::Panel_Top};
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    top.Add(chk);
                    window.Add(top);
                }
                frames(1);
            }
            else if(variant == "button-window") {
                {
                    Gorgon::Widgets::Button b{"New"};
                    window.Add(b);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "checkbox-nochecked") {
                {
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", false};
                    window.Add(chk);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "checkbox-orphan") {
                {
                    // Never added to the window.
                    Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                    (void)chk;
                }
                frames(1);
            }
            else if(variant == "checkbox-orphan-window") {
                Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                frames(2);
            }
            else if(variant == "checkbox-orphan-noframes") {
                Gorgon::Widgets::Checkbox chk{"Grid Snap", true};
                (void)chk;
            }
            else if(variant == "template-checkbox") {
                // Force the generator to produce the checkbox template, no widget involved.
                (void)Gorgon::Widgets::Registry::Active()[Gorgon::Widgets::Registry::Checkbox_Regular];
                frames(1);
            }
            else if(variant == "template-button") {
                (void)Gorgon::Widgets::Registry::Active()[Gorgon::Widgets::Registry::Button_Regular];
                frames(1);
            }
            else if(variant == "asset-box") {
                (void)theme->GetAsset({Gorgon::Widgets::SimpleGenerator::AssetID::Box,
                                       Gorgon::Graphics::Color::Regular,
                                       Gorgon::Widgets::SimpleGenerator::AssetID::None});
                frames(1);
            }
            else if(variant == "asset-tick") {
                (void)theme->GetAsset({Gorgon::Widgets::SimpleGenerator::AssetID::Tick,
                                       Gorgon::Graphics::Color::Regular,
                                       Gorgon::Widgets::SimpleGenerator::AssetID::None});
                frames(1);
            }
            else if(variant == "asset-background") {
                (void)theme->GetAsset({Gorgon::Widgets::SimpleGenerator::AssetID::Background,
                                       Gorgon::Graphics::Color::Regular,
                                       Gorgon::Widgets::SimpleGenerator::AssetID::All});
                frames(1);
            }
            else if(variant == "cb-default-theme") {
                // A generator with stock Init() defaults, leaked so its templates survive.
                auto *gen = new Gorgon::Widgets::SimpleGenerator();
                gen->Init();
                Gorgon::UI::Initialize(*gen);
                (void)Gorgon::Widgets::Registry::Active()[Gorgon::Widgets::Registry::Checkbox_Regular];
                frames(1);
            }
            else if(variant == "noop") {
                // Window + theme construct and destroy, nothing else.
            }
            else if(variant == "frames1") {
                frames(1);
            }
            else if(variant == "base") {
                frames(2);
            }
            else if(variant == "noclose") {
                frames(3);
            }
            else if(variant == "close") {
                frames(2);
                window.Close();
            }
            else if(variant == "close-frames") {
                frames(2);
                window.Close();
                frames(2);
            }
            else if(variant == "theme-first") {
                // Hand the theme over so it outlives the window.
                theme.release();
                frames(2);
                window.Close();
                frames(1);
            }
            else if(variant == "panel") {
                // Constructed and destroyed, never attached to the window.
                AssetBrowserPanel browser;
                (void)browser;
                frames(2);
            }
            else if(variant == "layers") {
                // Two extra layers attached to a bare Layerbox, mirroring the browser grid.
                {
                    Gorgon::Widgets::Layerbox box{Gorgon::Widgets::Registry::Layerbox_Blank};
                    window.Add(box);
                    Gorgon::Graphics::Layer gfx;
                    Gorgon::Input::Layer input;
                    box.GetLayer().Add(gfx);
                    box.GetLayer().Add(input);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "browser-nodraw") {
                {
                    AssetBrowserPanel browser;
                    browser.Attach(window);
                    browser.Layout(0, 0, 1280, 200);
                    frames(2);
                }
                frames(2);
            }
            else if(variant == "browser") {
                {
                    AssetBrowserPanel browser;
                    browser.Attach(window);
                    browser.Layout(0, 0, 1280, 200);
                    frames(3);
                }
                frames(3);
            }
            else if(variant == "browser-thumbs") {
                std::error_code ec;
                const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_diag_assets";
                std::filesystem::remove_all(dir, ec);
                std::filesystem::create_directories(dir, ec);
                WriteTinyPng(dir / "a.png");
                WriteTinyPng(dir / "b.png");
                {
                    AssetBrowserPanel browser;
                    browser.Attach(window);
                    browser.SetRoot(dir.generic_string());
                    browser.Layout(0, 0, 1280, 200);
                    frames(4);
                }
                frames(4);
                std::filesystem::remove_all(dir, ec);
            }
            else if(variant == "app") {
                {
                    EditorApp app(window);
                    frames(3);
                    window.Close();
                    frames(2);
                }
                frames(2);
            }
            else {
                std::cerr << "unknown diag variant: " << variant << "\n";
                result = 2;
            }

            window.Close();
            frames(1);
        }
        return result;
    }

} // namespace

int main(int argc, char *argv[]) {
    bool selftest = false;
    bool smoke = false;
    std::string diagVariant;
    std::string initialScenePath;

    for(int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if(arg == "--selftest") {
            selftest = true;
        } else if(arg == "--smoke") {
            smoke = true;
        } else if(arg == "--diag") {
            if(i + 1 < argc) {
                diagVariant = argv[++i];
            }
        } else if(initialScenePath.empty() && !arg.empty() && arg[0] != '-') {
            initialScenePath = arg;
        }
    }

    if(!diagVariant.empty()) {
        return RunDiag(diagVariant);
    }

    if(selftest) {
        return RunSelfTest();
    }

    Gorgon::Initialize("GorgonEditor");

    // The widget theme (Widgets::Registry) must outlive the window: Gorgon's window/widget
    // teardown still dereferences the active registry's templates, so destroying the theme
    // first corrupts the heap at process exit (exit code 0xC0000374). Declared before the
    // window, assigned after it exists, so it is destroyed *after* the window closes.
    std::unique_ptr<Gorgon::Widgets::SimpleGenerator> theme;

    Gorgon::UI::Window window({1280, 800}, "Gorgon 2D Editor");
    theme = Gorgon::Editor::CreateDarkEditorTheme(9.5f);

    Gorgon::Editor::EditorApp app(window);
    window.AllowResize();

    if(!initialScenePath.empty()) {
        std::string err;
        app.OpenSceneFile(initialScenePath, &err);
    }

    window.ClosingEvent.Register([&app](bool &allow) {
        allow = app.OnClosing();
    });

    if(smoke) {
        using namespace Gorgon::Editor;

        for(int i = 0; i < 3; ++i) {
            Gorgon::NextFrame();
        }

        // ---- Phase 3: hierarchy tree + inspector binding -------------------
        auto &scene = app.GetScene();
        const auto entityCount = scene.GetEntityCount();
        const long rowCount = app.GetHierarchy().GetRowCount();

        if(rowCount != long(entityCount)) {
            std::cerr << "FAIL: hierarchy rows (" << rowCount << ") != entity count ("
                      << entityCount << ")\n";
            window.Close();
            return 1;
        }

        EntityId picked = InvalidEntityId;
        for(const auto &entity : scene.GetEntities()) {
            if(entity.collider) {
                picked = entity.id;
                break;
            }
        }
        if(picked == InvalidEntityId && entityCount > 0)
            picked = scene.GetEntities().front().id;

        if(picked != InvalidEntityId) {
            app.GetViewport().SetSelectedEntity(picked, true);
            Gorgon::NextFrame();

            if(app.GetHierarchy().GetSelectedEntity() != picked) {
                std::cerr << "FAIL: viewport selection did not sync to the hierarchy\n";
                window.Close();
                return 1;
            }
        }

        // ---- Phase 5: undo/redo, play-in-editor -----------------------------
        {
            using namespace Gorgon::Editor;

            int historyUndoSteps = 0;
            int historyRedoSteps = 0;
            float dragStartX = 0.0f;
            float dragEndX = 0.0f;
            float dragUndoneX = 0.0f;
            int playScripts = 0;
            float playStartX = 0.0f;
            float playMovedX = 0.0f;
            float playRestoredX = 0.0f;
            std::string playError;

            auto fail = [&](const std::string &message) {
                std::cerr << "FAIL: " << message << "\n";
                window.Close();
                return 1;
            };

            if(picked == InvalidEntityId)
                return fail("phase 5 needs a selectable entity");

            app.GetViewport().SetSelectedEntity(picked, true);
            Gorgon::NextFrame();

            // -- 1. A real viewport drag becomes exactly one undo step -------------
            {
                const auto size = app.GetViewport().GetViewportSize();
                const auto &cam = app.GetViewport().GetCamera();
                const Transform world = scene.GetWorldTransform(picked);
                const auto press = cam.WorldToScreen(world.x, world.y,
                                                     float(size.Width), float(size.Height));
                dragStartX = scene.FindEntity(picked)->transform.x;

                if(app.GetViewport().HitTestEntity(press.X, press.Y) != picked) {
                    return fail("phase 5 drag press point did not hit the selected entity");
                }

                app.GetViewport().HandleMouseDown({int(press.X), int(press.Y)},
                                                 Gorgon::Input::Mouse::Button::Left);
                app.GetViewport().HandleMouseMove({int(press.X) + 45, int(press.Y) + 18});
                app.GetViewport().HandleMouseUp({int(press.X) + 45, int(press.Y) + 18},
                                                Gorgon::Input::Mouse::Button::Left);

                dragEndX = scene.FindEntity(picked)->transform.x;
                if(std::abs(dragEndX - dragStartX) < 1.0f) {
                    return fail("phase 5 drag did not move the entity");
                }
                if(!app.IsDirty()) {
                    return fail("phase 5 drag did not mark the scene dirty");
                }
                if(app.GetCommandStack().GetUndoCount() < 1) {
                    return fail("phase 5 drag did not push an undo step");
                }
                if(app.GetUndoName() != "Move Entity") {
                    return fail("phase 5 undo name is '" + app.GetUndoName() + "', expected 'Move Entity'");
                }
                historyUndoSteps = int(app.GetCommandStack().GetUndoCount());

                app.Undo();
                dragUndoneX = scene.FindEntity(picked)->transform.x;
                if(std::abs(dragUndoneX - dragStartX) > 1e-3f) {
                    return fail("phase 5 undo did not restore the dragged position");
                }
                if(!app.GetCommandStack().CanRedo()) {
                    return fail("phase 5 undo did not fill the redo stack");
                }
                historyRedoSteps = int(app.GetCommandStack().GetRedoCount());

                app.Redo();
                if(std::abs(scene.FindEntity(picked)->transform.x - dragEndX) > 1e-3f) {
                    return fail("phase 5 redo did not reapply the drag");
                }
            }

            // -- 2. Structural undo: duplicate, undo, redo ------------------------
            {
                const auto before = scene.GetEntityCount();
                app.DuplicateSelected();
                Gorgon::NextFrame();
                if(scene.GetEntityCount() != before + 1) {
                    return fail("phase 5 duplicate did not add an entity");
                }
                app.Undo();
                if(scene.GetEntityCount() != before) {
                    return fail("phase 5 undo did not remove the duplicate");
                }
                app.Redo();
                if(scene.GetEntityCount() != before + 1) {
                    return fail("phase 5 redo did not bring the duplicate back");
                }
                // Leave the scene as it was.
                app.Undo();
                app.GetCommandStack().Clear();
                Gorgon::NextFrame();
            }

            // -- 2b. Ctrl+Z / Ctrl+Y through the real shortcut dispatcher ---------
            {
                app.GetViewport().SetSelectedEntity(picked, true);
                const auto before = scene.GetEntityCount();
                app.DuplicateSelected();
                Gorgon::NextFrame();
                if(scene.GetEntityCount() != before + 1) {
                    return fail("phase 5 duplicate did not add an entity");
                }

                // The shortcuts manager listens on the window's KeyPreviewEvent and matches
                // Keyboard::CurrentModifier, so this drives the same path a real keypress does.
                auto &modifier = Gorgon::Input::Keyboard::CurrentModifier;
                const auto previousModifier = modifier;

                modifier = Gorgon::Input::Keyboard::Modifier::Ctrl;
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::Z, 1.0f);
                if(scene.GetEntityCount() != before) {
                    return fail("Ctrl+Z did not undo the duplicate");
                }
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::Y, 1.0f);
                if(scene.GetEntityCount() != before + 1) {
                    return fail("Ctrl+Y did not redo the duplicate");
                }
                // Shortcuts must not fire while nothing is bound to them either.
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::Z, 0.0f);

                // Ctrl+C / Ctrl+V through the real shortcut dispatcher + single-step Ctrl+Z undo
                const auto beforePaste = scene.GetEntityCount();
                app.GetViewport().SetSelectedEntity(picked, true);
                const float origX = scene.FindEntity(picked)->transform.x;
                const float origY = scene.FindEntity(picked)->transform.y;
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::C, 1.0f);
                if(!app.HasClipboard()) {
                    modifier = previousModifier;
                    return fail("Ctrl+C did not populate clipboard");
                }
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::V, 1.0f);
                if(scene.GetEntityCount() != beforePaste + 1) {
                    modifier = previousModifier;
                    return fail("Ctrl+V did not paste the copied entity");
                }
                const EntityId pastedId = app.GetViewport().GetSelectedEntity();
                const Entity *pastedEntity = scene.FindEntity(pastedId);
                if(!pastedEntity || pastedId == picked ||
                   std::abs(pastedEntity->transform.x - (origX + float(scene.settings.gridSize))) > 1e-2f ||
                   std::abs(pastedEntity->transform.y - (origY + float(scene.settings.gridSize))) > 1e-2f) {
                    modifier = previousModifier;
                    return fail("Ctrl+V pasted entity selection or offset mismatch");
                }
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::Z, 1.0f);
                if(scene.GetEntityCount() != beforePaste) {
                    modifier = previousModifier;
                    return fail("Ctrl+Z did not undo Ctrl+V paste");
                }

                modifier = previousModifier;

                app.Undo();
                app.GetCommandStack().Clear();
                Gorgon::NextFrame();
            }

            // -- 3. Play runs a real .gs script and Stop restores the transform ---
            {
                std::error_code pec;
                const auto playDir = std::filesystem::temp_directory_path() / "gorgon_editor_smoke_play";
                std::filesystem::remove_all(playDir, pec);
                std::filesystem::create_directories(playDir, pec);

                {
                    std::ofstream script(playDir / "spin.gs", std::ios::binary | std::ios::trunc);
                    if(!script.good())
                        return fail("could not write the play test script");
                    script << "using Input\n"
                              "function Update(self, dt)\n"
                              "    self.x = self.x + 100 * dt\n"
                              "    if(IsKeyDown(\"D\"))\n"
                              "        self.x = self.x + 50 * dt\n"
                              "    end if\n"
                              "    self.rotation = self.rotation + 90 * dt\n"
                              "    return 0\n"
                              "end function\n";
                }

                Entity *target = scene.FindEntity(picked);
                if(!target)
                    return fail("play target entity disappeared");

                const std::string previousBase = app.GetViewport().GetAssetBaseDir();
                target->script = ScriptComponent{"spin.gs"};
                app.GetViewport().SetAssetBaseDir(playDir.generic_string());

                playStartX = target->transform.x;
                if(!app.StartPlay(&playError)) {
                    return fail("phase 5 play did not start: " + playError);
                }
                playScripts = app.GetPlayingScriptCount();
                if(playScripts != 1) {
                    return fail("phase 5 play compiled " + std::to_string(playScripts) +
                                " scripts, expected 1");
                }

                // One deterministic step instead of waiting on frame deltas.
                app.TickPlay(0.25f);
                playMovedX = scene.FindEntity(picked)->transform.x;
                if(std::abs(playMovedX - (playStartX + 25.0f)) > 1e-2f) {
                    return fail("phase 5 play script did not move the entity (got "
                                + std::to_string(playMovedX) + ")");
                }

                // Input through the real path a keypress takes: window KeyPreviewEvent ->
                // EditorApp -> play session -> GScript Input library.
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::D, 1.0f);
                app.TickPlay(0.25f);
                const float holdingX = scene.FindEntity(picked)->transform.x;
                if(std::abs(holdingX - (playMovedX + 37.5f)) > 1e-2f) {
                    return fail("play script did not see the held key (got "
                                + std::to_string(holdingX) + ")");
                }
                window.KeyPreviewEvent(Gorgon::Input::Keyboard::Keycodes::D, 0.0f);
                app.TickPlay(0.25f);
                const float releasedX = scene.FindEntity(picked)->transform.x;
                if(std::abs(releasedX - (holdingX + 25.0f)) > 1e-2f) {
                    return fail("play script kept moving after the key was released (got "
                                + std::to_string(releasedX) + ")");
                }

                app.StopPlay();
                playRestoredX = scene.FindEntity(picked)->transform.x;
                if(std::abs(playRestoredX - playStartX) > 1e-3f) {
                    return fail("phase 5 stop did not restore the pre-play transform (got "
                                + std::to_string(playRestoredX) + ")");
                }
                if(!app.GetViewport().IsEditEnabled()) {
                    return fail("phase 5 stop did not re-enable viewport editing");
                }

                if(Entity *restored = scene.FindEntity(picked))
                    restored->script.reset();
                app.GetViewport().SetAssetBaseDir(previousBase);
                std::filesystem::remove_all(playDir, pec);
                Gorgon::NextFrame();
            }

            {
                std::ofstream report(std::filesystem::temp_directory_path() /
                                         "gorgon_editor_phase5_report.txt",
                                     std::ios::trunc);
                report << "undoSteps=" << historyUndoSteps << "\n"
                       << "redoSteps=" << historyRedoSteps << "\n"
                       << "dragStartX=" << dragStartX << "\n"
                       << "dragEndX=" << dragEndX << "\n"
                       << "dragUndoneX=" << dragUndoneX << "\n"
                       << "playScripts=" << playScripts << "\n"
                       << "playStartX=" << playStartX << "\n"
                       << "playMovedX=" << playMovedX << "\n"
                       << "playRestoredX=" << playRestoredX << "\n"
                       << "playMode=" << int(app.GetPlayMode()) << "\n";
            }

            Gorgon::NextFrame();
        }

        // A live edit written straight into the scene must survive an inspector refresh
        // and the save/open round-trip below.
        if(Entity *entity = scene.FindEntity(picked)) {
            entity->transform.x = 77.5f;
            if(entity->sprite)
                entity->sprite->tint = {10, 20, 30, 255};
        }
        app.GetInspector().Refresh();
        Gorgon::NextFrame();

        // ---- Phase 4: asset browser scan, thumbnails and insert-to-scene ----
        {
            std::error_code ec2;
            const auto assetDir = std::filesystem::temp_directory_path() / "gorgon_editor_smoke_assets";
            std::filesystem::remove_all(assetDir, ec2);
            std::filesystem::create_directories(assetDir, ec2);
            if(!WriteTinyPng(assetDir / "smoke.png")) {
                std::cerr << "FAIL: could not write the smoke test image\n";
                window.Close();
                return 1;
            }

            auto &browser = app.GetAssetBrowser();
            browser.SetRoot(assetDir.generic_string());
            Gorgon::NextFrame();

            if(browser.GetLibrary().GetVisibleCount() != 1) {
                std::cerr << "FAIL: asset browser listed " << browser.GetLibrary().GetVisibleCount()
                          << " entries, expected 1\n";
                window.Close();
                return 1;
            }
            if(browser.GetThumbnailCount() != 1) {
                std::cerr << "FAIL: thumbnail cache holds " << browser.GetThumbnailCount()
                          << " thumbnails, expected 1\n";
                window.Close();
                return 1;
            }

            // Drive the real double-click path: first click selects, second inserts.
            const auto tile = browser.GetTileBounds(0);
            const Gorgon::Geometry::Point tilePoint{tile.X + 6, tile.Y + 6};
            const auto beforeInsert = scene.GetEntityCount();

            browser.HandleMouseDown(tilePoint, Gorgon::Input::Mouse::Button::Left);
            if(browser.GetSelectedIndex() != 0) {
                std::cerr << "FAIL: clicking a tile did not select it\n";
                window.Close();
                return 1;
            }
            browser.HandleMouseDown(tilePoint, Gorgon::Input::Mouse::Button::Left);
            Gorgon::NextFrame();

            const Entity *inserted = scene.FindEntity(app.GetViewport().GetSelectedEntity());
            if(scene.GetEntityCount() != beforeInsert + 1 || !inserted || !inserted->sprite) {
                std::cerr << "FAIL: asset insert did not add a sprite entity\n";
                window.Close();
                return 1;
            }
            if(inserted->sprite->asset != "smoke.png") {
                std::cerr << "FAIL: inserted sprite path should be relative, got " << inserted->sprite->asset << "\n";
                window.Close();
                return 1;
            }
            if(!app.IsDirty()) {
                std::cerr << "FAIL: inserting an asset did not mark the scene dirty\n";
                window.Close();
                return 1;
            }

            // Machine readable proof of what the browser actually did (stdout of a windowed
            // run is not reliably captured).
            {
                std::ofstream report(std::filesystem::temp_directory_path() / "gorgon_editor_smoke_report.txt",
                                     std::ios::trunc);
                report << "assetEntries=" << browser.GetLibrary().GetVisibleCount() << "\n"
                       << "thumbnails=" << browser.GetThumbnailCount() << "\n"
                       << "entitiesBefore=" << beforeInsert << "\n"
                       << "entitiesAfter=" << scene.GetEntityCount() << "\n"
                       << "spriteAsset=" << inserted->sprite->asset << "\n"
                       << "dirty=" << (app.IsDirty() ? 1 : 0) << "\n"
                       << "breadcrumb=" << browser.GetLibrary().GetBreadcrumb() << "\n";
            }

            std::filesystem::remove_all(assetDir, ec2);
        }

        // Verify save/open round-trip through EditorApp in live window mode
        const auto tmpPath = (std::filesystem::temp_directory_path() / "gorgon_editor_smoke.gscene.json").string();
        std::string err;
        const bool saved = app.SaveSceneFile(tmpPath, &err);
        const bool opened = saved && app.OpenSceneFile(tmpPath, &err);
        const Entity *reloaded = opened ? app.GetScene().FindEntity(picked) : nullptr;
        const bool persisted = reloaded != nullptr && std::abs(reloaded->transform.x - 77.5f) < 1e-3f;
        std::filesystem::remove(tmpPath);
        window.Close();
        if(!saved || !opened) {
            std::cerr << "FAIL: smoke round-trip failed: " << err << "\n";
            return 1;
        }
        if(picked != InvalidEntityId && !persisted) {
            std::cerr << "FAIL: inspector edit did not persist through save/open\n";
            return 1;
        }
        std::cout << "GorgonEditor --smoke PASSED (window shell + dark theme + 3 frames + hierarchy/inspector + round-trip)\n";
        return 0;
    }

    window.Run();
    return 0;
}
