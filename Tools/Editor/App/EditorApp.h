#pragma once

#include "Core/Scene.h"
#include "AssetBrowserPanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "Viewport.h"

#include <Gorgon/UI/Organizers/Flow.h>
#include <Gorgon/UI/Window.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Checkbox.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Layerbox.h>
#include <Gorgon/Widgets/Panel.h>

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
        HierarchyPanel &GetHierarchy() { return *hierarchy_; }
        InspectorPanel &GetInspector() { return *inspector_; }
        const std::string &GetCurrentFilePath() const { return currentFilePath_; }
        bool IsDirty() const { return dirty_; }
        PlayMode GetPlayMode() const { return playMode_; }

    private:
        void buildToolbar();
        void buildDockPlaceholders();
        void populateSampleScene();
        void markDirty();
        void refreshPanels();
        void updateWindowTitle();
        void updateStatusText(float worldX, float worldY, float zoom);
        void updatePlayButtons();

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

        Gorgon::UI::Window &window_;
        Scene scene_;
        std::string currentFilePath_;
        bool dirty_ = false;
        PlayMode playMode_ = PlayMode::Stopped;

        // Top toolbar
        Gorgon::Widgets::Panel toolbarPanel_{Gorgon::Widgets::Registry::Panel_Top};
        Gorgon::UI::Organizers::Flow toolbarOrganizer_;
        Gorgon::Widgets::Button btnNew_{"New"};
        Gorgon::Widgets::Button btnOpen_{"Open"};
        Gorgon::Widgets::Button btnSave_{"Save"};
        Gorgon::Widgets::Button btnSaveAs_{"Save As"};
        Gorgon::Widgets::Button btnPlay_{"Play"};
        Gorgon::Widgets::Button btnPause_{"Pause"};
        Gorgon::Widgets::Button btnStop_{"Stop"};
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
    };

} // namespace Gorgon::Editor
