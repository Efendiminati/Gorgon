#include "Core/ComponentSimulation.h"
#include "Core/GameExporter.h"
#include "Core/Lightmap.h"
#include "Core/PlaySession.h"
#include "Core/Scene.h"
#include "Core/SceneRuntime.h"
#include "Core/SceneSerializer.h"
#include "Core/ScriptLog.h"
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

    /// Headless 2D lighting check, runs late in --selftest so its definition sits at the bottom of
    /// this file; declared here for the selftest body above it.
    int RunLightingSelfTest();

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
    int RunComponentsSelfTest();
    int RunConsoleSelfTest();
    int RunHotReloadSelfTest();
    int RunUserInterfaceSelfTest();

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

        if(const int componentResult = RunComponentsSelfTest()) {
            return componentResult;
        }

        if(const int consoleResult = RunConsoleSelfTest()) {
            return consoleResult;
        }

        if(const int hotReloadResult = RunHotReloadSelfTest()) {
            return hotReloadResult;
        }

        if(const int lightingResult = RunLightingSelfTest()) {
            return lightingResult;
        }

        if(const int uiResult = RunUserInterfaceSelfTest()) {
            return uiResult;
        }

        std::cout << "GorgonEditor --selftest PASSED (camera, hit-test, grid-snap drag, pan, multi-select, copy/paste, prefabs, script input, expanded components, script console, syntax check, hot-reload, 2D lighting, in-game UI, round-trip, asset browser)\n";
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

    /// Headless packaging: `GorgonEditor --export <scene.gscene.json> <outputDir>` writes a
    /// standalone bundle (game.json, scene, assets and GorgonPlayer.exe) and exits. Used by the
    /// build verification, and it makes export scriptable without opening the editor.
    int RunExport(const std::string &scenePath, const std::string &outputDir) {
        using namespace Gorgon::Editor;

        if(scenePath.empty() || outputDir.empty()) {
            std::cerr << "usage: GorgonEditor --export <scene.gscene.json> <outputDir>\n";
            return 1;
        }

        Scene scene;
        std::string error;
        if(!SceneSerializer::LoadFromFile(scenePath, scene, &error)) {
            std::cerr << "FAIL: " << (error.empty() ? "could not load the scene" : error) << "\n";
            return 1;
        }

        ExportOptions options;
        options.outputDir = outputDir;
        const auto parent = std::filesystem::path(scenePath).parent_path();
        if(!parent.empty())
            options.sceneBaseDir = parent.generic_string();
        // The player sits next to GorgonEditor.exe in the build output.
        options.playerBinaryPath = GameExporter::FindPlayerBinary(std::string());

        const ExportResult result = GameExporter::Export(scene, options);
        if(!result.ok) {
            std::cerr << "FAIL: export: " << result.error << "\n";
            return 1;
        }

        for(const auto &missing : result.missingAssets)
            std::cerr << "missing asset: " << missing << "\n";
        for(const auto &warning : result.warnings)
            std::cerr << "warning: " << warning << "\n";

        std::cout << "Exported '" << scene.name << "' to " << result.outputDir << " ("
                  << result.copiedAssets.size() << " assets, "
                  << result.copiedRuntimeDlls.size() << " runtime DLLs, player: "
                  << (result.playerExecutablePath.empty() ? std::string("none")
                                                          : result.playerExecutablePath)
                  << ")\n";
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


    /// Expanded components: tags/properties, JSON round-trip of every new component, the
    /// simulation that steps them and the runtime camera component. Runs headless.
    int RunComponentsSelfTest() {
        using namespace Gorgon::Editor;

        Scene scene;
        Entity &hero = scene.CreateEntity("Hero");
        hero.tags = {"hero", "player"};
        hero.properties["hp"] = "100";

        SpriteAnimComponent anim;
        anim.asset = "sheet.png";
        anim.frameW = 16;
        anim.frameH = 16;
        anim.frameCount = 4;
        anim.fps = 4.0f;
        hero.spriteAnim = anim;

        TextComponent text;
        text.text = "Hello";
        text.align = TextAlign::Center;
        hero.text = text;

        AudioComponent audio;
        audio.asset = "hit.wav";
        hero.audio = audio;

        ParticleComponent particles;
        particles.emitRate = 8.0f;
        particles.lifetime = 1.0f;
        hero.particles = particles;
        const EntityId heroId = hero.id;

        Entity &map = scene.CreateEntity("Map");
        TilemapComponent tilemap;
        tilemap.cols = 2;
        tilemap.rows = 2;
        tilemap.tiles = {0, 1, -1, 2};
        map.tilemap = tilemap;
        const EntityId mapId = map.id;

        Entity &camera = scene.CreateEntity("MainCamera");
        CameraComponent cameraComp;
        cameraComp.active = true;
        cameraComp.zoom = 2.0f;
        cameraComp.followTarget = heroId;
        camera.camera = cameraComp;
        const EntityId cameraId = camera.id;
        scene.FindEntity(heroId)->transform.x = 100.0f;
        scene.FindEntity(heroId)->transform.y = 50.0f;

        // Tag and property helpers (the GScript Scene library calls these).
        if(scene.FindEntityByTag("hero") != heroId ||
           scene.FindEntitiesByTag("player").size() != 1 ||
           !scene.HasTag(heroId, "hero") ||
           scene.GetProperty(heroId, "hp") != "100") {
            std::cerr << "FAIL: tag/property helpers\n";
            return 1;
        }

        // Every expanded component survives a JSON round-trip.
        std::string error;
        Scene reloaded;
        if(!SceneSerializer::FromJsonString(SceneSerializer::ToJsonString(scene), reloaded, &error)) {
            std::cerr << "FAIL: expanded component scene did not parse: " << error << "\n";
            return 1;
        }
        if(reloaded != scene) {
            std::cerr << "FAIL: expanded component scene did not survive the round-trip\n";
            return 1;
        }
        if(!reloaded.FindEntity(heroId)->spriteAnim ||
           !reloaded.FindEntity(heroId)->text ||
           !reloaded.FindEntity(heroId)->audio ||
           !reloaded.FindEntity(mapId)->tilemap ||
           !reloaded.FindEntity(cameraId)->camera) {
            std::cerr << "FAIL: a component was lost in the round-trip\n";
            return 1;
        }

        // The simulation steps animation + particles and fires play-on-start audio exactly once.
        ComponentSimulation simulation;
        std::vector<std::string> triggered;
        simulation.SetSoundSink([&triggered](const std::string &asset) { triggered.push_back(asset); });
        simulation.Step(scene, 0.25f);
        if(scene.FindEntity(heroId)->spriteAnim->currentFrame != 1) {
            std::cerr << "FAIL: sprite animation did not advance\n";
            return 1;
        }
        if(simulation.GetParticleCount(heroId) != 2) {
            std::cerr << "FAIL: particle emitter produced " << simulation.GetParticleCount(heroId)
                      << " particles instead of 2\n";
            return 1;
        }
        if(triggered.size() != 1 || triggered[0] != "hit.wav") {
            std::cerr << "FAIL: audio component did not fire exactly once\n";
            return 1;
        }
        simulation.Step(scene, 0.25f);
        if(triggered.size() != 1) {
            std::cerr << "FAIL: audio component fired more than once\n";
            return 1;
        }

        // The runtime steps the same components without any script and follows the camera. Reset
        // the animation so its first step is measurable from a known frame.
        scene.FindEntity(heroId)->spriteAnim->currentFrame = 0;
        scene.FindEntity(heroId)->spriteAnim->frameTime = 0.0f;

        SceneRuntime runtime;
        if(!runtime.LoadFromScene(scene, std::string(), &error)) {
            std::cerr << "FAIL: SceneRuntime did not load the component scene: " << error << "\n";
            return 1;
        }
        if(runtime.GetActiveCameraEntity() != cameraId) {
            std::cerr << "FAIL: SceneRuntime did not pick the active CameraComponent\n";
            return 1;
        }
        runtime.Update(0.25f);
        if(runtime.GetScene().FindEntity(heroId)->spriteAnim->currentFrame != 1) {
            std::cerr << "FAIL: SceneRuntime did not step the sprite animation\n";
            return 1;
        }
        if(runtime.GetParticles(heroId) == nullptr || runtime.GetParticles(heroId)->size() != 2) {
            std::cerr << "FAIL: SceneRuntime particle emitter did not step\n";
            return 1;
        }
        if(!Approx(runtime.GetCamera().panX, 100.0f) || !Approx(runtime.GetCamera().panY, 50.0f) ||
           !Approx(runtime.GetCamera().zoom, 2.0f)) {
            std::cerr << "FAIL: SceneRuntime camera component got ("
                      << runtime.GetCamera().panX << ", " << runtime.GetCamera().panY
                      << ") zoom " << runtime.GetCamera().zoom << "\n";
            return 1;
        }
        if(runtime.GetPlayedSounds().empty()) {
            std::cerr << "FAIL: SceneRuntime did not record the component sound\n";
            return 1;
        }

        return 0;
    }

    /// Script console & script editor support, headless: the log ring buffer, the widget-free
    /// filter/scroll view, capturing GScript Log()/Print() and compile/runtime failures, and the
    /// compile-only "Check Syntax" path. The panel widgets themselves are exercised by --smoke,
    /// which is the only mode that has a window and a theme.
    int RunConsoleSelfTest() {
        using namespace Gorgon::Editor;

        // ---- 1. Ring buffer -------------------------------------------------
        {
            ScriptLog log(3);
            log.Info("Player", "one");
            log.Warn("Player", "two");
            log.Error("Enemy", "three");

            if(log.GetCount() != 3 || log.CountOf(LogSeverity::Error) != 1 ||
               log.CountOf(LogSeverity::Warn) != 1 || log.CountOf(LogSeverity::Info) != 1) {
                std::cerr << "FAIL: ScriptLog did not classify the three severities\n";
                return 1;
            }
            if(log.GetEntries().front().message != "one" ||
               log.GetEntries().front().time.size() != 8 ||
               log.GetEntries().back().Format().find("ERROR") == std::string::npos ||
               log.GetEntries().back().Format().find("Enemy: three") == std::string::npos) {
                std::cerr << "FAIL: ScriptLog formatting/timestamp mismatch\n";
                return 1;
            }

            // The fourth line pushes the oldest out: the buffer never grows past its capacity.
            log.Info("Player", "four");
            if(log.GetCount() != 3 || !log.GetLast() || log.GetLast()->message != "four" ||
               log.FormatAll().find("one") != std::string::npos) {
                std::cerr << "FAIL: ScriptLog is not a bounded ring buffer\n";
                return 1;
            }

            log.Clear();
            if(!log.IsEmpty()) {
                std::cerr << "FAIL: ScriptLog::Clear left entries behind\n";
                return 1;
            }
        }

        // ---- 2. Console filter and scroll -----------------------------------
        {
            ScriptLog log;
            log.Info("A", "info line");
            log.Warn("A", "warn line");
            log.Error("B", "error line");

            ConsoleView view;
            view.SetSource(&log);
            if(view.GetLineCount() != 3 || view.GetLineText(0).find("info line") == std::string::npos) {
                std::cerr << "FAIL: ConsoleView lost the unfiltered lines\n";
                return 1;
            }

            view.SetErrorsOnly(true);
            if(view.GetLineCount() != 2 || view.GetLineText(0).find("warn line") == std::string::npos) {
                std::cerr << "FAIL: ConsoleView Errors filter did not keep warnings\n";
                return 1;
            }

            view.SetErrorsOnly(false);
            view.SetVisibleLines(1);
            view.ScrollTo(99);
            if(view.GetMaxScroll() != 2 || view.GetScrollOffset() != 2 || view.GetFirstLine() != 2) {
                std::cerr << "FAIL: ConsoleView scroll range is wrong (max " << view.GetMaxScroll()
                          << ", offset " << view.GetScrollOffset() << ")\n";
                return 1;
            }
            view.ScrollBy(-5);
            if(view.GetScrollOffset() != 0) {
                std::cerr << "FAIL: ConsoleView did not clamp a negative scroll\n";
                return 1;
            }

            if(view.GetStatusText().find("3 entries") == std::string::npos ||
               view.GetStatusText().find("1 error") == std::string::npos) {
                std::cerr << "FAIL: ConsoleView status text is wrong: " << view.GetStatusText() << "\n";
                return 1;
            }

            if(!view.Sync() || view.Sync()) {
                std::cerr << "FAIL: ConsoleView::Sync did not report exactly one change\n";
                return 1;
            }
        }

        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_console_selftest";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);

        {
            std::ofstream out(dir / "talk.gs", std::ios::binary | std::ios::trunc);
            if(!out.good()) {
                std::cerr << "FAIL: could not write the console test script\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            out << "function Update(self, dt)\n"
                   "    Log(\"hello from gs\")\n"
                   "    Print(\"printed line\")\n"
                   "    return 0\n"
                   "end function\n";
        }

        // ---- 3. PlaySession captures Log/Print and failures ------------------
        {
            Scene scene;
            Entity &talker = scene.CreateEntity("Talker");
            talker.script = ScriptComponent{"talk.gs"};

            Entity &broken = scene.CreateEntity("Broken");
            broken.script = ScriptComponent{"broken.gs"};

            PlaySession session;
            std::string error;
            // broken.gs does not exist -> a compile error, but talk.gs still runs.
            if(!session.Start(scene, dir.generic_string(), &error)) {
                std::cerr << "FAIL: console selftest could not start play: " << error << "\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            session.Tick(scene, 0.0f);

            bool sawCompileError = false;
            bool sawLog = false;
            bool sawPrint = false;
            for(const auto &entry : session.GetLog().GetEntries()) {
                if(entry.severity == LogSeverity::Error &&
                   entry.message.find("broken.gs") != std::string::npos &&
                   entry.source.find("Broken") != std::string::npos) {
                    sawCompileError = true;
                }
                if(entry.severity == LogSeverity::Info &&
                   entry.source.find("Talker") != std::string::npos) {
                    if(entry.message == "hello from gs")
                        sawLog = true;
                    if(entry.message == "printed line")
                        sawPrint = true;
                }
            }

            if(!sawCompileError || !sawLog || !sawPrint) {
                std::cerr << "FAIL: console capture missing (compile " << sawCompileError
                          << ", Log " << sawLog << ", Print " << sawPrint << ")\n"
                          << session.GetLog().FormatAll();
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            session.Stop(scene);
            if(session.GetLog().IsEmpty()) {
                std::cerr << "FAIL: the console log did not survive Stop()\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            // A fresh session starts a fresh console.
            if(!session.Start(scene, dir.generic_string(), &error) || session.GetLog().IsEmpty()) {
                std::cerr << "FAIL: a second play session did not reset the console\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            session.Stop(scene);
        }

        // ---- 4. Compile-only syntax check -----------------------------------
        {
            PlaySession compiler;
            std::string error;

            const std::string good =
                "function Update(self, dt)\n"
                "    self.x = self.x + 10 * dt\n"
                "    Log(\"compiled, never run\")\n"
                "    return 0\n"
                "end function\n";

            if(!compiler.CheckSyntax(good, "good.gs", &error) || !error.empty()) {
                std::cerr << "FAIL: CheckSyntax rejected a valid script: " << error << "\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            if(!compiler.GetLog().IsEmpty() || !compiler.GetLastError().empty() || compiler.IsRunning()) {
                std::cerr << "FAIL: CheckSyntax executed the script or left state behind\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            if(!compiler.CheckSyntax("x = 1\n", "partial.gs", &error)) {
                std::cerr << "FAIL: CheckSyntax rejected a half written script: " << error << "\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }

            if(compiler.CheckSyntax("function Update(self, dt)\n    if self.x == 1\n        self.x = 2\n"
                                    "    end if\n    return 0\nend function\n", "badif.gs", &error) ||
               error.empty()) {
                std::cerr << "FAIL: CheckSyntax accepted a script with a parse error\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
        }

        std::filesystem::remove_all(dir, ec);
        return 0;
    }

    /// Hot-reload: FileWatcher polling on disk touch for PNG sprites (Viewport cache
    /// invalidation + reload), added/removed files (AssetLibrary refresh), and .gs scripts
    /// (PlaySession::ReloadScripts live reload + compile error logging).
    int RunHotReloadSelfTest() {
        using namespace Gorgon::Editor;

        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_hotreload_selftest";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);

        const auto pngPath = dir / "hero.png";
        const auto scriptPath = dir / "mover.gs";
        if(!WriteTinyPng(pngPath)) {
            std::cerr << "FAIL: hot-reload selftest could not write hero.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        {
            std::ofstream out(scriptPath, std::ios::binary | std::ios::trunc);
            out << "function Update(self, dt)\n"
                << "    self.x = self.x + 10.0 * dt\n"
                << "    return 0\n"
                << "end function\n";
        }

        FileWatcher watcher;
        watcher.SetIntervalSeconds(0.5);
        watcher.SetRoot(dir.generic_string());
        if(watcher.GetTrackedFileCount() != 2 || !watcher.Poll().empty()) {
            std::cerr << "FAIL: hot-reload FileWatcher baseline snapshot mismatch\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // 1. Sprite texture invalidation on disk touch.
        Scene scene;
        Entity &hero = scene.CreateEntity("Hero");
        hero.sprite = SpriteComponent{"hero.png"};
        hero.script = ScriptComponent{"mover.gs"};
        const EntityId heroId = hero.id;

        EditorViewport viewport(scene);
        viewport.SetAssetBaseDir(dir.generic_string());
        // Populate the sprite cache entry via ComputeEntityBaseSize.
        (void)viewport.ComputeEntityBaseSize(*scene.FindEntity(heroId));
        if(!viewport.HasCachedSpriteEntry("hero.png")) {
            std::cerr << "FAIL: hot-reload selftest did not populate Viewport sprite cache\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // Touch hero.png on disk and bump its write time.
        if(!WriteTinyPng(pngPath)) {
            std::cerr << "FAIL: hot-reload selftest could not rewrite hero.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        const auto bumpedPng = std::filesystem::last_write_time(pngPath, ec) + std::chrono::seconds(5);
        std::filesystem::last_write_time(pngPath, bumpedPng, ec);

        auto changes = watcher.Tick(0.5);
        if(changes.size() != 1 || changes[0].kind != FileChangeKind::Modified || changes[0].extension != ".png") {
            std::cerr << "FAIL: FileWatcher did not detect modified hero.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(!viewport.InvalidateSprite(changes[0].path) || viewport.HasCachedSpriteEntry("hero.png")) {
            std::cerr << "FAIL: Viewport::InvalidateSprite did not evict modified hero.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        // Re-querying repopulates the cache from disk.
        (void)viewport.ComputeEntityBaseSize(*scene.FindEntity(heroId));
        if(!viewport.HasCachedSpriteEntry("hero.png")) {
            std::cerr << "FAIL: Viewport did not reload evicted hero.png on next query\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // 2. Added & removed asset detection + AssetLibrary refresh.
        AssetLibrary library;
        library.SetRoot(dir.generic_string());
        const int initialCount = library.GetVisibleCount();

        const auto extraPng = dir / "extra.png";
        WriteTinyPng(extraPng);
        changes = watcher.Tick(0.5);
        if(changes.size() != 1 || changes[0].kind != FileChangeKind::Added) {
            std::cerr << "FAIL: FileWatcher did not detect added extra.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        library.Refresh();
        if(library.GetVisibleCount() != initialCount + 1) {
            std::cerr << "FAIL: AssetLibrary::Refresh did not pick up added extra.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        std::filesystem::remove(extraPng, ec);
        changes = watcher.Tick(0.5);
        if(changes.size() != 1 || changes[0].kind != FileChangeKind::Removed) {
            std::cerr << "FAIL: FileWatcher did not detect removed extra.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        library.Refresh();
        if(library.GetVisibleCount() != initialCount) {
            std::cerr << "FAIL: AssetLibrary::Refresh did not drop removed extra.png\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // 3. Script hot-reload on disk touch (live reload + error logging).
        PlaySession session;
        std::string error;
        if(!session.Start(scene, dir.generic_string(), &error)) {
            std::cerr << "FAIL: hot-reload selftest could not start PlaySession: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        session.Tick(scene, 0.2f);
        if(!Approx(scene.FindEntity(heroId)->transform.x, 2.0f, 1e-2f)) {
            std::cerr << "FAIL: hot-reload selftest initial script tick mismatch\n";
            session.Stop(scene);
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        {
            std::ofstream out(scriptPath, std::ios::binary | std::ios::trunc);
            out << "function Update(self, dt)\n"
                << "    self.x = self.x + 50.0 * dt\n"
                << "    return 0\n"
                << "end function\n";
        }
        const auto bumpedScript = std::filesystem::last_write_time(scriptPath, ec) + std::chrono::seconds(5);
        std::filesystem::last_write_time(scriptPath, bumpedScript, ec);

        changes = watcher.Tick(0.5);
        if(changes.size() != 1 || changes[0].kind != FileChangeKind::Modified || changes[0].extension != ".gs") {
            std::cerr << "FAIL: FileWatcher did not detect modified mover.gs\n";
            session.Stop(scene);
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        if(!session.ReloadScripts(scene, changes[0].path, &error)) {
            std::cerr << "FAIL: PlaySession::ReloadScripts failed on valid script: " << error << "\n";
            session.Stop(scene);
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        session.Tick(scene, 0.2f);
        if(!Approx(scene.FindEntity(heroId)->transform.x, 12.0f, 1e-2f)) {
            std::cerr << "FAIL: hot-reloaded script did not apply updated speed (got "
                      << scene.FindEntity(heroId)->transform.x << ")\n";
            session.Stop(scene);
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        // Syntax error on disk logs an error to the console.
        {
            std::ofstream out(scriptPath, std::ios::binary | std::ios::trunc);
            out << "function Update(self, dt)\n"
                << "    if self.x == 1\n"
                << "    end if\n"
                << "end function\n";
        }
        const auto bumpedBad = std::filesystem::last_write_time(scriptPath, ec) + std::chrono::seconds(5);
        std::filesystem::last_write_time(scriptPath, bumpedBad, ec);
        changes = watcher.Tick(0.5);
        if(changes.size() != 1 || session.ReloadScripts(scene, changes[0].path, &error) ||
           session.GetLog().CountOf(LogSeverity::Error) == 0) {
            std::cerr << "FAIL: hot-reload did not report compile error for broken script\n";
            session.Stop(scene);
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        session.Stop(scene);
        std::filesystem::remove_all(dir, ec);
        return 0;
    }

    /// 2D lighting without a window: a lightmap is pure arithmetic, so a headless run can prove it
    /// brightens towards its light, cuts off past the radius and at a spot's cone edge, casts a
    /// shadow from a collider, and does nothing at all for a scene without lights.

    /// In-game UI: anchor math, the click/hover state machine, the UI script library driving a real
    /// .gs headlessly, and a JSON round-trip. Needs no window.
    int RunUserInterfaceSelfTest() {
        using namespace Gorgon::Editor;

        Scene scene;
        Entity &button = scene.CreateEntity("SelftestButton");
        button.ui = UIElementComponent{};
        button.ui->kind = UIElementKind::Button;
        button.ui->id = "go";
        button.ui->text = "Go";
        button.ui->width = 160.0f;
        button.ui->height = 48.0f;
        button.ui->anchorH = UIAnchorH::Right;
        button.ui->anchorV = UIAnchorV::Bottom;
        button.ui->x = 20.0f;
        button.ui->y = 20.0f;
        const EntityId buttonId = button.id;

        Entity &label = scene.CreateEntity("SelftestLabel");
        label.ui = UIElementComponent{};
        label.ui->kind = UIElementKind::Label;
        label.ui->id = "score";
        label.ui->text = "Score";
        const EntityId labelId = label.id;

        // A corner button hangs off the bottom-right corner and keeps its gap when the view resizes.
        UserInterface ui;
        ui.Layout(scene, 800, 600);
        if(ui.GetWidgetCount() != 2) {
            std::cerr << "FAIL: the UI laid out " << ui.GetWidgetCount() << " widgets instead of 2\n";
            return 1;
        }
        const auto *buttonWidget = ui.GetWidget("go");
        if(!buttonWidget || !Approx(buttonWidget->rect.Right(), 780.0f) ||
           !Approx(buttonWidget->rect.Bottom(), 580.0f)) {
            std::cerr << "FAIL: the right/bottom anchored button is not in its corner\n";
            return 1;
        }
        ui.Layout(scene, 1280, 720);
        buttonWidget = ui.GetWidget("go");
        if(!buttonWidget || !Approx(buttonWidget->rect.Right(), 1260.0f) ||
           !Approx(buttonWidget->rect.Bottom(), 700.0f)) {
            std::cerr << "FAIL: the anchored button moved when the view was resized\n";
            return 1;
        }
        const auto *labelWidget = ui.GetWidget("score");
        if(!labelWidget || !Approx(labelWidget->rect.x, 24.0f) || !Approx(labelWidget->rect.y, 24.0f)) {
            std::cerr << "FAIL: the top-left label is not at its offset\n";
            return 1;
        }

        // Click state: hover, press, one click on the release, nothing on the frame after it.
        const float cx = buttonWidget->rect.CenterX();
        const float cy = buttonWidget->rect.CenterY();
        ui.UpdateInput(0.0f, 0.0f, false);
        if(ui.CountHovered() != 0) {
            std::cerr << "FAIL: a widget reported hover with the pointer in the corner\n";
            return 1;
        }
        ui.UpdateInput(cx, cy, true);
        if(!ui.IsHovered("go") || !ui.IsPressed("go") || ui.CountClicked() != 0) {
            std::cerr << "FAIL: press state is wrong on the button-down frame\n";
            return 1;
        }
        ui.UpdateInput(cx, cy, false);
        if(ui.CountClicked() != 1 || !ui.IsClicked("go") || ui.IsPressed("go")) {
            std::cerr << "FAIL: the release did not produce exactly one click\n";
            return 1;
        }
        ui.UpdateInput(cx, cy, false);
        if(ui.CountClicked() != 0) {
            std::cerr << "FAIL: the click lasted more than one frame\n";
            return 1;
        }

        // A real script drives the same button through the UI library: two clicks, two increments.
        const auto dir = std::filesystem::temp_directory_path() / "gorgon_ui_selftest";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        {
            std::ofstream out(dir / "menu.gs", std::ios::binary | std::ios::trunc);
            out << "using UI\n"
                   "hits = 0\n"
                   "function Update(self, dt)\n"
                   "    c = UI_IsClicked(\"go\")\n"
                   "    if(c > 0)\n"
                   "        hits = hits + 1\n"
                   "    end if\n"
                   "    self.x = hits\n"
                   "    return 0\n"
                   "end function\n";
        }

        Scene scriptScene;
        Entity &scriptButton = scriptScene.CreateEntity("ScriptButton");
        scriptButton.ui = UIElementComponent{};
        scriptButton.ui->id = "go";
        const EntityId driverId = scriptScene.CreateEntity("Driver").id;
        scriptScene.FindEntity(driverId)->script = ScriptComponent{"menu.gs"};

        PlaySession session;
        std::string error;
        if(!session.Start(scriptScene, dir.generic_string(), &error)) {
            std::cerr << "FAIL: the UI script did not start: " << error << "\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }

        UserInterface &sessionUI = session.GetUserInterface();
        sessionUI.Layout(scriptScene, 800, 600);
        const auto *scriptWidget = sessionUI.GetWidget("go");
        if(!scriptWidget) {
            std::cerr << "FAIL: the session UI has no widget for the scripted button\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        const float sx = scriptWidget->rect.CenterX();
        const float sy = scriptWidget->rect.CenterY();

        sessionUI.UpdateInput(sx, sy, true);
        session.Tick(scriptScene, 0.0f);
        sessionUI.UpdateInput(sx, sy, false);
        session.Tick(scriptScene, 0.0f);
        sessionUI.UpdateInput(sx, sy, false);
        session.Tick(scriptScene, 0.0f);
        sessionUI.UpdateInput(sx, sy, true);
        session.Tick(scriptScene, 0.0f);
        sessionUI.UpdateInput(sx, sy, false);
        session.Tick(scriptScene, 0.0f);

        if(!Approx(scriptScene.FindEntity(driverId)->transform.x, 2.0f)) {
            std::cerr << "FAIL: the script counted "
                      << scriptScene.FindEntity(driverId)->transform.x
                      << " clicks instead of 2\n";
            std::filesystem::remove_all(dir, ec);
            return 1;
        }
        session.Stop(scriptScene);
        std::filesystem::remove_all(dir, ec);

        // The component survives a scene file: a UI element is authored data, not runtime state.
        Scene reloaded;
        const auto path = std::filesystem::temp_directory_path() / "gorgon_ui_selftest.gscene.json";
        if(!SceneSerializer::SaveToFile(scene, path.generic_string(), &error) ||
           !SceneSerializer::LoadFromFile(path.generic_string(), reloaded, &error)) {
            std::cerr << "FAIL: UI round-trip failed: " << error << "\n";
            std::filesystem::remove(path);
            return 1;
        }
        std::filesystem::remove(path);

        const Entity *reloadedButton = reloaded.FindEntity(buttonId);
        const Entity *reloadedLabel = reloaded.FindEntity(labelId);
        if(!reloadedButton || !reloadedButton->ui || reloadedButton->ui->id != "go" ||
           reloadedButton->ui->kind != UIElementKind::Button ||
           reloadedButton->ui->anchorH != UIAnchorH::Right) {
            std::cerr << "FAIL: the button did not survive save/load\n";
            return 1;
        }
        if(!reloadedLabel || !reloadedLabel->ui || reloadedLabel->ui->kind != UIElementKind::Label) {
            std::cerr << "FAIL: the label did not survive save/load\n";
            return 1;
        }

        // The exported scene lays out exactly like the authored one.
        SceneRuntime runtime;
        if(!runtime.LoadFromScene(reloaded, std::string(), &error)) {
            std::cerr << "FAIL: the reloaded UI scene did not load in the runtime\n";
            return 1;
        }
        runtime.SetViewSize(800, 600);
        runtime.Update(0.0f);
        Gorgon::Graphics::Layer layer;
        runtime.Render(layer, 800, 600);
        if(runtime.GetUserInterface().GetWidgetCount() != 2) {
            std::cerr << "FAIL: the runtime laid out "
                      << runtime.GetUserInterface().GetWidgetCount() << " widgets instead of 2\n";
            return 1;
        }
        if(runtime.GetUserInterface().GetLastDrawnQuadCount() < 2) {
            std::cerr << "FAIL: the runtime drew nothing for the UI\n";
            return 1;
        }

        return 0;
    }

    int RunLightingSelfTest() {
        using namespace Gorgon::Editor;

        Scene scene;
        Entity &lamp = scene.CreateEntity("SelftestLamp");
        lamp.transform.x = 0.0f;
        lamp.transform.y = 0.0f;

        Light2DComponent light;
        light.color = ColorRGBA{255, 255, 255, 255};
        light.radius = 64.0f;
        light.intensity = 1.0f;
        light.castShadows = true;
        lamp.light = light;
        // Scene stores entities in a vector, so the reference above dies on the next CreateEntity.
        const EntityId lampId = lamp.id;

        const Gorgon::Geometry::Rectanglef world{-128.0f, -128.0f, 256.0f, 256.0f};

        Lightmap open;
        if(!BuildSceneLightmap(scene, world, 64, 64, open)) {
            std::cerr << "FAIL: the lightmap builder ignored a scene with a light\n";
            return 1;
        }
        if(open.IsUniform()) {
            std::cerr << "FAIL: the headless lightmap is uniform with a light present\n";
            return 1;
        }
        if(!(open.SampleWorld(0.0f, 0.0f) > open.SampleWorld(0.0f, 30.0f))) {
            std::cerr << "FAIL: the lightmap is not brighter near the light\n";
            return 1;
        }
        if(open.SampleWorld(0.0f, 80.0f) != 0.0f) {
            std::cerr << "FAIL: the lightmap still lights past the light radius\n";
            return 1;
        }

        // A collider between the light and a sample must darken it.
        Entity &wall = scene.CreateEntity("SelftestWall");
        wall.transform.x = 24.0f;
        wall.transform.y = 0.0f;
        wall.collider = ColliderComponent{ColliderType::AABB, 12.0f, 12.0f, 6.0f, 0.0f, 0.0f};

        Lightmap shadowed;
        if(!BuildSceneLightmap(scene, world, 64, 64, shadowed)) {
            std::cerr << "FAIL: the lightmap builder failed once a caster existed\n";
            return 1;
        }
        if(shadowed.GetShadowCasterCount() != 1) {
            std::cerr << "FAIL: the lightmap did not pick up the collider as a shadow caster\n";
            return 1;
        }
        if(shadowed.SampleWorld(48.0f, 0.0f) != 0.0f) {
            std::cerr << "FAIL: the collider did not cast a shadow\n";
            return 1;
        }

        // A spot cuts off outside its cone.
        scene.FindEntity(lampId)->light->type = LightType::Spot;
        scene.FindEntity(lampId)->light->coneAngle = 60.0f;
        scene.FindEntity(lampId)->light->direction = 180.0f;   // -x, away from the wall
        Lightmap spot;
        if(!BuildSceneLightmap(scene, world, 64, 64, spot)) {
            std::cerr << "FAIL: the lightmap builder failed for a spot light\n";
            return 1;
        }
        if(spot.SampleWorld(-48.0f, 0.0f) <= 0.0f) {
            std::cerr << "FAIL: the spot light does not light the inside of its cone\n";
            return 1;
        }
        if(spot.SampleWorld(0.0f, 48.0f) != 0.0f) {
            std::cerr << "FAIL: the spot light leaked outside its cone\n";
            return 1;
        }

        // A scene without lights skips the pass entirely.
        Scene unlit;
        unlit.CreateEntity("Nothing");
        Lightmap none;
        if(BuildSceneLightmap(unlit, world, 64, 64, none)) {
            std::cerr << "FAIL: an unlit scene still reported a lightmap\n";
            return 1;
        }
        if(none.IsActive() || none.GetLightCount() != 0) {
            std::cerr << "FAIL: an unlit scene left an active lightmap behind\n";
            return 1;
        }

        // The runtime hands the same component to a game, so a light survives a scene file.
        std::string error;
        const auto path = (std::filesystem::temp_directory_path() / "gorgon_lighting_selftest.gscene.json");
        if(!SceneSerializer::SaveToFile(scene, path.string(), &error) ||
           !SceneSerializer::LoadFromFile(path.string(), unlit, &error)) {
            std::cerr << "FAIL: lighting round-trip failed: " << error << "\n";
            std::filesystem::remove(path);
            return 1;
        }
        std::filesystem::remove(path);

        const Entity *reloaded = unlit.FindEntity(lampId);
        if(!reloaded || !reloaded->light.has_value() || reloaded->light->type != LightType::Spot) {
            std::cerr << "FAIL: the light component did not survive save/load\n";
            return 1;
        }

        return 0;
    }

} // namespace

int main(int argc, char *argv[]) {
    bool selftest = false;
    bool smoke = false;
    std::string diagVariant;
    std::string initialScenePath;
    std::string exportScene;
    std::string exportOutput;

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
        } else if(arg == "--export") {
            if(i + 2 < argc) {
                exportScene = argv[++i];
                exportOutput = argv[++i];
            } else {
                std::cerr << "usage: GorgonEditor --export <scene.gscene.json> <outputDir>\n";
                return 1;
            }
        } else if(initialScenePath.empty() && !arg.empty() && arg[0] != '-') {
            initialScenePath = arg;
        }
    }

    if(!diagVariant.empty()) {
        return RunDiag(diagVariant);
    }

    // Headless packaging runs before Gorgon::Initialize: it needs no window.
    if(!exportScene.empty() || !exportOutput.empty()) {
        return RunExport(exportScene, exportOutput);
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

        // ---- Console panel & built-in script editor -------------------------
        {
            using namespace Gorgon::Editor;

            std::error_code cec;
            const auto workDir = std::filesystem::temp_directory_path() / "gorgon_editor_smoke_console";
            std::filesystem::remove_all(workDir, cec);
            std::filesystem::create_directories(workDir, cec);

            const auto scriptPath = (workDir / "talk.gs").generic_string();
            {
                std::ofstream out(scriptPath, std::ios::binary | std::ios::trunc);
                if(!out.good()) {
                    std::cerr << "FAIL: could not write the smoke console script\n";
                    window.Close();
                    return 1;
                }
                out << "function Update(self, dt)\n"
                       "    Log(\"hello from the smoke test\")\n"
                       "    return 0\n"
                       "end function\n";
            }

            auto fail = [&](const std::string &message) -> int {
                std::cerr << "FAIL: " << message << "\n";
                window.Close();
                return 1;
            };

            // -- Toggle the bottom dock to the console and log through it.
            app.ShowBottomTab(BottomTab::Console);
            Gorgon::NextFrame();
            if(app.GetBottomTab() != BottomTab::Console)
                return fail("ShowBottomTab(Console) did not switch the bottom dock");

            app.LogMessage(LogSeverity::Info, "smoke console line", "Smoke");
            Gorgon::NextFrame();

            auto &consolePanel = app.GetConsolePanel();
            if(consolePanel.GetLineCount() < 1)
                return fail("the console panel did not show the logged line");
            // The active tab is laid out inside the bottom dock; the inactive one is parked.
            if(consolePanel.GetPanel().GetCurrentLocation().X < 0)
                return fail("the console panel is parked off-screen instead of in the bottom dock");
            if(consolePanel.GetListBox().GetCurrentSize().Width < 100 ||
               consolePanel.GetListBox().GetCurrentSize().Height < 40)
                return fail("the console log view has no room to draw in");
            if(consolePanel.GetStatusText().find("entr") == std::string::npos)
                return fail("the console status text is empty: " + consolePanel.GetStatusText());

            // -- All/Errors filter: info is hidden, warnings and errors stay.
            consolePanel.SetErrorsOnly(true);
            const int filteredLines = consolePanel.GetLineCount();
            app.LogMessage(LogSeverity::Info, "info while filtered", "Smoke");
            app.LogMessage(LogSeverity::Error, "error while filtered", "Smoke");
            Gorgon::NextFrame();
            if(consolePanel.GetLineCount() != filteredLines + 1)
                return fail("the Errors filter did not hide the info line and keep the error");
            consolePanel.SetErrorsOnly(false);

            // -- Clear empties the log (the panel then reports the clear itself).
            consolePanel.Clear();
            Gorgon::NextFrame();
            if(consolePanel.GetLineCount() > 1)
                return fail("Clear did not empty the console");
            if(consolePanel.GetLineCount() == 1 &&
               consolePanel.GetLineText(0).find("Console cleared") == std::string::npos)
                return fail("Clear left an unexpected line behind: " + consolePanel.GetLineText(0));

            // -- Script editor: open the file, edit it, check it, save it.
            std::string openError;
            if(!app.OpenScriptEditor(scriptPath, &openError))
                return fail("OpenScriptEditor failed: " + openError);
            Gorgon::NextFrame();

            auto &editor = app.GetScriptEditor();
            if(!editor.IsOpen())
                return fail("the script editor did not open");
            if(editor.GetText().find("hello from the smoke test") == std::string::npos)
                return fail("the script editor did not load the file contents");

            editor.SetText("function Update(self, dt)\n"
                           "    Log(\"edited in the smoke test\")\n"
                           "    return 0\n"
                           "end function\n");
            if(!editor.IsDirty())
                return fail("editing the buffer did not mark the editor dirty");
            if(!editor.CheckSyntax())
                return fail("CheckSyntax rejected a valid buffer: " + editor.GetStatus());
            if(editor.GetStatus().find("Syntax OK") == std::string::npos)
                return fail("the editor did not report Syntax OK: " + editor.GetStatus());

            // A parse error is reported by the editor and written to the console.
            editor.SetText("function Update(self, dt)\n    if self.x == 1\n    end if\nend function\n");
            if(editor.CheckSyntax())
                return fail("CheckSyntax accepted a buffer with a parse error");
            if(editor.GetStatus().find("Syntax error") == std::string::npos)
                return fail("the editor did not report the syntax error: " + editor.GetStatus());

            Gorgon::NextFrame();
            bool sawSyntaxError = false;
            for(int i = 0; i < consolePanel.GetLineCount(); ++i) {
                if(consolePanel.GetLineText(i).find("Syntax error") != std::string::npos)
                    sawSyntaxError = true;
            }
            if(!sawSyntaxError)
                return fail("the syntax error never reached the console");

            // -- Save writes the buffer back and clears the dirty flag.
            editor.SetText("function Update(self, dt)\n"
                           "    Log(\"saved from the smoke test\")\n"
                           "    return 0\n"
                           "end function\n");
            std::string saveError;
            if(!editor.Save(&saveError))
                return fail("saving the script failed: " + saveError);
            if(editor.IsDirty())
                return fail("Save did not clear the dirty flag");
            {
                std::ifstream written(scriptPath, std::ios::binary);
                std::stringstream contents;
                contents << written.rdbuf();
                if(contents.str().find("saved from the smoke test") == std::string::npos)
                    return fail("Save did not write the buffer to disk");
            }

            // -- Double clicking a .gs in the asset browser opens it in the editor.
            editor.Close();
            app.ShowBottomTab(BottomTab::Assets);
            Gorgon::NextFrame();

            auto &browser = app.GetAssetBrowser();
            browser.SetRoot(workDir.generic_string());
            Gorgon::NextFrame();
            if(browser.GetLibrary().GetVisibleCount() != 1)
                return fail("the smoke script folder did not list exactly one asset");

            if(app.GetAssetBrowser().GetPanel().GetCurrentLocation().X < 0)
                return fail("switching back to Assets did not put the asset browser back in the dock");

            const auto tile = browser.GetTileBounds(0);
            const Gorgon::Geometry::Point tilePoint{tile.X + 6, tile.Y + 6};
            browser.HandleMouseDown(tilePoint, Gorgon::Input::Mouse::Button::Left);
            browser.HandleMouseDown(tilePoint, Gorgon::Input::Mouse::Button::Left);
            Gorgon::NextFrame();

            if(!app.GetScriptEditor().IsOpen())
                return fail("double clicking a .gs did not open the script editor");
            if(app.GetScriptEditor().GetText().find("saved from the smoke test") == std::string::npos)
                return fail("the script editor opened the wrong file");

            app.GetScriptEditor().Close();
            app.ShowBottomTab(BottomTab::Console);
            std::filesystem::remove_all(workDir, cec);
            Gorgon::NextFrame();
        }

        // ---- Standalone export: the toolbar path, end to end -----------------
        {
            using namespace Gorgon::Editor;

            std::error_code eec;
            const auto exportDir =
                std::filesystem::temp_directory_path() / "gorgon_editor_smoke_export";
            std::filesystem::remove_all(exportDir, eec);

            ExportOptions exportOptions;
            exportOptions.outputDir = exportDir.generic_string();
            const ExportResult exported = app.ExportGame(exportOptions);

            if(!exported.ok) {
                std::cerr << "FAIL: export failed: " << exported.error << "\n";
                window.Close();
                return 1;
            }
            if(!std::filesystem::exists(exported.manifestPath) ||
               !std::filesystem::exists(exported.scenePath)) {
                std::cerr << "FAIL: export did not write game.json and the exported scene\n";
                std::filesystem::remove_all(exportDir, eec);
                window.Close();
                return 1;
            }

            GameManifest manifest;
            std::string manifestError;
            if(!GameExporter::LoadManifestFromFile(exported.manifestPath, manifest, &manifestError)) {
                std::cerr << "FAIL: the exported game.json is unreadable: " << manifestError << "\n";
                std::filesystem::remove_all(exportDir, eec);
                window.Close();
                return 1;
            }
            if(manifest.startScene != "scene.gscene.json" ||
               !std::filesystem::exists(std::filesystem::path(exportDir) / manifest.startScene)) {
                std::cerr << "FAIL: game.json points at a missing scene: " << manifest.startScene << "\n";
                std::filesystem::remove_all(exportDir, eec);
                window.Close();
                return 1;
            }

            // The report is the console line: the export has to say where it wrote the bundle.
            app.ShowBottomTab(BottomTab::Console);
            Gorgon::NextFrame();

            auto &exportConsole = app.GetConsolePanel();
            bool sawExportLine = false;
            for(int i = 0; i < exportConsole.GetLineCount(); ++i) {
                if(exportConsole.GetLineText(i).find("Exported '") != std::string::npos)
                    sawExportLine = true;
            }
            if(!sawExportLine) {
                std::cerr << "FAIL: the export result never reached the console\n";
                std::filesystem::remove_all(exportDir, eec);
                window.Close();
                return 1;
            }

            {
                std::ofstream report(std::filesystem::temp_directory_path() /
                                         "gorgon_editor_export_report.txt",
                                     std::ios::trunc);
                report << "outputDir=" << exported.outputDir << "\n"
                       << "manifest=" << exported.manifestPath << "\n"
                       << "scene=" << exported.scenePath << "\n"
                       << "assets=" << exported.copiedAssets.size() << "\n"
                       << "missing=" << exported.missingAssets.size() << "\n"
                       << "dlls=" << exported.copiedRuntimeDlls.size() << "\n"
                       << "player=" << exported.playerExecutablePath << "\n";
            }

            std::filesystem::remove_all(exportDir, eec);
            app.ShowBottomTab(BottomTab::Assets);
            Gorgon::NextFrame();
        }

        // ---- Phase 6: tilemap painting (tool mode, brush, palette, undo) ------
        {
            using namespace Gorgon::Editor;

            auto fail = [&](const std::string &message) {
                std::cerr << "FAIL: " << message << "\n";
                window.Close();
                return 1;
            };

            if(picked == InvalidEntityId)
                return fail("the tilemap paint phase needs a selectable entity");

            Entity *target = scene.FindEntity(picked);
            if(!target)
                return fail("the tilemap paint target disappeared");

            const Transform savedTransform = target->transform;

            // Give the entity a 4x4 grid centred on the world origin, then reopen the scene so the
            // editor treats it as the committed baseline (undo must not un-attach the component).
            TilemapComponent map;
            map.cols = 4;
            map.rows = 4;
            map.tileW = 32.0f;
            map.tileH = 32.0f;
            map.tilesetCols = 4;
            map.tiles.assign(16, -1);
            target->tilemap = map;
            target->transform = Transform{};

            std::error_code pec;
            const auto paintScenePath =
                std::filesystem::temp_directory_path() / "gorgon_editor_smoke_tiles.gscene.json";
            std::string paintError;
            if(!app.SaveSceneFile(paintScenePath.string(), &paintError)) {
                std::filesystem::remove(paintScenePath, pec);
                return fail("could not save the tilemap smoke scene: " + paintError);
            }
            if(!app.OpenSceneFile(paintScenePath.string(), &paintError)) {
                std::filesystem::remove(paintScenePath, pec);
                return fail("could not reopen the tilemap smoke scene: " + paintError);
            }
            std::filesystem::remove(paintScenePath, pec);

            app.GetViewport().SetSelectedEntity(picked, true);
            Gorgon::NextFrame();

            if(!app.GetViewport().CanPaintTilemap())
                return fail("the reopened tilemap scene is not paintable");

            // Arm through the same path the toolbar button and the `T` shortcut use.
            if(!app.GetViewport().SetTilemapToolActive(true))
                return fail("the tile tool refused to arm on a tilemap");
            if(!app.GetViewport().IsTilemapToolActive())
                return fail("the tile tool reported armed but is inactive");

            const auto viewSize = app.GetViewport().GetViewportSize();
            const float viewW = static_cast<float>(viewSize.Width);
            const float viewH = static_cast<float>(viewSize.Height);

            const TilemapComponent &mapRef = *scene.FindEntity(picked)->tilemap;
            const TilemapHit cell = app.GetViewport().ScreenToTilemapCell(viewW * 0.5f, viewH * 0.5f);
            if(!cell.inside)
                return fail("the viewport centre did not map into the tilemap");

            // Aim at the middle of that cell rather than the exact view centre: the exact centre
            // of an 811px-wide view lands on a cell boundary, so which cell gets painted would be
            // a coin flip. The entity sits at the origin with no rotation or scale.
            float cellLocalX = 0.0f;
            float cellLocalY = 0.0f;
            TilemapPainter::CellOrigin(mapRef, cell.col, cell.row, cellLocalX, cellLocalY);
            const auto aim = app.GetViewport().GetCamera().WorldToScreen(
                cellLocalX + mapRef.tileW * 0.5f, cellLocalY + mapRef.tileH * 0.5f, viewW, viewH);
            const float px = aim.X;
            const float py = aim.Y;

            if(app.GetViewport().ScreenToTilemapCell(px, py) != cell)
                return fail("the aimed pixel does not belong to the picked cell");

            // The palette strip must offer swatches without covering that pixel.
            const auto &palette = app.GetViewport().GetTilePaletteLayout();
            float swx = 0.0f, swy = 0.0f, sww = 0.0f, swh = 0.0f;
            if(palette.count < 2 || !palette.SwatchRect(1, swx, swy, sww, swh))
                return fail("the tile palette strip has no swatches");
            if(app.GetViewport().HitTestTilePalette(px, py) >= 0)
                return fail("the palette strip covers the paint pixel");

            app.GetViewport().SetPaintTile(3);
            if(app.GetViewport().GetPaintTile() != 3)
                return fail("the paint tile did not stick");

            const std::size_t cellIndex = std::size_t(cell.row) * std::size_t(mapRef.cols) +
                                          std::size_t(cell.col);

            // -- paint one cell with the brush ----------------------------------
            const int undoBefore = int(app.GetCommandStack().GetUndoCount());
            app.GetViewport().HandleMouseDown({int(px), int(py)}, Gorgon::Input::Mouse::Button::Left);
            app.GetViewport().HandleMouseUp({int(px), int(py)}, Gorgon::Input::Mouse::Button::Left);

            const Entity *painted = scene.FindEntity(picked);
            if(!painted || !painted->tilemap)
                return fail("the paint stroke dropped the tilemap component");
            if(cellIndex >= painted->tilemap->tiles.size() || painted->tilemap->tiles[cellIndex] != 3)
                return fail("the brush did not paint the cell under the cursor");
            if(app.GetUndoName() != "Paint Tiles")
                return fail("the paint stroke did not become a 'Paint Tiles' undo step");
            if(int(app.GetCommandStack().GetUndoCount()) != undoBefore + 1)
                return fail("the paint stroke was not exactly one undo step");

            // -- Undo restores the pre-stroke cell -------------------------------
            app.Undo();
            const Entity *undone = scene.FindEntity(picked);
            if(!undone || !undone->tilemap)
                return fail("undo removed the tilemap component");
            if(undone->tilemap->tiles[cellIndex] != -1)
                return fail("undo did not restore the painted cell");

            // -- clicking the palette picks a tile instead of painting -----------
            app.GetViewport().HandleMouseDown({int(swx + sww * 0.5f), int(swy + swh * 0.5f)},
                                              Gorgon::Input::Mouse::Button::Left);
            app.GetViewport().HandleMouseUp({int(swx + sww * 0.5f), int(swy + swh * 0.5f)},
                                            Gorgon::Input::Mouse::Button::Left);
            if(app.GetViewport().GetPaintTile() != 1)
                return fail("clicking the palette did not select tile 1");

            // -- a rectangle fill drag is one undo step that restores every cell --
            app.GetViewport().SetPaintTile(5);
            app.GetViewport().SetTilemapTool(TilemapTool::Fill);
            const int fillUndoBefore = int(app.GetCommandStack().GetUndoCount());
            app.GetViewport().HandleMouseDown({int(px), int(py)}, Gorgon::Input::Mouse::Button::Left);
            app.GetViewport().HandleMouseMove({int(px) + 32, int(py) + 32});
            app.GetViewport().HandleMouseUp({int(px) + 32, int(py) + 32},
                                            Gorgon::Input::Mouse::Button::Left);

            const Entity *filled = scene.FindEntity(picked);
            if(!filled || !filled->tilemap)
                return fail("the fill drag dropped the tilemap component");

            int filledCells = 0;
            for(int tile : filled->tilemap->tiles) {
                if(tile == 5)
                    ++filledCells;
            }
            if(filledCells < 2)
                return fail("the fill drag did not paint a rectangle");
            if(int(app.GetCommandStack().GetUndoCount()) != fillUndoBefore + 1)
                return fail("the fill drag was not exactly one undo step");

            app.Undo();
            const Entity *cleared = scene.FindEntity(picked);
            if(!cleared || !cleared->tilemap)
                return fail("undo removed the tilemap component after the fill");
            for(int tile : cleared->tilemap->tiles) {
                if(tile != -1)
                    return fail("undo did not restore every filled cell");
            }

            // Leave the editor as the earlier phases left it.
            app.GetViewport().SetTilemapToolActive(false);
            app.GetCommandStack().Clear();
            if(Entity *restored = scene.FindEntity(picked))
                restored->transform = savedTransform;
            Gorgon::NextFrame();
        }

        // ---- Phase 7: animation timeline (tab, frames, scrub, undo) -----------
        {
            using namespace Gorgon::Editor;

            auto fail = [&](const std::string &message) {
                std::cerr << "FAIL: " << message << "\n";
                window.Close();
                return 1;
            };

            if(picked == InvalidEntityId)
                return fail("the timeline phase needs a selectable entity");

            Entity *target = scene.FindEntity(picked);
            if(!target)
                return fail("the timeline target disappeared");

            // Give the entity a 4-frame sheet animation, then reopen the scene so the editor treats
            // it as the committed baseline (undo must not un-attach the component).
            SpriteAnimComponent anim;
            anim.frameW = 32;
            anim.frameH = 32;
            anim.frameCount = 4;
            anim.fps = 10.0f; // 100 ms per frame
            anim.loop = true;
            target->spriteAnim = anim;

            std::error_code tec;
            const auto timelineScenePath =
                std::filesystem::temp_directory_path() / "gorgon_editor_smoke_timeline.gscene.json";
            std::string timelineError;
            if(!app.SaveSceneFile(timelineScenePath.string(), &timelineError)) {
                std::filesystem::remove(timelineScenePath, tec);
                return fail("could not save the timeline smoke scene: " + timelineError);
            }
            if(!app.OpenSceneFile(timelineScenePath.string(), &timelineError)) {
                std::filesystem::remove(timelineScenePath, tec);
                return fail("could not reopen the timeline smoke scene: " + timelineError);
            }
            std::filesystem::remove(timelineScenePath, tec);

            app.GetViewport().SetSelectedEntity(picked, true);
            app.ShowBottomTab(BottomTab::Timeline);
            Gorgon::NextFrame();

            if(app.GetBottomTab() != BottomTab::Timeline)
                return fail("the Timeline tab did not open");

            auto &timeline = app.GetTimelinePanel();
            const int originalFrames = timeline.GetFrameCount();
            if(originalFrames != 4)
                return fail("the timeline did not pick up the entity's 4 frames");
            if(timeline.GetVisibleFrameCount() != 4)
                return fail("the timeline strip did not lay out a cell per frame");

            // -- selecting a frame previews it without touching the scene selection ----
            const TimelineStripLayout strip = timeline.GetStripLayout();
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            if(!strip.CellRect(1, cx, cy, cw, ch))
                return fail("the timeline strip has no cell 1");

            timeline.HandleMouseDown({int(cx + cw * 0.5f), int(cy + 10.0f)},
                                     Gorgon::Input::Mouse::Button::Left);
            timeline.HandleMouseUp({int(cx + cw * 0.5f), int(cy + 10.0f)},
                                   Gorgon::Input::Mouse::Button::Left);

            if(timeline.GetSelectedFrame() != 1)
                return fail("clicking a frame cell did not select it");
            if(app.GetViewport().GetSelectedEntity() != picked)
                return fail("selecting a timeline frame disturbed the scene selection");
            if(app.GetViewport().GetDisplayedAnimationFrame(picked) != 1)
                return fail("the scrubbed frame was not previewed in the viewport");

            // -- add a frame, then delete it ------------------------------------------
            const int addedIndex = timeline.AddFrame();
            Gorgon::NextFrame();
            if(addedIndex != originalFrames)
                return fail("Add Frame did not append a frame at the end");
            if(timeline.GetFrameCount() != originalFrames + 1)
                return fail("Add Frame did not grow the animation");
            if(app.GetUndoName() != "Add Frame")
                return fail("Add Frame did not become an 'Add Frame' undo step");
            if(!app.IsDirty())
                return fail("a timeline edit did not mark the scene dirty");

            timeline.SetSelectedFrame(timeline.GetFrameCount() - 1);
            if(!timeline.DeleteFrame())
                return fail("Delete Frame refused to remove the appended frame");
            Gorgon::NextFrame();
            if(timeline.GetFrameCount() != originalFrames)
                return fail("Delete Frame did not shrink the animation back");

            // -- Undo restores the deleted frame, a second Undo removes the added one --
            app.Undo();
            Gorgon::NextFrame();
            if(timeline.GetFrameCount() != originalFrames + 1)
                return fail("undo did not bring the deleted frame back");

            app.Undo();
            Gorgon::NextFrame();
            if(timeline.GetFrameCount() != originalFrames)
                return fail("undoing the Add Frame did not restore the original frame count");
            if(scene.FindEntity(picked) == nullptr || !scene.FindEntity(picked)->spriteAnim)
                return fail("undoing the timeline edits dropped the animation component");

            // -- drag-to-reorder: frame 0 dropped between frames 2 and 3 ---------------
            for(int i = 0; i < originalFrames; ++i)
                timeline.SetFrameDuration(i, 100 * (i + 1)); // distinct durations to prove the order
            app.GetCommandStack().Clear();

            const TimelineStripLayout dragStrip = timeline.GetStripLayout();
            float x0 = 0.0f, y0 = 0.0f, w0 = 0.0f, h0 = 0.0f;
            float x2 = 0.0f, y2 = 0.0f, w2 = 0.0f, h2 = 0.0f;
            if(!dragStrip.CellRect(0, x0, y0, w0, h0) || !dragStrip.CellRect(2, x2, y2, w2, h2))
                return fail("the timeline strip cannot address the cells for a reorder drag");

            const int reorderUndoBefore = int(app.GetCommandStack().GetUndoCount());
            timeline.HandleMouseDown({int(x0 + w0 * 0.5f), int(y0 + 10.0f)},
                                     Gorgon::Input::Mouse::Button::Left);
            timeline.HandleMouseMove({int(x2 + w2 * 0.6f), int(y2 + 10.0f)});
            timeline.HandleMouseUp({int(x2 + w2 * 0.6f), int(y2 + 10.0f)},
                                   Gorgon::Input::Mouse::Button::Left);

            const SpriteAnimComponent *reordered = &*scene.FindEntity(picked)->spriteAnim;
            if(AnimationTimeline::FrameDurationMs(*reordered, 0) != 200 ||
               AnimationTimeline::FrameDurationMs(*reordered, 1) != 300 ||
               AnimationTimeline::FrameDurationMs(*reordered, 2) != 100 ||
               AnimationTimeline::FrameDurationMs(*reordered, 3) != 400) {
                return fail("dragging a frame did not reorder the animation");
            }
            if(app.GetUndoName() != "Reorder Frames")
                return fail("the reorder drag did not become a 'Reorder Frames' undo step");
            if(int(app.GetCommandStack().GetUndoCount()) != reorderUndoBefore + 1)
                return fail("the reorder drag was not exactly one undo step");

            app.Undo();
            Gorgon::NextFrame();
            if(AnimationTimeline::FrameDurationMs(*scene.FindEntity(picked)->spriteAnim, 0) != 100)
                return fail("undoing the reorder did not restore the frame order");

            // -- duration editing and the fps shortcut are undoable too ---------------
            const int durationUndoBefore = int(app.GetCommandStack().GetUndoCount());
            if(!timeline.SetFrameDuration(0, 250))
                return fail("the timeline refused a duration edit");
            if(AnimationTimeline::FrameDurationMs(*scene.FindEntity(picked)->spriteAnim, 0) != 250)
                return fail("the duration edit did not reach the component");
            if(app.GetUndoName() != "Set Frame Duration")
                return fail("a duration edit did not become a 'Set Frame Duration' undo step");
            if(int(app.GetCommandStack().GetUndoCount()) != durationUndoBefore + 1)
                return fail("a duration edit was not exactly one undo step");

            if(!timeline.RetimeToFps(20.0f))
                return fail("the fps shortcut did not retime the animation");
            if(AnimationTimeline::FrameDurationMs(*scene.FindEntity(picked)->spriteAnim, 1) != 50)
                return fail("the fps retime did not rewrite every duration");
            app.Undo();
            app.Undo();
            Gorgon::NextFrame();

            // -- the playhead runs at wall-clock speed and stops with Stop ------------
            timeline.SetSelectedFrame(0);
            timeline.Play();
            if(!timeline.IsPlaying())
                return fail("the timeline playhead did not start");

            bool advanced = false;
            for(int i = 0; i < 40 && !advanced; ++i) {
                app.GetTimelinePanel().Tick(1.0f / 30.0f);
                Gorgon::NextFrame();
                advanced = timeline.GetPlayheadFrame() != 0;
            }
            if(!advanced)
                return fail("the playhead never left the first frame");

            timeline.Stop();
            if(timeline.IsPlaying())
                return fail("Stop did not stop the timeline playhead");

            // Leave the editor as the earlier phases left it.
            app.ShowBottomTab(BottomTab::Assets);
            app.GetCommandStack().Clear();
            Gorgon::NextFrame();
        }

        // ---- Phase 8: 2D lighting (preview toggle + gizmos + lightmap) --------
        {
            using namespace Gorgon::Editor;

            auto fail = [&](const std::string &message) {
                std::cerr << "FAIL: " << message << "\n";
                window.Close();
                return 1;
            };

            // An unlit scene pays nothing: the preview draws no gizmo and composites no lightmap.
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.GetViewport().GetLightGizmoCount() != 0 || app.GetViewport().WasLightmapDrawn())
                return fail("an unlit scene drew lighting");

            // Add a light, then redraw: the gizmo and the lightmap both have to show up.
            Entity &lamp = scene.CreateEntity("SmokeLight");
            lamp.transform.x = 40.0f;
            lamp.transform.y = 30.0f;
            Light2DComponent light;
            light.radius = 220.0f;
            light.castShadows = false;
            lamp.light = light;
            const EntityId lightId = lamp.id;

            app.GetViewport().SetSelectedEntity(lightId, true);
            app.GetViewport().Redraw();
            Gorgon::NextFrame();

            if(app.GetViewport().GetLightGizmoCount() != 1)
                return fail("the light gizmo was not drawn for the scene's light");
            if(!app.GetViewport().WasLightmapDrawn())
                return fail("the lightmap was not composited for a lit scene");
            if(!app.GetViewport().GetLightmap().IsActive() ||
               app.GetViewport().GetLightmap().GetLightCount() != 1)
                return fail("the viewport lightmap did not pick up the light");
            if(app.GetViewport().GetLightmap().IsUniform())
                return fail("the viewport lightmap is uniform with a light present");
            if(!app.IsLightingPreviewEnabled())
                return fail("the lighting preview is off by default");

            // The toolbar toggle drives the preview through the real checkbox.
            app.GetLightsToggle().SetState(false);
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.IsLightingPreviewEnabled())
                return fail("the Lights toggle did not turn the preview off");
            if(app.GetViewport().WasLightmapDrawn())
                return fail("the lightmap was still drawn with the preview off");
            if(app.GetViewport().GetLightGizmoCount() != 1)
                return fail("turning the preview off hid the light gizmo as well");

            app.GetLightsToggle().SetState(true);
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(!app.IsLightingPreviewEnabled() || !app.GetViewport().WasLightmapDrawn())
                return fail("the Lights toggle did not turn the preview back on");

            // A spot light draws its cone gizmo without disturbing the preview.
            scene.FindEntity(lightId)->light->type = LightType::Spot;
            scene.FindEntity(lightId)->light->castShadows = true;
            scene.FindEntity(lightId)->light->coneAngle = 70.0f;
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.GetViewport().GetLightGizmoCount() != 1)
                return fail("the spot light lost its gizmo");

            // Restore the scene: no light, no lighting, preview back on.
            scene.RemoveEntity(lightId, true);
            app.GetViewport().ClearSelection(false);
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.GetViewport().GetLightGizmoCount() != 0 || app.GetViewport().WasLightmapDrawn())
                return fail("the scene was not restored to unlit");
            if(!app.IsLightingPreviewEnabled())
                return fail("the preview toggle was left off after the lighting phase");

            app.GetCommandStack().Clear();
            Gorgon::NextFrame();
        }


        // ---- Phase 9: in-game UI (viewport overlay, click-to-select) ---------
        {
            using namespace Gorgon::Editor;

            auto fail = [&](const std::string &message) {
                std::cerr << "FAIL: " << message << "\n";
                window.Close();
                return 1;
            };

            // A scene without a UI element draws no overlay at all.
            app.GetViewport().ClearSelection(false);
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.GetViewport().WasUserInterfaceDrawn() || app.GetViewport().GetUIWidgetCount() != 0)
                return fail("a scene without UI elements drew the overlay");

            // Add a button and a label straight into the scene, then redraw: the overlay has to
            // show both, anchored to the viewport the designer is looking at.
            Entity &uiButton = scene.CreateEntity("SmokeUIButton");
            uiButton.ui = UIElementComponent{};
            uiButton.ui->kind = UIElementKind::Button;
            uiButton.ui->id = "smoke";
            uiButton.ui->text = "Press";
            uiButton.ui->anchorH = UIAnchorH::Right;
            uiButton.ui->anchorV = UIAnchorV::Bottom;
            uiButton.ui->x = 24.0f;
            uiButton.ui->y = 24.0f;
            uiButton.ui->width = 150.0f;
            uiButton.ui->height = 44.0f;
            const EntityId uiButtonId = uiButton.id;

            Entity &uiLabel = scene.CreateEntity("SmokeUILabel");
            uiLabel.ui = UIElementComponent{};
            uiLabel.ui->kind = UIElementKind::Label;
            uiLabel.ui->id = "smokeLabel";
            uiLabel.ui->text = "Health";
            const EntityId uiLabelId = uiLabel.id;

            app.GetViewport().Redraw();
            Gorgon::NextFrame();

            if(!app.GetViewport().WasUserInterfaceDrawn())
                return fail("the UI overlay was not drawn for a scene with UI elements");
            if(app.GetViewport().GetUIWidgetCount() != 2)
                return fail("the UI overlay did not lay out the button and the label");

            const auto viewSize = app.GetViewport().GetViewportSize();
            const auto *buttonWidget = app.GetViewport().GetUserInterface().GetWidget("smoke");
            const auto *labelWidget = app.GetViewport().GetUserInterface().GetWidget("smokeLabel");
            if(!buttonWidget || !labelWidget)
                return fail("the laid out widgets are not addressable by their script id");
            if(uiButtonId != buttonWidget->entity || uiLabelId != labelWidget->entity)
                return fail("a widget is bound to the wrong entity");
            if(std::abs(buttonWidget->rect.Right() - (float(viewSize.Width) - 24.0f)) > 1.0f ||
               std::abs(buttonWidget->rect.Bottom() - (float(viewSize.Height) - 24.0f)) > 1.0f)
                return fail("the corner button is not anchored to the viewport corner");
            if(std::abs(labelWidget->rect.x - 24.0f) > 1.0f ||
               std::abs(labelWidget->rect.y - 24.0f) > 1.0f)
                return fail("the label is not anchored to the top-left corner");

            // Clicking the button with the real viewport handlers selects the entity that owns it,
            // which is how a UI element is picked while authoring.
            app.GetViewport().SetSelectedEntity(picked, true);
            const Gorgon::Geometry::Point press{
                int(buttonWidget->rect.CenterX()), int(buttonWidget->rect.CenterY())};
            app.GetViewport().HandleMouseDown(press, Gorgon::Input::Mouse::Button::Left);
            app.GetViewport().HandleMouseUp(press, Gorgon::Input::Mouse::Button::Left);
            Gorgon::NextFrame();
            if(app.GetViewport().GetSelectedEntity() != uiButtonId)
                return fail("clicking the UI button did not select its entity");

            // Selecting it previews the element outline (the widget is still drawn, still one pass).
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.GetViewport().GetUIWidgetCount() != 2)
                return fail("the selected UI element was not laid out");

            // Removing them restores the plain scene: no widgets, no overlay.
            scene.RemoveEntity(uiButtonId, true);
            scene.RemoveEntity(uiLabelId, true);
            app.GetViewport().ClearSelection(false);
            app.GetViewport().Redraw();
            Gorgon::NextFrame();
            if(app.GetViewport().WasUserInterfaceDrawn() || app.GetViewport().GetUIWidgetCount() != 0)
                return fail("the scene was not restored after removing the UI elements");

            app.GetViewport().SetSelectedEntity(picked, true);
            app.GetCommandStack().Clear();
            Gorgon::NextFrame();
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
        std::cout << "GorgonEditor --smoke PASSED (window shell + dark theme + 3 frames + hierarchy/inspector + console panel + script editor + export + tilemap paint + animation timeline + 2D lighting + in-game UI + round-trip)\n";
        return 0;
    }

    window.Run();
    return 0;
}
