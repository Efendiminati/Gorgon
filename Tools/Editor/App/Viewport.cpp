#include "App/Viewport.h"
#include "Core/AnimTimeline.h"
#include "Core/FileWatcher.h"

#include "Core/PlaySession.h"

#include <Gorgon/Graphics/Font.h>
#include <Gorgon/Input/Keyboard.h>
#include <Gorgon/Widgets/Registry.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegToRad = kPi / 180.0f;
        constexpr float kRadToDeg = 180.0f / kPi;
        constexpr float kDefaultEntitySize = 64.0f;
        constexpr float kHandleSizePx = 8.0f;
        constexpr float kHandleHitRadiusPx = 9.0f;
        constexpr float kAxisLengthPx = 56.0f;
        constexpr float kRotateStemPx = 28.0f;
        constexpr float kRotateHandleRadiusPx = 6.0f;
        constexpr float kRotateSnapDeg = 15.0f;

        Gorgon::Graphics::RGBAf ToRGBAf(const ColorRGBA &c) {
            return Gorgon::Graphics::RGBAf(
                c.r / 255.0f,
                c.g / 255.0f,
                c.b / 255.0f,
                c.a / 255.0f
            );
        }

        /// Stable preview colour per tile index, matching the runtime's tileset-less rendering.
        Gorgon::Graphics::RGBAf TilePreviewColor(int index) {
            static const Gorgon::Graphics::RGBAf palette[] = {
                {0.34f, 0.56f, 0.32f, 1.0f},
                {0.45f, 0.36f, 0.28f, 1.0f},
                {0.42f, 0.45f, 0.52f, 1.0f},
                {0.28f, 0.42f, 0.62f, 1.0f},
                {0.62f, 0.55f, 0.30f, 1.0f},
                {0.55f, 0.30f, 0.30f, 1.0f},
            };
            const std::size_t count = sizeof(palette) / sizeof(palette[0]);
            return palette[std::size_t(std::max(0, index)) % count];
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

    bool EditorViewport::InvalidateSprite(const std::string &path) {
        if(path.empty() || spriteCache_.empty())
            return false;

        const std::string resolvedTarget = resolveAssetPath(path);
        bool erased = false;
        for(auto it = spriteCache_.begin(); it != spriteCache_.end(); ) {
            const std::string resolvedKey = resolveAssetPath(it->first);
            if(FileWatcher::PathsEqual(it->first, path) ||
               FileWatcher::PathsEqual(it->first, resolvedTarget) ||
               FileWatcher::PathsEqual(resolvedKey, path) ||
               FileWatcher::PathsEqual(resolvedKey, resolvedTarget)) {
                it = spriteCache_.erase(it);
                erased = true;
            } else {
                ++it;
            }
        }
        if(erased && attached_) {
            gfxLayer_.Clear();
        }
        return erased;
    }

    bool EditorViewport::HasCachedSpriteEntry(const std::string &assetPath) const {
        if(assetPath.empty())
            return false;
        if(spriteCache_.find(assetPath) != spriteCache_.end())
            return true;
        const std::string resolved = resolveAssetPath(assetPath);
        if(spriteCache_.find(resolved) != spriteCache_.end())
            return true;
        for(const auto &kv : spriteCache_) {
            if(FileWatcher::PathsEqual(resolveAssetPath(kv.first), resolved))
                return true;
        }
        return false;
    }

    Gorgon::Geometry::Size EditorViewport::GetSpritePixelSize(const std::string &assetPath) const {
        // Ask the cache first: a sheet the viewport already drew costs nothing to measure.
        auto size = GetCachedSpriteSize(assetPath);
        if(size.Width > 0 && size.Height > 0)
            return size;

        if(const auto *cached = getOrLoadSprite(assetPath); cached && cached->bitmap) {
            const auto loaded = cached->bitmap->GetSize();
            if(loaded.Width > 0 && loaded.Height > 0)
                return loaded;
        }

        return {0, 0};
    }

    void EditorViewport::SetAnimationPreview(EntityId id, int frame) {
        if(frame < 0 || id == InvalidEntityId) {
            ClearAnimationPreview();
            return;
        }

        previewEntity_ = id;
        previewFrame_ = frame;
    }

    void EditorViewport::ClearAnimationPreview() {
        previewEntity_ = InvalidEntityId;
        previewFrame_ = -1;
    }

    int EditorViewport::GetDisplayedAnimationFrame(EntityId id) const {
        const Entity *entity = scene_.FindEntity(id);
        if(!entity || !entity->spriteAnim)
            return -1;

        if(id == previewEntity_ && previewFrame_ >= 0)
            return previewFrame_;

        return entity->spriteAnim->currentFrame;
    }

    Gorgon::Geometry::Size EditorViewport::GetCachedSpriteSize(const std::string &assetPath) const {
        if(assetPath.empty())
            return {0, 0};
        auto it = spriteCache_.find(assetPath);
        if(it == spriteCache_.end()) {
            const std::string resolved = resolveAssetPath(assetPath);
            it = spriteCache_.find(resolved);
            if(it == spriteCache_.end()) {
                for(auto iter = spriteCache_.begin(); iter != spriteCache_.end(); ++iter) {
                    if(FileWatcher::PathsEqual(resolveAssetPath(iter->first), resolved)) {
                        it = iter;
                        break;
                    }
                }
            }
        }
        if(it == spriteCache_.end() || !it->second.valid || !it->second.bitmap)
            return {0, 0};
        return {it->second.bitmap->GetWidth(), it->second.bitmap->GetHeight()};
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

    bool EditorViewport::IsEntitySelected(EntityId id) const {
        if(id == InvalidEntityId) return false;
        return std::find(selectedEntities_.begin(), selectedEntities_.end(), id) != selectedEntities_.end();
    }

    void EditorViewport::SetSelectedEntity(EntityId id, bool notify) {
        if(id != InvalidEntityId && !scene_.HasEntity(id)) {
            id = InvalidEntityId;
        }
        std::vector<EntityId> next;
        if(id != InvalidEntityId) {
            next.push_back(id);
        }
        SetSelectedEntities(next, notify);
    }

    void EditorViewport::SetSelectedEntities(const std::vector<EntityId> &ids, bool notify) {
        std::vector<EntityId> cleaned;
        cleaned.reserve(ids.size());
        for(EntityId id : ids) {
            if(id == InvalidEntityId || !scene_.HasEntity(id)) continue;
            if(std::find(cleaned.begin(), cleaned.end(), id) == cleaned.end()) {
                cleaned.push_back(id);
            }
        }
        const EntityId nextPrimary = cleaned.empty() ? InvalidEntityId : cleaned.back();
        if(cleaned == selectedEntities_ && selectedEntity_ == nextPrimary) return;
        selectedEntities_ = std::move(cleaned);
        selectedEntity_ = nextPrimary;
        clampTileToolToSelection();
        Redraw();
        if(notify && OnSelectionChanged) {
            OnSelectionChanged(selectedEntity_);
        }
    }

    void EditorViewport::ToggleEntitySelection(EntityId id, bool notify) {
        if(id == InvalidEntityId || !scene_.HasEntity(id)) return;
        auto it = std::find(selectedEntities_.begin(), selectedEntities_.end(), id);
        if(it != selectedEntities_.end()) {
            selectedEntities_.erase(it);
        } else {
            selectedEntities_.push_back(id);
        }
        selectedEntity_ = selectedEntities_.empty() ? InvalidEntityId : selectedEntities_.back();
        Redraw();
        if(notify && OnSelectionChanged) {
            OnSelectionChanged(selectedEntity_);
        }
    }

    void EditorViewport::ClearSelection(bool notify) {
        SetSelectedEntities({}, notify);
    }

    void EditorViewport::PruneSelection(bool notify) {
        std::vector<EntityId> kept;
        kept.reserve(selectedEntities_.size());
        for(EntityId id : selectedEntities_) {
            if(scene_.HasEntity(id)) {
                kept.push_back(id);
            }
        }
        SetSelectedEntities(kept, notify);
    }

    // ---- Tilemap painting ---------------------------------------------------

    const TilemapComponent *EditorViewport::GetEditableTilemap() const {
        if(selectedEntity_ == InvalidEntityId)
            return nullptr;

        const Entity *entity = scene_.FindEntity(selectedEntity_);
        if(!entity || !entity->visible || entity->locked || !entity->tilemap)
            return nullptr;

        return TilemapPainter::Paintable(*entity->tilemap) ? &*entity->tilemap : nullptr;
    }

    TilemapComponent *EditorViewport::GetEditableTilemap() {
        return const_cast<TilemapComponent *>(
            static_cast<const EditorViewport *>(this)->GetEditableTilemap());
    }

    bool EditorViewport::SetTilemapToolActive(bool active) {
        if(active && !CanPaintTilemap())
            return false;

        if(!active && dragMode_ == DragMode::PaintTiles)
            CancelTileStroke();

        if(tileToolActive_ == active)
            return tileToolActive_;

        tileToolActive_ = active;
        hoverCell_ = {};
        Redraw();
        if(OnTilemapToolChanged) OnTilemapToolChanged();
        return tileToolActive_;
    }

    void EditorViewport::SetTilemapTool(TilemapTool tool) {
        if(tileTool_ == tool)
            return;

        tileTool_ = tool;
        Redraw();
        if(OnTilemapToolChanged) OnTilemapToolChanged();
    }

    void EditorViewport::SetPaintTile(int tile) {
        const int count = GetTilePaletteCount();
        const int clamped = (count > 0) ? std::max(-1, std::min(tile, count - 1))
                                        : std::max(-1, tile);
        if(paintTile_ == clamped)
            return;

        paintTile_ = clamped;
        Redraw();
        if(OnPaintTileChanged) OnPaintTileChanged(paintTile_);
    }

    int EditorViewport::GetTilePaletteCount() const {
        const TilemapComponent *map = GetEditableTilemap();
        if(!map)
            return 16;

        int imageW = 0;
        int imageH = 0;
        if(!map->tilesetAsset.empty()) {
            if(const CachedSprite *cached = getOrLoadSprite(map->tilesetAsset); cached && cached->bitmap) {
                const auto size = cached->bitmap->GetSize();
                imageW = size.Width;
                imageH = size.Height;
            }
        }

        return TilemapPainter::TilesetTileCount(*map, imageW, imageH, 16);
    }

    void EditorViewport::updatePaletteLayout(float viewW, float viewH) const {
        constexpr int kSwatch = 28;
        constexpr int kGap = 6;
        const float margin = 10.0f;
        const float stripH = static_cast<float>(kSwatch + kGap * 2);
        const float stripY = std::max(0.0f, viewH - stripH - margin);
        const float maxWidth = std::max(0.0f, viewW - margin * 2.0f);

        const int count = tileToolActive_ ? GetTilePaletteCount() : 0;
        paletteLayout_ = TilePaletteLayout::Compute(margin, stripY, maxWidth, count, kSwatch, kGap, 1);
    }

    const TilePaletteLayout &EditorViewport::GetTilePaletteLayout() const {
        const auto size = GetViewportSize();
        updatePaletteLayout(static_cast<float>(size.Width), static_cast<float>(size.Height));
        return paletteLayout_;
    }

    int EditorViewport::HitTestTilePalette(float sx, float sy) const {
        if(!tileToolActive_)
            return -1;
        return GetTilePaletteLayout().HitTest(sx, sy);
    }

    TilemapHit EditorViewport::ScreenToTilemapCell(const TilemapComponent &map, float sx, float sy) const {
        const auto size = GetViewportSize();
        const Entity *entity = scene_.FindEntity(selectedEntity_);
        const Transform world = entity ? scene_.GetWorldTransform(entity->id) : Transform{};

        return TilemapPainter::ScreenToCell(map, world, camera_.panX, camera_.panY, camera_.zoom,
                                            sx, sy,
                                            static_cast<float>(size.Width),
                                            static_cast<float>(size.Height));
    }

    TilemapHit EditorViewport::ScreenToTilemapCell(float sx, float sy) const {
        const TilemapComponent *map = GetEditableTilemap();
        if(!map)
            return {};
        return ScreenToTilemapCell(*map, sx, sy);
    }

    int EditorViewport::paintTileStrokeTo(int col, int row) {
        Entity *entity = scene_.FindEntity(strokeTargetId_);
        if(!entity || !entity->tilemap)
            return 0;

        TilemapComponent &map = *entity->tilemap;
        const int tile = (tileTool_ == TilemapTool::Eraser) ? -1 : paintTile_;
        int changed = 0;

        if(tileTool_ == TilemapTool::Fill) {
            // Redraw the rectangle from the pre-stroke cells on every step, so dragging the far
            // corner moves the preview instead of accumulating coverage.
            map.tiles = strokeBaseline_;
            TilemapPainter::EnsureSize(map);
            changed = TilemapPainter::FillRect(map, strokeAnchor_.col, strokeAnchor_.row,
                                               col, row, tile);
        }
        else if(strokeCell_.col != col || strokeCell_.row != row) {
            // A straight line between mouse samples: a fast drag must not leave gaps.
            changed = TilemapPainter::PaintLine(map, strokeCell_.col, strokeCell_.row, col, row, tile);
        }
        else {
            changed = TilemapPainter::PaintCell(map, col, row, tile) ? 1 : 0;
        }

        strokeCell_ = {col, row};
        strokeChanged_ += changed;
        return changed;
    }

    bool EditorViewport::BeginTileStroke(float sx, float sy) {
        TilemapComponent *map = GetEditableTilemap();
        if(!map)
            return false;

        if(strokeActive_)
            CancelTileStroke();

        strokeTargetId_ = selectedEntity_;
        strokeBaseline_ = map->tiles;
        strokeChanged_ = 0;
        strokeActive_ = true;
        dragMode_ = DragMode::PaintTiles;

        // The press itself paints: a click is a one-cell stroke. Anchor and cursor start on the
        // pressed cell even when it is off the grid (Fill clamps, brush paints nothing yet).
        const TilemapHit hit = ScreenToTilemapCell(*map, sx, sy);
        strokeAnchor_ = {hit.col, hit.row};
        strokeCell_ = strokeAnchor_;
        hoverCell_ = hit;

        if(paintTileStrokeTo(hit.col, hit.row) > 0)
            Redraw();
        return true;
    }

    bool EditorViewport::DragTileStroke(float sx, float sy) {
        if(!strokeActive_)
            return false;

        Entity *entity = scene_.FindEntity(strokeTargetId_);
        if(!entity || !entity->tilemap)
            return false;

        const TilemapHit hit = ScreenToTilemapCell(*entity->tilemap, sx, sy);
        hoverCell_ = hit;

        // A brush drag that wanders off the grid stops painting but keeps its last cell, so it
        // resumes the line when it comes back. Fill is clamped by the Core rectangle fill.
        if(!hit.inside && tileTool_ != TilemapTool::Fill) {
            Redraw();
            return false;
        }

        const int changed = paintTileStrokeTo(hit.col, hit.row);
        Redraw();
        return changed > 0;
    }

    bool EditorViewport::EndTileStroke() {
        if(!strokeActive_)
            return false;

        strokeActive_ = false;
        if(dragMode_ == DragMode::PaintTiles)
            dragMode_ = DragMode::None;

        const int changed = strokeChanged_;
        strokeChanged_ = 0;
        strokeBaseline_.clear();
        Redraw();

        // One callback per stroke: the editor records exactly one undoable edit for the drag.
        if(changed > 0 && OnTileStrokeCommitted)
            OnTileStrokeCommitted();

        return changed > 0;
    }

    bool EditorViewport::CancelTileStroke() {
        if(!strokeActive_)
            return false;

        strokeActive_ = false;
        if(dragMode_ == DragMode::PaintTiles)
            dragMode_ = DragMode::None;

        if(Entity *entity = scene_.FindEntity(strokeTargetId_)) {
            if(entity->tilemap)
                entity->tilemap->tiles = strokeBaseline_;
        }

        strokeBaseline_.clear();
        strokeChanged_ = 0;
        Redraw();
        return true;
    }

    void EditorViewport::clampTileToolToSelection() {
        if(!tileToolActive_)
            return;

        if(CanPaintTilemap())
            return;

        if(dragMode_ == DragMode::PaintTiles) {
            CancelTileStroke();
        }

        tileToolActive_ = false;
        hoverCell_ = {};
        if(OnTilemapToolChanged) OnTilemapToolChanged();
    }

    std::vector<EntityId> EditorViewport::DuplicateSelectedEntities(bool notify) {
        if(selectedEntities_.empty()) return {};
        std::vector<EntityId> roots;
        roots.reserve(selectedEntities_.size());
        for(EntityId id : selectedEntities_) {
            if(!scene_.HasEntity(id)) continue;
            bool ancestorSelected = false;
            for(EntityId other : selectedEntities_) {
                if(other != id && scene_.IsAncestorOf(other, id)) {
                    ancestorSelected = true;
                    break;
                }
            }
            if(!ancestorSelected) {
                roots.push_back(id);
            }
        }

        std::vector<EntityId> created;
        created.reserve(roots.size());
        for(EntityId id : roots) {
            const EntityId copyId = scene_.DuplicateEntity(id, true);
            if(copyId != InvalidEntityId) {
                created.push_back(copyId);
            }
        }
        if(!created.empty()) {
            SetSelectedEntities(created, notify);
        }
        return created;
    }

    bool EditorViewport::DeleteSelectedEntities(bool notify) {
        if(selectedEntities_.empty()) return false;

        if(selectedEntities_.size() == 1) {
            const EntityId id = selectedEntities_.front();
            const Entity *entity = scene_.FindEntity(id);
            const EntityId parentId = entity ? entity->parentId : InvalidEntityId;
            if(!scene_.RemoveEntity(id, true)) return false;
            const EntityId nextSelection = scene_.HasEntity(parentId) ? parentId : InvalidEntityId;
            SetSelectedEntity(nextSelection, notify);
            return true;
        }

        const std::vector<EntityId> toRemove = selectedEntities_;
        bool removedAny = false;
        for(EntityId id : toRemove) {
            if(scene_.HasEntity(id)) {
                removedAny = scene_.RemoveEntity(id, true) || removedAny;
            }
        }
        if(removedAny) {
            ClearSelection(notify);
        }
        return removedAny;
    }

    bool EditorViewport::isShiftDown() const {
        using Mod = Gorgon::Input::Keyboard::Modifier;
        return shiftHeld_ || static_cast<bool>(Gorgon::Input::Keyboard::CurrentModifier & Mod(Mod::Shift));
    }

    void EditorViewport::captureGroupMoveTargets() {
        dragStartTransforms_.clear();
        dragStartTransforms_.reserve(selectedEntities_.size());
        for(EntityId id : selectedEntities_) {
            const Entity *e = scene_.FindEntity(id);
            if(!e || e->locked) continue;
            bool unlockedAncestorSelected = false;
            for(EntityId other : selectedEntities_) {
                if(other == id) continue;
                const Entity *anc = scene_.FindEntity(other);
                if(anc && !anc->locked && scene_.IsAncestorOf(other, id)) {
                    unlockedAncestorSelected = true;
                    break;
                }
            }
            if(!unlockedAncestorSelected) {
                dragStartTransforms_.push_back({id, e->transform});
            }
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

    Gorgon::Geometry::Pointf EditorViewport::GetMouseWorldPosition() const {
        const auto sz = GetViewportSize();
        return camera_.ScreenToWorld(
            static_cast<float>(lastMouse_.X), static_cast<float>(lastMouse_.Y),
            static_cast<float>(sz.Width), static_cast<float>(sz.Height)
        );
    }

    bool EditorViewport::IsMouseButtonDown(Gorgon::Input::Mouse::Button btn) const {
        using Btn = Gorgon::Input::Mouse::Button;
        const int bits = static_cast<int>(btn) & 0x1FF;

        if(bits & static_cast<int>(Btn::Left))   return mouseDown_[0];
        if(bits & static_cast<int>(Btn::Right))  return mouseDown_[1];
        if(bits & static_cast<int>(Btn::Middle)) return mouseDown_[2];

        return false;
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
        if(entity.spriteAnim.has_value()) {
            const auto &anim = *entity.spriteAnim;
            if(anim.frameW > 0 && anim.frameH > 0) {
                return {static_cast<float>(anim.frameW), static_cast<float>(anim.frameH)};
            }
        }
        if(entity.tilemap.has_value()) {
            const auto &map = *entity.tilemap;
            if(map.cols > 0 && map.rows > 0) {
                return {static_cast<float>(map.cols) * map.tileW,
                        static_cast<float>(map.rows) * map.tileH};
            }
        }
        if(entity.text.has_value()) {
            const auto &text = *entity.text;
            const float width = static_cast<float>(std::max<std::size_t>(1, text.text.size())) *
                                text.fontSize * 0.6f;
            return {std::max(16.0f, width), text.fontSize * 1.4f};
        }
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

    Gorgon::Geometry::Pointf EditorViewport::ComputeRotateHandleScreenPos(const Entity &entity, float viewW, float viewH) const {
        const auto wc = ComputeEntityWorldCorners(entity);
        const auto sc0 = camera_.WorldToScreen(wc[0].X, wc[0].Y, viewW, viewH);
        const auto sc1 = camera_.WorldToScreen(wc[1].X, wc[1].Y, viewW, viewH);
        const Gorgon::Geometry::Pointf topMid{(sc0.X + sc1.X) * 0.5f, (sc0.Y + sc1.Y) * 0.5f};

        const Transform wt = scene_.GetWorldTransform(entity.id);
        const auto center = camera_.WorldToScreen(wt.x, wt.y, viewW, viewH);
        float ux = topMid.X - center.X;
        float uy = topMid.Y - center.Y;
        const float len = std::sqrt(ux * ux + uy * uy);
        if(len > 1e-3f) {
            ux /= len;
            uy /= len;
        } else {
            const float rad = wt.rotation * kDegToRad;
            ux = std::sin(rad);
            uy = -std::cos(rad);
        }
        return {topMid.X + ux * kRotateStemPx, topMid.Y + uy * kRotateStemPx};
    }

    float EditorViewport::snapValue(float value) const {
        if(!gridSnap_) return value;
        const int gs = std::max(1, scene_.settings.gridSize);
        return std::round(value / static_cast<float>(gs)) * static_cast<float>(gs);
    }

    Gorgon::Geometry::Pointf EditorViewport::ScreenToWorldSnapped(float sx, float sy) const {
        const auto sz = GetViewportSize();
        const auto wp = camera_.ScreenToWorld(sx, sy, static_cast<float>(sz.Width), static_cast<float>(sz.Height));
        return {snapValue(wp.X), snapValue(wp.Y)};
    }

    void EditorViewport::SetDropPreview(float sx, float sy, const std::string &label, bool isScript) {
        const auto wp = ScreenToWorldSnapped(sx, sy);
        dropPreview_.active = true;
        dropPreview_.isScript = isScript;
        dropPreview_.label = label;
        dropPreview_.worldX = wp.X;
        dropPreview_.worldY = wp.Y;
        dropPreview_.targetEntityId = isScript ? HitTestEntity(sx, sy) : InvalidEntityId;
        Redraw();
    }

    void EditorViewport::ClearDropPreview() {
        if(!dropPreview_.active) return;
        dropPreview_ = DropPreview{};
        Redraw();
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

    std::vector<EntityId> EditorViewport::HitTestMarquee(float sx1, float sy1, float sx2, float sy2) const {
        const auto sz = GetViewportSize();
        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);
        const auto w1 = camera_.ScreenToWorld(std::min(sx1, sx2), std::min(sy1, sy2), viewW, viewH);
        const auto w2 = camera_.ScreenToWorld(std::max(sx1, sx2), std::max(sy1, sy2), viewW, viewH);
        const float mMinX = std::min(w1.X, w2.X);
        const float mMaxX = std::max(w1.X, w2.X);
        const float mMinY = std::min(w1.Y, w2.Y);
        const float mMaxY = std::max(w1.Y, w2.Y);

        std::vector<EntityId> hits;
        for(EntityId id : scene_.GetRenderOrder()) {
            const Entity *e = scene_.FindEntity(id);
            if(!e || !e->visible || e->locked) continue;
            const auto corners = ComputeEntityWorldCorners(*e);
            float eMinX = std::numeric_limits<float>::max();
            float eMaxX = std::numeric_limits<float>::lowest();
            float eMinY = std::numeric_limits<float>::max();
            float eMaxY = std::numeric_limits<float>::lowest();
            for(const auto &c : corners) {
                eMinX = std::min(eMinX, c.X);
                eMaxX = std::max(eMaxX, c.X);
                eMinY = std::min(eMinY, c.Y);
                eMaxY = std::max(eMaxY, c.Y);
            }
            if(eMaxX >= mMinX && eMinX <= mMaxX && eMaxY >= mMinY && eMinY <= mMaxY) {
                hits.push_back(e->id);
            }
        }
        return hits;
    }

    GizmoHandle EditorViewport::HitTestGizmo(float sx, float sy) const {
        if(selectedEntities_.size() != 1 || selectedEntity_ == InvalidEntityId) return GizmoHandle::None;
        const Entity *e = scene_.FindEntity(selectedEntity_);
        if(!e || !e->visible || e->locked) return GizmoHandle::None;

        const auto sz = GetViewportSize();
        const float viewW = static_cast<float>(sz.Width);
        const float viewH = static_cast<float>(sz.Height);
        const float hitSq = kHandleHitRadiusPx * kHandleHitRadiusPx;

        // 0. Rotation handle (above top-center edge)
        const auto rotPos = ComputeRotateHandleScreenPos(*e, viewW, viewH);
        if(DistSq(sx, sy, rotPos.X, rotPos.Y) <= hitSq) {
            return GizmoHandle::Rotate;
        }

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

        // Recorded before any early return: play-in-editor reads this even with editing disabled.
        if(hasButton(btn, Btn::Left))   mouseDown_[0] = true;
        if(hasButton(btn, Btn::Right))  mouseDown_[1] = true;
        if(hasButton(btn, Btn::Middle)) mouseDown_[2] = true;

        // Middle mouse or Space + Left mouse => Pan
        if(hasButton(btn, Btn::Middle) || (hasButton(btn, Btn::Left) && spaceHeld_)) {
            dragMode_ = DragMode::Pan;
            return;
        }

        if(!hasButton(btn, Btn::Left)) return;
        if(!editEnabled_) return;

        const float sx = static_cast<float>(pt.X);
        const float sy = static_cast<float>(pt.Y);

        // Tile painting owns the left button while armed: a click on the palette strip picks a
        // tile, anywhere else starts a stroke. Selection and gizmos stay out of the way.
        if(tileToolActive_) {
            const int swatch = HitTestTilePalette(sx, sy);
            if(swatch >= 0) {
                SetPaintTile(swatch);
                return;
            }
            BeginTileStroke(sx, sy);
            return;
        }

        dragStartWorld_ = camera_.ScreenToWorld(sx, sy, viewW, viewH);

        // Check gizmo handles on single selection first
        const GizmoHandle handle = HitTestGizmo(sx, sy);
        if(handle != GizmoHandle::None) {
            if(const Entity *e = scene_.FindEntity(selectedEntity_)) {
                activeHandle_ = handle;
                dragStartLocalTransform_ = e->transform;
                dragStartWorldTransform_ = scene_.GetWorldTransform(e->id);
                dragStartTransforms_.clear();
                dragStartTransforms_.push_back({e->id, e->transform});
                pendingSingleSelect_ = InvalidEntityId;
                dragMoved_ = false;
                if(handle == GizmoHandle::TranslateX) {
                    dragMode_ = DragMode::MoveEntityX;
                } else if(handle == GizmoHandle::TranslateY) {
                    dragMode_ = DragMode::MoveEntityY;
                } else if(handle == GizmoHandle::Rotate) {
                    dragMode_ = DragMode::RotateEntity;
                    dragStartAngleDeg_ = std::atan2(
                        dragStartWorld_.Y - dragStartWorldTransform_.y,
                        dragStartWorld_.X - dragStartWorldTransform_.x
                    ) * kRadToDeg;
                } else {
                    dragMode_ = DragMode::ScaleEntity;
                }
                if(OnInteractionChanged) OnInteractionChanged(true);
                return;
            }
        }

        // Otherwise pick entity under cursor
        const bool shift = isShiftDown();
        const EntityId hit = HitTestEntity(sx, sy);
        if(hit != InvalidEntityId) {
            if(shift) {
                ToggleEntitySelection(hit, true);
                dragMode_ = DragMode::None;
                pendingSingleSelect_ = InvalidEntityId;
                dragMoved_ = false;
                return;
            }

            if(!IsEntitySelected(hit)) {
                SetSelectedEntity(hit, true);
                pendingSingleSelect_ = InvalidEntityId;
            } else {
                pendingSingleSelect_ = (selectedEntities_.size() > 1) ? hit : InvalidEntityId;
            }

            if(const Entity *e = scene_.FindEntity(hit)) {
                dragStartLocalTransform_ = e->transform;
                dragStartWorldTransform_ = scene_.GetWorldTransform(e->id);
                dragMoved_ = false;
                captureGroupMoveTargets();
                if(!dragStartTransforms_.empty()) {
                    dragMode_ = DragMode::MoveEntity;
                    if(OnInteractionChanged) OnInteractionChanged(true);
                }
            }
        } else {
            // Empty space: begin marquee box selection (or clear on plain click)
            dragMode_ = DragMode::MarqueeSelect;
            marqueeStartScreen_ = {sx, sy};
            marqueeEndScreen_ = {sx, sy};
            marqueeAdditive_ = shift;
            marqueeBaseSelection_ = selectedEntities_;
            marqueeDragged_ = false;
            pendingSingleSelect_ = InvalidEntityId;
            dragMoved_ = false;
            if(!marqueeAdditive_) {
                ClearSelection(true);
            }
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

        if(dragMode_ == DragMode::PaintTiles) {
            DragTileStroke(sx, sy);
            if(OnStatusChanged) {
                OnStatusChanged(worldNow.X, worldNow.Y, camera_.zoom);
            }
            return;
        }

        // Track the cell under the cursor while the tool is armed so the highlight follows it.
        if(tileToolActive_ && editEnabled_) {
            const TilemapHit hover = ScreenToTilemapCell(sx, sy);
            if(hover != hoverCell_) {
                hoverCell_ = hover;
                Redraw();
            }
        }

        if(dragMode_ == DragMode::MarqueeSelect) {
            marqueeEndScreen_ = {sx, sy};
            if(!marqueeDragged_ && DistSq(marqueeStartScreen_.X, marqueeStartScreen_.Y, sx, sy) > 4.0f) {
                marqueeDragged_ = true;
            }
            if(marqueeDragged_) {
                const auto hits = HitTestMarquee(marqueeStartScreen_.X, marqueeStartScreen_.Y, sx, sy);
                std::vector<EntityId> nextSel;
                if(marqueeAdditive_) {
                    nextSel = marqueeBaseSelection_;
                    for(EntityId id : hits) {
                        if(std::find(nextSel.begin(), nextSel.end(), id) == nextSel.end()) {
                            nextSel.push_back(id);
                        }
                    }
                } else {
                    nextSel = hits;
                }
                SetSelectedEntities(nextSel, true);
                Redraw();
            }
            if(OnStatusChanged) {
                OnStatusChanged(worldNow.X, worldNow.Y, camera_.zoom);
            }
            return;
        }

        if(dragMode_ == DragMode::MoveEntity || dragMode_ == DragMode::MoveEntityX || dragMode_ == DragMode::MoveEntityY) {
            float dwx = worldNow.X - dragStartWorld_.X;
            float dwy = worldNow.Y - dragStartWorld_.Y;
            if(dragMode_ == DragMode::MoveEntityX) dwy = 0.0f;
            if(dragMode_ == DragMode::MoveEntityY) dwx = 0.0f;

            bool anyMoved = false;
            if(dragStartTransforms_.size() == 1) {
                const auto &entry = dragStartTransforms_[0];
                Entity *e = scene_.FindEntity(entry.id);
                if(e && !e->locked) {
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

                    float newX = entry.startLocal.x + dlx;
                    float newY = entry.startLocal.y + dly;
                    if(dragMode_ != DragMode::MoveEntityY) newX = snapValue(newX);
                    if(dragMode_ != DragMode::MoveEntityX) newY = snapValue(newY);

                    if(e->transform.x != newX || e->transform.y != newY) {
                        e->transform.x = newX;
                        e->transform.y = newY;
                        anyMoved = true;
                    }
                }
            } else if(dragStartTransforms_.size() > 1) {
                // Snap the anchor entity's target world position and apply the resulting delta to all selected entities.
                float targetWx = dragStartWorldTransform_.x + dwx;
                float targetWy = dragStartWorldTransform_.y + dwy;
                if(dragMode_ != DragMode::MoveEntityY) targetWx = snapValue(targetWx);
                if(dragMode_ != DragMode::MoveEntityX) targetWy = snapValue(targetWy);
                const float effDwx = targetWx - dragStartWorldTransform_.x;
                const float effDwy = targetWy - dragStartWorldTransform_.y;

                for(const auto &entry : dragStartTransforms_) {
                    Entity *e = scene_.FindEntity(entry.id);
                    if(!e || e->locked) continue;
                    float dlx = effDwx;
                    float dly = effDwy;
                    if(e->parentId != InvalidEntityId) {
                        const Transform pwt = scene_.GetWorldTransform(e->parentId);
                        const float rad = -pwt.rotation * kDegToRad;
                        const float c = std::cos(rad);
                        const float s = std::sin(rad);
                        const float rx = effDwx * c - effDwy * s;
                        const float ry = effDwx * s + effDwy * c;
                        dlx = (std::abs(pwt.scaleX) > 1e-4f) ? (rx / pwt.scaleX) : rx;
                        dly = (std::abs(pwt.scaleY) > 1e-4f) ? (ry / pwt.scaleY) : ry;
                    }
                    const float newX = entry.startLocal.x + dlx;
                    const float newY = entry.startLocal.y + dly;
                    if(e->transform.x != newX || e->transform.y != newY) {
                        e->transform.x = newX;
                        e->transform.y = newY;
                        anyMoved = true;
                    }
                }
            }

            if(anyMoved) {
                dragMoved_ = true;
                Redraw();
                if(OnSceneModified) OnSceneModified();
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
        } else if(dragMode_ == DragMode::RotateEntity) {
            Entity *e = scene_.FindEntity(selectedEntity_);
            if(e && !e->locked) {
                const float dx = worldNow.X - dragStartWorldTransform_.x;
                const float dy = worldNow.Y - dragStartWorldTransform_.y;
                if(dx * dx + dy * dy > 1e-4f) {
                    const float currentAngleDeg = std::atan2(dy, dx) * kRadToDeg;
                    float deltaDeg = currentAngleDeg - dragStartAngleDeg_;
                    while(deltaDeg > 180.0f) deltaDeg -= 360.0f;
                    while(deltaDeg < -180.0f) deltaDeg += 360.0f;

                    float newRot = dragStartLocalTransform_.rotation + deltaDeg;
                    if(gridSnap_) {
                        newRot = std::round(newRot / kRotateSnapDeg) * kRotateSnapDeg;
                    }
                    while(newRot > 180.0f) newRot -= 360.0f;
                    while(newRot <= -180.0f) newRot += 360.0f;

                    if(e->transform.rotation != newRot) {
                        e->transform.rotation = newRot;
                        Redraw();
                        if(OnSceneModified) OnSceneModified();
                    }
                }
            }
        }

        if(OnStatusChanged) {
            OnStatusChanged(worldNow.X, worldNow.Y, camera_.zoom);
        }
    }

    void EditorViewport::HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
        using Btn = Gorgon::Input::Mouse::Button;

        // Mirror the tracking in HandleMouseDown so the play session always sees the release.
        lastMouse_ = pt;
        const int bits = static_cast<int>(btn) & 0x1FF;
        if(bits & static_cast<int>(Btn::Left))   mouseDown_[0] = false;
        if(bits & static_cast<int>(Btn::Right))  mouseDown_[1] = false;
        if(bits & static_cast<int>(Btn::Middle)) mouseDown_[2] = false;

        if(dragMode_ == DragMode::PaintTiles || strokeActive_) {
            EndTileStroke();
            return;
        }

        if(dragMode_ == DragMode::MarqueeSelect) {
            const bool wasDragged = marqueeDragged_;
            const bool wasAdditive = marqueeAdditive_;
            dragMode_ = DragMode::None;
            marqueeDragged_ = false;
            marqueeBaseSelection_.clear();
            if(!wasDragged && !wasAdditive) {
                ClearSelection(true);
            }
            Redraw();
            return;
        }

        const bool wasEditing = dragMode_ == DragMode::MoveEntity ||
                                dragMode_ == DragMode::MoveEntityX ||
                                dragMode_ == DragMode::MoveEntityY ||
                                dragMode_ == DragMode::ScaleEntity ||
                                dragMode_ == DragMode::RotateEntity;
        const EntityId pendingSingle = pendingSingleSelect_;
        const bool moved = dragMoved_;
        dragMode_ = DragMode::None;
        activeHandle_ = GizmoHandle::None;
        pendingSingleSelect_ = InvalidEntityId;
        dragMoved_ = false;
        dragStartTransforms_.clear();
        if(wasEditing && OnInteractionChanged) OnInteractionChanged(false);
        if(pendingSingle != InvalidEntityId && !moved) {
            SetSelectedEntity(pendingSingle, true);
        }
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

    void EditorViewport::drawFilledCircle(float cx, float cy, float radius, Gorgon::Graphics::RGBAf color) {
        constexpr int kSegments = 16;
        const Gorgon::Geometry::Pointf center{cx, cy};
        Gorgon::Geometry::Pointf prev{cx + radius, cy};
        for(int i = 1; i <= kSegments; ++i) {
            const float a = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
            const Gorgon::Geometry::Pointf curr{cx + std::cos(a) * radius, cy + std::sin(a) * radius};
            gfxLayer_.Draw(center, prev, curr, center, color);
            prev = curr;
        }
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

            bool drewContent = false;

            if(e->spriteAnim.has_value()) {
                // Show the preview frame the timeline selected, or the frame the runtime is on.
                const auto &anim = *e->spriteAnim;
                const auto *cached = getOrLoadSprite(anim.asset);
                if(cached && cached->bitmap) {
                    const auto size = cached->bitmap->GetSize();
                    const float imageW = static_cast<float>(std::max(1, size.Width));
                    const float imageH = static_cast<float>(std::max(1, size.Height));
                    const int count = AnimationTimeline::FrameCount(anim);
                    int frame = GetDisplayedAnimationFrame(e->id);
                    if(frame < 0)
                        frame = 0;
                    frame = count > 0 ? std::clamp(frame, 0, count - 1) : 0;
                    const auto rect = AnimationTimeline::FrameSourceRect(anim, frame,
                                                                         size.Width, size.Height);

                    const Gorgon::Geometry::Pointf t1{rect.x / imageW, rect.y / imageH};
                    const Gorgon::Geometry::Pointf t2{(rect.x + rect.w) / imageW, rect.y / imageH};
                    const Gorgon::Geometry::Pointf t3{(rect.x + rect.w) / imageW, (rect.y + rect.h) / imageH};
                    const Gorgon::Geometry::Pointf t4{rect.x / imageW, (rect.y + rect.h) / imageH};
                    gfxLayer_.Draw(*cached->bitmap, sc[0], sc[1], sc[2], sc[3],
                                   t1, t2, t3, t4, ToRGBAf(anim.tint));
                    drewContent = true;
                }
            }
            else if(e->sprite.has_value() && !e->sprite->asset.empty()) {
                if(const auto *cached = getOrLoadSprite(e->sprite->asset); cached && cached->bitmap) {
                    gfxLayer_.Draw(*cached->bitmap, sc[0], sc[1], sc[2], sc[3], ToRGBAf(e->sprite->tint));
                    drewContent = true;
                }
            }

            if(!drewContent && e->tilemap.has_value()) {
                const auto &map = *e->tilemap;
                const auto *tileset = map.tilesetAsset.empty() ? nullptr : getOrLoadSprite(map.tilesetAsset);
                const Transform wt = scene_.GetWorldTransform(e->id);
                const float rad = wt.rotation * kDegToRad;
                const float c = std::cos(rad);
                const float s = std::sin(rad);
                auto tilePoint = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
                    const float wx = wt.x + (lx * wt.scaleX) * c - (ly * wt.scaleY) * s;
                    const float wy = wt.y + (lx * wt.scaleX) * s + (ly * wt.scaleY) * c;
                    return camera_.WorldToScreen(wx, wy, viewW, viewH);
                };

                const int cols = std::max(0, map.cols);
                const int rows = std::max(0, map.rows);
                const float originX = -static_cast<float>(cols) * map.tileW * 0.5f;
                const float originY = -static_cast<float>(rows) * map.tileH * 0.5f;

                for(int row = 0; row < rows; ++row) {
                    for(int col = 0; col < cols; ++col) {
                        const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) +
                                                  static_cast<std::size_t>(col);
                        if(index >= map.tiles.size() || map.tiles[index] < 0)
                            continue;

                        const float lx = originX + static_cast<float>(col) * map.tileW;
                        const float ly = originY + static_cast<float>(row) * map.tileH;
                        const Gorgon::Geometry::Pointf q[4] = {
                            tilePoint(lx, ly),
                            tilePoint(lx + map.tileW, ly),
                            tilePoint(lx + map.tileW, ly + map.tileH),
                            tilePoint(lx, ly + map.tileH)
                        };

                        const int tile = map.tiles[index];
                        if(tileset && tileset->bitmap) {
                            const auto size = tileset->bitmap->GetSize();
                            const int perRow = std::max(1, map.tilesetCols);
                            const int tx = (tile % perRow) * static_cast<int>(map.tileW);
                            const int ty = (tile / perRow) * static_cast<int>(map.tileH);
                            const float imageW = static_cast<float>(std::max(1, size.Width));
                            const float imageH = static_cast<float>(std::max(1, size.Height));
                            const Gorgon::Geometry::Pointf t1{tx / imageW, ty / imageH};
                            const Gorgon::Geometry::Pointf t2{(tx + map.tileW) / imageW, ty / imageH};
                            const Gorgon::Geometry::Pointf t3{(tx + map.tileW) / imageW, (ty + map.tileH) / imageH};
                            const Gorgon::Geometry::Pointf t4{tx / imageW, (ty + map.tileH) / imageH};
                            gfxLayer_.Draw(*tileset->bitmap, q[0], q[1], q[2], q[3],
                                           t1, t2, t3, t4, Gorgon::Graphics::RGBAf(1.0f));
                        }
                        else {
                            gfxLayer_.Draw(q[0], q[1], q[2], q[3], TilePreviewColor(tile));
                        }
                    }
                }
                drewContent = true;
            }

            if(!drewContent && e->text.has_value()) {
                const auto &label = *e->text;
                bool drew = false;
                try {
                    auto printer = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::Info);
                    printer.SetColor(ToRGBAf(label.color));
                    const auto textSize = printer.GetSize(label.text);
                    const Transform wt = scene_.GetWorldTransform(e->id);
                    const auto center = camera_.WorldToScreen(wt.x, wt.y, viewW, viewH);
                    int x = static_cast<int>(center.X);
                    if(label.align == TextAlign::Center)
                        x -= textSize.Width / 2;
                    else if(label.align == TextAlign::Right)
                        x -= textSize.Width;
                    const int y = static_cast<int>(center.Y) - textSize.Height / 2;
                    const auto align = label.align == TextAlign::Center
                        ? Gorgon::Graphics::TextAlignment::Center
                        : (label.align == TextAlign::Right
                            ? Gorgon::Graphics::TextAlignment::Right
                            : Gorgon::Graphics::TextAlignment::Left);
                    printer.PrintNoWrap(gfxLayer_, label.text, x, y, std::max(1, textSize.Width), align);
                    drew = true;
                }
                catch(...) {
                    // No widget theme active (headless selftest): fall through to the placeholder.
                    drew = false;
                }
                if(drew)
                    drewContent = true;
            }

            if(!drewContent) {
                const auto fill = FallbackColorForEntity(*e);
                gfxLayer_.Draw(sc[0], sc[1], sc[2], sc[3], fill);
                drawRectOutline(sc, 1.5f, Gorgon::Graphics::RGBAf(1.0f, 1.0f, 1.0f, 0.35f));
            }
        }
    }

    void EditorViewport::drawLighting(float viewW, float viewH) {
        lightmapDrawn_ = false;

        if(!lightingPreviewEnabled_) {
            lightmap_.Reset();
            if(lightmapTexture_.IsReady())
                lightmapTexture_.Clear();
            return;
        }

        // The map covers exactly the visible world rect at roughly one sample per 16 screen pixels,
        // so panning and zooming never change how soft a light looks.
        const auto bounds = CameraWorldBounds(camera_.panX, camera_.panY, camera_.zoom, viewW, viewH);
        int mapWidth = 0;
        int mapHeight = 0;
        LightmapGridSize(viewW, viewH, mapWidth, mapHeight);

        LightmapOptions options;
        options.ambient = lightAmbient_;
        options.occluderSize = [this](EntityId id) -> Gorgon::Geometry::Size {
            const Entity *entity = scene_.FindEntity(id);
            if(!entity)
                return {0, 0};

            const auto size = ComputeEntityBaseSize(*entity);
            return {int(size.X + 0.5f), int(size.Y + 0.5f)};
        };

        // No light in the scene: nothing is built, uploaded or drawn, which is what keeps editing an
        // unlit scene byte-for-byte and frame-for-frame what it was before lighting existed.
        if(!BuildSceneLightmap(scene_, bounds, mapWidth, mapHeight, lightmap_, options)) {
            if(lightmapTexture_.IsReady())
                lightmapTexture_.Clear();
            return;
        }

        lightmapTexture_.Upload(lightmap_);
        lightmapTexture_.Draw(gfxLayer_, viewW, viewH);
        lightmapDrawn_ = true;
    }

    void EditorViewport::drawLightGizmos(float viewW, float viewH) {
        lightGizmoCount_ = 0;

        for(const auto &entity : scene_.GetEntities()) {
            if(!entity.light.has_value() || !entity.visible)
                continue;

            ++lightGizmoCount_;

            const auto &light = *entity.light;
            const Transform world = scene_.GetWorldTransform(entity.id);
            const auto center = camera_.WorldToScreen(world.x, world.y, viewW, viewH);
            const float radiusPx = std::max(4.0f, light.radius * camera_.zoom);

            const auto color = ToRGBAf(light.color);
            const bool selected = (entity.id == selectedEntity_);
            const float ringAlpha = selected ? 1.0f : 0.85f;
            const Gorgon::Graphics::RGBAf ring(color.R, color.G, color.B, ringAlpha);
            const Gorgon::Graphics::RGBAf faint(color.R, color.G, color.B, 0.30f);

            // Radius ring: what the light reaches, in the same units the lightmap uses.
            constexpr int kSegments = 48;
            Gorgon::Geometry::Pointf prev{center.X + radiusPx, center.Y};
            for(int i = 1; i <= kSegments; ++i) {
                const float angle = 2.0f * kPi * float(i) / float(kSegments);
                const Gorgon::Geometry::Pointf curr{center.X + std::cos(angle) * radiusPx,
                                                    center.Y + std::sin(angle) * radiusPx};
                drawLine(prev.X, prev.Y, curr.X, curr.Y, selected ? 2.0f : 1.2f, ring);
                prev = curr;
            }

            if(light.type == LightType::Spot) {
                const float half = std::clamp(light.coneAngle, 1.0f, 360.0f) * 0.5f * kDegToRad;
                const float direction = light.direction * kDegToRad;

                // The two cone edges from the light out to its radius.
                for(int side = -1; side <= 1; side += 2) {
                    const float angle = direction + half * float(side);
                    drawLine(center.X, center.Y,
                             center.X + std::cos(angle) * radiusPx,
                             center.Y + std::sin(angle) * radiusPx,
                             1.5f, ring);
                }

                // Arc across the cone, so the beam's width reads at a glance.
                constexpr int kArcSegments = 24;
                Gorgon::Geometry::Pointf arcPrev{center.X + std::cos(direction - half) * radiusPx,
                                                 center.Y + std::sin(direction - half) * radiusPx};
                for(int i = 1; i <= kArcSegments; ++i) {
                    const float angle = direction - half + 2.0f * half * float(i) / float(kArcSegments);
                    const Gorgon::Geometry::Pointf curr{center.X + std::cos(angle) * radiusPx,
                                                        center.Y + std::sin(angle) * radiusPx};
                    drawLine(arcPrev.X, arcPrev.Y, curr.X, curr.Y, 1.2f, faint);
                    arcPrev = curr;
                }
            }

            // Emitter marker plus a direction tick, so a light with radius 0 is still visible.
            drawFilledCircle(center.X, center.Y, selected ? 5.0f : 4.0f, ring);
            const float dir = light.direction * kDegToRad;
            drawLine(center.X, center.Y,
                     center.X + std::cos(dir) * 14.0f,
                     center.Y + std::sin(dir) * 14.0f,
                     1.5f, ring);

            // Shadow casters are marked with a small square so the flag is visible without the
            // Inspector: it reads as the box a shadow is projected from.
            if(light.castShadows) {
                const std::array<Gorgon::Geometry::Pointf, 4> box = {
                    Gorgon::Geometry::Pointf{center.X - 9.0f, center.Y - 9.0f},
                    Gorgon::Geometry::Pointf{center.X - 3.0f, center.Y - 9.0f},
                    Gorgon::Geometry::Pointf{center.X - 3.0f, center.Y - 3.0f},
                    Gorgon::Geometry::Pointf{center.X - 9.0f, center.Y - 3.0f}
                };
                gfxLayer_.Draw(box[0], box[1], box[2], box[3], ring);
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

    void EditorViewport::drawParticles(float viewW, float viewH) {
        const auto order = scene_.GetRenderOrder();
        for(EntityId id : order) {
            const Entity *e = scene_.FindEntity(id);
            if(!e || !e->visible || !e->particles.has_value())
                continue;

            const auto &emitter = *e->particles;
            const Transform wt = scene_.GetWorldTransform(id);
            const float rad = wt.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);

            auto toScreen = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
                const float wx = wt.x + (lx * wt.scaleX) * c - (ly * wt.scaleY) * s;
                const float wy = wt.y + (lx * wt.scaleX) * s + (ly * wt.scaleY) * c;
                return camera_.WorldToScreen(wx, wy, viewW, viewH);
            };

            auto mix = [](std::uint8_t a, std::uint8_t b, float t) {
                const float fa = a / 255.0f;
                const float fb = b / 255.0f;
                return fa + (fb - fa) * t;
            };

            const std::vector<Particle> *live = playSession_ ? playSession_->GetParticles(id) : nullptr;
            if(live) {
                // Live simulation (play-in-editor): draw every particle, faded over its lifetime.
                const float size = std::max(0.5f, emitter.size);
                for(const auto &particle : *live) {
                    const float t = particle.life > 0.0f
                        ? std::clamp(particle.age / particle.life, 0.0f, 1.0f)
                        : 1.0f;
                    const Gorgon::Graphics::RGBAf color(
                        mix(emitter.startColor.r, emitter.endColor.r, t),
                        mix(emitter.startColor.g, emitter.endColor.g, t),
                        mix(emitter.startColor.b, emitter.endColor.b, t),
                        mix(emitter.startColor.a, emitter.endColor.a, t)
                    );
                    const float half = size * 0.5f;
                    const auto a = toScreen(particle.x - half, particle.y - half);
                    const auto b = toScreen(particle.x + half, particle.y - half);
                    const auto d = toScreen(particle.x + half, particle.y + half);
                    const auto f = toScreen(particle.x - half, particle.y + half);
                    gfxLayer_.Draw(a, b, d, f, color);
                }
            }

            // Emitter marker, always visible while editing so an emitter can be selected and moved.
            const auto center = toScreen(0.0f, 0.0f);
            const Gorgon::Graphics::RGBAf marker(1.0f, 0.85f, 0.35f, emitter.active ? 0.95f : 0.45f);
            drawLine(center.X - 7.0f, center.Y, center.X + 7.0f, center.Y, 1.5f, marker);
            drawLine(center.X, center.Y - 7.0f, center.X, center.Y + 7.0f, 1.5f, marker);
            drawLine(center.X - 5.0f, center.Y - 5.0f, center.X + 5.0f, center.Y + 5.0f, 1.0f, marker);
            drawLine(center.X - 5.0f, center.Y + 5.0f, center.X + 5.0f, center.Y - 5.0f, 1.0f, marker);
        }
    }

    void EditorViewport::drawCameraFrustum(float viewW, float viewH) {
        for(const auto &entity : scene_.GetEntities()) {
            if(!entity.camera.has_value() || !entity.camera->active)
                continue;

            // The component's view is the viewport size divided by its zoom, centred on the follow
            // target (or the camera entity itself). Drawn as a wireframe so a designer can see what
            // the game camera will show without entering play.
            const EntityId followId = entity.camera->followTarget;
            const EntityId centerId = (followId != InvalidEntityId && scene_.HasEntity(followId))
                ? followId : entity.id;
            const Transform centerWorld = scene_.GetWorldTransform(centerId);
            const float zoom = std::max(0.01f, entity.camera->zoom);
            const float halfW = viewW * 0.5f / zoom;
            const float halfH = viewH * 0.5f / zoom;

            const auto p0 = camera_.WorldToScreen(centerWorld.x - halfW, centerWorld.y - halfH, viewW, viewH);
            const auto p1 = camera_.WorldToScreen(centerWorld.x + halfW, centerWorld.y - halfH, viewW, viewH);
            const auto p2 = camera_.WorldToScreen(centerWorld.x + halfW, centerWorld.y + halfH, viewW, viewH);
            const auto p3 = camera_.WorldToScreen(centerWorld.x - halfW, centerWorld.y + halfH, viewW, viewH);

            const Gorgon::Graphics::RGBAf wire(1.0f, 0.62f, 0.28f, 0.85f);
            drawLine(p0.X, p0.Y, p1.X, p1.Y, 1.5f, wire);
            drawLine(p1.X, p1.Y, p2.X, p2.Y, 1.5f, wire);
            drawLine(p2.X, p2.Y, p3.X, p3.Y, 1.5f, wire);
            drawLine(p3.X, p3.Y, p0.X, p0.Y, 1.5f, wire);

            if(centerId != entity.id) {
                const Transform self = scene_.GetWorldTransform(entity.id);
                const auto from = camera_.WorldToScreen(self.x, self.y, viewW, viewH);
                const auto to = camera_.WorldToScreen(centerWorld.x, centerWorld.y, viewW, viewH);
                drawLine(from.X, from.Y, to.X, to.Y, 1.0f,
                         Gorgon::Graphics::RGBAf(1.0f, 0.62f, 0.28f, 0.45f));
            }
        }
    }

    void EditorViewport::drawSelectionGizmo(float viewW, float viewH) {
        if(!editEnabled_) return;
        if(selectedEntities_.empty()) return;

        // Draw selection outlines for all selected entities
        for(EntityId id : selectedEntities_) {
            const Entity *sel = scene_.FindEntity(id);
            if(!sel || !sel->visible) continue;
            const auto wcSel = ComputeEntityWorldCorners(*sel);
            std::array<Gorgon::Geometry::Pointf, 4> scSel;
            for(int i = 0; i < 4; ++i) {
                scSel[i] = camera_.WorldToScreen(wcSel[i].X, wcSel[i].Y, viewW, viewH);
            }
            const bool isPrimary = (id == selectedEntity_);
            const Gorgon::Graphics::RGBAf outlineCol = sel->locked
                ? Gorgon::Graphics::RGBAf(0.95f, 0.55f, 0.25f, 0.95f)
                : (isPrimary ? Gorgon::Graphics::RGBAf(0.22f, 0.74f, 0.97f, 1.0f)
                             : Gorgon::Graphics::RGBAf(0.22f, 0.74f, 0.97f, 0.75f));
            drawRectOutline(scSel, isPrimary ? 2.0f : 1.5f, outlineCol);
        }

        // Transform handles (scale/rotate/axis arrows) are shown for single unlocked selection
        if(selectedEntities_.size() != 1) return;
        const Entity *e = scene_.FindEntity(selectedEntity_);
        if(!e || !e->visible || e->locked) return;

        const auto wc = ComputeEntityWorldCorners(*e);
        std::array<Gorgon::Geometry::Pointf, 4> sc;
        for(int i = 0; i < 4; ++i) {
            sc[i] = camera_.WorldToScreen(wc[i].X, wc[i].Y, viewW, viewH);
        }
        const Gorgon::Graphics::RGBAf selColor(0.22f, 0.74f, 0.97f, 1.0f);

        // Corner scale handles
        const float hs = kHandleSizePx;
        const float half = hs * 0.5f;
        for(int i = 0; i < 4; ++i) {
            gfxLayer_.Draw(sc[i].X - half - 1.0f, sc[i].Y - half - 1.0f, hs + 2.0f, hs + 2.0f, Gorgon::Graphics::RGBAf(0.08f, 0.10f, 0.14f, 1.0f));
            gfxLayer_.Draw(sc[i].X - half, sc[i].Y - half, hs, hs, Gorgon::Graphics::RGBAf(0.95f, 0.97f, 1.0f, 1.0f));
        }

        // Rotation handle (stem from top-center edge + circular knob)
        const Gorgon::Geometry::Pointf topMid{(sc[0].X + sc[1].X) * 0.5f, (sc[0].Y + sc[1].Y) * 0.5f};
        const auto rotPos = ComputeRotateHandleScreenPos(*e, viewW, viewH);
        drawLine(topMid.X, topMid.Y, rotPos.X, rotPos.Y, 1.5f, selColor);
        drawFilledCircle(rotPos.X, rotPos.Y, kRotateHandleRadiusPx + 1.5f, Gorgon::Graphics::RGBAf(0.08f, 0.10f, 0.14f, 1.0f));
        const Gorgon::Graphics::RGBAf rotFill = (dragMode_ == DragMode::RotateEntity)
            ? Gorgon::Graphics::RGBAf(1.0f, 0.85f, 0.25f, 1.0f)
            : Gorgon::Graphics::RGBAf(0.22f, 0.74f, 0.97f, 1.0f);
        drawFilledCircle(rotPos.X, rotPos.Y, kRotateHandleRadiusPx, rotFill);

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

    void EditorViewport::drawMarquee() {
        if(dragMode_ != DragMode::MarqueeSelect || !marqueeDragged_) return;
        const float minX = std::min(marqueeStartScreen_.X, marqueeEndScreen_.X);
        const float maxX = std::max(marqueeStartScreen_.X, marqueeEndScreen_.X);
        const float minY = std::min(marqueeStartScreen_.Y, marqueeEndScreen_.Y);
        const float maxY = std::max(marqueeStartScreen_.Y, marqueeEndScreen_.Y);
        const float w = maxX - minX;
        const float h = maxY - minY;
        if(w < 1.0f && h < 1.0f) return;

        gfxLayer_.Draw(minX, minY, w, h, Gorgon::Graphics::RGBAf(0.22f, 0.74f, 0.97f, 0.18f));
        const std::array<Gorgon::Geometry::Pointf, 4> corners = {
            Gorgon::Geometry::Pointf{minX, minY},
            Gorgon::Geometry::Pointf{maxX, minY},
            Gorgon::Geometry::Pointf{maxX, maxY},
            Gorgon::Geometry::Pointf{minX, maxY}
        };
        drawRectOutline(corners, 1.5f, Gorgon::Graphics::RGBAf(0.32f, 0.80f, 1.0f, 0.90f));
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

        // 3b. 2D lighting preview: the lightmap is multiplied over what was just drawn, so the grid,
        //     the background and the entities all fall into shadow. A scene without lights skips it.
        drawLighting(viewW, viewH);

        // 4. Collider wireframes
        drawColliders(viewW, viewH);

        // 5. Particle emitters (live during play, marker while editing)
        drawParticles(viewW, viewH);

        // 5b. Light gizmos sit above the darkness: a designer always sees where the lights are.
        drawLightGizmos(viewW, viewH);

        // 6. CameraComponent frustum wireframe
        drawCameraFrustum(viewW, viewH);

        // 7. Selection bounding box & translate/scale gizmo
        drawSelectionGizmo(viewW, viewH);

        // 8. Marquee selection box (translucent overlay)
        drawMarquee();

        // 9. Asset drag-and-drop preview indicator
        drawDropPreview(viewW, viewH);

        // 10. Tile painting overlay: cell grid, hover highlight and the palette strip
        drawTileTool(viewW, viewH);
    }

    void EditorViewport::drawTileTool(float viewW, float viewH) {
        if(!tileToolActive_)
            return;

        Entity *entity = scene_.FindEntity(selectedEntity_);
        if(!entity || !entity->visible || !entity->tilemap)
            return;

        const TilemapComponent &map = *entity->tilemap;
        if(!TilemapPainter::Paintable(map))
            return;

        // Same local -> world -> screen mapping the map itself is drawn with, so the overlay lands
        // exactly on the cells (pan, zoom, rotation and scale included).
        const Transform world = scene_.GetWorldTransform(entity->id);
        const float rad = world.rotation * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        auto tileToScreen = [&](float lx, float ly) -> Gorgon::Geometry::Pointf {
            const float wx = world.x + (lx * world.scaleX) * c - (ly * world.scaleY) * s;
            const float wy = world.y + (lx * world.scaleX) * s + (ly * world.scaleY) * c;
            return camera_.WorldToScreen(wx, wy, viewW, viewH);
        };

        const int cols = std::max(0, map.cols);
        const int rows = std::max(0, map.rows);
        const float originX = -static_cast<float>(cols) * map.tileW * 0.5f;
        const float originY = -static_cast<float>(rows) * map.tileH * 0.5f;
        const float mapW = static_cast<float>(cols) * map.tileW;
        const float mapH = static_cast<float>(rows) * map.tileH;

        // 1. Cell grid: thin lines over the map only, so the rest of the scene stays readable.
        const Gorgon::Graphics::RGBAf gridColor(1.0f, 1.0f, 1.0f, 0.20f);
        for(int col = 0; col <= cols; ++col) {
            const float lx = originX + static_cast<float>(col) * map.tileW;
            const auto a = tileToScreen(lx, originY);
            const auto b = tileToScreen(lx, originY + mapH);
            drawLine(a.X, a.Y, b.X, b.Y, 1.0f, gridColor);
        }
        for(int row = 0; row <= rows; ++row) {
            const float ly = originY + static_cast<float>(row) * map.tileH;
            const auto a = tileToScreen(originX, ly);
            const auto b = tileToScreen(originX + mapW, ly);
            drawLine(a.X, a.Y, b.X, b.Y, 1.0f, gridColor);
        }

        // 2. Highlight the cell under the cursor, or the rectangle a Fill drag will cover.
        if(hoverCell_.inside || dragMode_ == DragMode::PaintTiles) {
            int c0 = hoverCell_.col;
            int c1 = hoverCell_.col;
            int r0 = hoverCell_.row;
            int r1 = hoverCell_.row;

            if(tileTool_ == TilemapTool::Fill && strokeActive_) {
                c0 = std::min(strokeAnchor_.col, strokeCell_.col);
                c1 = std::max(strokeAnchor_.col, strokeCell_.col);
                r0 = std::min(strokeAnchor_.row, strokeCell_.row);
                r1 = std::max(strokeAnchor_.row, strokeCell_.row);
            }

            c0 = std::max(c0, 0);
            r0 = std::max(r0, 0);
            c1 = std::min(c1, cols - 1);
            r1 = std::min(r1, rows - 1);

            if(c0 <= c1 && r0 <= r1) {
                const float lx0 = originX + static_cast<float>(c0) * map.tileW;
                const float ly0 = originY + static_cast<float>(r0) * map.tileH;
                const float lx1 = originX + static_cast<float>(c1 + 1) * map.tileW;
                const float ly1 = originY + static_cast<float>(r1 + 1) * map.tileH;

                const auto q0 = tileToScreen(lx0, ly0);
                const auto q1 = tileToScreen(lx1, ly0);
                const auto q2 = tileToScreen(lx1, ly1);
                const auto q3 = tileToScreen(lx0, ly1);
                const std::array<Gorgon::Geometry::Pointf, 4> quad{q0, q1, q2, q3};

                gfxLayer_.Draw(q0, q1, q2, q3, Gorgon::Graphics::RGBAf(1.0f, 0.85f, 0.30f, 0.20f));
                drawRectOutline(quad, 2.0f, Gorgon::Graphics::RGBAf(1.0f, 0.88f, 0.36f, 0.95f));
            }
        }

        // 3. Palette strip: real tileset crops when the map has one, stable per-index colours
        //    otherwise, with a bright border on the active tile.
        const TilePaletteLayout &layout = GetTilePaletteLayout();
        if(layout.count <= 0 || layout.width <= 0.0f || layout.height <= 0.0f)
            return;

        gfxLayer_.Draw(layout.x, layout.y, layout.width, layout.height,
                       Gorgon::Graphics::RGBAf(0.06f, 0.07f, 0.09f, 0.90f));

        const auto *tileset = map.tilesetAsset.empty() ? nullptr : getOrLoadSprite(map.tilesetAsset);
        const int perRow = std::max(1, map.tilesetCols);
        const float tileW = std::max(1.0f, map.tileW);
        const float tileH = std::max(1.0f, map.tileH);

        for(int index = 0; index < layout.count; ++index) {
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            if(!layout.SwatchRect(index, cx, cy, cw, ch))
                continue;

            if(tileset && tileset->bitmap) {
                const auto size = tileset->bitmap->GetSize();
                const float imageW = static_cast<float>(std::max(1, size.Width));
                const float imageH = static_cast<float>(std::max(1, size.Height));
                const float tx = static_cast<float>(index % perRow) * tileW;
                const float ty = static_cast<float>(index / perRow) * tileH;
                const float tx2 = std::min(imageW, tx + tileW);
                const float ty2 = std::min(imageH, ty + tileH);

                const Gorgon::Geometry::Pointf t1{tx / imageW, ty / imageH};
                const Gorgon::Geometry::Pointf t2{tx2 / imageW, ty / imageH};
                const Gorgon::Geometry::Pointf t3{tx2 / imageW, ty2 / imageH};
                const Gorgon::Geometry::Pointf t4{tx / imageW, ty2 / imageH};

                const Gorgon::Geometry::Pointf d0{cx, cy};
                const Gorgon::Geometry::Pointf d1{cx + cw, cy};
                const Gorgon::Geometry::Pointf d2{cx + cw, cy + ch};
                const Gorgon::Geometry::Pointf d3{cx, cy + ch};
                gfxLayer_.Draw(*tileset->bitmap, d0, d1, d2, d3,
                               t1, t2, t3, t4, Gorgon::Graphics::RGBAf(1.0f));
            }
            else {
                gfxLayer_.Draw(cx, cy, cw, ch, TilePreviewColor(index));
            }

            const Gorgon::Geometry::Pointf p0{cx, cy};
            const Gorgon::Geometry::Pointf p1{cx + cw, cy};
            const Gorgon::Geometry::Pointf p2{cx + cw, cy + ch};
            const Gorgon::Geometry::Pointf p3{cx, cy + ch};
            const std::array<Gorgon::Geometry::Pointf, 4> box{p0, p1, p2, p3};
            const bool active = (index == paintTile_);
            drawRectOutline(box, active ? 3.0f : 1.0f,
                            active ? Gorgon::Graphics::RGBAf(1.0f, 0.90f, 0.35f, 1.0f)
                                   : Gorgon::Graphics::RGBAf(1.0f, 1.0f, 1.0f, 0.22f));
        }
    }

    void EditorViewport::drawDropPreview(float viewW, float viewH) {
        if(!dropPreview_.active) return;

        if(dropPreview_.isScript && dropPreview_.targetEntityId != InvalidEntityId) {
            if(const Entity *target = scene_.FindEntity(dropPreview_.targetEntityId)) {
                const auto wc = ComputeEntityWorldCorners(*target);
                std::array<Gorgon::Geometry::Pointf, 4> sc;
                for(int i = 0; i < 4; ++i) {
                    sc[i] = camera_.WorldToScreen(wc[i].X, wc[i].Y, viewW, viewH);
                }
                gfxLayer_.Draw(sc[0], sc[1], sc[2], sc[3], Gorgon::Graphics::RGBAf(1.0f, 0.78f, 0.22f, 0.22f));
                drawRectOutline(sc, 3.0f, Gorgon::Graphics::RGBAf(1.0f, 0.82f, 0.28f, 0.98f));

                const Transform wt = scene_.GetWorldTransform(target->id);
                const auto center = camera_.WorldToScreen(wt.x, wt.y, viewW, viewH);
                const Gorgon::Graphics::RGBAf plusCol(1.0f, 0.92f, 0.55f, 1.0f);
                drawLine(center.X - 8.0f, center.Y, center.X + 8.0f, center.Y, 2.5f, plusCol);
                drawLine(center.X, center.Y - 8.0f, center.X, center.Y + 8.0f, 2.5f, plusCol);
                return;
            }
        }

        const auto center = camera_.WorldToScreen(dropPreview_.worldX, dropPreview_.worldY, viewW, viewH);
        const float half = std::max(12.0f, kDefaultEntitySize * 0.5f * camera_.zoom);
        const Gorgon::Graphics::RGBAf fillCol = dropPreview_.isScript
            ? Gorgon::Graphics::RGBAf(0.95f, 0.72f, 0.25f, 0.20f)
            : Gorgon::Graphics::RGBAf(0.22f, 0.74f, 0.97f, 0.20f);
        const Gorgon::Graphics::RGBAf outlineCol = dropPreview_.isScript
            ? Gorgon::Graphics::RGBAf(1.0f, 0.80f, 0.30f, 0.95f)
            : Gorgon::Graphics::RGBAf(0.32f, 0.85f, 1.0f, 0.95f);

        gfxLayer_.Draw(center.X - half, center.Y - half, half * 2.0f, half * 2.0f, fillCol);
        const std::array<Gorgon::Geometry::Pointf, 4> corners = {
            Gorgon::Geometry::Pointf{center.X - half, center.Y - half},
            Gorgon::Geometry::Pointf{center.X + half, center.Y - half},
            Gorgon::Geometry::Pointf{center.X + half, center.Y + half},
            Gorgon::Geometry::Pointf{center.X - half, center.Y + half}
        };
        drawRectOutline(corners, 2.0f, outlineCol);
        drawLine(center.X - 7.0f, center.Y, center.X + 7.0f, center.Y, 1.5f, outlineCol);
        drawLine(center.X, center.Y - 7.0f, center.X, center.Y + 7.0f, 1.5f, outlineCol);
    }

} // namespace Gorgon::Editor
