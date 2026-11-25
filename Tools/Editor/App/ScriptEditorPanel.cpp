#include "ScriptEditorPanel.h"

#include <Gorgon/UI/Dimension.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Gorgon :: Editor {

    namespace {
        constexpr int kTitleHeight = 22;
        constexpr int kButtonHeight = 26;

        std::string ReadWholeFile(const std::string &path) {
            std::ifstream in(path, std::ios::binary);
            if(!in)
                return {};

            std::ostringstream out;
            out << in.rdbuf();
            return out.str();
        }

        std::string FileNameOf(const std::string &path) {
            const std::filesystem::path p(path);
            const std::string name = p.filename().generic_string();
            return name.empty() ? path : name;
        }
    } // namespace

    ScriptEditorPanel::ScriptEditorPanel() {
        build();
    }

    ScriptEditorPanel::~ScriptEditorPanel() = default;

    void ScriptEditorPanel::build() {
        panel_.EnableScroll(false, false);

        lblTitle_.Text = "";
        textarea_.SetWordWrap(false);
        textarea_.SetAutoSelectAll(false);

        btnSave_.ClickEvent.Register([this]() { SaveInteractive(); });

        btnCheck_.ClickEvent.Register([this]() { CheckSyntax(); });

        btnClose_.ClickEvent.Register([this]() { Close(); });

        panel_.Add(lblTitle_);
        panel_.Add(textarea_);
        panel_.Add(btnSave_);
        panel_.Add(btnCheck_);
        panel_.Add(btnClose_);
        panel_.Add(lblStatus_);

        panel_.SetVisible(false);
    }

    void ScriptEditorPanel::Attach(Gorgon::UI::Window &window) {
        window.Add(panel_);
        attached_ = true;
    }

    void ScriptEditorPanel::layoutControls(int width, int height) {
        using Gorgon::UI::Pixels;

        const int pad = 8;

        lblTitle_.Move(Pixels(pad, pad));
        lblTitle_.SetWidth(Pixels(std::max(60, width - pad * 2)));
        lblTitle_.SetHeight(Pixels(kTitleHeight));

        const int buttonsTop = height - pad - kButtonHeight;
        const int areaTop = pad + kTitleHeight + 4;
        const int areaHeight = std::max(40, buttonsTop - 6 - areaTop);

        textarea_.Move(Pixels(pad, areaTop));
        textarea_.SetWidth(Pixels(std::max(80, width - pad * 2)));
        textarea_.SetHeight(Pixels(areaHeight));

        int x = pad;
        btnSave_.Move(Pixels(x, buttonsTop));
        btnSave_.SetWidth(Pixels(72));
        btnSave_.SetHeight(Pixels(kButtonHeight));
        x += 72 + 6;

        btnCheck_.Move(Pixels(x, buttonsTop));
        btnCheck_.SetWidth(Pixels(116));
        btnCheck_.SetHeight(Pixels(kButtonHeight));
        x += 116 + 6;

        btnClose_.Move(Pixels(x, buttonsTop));
        btnClose_.SetWidth(Pixels(72));
        btnClose_.SetHeight(Pixels(kButtonHeight));
        x += 72 + 10;

        lblStatus_.Move(Pixels(x, buttonsTop + 4));
        lblStatus_.SetWidth(Pixels(std::max(60, width - x - pad)));
    }

    void ScriptEditorPanel::Layout(int x, int y, int width, int height) {
        using Gorgon::UI::Pixels;

        if(width <= 0 || height <= 0)
            return;

        panel_.Move(Pixels(x, y));
        panel_.Resize(Pixels(width, height));

        layoutControls(width, height);
    }

    bool ScriptEditorPanel::OpenFile(const std::string &path, std::string *outError) {
        if(path.empty()) {
            if(outError) *outError = "No script path given.";
            return false;
        }

        std::error_code ec;
        if(!std::filesystem::exists(path, ec)) {
            if(outError) *outError = "Script not found: " + path;
            return false;
        }

        const std::string text = ReadWholeFile(path);
        std::ifstream check(path, std::ios::binary);
        if(!check) {
            if(outError) *outError = "Script could not be read: " + path;
            return false;
        }

        path_ = path;
        textarea_.SetText(text);
        savedText_ = text;
        open_ = true;
        status_ = "Opened " + FileNameOf(path);
        SetStatus(status_);
        panel_.SetVisible(true);

        if(OnOpened)
            OnOpened(path_);

        return true;
    }

    void ScriptEditorPanel::OpenBuffer(const std::string &displayPath, const std::string &text) {
        path_ = displayPath;
        textarea_.SetText(text);
        savedText_ = text;
        open_ = true;
        SetStatus(displayPath.empty() ? std::string("New script") : "Opened " + FileNameOf(displayPath));
        panel_.SetVisible(true);
    }

    void ScriptEditorPanel::Close() {
        if(!open_)
            return;

        open_ = false;
        panel_.SetVisible(false);

        if(OnClosed)
            OnClosed();
    }

    std::string ScriptEditorPanel::GetText() const {
        return textarea_.GetText();
    }

    void ScriptEditorPanel::SetText(const std::string &text) {
        textarea_.SetText(text);
        refreshTitle();
    }

    void ScriptEditorPanel::AppendLine(const std::string &line) {
        std::string text = textarea_.GetText();
        if(!text.empty() && text.back() != '\n')
            text += '\n';
        text += line;
        text += '\n';

        textarea_.SetText(text);
        refreshTitle();
    }

    bool ScriptEditorPanel::Save(std::string *outError) {
        if(path_.empty()) {
            if(outError) *outError = "This buffer has no file yet.";
            return false;
        }

        const std::filesystem::path parent = std::filesystem::path(path_).parent_path();
        if(!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        const std::string text = textarea_.GetText();
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        if(!out) {
            if(outError) *outError = "Could not write " + path_;
            return false;
        }

        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();

        if(!out) {
            if(outError) *outError = "Could not write " + path_;
            return false;
        }

        savedText_ = text;
        refreshTitle();

        if(OnSaved)
            OnSaved(path_);

        return true;
    }

    bool ScriptEditorPanel::SaveInteractive() {
        std::string error;
        if(Save(&error)) {
            SetStatus("Saved " + FileNameOf(path_));
            return true;
        }

        SetStatus(error.empty() ? "Could not save the script" : error);
        return false;
    }

    bool ScriptEditorPanel::CheckSyntax() {
        const std::string source = textarea_.GetText();
        if(source.empty()) {
            SetStatus("Nothing to check - the script is empty");
            return false;
        }

        if(!OnCheckSyntax) {
            SetStatus("Syntax checking is not available");
            return false;
        }

        std::string error;
        const bool compiled = OnCheckSyntax(source, path_, error);

        if(compiled) {
            SetStatus("Syntax OK");
        }
        else {
            // The full message also went to the console; the label shows a single-line summary.
            std::string summary = error.empty() ? std::string("Syntax error") : error;
            const auto newline = summary.find('\n');
            if(newline != std::string::npos)
                summary = summary.substr(0, newline);
            if(summary.size() > 90)
                summary = summary.substr(0, 87) + "...";

            SetStatus("Syntax error: " + summary);
        }

        return compiled;
    }

    void ScriptEditorPanel::SetStatus(const std::string &text) {
        status_ = text;
        lblStatus_.Text = text;
        refreshTitle();
    }

    std::string ScriptEditorPanel::GetTitleText() const {
        if(path_.empty())
            return {};

        std::string text = FileNameOf(path_);
        if(IsDirty())
            text += " *";

        return text;
    }

    void ScriptEditorPanel::refreshTitle() {
        std::string title = "Script Editor";
        const std::string name = GetTitleText();
        if(!name.empty())
            title += " - " + name;

        if(lblTitle_.Text != title)
            lblTitle_.Text = title;
    }

} // namespace Gorgon::Editor
