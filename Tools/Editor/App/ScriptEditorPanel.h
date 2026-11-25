#pragma once

#include <Gorgon/UI/Window.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Panel.h>
#include <Gorgon/Widgets/Textarea.h>

#include <functional>
#include <string>

namespace Gorgon :: Editor {

    /**
     * Built-in .gs script editor.
     *
     * Opened from the Inspector's "Edit Script" button or by double clicking a script in the asset
     * browser, it shows the file in a multi-line text area with Save (Ctrl+S), Check Syntax (compiles
     * the buffer through a GScript VM without running it and pushes the result to the console) and
     * Close. The widget never decides what "checking" means: the host installs OnCheckSyntax, which
     * is the same compile path play-in-editor uses, so a script that passes here will also compile
     * when Play runs it.
     */
    class ScriptEditorPanel {
    public:
        ScriptEditorPanel();
        ~ScriptEditorPanel();

        ScriptEditorPanel(const ScriptEditorPanel &) = delete;
        ScriptEditorPanel &operator=(const ScriptEditorPanel &) = delete;

        /// Adds the editor window to the given window. Call before Layout()/OpenFile().
        void Attach(Gorgon::UI::Window &window);

        /// Places the editor in the given window area (the editor overlays the viewport).
        void Layout(int x, int y, int width, int height);

        /// Reads `path` into the buffer and shows the editor. Returns false (with a message) when the
        /// file cannot be read; the panel then stays closed.
        bool OpenFile(const std::string &path, std::string *outError = nullptr);

        /// Shows the editor on an in-memory buffer (no file behind it until Save()).
        void OpenBuffer(const std::string &displayPath, const std::string &text);

        /// Hides the editor. Unsaved changes are kept in the buffer, not written.
        void Close();

        bool IsOpen() const { return open_; }
        const std::string &GetPath() const { return path_; }
        /// True when the buffer differs from the text last read or written.
        bool IsDirty() const { return GetText() != savedText_; }

        std::string GetText() const;
        /// Replaces the buffer (a following IsDirty() compares against the loaded text).
        void SetText(const std::string &text);
        /// Appends one line, the way a host or a test builds a script up.
        void AppendLine(const std::string &line);

        /// Writes the buffer to GetPath(), creating the parent folder when needed.
        bool Save(std::string *outError = nullptr);

        /// Save() plus a status label update: what the Save button and Ctrl+S do.
        bool SaveInteractive();

        /// Compiles the buffer without executing it and reports the outcome in the status label.
        bool CheckSyntax();

        std::string GetStatus() const { return status_; }
        void SetStatus(const std::string &text);

        /// "<name> *" while the buffer is dirty, "<name>" otherwise. "" when nothing is open.
        std::string GetTitleText() const;

        Gorgon::Widgets::Panel &GetPanel() { return panel_; }
        Gorgon::Widgets::Textarea &GetTextarea() { return textarea_; }

        /// Compiles `source` the way a play script is compiled. Returns true when it compiles; on
        /// failure `error` carries the compiler message. Installed by EditorApp.
        std::function<bool(const std::string &source, const std::string &path, std::string &error)> OnCheckSyntax;

        /// Fired after Save() wrote the file (the host refreshes the browser and logs it).
        std::function<void(const std::string &path)> OnSaved;
        /// Fired when a script was opened (the host can log it).
        std::function<void(const std::string &path)> OnOpened;
        /// Fired when the editor was closed.
        std::function<void()> OnClosed;

    private:
        void build();
        void layoutControls(int width, int height);
        void refreshTitle();

        Gorgon::Widgets::Panel panel_{Gorgon::Widgets::Registry::Panel_Regular};
        Gorgon::Widgets::Label lblTitle_{""};
        Gorgon::Widgets::Textarea textarea_{""};
        Gorgon::Widgets::Button btnSave_{"Save"};
        Gorgon::Widgets::Button btnCheck_{"Check Syntax"};
        Gorgon::Widgets::Button btnClose_{"Close"};
        Gorgon::Widgets::Label lblStatus_{""};

        bool attached_ = false;
        bool open_ = false;
        std::string path_;
        // Buffer contents as of the last read/write; IsDirty() compares the live buffer to this.
        std::string savedText_;
        std::string status_;
    };

} // namespace Gorgon::Editor
