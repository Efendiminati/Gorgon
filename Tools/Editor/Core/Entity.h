#pragma once

#include "Components.h"

#include <optional>
#include <string>

namespace Gorgon :: Editor {

    struct Entity {
        EntityId id = InvalidEntityId;
        EntityId parentId = InvalidEntityId;
        std::string name;
        bool visible = true;
        bool locked = false;
        int zOrder = 0;
        Transform transform;

        std::optional<SpriteComponent> sprite;
        std::optional<ColliderComponent> collider;
        std::optional<ScriptComponent> script;

        bool operator==(const Entity &other) const {
            return id == other.id &&
                   parentId == other.parentId &&
                   name == other.name &&
                   visible == other.visible &&
                   locked == other.locked &&
                   zOrder == other.zOrder &&
                   transform == other.transform &&
                   sprite == other.sprite &&
                   collider == other.collider &&
                   script == other.script;
        }

        bool operator!=(const Entity &other) const {
            return !(*this == other);
        }
    };

} // namespace Gorgon::Editor
