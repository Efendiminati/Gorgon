#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Core/Scene.h"
#include "Core/SceneSerializer.h"

#include <filesystem>
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
