#include "Viewport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegToRad = kPi / 180.0f;
        constexpr float kDefaultEntitySize = 64.0f;
        constexpr float kHandleSizePx = 8.0f;
        constexpr float kHandleHitRadiusPx = 9.0f;
        constexpr float kAxisLengthPx = 56.0f;

        Gorgon::Graphics::RGBAf ToRGBAf(const ColorRGBA &c) {
            return Gorgon::Graphics::RGBAf(
                c.r / 255.0f,
                c.g / 255.0f,
                c.b / 255.0f,
                c.a / 255.0f
            );
        }

        Gorgon::Graphics::RGBAf FallbackColorForEntity(const Entity &e) {
            if(e.sprite.has_value()) {
                const auto &t = e.sprite->tint;
                if(t.r != 255 || t.g != 255 || t.b != 255) {
                    return Gorgon::Graphics::RGBAf(t.r / 255.0f, t.g / 255.0f, t.b / 255.0f, std::max(0.45f, t.a / 255.0f));
                }
            }
            // Deterministic pleasant muted palette based on entity ID
            static const Gorgon::Graphics::RGBAf palette[] = {
                {0.26f, 0.52f, 0.85f, 0.85f},
                {0.85f, 0.42f, 0.28f, 0.85f},
                {0.30f, 0.72f, 0.52f, 0.85f},
                {0.76f, 0.45f, 0.82f, 0.85f},
                {0.85f, 0.70f, 0.26f, 0.85f},
                {0.28f, 0.70f, 0.78f, 0.85f}
            };
            return palette[e.id % (sizeof(palette) / sizeof(palette[0]))];
        }

        float DistSq(float x1, float y1, float x2, float y2) {
            float dx = x1 - x2;
            float dy = y1 - y2;
            return dx * dx + dy * dy;
        }

        float DistToSegmentSq(float px, float py, float ax, float ay, float bx, float by) {
            float abx = bx - ax;
            float aby = by - ay;
            float lenSq = abx * abx + aby * aby;
            if(lenSq < 1e-6f) {
                return DistSq(px, py, ax, ay);
            }
            float t = ((px - ax) * abx + (py - ay) * aby) / lenSq;
            t = std::clamp(t, 0.0f, 1.0f);
            return DistSq(px, py, ax + t * abx, ay + t * aby);
        }
    } // namespace

    // ---- ViewportCamera ----

    Gorgon::Geometry::Pointf ViewportCamera::WorldToScreen(float wx, float wy, float viewW, float viewH) const {
        return {
            (wx - panX) * zoom + viewW * 0.5f,
            (wy - panY) * zoom + viewH * 0.5f
        };
    }

    Gorgon::Geometry::Pointf ViewportCamera::ScreenToWorld(float sx, float sy, float viewW, float viewH) const {
        return {
            (sx - viewW * 0.5f) / zoom + panX,
            (sy - viewH * 0.5f) / zoom + panY
        };
    }

    void ViewportCamera::ZoomAt(float anchorSx, float anchorSy, float viewW, float viewH, float factor) {
        if(factor <= 0.0f) return;
        auto worldBefore = ScreenToWorld(anchorSx, anchorSy, viewW, viewH);
        zoom = std::clamp(zoom * factor, MinZoom, MaxZoom);
        panX = worldBefore.X - (anchorSx - viewW * 0.5f) / zoom;
        panY = worldBefore.Y - (anchorSy - viewH * 0.5f) / zoom;
    }

    void ViewportCamera::PanByScreenDelta(float dx, float dy) {
        panX -= dx / zoom;
        panY -= dy / zoom;
    }

    void ViewportCamera::Reset() {
        panX = 0.0f;
        panY = 0.0f;
        zoom = 1.0f;
    }

    // ---- EditorViewport ----

    EditorViewport::EditorViewport(Scene &scene, Gorgon::Widgets::Layerbox *layerbox)
        : scene_(scene) {
        if(layerbox) {
            Attach(*layerbox);
        }
    }

    EditorViewport::~EditorViewport() {
        gfxLayer_.Clear();
        spriteCache_.clear();
    }

    void EditorViewport::Attach(Gorgon::Widgets::Layerbox &layerbox) {
        if(attached_) return;
        layerbox_ = &layerbox;
        attached_ = true;

        gfxLayer_.EnableClipping();
        layerbox_->GetLayer().Add(gfxLayer_);
        layerbox_->GetLayer().Add(inputLayer_);

        inputLayer_.SetDown([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
            HandleMouseDown(pt, btn);
        });
        inputLayer_.SetMove([this](Gorgon::Geometry::Point pt) {
            HandleMouseMove(pt);
        });
        inputLayer_.SetUp([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
            HandleMouseUp(pt, btn);
        });
        inputLayer_.SetScroll([this](Gorgon::Geometry::Point pt, float amount) {
            return HandleMouseScroll(pt, amount);
        });
    }

    void EditorViewport::ClearSpriteCache() {
        gfxLayer_.Clear();
        spriteCache_.clear();
    }

    void EditorViewport::SetAssetBaseDir(const std::string &dir) {
        if(assetBaseDir_ != dir) {
            assetBaseDir_ = dir;
            ClearSpriteCache();
        }
    }

    void EditorViewport::ResetCamera() {
        camera_.Reset();
        Redraw();
        if(OnStatusChanged) {
            OnStatusChanged(camera_.panX, camera_.panY, camera_.zoom);
        }
    }

    void EditorViewport::SetSelectedEntity(EntityId id, bool notify) {
        if(id != InvalidEntityId && !scene_.HasEntity(id)) {
            id = InvalidEntityId;
        }
        if(selectedEntity_ == id) return;
        selectedEntity_ = id;
        Redraw();
        if(notify && OnSelectionChanged) {
            OnSelectionChanged(selectedEntity_);
        }
    }

    Gorgon::Geometry::Size EditorViewport::GetViewportSize() const {
        if(sizeOverride_.Width > 0 && sizeOverride_.Height > 0) {
            return sizeOverride_;
        }
        if(attached_) {
            auto sz = gfxLayer_.GetTargetSize();
            if(sz.Width > 0 && sz.Height > 0) {
                return sz;
            }
            if(layerbox_) {
                return layerbox_->GetCurrentSize();
            }
        }
        return {800, 600};
    }

    std::string EditorViewport::resolveAssetPath(const std::string &assetPath) const {
        if(assetPath.empty()) return {};
        std::filesystem::path p(assetPath);
        if(p.is_absolute() || assetBaseDir_.empty()) {
            return p.string();
        }
        return (std::filesystem::path(assetBaseDir_) / p).string();
    }

    const EditorViewport::CachedSprite *EditorViewport::getOrLoadSprite(const std::string &assetPath) const {
        if(assetPath.empty()) return nullptr;
        auto it = spriteCache_.find(assetPath);
        if(it != spriteCache_.end()) {
            return it->second.valid ? &it->second : nullptr;
        }

        CachedSprite entry;
        entry.attempted = true;
        if(attached_) {
            std::string fullPath = resolveAssetPath(assetPath);
            if(!fullPath.empty() && std::filesystem::exists(fullPath)) {
                auto bmp = std::make_unique<Gorgon::Graphics::Bitmap>();
                if(bmp->Import(fullPath) && bmp->HasData()) {
                    bmp->Prepare();
                    entry.bitmap = std::move(bmp);
                    entry.valid = true;
                }
            }
        }
        auto [inserted, _] = spriteCache_.emplace(assetPath, std::move(entry));
        return inserted->second.valid ? &inserted->second : nullptr;
    }

    Gorgon::Geometry::Pointf EditorViewport::ComputeEntityBaseSize(const Entity &entity) const {
        if(entity.sprite.has_value() && !entity.sprite->asset.empty()) {
            if(const auto *cached = getOrLoadSprite(entity.sprite->asset); cached && cached->bitmap) {
                auto sz = cached->bitmap->GetSize();
                if(sz.Width > 0 && sz.Height > 0) {
                    return {static_cast<float>(sz.Width), static_cast<float>(sz.Height)};
                }
            }
        }
        if(entity.collider.has_value()) {
            if(entity.collider->type == ColliderType::AABB && entity.collider->width > 0.0f && entity.collider->height > 0.0f) {
                return {entity.collider->width, entity.collider->height};
            }
            if(entity.collider->type == ColliderType::Circle && entity.collider->radius > 0.0f) {
                float d = entity.collider->radius * 2.0f;
                return {d, d};
            }
        }
        return {kDefaultEntitySize, kDefaultEntitySize};
    }

    std::array<Gorgon::Geometry::Pointf, 4> EditorViewport::ComputeEntityWorldCorners(const Entity &entity) const {
        const Transform wt = scene_.GetWorldTransform(entity.id);
        const auto baseSize = ComputeEntityBaseSize(entity);
        const float hw = baseSize.X * 0.5f * wt.scaleX;
        const float hh = baseSize.Y * 0.5f * wt.scaleY;

        const float rad = wt.rotation * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);

        auto transformPt = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
            return {
                wt.x + lx * c - ly * s,
                wt.y + lx * s + ly * c
            };
        };

        return {
            transformPt(-hw, -hh),
            transformPt( hw, -hh),
            transformPt( hw,  hh),
            transformPt(-hw,  hh)
        };
    }

    float EditorViewport::snapValue(float value) const {
        if(!gridSnap_) return value;
        const int gs = std::max(1, scene_.settings.gridSize);
        return std::round(value / static_cast<float>(gs)) * static_cast<float>(gs);
    }

    EntityId EditorViewport::HitTestEntity(float sx, float sy) const {
        const auto sz = GetViewportSize();
        const auto wp = camera_.ScreenToWorld(sx, sy, static_cast<float>(sz.Width), static_cast<float>(sz.Height));

        const auto order = scene_.GetRenderOrder();
        for(auto it = order.rbegin(); it != order.rend(); ++it) {
            const Entity *e = scene_.FindEntity(*it);
            if(!e || !e->visible || e->locked) continue;

            const Transform wt = scene_.GetWorldTransform(e->id);
            const auto baseSize = ComputeEntityBaseSize(*e);
            const float hw = std::abs(baseSize.X * 0.5f * wt.scaleX);
            const float hh = std::abs(baseSize.Y * 0.5f * wt.scaleY);

            // Inverse rotate world point into entity local space
            const float dx = wp.X - wt.x;
            const float dy = wp.Y - wt.y;
            const float rad = -wt.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);
            const float lx = dx * c - dy * s;
            const float ly = dx * s + dy * c;

            if(std::abs(lx) <= std::max(hw, 4.0f) && std::abs(ly) <= std::max(hh, 4.0f)) {
                return e->id;
            }
        }
        return InvalidEntityId;
    }

    GizmoHandle EditorViewport::HitTestGizmo(float sx, float sy) const {
        if(selectedEntity_ == InvalidEntityId) return GizmoHandle::None;
        const Entity *e = scene_.FindEntity(selectedEntity_);
        if(!e || !e->visible || e->locked) return GizmoHandle::None;

        const auto sz = GetViewportSize();
        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);
        const float hitSq = kHandleHitRadiusPx * kHandleHitRadiusPx;

        // 1. Corner scale handles
        const auto corners = ComputeEntityWorldCorners(*e);
        const GizmoHandle cornerHandles[4] = {
            GizmoHandle::ScaleTL,
            GizmoHandle::ScaleTR,
            GizmoHandle::ScaleBR,
            GizmoHandle::ScaleBL
        };
        for(int i = 0; i < 4; ++i) {
            const auto sc = camera_.WorldToScreen(corners[i].X, corners[i].Y, viewW, viewH);
            if(DistSq(sx, sy, sc.X, sc.Y) <= hitSq) {
                return cornerHandles[i];
            }
        }

        // 2. Translate X / Y axis arrows
        const Transform wt = scene_.GetWorldTransform(e->id);
        const auto center = camera_.WorldToScreen(wt.x, wt.y, viewW, viewH);
        if(DistToSegmentSq(sx, sy, center.X + 8.0f, center.Y, center.X + kAxisLengthPx, center.Y) <= 49.0f) {
            return GizmoHandle::TranslateX;
        }
        if(DistToSegmentSq(sx, sy, center.X, center.Y + 8.0f, center.X, center.Y + kAxisLengthPx) <= 49.0f) {
            return GizmoHandle::TranslateY;
        }

        return GizmoHandle::None;
    }

    void EditorViewport::HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
        using Btn = Gorgon::Input::Mouse::Button;
        lastMouse_ = pt;

        const auto sz = GetViewportSize();
        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);

        // Note: Gorgon::Input::Mouse::Button sets bit 9 (0x200) on all buttons,
        // so operator&& is true for any two non-None buttons. Mask the low 9 button bits.
        auto hasButton = [](Btn mask, Btn target) -> bool {
            return ((static_cast<int>(mask) & static_cast<int>(target)) & 0x1FF) != 0;
        };

        // Middle mouse or Space + Left mouse => Pan
        if(hasButton(btn, Btn::Middle) || (hasButton(btn, Btn::Left) && spaceHeld_)) {
            dragMode_ = DragMode::Pan;
            return;
        }

        if(!hasButton(btn, Btn::Left)) return;

        const float sx = static_cast<float>(pt.X);
        const float sy = static_cast<float>(pt.Y);
        dragStartWorld_ = camera_.ScreenToWorld(sx, sy, viewW, viewH);

        // Check gizmo handles on current selection first
        const GizmoHandle handle = HitTestGizmo(sx, sy);
        if(handle != GizmoHandle::None) {
            if(const Entity *e = scene_.FindEntity(selectedEntity_)) {
                activeHandle_ = handle;
                dragStartLocalTransform_ = e->transform;
                dragStartWorldTransform_ = scene_.GetWorldTransform(e->id);
                if(handle == GizmoHandle::TranslateX) {
                    dragMode_ = DragMode::MoveEntityX;
                } else if(handle == GizmoHandle::TranslateY) {
                    dragMode_ = DragMode::MoveEntityY;
                } else {
                    dragMode_ = DragMode::ScaleEntity;
                }
                return;
            }
        }

        // Otherwise pick entity under cursor
        const EntityId hit = HitTestEntity(sx, sy);
        SetSelectedEntity(hit, true);
        if(hit != InvalidEntityId) {
            if(const Entity *e = scene_.FindEntity(hit)) {
                dragStartLocalTransform_ = e->transform;
                dragStartWorldTransform_ = scene_.GetWorldTransform(e->id);
                dragMode_ = DragMode::MoveEntity;
            }
        } else {
            dragMode_ = DragMode::None;
        }
    }

    void EditorViewport::HandleMouseMove(Gorgon::Geometry::Point pt) {
        const auto sz = GetViewportSize();
        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);
        const float sx = static_cast<float>(pt.X);
        const float sy = static_cast<float>(pt.Y);
        const auto worldNow = camera_.ScreenToWorld(sx, sy, viewW, viewH);

        if(dragMode_ == DragMode::Pan) {
            const float dx = static_cast<float>(pt.X - lastMouse_.X);
            const float dy = static_cast<float>(pt.Y - lastMouse_.Y);
            camera_.PanByScreenDelta(dx, dy);
            lastMouse_ = pt;
            Redraw();
            if(OnStatusChanged) {
                OnStatusChanged(worldNow.X, worldNow.Y, camera_.zoom);
            }
            return;
        }

        lastMouse_ = pt;

        if(dragMode_ == DragMode::MoveEntity || dragMode_ == DragMode::MoveEntityX || dragMode_ == DragMode::MoveEntityY) {
            Entity *e = scene_.FindEntity(selectedEntity_);
            if(e && !e->locked) {
                float dwx = worldNow.X - dragStartWorld_.X;
                float dwy = worldNow.Y - dragStartWorld_.Y;
                if(dragMode_ == DragMode::MoveEntityX) dwy = 0.0f;
                if(dragMode_ == DragMode::MoveEntityY) dwx = 0.0f;

                // Convert world delta to parent's local space if parented
                float dlx = dwx;
                float dly = dwy;
                if(e->parentId != InvalidEntityId) {
                    const Transform pwt = scene_.GetWorldTransform(e->parentId);
                    const float rad = -pwt.rotation * kDegToRad;
                    const float c = std::cos(rad);
                    const float s = std::sin(rad);
                    const float rx = dwx * c - dwy * s;
                    const float ry = dwx * s + dwy * c;
                    dlx = (std::abs(pwt.scaleX) > 1e-4f) ? (rx / pwt.scaleX) : rx;
                    dly = (std::abs(pwt.scaleY) > 1e-4f) ? (ry / pwt.scaleY) : ry;
                }

                float newX = dragStartLocalTransform_.x + dlx;
                float newY = dragStartLocalTransform_.y + dly;
                if(dragMode_ != DragMode::MoveEntityY) newX = snapValue(newX);
                if(dragMode_ != DragMode::MoveEntityX) newY = snapValue(newY);

                if(e->transform.x != newX || e->transform.y != newY) {
                    e->transform.x = newX;
                    e->transform.y = newY;
                    Redraw();
                    if(OnSceneModified) OnSceneModified();
                }
            }
        } else if(dragMode_ == DragMode::ScaleEntity) {
            Entity *e = scene_.FindEntity(selectedEntity_);
            if(e && !e->locked) {
                const auto baseSize = ComputeEntityBaseSize(*e);
                const float halfW = std::max(1.0f, baseSize.X * 0.5f);
                const float halfH = std::max(1.0f, baseSize.Y * 0.5f);

                // Transform current mouse world pos into entity's unscaled local space
                const float dx = worldNow.X - dragStartWorldTransform_.x;
                const float dy = worldNow.Y - dragStartWorldTransform_.y;
                const float rad = -dragStartWorldTransform_.rotation * kDegToRad;
                const float c = std::cos(rad);
                const float s = std::sin(rad);
                const float lx = std::abs(dx * c - dy * s);
                const float ly = std::abs(dx * s + dy * c);

                float parentScaleX = 1.0f;
                float parentScaleY = 1.0f;
                if(e->parentId != InvalidEntityId) {
                    const Transform pwt = scene_.GetWorldTransform(e->parentId);
                    parentScaleX = std::max(0.05f, std::abs(pwt.scaleX));
                    parentScaleY = std::max(0.05f, std::abs(pwt.scaleY));
                }

                float newScaleX = std::max(0.05f, (lx / halfW) / parentScaleX);
                float newScaleY = std::max(0.05f, (ly / halfH) / parentScaleY);
                if(gridSnap_) {
                    newScaleX = std::max(0.1f, std::round(newScaleX * 10.0f) / 10.0f);
                    newScaleY = std::max(0.1f, std::round(newScaleY * 10.0f) / 10.0f);
                }

                if(e->transform.scaleX != newScaleX || e->transform.scaleY != newScaleY) {
                    e->transform.scaleX = newScaleX;
                    e->transform.scaleY = newScaleY;
                    Redraw();
                    if(OnSceneModified) OnSceneModified();
                }
            }
        }

        if(OnStatusChanged) {
            OnStatusChanged(worldNow.X, worldNow.Y, camera_.zoom);
        }
    }

    void EditorViewport::HandleMouseUp(Gorgon::Geometry::Point /*pt*/, Gorgon::Input::Mouse::Button /*btn*/) {
        dragMode_ = DragMode::None;
        activeHandle_ = GizmoHandle::None;
    }

    bool EditorViewport::HandleMouseScroll(Gorgon::Geometry::Point pt, float amount) {
        if(amount == 0.0f) return false;
        const auto sz = GetViewportSize();
        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);
        const float factor = std::pow(1.15f, amount);
        camera_.ZoomAt(static_cast<float>(pt.X), static_cast<float>(pt.Y), viewW, viewH, factor);
        Redraw();
        if(OnStatusChanged) {
            const auto wp = camera_.ScreenToWorld(static_cast<float>(pt.X), static_cast<float>(pt.Y), viewW, viewH);
            OnStatusChanged(wp.X, wp.Y, camera_.zoom);
        }
        return true;
    }

    // ---- Rendering ----

    void EditorViewport::drawLine(float x1, float y1, float x2, float y2, float thickness, Gorgon::Graphics::RGBAf color) {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if(len < 0.5f) return;

        const float nx = -dy / len * (thickness * 0.5f);
        const float ny =  dx / len * (thickness * 0.5f);

        gfxLayer_.Draw(
            Gorgon::Geometry::Pointf{x1 + nx, y1 + ny},
            Gorgon::Geometry::Pointf{x2 + nx, y2 + ny},
            Gorgon::Geometry::Pointf{x2 - nx, y2 - ny},
            Gorgon::Geometry::Pointf{x1 - nx, y1 - ny},
            color
        );
    }

    void EditorViewport::drawRectOutline(const std::array<Gorgon::Geometry::Pointf, 4> &pts, float thickness, Gorgon::Graphics::RGBAf color) {
        for(int i = 0; i < 4; ++i) {
            const auto &a = pts[i];
            const auto &b = pts[(i + 1) % 4];
            drawLine(a.X, a.Y, b.X, b.Y, thickness, color);
        }
    }

    void EditorViewport::drawGrid(float viewW, float viewH) {
        float step = static_cast<float>(std::max(4, scene_.settings.gridSize));
        // Adapt grid spacing when zoomed far out so lines stay legible
        while(step * camera_.zoom < 12.0f) {
            step *= 2.0f;
        }

        const auto topLeft = camera_.ScreenToWorld(0.0f, 0.0f, viewW, viewH);
        const auto botRight = camera_.ScreenToWorld(viewW, viewH, viewW, viewH);

        const float startX = std::floor(topLeft.X / step) * step;
        const float endX   = std::ceil(botRight.X / step) * step;
        const float startY = std::floor(topLeft.Y / step) * step;
        const float endY   = std::ceil(botRight.Y / step) * step;

        const Gorgon::Graphics::RGBAf minorCol(1.0f, 1.0f, 1.0f, 0.06f);
        const Gorgon::Graphics::RGBAf majorCol(1.0f, 1.0f, 1.0f, 0.13f);
        const float majorStep = step * 4.0f;

        for(float wx = startX; wx <= endX + 0.01f; wx += step) {
            const auto sp = camera_.WorldToScreen(wx, 0.0f, viewW, viewH);
            const bool isMajor = std::abs(std::fmod(std::round(wx / step), 4.0f)) < 0.01f;
            gfxLayer_.Draw(sp.X, 0.0f, 1.0f, viewH, isMajor ? majorCol : minorCol);
        }

        for(float wy = startY; wy <= endY + 0.01f; wy += step) {
            const auto sp = camera_.WorldToScreen(0.0f, wy, viewW, viewH);
            const bool isMajor = std::abs(std::fmod(std::round(wy / step), 4.0f)) < 0.01f;
            gfxLayer_.Draw(0.0f, sp.Y, viewW, 1.0f, isMajor ? majorCol : minorCol);
        }

        // World origin axes (X = red, Y = green)
        const auto origin = camera_.WorldToScreen(0.0f, 0.0f, viewW, viewH);
        if(origin.Y >= -2.0f && origin.Y <= viewH + 2.0f) {
            gfxLayer_.Draw(0.0f, origin.Y - 0.5f, viewW, 2.0f, Gorgon::Graphics::RGBAf(0.85f, 0.25f, 0.25f, 0.45f));
        }
        if(origin.X >= -2.0f && origin.X <= viewW + 2.0f) {
            gfxLayer_.Draw(origin.X - 0.5f, 0.0f, 2.0f, viewH, Gorgon::Graphics::RGBAf(0.25f, 0.80f, 0.35f, 0.45f));
        }
    }

    void EditorViewport::drawEntities(float viewW, float viewH) {
        const auto order = scene_.GetRenderOrder();
        for(EntityId id : order) {
            const Entity *e = scene_.FindEntity(id);
            if(!e || !e->visible) continue;

            const auto wc = ComputeEntityWorldCorners(*e);
            std::array<Gorgon::Geometry::Pointf, 4> sc;
            for(int i = 0; i < 4; ++i) {
                sc[i] = camera_.WorldToScreen(wc[i].X, wc[i].Y, viewW, viewH);
            }

            bool drewSprite = false;
            if(e->sprite.has_value() && !e->sprite->asset.empty()) {
                if(const auto *cached = getOrLoadSprite(e->sprite->asset); cached && cached->bitmap) {
                    gfxLayer_.Draw(*cached->bitmap, sc[0], sc[1], sc[2], sc[3], ToRGBAf(e->sprite->tint));
                    drewSprite = true;
                }
            }

            if(!drewSprite) {
                const auto fill = FallbackColorForEntity(*e);
                gfxLayer_.Draw(sc[0], sc[1], sc[2], sc[3], fill);
                drawRectOutline(sc, 1.5f, Gorgon::Graphics::RGBAf(1.0f, 1.0f, 1.0f, 0.35f));
            }
        }
    }

    void EditorViewport::drawColliders(float viewW, float viewH) {
        const Gorgon::Graphics::RGBAf colColor(0.29f, 0.87f, 0.50f, 0.90f);
        const auto order = scene_.GetRenderOrder();
        for(EntityId id : order) {
            const Entity *e = scene_.FindEntity(id);
            if(!e || !e->visible || !e->collider.has_value()) continue;
            const auto &col = *e->collider;
            if(col.type == ColliderType::None) continue;

            const Transform wt = scene_.GetWorldTransform(e->id);
            const float rad = wt.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);

            auto toScreen = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
                const float wx = wt.x + (lx * wt.scaleX) * c - (ly * wt.scaleY) * s;
                const float wy = wt.y + (lx * wt.scaleX) * s + (ly * wt.scaleY) * c;
                return camera_.WorldToScreen(wx, wy, viewW, viewH);
            };

            if(col.type == ColliderType::AABB) {
                const float hw = col.width * 0.5f;
                const float hh = col.height * 0.5f;
                std::array<Gorgon::Geometry::Pointf, 4> pts = {
                    toScreen(col.offsetX - hw, col.offsetY - hh),
                    toScreen(col.offsetX + hw, col.offsetY - hh),
                    toScreen(col.offsetX + hw, col.offsetY + hh),
                    toScreen(col.offsetX - hw, col.offsetY + hh)
                };
                drawRectOutline(pts, 1.5f, colColor);
            } else if(col.type == ColliderType::Circle) {
                constexpr int kSegments = 24;
                Gorgon::Geometry::Pointf prev = toScreen(col.offsetX + col.radius, col.offsetY);
                for(int i = 1; i <= kSegments; ++i) {
                    const float a = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
                    const auto curr = toScreen(col.offsetX + std::cos(a) * col.radius, col.offsetY + std::sin(a) * col.radius);
                    drawLine(prev.X, prev.Y, curr.X, curr.Y, 1.5f, colColor);
                    prev = curr;
                }
            }
        }
    }

    void EditorViewport::drawSelectionGizmo(float viewW, float viewH) {
        if(selectedEntity_ == InvalidEntityId) return;
        const Entity *e = scene_.FindEntity(selectedEntity_);
        if(!e || !e->visible) return;

        const auto wc = ComputeEntityWorldCorners(*e);
        std::array<Gorgon::Geometry::Pointf, 4> sc;
        for(int i = 0; i < 4; ++i) {
            sc[i] = camera_.WorldToScreen(wc[i].X, wc[i].Y, viewW, viewH);
        }

        // Selection bounding box
        const Gorgon::Graphics::RGBAf selColor = e->locked
            ? Gorgon::Graphics::RGBAf(0.95f, 0.55f, 0.25f, 0.95f)
            : Gorgon::Graphics::RGBAf(0.22f, 0.74f, 0.97f, 1.0f);
        drawRectOutline(sc, 2.0f, selColor);

        if(e->locked) return;

        // Corner scale handles
        const float hs = kHandleSizePx;
        const float half = hs * 0.5f;
        for(int i = 0; i < 4; ++i) {
            gfxLayer_.Draw(sc[i].X - half - 1.0f, sc[i].Y - half - 1.0f, hs + 2.0f, hs + 2.0f, Gorgon::Graphics::RGBAf(0.08f, 0.10f, 0.14f, 1.0f));
            gfxLayer_.Draw(sc[i].X - half, sc[i].Y - half, hs, hs, Gorgon::Graphics::RGBAf(0.95f, 0.97f, 1.0f, 1.0f));
        }

        // Translate X (red) and Y (green) arrows from entity center
        const Transform wt = scene_.GetWorldTransform(e->id);
        const auto center = camera_.WorldToScreen(wt.x, wt.y, viewW, viewH);
        const Gorgon::Graphics::RGBAf xCol(0.96f, 0.30f, 0.30f, 0.95f);
        const Gorgon::Graphics::RGBAf yCol(0.28f, 0.85f, 0.42f, 0.95f);

        // X axis
        drawLine(center.X, center.Y, center.X + kAxisLengthPx, center.Y, 2.5f, xCol);
        gfxLayer_.Draw(
            Gorgon::Geometry::Pointf{center.X + kAxisLengthPx + 7.0f, center.Y},
            Gorgon::Geometry::Pointf{center.X + kAxisLengthPx - 3.0f, center.Y - 5.0f},
            Gorgon::Geometry::Pointf{center.X + kAxisLengthPx - 3.0f, center.Y + 5.0f},
            Gorgon::Geometry::Pointf{center.X + kAxisLengthPx + 7.0f, center.Y},
            xCol
        );

        // Y axis
        drawLine(center.X, center.Y, center.X, center.Y + kAxisLengthPx, 2.5f, yCol);
        gfxLayer_.Draw(
            Gorgon::Geometry::Pointf{center.X, center.Y + kAxisLengthPx + 7.0f},
            Gorgon::Geometry::Pointf{center.X - 5.0f, center.Y + kAxisLengthPx - 3.0f},
            Gorgon::Geometry::Pointf{center.X + 5.0f, center.Y + kAxisLengthPx - 3.0f},
            Gorgon::Geometry::Pointf{center.X, center.Y + kAxisLengthPx + 7.0f},
            yCol
        );

        // Center pivot dot
        gfxLayer_.Draw(center.X - 4.0f, center.Y - 4.0f, 8.0f, 8.0f, Gorgon::Graphics::RGBAf(1.0f, 0.85f, 0.25f, 1.0f));
    }

    void EditorViewport::Redraw() {
        if(!attached_) return;

        gfxLayer_.Clear();
        const auto sz = GetViewportSize();
        if(sz.Width <= 0 || sz.Height <= 0) return;

        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);

        // 1. Background
        gfxLayer_.Draw(0.0f, 0.0f, viewW, viewH, ToRGBAf(scene_.settings.backgroundColor));

        // 2. Grid
        drawGrid(viewW, viewH);

        // 3. Scene entities (sorted by zOrder)
        drawEntities(viewW, viewH);

        // 4. Collider wireframes
        drawColliders(viewW, viewH);

        // 5. Selection bounding box & translate/scale gizmo
        drawSelectionGizmo(viewW, viewH);
    }

} // namespace Gorgon::Editor
