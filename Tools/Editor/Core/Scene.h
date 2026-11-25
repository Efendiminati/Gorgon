#pragma once

#include "Entity.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gorgon :: Editor {

    struct SceneSettings {
        int gridSize = 32;
        ColorRGBA backgroundColor{24, 24, 30, 255};

        bool operator==(const SceneSettings &other) const {
            return gridSize == other.gridSize && backgroundColor == other.backgroundColor;
        }
        bool operator!=(const SceneSettings &other) const {
            return !(*this == other);
        }
    };

    class Scene {
    public:
        static constexpr int CurrentSchemaVersion = 1;

        std::string name = "Untitled";
        SceneSettings settings;

        /// Creates a new entity with an auto-generated unique ID.
        /// If parentId is non-zero and does not exist, the entity is created at root (parentId = 0).
        Entity &CreateEntity(const std::string &entityName = "Entity", EntityId parentId = InvalidEntityId);

        /// Inserts an entity with an explicit ID (used for deserialization and undo/redo).
        /// Returns nullptr if id is InvalidEntityId or already exists.
        Entity *AddEntityWithId(Entity entity);

        /// Removes an entity by ID. When recursive is true, descendants are removed as well.
        /// When recursive is false, direct children are reparented to the removed entity's parent.
        bool RemoveEntity(EntityId id, bool recursive = true);

        /// Duplicates an entity (and optionally its subtree) with freshly allocated IDs.
        /// Returns the new root duplicate's ID, or InvalidEntityId if source id does not exist.
        EntityId DuplicateEntity(EntityId id, bool recursive = true);

        Entity *FindEntity(EntityId id);
        const Entity *FindEntity(EntityId id) const;

        /// Returns the id of the first entity (storage order) whose name matches exactly, or
        /// InvalidEntityId when no entity carries that name.
        EntityId FindEntityByName(const std::string &entityName) const;

        bool HasEntity(EntityId id) const;

        const std::vector<Entity> &GetEntities() const { return entities_; }
        std::size_t GetEntityCount() const { return entities_.size(); }

        void Clear();

        /// Returns IDs of all entities with parentId == InvalidEntityId in storage order.
        std::vector<EntityId> GetRootEntityIds() const;

        /// Returns IDs of all direct children of parentId in storage order.
        std::vector<EntityId> GetChildIds(EntityId parentId) const;

        /// Returns true if ancestorId is a strict ancestor of descendantId.
        bool IsAncestorOf(EntityId ancestorId, EntityId descendantId) const;

        /// Returns true if entityId can be safely reparented to newParentId without creating a cycle.
        bool CanReparent(EntityId entityId, EntityId newParentId) const;

        /// Reparents entityId under newParentId (or root when newParentId == InvalidEntityId).
        /// Returns false if entityId is missing, newParentId is invalid, or a cycle would be created.
        bool Reparent(EntityId entityId, EntityId newParentId);

        /// Computes the composed world transform by walking the parent chain.
        Transform GetWorldTransform(EntityId id) const;

        /// Returns all entity IDs sorted stably by zOrder ascending.
        std::vector<EntityId> GetRenderOrder() const;

        /// Returns the next entity ID that will be allocated.
        EntityId GetNextId() const { return nextId_; }

        bool operator==(const Scene &other) const;
        bool operator!=(const Scene &other) const { return !(*this == other); }

    private:
        void rebuildIndex();
        void collectSubtree(EntityId rootId, std::vector<EntityId> &out) const;

        std::vector<Entity> entities_;
        std::unordered_map<EntityId, std::size_t> indexById_;
        EntityId nextId_ = 1;
    };

} // namespace Gorgon::Editor
