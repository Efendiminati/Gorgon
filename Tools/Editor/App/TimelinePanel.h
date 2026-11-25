#pragma once

#include "Core/AnimTimeline.h"
#include "Viewport.h"

#include <Gorgon/Geometry/Point.h>
#include <Gorgon/Graphics/Layer.h>
#include <Gorgon/Input/Layer.h>
#include <Gorgon/UI/Window.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Checkbox.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Layerbox.h>
#include <Gorgon/Widgets/Numberbox.h>
#include <Gorgon/Widgets/Panel.h>

#include <functional>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /**
     * Animation timeline: the bottom-dock tab that draws the selected entity's sprite animation as
     * a horizontal strip of frame cells (index, duration in ms and the sheet sub-rect each frame
     * shows), with frame add / duplicate / delete / drag-to-reorder, per-frame duration editing, an
     * fps shortcut that retimes the whole animation, and a wall-clock playhead that previews the
     * animation in the viewport.
     *
     * Selecting a frame never touches the scene selection, and the viewport preview goes through
     * `EditorViewport::SetAnimationPreview`, so scrubbing and playing do not modify the scene (and
     * therefore do not dirty it or push undo steps). Every *edit* is applied to the component and
     * reported through `OnEdited`, which the app turns into one `recordEdit` undo step.
     */
    class TimelinePanel {
    public:
        TimelinePanel(Scene &scene, EditorViewport &viewport);
        ~TimelinePanel();

        TimelinePanel(const TimelinePanel &) = delete;
        TimelinePanel &operator=(const TimelinePanel &) = delete;

        /// Adds the dock panel and the strip to the window. Call before Layout().
        void Attach(Gorgon::UI::Window &window);

        /// Places the panel in the given window area and lays out its controls.
        void Layout(int x, int y, int width, int height);

        Gorgon::Widgets::Panel &GetPanel() { return panel_; }
        Gorgon::Widgets::Layerbox &GetStripBox() { return stripBox_; }

        // ---- Model access ------------------------------------------------------
        /// Entity whose animation is shown: the viewport's selection (InvalidEntityId when none).
        EntityId GetEntityId() const { return viewport_.GetSelectedEntity(); }
        /// The selected entity's animation, or nullptr when nothing/no animation is selected.
        SpriteAnimComponent *GetAnimation();
        const SpriteAnimComponent *GetAnimation() const;

        /// Re-reads the selection and the component. Called on selection changes, undo/redo and
        /// inspector edits. Keeps the frame selection and the playhead when the animation is still
        /// the same one.
        void Sync(bool force = false);

        /// One frame: advances the playhead when it is running and previews the frame it lands on.
        /// Returns true when the preview changed.
        bool Tick(float dt);

        int GetFrameCount() const;
        /// Frame the strip highlights (clamped to the animation; -1 when there is nothing to show).
        int GetSelectedFrame() const { return selectedFrame_; }
        /// Highlights a frame and previews it in the viewport. Never changes the scene selection.
        void SetSelectedFrame(int index);

        // ---- Timeline edits (each reports one undo step through OnEdited) -------
        /// Appends a copy of the last frame (or the first frame of an empty animation).
        int AddFrame();
        int DuplicateFrame();
        bool DeleteFrame();
        bool MoveFrame(int from, int to);
        bool SetFrameDuration(int index, int durationMs);
        bool RetimeToFps(float fps);
        /// Writes one frame's sheet sub-rect (a non-positive size restores the grid cell).
        bool SetFrameRect(int index, const AnimFrameRect &rect);

        // ---- Playhead ----------------------------------------------------------
        void Play();
        void Stop();
        void TogglePlay();
        bool IsPlaying() const { return playhead_.IsPlaying(); }
        int GetPlayheadFrame() const { return playhead_.GetFrame(); }
        /// Mirrors the component's `loop` flag; editing it is an undoable scene edit.
        bool GetLoop() const;
        void SetLoop(bool loop);

        /// Play-in-editor owns the scene while it runs: the timeline stops its own playhead and
        /// stops overriding the viewport, so the real animation (and the scripts driving it) is
        /// what the user sees. Call with false once play stops to restore the preview.
        void SetScenePlaying(bool playing);
        bool IsScenePlaying() const { return scenePlaying_; }

        // ---- Presentation ------------------------------------------------------
        /// Text shown instead of a strip when there is nothing to draw (no selection, no animation,
        /// zero frames). "" when the strip has frames.
        std::string GetHint() const;
        /// "#4 frames · 800 ms · 12.0 fps" summary shown next to the buttons.
        std::string GetStatusText() const;
        /// Number of cells the strip draws.
        int GetVisibleFrameCount() const;

        /// Strip geometry, strip-local pixels. Used by the drawing code, the mouse handlers and
        /// `--smoke`.
        const TimelineStripLayout &GetStripLayout() const { return layout_; }
        void Redraw();

        // ---- Mouse (the strip's input layer calls these; --smoke calls them directly) ---------
        bool HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        bool HandleMouseMove(Gorgon::Geometry::Point pt);
        bool HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);

        /// Fired after a timeline edit changed the scene; the app records it as one undo step.
        std::function<void(const std::string &)> OnEdited;
        /// Fired after the preview frame changed (the app can refresh other views).
        std::function<void()> OnPreviewChanged;

    private:
        void build();
        void layoutControls(int width, int height);
        void updateControls();
        /// Applies the current frame to the viewport preview (does not touch the component).
        void previewSelectedFrame();
        /// Ends any playhead run and drops the viewport preview.
        void stopPlayback();
        /// Applies an edit result: records the undo step, refreshes the widgets and redraws.
        void afterEdit(const std::string &name, int selectFrame = -1);
        SpriteAnimComponent *animFor(Entity &entity) const;
        /// Sheet size of the animation's asset, for the sub-rect display.
        Gorgon::Geometry::Size sheetSize(const SpriteAnimComponent &anim) const;

        Scene &scene_;
        EditorViewport &viewport_;

        TimelinePlayhead playhead_;
        int selectedFrame_ = 0;
        // Entity the panel was last synced to, so a selection change resets the frame selection
        // and the playhead instead of carrying them over to a different animation.
        EntityId syncedEntity_ = InvalidEntityId;
        // Frame the mouse press landed on, and whether the drag has already crossed into reorder.
        int dragFrom_ = -1;
        bool dragging_ = false;
        int dragInsertion_ = -1;
        Gorgon::Geometry::Point dragStart_{0, 0};

        mutable TimelineStripLayout layout_;

        Gorgon::Widgets::Panel panel_{Gorgon::Widgets::Registry::Panel_Bottom};
        Gorgon::Widgets::Button btnPlay_{"Play"};
        Gorgon::Widgets::Button btnStop_{"Stop"};
        Gorgon::Widgets::Checkbox chkLoop_{"Loop", true};
        Gorgon::Widgets::Button btnAdd_{"Add Frame"};
        Gorgon::Widgets::Button btnDuplicate_{"Duplicate"};
        Gorgon::Widgets::Button btnDelete_{"Delete Frame"};
        Gorgon::Widgets::Label lblFps_{"FPS"};
        Gorgon::Widgets::Numberbox fpsBox_{12.0f};
        Gorgon::Widgets::Button btnApplyFps_{"Retime"};
        Gorgon::Widgets::Integerbox durationBox_{100};
        Gorgon::Widgets::Label lblDuration_{"Duration (ms)"};
        Gorgon::Widgets::Label lblStatus_{""};

        Gorgon::Widgets::Layerbox stripBox_{Gorgon::Widgets::Registry::Layerbox_Blank};
        Gorgon::Graphics::Layer gfxLayer_;
        Gorgon::Input::Layer inputLayer_;
        bool attached_ = false;
        bool stripAttached_ = false;

        int stripTop_ = 0;
        int stripHeight_ = 0;
        int stripViewWidth_ = 0;
        // First frame cell drawn: the strip scrolls horizontally when an animation has more frames
        // than the dock fits, and follows the selected frame.
        int scrollFrame_ = 0;
        // True while play-in-editor runs: the viewport preview is the runtime's, not the timeline's.
        bool scenePlaying_ = false;
        bool syncing_ = false;
    };

} // namespace Gorgon::Editor
