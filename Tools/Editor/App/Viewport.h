#pragma once

#include "Core/Scene.h"

#include <Gorgon/Geometry/Point.h>
#include <Gorgon/Geometry/Size.h>
#include <Gorgon/Graphics/Bitmap.h>
#include <Gorgon/Graphics/Layer.h>
#include <Gorgon/Input/Layer.h>
#include <Gorgon/Widgets/Layerbox.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Gorgon :: Editor {

    struct ViewportCamera {
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom = 1.0f;

        static constexpr float MinZoom = 0.1f;
        static constexpr float MaxZoom = 16.0f;

        Gorgon::Geometry::Pointf WorldToScreen(float wx, float wy, float viewW, float viewH) const;
        Gorgon::Geometry::Pointf ScreenToWorld(float sx, float sy, float viewW, float viewH) const;

        /// Zooms by a multiplicative factor while keeping the world point under (anchorSx, anchorSy) stationary.
        void ZoomAt(float anchorSx, float anchorSy, float viewW, float viewH, float factor);

        /// Pans by screen-pixel delta.
        void PanByScreenDelta(float dx, float dy);

        /// Resets pan to (0, 0) and zoom to 1.0.
        void Reset();
    };

    enum class GizmoHandle {
        None = 0,
        TranslateX,
        TranslateY,
        ScaleTL,
        ScaleTR,
        ScaleBR,
        ScaleBL
    };

    class EditorViewport {
    public:
        EditorViewport(Scene &scene, Gorgon::Widgets::Layerbox *layerbox = nullptr);
        ~EditorViewport();

        /// Attaches rendering and input layers to the given Layerbox (may be called once).
        void Attach(Gorgon::Widgets::Layerbox &layerbox);

        /// Re-renders the viewport into its Graphics::Layer.
        void Redraw();

        /// Clears cached sprite textures (e.g. when switching projects or reloading assets).
        void ClearSpriteCache();

        /// Sets the base directory used to resolve relative sprite asset paths.
        void SetAssetBaseDir(const std::string &dir);
        const std::string &GetAssetBaseDir() const { return assetBaseDir_; }

        /// Camera controls
        ViewportCamera &GetCamera() { return camera_; }
        const ViewportCamera &GetCamera() const { return camera_; }
        void ResetCamera();

        /// Selection
        EntityId GetSelectedEntity() const { return selectedEntity_; }
        void SetSelectedEntity(EntityId id, bool notify = true);

        /// Grid snap toggle
        void SetGridSnap(bool enabled) { gridSnap_ = enabled; }
        bool IsGridSnapEnabled() const { return gridSnap_; }

        /// Space-bar state (fed from window KeyPreviewEvent for Space+left-drag panning)
        void SetSpaceHeld(bool held) { spaceHeld_ = held; }
        bool IsSpaceHeld() const { return spaceHeld_; }

        /// Explicit viewport size override (used when running headless / in selftests without a Layerbox).
        void SetViewportSizeOverride(Gorgon::Geometry::Size sz) { sizeOverride_ = sz; }
        Gorgon::Geometry::Size GetViewportSize() const;

        /// Hit-testing & interaction helpers (also directly testable in --selftest)
        EntityId HitTestEntity(float sx, float sy) const;
        GizmoHandle HitTestGizmo(float sx, float sy) const;
        Gorgon::Geometry::Pointf ComputeEntityBaseSize(const Entity &entity) const;
        std::array<Gorgon::Geometry::Pointf, 4> ComputeEntityWorldCorners(const Entity &entity) const;

        /// Mouse event handlers (called by Input::Layer and testable directly)
        void HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        void HandleMouseMove(Gorgon::Geometry::Point pt);
        void HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        bool HandleMouseScroll(Gorgon::Geometry::Point pt, float amount);

        /// Callbacks for EditorApp / Phase 3 / Phase 5
        std::function<void(EntityId)> OnSelectionChanged;
        std::function<void()> OnSceneModified;
        std::function<void(float worldX, float worldY, float zoom)> OnStatusChanged;

    private:
        struct CachedSprite {
            std::unique_ptr<Gorgon::Graphics::Bitmap> bitmap;
            bool attempted = false;
            bool valid = false;
        };

        enum class DragMode {
            None = 0,
            Pan,
            MoveEntity,
            MoveEntityX,
            MoveEntityY,
            ScaleEntity
        };

        const CachedSprite *getOrLoadSprite(const std::string &assetPath) const;
        std::string resolveAssetPath(const std::string &assetPath) const;
        float snapValue(float value) const;

        void drawLine(float x1, float y1, float x2, float y2, float thickness, Gorgon::Graphics::RGBAf color);
        void drawRectOutline(const std::array<Gorgon::Geometry::Pointf, 4> &pts, float thickness, Gorgon::Graphics::RGBAf color);
        void drawGrid(float viewW, float viewH);
        void drawEntities(float viewW, float viewH);
        void drawColliders(float viewW, float viewH);
        void drawSelectionGizmo(float viewW, float viewH);

        Scene &scene_;
        Gorgon::Widgets::Layerbox *layerbox_ = nullptr;
        Gorgon::Graphics::Layer gfxLayer_;
        Gorgon::Input::Layer inputLayer_;
        bool attached_ = false;

        ViewportCamera camera_;
        EntityId selectedEntity_ = InvalidEntityId;
        bool gridSnap_ = true;
        bool spaceHeld_ = false;
        Gorgon::Geometry::Size sizeOverride_{0, 0};

        std::string assetBaseDir_;
        mutable std::unordered_map<std::string, CachedSprite> spriteCache_;

        // Active drag state
        DragMode dragMode_ = DragMode::None;
        GizmoHandle activeHandle_ = GizmoHandle::None;
        Gorgon::Geometry::Point lastMouse_{0, 0};
        Gorgon::Geometry::Pointf dragStartWorld_{0.0f, 0.0f};
        Transform dragStartLocalTransform_;
        Transform dragStartWorldTransform_;
    };

} // namespace Gorgon::Editor
