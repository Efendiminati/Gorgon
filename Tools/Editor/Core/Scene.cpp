#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Gorgon :: Editor {

    Entity &Scene::CreateEntity(const std::string &entityName, EntityId parentId) {
        Entity e;
        e.id = nextId_++;
        e.parentId = (parentId != InvalidEntityId && HasEntity(parentId)) ? parentId : InvalidEntityId;
        e.name = entityName;
        indexById_[e.id] = entities_.size();
        entities_.push_back(std::move(e));
        return entities_.back();
    }

    Entity *Scene::AddEntityWithId(Entity entity) {
        if(entity.id == InvalidEntityId || HasEntity(entity.id)) {
            return nullptr;
        }
        if(entity.id >= nextId_) {
            nextId_ = entity.id + 1;
        }
        indexById_[entity.id] = entities_.size();
        entities_.push_back(std::move(entity));
        return &entities_.back();
    }

    void Scene::collectSubtree(EntityId rootId, std::vector<EntityId> &out) const {
        out.push_back(rootId);
        for(const auto &e : entities_) {
            if(e.parentId == rootId) {
                collectSubtree(e.id, out);
            }
        }
    }

    bool Scene::RemoveEntity(EntityId id, bool recursive) {
        auto it = indexById_.find(id);
        if(it == indexById_.end()) {
            return false;
        }

        if(recursive) {
            std::vector<EntityId> toRemove;
            collectSubtree(id, toRemove);
            std::unordered_set<EntityId> removeSet(toRemove.begin(), toRemove.end());

            entities_.erase(
                std::remove_if(entities_.begin(), entities_.end(),
                    [&](const Entity &e) { return removeSet.count(e.id) > 0; }),
                entities_.end()
            );
        } else {
            const EntityId grandParent = entities_[it->second].parentId;
            for(auto &e : entities_) {
                if(e.parentId == id) {
                    e.parentId = grandParent;
                }
            }
            entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(it->second));
        }

        rebuildIndex();
        return true;
    }

    EntityId Scene::DuplicateEntity(EntityId id, bool recursive) {
        const Entity *src = FindEntity(id);
        if(!src) {
            return InvalidEntityId;
        }

        if(!recursive) {
            Entity copy = *src;
            copy.id = nextId_++;
            copy.name += " Copy";
            indexById_[copy.id] = entities_.size();
            entities_.push_back(std::move(copy));
            return entities_.back().id;
        }

        std::vector<EntityId> subtree;
        collectSubtree(id, subtree);

        std::unordered_map<EntityId, EntityId> idMap;
        idMap.reserve(subtree.size());
        for(EntityId oldId : subtree) {
            idMap[oldId] = nextId_++;
        }

        std::vector<Entity> copies;
        copies.reserve(subtree.size());
        for(EntityId oldId : subtree) {
            const Entity *orig = FindEntity(oldId);
            if(!orig) continue;
            Entity copy = *orig;
            copy.id = idMap[oldId];
            if(oldId == id) {
                copy.name += " Copy";
                // Keep original parent for the duplicated root
            } else {
                auto pit = idMap.find(orig->parentId);
                copy.parentId = (pit != idMap.end()) ? pit->second : InvalidEntityId;
            }
            copies.push_back(std::move(copy));
        }

        for(auto &c : copies) {
            indexById_[c.id] = entities_.size();
            entities_.push_back(std::move(c));
        }

        return idMap[id];
    }

    Entity *Scene::FindEntity(EntityId id) {
        auto it = indexById_.find(id);
        if(it == indexById_.end()) {
            return nullptr;
        }
        return &entities_[it->second];
    }

    const Entity *Scene::FindEntity(EntityId id) const {
        auto it = indexById_.find(id);
        if(it == indexById_.end()) {
            return nullptr;
        }
        return &entities_[it->second];
    }

    EntityId Scene::FindEntityByName(const std::string &entityName) const {
        if(entityName.empty())
            return InvalidEntityId;

        for(const auto &e : entities_) {
            if(e.name == entityName)
                return e.id;
        }
        return InvalidEntityId;
    }

    bool Scene::HasEntity(EntityId id) const {
        return indexById_.find(id) != indexById_.end();
    }

    void Scene::Clear() {
        entities_.clear();
        indexById_.clear();
        nextId_ = 1;
    }

    std::vector<EntityId> Scene::GetRootEntityIds() const {
        std::vector<EntityId> roots;
        for(const auto &e : entities_) {
            if(e.parentId == InvalidEntityId) {
                roots.push_back(e.id);
            }
        }
        return roots;
    }

    std::vector<EntityId> Scene::GetChildIds(EntityId parentId) const {
        std::vector<EntityId> children;
        for(const auto &e : entities_) {
            if(e.parentId == parentId) {
                children.push_back(e.id);
            }
        }
        return children;
    }

    bool Scene::IsAncestorOf(EntityId ancestorId, EntityId descendantId) const {
        if(ancestorId == InvalidEntityId || descendantId == InvalidEntityId) {
            return false;
        }
        EntityId cur = descendantId;
        std::size_t guard = 0;
        const std::size_t maxDepth = entities_.size() + 1;
        while(cur != InvalidEntityId && guard++ < maxDepth) {
            const Entity *e = FindEntity(cur);
            if(!e) {
                return false;
            }
            if(e->parentId == ancestorId) {
                return true;
            }
            cur = e->parentId;
        }
        return false;
    }

    bool Scene::CanReparent(EntityId entityId, EntityId newParentId) const {
        if(!HasEntity(entityId)) {
            return false;
        }
        if(newParentId == InvalidEntityId) {
            return true;
        }
        if(entityId == newParentId) {
            return false;
        }
        if(!HasEntity(newParentId)) {
            return false;
        }
        if(IsAncestorOf(entityId, newParentId)) {
            return false;
        }
        return true;
    }

    bool Scene::Reparent(EntityId entityId, EntityId newParentId) {
        if(!CanReparent(entityId, newParentId)) {
            return false;
        }
        Entity *e = FindEntity(entityId);
        e->parentId = newParentId;
        return true;
    }

    Transform Scene::GetWorldTransform(EntityId id) const {
        const Entity *e = FindEntity(id);
        if(!e) {
            return Transform{};
        }

        std::vector<const Entity *> chain;
        std::size_t guard = 0;
        const std::size_t maxDepth = entities_.size() + 1;
        const Entity *cur = e;
        while(cur && guard++ < maxDepth) {
            chain.push_back(cur);
            cur = (cur->parentId != InvalidEntityId) ? FindEntity(cur->parentId) : nullptr;
        }

        Transform world;
        world.x = 0.0f;
        world.y = 0.0f;
        world.scaleX = 1.0f;
        world.scaleY = 1.0f;
        world.rotation = 0.0f;

        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        for(auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const Transform &local = (*it)->transform;
            const float rad = world.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);
            const float sx = local.x * world.scaleX;
            const float sy = local.y * world.scaleY;
            world.x += sx * c - sy * s;
            world.y += sx * s + sy * c;
            world.scaleX *= local.scaleX;
            world.scaleY *= local.scaleY;
            world.rotation += local.rotation;
        }
        return world;
    }

    std::vector<EntityId> Scene::GetRenderOrder() const {
        std::vector<EntityId> order;
        order.reserve(entities_.size());
        for(const auto &e : entities_) {
            order.push_back(e.id);
        }
        std::stable_sort(order.begin(), order.end(), [this](EntityId a, EntityId b) {
            return FindEntity(a)->zOrder < FindEntity(b)->zOrder;
        });
        return order;
    }

    bool Scene::operator==(const Scene &other) const {
        return name == other.name &&
               settings == other.settings &&
               entities_ == other.entities_;
    }

    void Scene::rebuildIndex() {
        indexById_.clear();
        indexById_.reserve(entities_.size());
        for(std::size_t i = 0; i < entities_.size(); ++i) {
            indexById_[entities_[i].id] = i;
        }
    }

} // namespace Gorgon::Editor
