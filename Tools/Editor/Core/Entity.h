#pragma once

#include "Components.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    struct Entity {
        EntityId id = InvalidEntityId;
        EntityId parentId = InvalidEntityId;
        std::string name;
        bool visible = true;
        bool locked = false;
        int zOrder = 0;
        Transform transform;

        /// Free-form labels used for grouping and GScript lookups (Scene.FindByTag).
        std::vector<std::string> tags;
        /// Free-form string key/value pairs a script can read and write (Scene.GetProp/SetProp).
        std::map<std::string, std::string> properties;

        std::optional<SpriteComponent> sprite;
        std::optional<ColliderComponent> collider;
        std::optional<ScriptComponent> script;
        std::optional<SpriteAnimComponent> spriteAnim;
        std::optional<TextComponent> text;
        std::optional<AudioComponent> audio;
        std::optional<CameraComponent> camera;
        std::optional<TilemapComponent> tilemap;
        std::optional<ParticleComponent> particles;
        std::optional<Light2DComponent> light;
        std::string prefabSource;

        bool HasTag(const std::string &tag) const {
            for(const auto &existing : tags) {
                if(existing == tag)
                    return true;
            }
            return false;
        }

        bool operator==(const Entity &other) const {
            return id == other.id &&
                   parentId == other.parentId &&
                   name == other.name &&
                   visible == other.visible &&
                   locked == other.locked &&
                   zOrder == other.zOrder &&
                   transform == other.transform &&
                   tags == other.tags &&
                   properties == other.properties &&
                   sprite == other.sprite &&
                   collider == other.collider &&
                   script == other.script &&
                   spriteAnim == other.spriteAnim &&
                   text == other.text &&
                   audio == other.audio &&
                   camera == other.camera &&
                   tilemap == other.tilemap &&
                   particles == other.particles &&
                   light == other.light &&
                   prefabSource == other.prefabSource;
        }

        bool operator!=(const Entity &other) const {
            return !(*this == other);
        }
    };

} // namespace Gorgon::Editor
