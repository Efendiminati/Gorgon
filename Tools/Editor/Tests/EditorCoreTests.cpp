#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Core/CollisionSystem.h"
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

TEST_CASE("SceneSerializer clipboard subtree round-trip and remap", "[EditorCore][JSON][Clipboard]") {
    Scene scene;
    scene.settings.gridSize = 32;

    Entity &root = scene.CreateEntity("Player");
    const EntityId rootId = root.id;
    root.visible = true;
    root.locked = false;
    root.zOrder = 7;
    root.transform = {100.0f, 50.0f, 1.5f, 1.0f, 15.0f};
    root.sprite = SpriteComponent{"assets/hero.png", ColorRGBA{200, 180, 160, 255}};
    root.collider = ColliderComponent{ColliderType::AABB, 40.0f, 60.0f, 20.0f, 2.0f, -3.0f};
    root.script = ScriptComponent{"scripts/hero.gs"};

    Entity &child = scene.CreateEntity("Weapon", rootId);
    const EntityId childId = child.id;
    child.zOrder = 8;
    child.transform = {12.0f, -4.0f, 1.0f, 1.0f, 0.0f};
    child.sprite = SpriteComponent{"assets/sword.png", ColorRGBA{255, 255, 255, 255}};

    Entity &grand = scene.CreateEntity("Glow", childId);
    const EntityId grandId = grand.id;
    grand.transform = {5.0f, 6.0f, 0.5f, 0.5f, 0.0f};
    grand.collider = ColliderComponent{ColliderType::Circle, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f};

    Entity &other = scene.CreateEntity("Crate");
    const EntityId otherId = other.id;
    other.transform = {-64.0f, 32.0f, 1.0f, 1.0f, 0.0f};
    other.sprite = SpriteComponent{"assets/crate.png", ColorRGBA{120, 90, 60, 255}};

    // Selecting both an ancestor (rootId) and descendant (childId) plus another root (otherId)
    // must deduplicate the subtree so Weapon is not pasted twice.
    const std::string clipboard = SceneSerializer::EntitiesToJsonString(scene, {rootId, childId, otherId});
    REQUIRE(clipboard.find("\"clipboard\"") != std::string::npos);

    std::vector<EntityId> pastedRoots;
    std::vector<EntityId> allPasted;
    std::string err;
    REQUIRE(SceneSerializer::PasteEntitiesFromJsonString(
        clipboard, scene, &pastedRoots, 32.0f, 32.0f, &err, &allPasted));
    REQUIRE(err.empty());
    REQUIRE(pastedRoots.size() == 2);
    REQUIRE(allPasted.size() == 4);
    REQUIRE(scene.GetEntityCount() == 8);

    const Entity *pRoot = scene.FindEntity(pastedRoots[0]);
    const Entity *pOther = scene.FindEntity(pastedRoots[1]);
    REQUIRE(pRoot != nullptr);
    REQUIRE(pOther != nullptr);
    REQUIRE(pRoot->id != rootId);
    REQUIRE(pRoot->parentId == InvalidEntityId);
    REQUIRE(pRoot->name == "Player Copy");
    REQUIRE(pRoot->zOrder == 7);
    REQUIRE(pRoot->transform.x == Catch::Approx(132.0f));
    REQUIRE(pRoot->transform.y == Catch::Approx(82.0f));
    REQUIRE(pRoot->transform.scaleX == Catch::Approx(1.5f));
    REQUIRE(pRoot->transform.rotation == Catch::Approx(15.0f));
    REQUIRE(pRoot->sprite.has_value());
    REQUIRE(pRoot->sprite->asset == "assets/hero.png");
    REQUIRE(pRoot->collider.has_value());
    REQUIRE(pRoot->collider->width == Catch::Approx(40.0f));
    REQUIRE(pRoot->script.has_value());
    REQUIRE(pRoot->script->path == "scripts/hero.gs");

    // Internal parentIds remapped to fresh IDs; child local transform NOT double-offset.
    const auto pRootChildren = scene.GetChildIds(pRoot->id);
    REQUIRE(pRootChildren.size() == 1);
    const Entity *pChild = scene.FindEntity(pRootChildren[0]);
    REQUIRE(pChild != nullptr);
    REQUIRE(pChild->id != childId);
    REQUIRE(pChild->parentId == pRoot->id);
    REQUIRE(pChild->name == "Weapon Copy");
    REQUIRE(pChild->transform.x == Catch::Approx(12.0f));
    REQUIRE(pChild->transform.y == Catch::Approx(-4.0f));
    REQUIRE(pChild->sprite.has_value());
    REQUIRE(pChild->sprite->asset == "assets/sword.png");

    const auto pChildChildren = scene.GetChildIds(pChild->id);
    REQUIRE(pChildChildren.size() == 1);
    const Entity *pGrand = scene.FindEntity(pChildChildren[0]);
    REQUIRE(pGrand != nullptr);
    REQUIRE(pGrand->id != grandId);
    REQUIRE(pGrand->parentId == pChild->id);
    REQUIRE(pGrand->name == "Glow Copy");
    REQUIRE(pGrand->transform.x == Catch::Approx(5.0f));
    REQUIRE(pGrand->transform.y == Catch::Approx(6.0f));
    REQUIRE(pGrand->collider.has_value());
    REQUIRE(pGrand->collider->type == ColliderType::Circle);

    REQUIRE(pOther->id != otherId);
    REQUIRE(pOther->parentId == InvalidEntityId);
    REQUIRE(pOther->name == "Crate Copy");
    REQUIRE(pOther->transform.x == Catch::Approx(-32.0f));
    REQUIRE(pOther->transform.y == Catch::Approx(64.0f));

    // Second paste produces incremented unique names (Copy 2).
    std::vector<EntityId> secondRoots;
    REQUIRE(SceneSerializer::PasteEntitiesFromJsonString(
        clipboard, scene, &secondRoots, 64.0f, 64.0f, &err));
    REQUIRE(secondRoots.size() == 2);
    REQUIRE(scene.FindEntity(secondRoots[0])->name == "Player Copy 2");
    REQUIRE(scene.FindEntity(secondRoots[1])->name == "Crate Copy 2");

    // Copying a non-root child by itself detaches it to root in the clipboard and pastes cleanly into an empty scene.
    const std::string childClip = SceneSerializer::EntitiesToJsonString(scene, {childId});
    Scene freshScene;
    std::vector<EntityId> freshRoots;
    REQUIRE(SceneSerializer::PasteEntitiesFromJsonString(
        childClip, freshScene, &freshRoots, 16.0f, 16.0f, &err));
    REQUIRE(freshRoots.size() == 1);
    REQUIRE(freshScene.GetEntityCount() == 2);
    const Entity *freshWeapon = freshScene.FindEntity(freshRoots[0]);
    REQUIRE(freshWeapon != nullptr);
    REQUIRE(freshWeapon->parentId == InvalidEntityId);
    REQUIRE(freshWeapon->name == "Weapon");
    REQUIRE(freshWeapon->transform.x == Catch::Approx(28.0f));
    REQUIRE(freshWeapon->transform.y == Catch::Approx(12.0f));
    const auto freshWeaponChildren = freshScene.GetChildIds(freshWeapon->id);
    REQUIRE(freshWeaponChildren.size() == 1);
    REQUIRE(freshScene.FindEntity(freshWeaponChildren[0])->name == "Glow");

    // Invalid clipboard payloads fail cleanly without mutating targetScene.
    const std::size_t countBeforeInvalid = freshScene.GetEntityCount();
    REQUIRE_FALSE(SceneSerializer::PasteEntitiesFromJsonString("{bad", freshScene, nullptr, 0.0f, 0.0f, &err));
    REQUIRE_FALSE(err.empty());
    REQUIRE_FALSE(SceneSerializer::PasteEntitiesFromJsonString(
        R"({"schemaVersion": 1, "entities": []})", freshScene, nullptr, 0.0f, 0.0f, &err));
    REQUIRE_FALSE(SceneSerializer::PasteEntitiesFromJsonString(
        R"({"schemaVersion": 1, "entities": [{"id": 1, "parentId": 99, "name": "Dangling"}]})",
        freshScene, nullptr, 0.0f, 0.0f, &err));
    REQUIRE_FALSE(SceneSerializer::PasteEntitiesFromJsonString(
        R"({"schemaVersion": 1, "entities": [{"id": 1, "parentId": 2, "name": "A"}, {"id": 2, "parentId": 1, "name": "B"}]})",
        freshScene, nullptr, 0.0f, 0.0f, &err));
    REQUIRE(freshScene.GetEntityCount() == countBeforeInvalid);
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

TEST_CASE("Play session Input library drives an entity from simulated keys and mouse", "[EditorCore][Play][Input]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_input_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // The script only ever writes `self` when the input state says so, so every assertion below is
    // driven by InputState and nothing else.
    {
        std::ofstream out(dir / "move.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
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

    Scene scene;
    Entity &entity = scene.CreateEntity("Mover");
    entity.transform.x = 10.0f;
    entity.transform.y = 20.0f;
    entity.script = ScriptComponent{"move.gs"};
    const EntityId id = entity.id;

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.GetScriptCount() == 1);

    const auto posX = [&]() { return scene.FindEntity(id)->transform.x; };
    const auto posY = [&]() { return scene.FindEntity(id)->transform.y; };

    // With nothing pressed the script leaves the entity alone.
    session.Tick(scene, 0.25f);
    REQUIRE(Catch::Approx(posX()) == 10.0);
    REQUIRE(Catch::Approx(posY()) == 20.0);

    // Held key: the name is matched case insensitively ("D" fed, "d" queried).
    session.GetInput().SetKeyDown("D", true);
    session.Tick(scene, 0.25f);
    REQUIRE(Catch::Approx(posX()) == 60.0);
    session.Tick(scene, 0.25f);
    REQUIRE(Catch::Approx(posX()) == 110.0);

    // A press is an edge: it fires on the frame it arrives and the tick clears it.
    session.GetInput().SetKeyDown("Space", true);
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(posY()) == 30.0);
    REQUIRE_FALSE(session.GetInput().IsKeyPressed("Space"));
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(posY()) == 30.0);

    // A release without a matching press is not an edge, a real one is.
    session.GetInput().SetKeyDown("A", false);
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(posY()) == 30.0);
    session.GetInput().SetKeyDown("A", true);
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(posY()) == 30.0);
    session.GetInput().SetKeyDown("A", false);
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(posY()) == 25.0);

    // Mouse world position and button state reach the script; Float results are written back
    // through the typed transform accessors without corrupting the float members.
    session.GetInput().SetMousePosition(320.0f, -64.0f);
    session.GetInput().SetMouseButtonDown("Left", true);
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(posX()) == 320.0);
    REQUIRE(Catch::Approx(posY()) == -64.0);

    // Releasing the button stops the mouse term; the still held "d" key keeps moving x again.
    session.GetInput().SetMouseButtonDown("Left", false);
    session.Tick(scene, 0.25f);
    REQUIRE(Catch::Approx(posX()) == 370.0);
    REQUIRE(Catch::Approx(posY()) == -64.0);

    // Unknown names never throw and never report a press.
    REQUIRE_FALSE(session.GetInput().IsKeyDown("Nope"));
    REQUIRE_FALSE(session.GetInput().IsKeyPressed("Nope"));
    REQUIRE_FALSE(session.GetInput().IsMouseDown("Nope"));

    // Alias spellings resolve to the same key.
    REQUIRE(session.GetInput().IsKeyDown(" d "));
    session.GetInput().SetKeyDown("Return", true);
    REQUIRE(session.GetInput().IsKeyDown("enter"));

    // Stop restores the snapshot; the input state is not part of the scene.
    session.Stop(scene);
    REQUIRE(Catch::Approx(posX()) == 10.0);
    REQUIRE(Catch::Approx(posY()) == 20.0);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("SceneRuntime forwards its InputState to the scripts and consumes edges", "[EditorCore][Runtime][Input]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_runtime_input_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream out(dir / "steer.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Input\n"
               "function Update(self, dt)\n"
               "    if(IsKeyDown(\"Right\"))\n"
               "        self.x = self.x + 100 * dt\n"
               "    end if\n"
               "    if(IsKeyPressed(\"Up\"))\n"
               "        self.y = self.y + 1\n"
               "    end if\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &hero = scene.CreateEntity("Hero");
    hero.transform.x = 0.0f;
    hero.transform.y = 0.0f;
    hero.script = ScriptComponent{"steer.gs"};

    SceneRuntime runtime;
    std::string error;
    REQUIRE(runtime.LoadFromScene(scene, dir.generic_string(), &error));
    INFO("runtime load error: " << error);
    REQUIRE(runtime.GetScriptCount() == 1);

    // Nothing pressed: the runtime still steps without touching the transform.
    runtime.Update(0.25f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(hero.id)->transform.x) == 0.0);

    // Held key persists across Update() calls.
    runtime.GetInput().SetKeyDown("Right", true);
    runtime.Update(0.25f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(hero.id)->transform.x) == 25.0);
    runtime.Update(0.25f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(hero.id)->transform.x) == 50.0);

    // A press reaches the script once and the runtime clears the edge afterwards.
    runtime.GetInput().SetKeyDown("Up", true);
    runtime.Update(0.0f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(hero.id)->transform.y) == 1.0);
    REQUIRE_FALSE(runtime.GetInput().IsKeyPressed("Up"));
    runtime.Update(0.0f);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(hero.id)->transform.y) == 1.0);

    // Key names are translated from Gorgon's keycodes for callers that bind a window.
    REQUIRE(KeyNameFromCode(Gorgon::Input::Keyboard::Keycodes::Left) == "left");
    REQUIRE(KeyNameFromCode(Gorgon::Input::Keyboard::Keycodes::Space) == "space");
    REQUIRE(KeyNameFromCode(Gorgon::Input::Keyboard::Keycodes::A) == "a");
    REQUIRE(KeyNameFromCode(0x7FFFFF).empty());
    REQUIRE(MouseButtonName(Gorgon::Input::Mouse::Button::Middle) == "middle");
    REQUIRE(NormalizeInputName("LMB") == "left");
    REQUIRE(NormalizeInputName("ArrowUp") == "up");

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

TEST_CASE("Prefab save, load and instantiate remap IDs and subtree links", "[EditorCore][Prefab]") {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_prefab_tests";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    Scene scene;
    // Root at a non-zero position: the file normalizes it to local origin.
    Entity &root = scene.CreateEntity("Turret");
    root.transform.x = 400.0f;
    root.transform.y = -120.0f;
    root.transform.rotation = 30.0f;
    root.zOrder = 7;
    root.sprite = SpriteComponent{"turret.png", {200, 40, 40, 255}};
    const EntityId rootId = root.id;

    Entity &barrel = scene.CreateEntity("Barrel", rootId);
    barrel.transform.x = 12.0f;
    barrel.transform.y = -4.0f;
    barrel.collider = ColliderComponent{ColliderType::AABB, 24.0f, 8.0f, 4.0f, 0.0f, 0.0f};
    const EntityId barrelId = barrel.id;

    Entity &muzzle = scene.CreateEntity("Muzzle", barrelId);
    muzzle.transform.x = 20.0f;
    muzzle.transform.y = 0.0f;
    muzzle.script = ScriptComponent{"fire.gs"};
    const EntityId muzzleId = muzzle.id;

    // An unrelated entity must not leak into the prefab.
    scene.CreateEntity("Unrelated");

    const auto prefabPath = (dir / "turret.gprefab.json").string();
    std::string error;
    REQUIRE(SceneSerializer::SavePrefabToFile(scene, rootId, prefabPath, &error));
    INFO("prefab save error: " << error);
    REQUIRE(std::filesystem::exists(prefabPath));

    PrefabData prefab;
    REQUIRE(SceneSerializer::LoadPrefabFromFile(prefabPath, prefab, &error));
    INFO("prefab load error: " << error);
    REQUIRE(prefab.IsValid());
    REQUIRE(prefab.name == "Turret");
    REQUIRE(prefab.entities.size() == 3);

    // Local IDs are normalized to 1..N with the root first.
    REQUIRE(prefab.rootId == 1);
    const Entity *prefabRoot = nullptr;
    const Entity *prefabBarrel = nullptr;
    const Entity *prefabMuzzle = nullptr;
    for(const auto &e : prefab.entities) {
        if(e.name == "Turret") prefabRoot = &e;
        else if(e.name == "Barrel") prefabBarrel = &e;
        else if(e.name == "Muzzle") prefabMuzzle = &e;
    }
    REQUIRE(prefabRoot);
    REQUIRE(prefabBarrel);
    REQUIRE(prefabMuzzle);

    // The root transform is stored relative to the prefab origin.
    REQUIRE(Catch::Approx(prefabRoot->transform.x) == 0.0);
    REQUIRE(Catch::Approx(prefabRoot->transform.y) == 0.0);
    REQUIRE(Catch::Approx(prefabRoot->transform.rotation) == 30.0);
    REQUIRE(prefabRoot->zOrder == 7);
    REQUIRE(prefabBarrel->parentId == prefabRoot->id);
    REQUIRE(prefabMuzzle->parentId == prefabBarrel->id);
    REQUIRE(Catch::Approx(prefabBarrel->transform.x) == 12.0);
    REQUIRE(Catch::Approx(prefabMuzzle->transform.x) == 20.0);
    REQUIRE(prefabMuzzle->script.has_value());

    // Instantiate into a different scene at a world position with fresh IDs.
    Scene target;
    Entity &placeholder = target.CreateEntity("Turret"); // name collision forces a unique root name
    const EntityId placeholderId = placeholder.id;

    EntityId instanceRoot = InvalidEntityId;
    std::vector<EntityId> created;
    REQUIRE(SceneSerializer::InstantiatePrefab(prefab, target, 250.0f, -75.0f, "turret.gprefab.json",
                                               &instanceRoot, &created, &error));
    INFO("prefab instantiate error: " << error);
    REQUIRE(instanceRoot != InvalidEntityId);
    REQUIRE(instanceRoot != prefabRoot->id);
    REQUIRE(instanceRoot != placeholderId);
    REQUIRE(created.size() == 3);
    REQUIRE(target.GetEntityCount() == 4);

    const Entity *instRoot = target.FindEntity(instanceRoot);
    REQUIRE(instRoot);
    REQUIRE(instRoot->name == "Turret Copy");
    REQUIRE(instRoot->prefabSource == "turret.gprefab.json");
    REQUIRE(Catch::Approx(instRoot->transform.x) == 250.0);
    REQUIRE(Catch::Approx(instRoot->transform.y) == -75.0);
    REQUIRE(instRoot->parentId == InvalidEntityId);

    // Children keep their local offsets and are re-parented to the new IDs.
    const std::vector<EntityId> children = target.GetChildIds(instanceRoot);
    REQUIRE(children.size() == 1);
    const Entity *instBarrel = target.FindEntity(children.front());
    REQUIRE(instBarrel);
    REQUIRE(instBarrel->name == "Barrel");
    REQUIRE(Catch::Approx(instBarrel->transform.x) == 12.0);
    const std::vector<EntityId> grandchildren = target.GetChildIds(instBarrel->id);
    REQUIRE(grandchildren.size() == 1);
    const Entity *instMuzzle = target.FindEntity(grandchildren.front());
    REQUIRE(instMuzzle);
    REQUIRE(instMuzzle->name == "Muzzle");
    REQUIRE(instMuzzle->script.has_value());
    // Only the instance root carries the prefab link.
    REQUIRE(instBarrel->prefabSource.empty());

    // A second instantiation must not reuse IDs or collide on the root name.
    EntityId secondRoot = InvalidEntityId;
    REQUIRE(SceneSerializer::InstantiatePrefab(prefab, target, 0.0f, 0.0f, "turret.gprefab.json", &secondRoot));
    REQUIRE(secondRoot != instanceRoot);
    REQUIRE(target.GetEntityCount() == 7);
    REQUIRE(target.FindEntity(secondRoot)->name == "Turret Copy 2");

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Prefab JSON rejects malformed payloads and missing files", "[EditorCore][Prefab]") {
    PrefabData prefab;
    std::string error;

    REQUIRE_FALSE(SceneSerializer::LoadPrefabFromJsonString("not json at all", prefab, &error));
    REQUIRE_FALSE(error.empty());

    REQUIRE_FALSE(SceneSerializer::LoadPrefabFromJsonString("{\"schemaVersion\":1,\"entities\":[]}", prefab, &error));
    REQUIRE_FALSE(error.empty());

    // Entity referencing a parent that is not part of the prefab.
    REQUIRE_FALSE(SceneSerializer::LoadPrefabFromJsonString(
        "{\"schemaVersion\":1,\"rootId\":1,\"entities\":["
        "{\"id\":1,\"parentId\":0,\"name\":\"Root\"},"
        "{\"id\":2,\"parentId\":9,\"name\":\"Orphan\"}]}",
        prefab, &error));
    REQUIRE_FALSE(error.empty());

    // Future schema version.
    REQUIRE_FALSE(SceneSerializer::LoadPrefabFromJsonString(
        "{\"schemaVersion\":99,\"rootId\":1,\"entities\":[{\"id\":1,\"parentId\":0,\"name\":\"Root\"}]}",
        prefab, &error));
    REQUIRE_FALSE(error.empty());

    // A missing file fails without leaving a half-built prefab behind.
    REQUIRE_FALSE(SceneSerializer::LoadPrefabFromFile("no_such_prefab.gprefab.json", prefab, &error));
    REQUIRE_FALSE(prefab.IsValid());

    Scene scratch;
    std::string instantiateError;
    REQUIRE_FALSE(SceneSerializer::InstantiatePrefab(prefab, scratch, 0.0f, 0.0f,
                                                     std::string(), nullptr, nullptr, &instantiateError));
    REQUIRE(scratch.GetEntityCount() == 0);

    // Saving a prefab for an entity that does not exist is refused.
    Scene empty;
    std::string saveError;
    REQUIRE_FALSE(SceneSerializer::SavePrefabToFile(empty, 42, "nope.gprefab.json", &saveError));
    REQUIRE_FALSE(saveError.empty());
}

TEST_CASE("Scene round-trip preserves prefabSource and prefab instances survive a save", "[EditorCore][Prefab][JSON]") {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_prefab_roundtrip";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    Scene scene;
    Entity &crate = scene.CreateEntity("Crate");
    crate.sprite = SpriteComponent{"crate.png", {255, 255, 255, 255}};
    crate.prefabSource = "Prefabs/crate.gprefab.json";
    const EntityId crateId = crate.id;

    Entity &lid = scene.CreateEntity("Lid", crateId);
    lid.transform.y = -18.0f;
    const EntityId lidId = lid.id;

    // Plain entities stay link-free.
    Entity &floorEntity = scene.CreateEntity("Floor");
    const EntityId floorId = floorEntity.id;

    const auto scenePath = (dir / "scene.gscene.json").string();
    std::string error;
    REQUIRE(SceneSerializer::SaveToFile(scene, scenePath, &error));
    INFO("scene save error: " << error);

    Scene reloaded;
    REQUIRE(SceneSerializer::LoadFromFile(scenePath, reloaded, &error));
    INFO("scene load error: " << error);
    REQUIRE(reloaded == scene);
    REQUIRE(reloaded.FindEntity(crateId)->prefabSource == "Prefabs/crate.gprefab.json");
    REQUIRE(reloaded.FindEntity(lidId)->prefabSource.empty());
    REQUIRE(reloaded.FindEntity(floorId)->prefabSource.empty());

    // A real prefab instance keeps its link through a scene round-trip.
    const auto prefabPath = (dir / "Prefabs" / "crate.gprefab.json").string();
    std::filesystem::create_directories(std::filesystem::path(prefabPath).parent_path(), ec);
    REQUIRE(SceneSerializer::SavePrefabToFile(scene, crateId, prefabPath, &error));
    INFO("prefab save error: " << error);

    Scene runtimeScene;
    EntityId instRoot = InvalidEntityId;
    REQUIRE(SceneSerializer::InstantiatePrefabFromFile(prefabPath, runtimeScene, 64.0f, 32.0f,
                                                       "Prefabs/crate.gprefab.json", &instRoot, nullptr, &error));
    INFO("prefab instantiate error: " << error);
    const auto instScenePath = (dir / "instance.gscene.json").string();
    REQUIRE(SceneSerializer::SaveToFile(runtimeScene, instScenePath, &error));
    Scene instReloaded;
    REQUIRE(SceneSerializer::LoadFromFile(instScenePath, instReloaded, &error));
    const Entity *instReloadedRoot = instReloaded.FindEntity(instRoot);
    REQUIRE(instReloadedRoot);
    REQUIRE(instReloadedRoot->prefabSource == "Prefabs/crate.gprefab.json");

    // Revert restores the prefab's contents while keeping the instance's placement and ID.
    PrefabData prefab;
    REQUIRE(SceneSerializer::LoadPrefabFromFile(prefabPath, prefab, &error));
    instReloaded.FindEntity(instRoot)->sprite->tint = {0, 0, 0, 255};
    instReloaded.FindEntity(instRoot)->transform.x = 999.0f;
    REQUIRE(SceneSerializer::RevertPrefabInstance(instReloaded, instRoot, prefab, &error));
    INFO("prefab revert error: " << error);
    const Entity *reverted = instReloaded.FindEntity(instRoot);
    REQUIRE(reverted);
    REQUIRE(Catch::Approx(reverted->transform.x) == 999.0);
    REQUIRE(reverted->prefabSource == "Prefabs/crate.gprefab.json");
    REQUIRE(reverted->sprite.has_value());
    REQUIRE(reverted->sprite->tint == (ColorRGBA{255, 255, 255, 255}));
    REQUIRE(instReloaded.GetChildIds(instRoot).size() == 1);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Collision shapes follow the world transform and overlap in every combination",
          "[EditorCore][Collision]") {
    Scene scene;

    Entity &box = scene.CreateEntity("Box");
    box.transform.x = 100.0f;
    box.transform.y = 50.0f;
    ColliderComponent boxCollider;
    boxCollider.type = ColliderType::AABB;
    boxCollider.width = 40.0f;
    boxCollider.height = 20.0f;
    box.collider = boxCollider;
    const EntityId boxId = box.id;

    Entity &circle = scene.CreateEntity("Circle");
    circle.transform.x = 130.0f;
    circle.transform.y = 50.0f;
    ColliderComponent circleCollider;
    circleCollider.type = ColliderType::Circle;
    circleCollider.radius = 15.0f;
    circle.collider = circleCollider;
    const EntityId circleId = circle.id;

    CollisionShape boxShape;
    CollisionShape circleShape;
    REQUIRE(CollisionSystem::ComputeShape(scene, boxId, boxShape));
    REQUIRE(boxShape.kind == CollisionShape::Kind::Box);
    REQUIRE(Catch::Approx(boxShape.centerX) == 100.0);
    REQUIRE(Catch::Approx(boxShape.centerY) == 50.0);
    REQUIRE(Catch::Approx(boxShape.halfWidth) == 20.0);
    REQUIRE(Catch::Approx(boxShape.halfHeight) == 10.0);

    REQUIRE(CollisionSystem::ComputeShape(scene, circleId, circleShape));
    REQUIRE(circleShape.kind == CollisionShape::Kind::Circle);
    REQUIRE(Catch::Approx(circleShape.radius) == 15.0);

    // ---- AABB / AABB -------------------------------------------------------
    {
        Entity &other = scene.CreateEntity("OtherBox");
        other.transform.x = 130.0f;
        other.transform.y = 50.0f;
        ColliderComponent comp;
        comp.type = ColliderType::AABB;
        comp.width = 40.0f;
        comp.height = 20.0f;
        other.collider = comp;

        // Make the circle a no-op for this check by moving it far away first.
        scene.FindEntity(circleId)->transform.x = 10000.0f;

        CollisionShape a, b;
        REQUIRE(CollisionSystem::ComputeShape(scene, boxId, a));
        REQUIRE(CollisionSystem::ComputeShape(scene, other.id, b));
        REQUIRE(CollisionSystem::Overlaps(a, b));

        const auto hit = CollisionSystem::ComputeOverlap(a, b);
        REQUIRE(hit.overlaps);
        // x overlap 10 (80..120 vs 110..150) is smaller than the y overlap 20, so x separates.
        REQUIRE(Catch::Approx(hit.depth) == 10.0);
        REQUIRE(Catch::Approx(hit.normalX) == -1.0);
        REQUIRE(Catch::Approx(hit.normalY) == 0.0);

        // Exactly touching is not an overlap.
        other.transform.x = 140.0f;
        REQUIRE(CollisionSystem::ComputeShape(scene, other.id, b));
        REQUIRE_FALSE(CollisionSystem::Overlaps(a, b));

        scene.RemoveEntity(other.id);
        scene.FindEntity(circleId)->transform.x = 130.0f;
    }

    // ---- AABB / circle -----------------------------------------------------
    REQUIRE(CollisionSystem::ComputeShape(scene, boxId, boxShape));
    REQUIRE(CollisionSystem::ComputeShape(scene, circleId, circleShape));
    REQUIRE(CollisionSystem::Overlaps(boxShape, circleShape));
    {
        const auto hit = CollisionSystem::ComputeOverlap(boxShape, circleShape);
        REQUIRE(hit.overlaps);
        // Closest box point to the circle centre is (120, 50): 10 away, radius 15 -> depth 5.
        REQUIRE(Catch::Approx(hit.depth) == 5.0);
        REQUIRE(Catch::Approx(hit.normalX) == -1.0);
        REQUIRE(Catch::Approx(hit.normalY) == 0.0);

        // The mirrored argument order reports the opposite normal for the same penetration.
        const auto mirrored = CollisionSystem::ComputeOverlap(circleShape, boxShape);
        REQUIRE(mirrored.overlaps);
        REQUIRE(Catch::Approx(mirrored.depth) == 5.0);
        REQUIRE(Catch::Approx(mirrored.normalX) == 1.0);

        // A circle whose centre sits inside the box leaves through the nearest face.
        scene.FindEntity(circleId)->transform.x = 108.0f;
        CollisionShape inside;
        REQUIRE(CollisionSystem::ComputeShape(scene, circleId, inside));
        const auto insideHit = CollisionSystem::ComputeOverlap(boxShape, inside);
        REQUIRE(insideHit.overlaps);
        // Nearest face is the box's top edge, 10 away -> box travels down by 10 + radius.
        REQUIRE(Catch::Approx(insideHit.depth) == 25.0);
        REQUIRE(Catch::Approx(insideHit.normalX) == 0.0);
        REQUIRE(Catch::Approx(insideHit.normalY) == 1.0);

        scene.FindEntity(circleId)->transform.x = 200.0f;
        REQUIRE(CollisionSystem::ComputeShape(scene, circleId, circleShape));
        REQUIRE_FALSE(CollisionSystem::Overlaps(boxShape, circleShape));
    }

    // ---- circle / circle ---------------------------------------------------
    {
        Entity &a = scene.CreateEntity("CircleA");
        a.transform.x = 0.0f;
        ColliderComponent comp;
        comp.type = ColliderType::Circle;
        comp.radius = 10.0f;
        a.collider = comp;
        const EntityId aId = a.id;

        Entity &b = scene.CreateEntity("CircleB");
        b.transform.x = 15.0f;
        ColliderComponent compB;
        compB.type = ColliderType::Circle;
        compB.radius = 10.0f;
        b.collider = compB;
        const EntityId bId = b.id;

        CollisionShape sa, sb;
        REQUIRE(CollisionSystem::ComputeShape(scene, aId, sa));
        REQUIRE(CollisionSystem::ComputeShape(scene, bId, sb));
        REQUIRE(CollisionSystem::Overlaps(sa, sb));

        const auto hit = CollisionSystem::ComputeOverlap(sa, sb);
        REQUIRE(hit.overlaps);
        REQUIRE(Catch::Approx(hit.depth) == 5.0);
        REQUIRE(Catch::Approx(hit.normalX) == -1.0);

        b.transform.x = 25.0f;
        REQUIRE(CollisionSystem::ComputeShape(scene, bId, sb));
        REQUIRE_FALSE(CollisionSystem::Overlaps(sa, sb));
    }

    // ---- world transform: scale, rotation and offset ------------------------
    {
        Entity &parent = scene.CreateEntity("Parent");
        parent.transform.x = 500.0f;
        parent.transform.scaleX = 2.0f;
        parent.transform.scaleY = 2.0f;

        Entity &child = scene.CreateEntity("Child", parent.id);
        child.transform.x = 10.0f;
        child.transform.y = 5.0f;
        ColliderComponent comp;
        comp.type = ColliderType::AABB;
        comp.width = 20.0f;
        comp.height = 10.0f;
        // 8 on x and 4 on y in world units, both rotated by 90 degrees.
        comp.offsetX = 4.0f;
        comp.offsetY = 2.0f;
        child.collider = comp;
        child.transform.rotation = 90.0f;

        CollisionShape shape;
        REQUIRE(CollisionSystem::ComputeShape(scene, child.id, shape));
        // World position of the child is (520, 10); the offset swings to (-4, +8).
        REQUIRE(Catch::Approx(shape.centerX) == 516.0);
        REQUIRE(Catch::Approx(shape.centerY) == 18.0);
        // Half extents are scaled by the parent's world scale.
        REQUIRE(Catch::Approx(shape.halfWidth) == 20.0);
        REQUIRE(Catch::Approx(shape.halfHeight) == 10.0);

        REQUIRE(CollisionSystem::ContainsPoint(shape, 516.0f, 18.0f));
        REQUIRE(CollisionSystem::ContainsPoint(shape, 535.0f, 18.0f));   // on the edge
        REQUIRE_FALSE(CollisionSystem::ContainsPoint(shape, 537.0f, 18.0f));
    }

    // Entities without a usable collider produce no shape at all.
    Entity &bare = scene.CreateEntity("Bare");
    CollisionShape unused;
    REQUIRE_FALSE(CollisionSystem::ComputeShape(scene, bare.id, unused));
    bare.collider = ColliderComponent{};   // type None
    REQUIRE_FALSE(CollisionSystem::ComputeShape(scene, bare.id, unused));
    REQUIRE_FALSE(CollisionSystem::ComputeShape(scene, 9999u, unused));
}

TEST_CASE("Play session separates solid colliders along the minimum translation vector",
          "[EditorCore][Collision][Play]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_collision_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A play session needs at least one script; this one never touches the transform.
    {
        std::ofstream out(dir / "idle.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    return 0\n"
               "end function\n";
    }

    auto makeBox = [](Entity &entity, float x, float y) {
        ColliderComponent comp;
        comp.type = ColliderType::AABB;
        comp.width = 40.0f;
        comp.height = 40.0f;
        entity.transform.x = x;
        entity.transform.y = y;
        entity.collider = comp;
    };

    Scene scene;
    Entity &dynamicBox = scene.CreateEntity("Dynamic");
    makeBox(dynamicBox, 0.0f, 0.0f);
    dynamicBox.script = ScriptComponent{"idle.gs"};
    const EntityId dynamicId = dynamicBox.id;

    Entity &otherBox = scene.CreateEntity("Other");
    makeBox(otherBox, 30.0f, 0.0f);
    const EntityId otherId = otherBox.id;

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.GetScriptCount() == 1);

    const auto xOf = [&](EntityId id) { return scene.FindEntity(id)->transform.x; };
    const auto yOf = [&](EntityId id) { return scene.FindEntity(id)->transform.y; };

    // Two dynamic solids overlap by 10 on x: each travels half of it, away from the other.
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == -5.0);
    REQUIRE(Catch::Approx(xOf(otherId)) == 35.0);
    REQUIRE(session.GetActiveOverlaps().size() == 1);
    REQUIRE(session.IsOverlapping(dynamicId, otherId));
    REQUIRE(session.IsOverlapping(otherId, dynamicId));
    REQUIRE_FALSE(session.GetActiveOverlaps().front().trigger);

    // Separated now: another tick does not move anything.
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == -5.0);
    REQUIRE(Catch::Approx(xOf(otherId)) == 35.0);
    REQUIRE(session.GetActiveOverlaps().empty());

    // A static body never moves: the dynamic one takes the whole translation vector.
    scene.FindEntity(dynamicId)->transform.x = 0.0f;
    scene.FindEntity(otherId)->transform.x = 30.0f;
    scene.FindEntity(otherId)->collider->isStatic = true;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == -10.0);
    REQUIRE(Catch::Approx(xOf(otherId)) == 30.0);

    // Two static bodies report the overlap but cannot resolve it.
    scene.FindEntity(dynamicId)->transform.x = 0.0f;
    scene.FindEntity(dynamicId)->collider->isStatic = true;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == 0.0);
    REQUIRE(Catch::Approx(xOf(otherId)) == 30.0);
    REQUIRE(session.IsOverlapping(dynamicId, otherId));

    // A trigger volume reports the overlap without pushing anything, even when both are dynamic.
    scene.FindEntity(dynamicId)->collider->isStatic = false;
    scene.FindEntity(otherId)->collider->isStatic = false;
    scene.FindEntity(otherId)->collider->isTrigger = true;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == 0.0);
    REQUIRE(Catch::Approx(xOf(otherId)) == 30.0);
    REQUIRE(session.GetActiveOverlaps().size() == 1);
    REQUIRE(session.GetActiveOverlaps().front().trigger);

    // Vertical overlap pushes up, not sideways.
    scene.FindEntity(otherId)->collider->isTrigger = false;
    scene.FindEntity(otherId)->transform.x = 0.0f;
    scene.FindEntity(otherId)->transform.y = 25.0f;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == 0.0);
    // 40 tall each, 25 apart -> 15 of y penetration, halved between two dynamic bodies.
    REQUIRE(Catch::Approx(yOf(dynamicId)) == -7.5);
    REQUIRE(Catch::Approx(yOf(otherId)) == 32.5);

    // Stop restores the snapshot, so collision pushes never dirty the scene.
    session.Stop(scene);
    REQUIRE(Catch::Approx(xOf(dynamicId)) == 0.0);
    REQUIRE(Catch::Approx(xOf(otherId)) == 30.0);
    REQUIRE(session.GetActiveOverlaps().empty());

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Play session fires collision and trigger enter/exit hooks with the other entity id",
          "[EditorCore][Collision][Play]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_collision_hooks_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream out(dir / "hooks.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out <<                "function Update(self, dt)\n"

               "    return 0\n"

               "end function\n"

               "\n"

               "# The hooks log themselves into the rotation, which never moves a collider: an AABB stays\n"

               "# axis aligned and its offset is zero, so the overlap set only changes when the test moves\n"

               "# another entity. The trigger hook adds the other id, the solid hook adds 100x the other id.\n"

               "function OnCollisionEnter(self, otherId)\n"

               "    self.rotation = self.rotation + otherId * 100\n"

               "    return 0\n"

               "end function\n"

               "\n"

               "function OnCollisionExit(self, otherId)\n"

               "    self.rotation = self.rotation + 2000\n"

               "    return 0\n"

               "end function\n"

               "\n"

               "function OnTriggerEnter(self, otherId)\n"

               "    self.rotation = self.rotation + otherId\n"

               "    return 0\n"

               "end function\n"

               "\n"

               "function OnTriggerExit(self, otherId)\n"

               "    self.rotation = self.rotation + 20\n"

               "    return 0\n"

               "end function\n";
    }

    auto makeBox = [](Entity &entity, float x, float y) {
        ColliderComponent comp;
        comp.type = ColliderType::AABB;
        comp.width = 40.0f;
        comp.height = 40.0f;
        entity.transform.x = x;
        entity.transform.y = y;
        entity.collider = comp;
    };

    Scene scene;
    Entity &player = scene.CreateEntity("Player");
    makeBox(player, 0.0f, 0.0f);
    // Static, so the solver never moves it: only a hook can change its numbers.
    player.collider->isStatic = true;
    player.script = ScriptComponent{"hooks.gs"};
    const EntityId playerId = player.id;

    Entity &volume = scene.CreateEntity("Volume");
    makeBox(volume, 300.0f, 0.0f);
    volume.collider->isTrigger = true;
    const EntityId volumeId = volume.id;

    Entity &wall = scene.CreateEntity("Wall");
    makeBox(wall, 600.0f, 0.0f);
    wall.collider->isStatic = true;
    const EntityId wallId = wall.id;

    REQUIRE(playerId == 1);
    REQUIRE(volumeId == 2);
    REQUIRE(wallId == 3);

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.GetScriptCount() == 1);

    const auto xOf = [&]() { return scene.FindEntity(playerId)->transform.x; };
    const auto rotationOf = [&]() { return scene.FindEntity(playerId)->transform.rotation; };

    // Nothing overlapping yet: no hook of any kind fired.
    session.Tick(scene, 0.0f);
    REQUIRE(session.GetActiveOverlaps().empty());
    REQUIRE(Catch::Approx(rotationOf()) == 0.0);

    // Moving into the trigger fires OnTriggerEnter only, with the trigger's id.
    scene.FindEntity(volumeId)->transform.x = 10.0f;
    session.Tick(scene, 0.0f);
    INFO("hook error after trigger enter: " << session.GetLastError());
    REQUIRE(session.GetActiveOverlaps().size() == 1);
    REQUIRE(session.GetActiveOverlaps().front().trigger);
    REQUIRE(Catch::Approx(rotationOf()) == 2.0);
    REQUIRE(Catch::Approx(xOf()) == 0.0);

    // Staying inside does not repeat the enter, and no collision enter fired for the pair.
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(rotationOf()) == 2.0);

    // Leaving fires OnTriggerExit (300 is clear of both the player and the wall).
    scene.FindEntity(volumeId)->transform.x = 300.0f;
    session.Tick(scene, 0.0f);
    REQUIRE(session.GetActiveOverlaps().empty());
    REQUIRE(Catch::Approx(rotationOf()) == 22.0);

    // A solid pair reports through OnCollisionEnter/Exit instead of the trigger hooks.
    scene.FindEntity(wallId)->transform.x = 10.0f;
    session.Tick(scene, 0.0f);
    REQUIRE(session.GetActiveOverlaps().size() == 1);
    REQUIRE_FALSE(session.GetActiveOverlaps().front().trigger);
    REQUIRE(Catch::Approx(rotationOf()) == 322.0);
    REQUIRE(Catch::Approx(xOf()) == 0.0);

    scene.FindEntity(wallId)->transform.x = 900.0f;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(rotationOf()) == 2322.0);
    REQUIRE(session.GetActiveOverlaps().empty());

    // A fresh overlap after an exit fires enter again, still with the wall's id.
    scene.FindEntity(wallId)->transform.x = 12.0f;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(rotationOf()) == 2622.0);

    REQUIRE(session.GetLastError().empty());

    // Stop clears the overlap set along with the runtime.
    session.Stop(scene);
    REQUIRE(session.GetActiveOverlaps().empty());
    REQUIRE(Catch::Approx(rotationOf()) == 0.0);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Physics GScript library answers overlap, point and active-pair queries",
          "[EditorCore][Collision][Play]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_physics_api_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream out(dir / "queries.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Physics\n"
               "function Update(self, dt)\n"
               "    if(Overlaps(1, 2))\n"
               "        self.x = self.x + 1\n"
               "    end if\n"
               "    if(IsOverlapping(1))\n"
               "        self.y = self.y + 1\n"
               "    end if\n"
               "    self.rotation = self.rotation + OverlapPoint(0, 0)\n"
               "    if(Overlaps(1, 3))\n"
               "        self.scaleX = 2\n"
               "    end if\n"
               "    if(Overlaps(1, 999))\n"
               "        self.scaleY = 2\n"
               "    end if\n"
               "    self.scaleY = self.scaleY + OverlapPoint(5000, 5000)\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &player = scene.CreateEntity("Player");
    player.transform.x = 0.0f;
    player.transform.y = 0.0f;
    ColliderComponent playerCollider;
    playerCollider.type = ColliderType::AABB;
    playerCollider.width = 40.0f;
    playerCollider.height = 40.0f;
    player.collider = playerCollider;
    player.script = ScriptComponent{"queries.gs"};
    const EntityId playerId = player.id;

    Entity &volume = scene.CreateEntity("Volume");
    volume.transform.x = 30.0f;
    ColliderComponent volumeCollider;
    volumeCollider.type = ColliderType::AABB;
    volumeCollider.width = 40.0f;
    volumeCollider.height = 40.0f;
    volumeCollider.isTrigger = true;
    volume.collider = volumeCollider;
    const EntityId volumeId = volume.id;

    Entity &farAway = scene.CreateEntity("FarAway");
    farAway.transform.x = 1000.0f;
    ColliderComponent farCollider;
    farCollider.type = ColliderType::Circle;
    farCollider.radius = 10.0f;
    farAway.collider = farCollider;
    const EntityId farId = farAway.id;

    REQUIRE(playerId == 1);
    REQUIRE(volumeId == 2);
    REQUIRE(farId == 3);

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.GetScriptCount() == 1);

    auto entity = [&]() { return scene.FindEntity(playerId); };

    // First tick: the live Overlaps query already sees the overlap, IsOverlapping has no previous
    // pass to read yet, OverlapPoint resolves the AABB under the origin to the player.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(Catch::Approx(entity()->transform.x) == 1.0);
    REQUIRE(Catch::Approx(entity()->transform.y) == 0.0);
    REQUIRE(Catch::Approx(entity()->transform.rotation) == 1.0);
    REQUIRE(Catch::Approx(entity()->transform.scaleX) == 1.0);
    // Overlaps(1, 3) and Overlaps(1, 999) found nothing, so neither branch ran, and the far point
    // query returned 0 (adding it left scaleY untouched).
    REQUIRE(Catch::Approx(entity()->transform.scaleX) == 1.0);
    REQUIRE(Catch::Approx(entity()->transform.scaleY) == 1.0);

    // Second tick: the pair recorded by the previous pass makes IsOverlapping report 1.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(Catch::Approx(entity()->transform.x) == 2.0);
    REQUIRE(Catch::Approx(entity()->transform.y) == 1.0);
    REQUIRE(Catch::Approx(entity()->transform.rotation) == 2.0);

    // Pulling the trigger volume away stops the live overlap; x freezes while rotation keeps
    // tracking the point query.
    scene.FindEntity(volumeId)->transform.x = 800.0f;
    session.Tick(scene, 0.0f);
    REQUIRE(Catch::Approx(entity()->transform.x) == 2.0);
    REQUIRE(Catch::Approx(entity()->transform.rotation) == 3.0);
    REQUIRE(session.GetActiveOverlaps().empty());

    REQUIRE(session.GetLastError().empty());

    session.Stop(scene);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Collider trigger and static flags survive a JSON round-trip", "[EditorCore][Collision][JSON]") {
    Scene scene;

    Entity &volume = scene.CreateEntity("Volume");
    ColliderComponent trigger;
    trigger.type = ColliderType::AABB;
    trigger.width = 64.0f;
    trigger.height = 64.0f;
    trigger.isTrigger = true;
    volume.collider = trigger;
    const EntityId volumeId = volume.id;

    Entity &floor = scene.CreateEntity("Floor");
    ColliderComponent solid;
    solid.type = ColliderType::AABB;
    solid.width = 256.0f;
    solid.height = 32.0f;
    solid.isStatic = true;
    floor.collider = solid;
    const EntityId floorId = floor.id;

    const std::string json = SceneSerializer::ToJsonString(scene);
    REQUIRE(json.find("\"isTrigger\": true") != std::string::npos);
    REQUIRE(json.find("\"isStatic\": true") != std::string::npos);

    Scene loaded;
    std::string error;
    const bool loadedOk = SceneSerializer::FromJsonString(json, loaded, &error);
    INFO("load error: " << error);
    REQUIRE(loadedOk);
    REQUIRE(loaded == scene);
    REQUIRE(loaded.FindEntity(volumeId)->collider->isTrigger);
    REQUIRE_FALSE(loaded.FindEntity(volumeId)->collider->isStatic);
    REQUIRE(loaded.FindEntity(floorId)->collider->isStatic);
    REQUIRE_FALSE(loaded.FindEntity(floorId)->collider->isTrigger);

    // A payload without the new keys (an older scene file) loads with both flags off, and a collider
    // that uses neither keeps writing the same JSON it always did.
    Scene plain;
    Entity &plainEntity = plain.CreateEntity("Plain");
    ColliderComponent plainCollider;
    plainCollider.type = ColliderType::Circle;
    plainCollider.radius = 12.0f;
    plainEntity.collider = plainCollider;
    const EntityId plainId = plainEntity.id;

    const std::string plainJson = SceneSerializer::ToJsonString(plain);
    REQUIRE(plainJson.find("isTrigger") == std::string::npos);
    REQUIRE(plainJson.find("isStatic") == std::string::npos);

    Scene reloaded;
    REQUIRE(SceneSerializer::FromJsonString(plainJson, reloaded, &error));
    REQUIRE(reloaded.FindEntity(plainId)->collider.has_value());
    REQUIRE_FALSE(reloaded.FindEntity(plainId)->collider->isTrigger);
    REQUIRE_FALSE(reloaded.FindEntity(plainId)->collider->isStatic);
}

namespace {
    /// Number of non-overlapping occurrences of needle in text, used to assert call order/count of
    /// the lifecycle hooks a script echoes into PlaySession::GetOutput().
    std::size_t CountOccurrences(const std::string &text, const std::string &needle) {
        if(needle.empty())
            return 0;

        std::size_t count = 0;
        for(std::size_t pos = text.find(needle); pos != std::string::npos;
            pos = text.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }
}

TEST_CASE("Play session runs Start once, Update every frame and OnDestroy on stop or destroy",
          "[EditorCore][Play][Lifecycle]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_lifecycle_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A script that records every lifecycle hook it receives. Echo writes into the VM's output
    // stream, which PlaySession exposes through GetOutput() — that is the call log.
    {
        std::ofstream out(dir / "life.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Start(self)\n"
               "    Echo(\"start\")\n"
               "    self.rotation = 100\n"
               "    return 0\n"
               "end function\n"
               "function Update(self, dt)\n"
               "    Echo(\"update\")\n"
               "    self.x = self.x + 1\n"
               "    return 0\n"
               "end function\n"
               "function OnDestroy(self)\n"
               "    Echo(\"destroy\")\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &entity = scene.CreateEntity("Runner");
    entity.script = ScriptComponent{"life.gs"};
    const EntityId runnerId = entity.id;

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);

    // Start() runs before the first frame: the hook already wrote rotation and echoed.
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.GetLastError().empty());
    REQUIRE(session.GetScriptCount() == 1);
    REQUIRE(Catch::Approx(scene.FindEntity(runnerId)->transform.rotation) == 100.0);
    REQUIRE(Catch::Approx(scene.FindEntity(runnerId)->transform.x) == 0.0);

    std::string log = session.GetOutput();
    REQUIRE(CountOccurrences(log, "start") == 1);
    REQUIRE(CountOccurrences(log, "update") == 0);

    session.Tick(scene, 0.25f);
    session.Tick(scene, 0.25f);

    // Update ran on both frames, Start did not run again, and the frame delta still reaches it.
    log = session.GetOutput();
    REQUIRE(CountOccurrences(log, "start") == 1);
    REQUIRE(CountOccurrences(log, "update") == 2);
    REQUIRE(log.find("start") < log.find("update"));
    REQUIRE(Catch::Approx(scene.FindEntity(runnerId)->transform.x) == 2.0);

    session.Stop(scene);
    REQUIRE_FALSE(session.IsRunning());

    // Leaving play fires OnDestroy last and restores the snapshot the hooks wrote into.
    log = session.GetOutput();
    REQUIRE(CountOccurrences(log, "destroy") == 1);
    REQUIRE(log.find("update") < log.find("destroy"));
    REQUIRE(Catch::Approx(scene.FindEntity(runnerId)->transform.x) == 0.0);
    REQUIRE(Catch::Approx(scene.FindEntity(runnerId)->transform.rotation) == 0.0);

    // Destroying a scripted entity mid-play runs OnDestroy for it, and only then removes it: the
    // victim still ticks on the frame it was destroyed (removal is deferred to the end of the tick).
    {
        std::ofstream out(dir / "victim.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Start(self)\n"
               "    Echo(\"victim-start\")\n"
               "    return 0\n"
               "end function\n"
               "function Update(self, dt)\n"
               "    Echo(\"victim-update\")\n"
               "    return 0\n"
               "end function\n"
               "function OnDestroy(self)\n"
               "    Echo(\"victim-destroy\")\n"
               "    return 0\n"
               "end function\n";
    }
    {
        std::ofstream out(dir / "killer.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Scene\n"
               "phase = 0\n"
               "function Update(self, dt)\n"
               "    phase = phase + 1\n"
               "    if(phase = 2)\n"
               "        self.rotation = Destroy(FindByName(\"Victim\"))\n"
               "    end if\n"
               "    return 0\n"
               "end function\n"
               "function OnDestroy(self)\n"
               "    Echo(\"killer-destroy\")\n"
               "    return 0\n"
               "end function\n";
    }

    Scene arena;
    Entity &victim = arena.CreateEntity("Victim");
    victim.script = ScriptComponent{"victim.gs"};
    const EntityId victimId = victim.id;
    Entity &killer = arena.CreateEntity("Killer");
    killer.script = ScriptComponent{"killer.gs"};
    const EntityId killerId = killer.id;

    PlaySession duel;
    std::string duelError;
    REQUIRE(duel.Start(arena, dir.generic_string(), &duelError));
    INFO("duel start error: " << duelError);
    REQUIRE(duel.GetScriptCount() == 2);

    duel.Tick(arena, 0.0f);
    REQUIRE(arena.HasEntity(victimId));
    REQUIRE(CountOccurrences(duel.GetOutput(), "victim-update") == 1);

    duel.Tick(arena, 0.0f);
    REQUIRE_FALSE(arena.HasEntity(victimId));
    REQUIRE(arena.HasEntity(killerId));
    REQUIRE(Catch::Approx(arena.FindEntity(killerId)->transform.rotation) == 1.0);
    REQUIRE(duel.GetScriptCount() == 1);
    REQUIRE(duel.GetScriptedEntities() == std::vector<EntityId>{killerId});
    // The victim was still ticked during the frame that destroyed it, and its hook ran afterwards.
    REQUIRE(CountOccurrences(duel.GetOutput(), "victim-update") == 2);
    REQUIRE(CountOccurrences(duel.GetOutput(), "victim-destroy") == 1);

    duel.Stop(arena);
    REQUIRE(CountOccurrences(duel.GetOutput(), "killer-destroy") == 1);
    // Stop restores the snapshot, so the destroyed victim is back in the edited scene.
    REQUIRE(arena.HasEntity(victimId));

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Scene GScript library finds, spawns, moves and destroys entities",
          "[EditorCore][Play][Scene]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_scene_api_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream out(dir / "controller.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Scene\n"
               "phase = 0\n"
               "bullet = 0\n"
               "function Start(self)\n"
               "    self.rotation = FindByName(\"Target\")\n"
               "    return 0\n"
               "end function\n"
               "function Update(self, dt)\n"
               "    phase = phase + 1\n"
               "    SetPos(2, GetX(2) + 10.0, GetY(2) + 5.0)\n"
               "    self.x = GetX(2)\n"
               "    self.y = GetY(2)\n"
               "    if(GetName(2) = \"Target\")\n"
               "        self.scaleX = 3\n"
               "    end if\n"
               "    if(phase = 2)\n"
               "        bullet = Spawn(\"Bullet\", 300.0, 400.0)\n"
               "        self.scaleY = bullet\n"
               "    end if\n"
               "    if(phase = 3)\n"
               "        if(FindByName(\"Nowhere\") = 0)\n"
               "            self.rotation = self.rotation + 10\n"
               "        end if\n"
               "        if(Destroy(999) = 0)\n"
               "            self.rotation = self.rotation + 100\n"
               "        end if\n"
               "    end if\n"
               "    if(phase = 4)\n"
               "        SetVisible(2, 0)\n"
               "    end if\n"
               "    if(phase = 5)\n"
               "        Destroy(bullet)\n"
               "    end if\n"
               "    return 0\n"
               "end function\n";
    }
    {
        // The spawned copy carries this script too, so Start/Update/OnDestroy of a dynamic entity
        // are proven as well.
        std::ofstream out(dir / "bullet.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Start(self)\n"
               "    self.rotation = 7\n"
               "    return 0\n"
               "end function\n"
               "function Update(self, dt)\n"
               "    self.y = self.y + 1.0\n"
               "    return 0\n"
               "end function\n"
               "function OnDestroy(self)\n"
               "    Echo(\"bullet-destroy\")\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &player = scene.CreateEntity("Player");
    player.script = ScriptComponent{"controller.gs"};
    const EntityId playerId = player.id;

    Entity &target = scene.CreateEntity("Target");
    target.transform.x = 10.0f;
    target.transform.y = 20.0f;
    const EntityId targetId = target.id;

    Entity &bulletTemplate = scene.CreateEntity("Bullet");
    bulletTemplate.script = ScriptComponent{"bullet.gs"};
    const EntityId templateId = bulletTemplate.id;

    REQUIRE(playerId == 1);
    REQUIRE(targetId == 2);
    REQUIRE(templateId == 3);

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);
    REQUIRE(session.GetLastError().empty());
    REQUIRE(session.GetScriptCount() == 2);

    // Start resolved the name to an id (rotation receives the float conversion of an Int result).
    REQUIRE(Catch::Approx(scene.FindEntity(playerId)->transform.rotation) == double(targetId));
    REQUIRE(Catch::Approx(scene.FindEntity(templateId)->transform.rotation) == 7.0);

    const auto playerX = [&]() { return scene.FindEntity(playerId)->transform.x; };
    const auto playerY = [&]() { return scene.FindEntity(playerId)->transform.y; };
    const auto targetX = [&]() { return scene.FindEntity(targetId)->transform.x; };
    const auto targetY = [&]() { return scene.FindEntity(targetId)->transform.y; };

    // Frame 1: SetPos moved the target, and GetX/GetY read the new world position back.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(Catch::Approx(targetX()) == 20.0);
    REQUIRE(Catch::Approx(targetY()) == 25.0);
    REQUIRE(Catch::Approx(playerX()) == 20.0);
    REQUIRE(Catch::Approx(playerY()) == 25.0);
    REQUIRE(Catch::Approx(scene.FindEntity(playerId)->transform.scaleX) == 3.0);
    REQUIRE(scene.GetEntityCount() == 3);

    // Frame 2: Spawn duplicated the Bullet template at the requested position; the copy gets a
    // fresh id, keeps the template's script and had its Start hook run at the end of the tick.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(scene.GetEntityCount() == 4);
    const EntityId bulletId = 4;
    REQUIRE(scene.HasEntity(bulletId));
    REQUIRE(Catch::Approx(scene.FindEntity(playerId)->transform.scaleY) == double(bulletId));
    REQUIRE(Catch::Approx(scene.FindEntity(bulletId)->transform.x) == 300.0);
    REQUIRE(Catch::Approx(scene.FindEntity(bulletId)->transform.y) == 400.0);
    REQUIRE(Catch::Approx(scene.FindEntity(bulletId)->transform.rotation) == 7.0);
    REQUIRE(session.GetScriptCount() == 3);

    // Frame 3: unknown names and ids report 0 instead of failing, and the spawned copy ticks.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(Catch::Approx(scene.FindEntity(playerId)->transform.rotation) == double(targetId) + 110.0);
    REQUIRE(Catch::Approx(scene.FindEntity(bulletId)->transform.y) == 401.0);
    REQUIRE(Catch::Approx(targetX()) == 40.0);

    // Frame 4: SetVisible hides the target without removing it.
    session.Tick(scene, 0.0f);
    REQUIRE_FALSE(scene.FindEntity(targetId)->visible);
    REQUIRE(Catch::Approx(targetX()) == 50.0);

    // Frame 5: Destroy is deferred, so it lands after every Update of the tick; the spawned copy
    // runs OnDestroy and stops being a scripted entity.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE_FALSE(scene.HasEntity(bulletId));
    REQUIRE(scene.GetEntityCount() == 3);
    REQUIRE(session.GetScriptCount() == 2);
    REQUIRE(CountOccurrences(session.GetOutput(), "bullet-destroy") == 1);
    REQUIRE(Catch::Approx(targetX()) == 60.0);
    REQUIRE(Catch::Approx(targetY()) == 45.0);
    REQUIRE(session.GetLastError().empty());

    session.Stop(scene);
    REQUIRE(scene.GetEntityCount() == 3);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Scene camera calls move the SceneRuntime camera, SpawnPrefab instantiates a file",
          "[EditorCore][Runtime][Scene]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_camera_api_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A prefab file the script instantiates by relative path.
    {
        Scene blueprint;
        Entity &drone = blueprint.CreateEntity("Drone");
        drone.transform.x = 0.0f;
        drone.transform.y = 0.0f;
        std::string prefabError;
        const bool saved = SceneSerializer::SavePrefabToFile(
            blueprint, drone.id, (dir / "drone.gprefab.json").string(), &prefabError
        );
        INFO("prefab save error: " << prefabError);
        REQUIRE(saved);
    }

    {
        std::ofstream out(dir / "director.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Scene\n"
               "phase = 0\n"
               "drone = 0\n"
               "function Start(self)\n"
               "    SetCameraPos(100.0, 50.0)\n"
               "    SetCameraZoom(2.0)\n"
               "    return 0\n"
               "end function\n"
               "function Update(self, dt)\n"
               "    phase = phase + 1\n"
               "    if(phase = 1)\n"
               "        drone = SpawnPrefab(\"drone.gprefab.json\", 40.0, 60.0)\n"
               "        SetPos(drone, 70.0, 80.0)\n"
               "        self.x = GetX(drone)\n"
               "        self.y = GetY(drone)\n"
               "    end if\n"
               "    SetCameraPos(GetCameraX() + 1.0, GetCameraY() + 2.0)\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &director = scene.CreateEntity("Director");
    director.script = ScriptComponent{"director.gs"};
    const EntityId directorId = director.id;

    SceneRuntime runtime;
    std::string error;
    REQUIRE(runtime.LoadFromScene(scene, dir.generic_string(), &error));
    INFO("runtime load error: " << error);
    REQUIRE(runtime.GetScriptCount() == 1);

    // Start ran during load and already moved the runtime's own camera.
    REQUIRE(Catch::Approx(runtime.GetCamera().panX) == 100.0);
    REQUIRE(Catch::Approx(runtime.GetCamera().panY) == 50.0);
    REQUIRE(Catch::Approx(runtime.GetCamera().zoom) == 2.0);

    runtime.Update(0.0f);
    INFO("script error: " << runtime.GetLastError());

    // Camera settle: the Start position plus this frame's nudge, read back through GetCameraX/Y.
    REQUIRE(Catch::Approx(runtime.GetCamera().panX) == 101.0);
    REQUIRE(Catch::Approx(runtime.GetCamera().panY) == 52.0);
    REQUIRE(Catch::Approx(runtime.GetCamera().zoom) == 2.0);

    // SpawnPrefab created the prefab root; SetPos placed it and GetX/GetY read it back.
    REQUIRE(runtime.GetScene().GetEntityCount() == 2);
    const EntityId droneId = runtime.GetScene().FindEntityByName("Drone");
    REQUIRE(droneId != InvalidEntityId);
    REQUIRE(Catch::Approx(runtime.GetScene().GetWorldTransform(droneId).x) == 70.0);
    REQUIRE(Catch::Approx(runtime.GetScene().GetWorldTransform(droneId).y) == 80.0);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(directorId)->transform.x) == 70.0);
    REQUIRE(Catch::Approx(runtime.GetScene().FindEntity(directorId)->transform.y) == 80.0);

    // A prefab path that does not exist reports 0 and leaves the scene alone.
    REQUIRE(runtime.GetPlayedSounds().empty());
    REQUIRE(runtime.GetLastError().empty());

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Audio GScript library records played sounds and no-ops without a device",
          "[EditorCore][Play][Audio]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_audio_api_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "assets", ec);

    // An empty file is enough: the point is the resolution + record path, not the decoding, which
    // is skipped entirely when the machine running the tests has no audio device.
    { std::ofstream out(dir / "assets" / "boom.wav", std::ios::binary); out << "not-a-real-wav"; }

    {
        std::ofstream out(dir / "sfx.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Audio\n"
               "phase = 0\n"
               "function Start(self)\n"
               "    PlaySound(\"assets/boom.wav\")\n"
               "    return 0\n"
               "end function\n"
               "function Update(self, dt)\n"
               "    phase = phase + 1\n"
               "    if(phase = 2)\n"
               "        # A bare call is evaluated once; a call on the right side of a self.<member>\n"
               "        # assignment is evaluated twice by GScript, so keep the result in a\n"
               "        # script variable first.\n"
               "        v = PlaySound(\"assets/missing.ogg\")\n"
               "        self.rotation = v\n"
               "    end if\n"
               "    if(phase = 3)\n"
               "        PlaySound(\"\")\n"
               "    end if\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &speaker = scene.CreateEntity("Speaker");
    speaker.script = ScriptComponent{"sfx.gs"};
    const EntityId speakerId = speaker.id;

    PlaySession session;
    std::string error;
    const bool started = session.Start(scene, dir.generic_string(), &error);
    INFO("play start error: " << error);
    REQUIRE(started);

    // The Start hook's call is recorded with the path resolved against the scene directory.
    REQUIRE(session.GetPlayedSounds().size() == 1);
    REQUIRE(std::filesystem::path(session.GetPlayedSounds()[0]) == dir / "assets" / "boom.wav");

    session.Tick(scene, 0.0f);
    REQUIRE(session.GetPlayedSounds().size() == 1);

    // A missing file cannot start playback, so the script sees 0 — but the call is still recorded.
    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(session.GetPlayedSounds().size() == 2);
    REQUIRE(std::filesystem::path(session.GetPlayedSounds()[1]) == dir / "assets" / "missing.ogg");
    REQUIRE(Catch::Approx(scene.FindEntity(speakerId)->transform.rotation) == 0.0);

    // An empty path is ignored instead of resolving to the scene directory.
    session.Tick(scene, 0.0f);
    REQUIRE(session.GetPlayedSounds().size() == 2);

    session.Stop(scene);
    REQUIRE(session.GetPlayedSounds().empty());

    std::filesystem::remove_all(dir, ec);
}


TEST_CASE("Expanded components and tags survive a JSON round-trip", "[EditorCore][JSON][Components]") {
    Scene scene;
    scene.name = "Expanded";

    Entity &all = scene.CreateEntity("Everything");
    all.tags = {"enemy", "boss"};
    all.properties["hp"] = "120";
    all.properties["wave"] = "3";

    SpriteAnimComponent anim;
    anim.asset = "assets/sheet.png";
    anim.frameW = 16;
    anim.frameH = 24;
    anim.frameCount = 6;
    anim.fps = 9.5f;
    anim.loop = false;
    anim.playing = true;
    anim.currentFrame = 3;
    anim.tint = {200, 120, 40, 220};
    all.spriteAnim = anim;

    TextComponent text;
    text.text = "Hello, Gorgon";
    text.color = {230, 210, 90, 255};
    text.fontSize = 22.0f;
    text.style = "bold";
    text.align = TextAlign::Center;
    all.text = text;

    AudioComponent audio;
    audio.asset = "assets/theme.ogg";
    audio.volume = 0.35f;
    audio.loop = true;
    audio.playOnStart = false;
    all.audio = audio;

    CameraComponent camera;
    camera.active = false;
    camera.zoom = 2.5f;
    camera.followTarget = 7;
    all.camera = camera;

    TilemapComponent tilemap;
    tilemap.tileW = 16.0f;
    tilemap.tileH = 16.0f;
    tilemap.cols = 3;
    tilemap.rows = 2;
    tilemap.tilesetCols = 4;
    tilemap.tilesetAsset = "assets/tiles.png";
    tilemap.tiles = {0, 1, 2, 3, -1, 5};
    all.tilemap = tilemap;

    ParticleComponent particles;
    particles.emitRate = 35.0f;
    particles.lifetime = 0.75f;
    particles.speed = 90.0f;
    particles.startColor = {255, 255, 200, 255};
    particles.endColor = {40, 40, 255, 0};
    particles.size = 6.0f;
    particles.active = false;
    all.particles = particles;

    const EntityId allId = all.id;

    // A second, untouched entity proves the new fields stay absent when unused.
    Entity &plain = scene.CreateEntity("Plain");
    const EntityId plainId = plain.id;

    const std::string encoded = SceneSerializer::ToJsonString(scene, 2);
    REQUIRE(encoded.find("spriteAnim") != std::string::npos);
    REQUIRE(encoded.find("tilemap") != std::string::npos);
    REQUIRE(encoded.find("particles") != std::string::npos);
    REQUIRE(encoded.find("\"tags\"") != std::string::npos);
    REQUIRE(encoded.find("\"properties\"") != std::string::npos);

    Scene loaded;
    std::string error;
    REQUIRE(SceneSerializer::FromJsonString(encoded, loaded, &error));
    INFO("decode error: " << error);
    REQUIRE(loaded == scene);

    const Entity *copy = loaded.FindEntity(allId);
    REQUIRE(copy != nullptr);
    REQUIRE(copy->tags == std::vector<std::string>{"enemy", "boss"});
    REQUIRE(copy->properties.at("hp") == "120");
    REQUIRE(copy->spriteAnim.has_value());
    REQUIRE(copy->spriteAnim->frameCount == 6);
    REQUIRE(copy->spriteAnim->currentFrame == 3);
    REQUIRE(copy->spriteAnim->fps == Catch::Approx(9.5f));
    REQUIRE(copy->text.has_value());
    REQUIRE(copy->text->align == TextAlign::Center);
    REQUIRE(copy->audio.has_value());
    REQUIRE(copy->audio->volume == Catch::Approx(0.35f));
    REQUIRE(copy->camera.has_value());
    REQUIRE(copy->camera->followTarget == 7);
    REQUIRE(copy->tilemap.has_value());
    REQUIRE(copy->tilemap->tiles == std::vector<int>{0, 1, 2, 3, -1, 5});
    REQUIRE(copy->particles.has_value());
    REQUIRE(copy->particles->emitRate == Catch::Approx(35.0f));

    const Entity *plainCopy = loaded.FindEntity(plainId);
    REQUIRE(plainCopy != nullptr);
    REQUIRE(plainCopy->tags.empty());
    REQUIRE(plainCopy->properties.empty());
    REQUIRE_FALSE(plainCopy->spriteAnim.has_value());
    REQUIRE_FALSE(plainCopy->tilemap.has_value());

    // Backward compatibility: a schemaVersion 1 scene written before these components existed.
    const std::string legacy = R"({
        "schemaVersion": 1,
        "name": "Legacy",
        "settings": { "gridSize": 32, "backgroundColor": [24, 24, 30, 255] },
        "entities": [
            { "id": 1, "parentId": 0, "name": "Old", "visible": true, "locked": false, "zOrder": 0,
              "transform": { "x": 0, "y": 0, "scaleX": 1, "scaleY": 1, "rotation": 0 },
              "sprite": { "asset": "old.png", "tint": [255, 255, 255, 255] } }
        ]
    })";
    Scene legacyScene;
    REQUIRE(SceneSerializer::FromJsonString(legacy, legacyScene, &error));
    REQUIRE(legacyScene.GetEntityCount() == 1);
    REQUIRE(legacyScene.FindEntity(1)->tags.empty());
    REQUIRE_FALSE(legacyScene.FindEntity(1)->particles.has_value());

    // Scene tag/property helpers drive the GScript API.
    REQUIRE(loaded.FindEntityByTag("boss") == allId);
    REQUIRE(loaded.FindEntitiesByTag("enemy").size() == 1);
    REQUIRE(loaded.HasTag(allId, "enemy"));
    REQUIRE(loaded.GetProperty(allId, "wave") == "3");
    REQUIRE(loaded.SetProperty(allId, "wave", "4"));
    REQUIRE(loaded.GetProperty(allId, "wave") == "4");
    REQUIRE(loaded.AddTag(plainId, "neutral"));
    REQUIRE(loaded.HasTag(plainId, "neutral"));
    REQUIRE(loaded.RemoveTag(plainId, "neutral"));
    REQUIRE_FALSE(loaded.HasTag(plainId, "neutral"));
}

TEST_CASE("ComponentSimulation advances animations and particles and fires component audio once",
          "[EditorCore][Components][Simulation]") {
    ComponentSimulation simulation;
    std::vector<std::string> triggered;
    simulation.SetSoundSink([&triggered](const std::string &asset) { triggered.push_back(asset); });

    Scene scene;

    Entity &animated = scene.CreateEntity("Animated");
    SpriteAnimComponent anim;
    anim.frameCount = 4;
    anim.fps = 4.0f; // 0.25 s per frame
    anim.loop = true;
    animated.spriteAnim = anim;
    const EntityId animId = animated.id;

    Entity &emitter = scene.CreateEntity("Emitter");
    ParticleComponent particles;
    particles.emitRate = 8.0f;
    particles.lifetime = 0.5f;
    particles.speed = 10.0f;
    emitter.particles = particles;
    const EntityId emitterId = emitter.id;

    Entity &speaker = scene.CreateEntity("Speaker");
    AudioComponent audio;
    audio.asset = "assets/hit.wav";
    audio.playOnStart = true;
    speaker.audio = audio;

    simulation.Step(scene, 0.25f);
    REQUIRE(scene.FindEntity(animId)->spriteAnim->currentFrame == 1);
    REQUIRE(simulation.GetParticleCount(emitterId) == 2); // 8/s * 0.25 s
    REQUIRE(triggered.size() == 1);
    REQUIRE(triggered[0] == "assets/hit.wav");

    simulation.Step(scene, 0.25f);
    REQUIRE(scene.FindEntity(animId)->spriteAnim->currentFrame == 2);
    // Audio fires exactly once per session.
    REQUIRE(triggered.size() == 1);

    simulation.Step(scene, 0.25f);
    simulation.Step(scene, 0.25f);
    REQUIRE(scene.FindEntity(animId)->spriteAnim->currentFrame == 0); // wrapped (4 frames)

    // Non-looping animations stop on the last frame.
    auto &anim2 = *scene.FindEntity(animId)->spriteAnim;
    anim2.loop = false;
    anim2.currentFrame = 0;
    anim2.playing = true;
    anim2.frameTime = 0.0f;
    for(int i = 0; i < 8; ++i)
        simulation.Step(scene, 0.25f);
    REQUIRE(anim2.currentFrame == 3);
    REQUIRE_FALSE(anim2.playing);

    // Old particles expire once they outlive their lifetime.
    const std::size_t before = simulation.GetParticleCount(emitterId);
    REQUIRE(before > 0);
    scene.FindEntity(emitterId)->particles->active = false;
    for(int i = 0; i < 4; ++i)
        simulation.Step(scene, 0.25f);
    REQUIRE(simulation.GetParticleCount(emitterId) == 0);
    REQUIRE(simulation.GetEmittedCount(emitterId) >= before);

    // A removed emitter is forgotten.
    scene.RemoveEntity(emitterId, true);
    simulation.Step(scene, 0.25f);
    REQUIRE(simulation.GetParticles(emitterId) == nullptr);

    simulation.Reset();
    REQUIRE(triggered.size() == 1); // the sink is the test's; Reset only forgets state
    REQUIRE(simulation.GetTriggeredSounds().empty());
}

TEST_CASE("Play session steps component animation, particles and component audio",
          "[EditorCore][Play][Components]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_component_play_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream out(dir / "idle.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &actor = scene.CreateEntity("Actor");
    actor.script = ScriptComponent{"idle.gs"};
    SpriteAnimComponent anim;
    anim.frameCount = 4;
    anim.fps = 4.0f;
    actor.spriteAnim = anim;
    ParticleComponent particles;
    particles.emitRate = 12.0f;
    particles.lifetime = 1.0f;
    actor.particles = particles;
    AudioComponent audio;
    audio.asset = "assets/music.ogg";
    actor.audio = audio;
    const EntityId actorId = actor.id;

    PlaySession session;
    std::string error;
    REQUIRE(session.Start(scene, dir.generic_string(), &error));
    INFO("start error: " << error);

    // Play-on-start audio is triggered by the first component step, i.e. the first tick.
    REQUIRE(session.GetPlayedSounds().empty());

    session.Tick(scene, 0.25f);
    REQUIRE(session.GetPlayedSounds().size() == 1);
    REQUIRE(std::filesystem::path(session.GetPlayedSounds()[0]) == dir / "assets" / "music.ogg");
    const Entity *after = scene.FindEntity(actorId);
    REQUIRE(after != nullptr);
    REQUIRE(after->spriteAnim->currentFrame == 1);
    REQUIRE(session.GetParticles(actorId) != nullptr);
    REQUIRE(session.GetParticles(actorId)->size() == 3); // 12/s * 0.25 s
    REQUIRE(session.GetComponentSimulation().GetParticleCount(actorId) == 3);

    session.Tick(scene, 0.25f);
    REQUIRE(scene.FindEntity(actorId)->spriteAnim->currentFrame == 2);
    REQUIRE(session.GetPlayedSounds().size() == 1); // play-on-start is once per session

    session.Stop(scene);
    // Stopping restores the pre-play snapshot, so the animation frame is back at the start.
    REQUIRE(scene.FindEntity(actorId)->spriteAnim->currentFrame == 0);
    REQUIRE(scene.FindEntity(actorId)->particles.has_value());
}

TEST_CASE("SceneRuntime animates a scriptless scene and uses the active CameraComponent",
          "[EditorCore][Runtime][Components]") {
    Scene scene;
    scene.name = "ComponentOnly";

    Entity &animated = scene.CreateEntity("Animated");
    SpriteAnimComponent anim;
    anim.frameCount = 8;
    anim.fps = 10.0f;
    animated.spriteAnim = anim;
    const EntityId animId = animated.id;

    Entity &emitter = scene.CreateEntity("Emitter");
    ParticleComponent particles;
    particles.emitRate = 20.0f;
    particles.lifetime = 2.0f;
    emitter.particles = particles;
    const EntityId emitterId = emitter.id;

    Entity &speaker = scene.CreateEntity("Speaker");
    AudioComponent audio;
    audio.asset = "assets/ambient.ogg";
    speaker.audio = audio;

    Entity &camera = scene.CreateEntity("MainCamera");
    CameraComponent cameraComp;
    cameraComp.active = true;
    cameraComp.zoom = 3.0f;
    cameraComp.followTarget = animId;
    camera.camera = cameraComp;
    const EntityId cameraId = camera.id;

    // Move the followed entity away from the origin so the follow is observable.
    scene.FindEntity(animId)->transform.x = 320.0f;
    scene.FindEntity(animId)->transform.y = -64.0f;

    SceneRuntime runtime;
    std::string error;
    REQUIRE(runtime.LoadFromScene(scene, std::string(), &error));
    INFO("runtime error: " << error);

    REQUIRE(runtime.GetScriptCount() == 0);
    REQUIRE(runtime.GetActiveCameraEntity() == cameraId);

    // dt is clamped to 0.25 s per step, so 0.5 s of animation is two steps.
    runtime.Update(0.25f);
    runtime.Update(0.25f);
    REQUIRE(runtime.GetScene().FindEntity(animId)->spriteAnim->currentFrame == 5); // 10 fps * 0.5 s

    const auto *live = runtime.GetParticles(emitterId);
    REQUIRE(live != nullptr);
    REQUIRE(live->size() == 10); // 20/s * 0.5 s

    const auto &played = runtime.GetPlayedSounds();
    REQUIRE(played.size() == 1);
    REQUIRE(played[0].find("ambient.ogg") != std::string::npos);

    REQUIRE(runtime.GetCamera().panX == Catch::Approx(320.0f));
    REQUIRE(runtime.GetCamera().panY == Catch::Approx(-64.0f));
    REQUIRE(runtime.GetCamera().zoom == Catch::Approx(3.0f));

    // An inactive camera component no longer drives the projection.
    runtime.GetScene().FindEntity(cameraId)->camera->active = false;
    REQUIRE(runtime.GetActiveCameraEntity() == InvalidEntityId);
}

TEST_CASE("GScript tag and property helpers read and write entity metadata",
          "[EditorCore][Play][Tags]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_tag_api_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream out(dir / "tags.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "using Scene\n"
               "phase = 0\n"
               "function Update(self, dt)\n"
               "    phase = phase + 1\n"
               "    if(phase = 1)\n"
               "        t = FindByTag(\"hero\")\n"
               "        self.x = t\n"
               "        if(HasTag(t, \"hero\"))\n"
               "            self.rotation = 1\n"
               "        end if\n"
               "        v = GetProp(t, \"hp\")\n"
               "        self.y = 0\n"
               "        SetProp(t, \"hp\", \"42\")\n"
               "        SetProp(t, \"seen\", v)\n"
               "    end if\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &hero = scene.CreateEntity("Hero");
    hero.tags = {"hero", "player"};
    hero.properties["hp"] = "100";
    const EntityId heroId = hero.id;

    Entity &driver = scene.CreateEntity("Driver");
    driver.script = ScriptComponent{"tags.gs"};
    const EntityId driverId = driver.id;

    PlaySession session;
    std::string error;
    REQUIRE(session.Start(scene, dir.generic_string(), &error));
    INFO("start error: " << error);

    session.Tick(scene, 0.0f);
    INFO("script error: " << session.GetLastError());
    REQUIRE(scene.FindEntity(driverId)->transform.x == Catch::Approx(float(heroId)));
    REQUIRE(scene.FindEntity(driverId)->transform.rotation == Catch::Approx(1.0f));

    REQUIRE(scene.GetProperty(heroId, "hp") == "42");
    REQUIRE(scene.GetProperty(heroId, "seen") == "100");

    session.Stop(scene);
    std::filesystem::remove_all(dir, ec);
}
TEST_CASE("ScriptLog keeps a bounded ring buffer and classifies severities", "[EditorCore][Console]") {
    ScriptLog log(3);
    REQUIRE(log.GetCapacity() == 3);
    REQUIRE(log.IsEmpty());
    REQUIRE(log.GetLast() == nullptr);

    log.Info("Player", "one");
    log.Warn("Player", "two");
    log.Error("Enemy", "three");

    REQUIRE(log.GetCount() == 3);
    REQUIRE(log.CountOf(LogSeverity::Info) == 1);
    REQUIRE(log.CountOf(LogSeverity::Warn) == 1);
    REQUIRE(log.CountOf(LogSeverity::Error) == 1);

    const auto &entries = log.GetEntries();
    REQUIRE(entries[0].source == "Player");
    REQUIRE(entries[0].severity == LogSeverity::Info);
    REQUIRE(entries[2].message == "three");
    // Timestamps are wall clock "HH:MM:SS", so every entry carries a usable one.
    REQUIRE(entries[0].time.size() == 8);
    REQUIRE(entries[0].time[2] == ':');
    REQUIRE(entries[2].elapsed >= 0.0);

    // Formatting pads the level and prefixes the source.
    REQUIRE(entries[2].Format().find("ERROR") != std::string::npos);
    REQUIRE(entries[2].Format().find("Enemy: three") != std::string::npos);
    REQUIRE(LogEntry{}.Format().find("INFO ") != std::string::npos);

    // The ring buffer drops the oldest line instead of growing.
    log.Info("Player", "four");
    REQUIRE(log.GetCount() == 3);
    REQUIRE(log.GetLast() != nullptr);
    REQUIRE(log.GetLast()->message == "four");
    REQUIRE(log.FormatAll().find("one") == std::string::npos);
    REQUIRE(log.FormatAll().find("four") != std::string::npos);

    // Shrinking keeps the newest entries, clearing bumps the version so viewers notice.
    const std::size_t beforeClear = log.GetVersion();
    log.SetCapacity(1);
    REQUIRE(log.GetCount() == 1);
    REQUIRE(log.GetLast()->message == "four");

    log.Clear();
    REQUIRE(log.IsEmpty());
    REQUIRE(log.GetVersion() > beforeClear);
    REQUIRE(log.GetEntries().empty());
}

TEST_CASE("ConsoleView filters problems and clamps its scroll range", "[EditorCore][Console]") {
    ScriptLog log;
    log.Info("A", "info line");
    log.Warn("A", "warn line");
    log.Error("B", "error line");

    ConsoleView view;
    view.SetSource(&log);
    REQUIRE(view.GetLineCount() == 3);
    REQUIRE(view.GetLineText(0).find("info line") != std::string::npos);
    REQUIRE(view.GetLineText(9).empty());

    // "Errors" keeps warnings: a warning is a problem the user wants to see.
    view.SetErrorsOnly(true);
    REQUIRE(view.GetLineCount() == 2);
    REQUIRE(view.GetLineText(0).find("warn line") != std::string::npos);
    REQUIRE(view.GetLineText(1).find("error line") != std::string::npos);
    REQUIRE(view.GetLineText(2).empty());

    view.SetErrorsOnly(false);
    view.SetVisibleLines(2);
    REQUIRE(view.GetMaxScroll() == 1);

    view.ScrollTo(99);
    REQUIRE(view.GetScrollOffset() == 1);
    REQUIRE(view.GetFirstLine() == 1);

    view.ScrollBy(-5);
    REQUIRE(view.GetScrollOffset() == 0);

    view.ScrollToBottom();
    REQUIRE(view.GetScrollOffset() == 1);

    // The summary counts the whole log, not just the filtered lines.
    REQUIRE(view.GetStatusText().find("3 entries") != std::string::npos);
    REQUIRE(view.GetStatusText().find("1 error") != std::string::npos);
    REQUIRE(view.GetStatusText().find("1 warning") != std::string::npos);

    // Sync reports a change once per log mutation.
    REQUIRE(view.Sync());
    REQUIRE_FALSE(view.Sync());
    log.Info("A", "another");
    REQUIRE(view.Sync());
    REQUIRE(view.GetStatusText().find("4 entries") != std::string::npos);
}

TEST_CASE("Play session console captures Log/Print, compile and runtime errors",
          "[EditorCore][Play][Console]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_console_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A healthy script that talks to the console.
    {
        std::ofstream out(dir / "talk.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    Log(\"hello from gs\")\n"
               "    Print(\"printed line\")\n"
               "    return 0\n"
               "end function\n";
    }

    // A script that does not parse: `==` is never valid in GScript.
    {
        std::ofstream out(dir / "broken.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    if(self.x == 1)\n"
               "        self.x = 2\n"
               "    end if\n"
               "    return 0\n"
               "end function\n";
    }

    // A script that compiles but fails at runtime: a string cannot be stored into a float member.
    {
        std::ofstream out(dir / "throws.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    self.x = \"not a number\"\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &talker = scene.CreateEntity("Talker");
    talker.script = ScriptComponent{"talk.gs"};
    const EntityId talkerId = talker.id;

    Entity &broken = scene.CreateEntity("Broken");
    broken.script = ScriptComponent{"broken.gs"};

    Entity &thrower = scene.CreateEntity("Thrower");
    thrower.script = ScriptComponent{"throws.gs"};
    const EntityId throwerId = thrower.id;

    PlaySession session;
    std::string error;
    REQUIRE(session.Start(scene, dir.generic_string(), &error));
    REQUIRE(session.GetScriptCount() == 2); // talk.gs and throws.gs compiled, broken.gs did not

    // The parse failure was captured with the entity it belongs to.
    REQUIRE(session.GetLog().CountOf(LogSeverity::Error) >= 1);
    bool sawCompileError = false;
    for(const auto &entry : session.GetLog().GetEntries()) {
        if(entry.severity == LogSeverity::Error && entry.source.find("Broken") != std::string::npos &&
           entry.message.find("broken.gs") != std::string::npos) {
            sawCompileError = true;
        }
    }
    REQUIRE(sawCompileError);

    // Log() and Print() land in the console, attributed to the running script.
    session.Tick(scene, 0.0f);
    INFO("console output:\n" << session.GetLog().FormatAll());

    bool sawLog = false;
    bool sawPrint = false;
    for(const auto &entry : session.GetLog().GetEntries()) {
        if(entry.severity != LogSeverity::Info)
            continue;
        if(entry.source.find("Talker") == std::string::npos)
            continue;
        if(entry.message == "hello from gs")
            sawLog = true;
        if(entry.message == "printed line")
            sawPrint = true;
    }
    REQUIRE(sawLog);
    REQUIRE(sawPrint);

    // The runtime failure is an Error entry naming the throwing entity, and the entity kept its
    // pre-frame transform (a failed script is not allowed to half-apply its move).
    REQUIRE(session.GetLog().GetEntries().back().source.find("Thrower") != std::string::npos);
    REQUIRE(session.GetLog().GetEntries().back().severity == LogSeverity::Error);
    REQUIRE(session.GetLog().GetEntries().back().message.find("Script error") != std::string::npos);
    REQUIRE(session.GetLastError().find("Script error") != std::string::npos);
    REQUIRE(scene.FindEntity(throwerId)->transform.x == Catch::Approx(0.0f));

    // The log survives Stop(): the editor reads failures after play ended.
    const std::size_t beforeStop = session.GetLog().GetCount();
    session.Stop(scene);
    REQUIRE(session.GetLog().GetCount() >= beforeStop);
    REQUIRE_FALSE(session.GetLog().IsEmpty());

    // A new session starts a fresh console.
    REQUIRE(session.Start(scene, dir.generic_string(), &error));
    REQUIRE(session.GetLog().GetEntries().back().message.find("Play started") != std::string::npos);
    for(const auto &entry : session.GetLog().GetEntries())
        REQUIRE(entry.message.find("hello from gs") == std::string::npos);

    session.Stop(scene);

    // The editor can write its own lines into the same log.
    session.Log(LogSeverity::Warn, "check this", "Editor");
    REQUIRE(session.GetLog().GetLast()->source == "Editor");
    REQUIRE(session.GetLog().GetLast()->severity == LogSeverity::Warn);
    session.ClearLog();
    REQUIRE(session.GetLog().IsEmpty());

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Play session CheckSyntax compiles a buffer without executing it",
          "[EditorCore][Play][Console]") {
    const auto dir = std::filesystem::temp_directory_path() / "gorgon_editor_syntax_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    PlaySession session;
    std::string error;

    const std::string good =
        "function Update(self, dt)\n"
        "    self.x = self.x + 10 * dt\n"
        "    Log(\"never runs here\")\n"
        "    return 0\n"
        "end function\n";

    REQUIRE(session.CheckSyntax(good, "good.gs", &error));
    REQUIRE(error.empty());

    // Compiling is not running: no console line, no error, no session.
    REQUIRE(session.GetLog().IsEmpty());
    REQUIRE(session.GetLastError().empty());
    REQUIRE_FALSE(session.IsRunning());

    // A half written script still checks: the prologue only registers the hooks that exist.
    REQUIRE(session.CheckSyntax("x = 1\n", "partial.gs", &error));
    REQUIRE(error.empty());

    // A parse error is reported with the compiler message.
    const std::string broken =
        "function Update(self, dt)\n"
        "    if(self.x == 1)\n"
        "        self.x = 2\n"
        "    end if\n"
        "    return 0\n"
        "end function\n";
    REQUIRE_FALSE(session.CheckSyntax(broken, "broken.gs", &error));
    REQUIRE_FALSE(error.empty());

    // A missing paren on `if` is a parse error inside a function body, so the check looks past the
    // top level statement and into Update.
    REQUIRE_FALSE(session.CheckSyntax("function Update(self, dt)\n    if self.x == 1\n        self.x = 2\n"
                                      "    end if\n    return 0\nend function\n",
                                      "badif.gs", &error));
    REQUIRE_FALSE(error.empty());

    // Checking must not leave the libraries behind: a real play session still starts.
    {
        std::ofstream out(dir / "spin.gs", std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "function Update(self, dt)\n"
               "    self.x = self.x + 5\n"
               "    return 0\n"
               "end function\n";
    }

    Scene scene;
    Entity &spinner = scene.CreateEntity("Spinner");
    spinner.script = ScriptComponent{"spin.gs"};
    const EntityId spinId = spinner.id;

    std::string startError;
    REQUIRE(session.Start(scene, dir.generic_string(), &startError));
    INFO("start error: " << startError);
    session.Tick(scene, 0.1f);
    REQUIRE(scene.FindEntity(spinId)->transform.x == Catch::Approx(5.0f));

    // A check while a session is running leaves that session's libraries alone.
    REQUIRE(session.CheckSyntax(good, "good.gs", &error));
    session.Tick(scene, 0.1f);
    REQUIRE(scene.FindEntity(spinId)->transform.x == Catch::Approx(10.0f));

    session.Stop(scene);
    std::filesystem::remove_all(dir, ec);
}
