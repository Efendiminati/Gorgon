#include "Lightmap.h"

#include "CollisionSystem.h"

#include <Gorgon/Containers/Image.h>
#include <Gorgon/GL.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegToRad = kPi / 180.0f;
        constexpr int kMaxGridSide = 256;
        /// Fade-in width of a spot's outer edge, as a fraction of its half angle. Inside this band a
        /// sample's light drops from full to zero, which is what softens the cone's edge.
        constexpr float kConeSoftEdge = 0.25f;

        float Luminance(const ColorRGBA &color) {
            return (0.299f * color.r + 0.587f * color.g + 0.114f * color.b) / 255.0f;
        }

        /// Wraps an angle difference into (-180, 180].
        float WrapDegrees(float degrees) {
            degrees = std::fmod(degrees + 180.0f, 360.0f);
            if(degrees < 0.0f)
                degrees += 360.0f;
            return degrees - 180.0f;
        }

        /// Light a single light leaves at a world sample point, 0..1 (before the ambient floor).
        float Contribution(const Light2DComponent &light, float lightX, float lightY,
                           float sampleX, float sampleY) {
            if(light.radius <= 0.0f || light.intensity <= 0.0f)
                return 0.0f;

            const float dx = sampleX - lightX;
            const float dy = sampleY - lightY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if(distance >= light.radius)
                return 0.0f;

            // Soft, monotone falloff: full at the centre, zero exactly at the radius.
            const float reach = 1.0f - distance / light.radius;
            float level = reach * reach;

            if(light.type == LightType::Spot) {
                const float half = std::clamp(light.coneAngle, 1.0f, 360.0f) * 0.5f;
                const float direction = std::atan2(dy, dx) / kDegToRad;
                const float delta = std::abs(WrapDegrees(direction - light.direction));
                if(delta >= half)
                    return 0.0f;

                // Ease the outer band of the cone so its edge is soft but still cut off exactly.
                const float soft = std::max(1.0f, half * kConeSoftEdge);
                level *= std::min(1.0f, (half - delta) / soft);
            }

            return level * light.intensity * Luminance(light.color);
        }

        /// True when the segment light -> sample crosses the box. Boxes that contain either end are
        /// filtered by the caller, so a plain slab test over the open segment is enough.
        bool SegmentHitsBox(const LightOccluder &box, float x0, float y0, float x1, float y1) {
            constexpr float eps = 1e-4f;

            float tmin = 0.0f;
            float tmax = 1.0f;

            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float left = box.x - box.halfW;
            const float right = box.x + box.halfW;
            const float top = box.y - box.halfH;
            const float bottom = box.y + box.halfH;

            if(std::abs(dx) < eps) {
                if(x0 < left || x0 > right)
                    return false;
            }
            else {
                float t1 = (left - x0) / dx;
                float t2 = (right - x0) / dx;
                if(t1 > t2)
                    std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if(tmin > tmax)
                    return false;
            }

            if(std::abs(dy) < eps) {
                if(y0 < top || y0 > bottom)
                    return false;
            }
            else {
                float t1 = (top - y0) / dy;
                float t2 = (bottom - y0) / dy;
                if(t1 > t2)
                    std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if(tmin > tmax)
                    return false;
            }

            // The box must be crossed strictly between the light and the sample.
            return tmin > eps && tmin < 1.0f - eps;
        }

        /// Axis-aligned world box of a locally sized quad around the entity's world transform.
        LightOccluder WorldBox(const Transform &world, float halfWidth, float halfHeight,
                               float offsetX = 0.0f, float offsetY = 0.0f) {
            const float rad = world.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);

            const float localX[4] = {offsetX - halfWidth, offsetX + halfWidth,
                                     offsetX + halfWidth, offsetX - halfWidth};
            const float localY[4] = {offsetY - halfHeight, offsetY - halfHeight,
                                     offsetY + halfHeight, offsetY + halfHeight};

            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();

            for(int i = 0; i < 4; ++i) {
                const float sx = localX[i] * world.scaleX;
                const float sy = localY[i] * world.scaleY;
                const float wx = world.x + sx * c - sy * s;
                const float wy = world.y + sx * s + sy * c;
                minX = std::min(minX, wx);
                maxX = std::max(maxX, wx);
                minY = std::min(minY, wy);
                maxY = std::max(maxY, wy);
            }

            LightOccluder box;
            box.x = (minX + maxX) * 0.5f;
            box.y = (minY + maxY) * 0.5f;
            box.halfW = (maxX - minX) * 0.5f;
            box.halfH = (maxY - minY) * 0.5f;
            return box;
        }
    } // namespace

    // ---- LightOccluder -------------------------------------------------------

    float LightOccluder::DistanceTo(float px, float py) const {
        const float dx = std::max(std::abs(px - x) - halfW, 0.0f);
        const float dy = std::max(std::abs(py - y) - halfH, 0.0f);
        return std::sqrt(dx * dx + dy * dy);
    }

    // ---- Lightmap ------------------------------------------------------------

    void Lightmap::Configure(const Gorgon::Geometry::Rectanglef &world, int width, int height) {
        lights_.clear();
        occluders_.clear();
        levels_.clear();
        overlay_.clear();
        shadowCasters_ = 0;
        shadowLights_ = 0;
        active_ = false;

        width_ = std::clamp(width, 0, kMaxGridSide);
        height_ = std::clamp(height, 0, kMaxGridSide);

        if(width_ <= 0 || height_ <= 0 || world.Width <= 0.0f || world.Height <= 0.0f) {
            width_ = 0;
            height_ = 0;
            world_ = Gorgon::Geometry::Rectanglef{0.0f, 0.0f, 0.0f, 0.0f};
            return;
        }

        world_ = world;
    }

    void Lightmap::Reset() {
        Configure(Gorgon::Geometry::Rectanglef{0.0f, 0.0f, 0.0f, 0.0f}, 0, 0);
    }

    void Lightmap::AddLight(const Light2DComponent &light, float worldX, float worldY) {
        if(width_ <= 0 || height_ <= 0)
            return;

        PlacedLight placed;
        placed.component = light;
        placed.x = worldX;
        placed.y = worldY;
        lights_.push_back(placed);
        active_ = true;
    }

    void Lightmap::AddOccluder(const LightOccluder &occluder) {
        if(!occluder.IsValid())
            return;

        occluders_.push_back(occluder);
    }

    void Lightmap::Build() {
        shadowCasters_ = 0;
        shadowLights_ = 0;

        const std::size_t count = std::size_t(width_) * std::size_t(height_);
        levels_.assign(count, 0.0f);
        overlay_.clear();

        if(!active_ || count == 0)
            return;

        const float cellW = world_.Width / float(width_);
        const float cellH = world_.Height / float(height_);

        for(const auto &light : lights_) {
            const auto &component = light.component;
            const float radius = component.radius;

            std::vector<const LightOccluder *> casters;
            if(component.castShadows && radius > 0.0f && component.intensity > 0.0f) {
                for(const auto &occluder : occluders_) {
                    // A box the light sits inside (its own sprite, a container it is in) must not
                    // black out the whole light.
                    if(occluder.Contains(light.x, light.y))
                        continue;
                    if(occluder.DistanceTo(light.x, light.y) > radius)
                        continue;
                    casters.push_back(&occluder);
                }

                // Nearest casters win; the rest stay unshadowed and are simply drawn normally.
                const int cap = std::clamp(options_.maxShadowCastersPerLight, 0, 64);
                if(int(casters.size()) > cap) {
                    std::partial_sort(casters.begin(), casters.begin() + cap, casters.end(),
                                      [&](const LightOccluder *a, const LightOccluder *b) {
                                          return a->DistanceTo(light.x, light.y) <
                                                 b->DistanceTo(light.x, light.y);
                                      });
                    casters.resize(std::size_t(cap));
                }

                if(!casters.empty()) {
                    shadowCasters_ += int(casters.size());
                    ++shadowLights_;
                }
            }

            // Only the light's own footprint has to be sampled.
            const float minWorldX = light.x - radius;
            const float maxWorldX = light.x + radius;
            const float minWorldY = light.y - radius;
            const float maxWorldY = light.y + radius;

            const int colFrom = std::clamp(int(std::floor((minWorldX - world_.X) / cellW)), 0, width_ - 1);
            const int colTo = std::clamp(int(std::floor((maxWorldX - world_.X) / cellW)), 0, width_ - 1);
            const int rowFrom = std::clamp(int(std::floor((minWorldY - world_.Y) / cellH)), 0, height_ - 1);
            const int rowTo = std::clamp(int(std::floor((maxWorldY - world_.Y) / cellH)), 0, height_ - 1);

            for(int row = rowFrom; row <= rowTo; ++row) {
                const float sampleY = world_.Y + (float(row) + 0.5f) * cellH;
                for(int col = colFrom; col <= colTo; ++col) {
                    const float sampleX = world_.X + (float(col) + 0.5f) * cellW;

                    float level = Contribution(component, light.x, light.y, sampleX, sampleY);
                    if(level <= 0.0f)
                        continue;

                    bool blocked = false;
                    for(const LightOccluder *caster : casters) {
                        // A sample inside an occluder is the occluder's own surface: keep it lit so
                        // sprites do not go pitch black in their own shadow.
                        if(caster->Contains(sampleX, sampleY))
                            continue;
                        if(SegmentHitsBox(*caster, light.x, light.y, sampleX, sampleY)) {
                            blocked = true;
                            break;
                        }
                    }
                    if(blocked)
                        continue;

                    levels_[std::size_t(row) * std::size_t(width_) + std::size_t(col)] += level;
                }
            }
        }

        overlay_.reserve(count);
        for(int row = 0; row < height_; ++row) {
            for(int col = 0; col < width_; ++col)
                overlay_.push_back(GetOverlayColor(col, row));
        }
    }

    float Lightmap::LevelAt(int col, int row) const {
        const std::size_t index = std::size_t(row) * std::size_t(width_) + std::size_t(col);
        if(index >= levels_.size())
            return 0.0f;
        return levels_[index];
    }

    float Lightmap::At(int col, int row) const {
        if(col < 0 || row < 0 || col >= width_ || row >= height_)
            return 0.0f;

        return std::clamp(LevelAt(col, row), 0.0f, 1.0f);
    }

    float Lightmap::SampleWorld(float worldX, float worldY) const {
        if(width_ <= 0 || height_ <= 0)
            return 0.0f;

        const float cellW = world_.Width / float(width_);
        const float cellH = world_.Height / float(height_);
        const int col = std::clamp(int(std::floor((worldX - world_.X) / cellW)), 0, width_ - 1);
        const int row = std::clamp(int(std::floor((worldY - world_.Y) / cellH)), 0, height_ - 1);
        return At(col, row);
    }

    ColorRGBA Lightmap::GetOverlayColor(int col, int row) const {
        const float level = At(col, row);
        const auto &ambient = options_.ambient;

        // Brute constants: an unlit sample is drawn fully in the ambient colour, a fully lit one is
        // left untouched. Alpha blending then yields `scene * level + ambient * (1 - level)`.
        ColorRGBA color;
        color.r = ambient.r;
        color.g = ambient.g;
        color.b = ambient.b;
        const float alpha = (1.0f - level) * (float(ambient.a) / 255.0f);
        color.a = std::uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return color;
    }

    bool Lightmap::IsUniform() const {
        if(levels_.empty())
            return true;

        const float first = levels_.front();
        for(float level : levels_) {
            if(std::abs(level - first) > 1e-6f)
                return false;
        }
        return true;
    }

    // ---- scene wiring --------------------------------------------------------

    bool BuildSceneLightmap(const Scene &scene, const Gorgon::Geometry::Rectanglef &world,
                            int width, int height, Lightmap &out, const LightmapOptions &options) {
        out.SetOptions(options);
        out.Configure(world, width, height);

        bool hasLight = false;
        for(const auto &entity : scene.GetEntities()) {
            if(entity.visible && entity.light.has_value()) {
                const Transform worldTransform = scene.GetWorldTransform(entity.id);
                out.AddLight(*entity.light, worldTransform.x, worldTransform.y);
                hasLight = true;
            }
        }

        if(!hasLight) {
            out.Reset();
            return false;
        }

        for(const auto &entity : scene.GetEntities()) {
            if(!entity.visible)
                continue;

            const Transform worldTransform = scene.GetWorldTransform(entity.id);

            // A collider is the authoritative occluder shape: it is what the physics sees.
            CollisionShape shape;
            if(CollisionSystem::ComputeShape(scene, entity, shape)) {
                LightOccluder box;
                box.x = shape.centerX;
                box.y = shape.centerY;
                if(shape.kind == CollisionShape::Kind::Circle) {
                    box.halfW = shape.radius;
                    box.halfH = shape.radius;
                }
                else {
                    box.halfW = shape.halfWidth;
                    box.halfH = shape.halfHeight;
                }
                out.AddOccluder(box);
                continue;
            }

            // Otherwise a sprite (or a tilemap) is its own shadow caster as long as a size is known.
            if(entity.spriteAnim.has_value()) {
                const auto &anim = *entity.spriteAnim;
                if(anim.frameW > 0 && anim.frameH > 0) {
                    out.AddOccluder(WorldBox(worldTransform, float(anim.frameW) * 0.5f,
                                             float(anim.frameH) * 0.5f));
                    continue;
                }
            }

            if(entity.tilemap.has_value()) {
                const auto &map = *entity.tilemap;
                if(map.cols > 0 && map.rows > 0 && map.tileW > 0.0f && map.tileH > 0.0f) {
                    out.AddOccluder(WorldBox(worldTransform,
                                             float(map.cols) * map.tileW * 0.5f,
                                             float(map.rows) * map.tileH * 0.5f));
                    continue;
                }
            }

            if(entity.sprite.has_value() && !entity.sprite->asset.empty() && options.occluderSize) {
                const auto size = options.occluderSize(entity.id);
                if(size.Width > 0 && size.Height > 0) {
                    out.AddOccluder(WorldBox(worldTransform, float(size.Width) * 0.5f,
                                             float(size.Height) * 0.5f));
                }
            }
        }

        out.Build();
        return true;
    }

    Gorgon::Geometry::Rectanglef CameraWorldBounds(float panX, float panY, float zoom,
                                                   float viewWidth, float viewHeight) {
        const float safeZoom = zoom > 0.0f ? zoom : 1.0f;
        const float halfW = viewWidth * 0.5f / safeZoom;
        const float halfH = viewHeight * 0.5f / safeZoom;
        return {panX - halfW, panY - halfH, halfW * 2.0f, halfH * 2.0f};
    }

    void LightmapGridSize(float viewWidth, float viewHeight, int &outWidth, int &outHeight,
                          float cellPixels) {
        const float cell = cellPixels > 0.0f ? cellPixels : 16.0f;
        outWidth = std::clamp(int(viewWidth / cell + 0.5f), 1, kMaxGridSide);
        outHeight = std::clamp(int(viewHeight / cell + 0.5f), 1, kMaxGridSide);
    }

    // ---- LightmapTexture -----------------------------------------------------

    void LightmapTexture::Upload(const Lightmap &map) {
        const int width = map.GetWidth();
        const int height = map.GetHeight();
        if(width <= 0 || height <= 0) {
            Clear();
            return;
        }

        if(size_.Width != width || size_.Height != height) {
            bitmap_ = Gorgon::Graphics::Bitmap({width, height}, Gorgon::Graphics::ColorMode::RGBA);
            bitmap_.Prepare();
            size_ = {width, height};
        }

        Gorgon::Containers::Image image({width, height}, Gorgon::Graphics::ColorMode::RGBA);
        auto *data = image.RawData();
        const auto &pixels = map.GetOverlay();
        const std::size_t count = std::min(pixels.size(), std::size_t(width) * std::size_t(height));
        for(std::size_t i = 0; i < count; ++i) {
            const auto &pixel = pixels[i];
            data[i * 4 + 0] = pixel.r;
            data[i * 4 + 1] = pixel.g;
            data[i * 4 + 2] = pixel.b;
            data[i * 4 + 3] = pixel.a;
        }

        Gorgon::GL::CopyToTexture(bitmap_.GetID(), image, {0, 0});
        ++uploads_;
    }

    void LightmapTexture::Draw(Gorgon::Graphics::Layer &target, float viewWidth, float viewHeight) const {
        if(!IsReady() || viewWidth <= 0.0f || viewHeight <= 0.0f)
            return;

        // Straight alpha blend of the ambient colour: `scene * level + ambient * (1 - level)`.
        target.Draw(
            bitmap_,
            Gorgon::Geometry::Pointf{0.0f, 0.0f},
            Gorgon::Geometry::Pointf{viewWidth, 0.0f},
            Gorgon::Geometry::Pointf{viewWidth, viewHeight},
            Gorgon::Geometry::Pointf{0.0f, viewHeight},
            Gorgon::Graphics::RGBAf(1.0f)
        );
    }

    void LightmapTexture::Clear() {
        bitmap_ = Gorgon::Graphics::Bitmap();
        size_ = {0, 0};
    }

} // namespace Gorgon::Editor
