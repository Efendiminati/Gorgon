#include "EditorApp.h"

#include "Core/SceneSerializer.h"

#include <Gorgon/Input/Keyboard.h>
#include <Gorgon/UI/Dialog.h>
#include <Gorgon/UI/Dimension.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace Gorgon :: Editor {

    namespace {
        std::string EnsureSceneExtension(std::string path) {
            if(path.empty()) return path;
            const std::string ext = ".gscene.json";
            if(path.size() < ext.size() || path.compare(path.size() - ext.size(), ext.size(), ext) != 0) {
                if(path.find('.') == std::string::npos) {
                    path += ext;
                }
            }
            return path;
        }
    } // namespace

    EditorApp::EditorApp(Gorgon::UI::Window &window)
        : window_(window), viewport_(scene_) {
        window_.Add(toolbarPanel_);
        window_.Add(hierarchyPanel_);
        window_.Add(inspectorPanel_);
        window_.Add(assetBrowserPanel_);
        window_.Add(viewportBox_);

        toolbarPanel_.EnableScroll(false, false);
        hierarchyPanel_.EnableScroll(true, false);
        inspectorPanel_.EnableScroll(true, false);
        assetBrowserPanel_.EnableScroll(true, false);

        buildToolbar();
        buildDockPlaceholders();

        viewport_.Attach(viewportBox_);
        viewport_.SetGridSnap(chkGridSnap_.GetState());
        viewport_.OnSelectionChanged = [this](EntityId) {
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        };
        viewport_.OnSceneModified = [this]() {
            if(!dirty_) {
                dirty_ = true;
                updateWindowTitle();
            }
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        };
        viewport_.OnStatusChanged = [this](float wx, float wy, float zoom) {
            updateStatusText(wx, wy, zoom);
        };

        // Keyboard preview for Space+left-drag pan and R to reset view
        window_.KeyPreviewEvent.Register([this](Gorgon::Input::Key key, float amount) {
            if(key == Gorgon::Input::Keyboard::Keycodes::Space) {
                viewport_.SetSpaceHeld(amount > 0.0f);
            }
        });

        window_.ResizedEvent.Register([this]() {
            LayoutPanes();
        });

        populateSampleScene();
        LayoutPanes();
        updatePlayButtons();
        updateWindowTitle();
        updateStatusText(0.0f, 0.0f, 1.0f);
        viewport_.Redraw();
    }

    void EditorApp::buildToolbar() {
        using Gorgon::UI::Units;
        toolbarPanel_.AttachOrganizer(toolbarOrganizer_);

        btnNew_.SetWidth(Units(2.5));
        btnOpen_.SetWidth(Units(2.5));
        btnSave_.SetWidth(Units(2.5));
        btnSaveAs_.SetWidth(Units(3.2));
        btnPlay_.SetWidth(Units(2.5));
        btnPause_.SetWidth(Units(2.7));
        btnStop_.SetWidth(Units(2.5));
        btnResetView_.SetWidth(Units(3.8));
        chkGridSnap_.SetWidth(Units(4.0));
        lblStatus_.SetWidth(Units(14.0));

        btnNew_.ClickEvent.Register(*this, &EditorApp::onNewClicked);
        btnOpen_.ClickEvent.Register(*this, &EditorApp::onOpenClicked);
        btnSave_.ClickEvent.Register(*this, &EditorApp::onSaveClicked);
        btnSaveAs_.ClickEvent.Register(*this, &EditorApp::onSaveAsClicked);
        btnPlay_.ClickEvent.Register(*this, &EditorApp::onPlayClicked);
        btnPause_.ClickEvent.Register(*this, &EditorApp::onPauseClicked);
        btnStop_.ClickEvent.Register(*this, &EditorApp::onStopClicked);
        btnResetView_.ClickEvent.Register([this]() {
            viewport_.ResetCamera();
        });
        chkGridSnap_.ChangedEvent.Register([this]() {
            viewport_.SetGridSnap(chkGridSnap_.GetState());
        });

        toolbarOrganizer_
            << btnNew_ << btnOpen_ << btnSave_ << btnSaveAs_
            << btnPlay_ << btnPause_ << btnStop_
            << btnResetView_ << chkGridSnap_ << lblStatus_;
    }

    void EditorApp::buildDockPlaceholders() {
        using Gorgon::UI::Pixels;

        lblHierarchyHeader_.Move(Pixels(8, 8));
        lblHierarchyHint_.Move(Pixels(8, 32));
        hierarchyPanel_.Add(lblHierarchyHeader_);
        hierarchyPanel_.Add(lblHierarchyHint_);

        lblInspectorHeader_.Move(Pixels(8, 8));
        lblInspectorHint_.Move(Pixels(8, 32));
        inspectorPanel_.Add(lblInspectorHeader_);
        inspectorPanel_.Add(lblInspectorHint_);

        lblAssetsHeader_.Move(Pixels(8, 8));
        lblAssetsHint_.Move(Pixels(8, 32));
        assetBrowserPanel_.Add(lblAssetsHeader_);
        assetBrowserPanel_.Add(lblAssetsHint_);
    }

    void EditorApp::LayoutPanes() {
        using Gorgon::UI::Pixels;
        const auto sz = window_.GetInteriorSize();
        if(sz.Width <= 0 || sz.Height <= 0) return;

        const int toolbarH = std::min(44, std::max(32, sz.Height / 12));
        const int bottomH  = std::clamp(sz.Height / 5, 110, 210);
        const int leftW    = std::clamp(sz.Width / 6,  170, 260);
        const int rightW   = std::clamp(sz.Width / 5,  200, 300);

        const int middleY = toolbarH;
        const int middleH = std::max(120, sz.Height - toolbarH - bottomH);
        const int centerW = std::max(160, sz.Width - leftW - rightW);

        toolbarPanel_.Move(Pixels(0, 0));
        toolbarPanel_.Resize(Pixels(sz.Width, toolbarH));

        hierarchyPanel_.Move(Pixels(0, middleY));
        hierarchyPanel_.Resize(Pixels(leftW, middleH));

        viewportBox_.Move(Pixels(leftW, middleY));
        viewportBox_.Resize(Pixels(centerW, middleH));
        viewportBox_.Refresh();

        inspectorPanel_.Move(Pixels(leftW + centerW, middleY));
        inspectorPanel_.Resize(Pixels(rightW, middleH));

        assetBrowserPanel_.Move(Pixels(0, middleY + middleH));
        assetBrowserPanel_.Resize(Pixels(sz.Width, sz.Height - (middleY + middleH)));

        viewport_.Redraw();
    }

    void EditorApp::populateSampleScene() {
        scene_.Clear();
        scene_.name = "SampleScene";
        scene_.settings.gridSize = 32;
        scene_.settings.backgroundColor = {24, 26, 33, 255};

        Entity &ground = scene_.CreateEntity("Ground");
        ground.zOrder = 0;
        ground.transform.x = 0.0f;
        ground.transform.y = 128.0f;
        ground.transform.scaleX = 1.0f;
        ground.transform.scaleY = 1.0f;
        ground.sprite = SpriteComponent{"", {72, 112, 92, 255}};
        ground.collider = ColliderComponent{ColliderType::AABB, 384.0f, 48.0f, 24.0f, 0.0f, 0.0f};

        Entity &player = scene_.CreateEntity("Player");
        player.zOrder = 10;
        player.transform.x = -64.0f;
        player.transform.y = 32.0f;
        player.sprite = SpriteComponent{"", {76, 152, 240, 255}};
        player.collider = ColliderComponent{ColliderType::AABB, 64.0f, 64.0f, 32.0f, 0.0f, 0.0f};

        Entity &orb = scene_.CreateEntity("EnergyOrb");
        orb.zOrder = 15;
        orb.transform.x = 96.0f;
        orb.transform.y = 0.0f;
        orb.sprite = SpriteComponent{"", {240, 178, 66, 255}};
        orb.collider = ColliderComponent{ColliderType::Circle, 48.0f, 48.0f, 28.0f, 0.0f, 0.0f};

        viewport_.SetSelectedEntity(player.id, false);
    }

    void EditorApp::NewScene(bool populateSample) {
        currentFilePath_.clear();
        dirty_ = false;
        viewport_.ClearSpriteCache();
        if(populateSample) {
            populateSampleScene();
        } else {
            scene_.Clear();
            scene_.name = "Untitled";
            viewport_.SetSelectedEntity(InvalidEntityId, false);
        }
        viewport_.ResetCamera();
        updateWindowTitle();
        updateStatusText(0.0f, 0.0f, viewport_.GetCamera().zoom);
    }

    bool EditorApp::OpenSceneFile(const std::string &path, std::string *outError) {
        Scene loaded;
        if(!SceneSerializer::LoadFromFile(path, loaded, outError)) {
            return false;
        }
        scene_ = std::move(loaded);
        currentFilePath_ = path;
        dirty_ = false;

        std::filesystem::path p(path);
        viewport_.SetAssetBaseDir(p.has_parent_path() ? p.parent_path().string() : std::string{});
        viewport_.SetSelectedEntity(InvalidEntityId, false);
        viewport_.ResetCamera();
        updateWindowTitle();
        updateStatusText(0.0f, 0.0f, viewport_.GetCamera().zoom);
        return true;
    }

    bool EditorApp::SaveSceneFile(const std::string &path, std::string *outError) {
        const std::string finalPath = EnsureSceneExtension(path);
        if(!SceneSerializer::SaveToFile(scene_, finalPath, outError)) {
            return false;
        }
        currentFilePath_ = finalPath;
        dirty_ = false;
        updateWindowTitle();
        return true;
    }

    void EditorApp::updateWindowTitle() {
        std::string title = "Gorgon 2D Editor - " + scene_.name;
        if(!currentFilePath_.empty()) {
            title += " [" + currentFilePath_ + "]";
        }
        if(dirty_) {
            title += " *";
        }
        window_.SetTitle(title);
    }

    void EditorApp::updateStatusText(float worldX, float worldY, float zoom) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0)
            << "Zoom: " << (zoom * 100.0f) << "%  Pos: (" << worldX << ", " << worldY << ")";
        if(const Entity *e = scene_.FindEntity(viewport_.GetSelectedEntity())) {
            oss << "  Sel: " << e->name << " (#" << e->id << ")";
        }
        if(playMode_ == PlayMode::Playing) {
            oss << "  [PLAYING]";
        } else if(playMode_ == PlayMode::Paused) {
            oss << "  [PAUSED]";
        }
        lblStatus_.Text = oss.str();
    }

    void EditorApp::updatePlayButtons() {
        btnPlay_.SetEnabled(playMode_ != PlayMode::Playing);
        btnPause_.SetEnabled(playMode_ == PlayMode::Playing);
        btnStop_.SetEnabled(playMode_ != PlayMode::Stopped);
    }

    void EditorApp::onNewClicked() {
        if(dirty_) {
            Gorgon::UI::Confirm("New Scene", "Discard unsaved changes and create a new empty scene?", [this]() {
                NewScene(false);
            });
        } else {
            NewScene(false);
        }
    }

    void EditorApp::onOpenClicked() {
        const std::string def = currentFilePath_.empty() ? "scene.gscene.json" : currentFilePath_;
        Gorgon::UI::Input<std::string>(
            "Open Scene",
            "Enter path to a .gscene.json file:",
            "Path",
            [this](const std::string &path) {
                std::string err;
                if(!OpenSceneFile(path, &err)) {
                    Gorgon::UI::ShowMessage("Open Failed", err.empty() ? "Could not load scene file." : err);
                }
            },
            def,
            [](const std::string &s) { return !s.empty(); },
            Gorgon::UI::CloseOption::Cancel
        );
    }

    void EditorApp::onSaveClicked() {
        if(currentFilePath_.empty()) {
            onSaveAsClicked();
            return;
        }
        std::string err;
        if(!SaveSceneFile(currentFilePath_, &err)) {
            Gorgon::UI::ShowMessage("Save Failed", err.empty() ? "Could not save scene file." : err);
        }
    }

    void EditorApp::onSaveAsClicked() {
        const std::string def = currentFilePath_.empty() ? (scene_.name + ".gscene.json") : currentFilePath_;
        Gorgon::UI::Input<std::string>(
            "Save Scene As",
            "Enter destination .gscene.json file path:",
            "Path",
            [this](const std::string &path) {
                std::string err;
                if(!SaveSceneFile(path, &err)) {
                    Gorgon::UI::ShowMessage("Save Failed", err.empty() ? "Could not save scene file." : err);
                }
            },
            def,
            [](const std::string &s) { return !s.empty(); },
            Gorgon::UI::CloseOption::Cancel
        );
    }

    void EditorApp::onPlayClicked() {
        playMode_ = PlayMode::Playing;
        updatePlayButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
    }

    void EditorApp::onPauseClicked() {
        playMode_ = PlayMode::Paused;
        updatePlayButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
    }

    void EditorApp::onStopClicked() {
        playMode_ = PlayMode::Stopped;
        updatePlayButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
    }

    bool EditorApp::OnClosing() {
        return true;
    }

} // namespace Gorgon::Editor
