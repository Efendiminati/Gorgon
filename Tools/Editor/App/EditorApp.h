#pragma once

#include "Core/CommandStack.h"
#include "Core/PlaySession.h"
#include "Core/Scene.h"
#include "AssetBrowserPanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
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

namespace Gorgon :: Editor {

    enum class PlayMode {
        Stopped = 0,
        Playing,
        Paused
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

    private:
        void buildToolbar();
        void buildDockPlaceholders();
        void populateSampleScene();
        void markDirty();
        void refreshPanels();
        void updateWindowTitle();
        void updateStatusText(float worldX, float worldY, float zoom);
        void updatePlayButtons();

        // Phase 5 helpers
        void setupShortcuts();
        void recordEdit(const std::string &name, bool merge = false);
        void applyHistoryChange();
        void syncDirtyFromScene();
        void updateUndoButtons();
        void onFrame();
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
        Gorgon::Widgets::Button btnPlay_{"Play"};
        Gorgon::Widgets::Button btnPause_{"Pause"};
        Gorgon::Widgets::Button btnStop_{"Stop"};
        Gorgon::Widgets::Button btnUndo_{"Undo"};
        Gorgon::Widgets::Button btnRedo_{"Redo"};
        Gorgon::Widgets::Button btnResetView_{"Reset View"};
        Gorgon::Widgets::Checkbox chkGridSnap_{"Grid Snap", true};
        Gorgon::Widgets::Label lblStatus_{"Ready"};

        // 4-pane docks + center viewport
        Gorgon::Widgets::Panel hierarchyPanel_{Gorgon::Widgets::Registry::Panel_Left};
        Gorgon::Widgets::Panel inspectorPanel_{Gorgon::Widgets::Registry::Panel_Right};
        AssetBrowserPanel assetBrowser_;
        Gorgon::Widgets::Layerbox viewportBox_{Gorgon::Widgets::Registry::Layerbox_Blank};

        // The Asset Browser dock itself lives in AssetBrowserPanel (Phase 4).

        EditorViewport viewport_;
        std::unique_ptr<HierarchyPanel> hierarchy_;
        std::unique_ptr<InspectorPanel> inspector_;

        // Declared last so it detaches from the window before the rest of the editor dies:
        // the shortcut handlers capture `this`.
        Gorgon::Input::Shortcuts shortcuts_{false};
    };

} // namespace Gorgon::Editor
