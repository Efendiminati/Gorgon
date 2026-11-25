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
    REQUIRE(session.Start(scene, dir.generic_string(), &error));
    INFO("play start error: " << error);
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
