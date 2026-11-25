#include <Gorgon/EntryPoint.h>
#include <Gorgon/Main.h>
#include <Gorgon/Time.h>
#include <Gorgon/Window.h>
#include <Gorgon/Graphics/Layer.h>
#include <Gorgon/Input/Layer.h>

#include "Core/GameExporter.h"
#include "Core/InputState.h"
#include "Core/SceneRuntime.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

    namespace fs = std::filesystem;

    /// Target frame length: the player steps its scripts at a fixed 60 fps.
    constexpr double kFrameSeconds = 1.0 / 60.0;

    /// Resolves the manifest to play: an explicit file/folder argument wins, then `game.json`
    /// next to the executable, then the working directory.
    std::string ResolveManifestPath(const std::vector<std::string> &args) {
        for(std::size_t i = 1; i < args.size(); ++i) {
            const std::string &arg = args[i];
            if(arg.empty() || arg[0] == '-') {
                // `--frames N`: the value belongs to the flag, it is not the bundle to play.
                if(arg == "--frames")
                    ++i;
                continue;
            }

            std::error_code ec;
            if(fs::is_directory(arg, ec)) {
                const auto candidate = fs::path(arg) / "game.json";
                if(fs::is_regular_file(candidate, ec))
                    return candidate.generic_string();
                continue;
            }

            return arg;
        }

        std::vector<std::string> roots;
        if(!args.empty() && !args[0].empty())
            roots.push_back(fs::absolute(args[0]).parent_path().generic_string());
        roots.push_back(fs::current_path().generic_string());

        for(const auto &root : roots) {
            const auto candidate = fs::path(root) / "game.json";
            std::error_code ec;
            if(fs::is_regular_file(candidate, ec))
                return candidate.generic_string();
        }

        return {};
    }

    int LookupFrameLimit(const std::vector<std::string> &args) {
        for(std::size_t i = 1; i + 1 < args.size(); ++i) {
            if(args[i] == "--frames")
                return std::atoi(args[i + 1].c_str());
        }
        return 0;
    }

    bool HasFlag(const std::vector<std::string> &args, const char *flag) {
        for(const auto &arg : args) {
            if(arg == flag)
                return true;
        }
        return false;
    }

    struct LoadedGame {
        Gorgon::Editor::GameManifest manifest;
        std::string scenePath;
        std::string baseDir;
    };

    /// Reads game.json and resolves the scene it points at. Used by both the headless selftest and
    /// the real windowed player so both paths validate the same bundle.
    bool LoadGame(const std::string &manifestPath, LoadedGame &outGame, std::string &outError) {
        if(manifestPath.empty()) {
            outError = "No game.json found. Put one next to GorgonPlayer.exe or pass its path.";
            return false;
        }

        std::string error;
        if(!Gorgon::Editor::GameExporter::LoadManifestFromFile(manifestPath, outGame.manifest, &error)) {
            outError = "Could not read '" + manifestPath + "': " + error;
            return false;
        }

        const fs::path manifestDir = fs::path(manifestPath).parent_path();
        outGame.baseDir = manifestDir.generic_string();
        outGame.scenePath = (manifestDir / outGame.manifest.startScene).generic_string();
        return true;
    }

    /// Headless verification: loads the bundle, ticks the scene and reports what moved. Needs no
    /// window, so it runs in CI and from the export tests.
    int RunSelfTest(const std::string &manifestPath) {
        LoadedGame game;
        std::string error;
        if(!LoadGame(manifestPath, game, error)) {
            std::cerr << "FAIL: " << error << "\n";
            return 1;
        }

        Gorgon::Editor::SceneRuntime runtime;
        if(!runtime.Load(game.scenePath, &error)) {
            std::cerr << "FAIL: " << (error.empty() ? runtime.GetLastError() : error) << "\n";
            return 1;
        }

        const auto &scene = runtime.GetScene();
        if(scene.GetEntityCount() <= 0) {
            std::cerr << "FAIL: the exported scene has no entities\n";
            return 1;
        }

        const int entities = scene.GetEntityCount();
        const int scripts = runtime.GetScriptCount();

        // Snapshot the transforms, run half a second of updates and check the scene stayed usable.
        std::size_t moved = 0;
        for(int frame = 0; frame < 30; ++frame)
            runtime.Update(float(kFrameSeconds));

        if(runtime.GetLastError().find("Script error") != std::string::npos) {
            std::cerr << "FAIL: script error: " << runtime.GetLastError() << "\n";
            return 1;
        }

        for(const auto &entity : scene.GetEntities()) {
            if(entity.script.has_value())
                ++moved;
        }

        {
            std::error_code ec;
            const auto reportPath = fs::temp_directory_path(ec) / "gorgon_player_report.txt";
            if(!ec) {
                std::ofstream report(reportPath, std::ios::trunc);
                report << "manifest=" << manifestPath << "\n"
                       << "title=" << game.manifest.title << "\n"
                       << "size=" << game.manifest.width << "x" << game.manifest.height << "\n"
                       << "scene=" << game.scenePath << "\n"
                       << "entities=" << entities << "\n"
                       << "scripts=" << scripts << "\n"
                       << "frames=30\n";
            }
        }

        std::cout << "GorgonPlayer --selftest PASSED (" << entities << " entities, " << scripts
                  << " running scripts, " << moved << " scripted, 30 frames)\n";
        return 0;
    }

} // namespace

int Main(const std::vector<std::string> &args) {
    const std::string manifestPath = ResolveManifestPath(args);

    if(HasFlag(args, "--selftest"))
        return RunSelfTest(manifestPath);

    LoadedGame game;
    std::string error;
    if(!LoadGame(manifestPath, game, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    const int width = game.manifest.width > 0 ? game.manifest.width : 1280;
    const int height = game.manifest.height > 0 ? game.manifest.height : 720;
    const std::string title = game.manifest.title.empty() ? std::string("Gorgon Game")
                                                          : game.manifest.title;

    Gorgon::Initialize("GorgonPlayer");

    Gorgon::Window window({width, height}, title);

    Gorgon::Editor::SceneRuntime runtime;
    if(!runtime.Load(game.scenePath, &error)) {
        std::cerr << "Could not load '" << game.scenePath << "': "
                  << (error.empty() ? runtime.GetLastError() : error) << "\n";
        return 1;
    }
    runtime.CenterCamera(width, height);

    Gorgon::Graphics::Layer sceneLayer;
    window.Add(sceneLayer);

    // The pointer is tracked through an input layer covering the whole window: Gorgon routes
    // move/down/up events to the layer under the cursor, exactly like the editor viewport does.
    Gorgon::Input::Layer inputLayer;
    window.Add(inputLayer);

    Gorgon::Geometry::Point mousePosition{width / 2, height / 2};

    inputLayer.SetMove([&mousePosition](Gorgon::Geometry::Point point) {
        mousePosition = point;
    });
    inputLayer.SetDown([&runtime](Gorgon::Geometry::Point, Gorgon::Input::Mouse::Button button) {
        const auto name = Gorgon::Editor::MouseButtonName(button);
        if(!name.empty())
            runtime.GetInput().SetMouseButtonDown(name, true);
    });
    inputLayer.SetUp([&runtime](Gorgon::Geometry::Point, Gorgon::Input::Mouse::Button button) {
        const auto name = Gorgon::Editor::MouseButtonName(button);
        if(!name.empty())
            runtime.GetInput().SetMouseButtonDown(name, false);
    });

    window.KeyPreviewEvent.Register([&runtime](Gorgon::Input::Key key, float amount) {
        const auto name = Gorgon::Editor::KeyNameFromCode(key);
        if(!name.empty())
            runtime.GetInput().SetKeyDown(name, amount > 0.0f);
    });

    const int frameLimit = LookupFrameLimit(args);
    int frame = 0;

    while(!window.IsClosed() && (frameLimit <= 0 || frame < frameLimit)) {
        const auto frameStart = std::chrono::steady_clock::now();

        // Feed the pointer in world units, then step the scripts and draw.
        const auto world = runtime.GetCamera().ScreenToWorld(
            float(mousePosition.X), float(mousePosition.Y), float(width), float(height));
        runtime.GetInput().SetMousePosition(world.X, world.Y);

        runtime.Update(float(kFrameSeconds));

        sceneLayer.Clear();
        runtime.Render(sceneLayer, width, height);
        Gorgon::NextFrame();

        ++frame;

        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - frameStart).count();
        if(elapsed < kFrameSeconds)
            std::this_thread::sleep_for(std::chrono::duration<double>(kFrameSeconds - elapsed));
    }

    return 0;
}
