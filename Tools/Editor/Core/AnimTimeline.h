#pragma once

#include "Components.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// A pixel rectangle inside a sprite sheet: the part of the image one frame shows.
    struct AnimFrameRect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        bool operator==(const AnimFrameRect &other) const {
            return x == other.x && y == other.y && w == other.w && h == other.h;
        }
        bool operator!=(const AnimFrameRect &other) const { return !(*this == other); }
    };

    /// One frame of an animation, resolved for display: index, duration, sheet rect and the time
    /// (milliseconds) at which it starts inside the whole animation.
    struct TimelineFrameInfo {
        int index = 0;
        int durationMs = 0;
        int startMs = 0;
        AnimFrameRect rect;
    };

    /**
     * Geometry of the timeline strip the panel draws in the bottom dock: a row of frame cells.
     *
     * The layout is pure arithmetic on pixels, so the panel draws cells at exactly the rectangles
     * this class hit-tests, the drag-to-reorder insertion index comes from the same numbers, and
     * the Catch2 suite can verify picking without a window.
     */
    struct TimelineStripLayout {
        float originX = 0.0f;   // left edge of the first cell
        float originY = 0.0f;   // top edge of the strip
        float cellWidth = 104.0f;
        float cellHeight = 74.0f;
        float gap = 6.0f;
        int count = 0;          // frame cells drawn (0..count-1)

        /// Lays out `count` cells starting at (originX, originY).
        static TimelineStripLayout Compute(float originX, float originY, int count,
                                           float cellWidth = 104.0f, float cellHeight = 74.0f,
                                           float gap = 6.0f);

        /// Strip-local rectangle of the cell for `index`. False when the frame is not shown.
        bool CellRect(int index, float &outX, float &outY, float &outW, float &outH) const;

        /// Rectangle of the editable duration box inside a cell (the lower band of the cell).
        bool DurationRect(int index, float &outX, float &outY, float &outW, float &outH) const;

        /// Frame index under a strip-local point, -1 when the point misses every cell or falls in
        /// a gap between two cells.
        int HitTest(float x, float y) const;

        /// Index whose duration box holds the point, -1 when none.
        int HitTestDuration(float x, float y) const;

        /// Where a frame dragged to `x` would land: the number of cells whose centre is left of the
        /// cursor, i.e. 0..count. Equal to `from`/`from + 1` means the drop changes nothing.
        int InsertionIndex(float x) const;

        /// Pixels the `count` cells (gaps included) need.
        float Width() const;

        /// X of the playhead marker inside `frame`: `progress01` (0..1) runs across that cell.
        float PlayheadX(int frame, float progress01 = 0.0f) const;
    };

    /**
     * Pure animation-timeline model shared by the timeline panel, the runtime stepper and the
     * Catch2 suite. Nothing here touches Gorgon rendering or the undo stack.
     *
     * Frame model: `SpriteAnimComponent::frames` is the authored timeline (per-frame duration in
     * milliseconds plus an optional sheet sub-rect). While it is empty the component keeps its
     * original meaning — `frameCount` grid cells of `frameW x frameH` laid out row-major across the
     * sheet, each shown for `1 / fps` seconds — so scenes written before the timeline existed load,
     * play and serialize exactly as they did. Every editing operation materializes the implicit
     * grid into explicit frames first.
     */
    class AnimationTimeline {
    public:
        /// Frames the animation plays. Zero means "no frames" (the panel shows a hint).
        static int FrameCount(const SpriteAnimComponent &anim);

        /// True when the component carries an authored (explicit) timeline.
        static bool HasExplicitFrames(const SpriteAnimComponent &anim) { return !anim.frames.empty(); }

        /// Duration of one frame in milliseconds; 0 when `index` is out of range.
        static int FrameDurationMs(const SpriteAnimComponent &anim, int index);

        /// Whole animation length in milliseconds.
        static int TotalDurationMs(const SpriteAnimComponent &anim);

        /// Sheet rect a frame shows. `sheetWidth`/`sheetHeight` are the sprite sheet's pixel size;
        /// pass 0 when it is unknown and the grid rect is derived from `frameW`/`frameH` alone
        /// (single row). A frame that defines its own rect always wins over the grid.
        static AnimFrameRect FrameSourceRect(const SpriteAnimComponent &anim, int index,
                                             int sheetWidth = 0, int sheetHeight = 0);

        /// Every frame resolved for display, in play order.
        static std::vector<TimelineFrameInfo> BuildView(const SpriteAnimComponent &anim,
                                                        int sheetWidth = 0, int sheetHeight = 0);

        /// Average playback rate implied by the frame durations (0 when there are no frames).
        static float EffectiveFps(const SpriteAnimComponent &anim);

        /// Expands the implicit `frameCount` grid into explicit frames (`1 / fps` each, grid rect).
        /// No-op when the timeline is already explicit. Returns true when frames were created.
        static bool Materialize(SpriteAnimComponent &anim);

        // ---- Editing ---------------------------------------------------------
        // Every operation materializes first, ignores out-of-range indices and returns false/falsey
        // when it changed nothing, so the caller only records an undo step for a real edit.

        /// Appends a copy of the frame at `afterIndex` (clamped; -1 appends at the end). Returns the
        /// index of the new frame, or -1 when the cap (`MaxFrames`) was hit.
        static int AddFrame(SpriteAnimComponent &anim, int afterIndex);

        /// Inserts a copy of `index` directly after it. Returns the new index or -1.
        static int DuplicateFrame(SpriteAnimComponent &anim, int index);

        /// Removes one frame and keeps the current frame in range. Returns true when it was removed.
        static bool DeleteFrame(SpriteAnimComponent &anim, int index);

        /// Moves the frame at `from` so it ends up at `to`. Returns true when the order changed.
        static bool MoveFrame(SpriteAnimComponent &anim, int from, int to);

        /// Sets one frame's duration (clamped to `MinFrameDurationMs`). Returns true when changed.
        static bool SetFrameDuration(SpriteAnimComponent &anim, int index, int durationMs);

        /// Sets one frame's sheet rect. A non-positive width or height falls back to the grid cell.
        static bool SetFrameRect(SpriteAnimComponent &anim, int index, const AnimFrameRect &rect);

        /// Rewrites every frame duration to `1000 / fps` and stores `fps` on the component.
        /// Returns true when anything changed.
        static bool RetimeToFps(SpriteAnimComponent &anim, float fps);

        inline static constexpr int MinFrameDurationMs = 1;
        inline static constexpr int DefaultFrameDurationMs = 100;
        inline static constexpr int MaxFrames = 256;
    };

    /**
     * Wall-clock playhead over one animation.
     *
     * The playhead never mutates the component: the panel pushes `GetFrame()` into the viewport's
     * preview, so scrubbing and playing cannot dirty the scene or fight the undo stack. `Advance`
     * uses the authored per-frame durations (and `loop`), which is what makes the preview run at
     * wall-clock speed instead of one frame per editor tick.
     */
    class TimelinePlayhead {
    public:
        void Play() { playing_ = true; }
        void Stop() { playing_ = false; }
        void Toggle() { playing_ = !playing_; }
        bool IsPlaying() const { return playing_; }

        /// Frame the playhead currently sits on.
        int GetFrame() const { return frame_; }
        /// Milliseconds spent inside the current frame.
        float GetElapsedMs() const { return elapsedMs_; }

        /// Jumps to `frame` (clamped to the animation) and restarts the frame timer.
        void Scrub(const SpriteAnimComponent &anim, int frame);

        /// Resets to the first frame and stands still.
        void Reset();

        /// Advances by `dtSeconds` of wall-clock time, honouring each frame's duration. When the
        /// animation ends and `anim.loop` is false the playhead stops on the last frame. Returns
        /// true when the frame changed, so the caller can redraw only then.
        bool Advance(const SpriteAnimComponent &anim, float dtSeconds);

        /// Progress through the whole animation, 0..1, for the playhead marker.
        float GetProgress(const SpriteAnimComponent &anim) const;

        /// Progress inside the current frame, 0..1.
        float GetFrameProgress(const SpriteAnimComponent &anim) const;

    private:
        bool playing_ = false;
        int frame_ = 0;
        float elapsedMs_ = 0.0f;
    };

} // namespace Gorgon::Editor
