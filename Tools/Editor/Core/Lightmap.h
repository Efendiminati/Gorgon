#pragma once

#include "Components.h"
#include "Scene.h"

#include <Gorgon/Geometry/Point.h>
#include <Gorgon/Geometry/Rectangle.h>
#include <Gorgon/Geometry/Size.h>
#include <Gorgon/Graphics/Bitmap.h>
#include <Gorgon/Graphics/Layer.h>

#include <functional>
#include <vector>

namespace Gorgon :: Editor {

    /// An axis aligned world space box a light cannot shine through: an entity's collider, or the
    /// frame box of its sprite. Rotation folds into the box (the collision system does the same), so
    /// a shadow is always cast by an upright rectangle.
    struct LightOccluder {
        float x = 0.0f;      // world centre
        float y = 0.0f;
        float halfW = 0.0f;
        float halfH = 0.0f;

        bool IsValid() const { return halfW > 0.0f && halfH > 0.0f; }
        bool Contains(float px, float py) const {
            return px >= x - halfW && px <= x + halfW && py >= y - halfH && py <= y + halfH;
        }
        /// Nearest distance from the box to a point (0 inside the box).
        float DistanceTo(float px, float py) const;
    };

    /// Everything outside a light's reach is drawn in `ambient` instead of being left untouched.
    /// The default (opaque black) means "unlit is dark"; a warm or blue ambient tints the shadows
    /// without touching a fully lit pixel.
    struct LightmapOptions {
        ColorRGBA ambient{0, 0, 0, 255};

        /// Shadow casters a single light may test. Kept small on purpose: shadows re-test every
        /// occluder for every sample inside the light, so the cap bounds the per-frame cost. The
        /// nearest occluders to the light win, the rest are ignored (and still drawn normally).
        int maxShadowCastersPerLight = 16;

        /// World size of an entity, used to make a sprite without a collider an occluder. Return a
        /// non-positive size to keep the entity out of the shadow pass. When empty, only colliders
        /// and sprite-animation frame boxes cast shadows.
        std::function<Gorgon::Geometry::Size(EntityId)> occluderSize;
    };

    /**
     * A lightmap is a small grid of light levels over a world rectangle. Each sample accumulates
     * the contribution of every light in range, then the grid is turned into an overlay: the colour
     * unlit pixels are darkened towards plus the alpha that darkens them.
     *
     * The grid itself is pure arithmetic, so it builds and samples without a window. `LightmapTexture`
     * is the only GPU-touching part, kept apart so headless callers (tests, `--selftest`) can use the
     * data model on its own.
     *
     * Light maths, per sample point S and light at L:
     *   d       = |S - L|
     *   reach   = clamp(1 - d / radius, 0, 1) squared  (soft falloff, zero past the radius)
     *   cone    = spots only: 0 outside `coneAngle / 2` of `direction`, eased over the outer 25%
     *   level  += reach * cone * intensity * colour
     * `SampleWorld()` reports the luminance of that accumulated colour, clamped to 0..1.
     */
    class Lightmap {
    public:
        /// Documented cap on the shadow casters one light tests (default of
        /// LightmapOptions::maxShadowCastersPerLight).
        static constexpr int MaxShadowCasters = 16;

        /// Prepares the grid over `world`: `width` x `height` samples, both clamped to 1..256.
        /// Clears any lights and occluders from a previous build. A degenerate world rect (zero or
        /// negative size) leaves the map inactive, so Build() is a no-op.
        void Configure(const Gorgon::Geometry::Rectanglef &world, int width, int height);

        /// Forgets the grid, lights and occluders.
        void Reset();

        /// True once a light has been added to a configured grid: the caller can skip its whole
        /// lighting pass while this is false, which is what keeps unlit scenes at zero cost.
        bool IsActive() const { return active_; }

        int GetWidth() const { return width_; }
        int GetHeight() const { return height_; }
        int GetLightCount() const { return int(lights_.size()); }
        int GetOccluderCount() const { return int(occluders_.size()); }

        const Gorgon::Geometry::Rectanglef &GetWorldBounds() const { return world_; }

        /// Adds a light at a world position. Ignored when the map is not configured. A light with a
        /// non-positive radius or intensity contributes nothing but still counts as present (the
        /// pass runs, it simply lights nothing).
        void AddLight(const Light2DComponent &light, float worldX, float worldY);

        void AddOccluder(const LightOccluder &occluder);

        /// Recomputes every sample. Cheap to call every frame; a no-op while the map is inactive.
        void Build();

        /// Light level (0..1) of a grid sample. Out of range samples report 0.
        float At(int col, int row) const;

        /// Light level at a world position, using the sample that contains it. Values outside the
        /// grid clamp to its border.
        float SampleWorld(float worldX, float worldY) const;

        /// Darkening overlay colour of a grid sample: the ambient colour with an alpha of
        /// `(1 - level) * ambient.a`, which is exactly what turns an unlit pixel into the ambient
        /// colour when the overlay is alpha blended over the scene.
        ColorRGBA GetOverlayColor(int col, int row) const;

        /// The whole overlay, row-major, `width * height` entries.
        const std::vector<ColorRGBA> &GetOverlay() const { return overlay_; }

        /// Shadow casters the last Build() tested (the per-light cap applied).
        int GetShadowCasterCount() const { return shadowCasters_; }

        /// Number of lights whose last build actually cast shadows.
        int GetShadowLightCount() const { return shadowLights_; }

        /// True when every sample carries the same level (an empty or fully unlit map).
        bool IsUniform() const;

        /// Ambient colour the overlay darkens towards.
        void SetAmbient(const ColorRGBA &ambient) { options_.ambient = ambient; }
        const LightmapOptions &GetOptions() const { return options_; }
        void SetOptions(const LightmapOptions &options) { options_ = options; }

    private:
        struct PlacedLight {
            Light2DComponent component;
            float x = 0.0f;
            float y = 0.0f;
        };

        float LevelAt(int col, int row) const;

        Gorgon::Geometry::Rectanglef world_{0.0f, 0.0f, 0.0f, 0.0f};
        int width_ = 0;
        int height_ = 0;
        bool active_ = false;

        LightmapOptions options_;
        std::vector<PlacedLight> lights_;
        std::vector<LightOccluder> occluders_;

        std::vector<float> levels_;
        std::vector<ColorRGBA> overlay_;
        int shadowCasters_ = 0;
        int shadowLights_ = 0;
    };

    /**
     * Builds a lightmap for a scene over the given world rectangle.
     *
     * Lights are the visible entities carrying a Light2DComponent, placed at their world position.
     * Occluders are entity colliders, sprite-animation frame boxes, and any entity `occluderSize`
     * reports a size for, folded into world-space boxes so a parent's scale and rotation are honoured.
     *
     * Returns false when the scene has no light, leaving `out` Reset() so the caller can skip its
     * pass entirely. Grid resolutions of `width` x `height` samples are clamped by Lightmap.
     */
    bool BuildSceneLightmap(const Scene &scene, const Gorgon::Geometry::Rectanglef &world,
                            int width, int height, Lightmap &out,
                            const LightmapOptions &options = {});

    /// Visible world rectangle of an orthographic 2D camera (pan = world point at the centre,
    /// zoom = pixels per world unit).
    Gorgon::Geometry::Rectanglef CameraWorldBounds(float panX, float panY, float zoom,
                                                   float viewWidth, float viewHeight);

    /// Sample grid resolution for a viewport: roughly one sample per `cellPixels` screen pixels,
    /// clamped so a tiny or huge viewport still produces a usable map.
    void LightmapGridSize(float viewWidth, float viewHeight, int &outWidth, int &outHeight,
                          float cellPixels = 16.0f);

    /**
     * Owns the GPU texture that carries a Lightmap overlay into a Graphics::Layer.
     *
     * Rendering needs a graphics context, so this is the only lighting class a headless run must
     * avoid; the Lightmap itself builds and samples without one.
     */
    class LightmapTexture {
    public:
        LightmapTexture() = default;
        LightmapTexture(const LightmapTexture &) = delete;
        LightmapTexture &operator=(const LightmapTexture &) = delete;

        /// Uploads the map's overlay pixels. The texture is only reallocated when the grid size
        /// changes, so a moving light costs one small pixel upload per frame.
        void Upload(const Lightmap &map);

        bool IsReady() const { return size_.Width > 0 && size_.Height > 0; }
        Gorgon::Geometry::Size GetSize() const { return size_; }
        /// Number of Upload() calls, so a caller can assert a light actually reached the texture.
        int GetUploadCount() const { return uploads_; }

        /// Draws the overlay stretched over the whole target, alpha blended, which turns an unlit
        /// pixel into the ambient colour and leaves a fully lit pixel untouched.
        void Draw(Gorgon::Graphics::Layer &target, float viewWidth, float viewHeight) const;

        void Clear();

    private:
        Gorgon::Graphics::Bitmap bitmap_;
        Gorgon::Geometry::Size size_{0, 0};
        int uploads_ = 0;
    };

} // namespace Gorgon::Editor
