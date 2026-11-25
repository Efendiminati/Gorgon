#pragma once

#include "Core/CommandStack.h"
#include "Core/FileWatcher.h"
#include "Core/GameExporter.h"
#include "Core/PlaySession.h"
#include "Core/Scene.h"
#include "AssetBrowserPanel.h"
#include "ConsolePanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "ScriptEditorPanel.h"
#include "TimelinePanel.h"
#include "Viewport.h"

#include <Gorgon/Input/Shortcuts.h>
#include <Gorgon/UI/Organizers/Flow.h>
#include <Gorgon/UI/Window.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Checkbox.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Layerbox.h>
#include <Gorgon/Widgets/Panel.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace Gorgon :: Editor {

    enum class PlayMode {
        Stopped = 0,
        Playing,
        Paused
    };

    /// Panel that fills the bottom dock. Both live in the same window area; only the active one is
    /// laid out inside it, so switching tabs never needs a re-creation of either panel.
    enum class BottomTab {
        Assets = 0,
        Console,
        Timeline
    };

    class EditorApp {
    public:
        explicit EditorApp(Gorgon::UI::Window &window);
        ~EditorApp();

        bool OnClosing();
        void LayoutPanes();

        // Scene lifecycle (also callable programmatically for tests / Phase 3-5)
        void NewScene(bool populateSample = false);
        bool OpenSceneFile(const std::string &path, std::string *outError = nullptr);
        bool SaveSceneFile(const std::string &path, std::string *outError = nullptr);

        // Accessors for Phase 3 (Hierarchy/Inspector), Phase 4 (Asset Browser), Phase 5 (Play/Undo)
        Scene &GetScene() { return scene_; }
        const Scene &GetScene() const { return scene_; }
        EditorViewport &GetViewport() { return viewport_; }
        Gorgon::Widgets::Panel &GetHierarchyPanel() { return hierarchyPanel_; }
        Gorgon::Widgets::Panel &GetInspectorPanel() { return inspectorPanel_; }
        Gorgon::Widgets::Panel &GetAssetBrowserPanel() { return assetBrowser_.GetPanel(); }
        AssetBrowserPanel &GetAssetBrowser() { return assetBrowser_; }

        // ---- Console & script editor ----------------------------------------
        ConsolePanel &GetConsolePanel() { return console_; }
        ScriptEditorPanel &GetScriptEditor() { return scriptEditor_; }
        // ---- Animation timeline ---------------------------------------------
        TimelinePanel &GetTimelinePanel() { return timeline_; }

        BottomTab GetBottomTab() const { return bottomTab_; }
        /// Switches the bottom dock between the asset browser, the console and the timeline.
        void ShowBottomTab(BottomTab tab);

        /// Opens the built-in .gs editor on `path`. A relative path is resolved against the scene's
        /// asset folder (then the browser root), so a scene-stored script path works unchanged.
        bool OpenScriptEditor(const std::string &path, std::string *outError = nullptr);

        /// Writes one line to the console (the play session's log is the single sink, so it is
        /// visible in the Console tab and survives Play being stopped).
        void LogMessage(LogSeverity severity, const std::string &message,
                        const std::string &source = std::string("Editor"));

        /// Inserts the asset selected in the browser into the scene at the viewport center.
        bool InsertSelectedAssetIntoScene(std::string *outError = nullptr);
        bool DropAssetAtViewportPoint(const AssetEntry &entry, float sx, float sy, std::string *outError = nullptr);

        // ---- Prefabs ---------------------------------------------------------
        /// Writes `entityId` and its subtree to `path` as a .gprefab.json (adds the extension
        /// when missing). Returns false with a message when the entity is missing or unwritable.
        bool SaveEntityAsPrefab(EntityId entityId, const std::string &path, std::string *outError = nullptr);
        /// Instantiates a prefab file into the scene under the viewport center (grid snapped).
        bool InstantiatePrefabFile(const std::string &path, float worldX, float worldY,
                                   EntityId *outRootId = nullptr, std::string *outError = nullptr);
        /// Copies the prefab file's contents onto an existing instance as one undo step.
        bool ApplyPrefabToEntity(EntityId entityId, std::string *outError = nullptr);
        /// Resets an instance to the prefab file on disk as one undo step.
        bool RevertEntityToPrefab(EntityId entityId, std::string *outError = nullptr);

        /// Directory new prefabs are written to (the scene's folder, else the asset browser root).
        std::string GetPrefabDirectory() const;

        // ---- Standalone export (GorgonPlayer) --------------------------------
        /// Default folder the Export dialog offers: `<scene folder>/Export/<scene name>`.
        std::string GetDefaultExportDirectory() const;

        /// Packages the current scene into a runnable folder: `game.json`, the scene, every
        /// referenced asset (prefabs scanned recursively) and `GorgonPlayer.exe` plus its runtime
        /// DLLs. Empty option fields default to the scene folder, the scene name and 1280x720.
        /// Every step is reported to the console.
        ExportResult ExportGame(const ExportOptions &options);
        HierarchyPanel &GetHierarchy() { return *hierarchy_; }
        InspectorPanel &GetInspector() { return *inspector_; }
        const std::string &GetCurrentFilePath() const { return currentFilePath_; }
        bool IsDirty() const { return dirty_; }
        PlayMode GetPlayMode() const { return playMode_; }

        // ---- Phase 5: history, shortcuts, play-in-editor --------------------
        CommandStack &GetCommandStack() { return commands_; }

        /// Undo/redo the last edit. Ignored while playing (the scene belongs to the play VM).
        void Undo();
        void Redo();

        /// Prompts for confirmation when the scene is dirty, then starts a new scene.
        void NewSceneInteractive();
        /// Prompts for confirmation when the scene is dirty, then prompts for a path to open.
        void OpenSceneInteractive();

        /// Play-in-editor. StartPlay snapshots the scene and runs every attached script;
        /// TickPlay advances the scripts; PausePlay toggles between Playing and Paused;
        /// StopPlay tears the VM down and restores the exact pre-play scene.
        bool StartPlay(std::string *outError = nullptr);
        void PausePlay();
        void StopPlay();
        void TickPlay(float dt);
        bool IsPlaying() const { return playMode_ == PlayMode::Playing; }
        /// Number of entity scripts the running play session compiled and drives.
        int GetPlayingScriptCount() const { return play_.GetScriptCount(); }

        /// Keyboard/mouse state handed to the running play scripts (`using Input`). The editor
        /// fills it from the window and viewport every frame; tests can drive it directly and call
        /// TickPlay() to step a script deterministically.
        InputState &GetPlayInput() { return playInput_; }

        /// Camera focus / entity actions, also bound to shortcuts.
        void FocusSelectedEntity();
        void DuplicateSelected();
        void DeleteSelected();
        bool CopySelected();
        bool PasteClipboard();
        bool HasClipboard() const { return !clipboardJson_.empty(); }
        const std::string &GetClipboardJson() const { return clipboardJson_; }

        /// Name of the command Ctrl+Z would revert ("" when history is empty).
        const std::string &GetUndoName() const { return commands_.GetUndoName(); }
        const std::string &GetRedoName() const { return commands_.GetRedoName(); }

        /// Hot-reload: polls the project directory on a ~0.5s interval (or immediately when
        /// force is true), invalidating changed sprites/thumbnails, refreshing the asset
        /// browser on add/remove, and reloading/syntax-checking modified .gs scripts.
        std::size_t PollHotReload(double dtSeconds, bool force = false);
        FileWatcher &GetFileWatcher() { return fileWatcher_; }
        const FileWatcher &GetFileWatcher() const { return fileWatcher_; }
        bool HasDirtyScripts() const { return !dirtyScripts_.empty(); }
        void ClearDirtyScripts() { dirtyScripts_.clear(); }

    private:
        void buildToolbar();
        void buildDockPlaceholders();
        void populateSampleScene();
        void markDirty();
        void refreshPanels();
        void updateWindowTitle();
        void updateStatusText(float worldX, float worldY, float zoom);
        void updatePlayButtons();
        void updateBottomTabButtons();
        /// Console entries appear from frame ticks and from panel actions; push them into the panel.
        void syncConsole();
        /// Points the console at the play session's log and installs the script editor callbacks.
        void setupScriptEditor();
        /// Processes a batch of disk changes reported by fileWatcher_.
        void handleDiskChanges(const std::vector<FileChange> &changes);

        // Phase 5 helpers
        void setupShortcuts();
        void recordEdit(const std::string &name, bool merge = false);
        void applyHistoryChange();
        void syncDirtyFromScene();
        void updateUndoButtons();
        void onFrame();
        /// Toggles the viewport's tile-painting tool (toolbar button and the `T` shortcut path).
        void toggleTileTool();
        /// Keeps the toolbar button's label/enabled state in sync with the viewport tool.
        void updateTileToolButton();
        bool confirmDiscardChanges(const std::string &action, std::function<void()> proceed);
        void showPlayError(const std::string &message);
        /// Copies the editor's input snapshot into the play session before it steps.
        void syncPlayInput();

        // Toolbar actions
        void onNewClicked();
        void onOpenClicked();
        void onSaveClicked();
        void onSaveAsClicked();
        void onPlayClicked();
        void onPauseClicked();
        void onStopClicked();

        // Asset browser helpers
        bool insertAsset(const AssetEntry &entry, std::string *outError);
        void setupAssetBrowser();

        // Prefab helpers
        bool resolvePrefabPath(const std::string &prefabSource, const std::string &baseDir,
                               std::string &outPath) const;
        void onSavePrefabRequested(EntityId entityId);

        // Export helpers
        void onExportClicked();

        Gorgon::UI::Window &window_;
        Scene scene_;
        std::string currentFilePath_;
        bool dirty_ = false;
        PlayMode playMode_ = PlayMode::Stopped;

        // ---- Phase 5 state ---------------------------------------------------
        // Scene state as of the last commit; each edit pushes a before/after pair built from it.
        Scene lastCommittedScene_;
        // Scene state as of the last New/Open/Save, used to derive the "*" dirty marker.
        Scene savedScene_;
        CommandStack commands_;
        PlaySession play_;
        // Keyboard/mouse snapshot the editor feeds to the play session each frame. Held state
        // persists; pressed/released edges are cleared once the session consumed them.
        InputState playInput_;
        // True while a viewport drag is in progress: consecutive "Move" edits collapse into one
        // undo step instead of one step per mouse move.
        bool gestureActive_ = false;
        bool gesturePushed_ = false;
        Gorgon::Event<>::Token frameToken_ = 0;
        std::string clipboardJson_;
        int pasteCount_ = 0;

        // Top toolbar
        // NOTE: the organizer is declared *before* the panel it drives. Gorgon's
        // WidgetContainer keeps a raw pointer to its organizer and touches it while
        // tearing down, so the organizer must outlive the panel (reverse-order destruction).
        Gorgon::UI::Organizers::Flow toolbarOrganizer_;
        Gorgon::Widgets::Panel toolbarPanel_{Gorgon::Widgets::Registry::Panel_Top};
        Gorgon::Widgets::Button btnNew_{"New"};
        Gorgon::Widgets::Button btnOpen_{"Open"};
        Gorgon::Widgets::Button btnSave_{"Save"};
        Gorgon::Widgets::Button btnSaveAs_{"Save As"};
        Gorgon::Widgets::Button btnExport_{"Export"};
        Gorgon::Widgets::Button btnPlay_{"Play"};
        Gorgon::Widgets::Button btnPause_{"Pause"};
        Gorgon::Widgets::Button btnStop_{"Stop"};
        Gorgon::Widgets::Button btnUndo_{"Undo"};
        Gorgon::Widgets::Button btnRedo_{"Redo"};
        Gorgon::Widgets::Button btnResetView_{"Reset View"};
        Gorgon::Widgets::Button btnTiles_{"Tiles (T)"};
        Gorgon::Widgets::Button btnAssetsTab_{"Assets"};
        Gorgon::Widgets::Button btnConsoleTab_{"Console"};
        Gorgon::Widgets::Button btnTimelineTab_{"Timeline"};
        Gorgon::Widgets::Checkbox chkGridSnap_{"Grid Snap", true};
        Gorgon::Widgets::Label lblStatus_{"Ready"};

        // 4-pane docks + center viewport
        Gorgon::Widgets::Panel hierarchyPanel_{Gorgon::Widgets::Registry::Panel_Left};
        Gorgon::Widgets::Panel inspectorPanel_{Gorgon::Widgets::Registry::Panel_Right};
        AssetBrowserPanel assetBrowser_;
        ConsolePanel console_;
        Gorgon::Widgets::Layerbox viewportBox_{Gorgon::Widgets::Registry::Layerbox_Blank};

        // Compile-only session behind the script editor's "Check Syntax". Never started, so it owns
        // no scene and its libraries are built and dropped inside CheckSyntax().
        PlaySession compiler_;
        // Polls the project folder (~0.5s) to hot-reload changed images, assets and .gs scripts.
        FileWatcher fileWatcher_;
        std::unordered_set<std::string> dirtyScripts_;
        // Overlays the viewport while open.
        ScriptEditorPanel scriptEditor_;
        BottomTab bottomTab_ = BottomTab::Assets;

        EditorViewport viewport_;
        // Bottom dock animation timeline. Declared after the viewport it previews through.
        TimelinePanel timeline_{scene_, viewport_};
        std::unique_ptr<HierarchyPanel> hierarchy_;
        std::unique_ptr<InspectorPanel> inspector_;

        // Declared last so it detaches from the window before the rest of the editor dies:
        // the shortcut handlers capture `this`.
        Gorgon::Input::Shortcuts shortcuts_{false};
    };

} // namespace Gorgon::Editor
