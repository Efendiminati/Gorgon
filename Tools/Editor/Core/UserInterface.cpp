#include "UserInterface.h"

#include <algorithm>
#include <cmath>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kTextInset = 8.0f;

        Gorgon::Graphics::RGBAf ToRGBAf(const ColorRGBA &c) {
            return Gorgon::Graphics::RGBAf(c.r / 255.0f, c.g / 255.0f,
                                           c.b / 255.0f, c.a / 255.0f);
        }

        /// Draw order: panels sit under everything, labels over panels, buttons over both. Within a
        /// kind the scene order is kept, so the list doubles as the hit-test order read backwards.
        int KindRank(UIElementKind kind) {
            switch(kind) {
                case UIElementKind::Panel:  return 0;
                case UIElementKind::Label:  return 1;
                case UIElementKind::Button:
                default:                    return 2;
            }
        }
    } // namespace

    UIScreenRect ComputeUIElementRect(const UIElementComponent &element, float viewWidth,
                                      float viewHeight) {
        const float w = std::max(0.0f, element.width);
        const float h = std::max(0.0f, element.height);

        float x = 0.0f;
        switch(element.anchorH) {
            case UIAnchorH::Center: x = viewWidth * 0.5f + element.x - w * 0.5f; break;
            case UIAnchorH::Right:  x = viewWidth - element.x - w;               break;
            case UIAnchorH::Left:
            default:                x = element.x;                               break;
        }

        float y = 0.0f;
        switch(element.anchorV) {
            case UIAnchorV::Middle: y = viewHeight * 0.5f + element.y - h * 0.5f; break;
            case UIAnchorV::Bottom: y = viewHeight - element.y - h;               break;
            case UIAnchorV::Top:
            default:                y = element.y;                                break;
        }

        return UIScreenRect{x, y, w, h};
    }

    void UserInterface::Layout(const Scene &scene, int viewWidth, int viewHeight) {
        // A re-layout (the host may call one before drawing, after updating input) keeps the pointer
        // state of widgets that are still there: the click a script just read must survive the draw.
        std::vector<UIWidgetState> previous;
        previous.swap(widgets_);

        viewWidth_ = std::max(0, viewWidth);
        viewHeight_ = std::max(0, viewHeight);
        if(viewWidth_ <= 0 || viewHeight_ <= 0) {
            Reset();
            return;
        }

        const float w = float(viewWidth_);
        const float h = float(viewHeight_);

        for(const auto &entity : scene.GetEntities()) {
            if(!entity.ui.has_value() || !entity.visible || !entity.ui->visible)
                continue;

            UIWidgetState widget;
            widget.entity = entity.id;
            widget.id = entity.ui->id;
            widget.kind = entity.ui->kind;
            widget.element = *entity.ui;
            widget.rect = ComputeUIElementRect(*entity.ui, w, h);

            for(const auto &old : previous) {
                if(old.entity != widget.entity)
                    continue;
                widget.hovered = old.hovered;
                widget.pressed = old.pressed;
                widget.clicked = old.clicked;
                break;
            }

            widgets_.push_back(std::move(widget));
        }

        std::stable_sort(widgets_.begin(), widgets_.end(),
                         [](const UIWidgetState &lhs, const UIWidgetState &rhs) {
                             return KindRank(lhs.kind) < KindRank(rhs.kind);
                         });

        // A widget that vanished from the scene cannot stay the press target.
        if(pressTarget_ != InvalidEntityId && !GetWidget(pressTarget_))
            pressTarget_ = InvalidEntityId;
    }

    void UserInterface::UpdateInput(float screenX, float screenY, bool primaryDown) {
        // A click lives for exactly one frame: whatever the previous frame reported is gone now.
        for(auto &widget : widgets_)
            widget.clicked = false;

        const UIWidgetState *hit = HitTest(screenX, screenY);

        for(auto &widget : widgets_) {
            const bool isHit = hit != nullptr && hit->entity == widget.entity;
            widget.hovered = isHit;

            if(widget.kind != UIElementKind::Button) {
                widget.pressed = false;
                continue;
            }

            if(primaryDown) {
                // The press starts on the widget under the button-down, and stays with it: dragging
                // off the button and releasing elsewhere is not a click.
                if(!primaryDown_ && isHit)
                    pressTarget_ = widget.entity;

                // Pressed only while the pointer is still on the widget: dragging off pops the
                // button back up, and the release then lands somewhere that is not a click.
                widget.pressed = pressTarget_ == widget.entity && isHit;
            }
            else {
                if(primaryDown_ && pressTarget_ == widget.entity && isHit)
                    widget.clicked = true;

                widget.pressed = false;
            }
        }

        if(!primaryDown && primaryDown_)
            pressTarget_ = InvalidEntityId;

        primaryDown_ = primaryDown;
    }

    void UserInterface::Reset() {
        for(auto &widget : widgets_) {
            widget.hovered = false;
            widget.pressed = false;
            widget.clicked = false;
        }
        pressTarget_ = InvalidEntityId;
        primaryDown_ = false;
    }

    bool UserInterface::FindWidget(const std::string &id, UIWidgetState &out) const {
        const UIWidgetState *widget = GetWidget(id);
        if(!widget)
            return false;

        out = *widget;
        return true;
    }

    const UIWidgetState *UserInterface::GetWidget(const std::string &id) const {
        if(id.empty())
            return nullptr;

        for(const auto &widget : widgets_) {
            if(widget.id == id)
                return &widget;
        }
        return nullptr;
    }

    const UIWidgetState *UserInterface::GetWidget(EntityId entity) const {
        if(entity == InvalidEntityId)
            return nullptr;

        for(const auto &widget : widgets_) {
            if(widget.entity == entity)
                return &widget;
        }
        return nullptr;
    }

    bool UserInterface::IsHovered(const std::string &id) const {
        const auto *widget = GetWidget(id);
        return widget != nullptr && widget->hovered;
    }

    bool UserInterface::IsClicked(const std::string &id) const {
        const auto *widget = GetWidget(id);
        return widget != nullptr && widget->clicked;
    }

    bool UserInterface::IsPressed(const std::string &id) const {
        const auto *widget = GetWidget(id);
        return widget != nullptr && widget->pressed;
    }

    const UIWidgetState *UserInterface::HitTest(float screenX, float screenY) const {
        // The list is in draw order, so the last button containing the point is the visible one.
        for(auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
            if(it->kind != UIElementKind::Button)
                continue;
            if(it->rect.Contains(screenX, screenY))
                return &(*it);
        }
        return nullptr;
    }

    int UserInterface::CountHovered() const {
        return int(std::count_if(widgets_.begin(), widgets_.end(),
                                 [](const UIWidgetState &w) { return w.hovered; }));
    }

    int UserInterface::CountPressed() const {
        return int(std::count_if(widgets_.begin(), widgets_.end(),
                                 [](const UIWidgetState &w) { return w.pressed; }));
    }

    int UserInterface::CountClicked() const {
        return int(std::count_if(widgets_.begin(), widgets_.end(),
                                 [](const UIWidgetState &w) { return w.clicked; }));
    }

    int UserInterface::Render(Gorgon::Graphics::Layer &target,
                              const Gorgon::Graphics::StyledPrinter *printer,
                              const FontProvider &fontForSize) const {
        lastDrawnQuads_ = 0;
        if(viewWidth_ <= 0 || viewHeight_ <= 0)
            return 0;

        for(const auto &widget : widgets_) {
            const auto &element = widget.element;
            const auto &rect = widget.rect;

            ColorRGBA fill = element.background;
            if(widget.kind == UIElementKind::Button) {
                if(widget.pressed)
                    fill = element.pressedBackground;
                else if(widget.hovered)
                    fill = element.hoverBackground;
            }

            if(fill.a > 0 && rect.width > 0.0f && rect.height > 0.0f) {
                target.Draw(rect.x, rect.y, rect.width, rect.height, ToRGBAf(fill));
                ++lastDrawnQuads_;
            }

            if(widget.kind == UIElementKind::Panel)
                continue;

            const std::string &text = element.text;
            if(text.empty())
                continue;

            const Gorgon::Graphics::StyledPrinter *font = printer;
            if(fontForSize) {
                if(const auto *candidate = fontForSize(element.fontSize))
                    font = candidate;
            }

            if(font) {
                auto styled = *font;
                styled.SetColor(ToRGBAf(element.textColor));
                const auto textSize = styled.GetSize(text);
                const int x = int(std::lround(rect.CenterX() - float(textSize.Width) * 0.5f));
                const int y = int(std::lround(rect.CenterY() - float(textSize.Height) * 0.5f));
                styled.PrintNoWrap(target, text, x, y, std::max(1, textSize.Width),
                                   Gorgon::Graphics::TextAlignment::Left);
                ++lastDrawnQuads_;
            }
            else {
                // No font available (a headless run): a bar in the text colour, sized from the
                // element's font size, so the layout is still visible.
                const float barWidth = std::min(std::max(0.0f, rect.width - kTextInset * 2.0f),
                                                float(text.size()) * element.fontSize * 0.55f);
                const float barHeight = std::min(std::max(0.0f, rect.height - kTextInset),
                                                 element.fontSize);
                if(barWidth > 1.0f && barHeight > 1.0f) {
                    target.Draw(rect.CenterX() - barWidth * 0.5f,
                                rect.CenterY() - barHeight * 0.5f,
                                barWidth, barHeight, ToRGBAf(element.textColor));
                    ++lastDrawnQuads_;
                }
            }
        }

        return lastDrawnQuads_;
    }

} // namespace Gorgon::Editor
