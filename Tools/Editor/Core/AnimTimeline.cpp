#include "AnimTimeline.h"

#include <algorithm>
#include <cmath>

namespace Gorgon :: Editor {

    namespace {
        int gridFrameWidth(const SpriteAnimComponent &anim) {
            return std::max(1, anim.frameW);
        }

        int gridFrameHeight(const SpriteAnimComponent &anim) {
            return std::max(1, anim.frameH);
        }

        /// Default duration a legacy grid implies: one frame per `1 / fps` second.
        int legacyFrameDurationMs(const SpriteAnimComponent &anim) {
            if(!(anim.fps > 0.0f))
                return AnimationTimeline::DefaultFrameDurationMs;
            const int ms = int(std::lround(1000.0f / anim.fps));
            return std::max(AnimationTimeline::MinFrameDurationMs, ms);
        }

        int clampDuration(int durationMs) {
            return std::max(AnimationTimeline::MinFrameDurationMs, durationMs);
        }
    } // namespace

    // ---- Layout -------------------------------------------------------------

    TimelineStripLayout TimelineStripLayout::Compute(float originX, float originY, int count,
                                                     float cellWidth, float cellHeight, float gap) {
        TimelineStripLayout layout;
        layout.originX = originX;
        layout.originY = originY;
        layout.cellWidth = std::max(1.0f, cellWidth);
        layout.cellHeight = std::max(1.0f, cellHeight);
        layout.gap = std::max(0.0f, gap);
        layout.count = std::max(0, count);
        return layout;
    }

    bool TimelineStripLayout::CellRect(int index, float &outX, float &outY, float &outW,
                                       float &outH) const {
        if(index < 0 || index >= count)
            return false;

        outX = originX + float(index) * (cellWidth + gap);
        outY = originY;
        outW = cellWidth;
        outH = cellHeight;
        return true;
    }

    bool TimelineStripLayout::DurationRect(int index, float &outX, float &outY, float &outW,
                                           float &outH) const {
        if(!CellRect(index, outX, outY, outW, outH))
            return false;

        // The lower band of the cell, inset from its edges: the box the user clicks to edit the
        // frame's duration.
        const float bandHeight = std::min(22.0f, std::max(12.0f, cellHeight * 0.28f));
        outY = outY + cellHeight - bandHeight - 5.0f;
        outX = outX + 6.0f;
        outW = outW - 12.0f;
        outH = bandHeight;
        return true;
    }

    int TimelineStripLayout::HitTest(float x, float y) const {
        if(y < originY || y >= originY + cellHeight)
            return -1;

        for(int i = 0; i < count; ++i) {
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            if(!CellRect(i, cx, cy, cw, ch))
                break;
            if(x >= cx && x < cx + cw)
                return i;
        }

        return -1;
    }

    int TimelineStripLayout::HitTestDuration(float x, float y) const {
        const int cell = HitTest(x, y);
        if(cell < 0)
            return -1;

        float dx = 0.0f, dy = 0.0f, dw = 0.0f, dh = 0.0f;
        if(!DurationRect(cell, dx, dy, dw, dh))
            return -1;

        return (x >= dx && x < dx + dw && y >= dy && y < dy + dh) ? cell : -1;
    }

    int TimelineStripLayout::InsertionIndex(float x) const {
        for(int i = 0; i < count; ++i) {
            const float centre = originX + float(i) * (cellWidth + gap) + cellWidth * 0.5f;
            if(x < centre)
                return i;
        }
        return count;
    }

    float TimelineStripLayout::Width() const {
        if(count <= 0)
            return 0.0f;
        return float(count) * cellWidth + float(count - 1) * gap;
    }

    float TimelineStripLayout::PlayheadX(int frame, float progress01) const {
        if(count <= 0)
            return originX;

        const int clamped = std::clamp(frame, 0, count - 1);
        const float cellX = originX + float(clamped) * (cellWidth + gap);
        const float progress = std::clamp(progress01, 0.0f, 1.0f);
        return cellX + cellWidth * progress;
    }

    // ---- Frame model --------------------------------------------------------

    int AnimationTimeline::FrameCount(const SpriteAnimComponent &anim) {
        if(!anim.frames.empty()) {
            return int(std::min<std::size_t>(anim.frames.size(),
                                             std::size_t(std::max(0, MaxFrames))));
        }

        return std::max(0, anim.frameCount);
    }

    int AnimationTimeline::FrameDurationMs(const SpriteAnimComponent &anim, int index) {
        if(index < 0 || index >= FrameCount(anim))
            return 0;

        if(!anim.frames.empty())
            return clampDuration(anim.frames[std::size_t(index)].durationMs);

        return legacyFrameDurationMs(anim);
    }

    int AnimationTimeline::TotalDurationMs(const SpriteAnimComponent &anim) {
        int total = 0;
        const int count = FrameCount(anim);
        if(!anim.frames.empty()) {
            for(int i = 0; i < count; ++i)
                total += clampDuration(anim.frames[std::size_t(i)].durationMs);
            return total;
        }

        return count > 0 ? count * legacyFrameDurationMs(anim) : 0;
    }

    AnimFrameRect AnimationTimeline::FrameSourceRect(const SpriteAnimComponent &anim, int index,
                                                     int sheetWidth, int sheetHeight) {
        const int fw = gridFrameWidth(anim);
        const int fh = gridFrameHeight(anim);

        if(index < 0)
            index = 0;

        if(!anim.frames.empty() && index < int(anim.frames.size())) {
            const auto &frame = anim.frames[std::size_t(index)];
            if(frame.w > 0 && frame.h > 0)
                return AnimFrameRect{frame.x, frame.y, frame.w, frame.h};
        }

        // Grid fallback: row-major across the sheet, as the renderers always laid it out. When the
        // sheet size is unknown a single row is assumed, which keeps the rect deterministic for
        // headless runs.
        const int perRow = sheetWidth > 0 ? std::max(1, sheetWidth / fw) : std::max(1, FrameCount(anim));
        const int col = index % perRow;
        const int row = index / perRow;

        return AnimFrameRect{col * fw, row * fh, fw, fh};
    }

    std::vector<TimelineFrameInfo> AnimationTimeline::BuildView(const SpriteAnimComponent &anim,
                                                               int sheetWidth, int sheetHeight) {
        std::vector<TimelineFrameInfo> out;
        const int count = FrameCount(anim);
        out.reserve(std::size_t(count));

        int start = 0;
        for(int i = 0; i < count; ++i) {
            TimelineFrameInfo info;
            info.index = i;
            info.durationMs = FrameDurationMs(anim, i);
            info.startMs = start;
            info.rect = FrameSourceRect(anim, i, sheetWidth, sheetHeight);
            out.push_back(info);
            start += info.durationMs;
        }
        return out;
    }

    float AnimationTimeline::EffectiveFps(const SpriteAnimComponent &anim) {
        const int count = FrameCount(anim);
        const int total = TotalDurationMs(anim);
        if(count <= 0 || total <= 0)
            return 0.0f;

        // Frames per second the animation actually runs at, averaged over its length.
        return (float(count) * 1000.0f) / float(total);
    }

    bool AnimationTimeline::Materialize(SpriteAnimComponent &anim) {
        if(!anim.frames.empty())
            return false;

        const int count = std::clamp(FrameCount(anim), 0, MaxFrames);
        if(count <= 0)
            return false;

        const int duration = legacyFrameDurationMs(anim);
        anim.frames.reserve(std::size_t(count));
        // Durations become explicit, the rects stay implicit (0 x 0): a frame without its own rect
        // keeps addressing the grid cell its index names, at whatever sheet size the renderer sees.
        // Baking pixel rects here would freeze in the guess made while no image size was known.
        for(int i = 0; i < count; ++i) {
            AnimFrame frame;
            frame.durationMs = duration;
            anim.frames.push_back(frame);
        }

        anim.frameCount = int(anim.frames.size());
        if(anim.currentFrame < 0 || anim.currentFrame >= anim.frameCount)
            anim.currentFrame = 0;

        return true;
    }

    int AnimationTimeline::AddFrame(SpriteAnimComponent &anim, int afterIndex) {
        Materialize(anim);

        if(int(anim.frames.size()) >= MaxFrames)
            return -1;

        if(anim.frames.empty()) {
            // An animation with no frames: seed it with one frame that uses the grid's first cell.
            AnimFrame frame;
            frame.durationMs = legacyFrameDurationMs(anim);
            anim.frames.push_back(frame);
            anim.frameCount = 1;
            anim.currentFrame = 0;
            return 0;
        }

        const int insertAt = std::clamp(afterIndex + 1, 0, int(anim.frames.size()));
        // "Add frame" appends a copy of its predecessor, which is what makes building a run of
        // similar poses fast: tweak the copy instead of authoring it from scratch.
        AnimFrame frame = anim.frames[std::size_t(std::clamp(insertAt - 1, 0,
                                                             int(anim.frames.size()) - 1))];
        anim.frames.insert(anim.frames.begin() + insertAt, frame);
        anim.frameCount = int(anim.frames.size());
        if(anim.currentFrame < 0 || anim.currentFrame >= anim.frameCount)
            anim.currentFrame = 0;
        return insertAt;
    }

    int AnimationTimeline::DuplicateFrame(SpriteAnimComponent &anim, int index) {
        Materialize(anim);

        if(index < 0 || index >= int(anim.frames.size()))
            return -1;
        if(int(anim.frames.size()) >= MaxFrames)
            return -1;

        const AnimFrame copy = anim.frames[std::size_t(index)];
        const int insertAt = index + 1;
        anim.frames.insert(anim.frames.begin() + insertAt, copy);
        anim.frameCount = int(anim.frames.size());
        if(anim.currentFrame < 0 || anim.currentFrame >= anim.frameCount)
            anim.currentFrame = 0;
        return insertAt;
    }

    bool AnimationTimeline::DeleteFrame(SpriteAnimComponent &anim, int index) {
        Materialize(anim);

        if(index < 0 || index >= int(anim.frames.size()))
            return false;

        anim.frames.erase(anim.frames.begin() + index);
        anim.frameCount = int(anim.frames.size());
        if(anim.frameCount == 0)
            anim.currentFrame = 0;
        else if(anim.currentFrame >= anim.frameCount)
            anim.currentFrame = anim.frameCount - 1;

        return true;
    }

    bool AnimationTimeline::MoveFrame(SpriteAnimComponent &anim, int from, int to) {
        Materialize(anim);

        const int count = int(anim.frames.size());
        if(from < 0 || from >= count || to < 0 || to >= count || from == to)
            return false;

        const AnimFrame moved = anim.frames[std::size_t(from)];
        anim.frames.erase(anim.frames.begin() + from);
        anim.frames.insert(anim.frames.begin() + to, moved);

        if(anim.currentFrame >= 0 && anim.currentFrame < count) {
            // Keep the current frame pointing at the same *content* when it moved.
            if(anim.currentFrame == from)
                anim.currentFrame = to;
            else if(from < anim.currentFrame && to >= anim.currentFrame)
                --anim.currentFrame;
            else if(from > anim.currentFrame && to <= anim.currentFrame)
                ++anim.currentFrame;
        }

        return true;
    }

    bool AnimationTimeline::SetFrameDuration(SpriteAnimComponent &anim, int index, int durationMs) {
        Materialize(anim);

        if(index < 0 || index >= int(anim.frames.size()))
            return false;

        const int clamped = clampDuration(durationMs);
        if(anim.frames[std::size_t(index)].durationMs == clamped)
            return false;

        anim.frames[std::size_t(index)].durationMs = clamped;
        const float fps = EffectiveFps(anim);
        if(fps > 0.0f)
            anim.fps = fps;
        return true;
    }

    bool AnimationTimeline::SetFrameRect(SpriteAnimComponent &anim, int index,
                                         const AnimFrameRect &rect) {
        Materialize(anim);

        if(index < 0 || index >= int(anim.frames.size()))
            return false;

        auto &frame = anim.frames[std::size_t(index)];
        if(frame.x == rect.x && frame.y == rect.y && frame.w == rect.w && frame.h == rect.h)
            return false;

        frame.x = std::max(0, rect.x);
        frame.y = std::max(0, rect.y);
        frame.w = rect.w > 0 ? rect.w : 0;
        frame.h = rect.h > 0 ? rect.h : 0;
        return true;
    }

    bool AnimationTimeline::RetimeToFps(SpriteAnimComponent &anim, float fps) {
        if(!(fps > 0.0f))
            return false;

        Materialize(anim);
        if(anim.frames.empty())
            return false;

        const int duration = std::max(MinFrameDurationMs, int(std::lround(1000.0f / fps)));
        bool changed = false;
        for(auto &frame : anim.frames) {
            if(frame.durationMs != duration) {
                frame.durationMs = duration;
                changed = true;
            }
        }

        if(anim.fps != fps) {
            anim.fps = fps;
            changed = true;
        }

        return changed;
    }

    // ---- Playhead -----------------------------------------------------------

    void TimelinePlayhead::Scrub(const SpriteAnimComponent &anim, int frame) {
        const int count = AnimationTimeline::FrameCount(anim);
        if(count <= 0) {
            frame_ = 0;
            elapsedMs_ = 0.0f;
            return;
        }

        frame_ = std::clamp(frame, 0, count - 1);
        elapsedMs_ = 0.0f;
    }

    void TimelinePlayhead::Reset() {
        playing_ = false;
        frame_ = 0;
        elapsedMs_ = 0.0f;
    }

    bool TimelinePlayhead::Advance(const SpriteAnimComponent &anim, float dtSeconds) {
        const int count = AnimationTimeline::FrameCount(anim);
        if(count <= 0) {
            playing_ = false;
            frame_ = 0;
            elapsedMs_ = 0.0f;
            return false;
        }

        if(frame_ < 0 || frame_ >= count)
            frame_ = 0;

        float remaining = std::max(0.0f, dtSeconds) * 1000.0f;
        if(remaining <= 0.0f)
            return false;

        const int startFrame = frame_;
        // A long frame must not spin forever; the clamp keeps a stalled editor bounded.
        int advances = 0;
        while(remaining > 0.0f && advances < 4096) {
            const int duration = std::max(1, AnimationTimeline::FrameDurationMs(anim, frame_));
            const float needed = float(duration) - elapsedMs_;
            if(remaining < needed) {
                elapsedMs_ += remaining;
                remaining = 0.0f;
                break;
            }

            remaining -= needed;
            elapsedMs_ = 0.0f;
            ++advances;

            if(frame_ + 1 >= count) {
                if(anim.loop) {
                    frame_ = 0;
                }
                else {
                    frame_ = count - 1;
                    elapsedMs_ = 0.0f;
                    playing_ = false;
                    break;
                }
            }
            else {
                ++frame_;
            }
        }

        return frame_ != startFrame;
    }

    float TimelinePlayhead::GetProgress(const SpriteAnimComponent &anim) const {
        const int total = AnimationTimeline::TotalDurationMs(anim);
        if(total <= 0)
            return 0.0f;

        int before = 0;
        for(int i = 0; i < frame_; ++i)
            before += AnimationTimeline::FrameDurationMs(anim, i);

        return std::clamp((float(before) + elapsedMs_) / float(total), 0.0f, 1.0f);
    }

    float TimelinePlayhead::GetFrameProgress(const SpriteAnimComponent &anim) const {
        const int duration = AnimationTimeline::FrameDurationMs(anim, frame_);
        if(duration <= 0)
            return 0.0f;

        return std::clamp(elapsedMs_ / float(duration), 0.0f, 1.0f);
    }

} // namespace Gorgon::Editor
