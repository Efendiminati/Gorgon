#include <Gorgon/EntryPoint.h>
#include <Gorgon/Main.h>
#include <Gorgon/Time.h>
#include <Gorgon/Window.h>
#include <Gorgon/Graphics/Layer.h>

#include "Core/SceneRuntime.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

    /// Resolves the scene argument: an explicit path wins, otherwise look next to the executable.
    std::string ResolveScenePath(const std::vector<std::string> &args) {
        if(args.size() > 1 && !args[1].empty() && args[1][0] != '-')
            return args[1];

        const std::filesystem::path exeDir = std::filesystem::absolute(args[0]).parent_path();
        for(const auto &candidate : {std::filesystem::path("Scene/DemoScene.gscene.json"),
                                     exeDir / "Scene/DemoScene.gscene.json"}) {
            if(std::filesystem::exists(candidate))
                return candidate.string();
        }
        return "Scene/DemoScene.gscene.json";
    }

    std::string LookupArgument(const std::vector<std::string> &args, const std::string &name) {
        for(size_t i = 1; i + 1 < args.size(); ++i) {
            if(args[i] == name)
                return args[i + 1];
        }
        return {};
    }

    /// Headless check: loads the demo scene, ticks it and confirms the scripts actually moved
    /// their entities. Used by the build verification, needs no window.
    int RunSelfTest(const std::string &scenePath) {
        Gorgon::Editor::SceneRuntime runtime;
        if(!runtime.Load(scenePath)) {
            std::cerr << "FAIL: " << runtime.GetLastError() << "\n";
            return 1;
        }

        const auto &scene = runtime.GetScene();
        if(scene.GetEntityCount() != 6) {
            std::cerr << "FAIL: expected 6 entities, got " << scene.GetEntityCount() << "\n";
            return 1;
        }
        if(runtime.GetScriptCount() != 2) {
            std::cerr << "FAIL: expected 2 running scripts, got " << runtime.GetScriptCount()
                      << " (" << runtime.GetLastError() << ")\n";
            return 1;
        }

        // Every sprite the scene references must resolve next to the .gscene.json file.
        for(const auto &entity : scene.GetEntities()) {
            if(!entity.sprite || entity.sprite->asset.empty())
                continue;
            const std::string full = runtime.ResolveAssetPath(entity.sprite->asset);
            if(!std::filesystem::exists(full)) {
                std::cerr << "FAIL: sprite missing for '" << entity.name << "': " << full << "\n";
                return 1;
            }
        }

        const auto beforeOrb = scene.FindEntity(6)->transform;
        const auto beforeLantern = scene.FindEntity(5)->transform;

        for(int i = 0; i < 30; ++i)
            runtime.Update(1.0f / 60.0f);

        const auto afterOrb = scene.FindEntity(6)->transform;
        const auto afterLantern = scene.FindEntity(5)->transform;
        const float orbDelta = std::abs(afterOrb.x - beforeOrb.x) + std::abs(afterOrb.y - beforeOrb.y);
        const float lanternDelta = std::abs(afterLantern.y - beforeLantern.y);

        if(runtime.GetLastError().find("Script error") != std::string::npos) {
            std::cerr << "FAIL: script error: " << runtime.GetLastError() << "\n";
            return 1;
        }
        if(orbDelta < 0.5f) {
            std::cerr << "FAIL: orbit.gs did not move the Orb (delta " << orbDelta << ")\n";
            return 1;
        }
        if(lanternDelta < 0.01f) {
            std::cerr << "FAIL: bob.gs did not move the Lantern (delta " << lanternDelta << ")\n";
            return 1;
        }

        {
            std::ofstream report(std::filesystem::temp_directory_path() / "gorgon_editor_demo_report.txt",
                                 std::ios::trunc);
            report << "scene=" << scene.name << "\n"
                   << "entities=" << scene.GetEntityCount() << "\n"
                   << "scripts=" << runtime.GetScriptCount() << "\n"
                   << "orbX=" << beforeOrb.x << "->" << afterOrb.x << "\n"
                   << "orbY=" << beforeOrb.y << "->" << afterOrb.y << "\n"
                   << "lanternY=" << beforeLantern.y << "->" << afterLantern.y << "\n";
        }

        std::cout << "EditorDemo --selftest PASSED (6 entities, 2 scripts, orb moved "
                  << orbDelta << ", lantern moved " << lanternDelta << ")\n";
        return 0;
    }

} // namespace

int Main(const std::vector<std::string> &args) {
    const std::string scenePath = ResolveScenePath(args);

    // Headless verification path (no window, no rendering).
    for(const auto &arg : args) {
        if(arg == "--selftest")
            return RunSelfTest(scenePath);
    }

    int frameLimit = 0;
    if(const std::string limit = LookupArgument(args, "--frames"); !limit.empty())
        frameLimit = std::atoi(limit.c_str());

    Gorgon::Initialize("EditorDemo");

    Gorgon::Window window({960, 600}, "Gorgon Editor Demo");

    // This is the whole runtime integration: load the scene, then update and draw it every frame.
    Gorgon::Editor::SceneRuntime runtime;
    if(!runtime.Load(scenePath)) {
        std::cerr << "Could not load '" << scenePath << "': " << runtime.GetLastError() << "\n";
        return 1;
    }

    Gorgon::Graphics::Layer sceneLayer;
    window.Add(sceneLayer);
    runtime.CenterCamera(960, 600);

    for(int frame = 0; frameLimit == 0 || frame < frameLimit; ++frame) {
        runtime.Update(Gorgon::Time::DeltaTime() / 1000.0f);

        sceneLayer.Clear();
        runtime.Render(sceneLayer);
        Gorgon::NextFrame();
    }

    return 0;
}
