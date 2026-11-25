#pragma once

#include "Core/ScriptLog.h"

#include <Gorgon/Geometry/Point.h>
#include <Gorgon/Geometry/Rectangle.h>
#include <Gorgon/Graphics/Layer.h>
#include <Gorgon/Input/Layer.h>
#include <Gorgon/Input/Mouse.h>
#include <Gorgon/UI/Window.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Checkbox.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Layerbox.h>
#include <Gorgon/Widgets/Panel.h>

#include <functional>
#include <string>

namespace Gorgon :: Editor {

    /**
     * Bottom dock console: a scrollable, colour coded view of the play session's script log
     * (GScript Log()/Print() output, compile failures and runtime errors), with an All/Errors
     * filter and a Clear button.
     *
     * The panel owns no log: point it at a PlaySession's log with SetSource() and call Sync()
     * once a frame (or after an action); it redraws only when the log changed. The filtering and
     * scrolling live in Core::ConsoleView so they can be exercised without a window.
     */
    class ConsolePanel {
    public:
        ConsolePanel();
        ~ConsolePanel();

        ConsolePanel(const ConsolePanel &) = delete;
        ConsolePanel &operator=(const ConsolePanel &) = delete;

        /// Adds the dock panel and the log view to the window. Call before Layout().
        void Attach(Gorgon::UI::Window &window);

        /// Places the console in the given window area and lays out its controls.
        void Layout(int x, int y, int width, int height);

        Gorgon::Widgets::Panel &GetPanel() { return panel_; }
        Gorgon::Widgets::Layerbox &GetListBox() { return listBox_; }

        /// Log shown by the console. Not owned; pass nullptr to empty the view.
        void SetSource(const ScriptLog *log);
        const ScriptLog *GetSource() const { return view_.GetSource(); }

        /// Re-reads the log and redraws when it changed. Cheap enough to call every frame.
        bool Sync();
        /// Forces a redraw from the current log and filter.
        void Redraw();

        /// All/Errors filter. "Errors" keeps warnings too; only Info lines are hidden.
        void SetErrorsOnly(bool value);
        bool GetErrorsOnly() const { return view_.GetErrorsOnly(); }

        /// Empties the log this panel shows (and therefore every other viewer of it).
        void Clear();

        /// Scrolls the list by `lines` (negative scrolls up); clamped to the content.
        void ScrollBy(int lines);
        void ScrollToBottom();
        int GetScrollOffset() const { return view_.GetScrollOffset(); }
        /// Entries currently passing the filter.
        int GetLineCount() const { return view_.GetLineCount(); }
        /// Entries that fit into the view at the current size.
        int GetVisibleLineCount() const { return view_.GetVisibleLines(); }
        /// Text of the index-th entry passing the filter ("" when out of range).
        std::string GetLineText(int index) const { return view_.GetLineText(index); }
        /// "12 entries, 1 error" summary shown next to the buttons.
        std::string GetStatusText() const { return view_.GetStatusText(); }

        /// Wheel scrolling over the log area. Returns true when it consumed the event.
        bool HandleMouseScroll(Gorgon::Geometry::Point pt, float amount);

        /// Fired after the Clear button emptied the log (the host can log that it happened).
        std::function<void()> OnCleared;

    private:
        void build();
        void layoutControls(int width, int height);
        void updateControls();

        ConsoleView view_;
        // Same log the view holds, non-const so Clear() can empty it. Set in SetSource().
        ScriptLog *source_ = nullptr;

        Gorgon::Widgets::Panel panel_{Gorgon::Widgets::Registry::Panel_Bottom};
        Gorgon::Widgets::Button btnClear_{"Clear"};
        Gorgon::Widgets::Checkbox chkErrorsOnly_{"Errors only", false};
        Gorgon::Widgets::Label lblStatus_{""};
        Gorgon::Widgets::Label lblHint_{"GScript Log() / Print() output and script errors"};

        Gorgon::Widgets::Layerbox listBox_{Gorgon::Widgets::Registry::Layerbox_Blank};
        Gorgon::Graphics::Layer gfxLayer_;
        Gorgon::Input::Layer inputLayer_;
        bool attached_ = false;
        bool listAttached_ = false;

        int listTop_ = 0;
        std::string drawnStatus_;
    };

} // namespace Gorgon::Editor
