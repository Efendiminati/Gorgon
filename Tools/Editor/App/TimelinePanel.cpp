#include "TimelinePanel.h"

#include <Gorgon/Graphics/Font.h>
#include <Gorgon/UI/Dimension.h>
#include <Gorgon/Widgets/Registry.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Gorgon :: Editor {

    namespace {
        constexpr int kControlHeight = 24;
        constexpr int kPad = 6;
        constexpr float kCellWidth = 104.0f;
        constexpr float kGap = 6.0f;
        constexpr float kCellMarginY = 6.0f;

        Gorgon::Graphics::RGBAf Col(float r, float g, float b, float a = 1.0f) {
            return Gorgon::Graphics::RGBAf(r, g, b, a);
        }

        /// Shortens text with a trailing ellipsis until it fits maxWidth pixels.
        std::string Ellipsize(const Gorgon::Graphics::StyledPrinter &printer, const std::string &text,
                              int maxWidth) {
            if(maxWidth <= 0)
                return {};
            if(printer.GetSize(text).Width <= maxWidth)
                return text;

            std::string out = text;
            while(!out.empty()) {
                out.pop_back();
                if(printer.GetSize(out + "...").Width <= maxWidth)
                    return out + "...";
            }
            return "...";
        }

        /// Thin filled rectangle used for cell borders and the playhead marker.
        void FillRect(Gorgon::Graphics::Layer &layer, float x, float y, float w, float h,
                      Gorgon::Graphics::RGBAf color) {
            if(w <= 0.0f || h <= 0.0f)
                return;
            layer.Draw(x, y, w, h, color);
        }

        void DrawBorder(Gorgon::Graphics::Layer &layer, float x, float y, float w, float h,
                        Gorgon::Graphics::RGBAf color) {
            FillRect(layer, x, y, w, 1.0f, color);
            FillRect(layer, x, y + h - 1.0f, w, 1.0f, color);
            FillRect(layer, x, y, 1.0f, h, color);
            FillRect(layer, x + w - 1.0f, y, 1.0f, h, color);
        }
    } // namespace

    TimelinePanel::TimelinePanel(Scene &scene, EditorViewport &viewport)
        : scene_(scene), viewport_(viewport) {
        build();
    }

    TimelinePanel::~TimelinePanel() = default;

    void TimelinePanel::build() {
        panel_.EnableScroll(false, false);

        btnPlay_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnPlay_.ClickEvent.Register([this]() { Play(); });

        btnStop_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnStop_.ClickEvent.Register([this]() { Stop(); });

        chkLoop_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        chkLoop_.ChangedEvent.Register([this](bool state) {
            if(syncing_)
                return;
            SetLoop(state);
        });

        btnAdd_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnAdd_.ClickEvent.Register([this]() { AddFrame(); });

        btnDuplicate_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnDuplicate_.ClickEvent.Register([this]() { DuplicateFrame(); });

        btnDelete_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnDelete_.ClickEvent.Register([this]() { DeleteFrame(); });

        fpsBox_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnApplyFps_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnApplyFps_.ClickEvent.Register([this]() { RetimeToFps(fpsBox_.Get()); });

        durationBox_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        durationBox_.ChangedEvent.Register([this](int value) {
            if(syncing_)
                return;
            const int frame = std::max(0, selectedFrame_);
            SetFrameDuration(frame, value);
        });

        panel_.Add(btnPlay_);
        panel_.Add(btnStop_);
        panel_.Add(chkLoop_);
        panel_.Add(btnAdd_);
        panel_.Add(btnDuplicate_);
        panel_.Add(btnDelete_);
        panel_.Add(lblFps_);
        panel_.Add(fpsBox_);
        panel_.Add(btnApplyFps_);
        panel_.Add(lblDuration_);
        panel_.Add(durationBox_);
        panel_.Add(lblStatus_);

        updateControls();
    }

    void TimelinePanel::Attach(Gorgon::UI::Window &window) {
        window.Add(panel_);
        window.Add(stripBox_);

        if(!stripAttached_) {
            gfxLayer_.EnableClipping();
            stripBox_.GetLayer().Add(gfxLayer_);
            stripBox_.GetLayer().Add(inputLayer_);

            inputLayer_.SetDown([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
                HandleMouseDown(pt, btn);
            });
            inputLayer_.SetMove([this](Gorgon::Geometry::Point pt) { HandleMouseMove(pt); });
            inputLayer_.SetUp([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
                HandleMouseUp(pt, btn);
            });
            inputLayer_.SetScroll([this](Gorgon::Geometry::Point pt, float amount) {
                if(amount == 0.0f)
                    return false;

                const int count = GetFrameCount();
                if(count <= 0)
                    return false;

                scrollFrame_ = std::clamp(scrollFrame_ + (amount > 0.0f ? -1 : 1), 0,
                                          std::max(0, count - 1));
                Redraw();
                (void)pt;
                return true;
            });

            stripAttached_ = true;
        }

        attached_ = true;
    }

    void TimelinePanel::layoutControls(int width, int /*height*/) {
        using Gorgon::UI::Pixels;

        int x = kPad;
        auto place = [&](Gorgon::UI::Widget &widget, int w) {
            widget.Move(Pixels(x, 5));
            widget.SetWidth(Pixels(w));
            widget.SetHeight(Pixels(kControlHeight));
            x += w + 4;
        };

        place(btnPlay_, 54);
        place(btnStop_, 54);
        place(chkLoop_, 68);
        x += 4;
        place(btnAdd_, 88);
        place(btnDuplicate_, 82);
        place(btnDelete_, 100);

        x += 4;
        lblFps_.Move(Pixels(x, 5));
        lblFps_.SetWidth(Pixels(28));
        lblFps_.SetHeight(Pixels(kControlHeight));
        x += 30;
        fpsBox_.Move(Pixels(x, 5));
        fpsBox_.SetWidth(Pixels(58));
        fpsBox_.SetHeight(Pixels(kControlHeight));
        x += 62;
        place(btnApplyFps_, 68);

        x += 6;
        lblDuration_.Move(Pixels(x, 5));
        lblDuration_.SetWidth(Pixels(98));
        lblDuration_.SetHeight(Pixels(kControlHeight));
        x += 100;
        durationBox_.Move(Pixels(x, 5));
        durationBox_.SetWidth(Pixels(64));
        durationBox_.SetHeight(Pixels(kControlHeight));
        x += 68 + 8;

        lblStatus_.Move(Pixels(x, 5));
        lblStatus_.SetWidth(Pixels(std::max(60, width - x - kPad)));
        lblStatus_.SetHeight(Pixels(kControlHeight));

        stripTop_ = 5 + kControlHeight + 4;
    }

    void TimelinePanel::Layout(int x, int y, int width, int height) {
        using Gorgon::UI::Pixels;

        if(width <= 0 || height <= 0)
            return;

        panel_.Move(Pixels(x, y));
        panel_.Resize(Pixels(width, height));

        layoutControls(width, height);

        const int stripX = x + kPad;
        const int stripY = y + stripTop_;
        stripViewWidth_ = std::max(32, width - kPad * 2);
        stripHeight_ = std::max(24, height - stripTop_ - kPad);

        stripBox_.Move(Pixels(stripX, stripY));
        stripBox_.Resize(Pixels(stripViewWidth_, stripHeight_));
        stripBox_.Refresh();

        Redraw();
    }

    // ---- Model access ------------------------------------------------------

    SpriteAnimComponent *TimelinePanel::GetAnimation() {
        Entity *entity = scene_.FindEntity(GetEntityId());
        return entity && entity->spriteAnim ? &*entity->spriteAnim : nullptr;
    }

    const SpriteAnimComponent *TimelinePanel::GetAnimation() const {
        const Entity *entity = scene_.FindEntity(GetEntityId());
        return entity && entity->spriteAnim ? &*entity->spriteAnim : nullptr;
    }

    int TimelinePanel::GetFrameCount() const {
        const auto *anim = GetAnimation();
        return anim ? AnimationTimeline::FrameCount(*anim) : 0;
    }

    Gorgon::Geometry::Size TimelinePanel::sheetSize(const SpriteAnimComponent &anim) const {
        if(anim.asset.empty())
            return {0, 0};
        return viewport_.GetSpritePixelSize(anim.asset);
    }

    void TimelinePanel::Sync(bool force) {
        const EntityId id = GetEntityId();
        auto *anim = GetAnimation();
        const int count = anim ? AnimationTimeline::FrameCount(*anim) : 0;

        const bool entityChanged = id != syncedEntity_;
        if(entityChanged) {
            syncedEntity_ = id;
            playhead_.Reset();
            selectedFrame_ = count > 0 ? 0 : -1;
            scrollFrame_ = 0;
            dragFrom_ = -1;
            dragging_ = false;
            dragInsertion_ = -1;
        }

        if(count <= 0) {
            selectedFrame_ = -1;
            playhead_.Stop();
            viewport_.ClearAnimationPreview();
        }
        else {
            selectedFrame_ = std::clamp(selectedFrame_ < 0 ? 0 : selectedFrame_, 0, count - 1);
            if(playhead_.GetFrame() >= count)
                playhead_.Scrub(*anim, count - 1);
            if(!playhead_.IsPlaying())
                viewport_.SetAnimationPreview(id, selectedFrame_);
        }

        if(force || entityChanged || count > 0)
            updateControls();
        Redraw();
    }

    void TimelinePanel::SetSelectedFrame(int index) {
        const int count = GetFrameCount();
        if(count <= 0) {
            selectedFrame_ = -1;
            viewport_.ClearAnimationPreview();
            Redraw();
            return;
        }

        selectedFrame_ = std::clamp(index, 0, count - 1);
        previewSelectedFrame();
        updateControls();
        Redraw();
    }

    void TimelinePanel::previewSelectedFrame() {
        const EntityId id = GetEntityId();
        if(scenePlaying_ || id == InvalidEntityId || selectedFrame_ < 0 || GetFrameCount() <= 0) {
            if(!scenePlaying_)
                viewport_.ClearAnimationPreview();
            viewport_.Redraw();
            return;
        }

        viewport_.SetAnimationPreview(id, selectedFrame_);
        viewport_.Redraw();
        if(OnPreviewChanged)
            OnPreviewChanged();
    }

    void TimelinePanel::stopPlayback() {
        playhead_.Stop();
        previewSelectedFrame();
    }

    // ---- Editing -----------------------------------------------------------

    void TimelinePanel::afterEdit(const std::string &name, int selectFrame) {
        auto *anim = GetAnimation();
        const int count = anim ? AnimationTimeline::FrameCount(*anim) : 0;

        if(selectFrame >= 0 && count > 0)
            selectedFrame_ = std::clamp(selectFrame, 0, count - 1);
        else if(count <= 0)
            selectedFrame_ = -1;
        else
            selectedFrame_ = std::clamp(selectedFrame_, 0, count - 1);

        if(count > 0 && playhead_.GetFrame() >= count)
            playhead_.Scrub(*anim, count - 1);

        updateControls();
        previewSelectedFrame();
        Redraw();

        if(OnEdited)
            OnEdited(name);
    }

    int TimelinePanel::AddFrame() {
        auto *anim = GetAnimation();
        if(!anim)
            return -1;

        const int last = AnimationTimeline::FrameCount(*anim) - 1;
        const int index = AnimationTimeline::AddFrame(*anim, last);
        if(index < 0)
            return -1;

        afterEdit("Add Frame", index);
        return index;
    }

    int TimelinePanel::DuplicateFrame() {
        auto *anim = GetAnimation();
        if(!anim)
            return -1;

        const int count = AnimationTimeline::FrameCount(*anim);
        if(count <= 0)
            return -1;

        const int index = std::clamp(selectedFrame_ < 0 ? 0 : selectedFrame_, 0, count - 1);
        const int inserted = AnimationTimeline::DuplicateFrame(*anim, index);
        if(inserted < 0)
            return -1;

        afterEdit("Duplicate Frame", inserted);
        return inserted;
    }

    bool TimelinePanel::DeleteFrame() {
        auto *anim = GetAnimation();
        if(!anim)
            return false;

        const int count = AnimationTimeline::FrameCount(*anim);
        if(count <= 0)
            return false;

        const int index = std::clamp(selectedFrame_ < 0 ? 0 : selectedFrame_, 0, count - 1);
        if(!AnimationTimeline::DeleteFrame(*anim, index))
            return false;

        const int remaining = AnimationTimeline::FrameCount(*anim);
        afterEdit("Delete Frame", remaining > 0 ? std::min(index, remaining - 1) : -1);
        return true;
    }

    bool TimelinePanel::MoveFrame(int from, int to) {
        auto *anim = GetAnimation();
        if(!anim)
            return false;
        if(!AnimationTimeline::MoveFrame(*anim, from, to))
            return false;

        afterEdit("Reorder Frames", to);
        return true;
    }

    bool TimelinePanel::SetFrameDuration(int index, int durationMs) {
        auto *anim = GetAnimation();
        if(!anim)
            return false;
        if(!AnimationTimeline::SetFrameDuration(*anim, index, durationMs))
            return false;

        afterEdit("Set Frame Duration", index);
        return true;
    }

    bool TimelinePanel::RetimeToFps(float fps) {
        auto *anim = GetAnimation();
        if(!anim)
            return false;
        if(!AnimationTimeline::RetimeToFps(*anim, fps))
            return false;

        afterEdit("Retime Frames", selectedFrame_);
        return true;
    }

    bool TimelinePanel::SetFrameRect(int index, const AnimFrameRect &rect) {
        auto *anim = GetAnimation();
        if(!anim)
            return false;
        if(!AnimationTimeline::SetFrameRect(*anim, index, rect))
            return false;

        afterEdit("Set Frame Rect", index);
        return true;
    }

    // ---- Playhead ----------------------------------------------------------

    void TimelinePanel::Play() {
        auto *anim = GetAnimation();
        if(!anim || AnimationTimeline::FrameCount(*anim) <= 0)
            return;

        const int start = selectedFrame_ < 0 ? 0 : selectedFrame_;
        playhead_.Scrub(*anim, start);
        playhead_.Play();
        selectedFrame_ = playhead_.GetFrame();
        updateControls();
        Redraw();
    }

    void TimelinePanel::Stop() {
        if(!playhead_.IsPlaying())
            return;

        stopPlayback();
        updateControls();
    }

    void TimelinePanel::TogglePlay() {
        if(playhead_.IsPlaying())
            Stop();
        else
            Play();
    }

    bool TimelinePanel::Tick(float dt) {
        if(!playhead_.IsPlaying())
            return false;

        auto *anim = GetAnimation();
        if(!anim) {
            playhead_.Stop();
            updateControls();
            return false;
        }

        const bool changed = playhead_.Advance(*anim, dt);
        if(!changed)
            return false;

        const int count = AnimationTimeline::FrameCount(*anim);
        selectedFrame_ = count > 0 ? std::clamp(playhead_.GetFrame(), 0, count - 1) : -1;
        previewSelectedFrame();
        updateControls();
        Redraw();

        // A non-looping animation stops itself on the last frame; the buttons must follow.
        if(!playhead_.IsPlaying())
            updateControls();

        return true;
    }

    bool TimelinePanel::GetLoop() const {
        if(const auto *anim = GetAnimation())
            return anim->loop;
        return true;
    }

    void TimelinePanel::SetLoop(bool loop) {
        auto *anim = GetAnimation();
        if(!anim || anim->loop == loop) {
            updateControls();
            return;
        }

        anim->loop = loop;
        if(OnEdited)
            OnEdited("Set Loop");
        updateControls();
    }

    void TimelinePanel::SetScenePlaying(bool playing) {
        scenePlaying_ = playing;
        if(playing) {
            playhead_.Stop();
            viewport_.ClearAnimationPreview();
            viewport_.Redraw();
        }
        else {
            previewSelectedFrame();
        }

        updateControls();
        Redraw();
    }

    // ---- Presentation ------------------------------------------------------

    std::string TimelinePanel::GetHint() const {
        const EntityId id = GetEntityId();
        if(id == InvalidEntityId)
            return "Select an entity with an animation to build its timeline";

        const Entity *entity = scene_.FindEntity(id);
        if(!entity)
            return "Select an entity with an animation to build its timeline";

        if(!entity->spriteAnim)
            return "'" + entity->name + "' has no animation - add one from the Details panel";

        if(AnimationTimeline::FrameCount(*entity->spriteAnim) <= 0)
            return "This animation has no frames - press Add Frame to create the first one";

        return {};
    }

    std::string TimelinePanel::GetStatusText() const {
        const auto *anim = GetAnimation();
        if(!anim)
            return {};

        const int count = AnimationTimeline::FrameCount(*anim);
        if(count <= 0)
            return "no frames";

        const int total = AnimationTimeline::TotalDurationMs(*anim);
        const float fps = AnimationTimeline::EffectiveFps(*anim);

        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%d frames - %d ms - %.1f fps", count, total,
                      double(fps));
        return buffer;
    }

    int TimelinePanel::GetVisibleFrameCount() const {
        return std::max(0, layout_.count);
    }

    void TimelinePanel::updateControls() {
        syncing_ = true;

        const auto *anim = GetAnimation();
        const int count = anim ? AnimationTimeline::FrameCount(*anim) : 0;
        const bool hasAnim = anim != nullptr && count > 0;

        btnPlay_.SetEnabled(hasAnim);
        btnStop_.SetEnabled(playhead_.IsPlaying());
        btnAdd_.SetEnabled(anim != nullptr);
        btnDuplicate_.SetEnabled(hasAnim);
        btnDelete_.SetEnabled(hasAnim);
        durationBox_.SetEnabled(hasAnim);
        fpsBox_.SetEnabled(anim != nullptr);
        btnApplyFps_.SetEnabled(hasAnim);
        chkLoop_.SetEnabled(anim != nullptr);

        btnPlay_.Text = playhead_.IsPlaying() ? "Playing" : "Play";

        if(anim)
            chkLoop_.SetState(anim->loop);

        if(anim) {
            if(!fpsBox_.IsFocused()) {
                const float fps = AnimationTimeline::EffectiveFps(*anim);
                fpsBox_.Set(fps > 0.0f ? fps : anim->fps);
            }

            if(hasAnim && !durationBox_.IsFocused()) {
                durationBox_.Set(AnimationTimeline::FrameDurationMs(*anim,
                                                                    std::max(0, selectedFrame_)));
            }
        }

        lblDuration_.Text = hasAnim
            ? ("Frame " + std::to_string(std::max(0, selectedFrame_)) + " (ms)")
            : std::string("Duration (ms)");

        lblStatus_.Text = GetStatusText();

        syncing_ = false;
    }

    // ---- Drawing -----------------------------------------------------------

    void TimelinePanel::Redraw() {
        if(!attached_ || !stripAttached_)
            return;

        gfxLayer_.Clear();

        auto size = gfxLayer_.GetTargetSize();
        if(size.Width <= 0 || size.Height <= 0)
            size = stripBox_.GetCurrentSize();
        if(size.Width <= 0 || size.Height <= 0)
            return;

        const float viewW = static_cast<float>(size.Width);
        const float viewH = static_cast<float>(size.Height);
        gfxLayer_.Draw(0.0f, 0.0f, viewW, viewH, Col(0.09f, 0.10f, 0.13f));

        auto printer = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::Info);
        auto small = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::Small);

        const auto *anim = GetAnimation();
        const int count = anim ? AnimationTimeline::FrameCount(*anim) : 0;

        const std::string hint = GetHint();
        if(count <= 0 || !anim) {
            layout_ = TimelineStripLayout::Compute(0.0f, 0.0f, 0);
            printer.SetColor(Col(0.52f, 0.56f, 0.63f));
            const std::string message = hint.empty() ? std::string("No frames") : hint;
            const auto textSize = printer.GetSize(message);
            printer.PrintNoWrap(gfxLayer_, message, std::max(6, (size.Width - textSize.Width) / 2),
                                std::max(4, (size.Height - textSize.Height) / 2), size.Width - 12);
            return;
        }

        const float cellHeight = std::max(40.0f, viewH - kCellMarginY * 2.0f);
        const float originX = kPad - static_cast<float>(scrollFrame_) * (kCellWidth + kGap);
        layout_ = TimelineStripLayout::Compute(originX, kCellMarginY, count, kCellWidth, cellHeight,
                                               kGap);

        const Gorgon::Geometry::Size sheet = sheetSize(*anim);
        const int playheadFrame = std::clamp(playhead_.GetFrame(), 0, count - 1);

        for(int i = 0; i < count; ++i) {
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            if(!layout_.CellRect(i, cx, cy, cw, ch))
                break;
            // Skip cells scrolled out of the strip; nothing outside it would be visible anyway.
            if(cx + cw < 0.0f || cx >= viewW)
                continue;

            const bool selected = i == selectedFrame_;
            const bool isPlayhead = i == playheadFrame && (playhead_.IsPlaying() || selected);

            FillRect(gfxLayer_, cx, cy, cw, ch,
                     selected ? Col(0.21f, 0.26f, 0.37f) : Col(0.14f, 0.16f, 0.21f));
            DrawBorder(gfxLayer_, cx, cy, cw, ch,
                       selected ? Col(0.52f, 0.70f, 0.95f) : Col(0.26f, 0.29f, 0.36f));

            const int durationMs = AnimationTimeline::FrameDurationMs(*anim, i);
            const auto rect = AnimationTimeline::FrameSourceRect(*anim, i, sheet.Width, sheet.Height);

            printer.SetColor(selected ? Col(0.92f, 0.95f, 1.0f) : Col(0.76f, 0.80f, 0.87f));
            printer.PrintNoWrap(gfxLayer_, "#" + std::to_string(i), int(cx) + 8, int(cy) + 6,
                                int(cw) - 16);

            small.SetColor(Col(0.60f, 0.66f, 0.76f));
            const bool ownRect = i < int(anim->frames.size()) &&
                                 anim->frames[std::size_t(i)].w > 0 &&
                                 anim->frames[std::size_t(i)].h > 0;
            small.PrintNoWrap(gfxLayer_,
                              std::to_string(rect.x) + "," + std::to_string(rect.y) + " " +
                                  std::to_string(rect.w) + "x" + std::to_string(rect.h) +
                                  (ownRect ? " *" : ""),
                              int(cx) + 8, int(cy) + 24, int(cw) - 16);

            // Duration box: the rectangle the mouse handler hit-tests for in-place editing.
            float dx = 0.0f, dy = 0.0f, dw = 0.0f, dh = 0.0f;
            if(layout_.DurationRect(i, dx, dy, dw, dh)) {
                FillRect(gfxLayer_, dx, dy, dw, dh,
                         selected ? Col(0.30f, 0.36f, 0.49f) : Col(0.20f, 0.23f, 0.30f));
                printer.SetColor(Col(0.90f, 0.93f, 0.98f));
                printer.PrintNoWrap(gfxLayer_, std::to_string(durationMs) + " ms", int(dx) + 6,
                                    int(dy) + 3, int(dw) - 10);
            }
        }

        // Playhead marker: a bar over the frame the animation is showing, moving across the frame
        // while it plays so the wall-clock position is visible.
        if(playheadFrame >= 0) {
            const float markerX = layout_.PlayheadX(playheadFrame,
                                                    playhead_.GetFrameProgress(*anim));
            FillRect(gfxLayer_, markerX - 1.0f, 0.0f, 2.0f, viewH, Col(0.98f, 0.42f, 0.35f));
            const float cellY = kCellMarginY;
            FillRect(gfxLayer_, markerX - 5.0f, cellY - 1.0f, 10.0f, 5.0f,
                     Col(0.98f, 0.42f, 0.35f));
        }

        // Drop indicator while a frame is dragged to a new position.
        if(dragging_ && dragInsertion_ >= 0) {
            const int clamped = std::clamp(dragInsertion_, 0, count);
            float markerX = originX + static_cast<float>(clamped) * (kCellWidth + kGap) - kGap * 0.5f;
            markerX = std::clamp(markerX, 1.0f, viewW - 2.0f);
            FillRect(gfxLayer_, markerX - 1.5f, 1.0f, 3.0f, viewH - 2.0f, Col(0.98f, 0.78f, 0.32f));
        }

        // Scroll hint when the strip does not fit every frame.
        const int visibleCells = std::max(1, int((viewW - kPad) / (kCellWidth + kGap)));
        if(count > visibleCells) {
            small.SetColor(Col(0.62f, 0.66f, 0.74f));
            const std::string label = "frames " + std::to_string(scrollFrame_ + 1) + "+ of " +
                                      std::to_string(count) + " (scroll)";
            small.PrintNoWrap(gfxLayer_, label, int(viewW) - 170, 1, 164);
        }
    }

    // ---- Mouse -------------------------------------------------------------

    bool TimelinePanel::HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
        if(btn != Gorgon::Input::Mouse::Button::Left)
            return false;

        if(GetFrameCount() <= 0)
            return false;

        const int hit = layout_.HitTest(static_cast<float>(pt.X), static_cast<float>(pt.Y));
        if(hit < 0)
            return false;

        SetSelectedFrame(hit);
        dragFrom_ = hit;
        dragStart_ = pt;
        dragging_ = false;
        dragInsertion_ = -1;

        // Clicking the duration box of a cell drops the caret into the duration field, so editing a
        // frame's timing is one click plus typing.
        if(layout_.HitTestDuration(static_cast<float>(pt.X), static_cast<float>(pt.Y)) == hit)
            durationBox_.Focus();

        Redraw();
        return true;
    }

    bool TimelinePanel::HandleMouseMove(Gorgon::Geometry::Point pt) {
        if(dragFrom_ < 0)
            return false;

        // A press that stays put is a selection click; only a real horizontal move starts a reorder.
        if(!dragging_ && std::abs(pt.X - dragStart_.X) < 6)
            return false;

        dragging_ = true;
        dragInsertion_ = layout_.InsertionIndex(static_cast<float>(pt.X));
        Redraw();
        return true;
    }

    bool TimelinePanel::HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
        if(btn != Gorgon::Input::Mouse::Button::Left || dragFrom_ < 0)
            return false;

        const int from = dragFrom_;
        const bool wasDragging = dragging_;
        const int insertion = dragInsertion_;

        dragFrom_ = -1;
        dragging_ = false;
        dragInsertion_ = -1;

        if(!wasDragging) {
            Redraw();
            return true;
        }

        const int count = GetFrameCount();
        int to = insertion;
        if(to > from)
            --to;   // the dragged frame leaves a hole behind it
        to = std::clamp(to, 0, std::max(0, count - 1));

        (void)pt;
        if(to != from)
            MoveFrame(from, to);
        else
            Redraw();

        return true;
    }

} // namespace Gorgon::Editor
