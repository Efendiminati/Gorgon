#include "SceneRuntime.h"

#include "AnimTimeline.h"
#include "PlaySession.h"
#include "SceneSerializer.h"

#include <Gorgon/Graphics/Bitmap.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegToRad = kPi / 180.0f;
        constexpr float kDefaultEntitySize = 64.0f;

        Gorgon::Graphics::RGBAf ToRGBAf(const ColorRGBA &c) {
            return Gorgon::Graphics::RGBAf(
                c.r / 255.0f,
                c.g / 255.0f,
                c.b / 255.0f,
                c.a / 255.0f
            );
        }

        /// Linear blend used for particle lifetime colours.
        Gorgon::Graphics::RGBAf LerpColor(const ColorRGBA &from, const ColorRGBA &to, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            const auto mix = [t](std::uint8_t a, std::uint8_t b) {
                return (a / 255.0f) + (b / 255.0f - a / 255.0f) * t;
            };
            return Gorgon::Graphics::RGBAf(mix(from.r, to.r), mix(from.g, to.g),
                                           mix(from.b, to.b), mix(from.a, to.a));
        }

        /// Stable colour for a tilemap cell drawn without a tileset texture.
        Gorgon::Graphics::RGBAf TileColor(int index) {
            static const Gorgon::Graphics::RGBAf palette[] = {
                {0.34f, 0.56f, 0.32f, 1.0f}, // grass
                {0.45f, 0.36f, 0.28f, 1.0f}, // dirt
                {0.42f, 0.45f, 0.52f, 1.0f}, // stone
                {0.28f, 0.42f, 0.62f, 1.0f}, // water
                {0.62f, 0.55f, 0.30f, 1.0f}, // sand
                {0.55f, 0.30f, 0.30f, 1.0f}, // brick
            };
            const std::size_t count = sizeof(palette) / sizeof(palette[0]);
            return palette[std::size_t(std::max(0, index)) % count];
        }

        /// Placeholder fill for entities without a (loadable) sprite. Matches the editor viewport so
        /// a scene looks the same in both.
        Gorgon::Graphics::RGBAf FallbackColorForEntity(const Entity &entity) {
            if(entity.sprite.has_value()) {
                const auto &t = entity.sprite->tint;
                if(t.r != 255 || t.g != 255 || t.b != 255) {
                    return Gorgon::Graphics::RGBAf(
                        t.r / 255.0f, t.g / 255.0f, t.b / 255.0f,
                        std::max(0.45f, t.a / 255.0f)
                    );
                }
            }

            static const Gorgon::Graphics::RGBAf palette[] = {
                {0.26f, 0.52f, 0.85f, 0.85f},
                {0.85f, 0.42f, 0.28f, 0.85f},
                {0.30f, 0.72f, 0.52f, 0.85f},
                {0.76f, 0.45f, 0.82f, 0.85f},
                {0.85f, 0.70f, 0.26f, 0.85f},
                {0.28f, 0.70f, 0.78f, 0.85f}
            };
            return palette[entity.id % (sizeof(palette) / sizeof(palette[0]))];
        }

        void DrawLine(Gorgon::Graphics::Layer &target, float x1, float y1, float x2, float y2,
                      float thickness, Gorgon::Graphics::RGBAf color) {
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float len = std::sqrt(dx * dx + dy * dy);
            if(len < 0.5f)
                return;

            const float nx = -dy / len * (thickness * 0.5f);
            const float ny = dx / len * (thickness * 0.5f);

            target.Draw(
                Gorgon::Geometry::Pointf{x1 + nx, y1 + ny},
                Gorgon::Geometry::Pointf{x2 + nx, y2 + ny},
                Gorgon::Geometry::Pointf{x2 - nx, y2 - ny},
                Gorgon::Geometry::Pointf{x1 - nx, y1 - ny},
                color
            );
        }
    } // namespace

    // ---- SceneRuntime --------------------------------------------------------

    struct SceneRuntime::Impl {
        Scene scene;
        std::string scenePath;
        std::string baseDir;

        RuntimeCamera camera;
        SceneRenderOptions options;

        // Keyboard/mouse state the caller fills; pushed into the play session before every step.
        InputState input;

        std::unique_ptr<PlaySession> play;
        std::unordered_map<std::string, std::unique_ptr<Gorgon::Graphics::Bitmap>> sprites;

        // Steps animations/particles/audio when there is no PlaySession (a scene without scripts).
        ComponentSimulation components;
        std::vector<std::string> componentSounds;
        const Gorgon::Graphics::StyledPrinter *textPrinter = nullptr;

        // 2D lighting: the lightmap is rebuilt from the live scene every frame (so a script that
        // moves a light is picked up) and skipped wholesale when the scene carries no light.
        Lightmap lightmap;
        LightmapTexture lightmapTexture;

        std::string lastError;
        bool loaded = false;

        void clearRuntimeState() {
            play.reset();
            sprites.clear();
            components.Reset();
            componentSounds.clear();
            lightmap.Reset();
            lightmapTexture.Clear();
        }

        /// The first active CameraComponent drives the projection when a scene carries one.
        EntityId activeCameraEntity() const {
            for(const auto &entity : scene.GetEntities()) {
                if(entity.camera.has_value() && entity.camera->active)
                    return entity.id;
            }
            return InvalidEntityId;
        }

        /// Copies the active camera component into the runtime camera (zoom + follow target).
        void applyCameraComponent() {
            const EntityId id = activeCameraEntity();
            if(id == InvalidEntityId)
                return;

            const Entity *entity = scene.FindEntity(id);
            if(!entity || !entity->camera)
                return;

            EntityId centerId = entity->camera->followTarget;
            if(centerId == InvalidEntityId || !scene.HasEntity(centerId))
                centerId = id;

            const Transform world = scene.GetWorldTransform(centerId);
            camera.panX = world.x;
            camera.panY = world.y;
            camera.zoom = std::clamp(entity->camera->zoom, 0.01f, 64.0f);
        }

        bool sceneHasScripts() const {
            for(const auto &entity : scene.GetEntities()) {
                if(entity.script && !entity.script->path.empty())
                    return true;
            }
            return false;
        }

        void startScripts() {
            play.reset();
            lastError.clear();

            if(!sceneHasScripts())
                return;

            play = std::unique_ptr<PlaySession>(new PlaySession);
            // The session writes the camera through this pointer, so a script's SetCameraPos/
            // SetCameraZoom changes what Render() shows on the next frame.
            play->SetCamera(&camera);
            // Seed the session with the caller's held state. Start() drops the edge flags, so a key
            // pressed before the scripts ran does not read as freshly pressed on frame one.
            play->GetInput() = input;
            std::string error;
            if(!play->Start(scene, baseDir, &error)) {
                lastError = error;
                play.reset();
            }
        }

        /// Bitmaps are cached per resolved path; a missing/invalid image is cached as nullptr so a
        /// broken reference is only attempted once.
        const Gorgon::Graphics::Bitmap *getSprite(const std::string &asset) {
            if(asset.empty())
                return nullptr;

            auto it = sprites.find(asset);
            if(it != sprites.end())
                return it->second.get();

            std::unique_ptr<Gorgon::Graphics::Bitmap> bitmap;
            const std::string full = ResolvePath(asset);
            if(!full.empty() && std::filesystem::exists(full)) {
                auto candidate = std::unique_ptr<Gorgon::Graphics::Bitmap>(new Gorgon::Graphics::Bitmap);
                // A corrupt or unsupported image must not take the runtime down; it is reported as
                // a missing sprite and the entity falls back to its placeholder rectangle.
                try {
                    if(candidate->Import(full) && candidate->HasData()) {
                        candidate->Prepare();
                        bitmap = std::move(candidate);
                    }
                }
                catch(const std::exception &ex) {
                    lastError = std::string("Sprite could not be loaded: ") + full + ": " + ex.what();
                }
                catch(...) {
                    lastError = "Sprite could not be loaded: " + full;
                }
            }

            const auto *result = bitmap.get();
            sprites.emplace(asset, std::move(bitmap));
            return result;
        }

        std::string ResolvePath(const std::string &path) const {
            if(path.empty())
                return {};

            const std::filesystem::path p(path);
            if(p.is_absolute() || baseDir.empty())
                return p.string();

            return (std::filesystem::path(baseDir) / p).string();
        }

        Gorgon::Geometry::Pointf entityBaseSize(const Entity &entity) {
            if(entity.spriteAnim.has_value()) {
                const auto &anim = *entity.spriteAnim;
                if(anim.frameW > 0 && anim.frameH > 0)
                    return {float(anim.frameW), float(anim.frameH)};
            }

            if(entity.sprite.has_value() && !entity.sprite->asset.empty()) {
                if(const auto *bitmap = getSprite(entity.sprite->asset)) {
                    const auto size = bitmap->GetSize();
                    if(size.Width > 0 && size.Height > 0)
                        return {float(size.Width), float(size.Height)};
                }
            }

            if(entity.tilemap.has_value()) {
                const auto &map = *entity.tilemap;
                if(map.cols > 0 && map.rows > 0)
                    return {float(map.cols) * map.tileW, float(map.rows) * map.tileH};
            }

            if(entity.text.has_value()) {
                const auto &text = *entity.text;
                const float width = float(std::max<std::size_t>(1, text.text.size())) * text.fontSize * 0.6f;
                return {std::max(16.0f, width), text.fontSize * 1.4f};
            }

            if(entity.collider.has_value()) {

                if(entity.collider->type == ColliderType::AABB &&
                   entity.collider->width > 0.0f && entity.collider->height > 0.0f) {
                    return {entity.collider->width, entity.collider->height};
                }
                if(entity.collider->type == ColliderType::Circle && entity.collider->radius > 0.0f) {
                    const float diameter = entity.collider->radius * 2.0f;
                    return {diameter, diameter};
                }
            }

            return {kDefaultEntitySize, kDefaultEntitySize};
        }
    };

    SceneRuntime::SceneRuntime() : impl_(new Impl) { }

    SceneRuntime::~SceneRuntime() = default;

    bool SceneRuntime::Load(const std::string &sceneFile, std::string *outError) {
        auto &impl = *impl_;

        Scene scene;
        std::string error;
        if(!SceneSerializer::LoadFromFile(sceneFile, scene, &error)) {
            impl.lastError = error.empty() ? ("Could not load scene: " + sceneFile) : error;
            if(outError)
                *outError = impl.lastError;
            return false;
        }

        std::string baseDir;
        const std::filesystem::path path(sceneFile);
        const auto parent = path.parent_path();
        if(!parent.empty())
            baseDir = parent.string();

        if(!LoadFromScene(std::move(scene), baseDir, outError))
            return false;

        impl.scenePath = sceneFile;
        return true;
    }

    bool SceneRuntime::LoadFromScene(Scene scene, const std::string &baseDir, std::string *outError) {
        auto &impl = *impl_;

        impl.clearRuntimeState();
        impl.scene = std::move(scene);
        impl.baseDir = baseDir;
        impl.scenePath.clear();
        impl.lastError.clear();
        impl.loaded = true;

        // Component audio plays through the session when one runs, and is otherwise only recorded
        // (the runtime has no audio device of its own).
        Impl *self = &impl;
        impl.components.Reset();
        impl.components.SetSoundSink([self](const std::string &asset) {
            if(self->play)
                self->play->TriggerSound(asset);
            else
                self->componentSounds.push_back(self->ResolvePath(asset));
        });

        impl.startScripts();

        if(outError)
            *outError = impl.lastError;

        return true;
    }

    void SceneRuntime::Unload() {
        auto &impl = *impl_;
        impl.clearRuntimeState();
        impl.scene = Scene{};
        impl.scenePath.clear();
        impl.baseDir.clear();
        impl.lastError.clear();
        impl.loaded = false;
    }

    bool SceneRuntime::IsLoaded() const {
        return impl_->loaded;
    }

    void SceneRuntime::Update(float dt) {
        auto &impl = *impl_;

        // A scriptless scene still animates its sprites, particles and play-on-start audio.
        if(!impl.loaded || !impl.play) {
            if(!impl.loaded)
                return;

            impl.components.Step(impl.scene, dt);
            impl.applyCameraComponent();
            return;
        }

        // Hand the caller's state to the scripts for this step. Tick() clears the session's edge
        // flags, so the caller's own pressed/released flags are cleared here as well — they have
        // been delivered exactly once.
        impl.play->GetInput() = impl.input;
        impl.play->Tick(impl.scene, dt);
        impl.input.ClearEdges();

        // An active CameraComponent owns the projection; a script's SetCameraPos is overridden.
        impl.applyCameraComponent();

        const auto &error = impl.play->GetLastError();
        if(!error.empty())
            impl.lastError = error;
    }

    InputState &SceneRuntime::GetInput() {
        return impl_->input;
    }

    const InputState &SceneRuntime::GetInput() const {
        return impl_->input;
    }

    void SceneRuntime::Render(Gorgon::Graphics::Layer &target) {
        const auto size = target.GetTargetSize();
        Render(target, size.Width, size.Height);
    }

    void SceneRuntime::Render(Gorgon::Graphics::Layer &target, int viewWidth, int viewHeight) {
        auto &impl = *impl_;
        if(!impl.loaded)
            return;

        if(viewWidth <= 0 || viewHeight <= 0)
            return;

        const float viewW = float(viewWidth);
        const float viewH = float(viewHeight);
        const auto &camera = impl.camera;

        if(impl.options.drawBackground) {
            const auto bg = ToRGBAf(impl.scene.settings.backgroundColor);
            target.Draw(0.0f, 0.0f, viewW, viewH, bg);
        }

        const auto order = impl.scene.GetRenderOrder();
        for(EntityId id : order) {
            const Entity *entity = impl.scene.FindEntity(id);
            if(!entity || !entity->visible)
                continue;

            const Transform world = impl.scene.GetWorldTransform(entity->id);
            const auto baseSize = impl.entityBaseSize(*entity);

            const float halfW = baseSize.X * 0.5f * world.scaleX;
            const float halfH = baseSize.Y * 0.5f * world.scaleY;
            const float rad = world.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);

            auto toScreen = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
                const float wx = world.x + lx * c - ly * s;
                const float wy = world.y + lx * s + ly * c;
                return camera.WorldToScreen(wx, wy, viewW, viewH);
            };

            if(impl.options.drawEntities) {
                const Gorgon::Geometry::Pointf corners[4] = {
                    toScreen(-halfW, -halfH),
                    toScreen( halfW, -halfH),
                    toScreen( halfW,  halfH),
                    toScreen(-halfW,  halfH)
                };

                const Gorgon::Graphics::Bitmap *bitmap = entity->sprite.has_value()
                    ? impl.getSprite(entity->sprite->asset)
                    : nullptr;

                if(entity->spriteAnim.has_value()) {
                    // Sprite sheet: the visible quad is one frame of the sheet, selected through
                    // normalized texture coordinates.
                    const auto &anim = *entity->spriteAnim;
                    const auto *sheet = impl.getSprite(anim.asset);
                    bool drew = false;
                    if(sheet) {
                        const auto size = sheet->GetSize();
                        const float imageW = float(std::max(1, size.Width));
                        const float imageH = float(std::max(1, size.Height));
                        const int count = AnimationTimeline::FrameCount(anim);
                        int frame = count > 0
                            ? std::clamp(anim.currentFrame, 0, count - 1)
                            : 0;
                        // The frame names a sub-rect of the sheet; frames that do not set one keep
                        // using their grid cell, so a sheet authored before the timeline existed
                        // plays identically.
                        const auto rect = AnimationTimeline::FrameSourceRect(anim, frame, size.Width,
                                                                            size.Height);

                        const Gorgon::Geometry::Pointf t1{rect.x / imageW, rect.y / imageH};
                        const Gorgon::Geometry::Pointf t2{(rect.x + rect.w) / imageW, rect.y / imageH};
                        const Gorgon::Geometry::Pointf t3{(rect.x + rect.w) / imageW, (rect.y + rect.h) / imageH};
                        const Gorgon::Geometry::Pointf t4{rect.x / imageW, (rect.y + rect.h) / imageH};
                        target.Draw(*sheet, corners[0], corners[1], corners[2], corners[3],
                                    t1, t2, t3, t4, ToRGBAf(anim.tint));
                        drew = true;
                    }
                    if(!drew) {
                        target.Draw(corners[0], corners[1], corners[2], corners[3],
                                    Gorgon::Graphics::RGBAf(0.45f, 0.62f, 0.95f, 0.55f));
                    }
                }
                else if(bitmap) {
                    target.Draw(*bitmap, corners[0], corners[1], corners[2], corners[3],
                                ToRGBAf(entity->sprite->tint));
                }
                else if(entity->tilemap.has_value()) {
                    // Tilemap: one quad per cell, either a sub-rect of the tileset or a stable
                    // colour per tile index (so a map renders without a tileset asset).
                    const auto &map = *entity->tilemap;
                    const auto *tileset = map.tilesetAsset.empty() ? nullptr : impl.getSprite(map.tilesetAsset);
                    const int cols = std::max(0, map.cols);
                    const int rows = std::max(0, map.rows);
                    const float originX = -float(cols) * map.tileW * 0.5f;
                    const float originY = -float(rows) * map.tileH * 0.5f;

                    for(int row = 0; row < rows; ++row) {
                        for(int col = 0; col < cols; ++col) {
                            const std::size_t index = std::size_t(row) * std::size_t(cols) + std::size_t(col);
                            if(index >= map.tiles.size())
                                continue;
                            const int tile = map.tiles[index];
                            if(tile < 0)
                                continue;

                            const float lx = originX + float(col) * map.tileW;
                            const float ly = originY + float(row) * map.tileH;
                            const Gorgon::Geometry::Pointf q[4] = {
                                toScreen(lx, ly),
                                toScreen(lx + map.tileW, ly),
                                toScreen(lx + map.tileW, ly + map.tileH),
                                toScreen(lx, ly + map.tileH)
                            };

                            if(tileset) {
                                const auto size = tileset->GetSize();
                                const int perRow = std::max(1, map.tilesetCols);
                                const int tx = (tile % perRow) * int(map.tileW);
                                const int ty = (tile / perRow) * int(map.tileH);
                                const float imageW = float(std::max(1, size.Width));
                                const float imageH = float(std::max(1, size.Height));
                                const Gorgon::Geometry::Pointf t1{tx / imageW, ty / imageH};
                                const Gorgon::Geometry::Pointf t2{(tx + map.tileW) / imageW, ty / imageH};
                                const Gorgon::Geometry::Pointf t3{(tx + map.tileW) / imageW,
                                                                  (ty + map.tileH) / imageH};
                                const Gorgon::Geometry::Pointf t4{tx / imageW,
                                                                  (ty + map.tileH) / imageH};
                                target.Draw(*tileset, q[0], q[1], q[2], q[3],
                                            t1, t2, t3, t4, Gorgon::Graphics::RGBAf(1.0f));
                            }
                            else {
                                target.Draw(q[0], q[1], q[2], q[3], TileColor(tile));
                            }
                        }
                    }
                }
                else if(entity->text.has_value()) {
                    const auto &label = *entity->text;
                    const Gorgon::Geometry::Pointf origin = toScreen(0.0f, 0.0f);
                    bool drew = false;
                    if(impl.textPrinter) {
                        auto printer = *impl.textPrinter;
                        printer.SetColor(ToRGBAf(label.color));
                        const auto textSize = printer.GetSize(label.text);
                        int x = int(origin.X);
                        if(label.align == TextAlign::Center)
                            x -= textSize.Width / 2;
                        else if(label.align == TextAlign::Right)
                            x -= textSize.Width;
                        const int y = int(origin.Y) - textSize.Height / 2;
                        const auto alignment = label.align == TextAlign::Center
                            ? Gorgon::Graphics::TextAlignment::Center
                            : (label.align == TextAlign::Right
                                ? Gorgon::Graphics::TextAlignment::Right
                                : Gorgon::Graphics::TextAlignment::Left);
                        printer.PrintNoWrap(target, label.text, x, y,
                                            std::max(1, textSize.Width), alignment);
                        drew = true;
                    }
                    if(!drew)
                        target.Draw(corners[0], corners[1], corners[2], corners[3],
                                    Gorgon::Graphics::RGBAf(0.85f, 0.85f, 0.95f, 0.35f));
                }
                else {
                    const auto fill = FallbackColorForEntity(*entity);
                    target.Draw(corners[0], corners[1], corners[2], corners[3], fill);
                }
            }

            // Particles are additive-looking quads, drawn over the entity that emits them. The live
            // list belongs to whichever simulation is ticking: the session when scripts run, the
            // runtime's own otherwise.
            if(impl.options.drawEntities && entity->particles.has_value()) {
                const auto &emitter = *entity->particles;
                const auto *particles = impl.play
                    ? impl.play->GetParticles(entity->id)
                    : impl.components.GetParticles(entity->id);
                if(particles) {
                    const float baseSize = std::max(0.5f, emitter.size) *
                                           std::max(0.01f, (std::abs(world.scaleX) + std::abs(world.scaleY)) * 0.5f);
                    for(const auto &particle : *particles) {
                        const float t = particle.life > 0.0f
                            ? std::clamp(particle.age / particle.life, 0.0f, 1.0f)
                            : 1.0f;
                        const auto color = LerpColor(emitter.startColor, emitter.endColor, t);
                        const float half = baseSize * 0.5f;
                        const Gorgon::Geometry::Pointf quad[4] = {
                            toScreen(particle.x - half, particle.y - half),
                            toScreen(particle.x + half, particle.y - half),
                            toScreen(particle.x + half, particle.y + half),
                            toScreen(particle.x - half, particle.y + half)
                        };
                        target.Draw(quad[0], quad[1], quad[2], quad[3], color);
                    }
                }
            }

            if(impl.options.drawColliders && entity->collider.has_value() &&
               entity->collider->type != ColliderType::None) {
                const auto &collider = *entity->collider;
                // Triggers are drawn amber so a volume that never blocks anything is obvious.
                const Gorgon::Graphics::RGBAf wire = collider.isTrigger
                    ? Gorgon::Graphics::RGBAf(0.95f, 0.71f, 0.24f, 0.90f)
                    : Gorgon::Graphics::RGBAf(0.29f, 0.87f, 0.50f, 0.90f);

                auto colliderPoint = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
                    const float offX = (collider.offsetX + lx) * world.scaleX;
                    const float offY = (collider.offsetY + ly) * world.scaleY;
                    const float wx = world.x + offX * c - offY * s;
                    const float wy = world.y + offX * s + offY * c;
                    return camera.WorldToScreen(wx, wy, viewW, viewH);
                };

                if(collider.type == ColliderType::AABB) {
                    const float hw = collider.width * 0.5f;
                    const float hh = collider.height * 0.5f;
                    const Gorgon::Geometry::Pointf pts[4] = {
                        colliderPoint(-hw, -hh), colliderPoint(hw, -hh),
                        colliderPoint(hw, hh), colliderPoint(-hw, hh)
                    };
                    for(int i = 0; i < 4; ++i) {
                        const auto &a = pts[i];
                        const auto &b = pts[(i + 1) % 4];
                        DrawLine(target, a.X, a.Y, b.X, b.Y, 1.5f, wire);
                    }
                }
                else if(collider.type == ColliderType::Circle) {
                    constexpr int kSegments = 24;
                    auto prev = colliderPoint(collider.radius, 0.0f);
                    for(int i = 1; i <= kSegments; ++i) {
                        const float angle = 2.0f * kPi * float(i) / float(kSegments);
                        const auto curr = colliderPoint(std::cos(angle) * collider.radius,
                                                        std::sin(angle) * collider.radius);
                        DrawLine(target, prev.X, prev.Y, curr.X, curr.Y, 1.5f, wire);
                        prev = curr;
                    }
                }
            }
        }

        // ---- 2D lighting --------------------------------------------------------
        // The lightmap covers exactly what the camera shows and is rebuilt from the live scene, so a
        // script that moves or creates a light is on screen the same frame. A scene without a light
        // returns false here: nothing is allocated, uploaded or drawn, which keeps an unlit scene at
        // the cost and the exact picture it had before lighting existed.
        if(impl.options.drawLighting && impl.options.drawEntities) {
            const auto bounds = CameraWorldBounds(camera.panX, camera.panY, camera.zoom, viewW, viewH);
            int mapWidth = 0;
            int mapHeight = 0;
            LightmapGridSize(viewW, viewH, mapWidth, mapHeight);

            LightmapOptions lightOptions;
            lightOptions.ambient = impl.options.lightAmbient;
            lightOptions.occluderSize = [&impl](EntityId id) -> Gorgon::Geometry::Size {
                const Entity *entity = impl.scene.FindEntity(id);
                if(!entity)
                    return {0, 0};

                const auto size = impl.entityBaseSize(*entity);
                return {int(size.X + 0.5f), int(size.Y + 0.5f)};
            };

            if(BuildSceneLightmap(impl.scene, bounds, mapWidth, mapHeight, impl.lightmap, lightOptions)) {
                impl.lightmapTexture.Upload(impl.lightmap);
                impl.lightmapTexture.Draw(target, viewW, viewH);
            }
        }
        else {
            impl.lightmap.Reset();
        }
    }

    Scene &SceneRuntime::GetScene() {
        return impl_->scene;
    }

    const Scene &SceneRuntime::GetScene() const {
        return impl_->scene;
    }

    RuntimeCamera &SceneRuntime::GetCamera() {
        return impl_->camera;
    }

    const RuntimeCamera &SceneRuntime::GetCamera() const {
        return impl_->camera;
    }

    void SceneRuntime::SetCamera(const RuntimeCamera &camera) {
        impl_->camera = camera;
    }

    void SceneRuntime::CenterCamera(int viewWidth, int viewHeight) {
        auto &impl = *impl_;
        if(!impl.loaded)
            return;

        const auto order = impl.scene.GetRenderOrder();
        bool hasBounds = false;
        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

        for(EntityId id : order) {
            const Entity *entity = impl.scene.FindEntity(id);
            if(!entity || !entity->visible)
                continue;

            const Transform world = impl.scene.GetWorldTransform(entity->id);
            const auto baseSize = impl.entityBaseSize(*entity);
            const float halfW = std::abs(baseSize.X * 0.5f * world.scaleX);
            const float halfH = std::abs(baseSize.Y * 0.5f * world.scaleY);

            minX = hasBounds ? std::min(minX, world.x - halfW) : world.x - halfW;
            maxX = hasBounds ? std::max(maxX, world.x + halfW) : world.x + halfW;
            minY = hasBounds ? std::min(minY, world.y - halfH) : world.y - halfH;
            maxY = hasBounds ? std::max(maxY, world.y + halfH) : world.y + halfH;
            hasBounds = true;
        }

        if(!hasBounds) {
            impl.camera.Reset();
            return;
        }

        impl.camera.panX = (minX + maxX) * 0.5f;
        impl.camera.panY = (minY + maxY) * 0.5f;

        if(viewWidth > 0 && viewHeight > 0) {
            const float spanX = maxX - minX;
            const float spanY = maxY - minY;
            if(spanX > 1.0f && spanY > 1.0f) {
                const float fit = std::min((viewWidth * 0.9f) / spanX, (viewHeight * 0.9f) / spanY);
                impl.camera.zoom = std::clamp(fit, 0.1f, 4.0f);
            }
        }
    }

    SceneRenderOptions &SceneRuntime::GetRenderOptions() {
        return impl_->options;
    }

    const SceneRenderOptions &SceneRuntime::GetRenderOptions() const {
        return impl_->options;
    }

    const std::string &SceneRuntime::GetScenePath() const {
        return impl_->scenePath;
    }

    const std::string &SceneRuntime::GetBaseDir() const {
        return impl_->baseDir;
    }

    void SceneRuntime::SetBaseDir(const std::string &baseDir) {
        auto &impl = *impl_;
        if(impl.baseDir == baseDir)
            return;

        // Cached bitmaps were resolved against the old root; scripts resolve paths at start.
        impl.baseDir = baseDir;
        impl.sprites.clear();
        if(impl.loaded)
            impl.startScripts();
    }

    std::string SceneRuntime::ResolveAssetPath(const std::string &path) const {
        return impl_->ResolvePath(path);
    }

    int SceneRuntime::GetScriptCount() const {
        return impl_->play ? impl_->play->GetScriptCount() : 0;
    }

    const std::vector<CollisionPair> &SceneRuntime::GetActiveOverlaps() const {
        static const std::vector<CollisionPair> empty;
        return impl_->play ? impl_->play->GetActiveOverlaps() : empty;
    }

    bool SceneRuntime::IsOverlapping(EntityId a, EntityId b) const {
        return impl_->play ? impl_->play->IsOverlapping(a, b) : false;
    }

    const std::vector<std::string> &SceneRuntime::GetPlayedSounds() const {
        static const std::vector<std::string> empty;
        if(impl_->play)
            return impl_->play->GetPlayedSounds();
        return impl_->componentSounds.empty() ? empty : impl_->componentSounds;
    }

    const std::vector<Particle> *SceneRuntime::GetParticles(EntityId id) const {
        if(impl_->play)
            return impl_->play->GetParticles(id);
        return impl_->components.GetParticles(id);
    }

    EntityId SceneRuntime::GetActiveCameraEntity() const {
        return impl_->activeCameraEntity();
    }

    void SceneRuntime::SetTextPrinter(const Gorgon::Graphics::StyledPrinter *printer) {
        impl_->textPrinter = printer;
    }

    int SceneRuntime::GetLoadedSpriteCount() const {
        int count = 0;
        for(const auto &entry : impl_->sprites) {
            if(entry.second)
                ++count;
        }
        return count;
    }

    const std::string &SceneRuntime::GetLastError() const {
        return impl_->lastError;
    }

    const Lightmap &SceneRuntime::GetLightmap() const {
        return impl_->lightmap;
    }

} // namespace Gorgon::Editor
