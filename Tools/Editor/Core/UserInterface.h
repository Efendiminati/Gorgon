#pragma once

#include "Scene.h"

#include <Gorgon/Graphics/Font.h>
#include <Gorgon/Graphics/Layer.h>

#include <functional>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// A rectangle on the screen, in pixels, with the origin in the top-left corner of the view.
    struct UIScreenRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        float Right() const { return x + width; }
        float Bottom() const { return y + height; }
        float CenterX() const { return x + width * 0.5f; }
        float CenterY() const { return y + height * 0.5f; }

        /// Half-open containment: the left/top edges are inside, the right/bottom edges are not.
        bool Contains(float pointX, float pointY) const {
            return pointX >= x && pointX < Right() && pointY >= y && pointY < Bottom();
        }

        bool operator==(const UIScreenRect &other) const {
            return x == other.x && y == other.y &&
                   width == other.width && height == other.height;
        }
        bool operator!=(const UIScreenRect &other) const { return !(*this == other); }
    };

    /**
     * Screen rectangle a UI element occupies on a `viewWidth x viewHeight` view.
     *
     * The element hangs off the screen edge its anchors name and `x`/`y` are the gap from that
     * edge, so a right-anchored element keeps its distance to the right edge when the window is
     * resized. For the centre anchors `x`/`y` shift the element off-centre instead (positive moves
     * right/down), which keeps a centred button centred at every size.
     */
    UIScreenRect ComputeUIElementRect(const UIElementComponent &element, float viewWidth,
                                      float viewHeight);

    /// One widget of the current layout, plus the pointer state it reported on the last frame.
    struct UIWidgetState {
        EntityId entity = InvalidEntityId;
        /// Handle scripts use; empty when the author did not give the element an id.
        std::string id;
        UIElementKind kind = UIElementKind::Button;
        /// The component as it was when the layout was built. A script's UI_SetText reaches the
        /// screen on the next Layout(), which is the frame after the script ran.
        UIElementComponent element;
        UIScreenRect rect;
        /// True when the pointer is over the widget this frame.
        bool hovered = false;
        /// True while the primary button is held down over the widget.
        bool pressed = false;
        /// True for exactly one frame: the frame the primary button was released over the widget.
        bool clicked = false;
    };

    /**
     * The in-game UI layer: a headless widget model plus a `Graphics::Layer` renderer.
     *
     * `Layout()` turns the scene's UIElementComponents into screen rectangles (anchored, so a
     * resize keeps everything pinned), `UpdateInput()` advances one frame of pointer state — hover,
     * press and a one-shot click — and `Render()` draws them. Nothing in here needs a window, so
     * both the player and the tests drive the same code; only Render touches a graphics layer.
     *
     * Only **buttons** are hit-tested: a panel or a label behind a button never steals its click,
     * and a label can never be the target of `IsClicked`. Draw order is panels first, then labels,
     * then buttons (scene order within each kind), so a button always sits on top of the panel it
     * was placed on and the topmost button wins a hit test.
     *
     * A widget's `id` is what scripts query (`UI_IsClicked("start")`). An element without an id
     * keeps its click for the drawing (hover/pressed colours) but cannot be addressed by a script.
     */
    class UserInterface {
    public:
        /// Resolves a printer for a font size, so a host can serve real per-size fonts. Falls back
        /// to the single printer passed to Render() when empty or when it returns nullptr.
        using FontProvider = std::function<const Gorgon::Graphics::StyledPrinter *(float fontSize)>;

        /// Rebuilds the widget list from every visible entity of `scene` that carries a UI element.
        /// A non-positive view size yields an empty layout.
        void Layout(const Scene &scene, int viewWidth, int viewHeight);

        /// Advances one frame of pointer input in **screen pixels** (origin top-left of the view).
        /// Hover and press follow the pointer; `clicked` is set on the frame the primary button is
        /// released over the widget it was pressed on and cleared by the next call, so a click is
        /// reported for exactly one frame no matter how many scripts ask.
        void UpdateInput(float screenX, float screenY, bool primaryDown);

        /// Forgets hover/press/click state (and the press target). The layout is kept.
        void Reset();

        /// Copies the widget with this id into `out`. Returns false when the current layout has no
        /// widget with that id (or the id is empty).
        bool FindWidget(const std::string &id, UIWidgetState &out) const;

        int GetWidgetCount() const { return int(widgets_.size()); }
        const std::vector<UIWidgetState> &GetWidgets() const { return widgets_; }
        const UIWidgetState *GetWidget(const std::string &id) const;
        const UIWidgetState *GetWidget(EntityId entity) const;

        bool IsHovered(const std::string &id) const;
        bool IsClicked(const std::string &id) const;
        bool IsPressed(const std::string &id) const;

        /// Topmost button under a screen point, or nullptr. Only buttons are hit-testable.
        const UIWidgetState *HitTest(float screenX, float screenY) const;

        /// Draws the widgets laid out by Layout() in screen space. `printer` renders the text;
        /// without one (or with a `fontForSize` that has no font for the size) the text falls back
        /// to a translucent bar, which is what a headless run shows. Returns the number of quads
        /// drawn, which is what the render smoke test asserts on.
        int Render(Gorgon::Graphics::Layer &target,
                   const Gorgon::Graphics::StyledPrinter *printer = nullptr,
                   const FontProvider &fontForSize = FontProvider()) const;

        int GetLastDrawnQuadCount() const { return lastDrawnQuads_; }
        /// Forgets the last draw count. A host that skipped the UI pass calls this so
        /// GetLastDrawnQuadCount() reports 0 instead of the previous frame's total.
        void ResetDrawCount() { lastDrawnQuads_ = 0; }
        int GetViewWidth() const { return viewWidth_; }
        int GetViewHeight() const { return viewHeight_; }

        /// Number of widgets that reported hover / press / click on the last frame (test helper).
        int CountHovered() const;
        int CountPressed() const;
        int CountClicked() const;

    private:
        std::vector<UIWidgetState> widgets_;
        EntityId pressTarget_ = InvalidEntityId;
        bool primaryDown_ = false;
        int viewWidth_ = 0;
        int viewHeight_ = 0;
        mutable int lastDrawnQuads_ = 0;
    };

} // namespace Gorgon::Editor
