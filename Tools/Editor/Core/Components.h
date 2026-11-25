#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
        /// A trigger volume reports overlaps (OnTriggerEnter/Exit) but is never pushed apart.
        bool isTrigger = false;
        /// A static collider is never moved by the solid solver; dynamic colliders are pushed out
        /// of it instead. Two static colliders never resolve against each other.
        bool isStatic = false;

        bool operator==(const ColliderComponent &other) const {
            return type == other.type &&
                   width == other.width && height == other.height &&
                   radius == other.radius &&
                   offsetX == other.offsetX && offsetY == other.offsetY &&
                   isTrigger == other.isTrigger && isStatic == other.isStatic;
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

    /// One authored frame of a sprite-sheet animation: how long it stays on screen and which
    /// sub-rect of the sheet it shows. A rect with a non-positive size means "use the grid cell
    /// this frame's index names" (`frameW` x `frameH`, row-major across the sheet), so a frame only
    /// has to override the part that differs from the sheet layout.
    struct AnimFrame {
        int durationMs = 100;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        bool operator==(const AnimFrame &other) const {
            return durationMs == other.durationMs && x == other.x && y == other.y &&
                   w == other.w && h == other.h;
        }
        bool operator!=(const AnimFrame &other) const {
            return !(*this == other);
        }
    };

    /// A sprite sheet animation: one asset split into a `cols x rows` grid of frames.
    /// `frameTime` is runtime state (advance accumulator) and is never serialized.
    struct SpriteAnimComponent {
        std::string asset;
        int frameW = 32;
        int frameH = 32;
        int frameCount = 1;
        float fps = 12.0f;
        bool loop = true;
        bool playing = true;
        int currentFrame = 0;
        ColorRGBA tint{255, 255, 255, 255};
        float frameTime = 0.0f;
        /// Authored timeline: one entry per frame with its own duration and optional sheet rect.
        /// Empty (the default) keeps the classic behaviour — `frameCount` grid cells of
        /// `frameW x frameH`, each shown for `1 / fps` seconds — so scenes written before the
        /// timeline existed load, play and serialize unchanged. The editor's timeline writes this
        /// the first time a frame is edited.
        std::vector<AnimFrame> frames;

        bool operator==(const SpriteAnimComponent &other) const {
            return asset == other.asset &&
                   frameW == other.frameW && frameH == other.frameH &&
                   frameCount == other.frameCount && fps == other.fps &&
                   loop == other.loop && playing == other.playing &&
                   currentFrame == other.currentFrame && tint == other.tint &&
                   frames == other.frames;
        }
        bool operator!=(const SpriteAnimComponent &other) const {
            return !(*this == other);
        }
    };

    enum class TextAlign {
        Left = 0,
        Center,
        Right
    };

    inline std::string TextAlignToString(TextAlign value) {
        switch(value) {
            case TextAlign::Center: return "center";
            case TextAlign::Right:  return "right";
            case TextAlign::Left:
            default:                return "left";
        }
    }

    inline bool TextAlignFromString(const std::string &str, TextAlign &out) {
        if(str == "left")   { out = TextAlign::Left;   return true; }
        if(str == "center") { out = TextAlign::Center; return true; }
        if(str == "right")  { out = TextAlign::Right;  return true; }
        return false;
    }

    /// Text drawn at the entity origin. `style` names a font registered by the host
    /// ("regular", "bold", ...); unknown names fall back to the host's default font.
    struct TextComponent {
        std::string text = "Text";
        ColorRGBA color{255, 255, 255, 255};
        float fontSize = 16.0f;
        std::string style = "regular";
        TextAlign align = TextAlign::Left;

        bool operator==(const TextComponent &other) const {
            return text == other.text && color == other.color &&
                   fontSize == other.fontSize && style == other.style &&
                   align == other.align;
        }
        bool operator!=(const TextComponent &other) const {
            return !(*this == other);
        }
    };

    /// Background music / sound effect an entity plays.
    struct AudioComponent {
        std::string asset;
        float volume = 1.0f;
        bool loop = false;
        bool playOnStart = true;

        bool operator==(const AudioComponent &other) const {
            return asset == other.asset && volume == other.volume &&
                   loop == other.loop && playOnStart == other.playOnStart;
        }
        bool operator!=(const AudioComponent &other) const {
            return !(*this == other);
        }
    };

    /// Marks the entity as the scene camera. The runtime uses the active component when present,
    /// and a follow target keeps the camera centred on another entity every frame.
    struct CameraComponent {
        bool active = true;
        float zoom = 1.0f;
        EntityId followTarget = InvalidEntityId;

        bool operator==(const CameraComponent &other) const {
            return active == other.active && zoom == other.zoom &&
                   followTarget == other.followTarget;
        }
        bool operator!=(const CameraComponent &other) const {
            return !(*this == other);
        }
    };

    /// A uniform grid of tiles. `tiles` is row-major with cols * rows entries; a negative index
    /// means the cell is empty. Tile indices address the tileset sheet in the same row-major order
    /// with `tilesetCols` columns.
    struct TilemapComponent {
        float tileW = 32.0f;
        float tileH = 32.0f;
        int cols = 0;
        int rows = 0;
        int tilesetCols = 8;
        std::string tilesetAsset;
        std::vector<int> tiles;

        bool operator==(const TilemapComponent &other) const {
            return tileW == other.tileW && tileH == other.tileH &&
                   cols == other.cols && rows == other.rows &&
                   tilesetCols == other.tilesetCols &&
                   tilesetAsset == other.tilesetAsset && tiles == other.tiles;
        }
        bool operator!=(const TilemapComponent &other) const {
            return !(*this == other);
        }
    };

    /// CPU particle emitter. The component holds the emitter settings; the live particles live in
    /// the runtime (PlaySession / SceneRuntime) so nothing simulation-only reaches a scene file.
    struct ParticleComponent {
        float emitRate = 20.0f; // particles per second
        float lifetime = 1.0f;  // seconds a particle lives
        float speed = 60.0f;    // initial world units per second
        ColorRGBA startColor{255, 200, 80, 255};
        ColorRGBA endColor{255, 80, 20, 0};
        float size = 4.0f;
        bool active = true;

        bool operator==(const ParticleComponent &other) const {
            return emitRate == other.emitRate && lifetime == other.lifetime &&
                   speed == other.speed && startColor == other.startColor &&
                   endColor == other.endColor && size == other.size &&
                   active == other.active;
        }
        bool operator!=(const ParticleComponent &other) const {
            return !(*this == other);
        }
    };

    /// One live particle. Positions are local to the emitter entity.
    struct Particle {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float age = 0.0f;
        float life = 1.0f;
    };

    enum class LightType {
        Point = 0,
        Spot
    };

    inline std::string LightTypeToString(LightType type) {
        switch(type) {
            case LightType::Spot: return "spot";
            case LightType::Point:
            default:              return "point";
        }
    }

    inline bool LightTypeFromString(const std::string &str, LightType &out) {
        if(str == "point") { out = LightType::Point; return true; }
        if(str == "spot")  { out = LightType::Spot;  return true; }
        return false;
    }

    /// A 2D light placed at the entity's world position. The runtime multiplies a lightmap built
    /// from every light in the scene over the rendered scene; a scene without lights pays nothing.
    ///
    /// `radius` and `intensity` are the only two values a point light needs; a spot adds a cone of
    /// `coneAngle` degrees (full width, 1..360) centred on `direction` (degrees, 0 = +x, growing
    /// clockwise because y points down). `castShadows` makes the light test occluders (collider and
    /// sprite boxes) between it and each sample point.
    struct Light2DComponent {
        LightType type = LightType::Point;
        ColorRGBA color{255, 244, 214, 255};
        float radius = 256.0f;      // world units the light reaches
        float intensity = 1.0f;     // multiplies the colour; 1 is a full strength light
        float coneAngle = 60.0f;    // spot only: full cone width in degrees
        float direction = 0.0f;     // spot only: degrees, 0 points along +x
        bool castShadows = false;

        bool operator==(const Light2DComponent &other) const {
            return type == other.type && color == other.color &&
                   radius == other.radius && intensity == other.intensity &&
                   coneAngle == other.coneAngle && direction == other.direction &&
                   castShadows == other.castShadows;
        }
        bool operator!=(const Light2DComponent &other) const {
            return !(*this == other);
        }
    };

} // namespace Gorgon::Editor
