#include "Core/Scene.h"
#include "Core/SceneSerializer.h"
#include "EditorApp.h"
#include "EditorTheme.h"
#include "Viewport.h"

#include <Gorgon/Main.h>
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

        if(const int assetResult = RunAssetBrowserSelfTest()) {
            return assetResult;
        }

        std::cout << "GorgonEditor --selftest PASSED (camera, hit-test, grid-snap drag, pan, round-trip, asset browser)\n";
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

        std::filesystem::remove_all(root, ec);
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
