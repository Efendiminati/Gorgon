#include "ConsolePanel.h"

#include <Gorgon/Graphics/Font.h>
#include <Gorgon/UI/Dimension.h>
#include <Gorgon/Widgets/Registry.h>

#include <algorithm>
#include <cmath>

namespace Gorgon :: Editor {

    namespace {
        constexpr int kControlHeight = 24;

        Gorgon::Graphics::RGBAf Col(float r, float g, float b, float a = 1.0f) {
            return Gorgon::Graphics::RGBAf(r, g, b, a);
        }

        /// Colour of one severity: grey for output, amber for warnings, red for errors.
        Gorgon::Graphics::RGBAf SeverityColor(LogSeverity severity) {
            switch(severity) {
                case LogSeverity::Warn:  return Col(0.98f, 0.78f, 0.32f);
                case LogSeverity::Error: return Col(0.97f, 0.45f, 0.45f);
                case LogSeverity::Info:
                default:                 return Col(0.78f, 0.81f, 0.87f);
            }
        }

        /// Shortens text with a trailing ellipsis until it fits maxWidth pixels.
        std::string Ellipsize(const Gorgon::Graphics::StyledPrinter &printer, const std::string &text, int maxWidth) {
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
    } // namespace

    ConsolePanel::ConsolePanel() {
        build();
    }

    ConsolePanel::~ConsolePanel() = default;

    void ConsolePanel::build() {
        panel_.EnableScroll(false, false);

        btnClear_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        btnClear_.ClickEvent.Register([this]() { Clear(); });

        chkErrorsOnly_.SetHeight(Gorgon::UI::Pixels(kControlHeight));
        chkErrorsOnly_.ChangedEvent.Register([this]() {
            SetErrorsOnly(chkErrorsOnly_.GetState());
        });

        panel_.Add(btnClear_);
        panel_.Add(chkErrorsOnly_);
        panel_.Add(lblStatus_);
        panel_.Add(lblHint_);

        updateControls();
    }

    void ConsolePanel::Attach(Gorgon::UI::Window &window) {
        window.Add(panel_);
        window.Add(listBox_);

        if(!listAttached_) {
            gfxLayer_.EnableClipping();
            listBox_.GetLayer().Add(gfxLayer_);
            listBox_.GetLayer().Add(inputLayer_);

            inputLayer_.SetScroll([this](Gorgon::Geometry::Point pt, float amount) {
                return HandleMouseScroll(pt, amount);
            });

            listAttached_ = true;
        }

        attached_ = true;
    }

    void ConsolePanel::SetSource(const ScriptLog *log) {
        // The panel only ever reads through the view; Clear() is the one place that writes, so it
        // keeps a non-const handle to the same object.
        source_ = const_cast<ScriptLog *>(log);
        view_.SetSource(log);
        // A new source is new content: jump to the newest line, which is what the user wants to see
        // when switching to the console after pressing Play.
        view_.ScrollToBottom();
        updateControls();
        Redraw();
    }

    void ConsolePanel::layoutControls(int width, int height) {
        using Gorgon::UI::Pixels;

        const int pad = 6;
        const int clearW = 64;
        const int filterW = std::min(140, std::max(96, width / 6));

        btnClear_.Move(Pixels(pad, 5));
        btnClear_.SetWidth(Pixels(clearW));
        btnClear_.SetHeight(Pixels(kControlHeight));

        chkErrorsOnly_.Move(Pixels(pad + clearW + pad, 5));
        chkErrorsOnly_.SetWidth(Pixels(filterW));
        chkErrorsOnly_.SetHeight(Pixels(kControlHeight));

        int x = pad + clearW + pad + filterW + pad;
        const int hintWidth = 210;
        int statusWidth = width - x - pad - (width > 720 ? hintWidth + pad : 0);
        if(statusWidth < 80)
            statusWidth = 80;

        lblStatus_.Move(Pixels(x, 5));
        lblStatus_.SetWidth(Pixels(statusWidth));
        lblStatus_.SetHeight(Pixels(kControlHeight));

        if(width > 720) {
            lblHint_.SetVisible(true);
            lblHint_.Move(Pixels(width - hintWidth - pad, 5));
            lblHint_.SetWidth(Pixels(hintWidth));
            lblHint_.SetHeight(Pixels(kControlHeight));
        }
        else {
            lblHint_.SetVisible(false);
        }

        listTop_ = 5 + kControlHeight + 4;
    }

    void ConsolePanel::Layout(int x, int y, int width, int height) {
        using Gorgon::UI::Pixels;

        if(width <= 0 || height <= 0)
            return;

        panel_.Move(Pixels(x, y));
        panel_.Resize(Pixels(width, height));

        layoutControls(width, height);

        const int listX = x + 6;
        const int listY = y + listTop_;
        const int listW = std::max(32, width - 12);
        const int listH = std::max(24, height - listTop_ - 6);

        listBox_.Move(Pixels(listX, listY));
        listBox_.Resize(Pixels(listW, listH));
        listBox_.Refresh();

        Redraw();
    }

    bool ConsolePanel::Sync() {
        const bool changed = view_.Sync();
        if(!changed)
            return false;

        // A new line scrolls into view when the console is showing the newest output, and only
        // then: a user reading back through old lines should not be pushed down by a chatty script.
        const bool wasAtBottom = view_.GetScrollOffset() >= view_.GetMaxScroll() - 1;
        if(wasAtBottom)
            view_.ScrollToBottom();

        updateControls();
        Redraw();
        return true;
    }

    void ConsolePanel::SetErrorsOnly(bool value) {
        view_.SetErrorsOnly(value);
        // Re-derive the scroll range for the filtered list.
        view_.SetVisibleLines(view_.GetVisibleLines());
        view_.ScrollToBottom();
        Redraw();
    }

    void ConsolePanel::Clear() {
        view_.ScrollTo(0);
        if(source_)
            source_->Clear();

        updateControls();
        Redraw();

        if(OnCleared)
            OnCleared();
    }

    void ConsolePanel::ScrollBy(int lines) {
        view_.ScrollBy(lines);
        Redraw();
    }

    void ConsolePanel::ScrollToBottom() {
        view_.ScrollToBottom();
        Redraw();
    }

    bool ConsolePanel::HandleMouseScroll(Gorgon::Geometry::Point /*pt*/, float amount) {
        if(amount == 0.0f)
            return false;

        // Three lines per notch, matching how the asset grid steps its tiles.
        view_.ScrollBy(amount > 0.0f ? -3 : 3);
        Redraw();
        return true;
    }

    void ConsolePanel::updateControls() {
        const std::string status = view_.GetStatusText();
        if(lblStatus_.Text != status) {
            lblStatus_.Text = status;
        }
    }

    void ConsolePanel::Redraw() {
        if(!attached_ || !listAttached_)
            return;

        gfxLayer_.Clear();

        auto size = gfxLayer_.GetTargetSize();
        if(size.Width <= 0 || size.Height <= 0)
            size = listBox_.GetCurrentSize();
        if(size.Width <= 0 || size.Height <= 0)
            return;

        gfxLayer_.Draw(0.0f, 0.0f, static_cast<float>(size.Width), static_cast<float>(size.Height),
                       Col(0.10f, 0.11f, 0.14f));

        auto printer = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::FixedWidth);
        const int lineHeight = std::max(12, printer.GetHeight() + 3);
        const int textPad = 6;
        const int visible = std::max(1, (size.Height - textPad) / lineHeight);

        view_.SetVisibleLines(visible);

        const auto lines = view_.GetLines();
        if(lines.empty()) {
            printer.SetColor(Col(0.52f, 0.56f, 0.63f));
            const std::string empty = view_.GetErrorsOnly()
                ? std::string("No errors or warnings")
                : std::string("No console output yet - press Play to run scripts, or use Log()/Print() from GScript");
            printer.PrintNoWrap(gfxLayer_, empty, textPad, textPad, size.Width - textPad * 2);
            return;
        }

        const int first = view_.GetFirstLine();
        const int last = std::min(static_cast<int>(lines.size()), first + visible);

        int y = textPad;
        for(int i = first; i < last; ++i) {
            const LogEntry &entry = *lines[std::size_t(i)];

            // A faint band behind warnings and errors so a red line is findable in a wall of output.
            if(entry.severity != LogSeverity::Info) {
                const auto tint = entry.severity == LogSeverity::Error
                    ? Col(0.32f, 0.12f, 0.13f, 0.55f)
                    : Col(0.30f, 0.24f, 0.10f, 0.55f);
                gfxLayer_.Draw(0.0f, static_cast<float>(y - 1),
                               static_cast<float>(size.Width), static_cast<float>(lineHeight), tint);
            }

            printer.SetColor(SeverityColor(entry.severity));
            printer.PrintNoWrap(gfxLayer_,
                                Ellipsize(printer, entry.Format(), size.Width - textPad * 2),
                                textPad, y, size.Width - textPad * 2);
            y += lineHeight;
        }
    }

} // namespace Gorgon::Editor
