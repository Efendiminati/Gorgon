#include "Core/Scene.h"
#include "Core/SceneSerializer.h"
#include "EditorApp.h"
#include "EditorTheme.h"
#include "Viewport.h"

#include <Gorgon/Main.h>
#include <Gorgon/UI/Window.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

    bool Approx(float a, float b, float eps = 1e-2f) {
        return std::abs(a - b) <= eps;
    }

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

        std::cout << "GorgonEditor --selftest PASSED (camera, hit-test, grid-snap drag, pan, round-trip)\n";
        return 0;
    }

} // namespace

int main(int argc, char *argv[]) {
    bool selftest = false;
    bool smoke = false;
    std::string initialScenePath;

    for(int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if(arg == "--selftest") {
            selftest = true;
        } else if(arg == "--smoke") {
            smoke = true;
        } else if(initialScenePath.empty() && !arg.empty() && arg[0] != '-') {
            initialScenePath = arg;
        }
    }

    if(selftest) {
        return RunSelfTest();
    }

    Gorgon::Initialize("GorgonEditor");

    Gorgon::UI::Window window({1280, 800}, "Gorgon 2D Editor");
    auto theme = Gorgon::Editor::CreateDarkEditorTheme(9.5f);

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
        for(int i = 0; i < 3; ++i) {
            Gorgon::NextFrame();
        }
        // Verify save/open round-trip through EditorApp in live window mode
        const auto tmpPath = (std::filesystem::temp_directory_path() / "gorgon_editor_smoke.gscene.json").string();
        std::string err;
        const bool saved = app.SaveSceneFile(tmpPath, &err);
        const bool opened = saved && app.OpenSceneFile(tmpPath, &err);
        std::filesystem::remove(tmpPath);
        window.Close();
        if(!saved || !opened) {
            std::cerr << "FAIL: smoke round-trip failed: " << err << "\n";
            return 1;
        }
        std::cout << "GorgonEditor --smoke PASSED (window shell + dark theme + 3 frames + round-trip)\n";
        return 0;
    }

    window.Run();
    return 0;
}
