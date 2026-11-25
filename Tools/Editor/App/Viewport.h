#pragma once

#include "Core/Scene.h"

#include <Gorgon/Geometry/Point.h>
#include <Gorgon/Geometry/Size.h>
#include <Gorgon/Graphics/Bitmap.h>
#include <Gorgon/Graphics/Layer.h>
#include <Gorgon/Input/Layer.h>
#include <Gorgon/Widgets/Layerbox.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gorgon :: Editor {

    class PlaySession;

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
        ScaleBL,
        Rotate
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

        /// Live play session to read particle state from while playing. The viewport never owns it;
        /// passing nullptr (or stopping play) falls back to drawing the emitter marker only.
        void SetPlaySession(const PlaySession *session) { playSession_ = session; }

        /// Sets the base directory used to resolve relative sprite asset paths.
        void SetAssetBaseDir(const std::string &dir);
        const std::string &GetAssetBaseDir() const { return assetBaseDir_; }

        /// Camera controls
        ViewportCamera &GetCamera() { return camera_; }
        const ViewportCamera &GetCamera() const { return camera_; }
        void ResetCamera();

        /// Selection (primary + multi-select set)
        EntityId GetSelectedEntity() const { return selectedEntity_; }
        const std::vector<EntityId> &GetSelectedEntities() const { return selectedEntities_; }
        std::size_t GetSelectionCount() const { return selectedEntities_.size(); }
        bool IsEntitySelected(EntityId id) const;
        void SetSelectedEntity(EntityId id, bool notify = true);
        void SetSelectedEntities(const std::vector<EntityId> &ids, bool notify = true);
        void ToggleEntitySelection(EntityId id, bool notify = true);
        void ClearSelection(bool notify = true);
        /// Drops any selected entity IDs that no longer exist in the scene.
        void PruneSelection(bool notify = false);

        /// Group operations on the current selection set.
        /// DuplicateSelectedEntities duplicates all top-level selected entities and selects the copies.
        std::vector<EntityId> DuplicateSelectedEntities(bool notify = false);
        /// DeleteSelectedEntities removes all selected entities and updates selection.
        bool DeleteSelectedEntities(bool notify = false);

        /// Grid snap toggle
        void SetGridSnap(bool enabled) { gridSnap_ = enabled; }
        bool IsGridSnapEnabled() const { return gridSnap_; }

        /// Convert viewport-local screen coordinates to world coordinates, applying grid snap if enabled.
        Gorgon::Geometry::Pointf ScreenToWorldSnapped(float sx, float sy) const;

        /// Visual preview shown while dragging an asset from the Asset Browser over the viewport.
        struct DropPreview {
            bool active = false;
            bool isScript = false;
            std::string label;
            float worldX = 0.0f;
            float worldY = 0.0f;
            EntityId targetEntityId = InvalidEntityId;
        };

        void SetDropPreview(float sx, float sy, const std::string &label, bool isScript);
        void ClearDropPreview();
        const DropPreview &GetDropPreview() const { return dropPreview_; }

        /// Edit interactions (click-select, translate/scale gizmos, drag-move). Pan and zoom stay
        /// available while editing is disabled, which is what play-in-editor needs.
        void SetEditEnabled(bool enabled) { editEnabled_ = enabled; }
        bool IsEditEnabled() const { return editEnabled_; }

        /// Modifier key states (fed from window KeyPreviewEvent and testable directly)
        void SetSpaceHeld(bool held) { spaceHeld_ = held; }
        bool IsSpaceHeld() const { return spaceHeld_; }
        void SetShiftHeld(bool held) { shiftHeld_ = held; }
        bool IsShiftHeld() const { return shiftHeld_; }

        /// Mouse state the viewport tracks from its own input layer. Play-in-editor reads it to
        /// feed the script Input API, so it is recorded even while editing is disabled.
        Gorgon::Geometry::Point GetLastMousePosition() const { return lastMouse_; }
        /// Last mouse position in world units (no grid snap) — what scripts see as MouseX/MouseY.
        Gorgon::Geometry::Pointf GetMouseWorldPosition() const;
        bool IsMouseButtonDown(Gorgon::Input::Mouse::Button btn) const;

        /// Explicit viewport size override (used when running headless / in selftests without a Layerbox).
        void SetViewportSizeOverride(Gorgon::Geometry::Size sz) { sizeOverride_ = sz; }
        Gorgon::Geometry::Size GetViewportSize() const;

        /// Hit-testing & interaction helpers (also directly testable in --selftest)
        EntityId HitTestEntity(float sx, float sy) const;
        std::vector<EntityId> HitTestMarquee(float sx1, float sy1, float sx2, float sy2) const;
        GizmoHandle HitTestGizmo(float sx, float sy) const;
        Gorgon::Geometry::Pointf ComputeEntityBaseSize(const Entity &entity) const;
        std::array<Gorgon::Geometry::Pointf, 4> ComputeEntityWorldCorners(const Entity &entity) const;
        Gorgon::Geometry::Pointf ComputeRotateHandleScreenPos(const Entity &entity, float viewW, float viewH) const;

        /// Mouse event handlers (called by Input::Layer and testable directly)
        void HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        void HandleMouseMove(Gorgon::Geometry::Point pt);
        void HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        bool HandleMouseScroll(Gorgon::Geometry::Point pt, float amount);

        /// Callbacks for EditorApp / Phase 3 / Phase 5
        std::function<void(EntityId)> OnSelectionChanged;
        std::function<void()> OnSceneModified;
        std::function<void(float worldX, float worldY, float zoom)> OnStatusChanged;

        /// Fired with true when an entity transform drag begins and false when it ends, so the
        /// editor can collapse a whole drag into a single undo step.
        std::function<void(bool active)> OnInteractionChanged;

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
            ScaleEntity,
            RotateEntity,
            MarqueeSelect
        };

        struct DragEntityEntry {
            EntityId id = InvalidEntityId;
            Transform startLocal;
        };

        const CachedSprite *getOrLoadSprite(const std::string &assetPath) const;
        std::string resolveAssetPath(const std::string &assetPath) const;
        float snapValue(float value) const;
        bool isShiftDown() const;
        void captureGroupMoveTargets();

        void drawLine(float x1, float y1, float x2, float y2, float thickness, Gorgon::Graphics::RGBAf color);
        void drawFilledCircle(float cx, float cy, float radius, Gorgon::Graphics::RGBAf color);
        void drawRectOutline(const std::array<Gorgon::Geometry::Pointf, 4> &pts, float thickness, Gorgon::Graphics::RGBAf color);
        void drawGrid(float viewW, float viewH);
        void drawEntities(float viewW, float viewH);
        void drawColliders(float viewW, float viewH);
        void drawParticles(float viewW, float viewH);
        void drawCameraFrustum(float viewW, float viewH);
        void drawSelectionGizmo(float viewW, float viewH);
        void drawMarquee();
        void drawDropPreview(float viewW, float viewH);

        Scene &scene_;
        Gorgon::Widgets::Layerbox *layerbox_ = nullptr;
        Gorgon::Graphics::Layer gfxLayer_;
        Gorgon::Input::Layer inputLayer_;
        bool attached_ = false;

        ViewportCamera camera_;
        EntityId selectedEntity_ = InvalidEntityId;
        std::vector<EntityId> selectedEntities_;
        bool gridSnap_ = true;
        bool editEnabled_ = true;
        bool spaceHeld_ = false;
        bool shiftHeld_ = false;
        // Tracked from HandleMouseDown/HandleMouseUp regardless of edit mode, so scripts keep
        // seeing the buttons while play-in-editor has editing disabled. Order: left, right, middle.
        bool mouseDown_[3] = {false, false, false};
        Gorgon::Geometry::Size sizeOverride_{0, 0};

        const PlaySession *playSession_ = nullptr;

        std::string assetBaseDir_;
        mutable std::unordered_map<std::string, CachedSprite> spriteCache_;

        // Active drag state
        DragMode dragMode_ = DragMode::None;
        GizmoHandle activeHandle_ = GizmoHandle::None;
        Gorgon::Geometry::Point lastMouse_{0, 0};
        Gorgon::Geometry::Pointf dragStartWorld_{0.0f, 0.0f};
        Transform dragStartLocalTransform_;
        Transform dragStartWorldTransform_;
        float dragStartAngleDeg_ = 0.0f;
        std::vector<DragEntityEntry> dragStartTransforms_;
        EntityId pendingSingleSelect_ = InvalidEntityId;
        bool dragMoved_ = false;

        // Active marquee selection state
        Gorgon::Geometry::Pointf marqueeStartScreen_{0.0f, 0.0f};
        Gorgon::Geometry::Pointf marqueeEndScreen_{0.0f, 0.0f};
        bool marqueeAdditive_ = false;
        bool marqueeDragged_ = false;
        std::vector<EntityId> marqueeBaseSelection_;

        // Active asset drag-and-drop preview
        DropPreview dropPreview_;
    };

} // namespace Gorgon::Editor
