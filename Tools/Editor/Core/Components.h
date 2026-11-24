#pragma once

#include <cstdint>
#include <string>

namespace Gorgon :: Editor {

    using EntityId = std::uint32_t;
    inline constexpr EntityId InvalidEntityId = 0;

    struct ColorRGBA {
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;

        bool operator==(const ColorRGBA &other) const {
            return r == other.r && g == other.g && b == other.b && a == other.a;
        }
        bool operator!=(const ColorRGBA &other) const {
            return !(*this == other);
        }
    };

    struct Transform {
        float x = 0.0f;
        float y = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float rotation = 0.0f; // degrees

        bool operator==(const Transform &other) const {
            return x == other.x && y == other.y &&
                   scaleX == other.scaleX && scaleY == other.scaleY &&
                   rotation == other.rotation;
        }
        bool operator!=(const Transform &other) const {
            return !(*this == other);
        }
    };

    struct SpriteComponent {
        std::string asset;
        ColorRGBA tint{255, 255, 255, 255};

        bool operator==(const SpriteComponent &other) const {
            return asset == other.asset && tint == other.tint;
        }
        bool operator!=(const SpriteComponent &other) const {
            return !(*this == other);
        }
    };

    enum class ColliderType {
        None = 0,
        AABB,
        Circle
    };

    inline std::string ColliderTypeToString(ColliderType type) {
        switch(type) {
            case ColliderType::AABB:   return "aabb";
            case ColliderType::Circle: return "circle";
            case ColliderType::None:
            default:                   return "none";
        }
    }

    inline bool ColliderTypeFromString(const std::string &str, ColliderType &out) {
        if(str == "none")   { out = ColliderType::None;   return true; }
        if(str == "aabb")   { out = ColliderType::AABB;   return true; }
        if(str == "circle") { out = ColliderType::Circle; return true; }
        return false;
    }

    struct ColliderComponent {
        ColliderType type = ColliderType::None;
        float width = 32.0f;
        float height = 32.0f;
        float radius = 16.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;

        bool operator==(const ColliderComponent &other) const {
            return type == other.type &&
                   width == other.width && height == other.height &&
                   radius == other.radius &&
                   offsetX == other.offsetX && offsetY == other.offsetY;
        }
        bool operator!=(const ColliderComponent &other) const {
            return !(*this == other);
        }
    };

    struct ScriptComponent {
        std::string path;

        bool operator==(const ScriptComponent &other) const {
            return path == other.path;
        }
        bool operator!=(const ScriptComponent &other) const {
            return !(*this == other);
        }
    };

} // namespace Gorgon::Editor
