#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Core/CommandStack.h"
#include "Core/PlaySession.h"
#include "Core/Scene.h"
#include "Core/SceneRuntime.h"
#include "Core/SceneSerializer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <string>

using namespace Gorgon::Editor;

TEST_CASE("Scene entity CRUD and ID generation", "[EditorCore][Scene]") {
    Scene scene;
    REQUIRE(scene.GetEntityCount() == 0);
    REQUIRE(scene.GetNextId() == 1);

    Entity &a = scene.CreateEntity("Player");
    EntityId aId = a.id;
    REQUIRE(aId == 1);
    REQUIRE(a.parentId == InvalidEntityId);
    REQUIRE(a.visible == true);
    REQUIRE(a.locked == false);
    REQUIRE(a.zOrder == 0);

    Entity &b = scene.CreateEntity("Weapon", aId);
    EntityId bId = b.id;
    REQUIRE(bId == 2);
    REQUIRE(b.parentId == aId);

    // Non-existent parent falls back to root
    Entity &c = scene.CreateEntity("Orphan", 999);
    EntityId cId = c.id;
    REQUIRE(cId == 3);
    REQUIRE(c.parentId == InvalidEntityId);

    REQUIRE(scene.GetEntityCount() == 3);
    REQUIRE(scene.HasEntity(aId));
    REQUIRE(scene.FindEntity(bId)->name == "Weapon");
    REQUIRE(scene.FindEntity(999) == nullptr);

    // Non-recursive delete promotes children to grandparent (root)
    REQUIRE(scene.RemoveEntity(aId, false));
    REQUIRE_FALSE(scene.HasEntity(aId));
    REQUIRE(scene.HasEntity(bId));
    REQUIRE(scene.FindEntity(bId)->parentId == InvalidEntityId);
    REQUIRE(scene.GetEntityCount() == 2);

    // Deleting missing entity returns false
    REQUIRE_FALSE(scene.RemoveEntity(aId));

    // Recursive delete removes whole subtree
    EntityId childId = scene.CreateEntity("Child", bId).id;
    EntityId grandChildId = scene.CreateEntity("GrandChild", childId).id;
    REQUIRE(scene.GetEntityCount() == 4);
    REQUIRE(scene.RemoveEntity(bId, true));
    REQUIRE_FALSE(scene.HasEntity(bId));
    REQUIRE_FALSE(scene.HasEntity(childId));
    REQUIRE_FALSE(scene.HasEntity(grandChildId));
    REQUIRE(scene.HasEntity(cId));
    REQUIRE(scene.GetEntityCount() == 1);

    // Clear resets entities and ID generator
    scene.Clear();
    REQUIRE(scene.GetEntityCount() == 0);
    REQUIRE(scene.GetNextId() == 1);
}

TEST_CASE("Scene hierarchy queries, reparenting, and cycle rejection", "[EditorCore][Hierarchy]") {
    Scene scene;
    EntityId rootA = scene.CreateEntity("RootA").id;
    EntityId rootB = scene.CreateEntity("RootB").id;
    EntityId childA1 = scene.CreateEntity("ChildA1", rootA).id;
    EntityId grandA1 = scene.CreateEntity("GrandA1", childA1).id;

    auto roots = scene.GetRootEntityIds();
    REQUIRE(roots.size() == 2);
    REQUIRE(roots[0] == rootA);
    REQUIRE(roots[1] == rootB);

    auto childrenOfA = scene.GetChildIds(rootA);
    REQUIRE(childrenOfA.size() == 1);
    REQUIRE(childrenOfA[0] == childA1);

    REQUIRE(scene.IsAncestorOf(rootA, childA1));
    REQUIRE(scene.IsAncestorOf(rootA, grandA1));
    REQUIRE_FALSE(scene.IsAncestorOf(grandA1, rootA));
    REQUIRE_FALSE(scene.IsAncestorOf(rootA, rootA));

    // Self-reparenting rejected
    REQUIRE_FALSE(scene.CanReparent(rootA, rootA));
    REQUIRE_FALSE(scene.Reparent(rootA, rootA));

    // Cycle reparenting (ancestor under descendant) rejected
    REQUIRE_FALSE(scene.CanReparent(rootA, childA1));
    REQUIRE_FALSE(scene.Reparent(rootA, childA1));
    REQUIRE_FALSE(scene.CanReparent(rootA, grandA1));
    REQUIRE_FALSE(scene.Reparent(rootA, grandA1));

    // Missing entity / missing target parent rejected
    REQUIRE_FALSE(scene.Reparent(999, rootA));
    REQUIRE_FALSE(scene.Reparent(rootA, 999));

    // Valid reparenting succeeds
    REQUIRE(scene.CanReparent(childA1, rootB));
    REQUIRE(scene.Reparent(childA1, rootB));
    REQUIRE(scene.FindEntity(childA1)->parentId == rootB);
    REQUIRE(scene.IsAncestorOf(rootB, grandA1));
    REQUIRE_FALSE(scene.IsAncestorOf(rootA, grandA1));

    // Reparenting to root (InvalidEntityId) succeeds
    REQUIRE(scene.Reparent(grandA1, InvalidEntityId));
    REQUIRE(scene.FindEntity(grandA1)->parentId == InvalidEntityId);
    REQUIRE(scene.GetRootEntityIds().size() == 3);
}

TEST_CASE("Scene duplication, render order, and world transform", "[EditorCore][Scene]") {
    Scene scene;
    Entity &parent = scene.CreateEntity("Parent");
    parent.zOrder = 10;
    parent.transform = {100.0f, 50.0f, 2.0f, 2.0f, 90.0f};
    EntityId parentId = parent.id;

    Entity &child = scene.CreateEntity("Child", parentId);
    child.zOrder = -5;
    child.transform = {10.0f, 0.0f, 1.0f, 1.0f, 0.0f};
    EntityId childId = child.id;

    // Render order is sorted ascending by zOrder
    auto order = scene.GetRenderOrder();
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == childId);
    REQUIRE(order[1] == parentId);

    // World transform: local (10,0) scaled by 2 -> (20,0), rotated +90 deg -> (0,20), + (100,50) = (100,70)
    Transform wt = scene.GetWorldTransform(childId);
    REQUIRE(wt.x == Catch::Approx(100.0f).margin(1e-3f));
    REQUIRE(wt.y == Catch::Approx(70.0f).margin(1e-3f));
    REQUIRE(wt.scaleX == Catch::Approx(2.0f));
    REQUIRE(wt.scaleY == Catch::Approx(2.0f));
    REQUIRE(wt.rotation == Catch::Approx(90.0f));

    // Recursive duplication preserves parent-child structure with new IDs
    EntityId dupId = scene.DuplicateEntity(parentId, true);
    REQUIRE(dupId != InvalidEntityId);
    REQUIRE(dupId != parentId);
    REQUIRE(scene.GetEntityCount() == 4);
    auto dupChildren = scene.GetChildIds(dupId);
    REQUIRE(dupChildren.size() == 1);
    REQUIRE(dupChildren[0] != childId);
    REQUIRE(scene.FindEntity(dupChildren[0])->name == "Child");
}

TEST_CASE("Scene JSON serialization lossless round-trip (string and file)", "[EditorCore][JSON]") {
    Scene original;
    original.name = "Level01";
    original.settings.gridSize = 64;
    original.settings.backgroundColor = ColorRGBA{12, 34, 56, 255};

    Entity &player = original.CreateEntity("Player");
    EntityId playerId = player.id;
    player.visible = true;
    player.locked = false;
    player.zOrder = 5;
    player.transform = {120.5f, 80.25f, 1.5f, 0.75f, 45.0f};
    player.sprite = SpriteComponent{"assets/player.png", ColorRGBA{255, 200, 100, 220}};
    player.collider = ColliderComponent{ColliderType::AABB, 32.0f, 48.0f, 16.0f, -2.0f, 4.0f};
    player.script = ScriptComponent{"scripts/player.gs"};

    Entity &orb = original.CreateEntity("Orb", playerId);
    orb.visible = false;
    orb.locked = true;
    orb.zOrder = -2;
    orb.transform = {-16.0f, 24.0f, 1.0f, 1.0f, 0.0f};
    orb.collider = ColliderComponent{ColliderType::Circle, 0.0f, 0.0f, 12.5f, 1.0f, -1.0f};
    // Intentionally leave sprite and script unset to verify optional omission

    // String round-trip
    const std::string encoded = SceneSerializer::ToJsonString(original, 2);
    REQUIRE(encoded.find("\"schemaVersion\"") != std::string::npos);
    REQUIRE(encoded.find("\"Level01\"") != std::string::npos);

    Scene fromStr;
    std::string err;
    REQUIRE(SceneSerializer::FromJsonString(encoded, fromStr, &err));
    REQUIRE(err.empty());
    REQUIRE(fromStr == original);
    REQUIRE(fromStr.GetNextId() == original.GetNextId());
    REQUIRE_FALSE(fromStr.FindEntity(orb.id)->sprite.has_value());
    REQUIRE_FALSE(fromStr.FindEntity(orb.id)->script.has_value());
    REQUIRE(fromStr.FindEntity(orb.id)->collider.has_value());
    REQUIRE(fromStr.FindEntity(orb.id)->collider->type == ColliderType::Circle);
    REQUIRE(fromStr.FindEntity(orb.id)->collider->radius == Catch::Approx(12.5f));

    // File round-trip (.gscene.json)
    const auto tmpPath = (std::filesystem::temp_directory_path() / "gorgon_editor_core_roundtrip.gscene.json").string();
    REQUIRE(SceneSerializer::SaveToFile(original, tmpPath, &err));

    Scene fromFile;
    REQUIRE(SceneSerializer::LoadFromFile(tmpPath, fromFile, &err));
    REQUIRE(fromFile == original);

    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);
}

TEST_CASE("Scene JSON deserializer rejects invalid inputs cleanly", "[EditorCore][JSON]") {
    Scene out;
    std::string err;

    // Malformed JSON
    REQUIRE_FALSE(SceneSerializer::FromJsonString("{bad json", out, &err));
    REQUIRE_FALSE(err.empty());

    // Unsupported schemaVersion
    err.clear();
    REQUIRE_FALSE(SceneSerializer::FromJsonString(R"({"schemaVersion": 99, "name": "Future", "entities": []})", out, &err));
    REQUIRE(err.find("99") != std::string::npos);

    // Duplicate entity IDs
    err.clear();
    REQUIRE_FALSE(SceneSerializer::FromJsonString(R"({
        "schemaVersion": 1,
        "entities": [{"id": 1, "name": "A"}, {"id": 1, "name": "B"}]
    })", out, &err));
    REQUIRE_FALSE(err.empty());

    // Dangling parentId
    err.clear();
    REQUIRE_FALSE(SceneSerializer::FromJsonString(R"({
        "schemaVersion": 1,
        "entities": [{"id": 1, "parentId": 42, "name": "A"}]
    })", out, &err));
    REQUIRE_FALSE(err.empty());

    // Cycle in JSON hierarchy
    err.clear();
    REQUIRE_FALSE(SceneSerializer::FromJsonString(R"({
        "schemaVersion": 1,
        "entities": [
            {"id": 1, "parentId": 2, "name": "A"},
            {"id": 2, "parentId": 1, "name": "B"}
        ]
    })", out, &err));
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("Command stack undo and redo", "[EditorCore][Undo]") {
    Scene scene;
    const EntityId first = scene.CreateEntity("Player").id;

    CommandStack stack;
    REQUIRE_FALSE(stack.CanUndo());
    REQUIRE_FALSE(stack.CanRedo());
    REQUIRE(stack.Undo(scene) == false);

    auto push = [&](const std::string &name, const Scene &before, bool merge) {
        stack.Push(std::unique_ptr<Command>(new SceneSnapshotCommand(name, before, scene)), merge);
    };

    // Move
    Scene beforeMove = scene;
    scene.FindEntity(first)->transform.x = 64.0f;
    push("Move", beforeMove, false);
    REQUIRE(stack.CanUndo());
    REQUIRE(stack.GetUndoName() == "Move");

    // Property edit
    Scene beforeProp = scene;
    scene.FindEntity(first)->name = "Hero";
    scene.FindEntity(first)->visible = false;
    push("Edit Properties", beforeProp, false);

    // Entity creation
    Scene beforeCreate = scene;
    const EntityId second = scene.CreateEntity("Orb").id;
    push("Add Entity", beforeCreate, false);
    REQUIRE(stack.GetUndoCount() == 3);

    // Undo creation, the property edit and the move, newest first.
    REQUIRE(stack.Undo(scene));
    REQUIRE_FALSE(scene.HasEntity(second));
    REQUIRE(stack.Undo(scene));
    REQUIRE(scene.FindEntity(first)->name == "Player");
    REQUIRE(scene.FindEntity(first)->visible == true);
    REQUIRE(stack.Undo(scene));
    REQUIRE(Catch::Approx(scene.FindEntity(first)->transform.x) == 0.0);
    REQUIRE_FALSE(stack.CanUndo());
    REQUIRE(stack.GetUndoCount() == 0);
    REQUIRE(stack.GetRedoCount() == 3);

    // Redo reapplies them in the same order.
    REQUIRE(stack.Redo(scene));
    REQUIRE(Catch::Approx(scene.FindEntity(first)->transform.x) == 64.0);
    REQUIRE(stack.Redo(scene));
    REQUIRE(scene.FindEntity(first)->name == "Hero");
    REQUIRE(stack.Redo(scene));
    REQUIRE(scene.HasEntity(second));
    REQUIRE_FALSE(stack.CanRedo());

    // A new edit after an undo drops the redo branch.
    REQUIRE(stack.Undo(scene));   // redo branch now holds "Add Entity"
    REQUIRE_FALSE(scene.HasEntity(second));
    Scene beforeDelete = scene;
    scene.CreateEntity("Temp");
    push("Add Temp", beforeDelete, false);
    REQUIRE_FALSE(stack.CanRedo());
    REQUIRE(stack.Undo(scene));
    REQUIRE_FALSE(scene.HasEntity(second));

    // Merging collapses a continuous drag into a single step.
    CommandStack merged;
    Scene baseline = scene;
    const float baselineX = baseline.FindEntity(first)->transform.x;
    for(int i = 0; i < 5; ++i) {
        scene.FindEntity(first)->transform.x = float(i + 1) * 10.0f;
        merged.Push(std::unique_ptr<Command>(new SceneSnapshotCommand("Drag", baseline, scene)),
                    i > 0);
    }
    REQUIRE(merged.GetUndoCount() == 1);
    REQUIRE(merged.Undo(scene));
    REQUIRE(Catch::Approx(scene.FindEntity(first)->transform.x) == baselineX);
    REQUIRE(merged.Redo(scene));
    REQUIRE(Catch::Approx(scene.FindEntity(first)->transform.x) == 50.0);

    // The stack honours its limit.
    CommandStack limited(2);
    Scene state = scene;
    for(int i = 0; i < 4; ++i) {
        Scene before = state;
        state.FindEntity(first)->transform.y = float(i + 1);
        limited.Push(std::unique_ptr<Command>(new SceneSnapshotCommand("Move", before, state)), false);
    }
    REQUIRE(limited.GetUndoCount() == 2);

    // Clearing drops both sides.
    limited.Clear();
    REQUIRE_FALSE(limited.CanUndo());
    REQUIRE_FALSE(limited.CanRedo());
}

TEST_CASE("Play session runs a GScript and restores the snapshot", "[EditorCore][Play]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_play_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto script = dir / "spin.gs";
    {
        std::ofstream out(script, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    self.x = self.x + 100 * dt\n"
               "    self.rotation = self.rotation + 90 * dt\n"
               "    return 0\n"
               "end function\n";
    }
    REQUIRE(std::filesystem::exists(script));

    Scene scene;
    Entity &entity = scene.CreateEntity("Spinner");
    entity.transform.x = 10.0f;
    entity.transform.y = 20.0f;
    entity.transform.rotation = 5.0f;
    entity.script = ScriptComponent{"spin.gs"};
    const EntityId id = entity.id;

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.IsRunning());
    REQUIRE(session.GetScriptCount() == 1);
    REQUIRE(session.GetScriptedEntities() == std::vector<EntityId>{id});

    // dt is clamped to 0.25 s per tick so a stalled frame cannot teleport an entity.
    session.Tick(scene, 0.5f);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.x) == 35.0);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.rotation) == 27.5);

    session.Tick(scene, 0.1f);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.x) == 45.0);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.rotation) == 36.5);

    // Stop restores the exact pre-play scene, including values the script overwrote.
    session.Stop(scene);
    REQUIRE_FALSE(session.IsRunning());
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.x) == 10.0);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.y) == 20.0);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.rotation) == 5.0);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.scaleX) == 1.0);

    // Playing again works from the restored state, and a missing script is reported.
    REQUIRE(session.Start(scene, dir.generic_string()));
    session.Tick(scene, 0.1f);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.x) == 20.0);
    session.Stop(scene);
    REQUIRE(Catch::Approx(scene.FindEntity(id)->transform.x) == 10.0);

    Scene broken;
    broken.CreateEntity("NoScript").script = ScriptComponent{"missing.gs"};
    PlaySession failing;
    std::string failError;
    REQUIRE_FALSE(failing.Start(broken, dir.generic_string(), &failError));
    REQUIRE_FALSE(failError.empty());
    REQUIRE_FALSE(failing.IsRunning());

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("SceneRuntime loads a scene file, resolves paths and steps scripts", "[EditorCore][Runtime]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_runtime_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "assets", ec);
    std::filesystem::create_directories(dir / "scripts", ec);

    { std::ofstream out(dir / "assets" / "hero.png", std::ios::binary); out << "not-a-real-png"; }
    {
        // Script globals must survive between Update calls: `phase` accumulates dt and drives both
        // a Math call and the entity transform.
        std::ofstream out(dir / "scripts" / "orbit.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Math\n"
               "phase = 0.0\n"
               "function Update(self, dt)\n"
               "    phase = phase + dt\n"
               "    self.x = 100 + 100 * phase\n"
               "    self.y = 50 + 10 * Sin(phase)\n"
               "    return 0\n"
               "end function\n";
    }

    const auto scenePath = dir / "RuntimeTest.gscene.json";
    {
        std::ofstream out(scenePath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "{\n"
               "  \"schemaVersion\": 1,\n"
               "  \"name\": \"RuntimeTest\",\n"
               "  \"settings\": { \"gridSize\": 16, \"backgroundColor\": [10, 20, 30, 255] },\n"
               "  \"entities\": [\n"
               "    { \"id\": 1, \"parentId\": 0, \"name\": \"Hero\", \"visible\": true, \"locked\": false,\n"
               "      \"zOrder\": 2,\n"
               "      \"transform\": { \"x\": 100.0, \"y\": 50.0, \"scaleX\": 1.0, \"scaleY\": 1.0, \"rotation\": 0.0 },\n"
               "      \"sprite\": { \"asset\": \"assets/hero.png\", \"tint\": [255, 255, 255, 255] },\n"
               "      \"script\": { \"path\": \"scripts/orbit.gs\" } },\n"
               "    { \"id\": 2, \"parentId\": 1, \"name\": \"Child\", \"visible\": true, \"locked\": false,\n"
               "      \"zOrder\": 1,\n"
               "      \"transform\": { \"x\": 10.0, \"y\": 5.0, \"scaleX\": 1.0, \"scaleY\": 1.0, \"rotation\": 0.0 },\n"
               "      \"collider\": { \"type\": \"aabb\", \"w\": 20.0, \"h\": 20.0, \"radius\": 8.0, \"offsetX\": 0.0, \"offsetY\": 0.0 } }\n"
               "  ]\n"
               "}\n";
    }

    SceneRuntime runtime;
    std::string error;
    REQUIRE(runtime.Load(scenePath.string(), &error));
    INFO("runtime load error: " << error);
    REQUIRE(runtime.IsLoaded());
    REQUIRE(runtime.GetScene().name == "RuntimeTest");
    REQUIRE(runtime.GetScene().GetEntityCount() == 2);
    REQUIRE(runtime.GetScenePath() == scenePath.string());
    REQUIRE(runtime.GetBaseDir() == dir.string());
    REQUIRE(runtime.GetScriptCount() == 1);
    REQUIRE(runtime.GetLastError().empty());

    // Relative references resolve against the scene file's directory; absolute ones pass through.
    const auto resolved = runtime.ResolveAssetPath("assets/hero.png");
    REQUIRE(std::filesystem::path(resolved) == dir / "assets" / "hero.png");
    REQUIRE(std::filesystem::exists(resolved));
    const auto absolute = (dir / "assets" / "hero.png").string();
    REQUIRE(runtime.ResolveAssetPath(absolute) == absolute);

    // Ticking drives the script; the hierarchy still composes child transforms on top.
    runtime.Update(0.25f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(1)->transform.x) == 125.0);
    runtime.Update(0.25f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(1)->transform.x) == 150.0);
    const float expectedRuntimeY = 50.0f + 10.0f * std::sin(0.5f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(1)->transform.y).epsilon(1e-3) == expectedRuntimeY);
    REQUIRE(Catch::Approx(runtime.GetScene().GetWorldTransform(2).x) == 160.0);

    // A second tick keeps accumulating from the script global instead of restarting at zero.
    REQUIRE(runtime.GetLastError().find("Script error") == std::string::npos);

    // An explicit base dir override re-resolves assets and restarts the scripts.
    runtime.SetBaseDir(dir.generic_string());
    REQUIRE(runtime.GetScriptCount() == 1);

    // Camera: pan centre maps to the viewport centre, and the inverse round-trips.
    RuntimeCamera camera;
    camera.panX = 120.0f;
    camera.panY = 80.0f;
    camera.zoom = 2.0f;
    const auto centre = camera.WorldToScreen(120.0f, 80.0f, 800.0f, 600.0f);
    REQUIRE(Catch::Approx(centre.X) == 400.0);
    REQUIRE(Catch::Approx(centre.Y) == 300.0);
    const auto world = camera.ScreenToWorld(560.0f, 180.0f, 800.0f, 600.0f);
    REQUIRE(Catch::Approx(world.X) == 200.0);
    REQUIRE(Catch::Approx(world.Y) == 20.0);
    camera.Reset();
    REQUIRE(camera.panX == 0.0f);
    REQUIRE(camera.zoom == 1.0f);

    {
        const auto size = runtime.GetScene().FindEntity(1)->transform;
        (void)size;
        runtime.GetCamera().zoom = 1.0f;
        runtime.CenterCamera(0, 0);
        // Entity 1 is at y = 50 + 10*sin(0.5) and entity 2 hangs off it; the camera centres the
        // bounds with zoom left untouched when the target size is unknown.
        REQUIRE(runtime.GetCamera().panX > 100.0f);
        REQUIRE(Catch::Approx(runtime.GetCamera().zoom) == 1.0);
    }

    SceneRenderOptions defaults;
    REQUIRE(defaults.drawBackground);
    REQUIRE(defaults.drawEntities);
    REQUIRE_FALSE(defaults.drawColliders);
    runtime.GetRenderOptions().drawColliders = true;
    REQUIRE(runtime.GetRenderOptions().drawColliders);

    // Unloading drops the scene, the scripts and the path resolution root.
    runtime.Unload();
    REQUIRE_FALSE(runtime.IsLoaded());
    REQUIRE(runtime.GetScriptCount() == 0);
    REQUIRE(runtime.GetScene().GetEntityCount() == 0);
    REQUIRE(runtime.GetBaseDir().empty());

    // A missing file fails cleanly and reports why.
    SceneRuntime missing;
    std::string missingError;
    REQUIRE_FALSE(missing.Load((dir / "nope.gscene.json").string(), &missingError));
    REQUIRE_FALSE(missingError.empty());
    REQUIRE_FALSE(missing.IsLoaded());

    std::filesystem::remove_all(dir, ec);
}
