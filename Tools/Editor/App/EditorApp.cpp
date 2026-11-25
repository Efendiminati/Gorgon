#include "EditorApp.h"

#include "Core/SceneSerializer.h"

#include <Gorgon/Input/Keyboard.h>
#include <Gorgon/Main.h>
#include <Gorgon/Time.h>
#include <Gorgon/UI/Dialog.h>
#include <Gorgon/UI/Dimension.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <vector>

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

        std::string EnsurePrefabExtension(std::string path) {
            if(path.empty()) return path;
            const std::string ext = ".gprefab.json";
            if(path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0)
                return path;

            const std::string shortExt = ".gprefab";
            if(path.size() >= shortExt.size() &&
               path.compare(path.size() - shortExt.size(), shortExt.size(), shortExt) == 0) {
                return path + ".json";
            }

            return path + ext;
        }
    } // namespace

    EditorApp::EditorApp(Gorgon::UI::Window &window)
        : window_(window), viewport_(scene_) {
        window_.Add(toolbarPanel_);
        window_.Add(hierarchyPanel_);
        window_.Add(inspectorPanel_);
        assetBrowser_.Attach(window_);
        console_.Attach(window_);
        window_.Add(viewportBox_);
        // Added last so the script editor's window sits above the viewport it overlays.
        scriptEditor_.Attach(window_);
        // Shares the bottom dock with the asset browser and the console.
        timeline_.Attach(window_);

        toolbarPanel_.EnableScroll(false, false);
        hierarchyPanel_.EnableScroll(true, false);
        inspectorPanel_.EnableScroll(true, false);

        buildToolbar();
        buildDockPlaceholders();

        viewport_.Attach(viewportBox_);
        viewport_.SetGridSnap(chkGridSnap_.GetState());
        viewport_.OnSelectionChanged = [this](EntityId) {
            if(hierarchy_)
                hierarchy_->SyncSelection();
            if(inspector_)
                inspector_->Rebuild();
            // The tile brush follows the selection: it only arms on a paintable tilemap.
            updateTileToolButton();
            // The timeline always shows the selected entity's animation (and drops the preview when
            // the selection is not animatable).
            timeline_.Sync();
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        };
        viewport_.OnSceneModified = [this]() {
            if(inspector_)
                inspector_->Refresh();
            // A drag reports every mouse move; collapse the whole gesture into one undo step.
            recordEdit(gestureActive_ ? "Move Entity" : "Viewport Edit",
                       gestureActive_ && gesturePushed_);
            if(gestureActive_)
                gesturePushed_ = true;
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        };
        viewport_.OnInteractionChanged = [this](bool active) {
            gestureActive_ = active;
            if(active)
                gesturePushed_ = false;
        };
        // Tile painting reports once per finished stroke, so one drag is exactly one undo step
        // and Ctrl+Z restores the cells as they were before the stroke.
        viewport_.OnTileStrokeCommitted = [this]() {
            recordEdit("Paint Tiles");
            if(inspector_)
                inspector_->Refresh();
            viewport_.Redraw();
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY,
                             viewport_.GetCamera().zoom);
        };
        viewport_.OnPaintTileChanged = [this](int) {
            if(inspector_)
                inspector_->Refresh();
            updateTileToolButton();
        };
        viewport_.OnTilemapToolChanged = [this]() { updateTileToolButton(); };
        viewport_.OnStatusChanged = [this](float wx, float wy, float zoom) {
            updateStatusText(wx, wy, zoom);
        };

        // ---- Animation timeline (bottom dock) --------------------------------
        // The panel edits the scene in place and reports the name of the edit; the app records one
        // undo step per edit exactly like the other panels do.
        timeline_.OnEdited = [this](const std::string &name) {
            viewport_.Redraw();
            if(inspector_)
                inspector_->Refresh();
            recordEdit(name);
        };
        timeline_.OnPreviewChanged = [this]() { viewport_.Redraw(); };
        timeline_.Sync(true);

        // Phase 3 panels: hierarchy tree (left) and details inspector (right).
        hierarchy_ = std::make_unique<HierarchyPanel>(scene_, viewport_, hierarchyPanel_);
        inspector_ = std::make_unique<InspectorPanel>(scene_, viewport_, inspectorPanel_);

        if(hierarchy_)
            hierarchy_->OnSelectionChanged = [this](EntityId) {
                if(inspector_)
                    inspector_->Rebuild();
                updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
            };
        if(hierarchy_)
            hierarchy_->OnSceneModified = [this]() {
                viewport_.Redraw();
                recordEdit("Edit Entity");
            };
        if(hierarchy_)
            hierarchy_->OnStructureChanged = [this]() {
                if(inspector_)
                    inspector_->Rebuild();
                viewport_.Redraw();
                recordEdit("Edit Hierarchy");
            };

        if(inspector_)
            inspector_->OnSceneModified = [this]() {
                viewport_.Redraw();
                recordEdit("Edit Property");
                // Adding/removing a tilemap in the inspector decides whether the brush can arm.
                updateTileToolButton();
                // Inspector edits to an animation (fps, asset) must show up in the timeline strip.
                timeline_.Sync();
                updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
            };
        if(inspector_)
            inspector_->OnStructureChanged = [this]() {
                if(hierarchy_)
                    hierarchy_->Rebuild();
                viewport_.Redraw();
                recordEdit("Edit Component");
                // A component added or removed changes what the timeline can show.
                timeline_.Sync(true);
            };
        if(inspector_)
            inspector_->OnBrowseRequested = [this](bool isScript) {
                // Route the Inspector's "..." buttons through the Asset Browser picker.
                const std::string base = viewport_.GetAssetBaseDir();
                assetBrowser_.BeginPicker(
                    isScript ? AssetType::Scripts : AssetType::Images,
                    [this, isScript, base](const AssetEntry &entry) {
                        if(entry.isDirectory)
                            return;

                        std::string path = assetBrowser_.GetLibrary().MakeSceneRelativePath(
                            entry.fullPath, base);
                        if(path.empty())
                            path = entry.relativePath;

                        if(inspector_)
                            inspector_->SetPickedAsset(path, isScript);
                        assetBrowser_.EndPicker();
                        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
                    });
            };

        if(inspector_)
            inspector_->OnSavePrefabRequested = [this](EntityId id) { onSavePrefabRequested(id); };
        if(inspector_)
            inspector_->OnApplyPrefabRequested = [this](EntityId id) {
                std::string err;
                if(!ApplyPrefabToEntity(id, &err)) {
                    Gorgon::UI::ShowMessage("Apply Prefab", err.empty() ? "Could not apply the prefab." : err);
                }
            };
        if(inspector_)
            inspector_->OnRevertPrefabRequested = [this](EntityId id) {
                std::string err;
                if(!RevertEntityToPrefab(id, &err)) {
                    Gorgon::UI::ShowMessage("Revert Prefab", err.empty() ? "Could not revert to the prefab." : err);
                }
            };
        if(inspector_)
            inspector_->OnEditScriptRequested = [this](const std::string &path) {
                std::string err;
                if(!OpenScriptEditor(path, &err)) {
                    Gorgon::UI::ShowMessage("Edit Script", err.empty() ? "Could not open the script." : err);
                }
            };

        // Keyboard preview for Space+left-drag pan and Shift multi-select
        {
            window_.KeyPreviewEvent.Register([this](Gorgon::Input::Key key, float amount) {
                namespace Keys = Gorgon::Input::Keyboard::Keycodes;

                // Every key the input API knows also feeds the play session: a running script reads
                // IsKeyDown/IsKeyPressed/IsKeyReleased by name, not just the editor's modifiers.
                const std::string name = KeyNameFromCode(key);
                if(!name.empty())
                    playInput_.SetKeyDown(name, amount > 0.0f);

                if(key == Keys::Space) {
                    viewport_.SetSpaceHeld(amount > 0.0f);
                } else if(key == Keys::Shift || key == Keys::RShift) {
                    viewport_.SetShiftHeld(amount > 0.0f);
                }
            });

            window_.ResizedEvent.Register([this]() {
                LayoutPanes();
            });
        }

        populateSampleScene();
        setupAssetBrowser();
        setupScriptEditor();
        if(hierarchy_)
            hierarchy_->Rebuild();
        if(inspector_)
            inspector_->Rebuild();
        LayoutPanes();

        // Phase 5: history baseline (the sample scene is not an unsaved change), shortcuts and
        // the per-frame play tick.
        lastCommittedScene_ = scene_;
        savedScene_ = scene_;
        dirty_ = false;
        commands_.Clear();
        setupShortcuts();
        frameToken_ = Gorgon::BeforeFrameEvent.Register([this]() { onFrame(); });

        updateUndoButtons();
        updatePlayButtons();
        updateTileToolButton();
        updateWindowTitle();
        updateStatusText(0.0f, 0.0f, 1.0f);
        viewport_.Redraw();
    }

    EditorApp::~EditorApp() {
        if(frameToken_) {
            Gorgon::BeforeFrameEvent.Unregister(frameToken_);
            frameToken_ = 0;
        }

        // Detach shortcuts and end play before the panels/scene go away; both hold `this`.
        shortcuts_.Detach();
        play_.Stop(scene_);

        // Tear the Phase 3 panels down before the dock panels they populate.
        hierarchy_.reset();
        inspector_.reset();
    }

    // ---- Phase 5: history ---------------------------------------------------

    void EditorApp::recordEdit(const std::string &name, bool merge) {
        // Play-in-editor owns the scene; editing is disabled until Stop restores the snapshot.
        if(playMode_ != PlayMode::Stopped)
            return;

        // Callbacks can arrive without an actual change (panel refresh, no-op widget event).
        // Those must not create undo steps.
        if(scene_ == lastCommittedScene_) {
            syncDirtyFromScene();
            return;
        }

        commands_.Push(std::unique_ptr<Command>(
            new SceneSnapshotCommand(name, lastCommittedScene_, scene_)), merge);
        lastCommittedScene_ = scene_;
        syncDirtyFromScene();
        updateUndoButtons();
    }

    void EditorApp::Undo() {
        if(playMode_ != PlayMode::Stopped)
            return;

        if(!commands_.Undo(scene_))
            return;

        applyHistoryChange();
    }

    void EditorApp::Redo() {
        if(playMode_ != PlayMode::Stopped)
            return;

        if(!commands_.Redo(scene_))
            return;

        applyHistoryChange();
    }

    void EditorApp::applyHistoryChange() {
        // The restored scene *is* the new committed baseline: undoing must not record an edit.
        lastCommittedScene_ = scene_;

        viewport_.PruneSelection(false);

        viewport_.ClearSpriteCache();
        refreshPanels();
        viewport_.Redraw();
        syncDirtyFromScene();
        updateUndoButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
    }

    void EditorApp::syncDirtyFromScene() {
        const bool dirty = scene_ != savedScene_;
        if(dirty == dirty_)
            return;

        dirty_ = dirty;
        updateWindowTitle();
    }

    void EditorApp::updateUndoButtons() {
        btnUndo_.SetEnabled(commands_.CanUndo() && playMode_ == PlayMode::Stopped);
        btnRedo_.SetEnabled(commands_.CanRedo() && playMode_ == PlayMode::Stopped);
    }

    void EditorApp::toggleTileTool() {
        // The tile tool edits the scene, which the play session owns while it runs.
        if(playMode_ != PlayMode::Stopped)
            return;

        if(!viewport_.SetTilemapToolActive(!viewport_.IsTilemapToolActive())) {
            LogMessage(LogSeverity::Info,
                       "Select an entity with a tilemap component to paint tiles");
        }

        updateTileToolButton();
        viewport_.Redraw();
    }

    void EditorApp::updateTileToolButton() {
        const bool armed = viewport_.IsTilemapToolActive();
        btnTiles_.SetEnabled(playMode_ == PlayMode::Stopped &&
                             (armed || viewport_.CanPaintTilemap()));
        btnTiles_.Text = armed ? ("Tiles: " + TilemapToolToString(viewport_.GetTilemapTool()))
                               : std::string("Tiles (T)");
    }

    bool EditorApp::confirmDiscardChanges(const std::string &action, std::function<void()> proceed) {
        if(!dirty_) {
            proceed();
            return true;
        }

        // Non-modal confirmation: the action runs from the callback when the user accepts.
        Gorgon::UI::Confirm(
            action,
            "The scene has unsaved changes. Discard them and continue?",
            [proceed]() {
                if(proceed)
                    proceed();
            });

        return false;
    }

    void EditorApp::setupShortcuts() {
        namespace Keys = Gorgon::Input::Keyboard::Keycodes;
        using M = Gorgon::Input::Keyboard::Modifier;

        shortcuts_.AttachTo(window_);

        shortcuts_.Set(Keys::Z, M::Ctrl, [this]() { Undo(); });
        shortcuts_.Set(Keys::Y, M::Ctrl, [this]() { Redo(); });
        shortcuts_.Set(Keys::Z, M::ShiftCtrl, [this]() { Redo(); });
        shortcuts_.Set(Keys::N, M::Ctrl, [this]() { NewSceneInteractive(); });
        shortcuts_.Set(Keys::O, M::Ctrl, [this]() { OpenSceneInteractive(); });
        // Ctrl+S saves whatever the user is looking at: the open script when the script editor is
        // up, the scene otherwise.
        shortcuts_.Set(Keys::S, M::Ctrl, [this]() {
            if(scriptEditor_.IsOpen()) {
                scriptEditor_.SaveInteractive();
                return;
            }

            onSaveClicked();
        });
        shortcuts_.Set(Keys::S, M::ShiftCtrl, [this]() { onSaveAsClicked(); });

        // Entity shortcuts are meaningless while a widget is capturing keystrokes: typing a name
        // in the inspector must not delete the entity or move the camera.
        shortcuts_.Set(Keys::C, M::Ctrl, [this]() {
            if(window_.HasFocusedWidget())
                return;
            CopySelected();
        });
        shortcuts_.Set(Keys::V, M::Ctrl, [this]() {
            if(window_.HasFocusedWidget())
                return;
            PasteClipboard();
        });
        shortcuts_.Set(Keys::D, M::Ctrl, [this]() {
            if(window_.HasFocusedWidget())
                return;
            DuplicateSelected();
        });
        shortcuts_.Set(Keys::Delete, [this]() {
            if(window_.HasFocusedWidget())
                return;
            DeleteSelected();
        });
        shortcuts_.Set(Keys::F, [this]() {
            if(window_.HasFocusedWidget())
                return;
            FocusSelectedEntity();
        });
        // T arms the tile brush for the selected tilemap; pressing it again leaves paint mode.
        shortcuts_.Set(Keys::T, [this]() {
            if(window_.HasFocusedWidget())
                return;
            toggleTileTool();
        });
    }

    void EditorApp::markDirty() {
        syncDirtyFromScene();
    }

    void EditorApp::refreshPanels() {
        if(hierarchy_)
            hierarchy_->Rebuild();
        if(inspector_)
            inspector_->Rebuild();
        // Undo/redo replaced the scene wholesale: re-resolve the timeline's entity and frames.
        timeline_.Sync();
    }

    void EditorApp::buildToolbar() {
        using Gorgon::UI::Units;
        toolbarPanel_.AttachOrganizer(toolbarOrganizer_);

        btnNew_.SetWidth(Units(2.5));
        btnOpen_.SetWidth(Units(2.5));
        btnSave_.SetWidth(Units(2.5));
        btnSaveAs_.SetWidth(Units(3.2));
        btnExport_.SetWidth(Units(2.8));
        btnPlay_.SetWidth(Units(2.5));
        btnPause_.SetWidth(Units(2.7));
        btnStop_.SetWidth(Units(2.5));
        btnUndo_.SetWidth(Units(2.5));
        btnRedo_.SetWidth(Units(2.5));
        btnResetView_.SetWidth(Units(3.8));
        btnTiles_.SetWidth(Units(4.4));
        chkGridSnap_.SetWidth(Units(4.0));
        btnAssetsTab_.SetWidth(Units(2.7));
        btnConsoleTab_.SetWidth(Units(3.0));
        lblStatus_.SetWidth(Units(14.0));

        btnNew_.ClickEvent.Register(*this, &EditorApp::onNewClicked);
        btnOpen_.ClickEvent.Register(*this, &EditorApp::onOpenClicked);
        btnSave_.ClickEvent.Register(*this, &EditorApp::onSaveClicked);
        btnSaveAs_.ClickEvent.Register(*this, &EditorApp::onSaveAsClicked);
        btnExport_.ClickEvent.Register([this]() { onExportClicked(); });
        btnPlay_.ClickEvent.Register(*this, &EditorApp::onPlayClicked);
        btnPause_.ClickEvent.Register(*this, &EditorApp::onPauseClicked);
        btnStop_.ClickEvent.Register(*this, &EditorApp::onStopClicked);
        btnUndo_.ClickEvent.Register([this]() { Undo(); });
        btnRedo_.ClickEvent.Register([this]() { Redo(); });
        btnResetView_.ClickEvent.Register([this]() {
            viewport_.ResetCamera();
        });
        btnTiles_.ClickEvent.Register([this]() { toggleTileTool(); });
        chkGridSnap_.ChangedEvent.Register([this]() {
            viewport_.SetGridSnap(chkGridSnap_.GetState());
        });
        btnAssetsTab_.ClickEvent.Register([this]() { ShowBottomTab(BottomTab::Assets); });
        btnConsoleTab_.ClickEvent.Register([this]() { ShowBottomTab(BottomTab::Console); });
        btnTimelineTab_.ClickEvent.Register([this]() { ShowBottomTab(BottomTab::Timeline); });

        toolbarOrganizer_
            << btnNew_ << btnOpen_ << btnSave_ << btnSaveAs_ << btnExport_
            << btnPlay_ << btnPause_ << btnStop_
            << btnUndo_ << btnRedo_
            << btnResetView_ << btnTiles_ << chkGridSnap_
            // Bottom dock tabs: asset browser, script console and animation timeline share one row.
            << btnAssetsTab_ << btnConsoleTab_ << btnTimelineTab_ << lblStatus_;
    }

    void EditorApp::buildDockPlaceholders() {
        using Gorgon::UI::Pixels;

        // Hierarchy (left) and Inspector (right) are owned by their Phase 3 panels.
        // Only the asset browser still shows a Phase 4 placeholder.
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

        // Bottom dock: the asset browser, the console and the timeline share the row; only the
        // active tab is laid out inside it. The inactive ones are parked far outside the window
        // instead of being hidden, because a hidden Layerbox keeps its layers attached and would
        // still swallow the wheel over the visible panel.
        const int bottomTop = middleY + middleH;
        const int bottomHeight = std::max(0, sz.Height - bottomTop);
        constexpr int kParkedX = -16384;
        const int assetsX = bottomTab_ == BottomTab::Assets ? 0 : kParkedX;
        const int consoleX = bottomTab_ == BottomTab::Console ? 0 : kParkedX;
        const int timelineX = bottomTab_ == BottomTab::Timeline ? 0 : kParkedX;

        assetBrowser_.Layout(assetsX, bottomTop, sz.Width, bottomHeight);
        console_.Layout(consoleX, bottomTop, sz.Width, bottomHeight);
        timeline_.Layout(timelineX, bottomTop, sz.Width, bottomHeight);

        if(scriptEditor_.IsOpen())
            scriptEditor_.Layout(leftW, middleY, centerW, middleH);

        if(hierarchy_)
            hierarchy_->Layout();
        if(inspector_)
            inspector_->Layout();

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
        viewport_.ClearSpriteCache();
        if(populateSample) {
            populateSampleScene();
        } else {
            scene_.Clear();
            scene_.name = "Untitled";
            viewport_.SetSelectedEntity(InvalidEntityId, false);
        }
        viewport_.ResetCamera();
        refreshPanels();

        // A new scene starts with empty history and a clean saved baseline.
        commands_.Clear();
        lastCommittedScene_ = scene_;
        savedScene_ = scene_;
        dirty_ = false;
        gestureActive_ = false;
        gesturePushed_ = false;

        updateUndoButtons();
        updateWindowTitle();
        updateStatusText(0.0f, 0.0f, viewport_.GetCamera().zoom);
    }

    void EditorApp::NewSceneInteractive() {
        confirmDiscardChanges("New Scene", [this]() { NewScene(false); });
    }

    bool EditorApp::OpenSceneFile(const std::string &path, std::string *outError) {
        Scene loaded;
        if(!SceneSerializer::LoadFromFile(path, loaded, outError)) {
            return false;
        }
        scene_ = std::move(loaded);
        currentFilePath_ = path;

        std::filesystem::path p(path);
        std::error_code ec;
        viewport_.SetAssetBaseDir(p.has_parent_path() ? p.parent_path().string() : std::string{});
        if(p.has_parent_path() && std::filesystem::is_directory(p.parent_path(), ec)) {
            const std::string dir = p.parent_path().generic_string();
            assetBrowser_.SetRoot(dir);
            fileWatcher_.SetRoot(dir);
        }
        viewport_.SetSelectedEntity(InvalidEntityId, false);
        viewport_.ResetCamera();

        commands_.Clear();
        lastCommittedScene_ = scene_;
        savedScene_ = scene_;
        dirty_ = false;
        gestureActive_ = false;
        gesturePushed_ = false;

        refreshPanels();
        updateUndoButtons();
        updateWindowTitle();
        updateStatusText(0.0f, 0.0f, viewport_.GetCamera().zoom);
        return true;
    }

    void EditorApp::OpenSceneInteractive() {
        confirmDiscardChanges("Open Scene", [this]() { onOpenClicked(); });
    }

    bool EditorApp::SaveSceneFile(const std::string &path, std::string *outError) {
        const std::string finalPath = EnsureSceneExtension(path);
        if(!SceneSerializer::SaveToFile(scene_, finalPath, outError)) {
            return false;
        }
        currentFilePath_ = finalPath;
        // Undo history survives a save; only the dirty baseline moves.
        savedScene_ = scene_;
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
        const std::size_t selCount = viewport_.GetSelectionCount();
        if(selCount > 1) {
            oss << "  Sel: " << selCount << " entities";
        } else if(const Entity *e = scene_.FindEntity(viewport_.GetSelectedEntity())) {
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
        // Painting edits the scene, so the tile tool is only offered outside play.
        updateTileToolButton();
    }

    void EditorApp::onNewClicked() {
        NewSceneInteractive();
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
        std::string err;
        if(!StartPlay(&err))
            showPlayError(err);
        else
            viewport_.Redraw();
    }

    void EditorApp::onPauseClicked() {
        PausePlay();
    }

    void EditorApp::onStopClicked() {
        StopPlay();
    }

    // ---- Phase 5: play-in-editor -------------------------------------------

    void EditorApp::onFrame() {
        const float dt = float(Gorgon::Time::DeltaTime()) / 1000.0f;
        if(playMode_ == PlayMode::Playing)
            TickPlay(dt);

        // The timeline's own playhead runs at wall-clock speed and previews into the viewport
        // without touching the scene.
        timeline_.Tick(dt);

        PollHotReload(static_cast<double>(dt));

        // Script Log()/Print() output, compile failures and runtime errors arrive from the play tick
        // and from panel actions; one version check per frame keeps the console current without
        // rebuilding it when nothing was written.
        syncConsole();
    }

    void EditorApp::TickPlay(float dt) {
        if(playMode_ != PlayMode::Playing)
            return;

        syncPlayInput();
        play_.Tick(scene_, dt);
        // The session consumed this frame's edges. Clearing the editor's copy keeps a press from
        // being delivered again on the next frame.
        playInput_.ClearEdges();

        // A script that moved the camera during play moves the viewport with it; the editor only
        // copies when a script asked for a change so a manual pan is never fought over.
        if(play_.ConsumeCameraChange()) {
            auto &camera = viewport_.GetCamera();
            const auto &playCamera = play_.GetCamera();
            camera.panX = playCamera.panX;
            camera.panY = playCamera.panY;
            camera.zoom = playCamera.zoom;
        }

        viewport_.Redraw();
    }

    void EditorApp::syncPlayInput() {
        using Btn = Gorgon::Input::Mouse::Button;

        // The viewport records the pointer even while editing is disabled, which is exactly what
        // play-in-editor needs.
        const auto world = viewport_.GetMouseWorldPosition();
        playInput_.SetMousePosition(world.X, world.Y);
        playInput_.SetMouseButtonDown("Left", viewport_.IsMouseButtonDown(Btn::Left));
        playInput_.SetMouseButtonDown("Right", viewport_.IsMouseButtonDown(Btn::Right));
        playInput_.SetMouseButtonDown("Middle", viewport_.IsMouseButtonDown(Btn::Middle));

        play_.GetInput() = playInput_;
    }

    bool EditorApp::StartPlay(std::string *outError) {
        if(playMode_ == PlayMode::Playing)
            return true;

        if(playMode_ == PlayMode::Paused) {
            // Play resumes a paused session.
            playMode_ = PlayMode::Playing;
            updatePlayButtons();
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
            return true;
        }

        std::string error;
        if(!play_.Start(scene_, viewport_.GetAssetBaseDir(), &error)) {
            if(outError) *outError = error;
            syncConsole();
            return false;
        }
        dirtyScripts_.clear();

        // Keys pressed while editing must not read as freshly pressed on the first play frame.
        playInput_.ClearEdges();
        play_.GetInput() = playInput_;

        playMode_ = PlayMode::Playing;
        gestureActive_ = false;
        gesturePushed_ = false;
        // Edit-only gizmos, click-select and drag are off while the scripts own the scene.
        viewport_.ClearDropPreview();
        viewport_.SetEditEnabled(false);
        // The viewport reads live particle state from the running session.
        viewport_.SetPlaySession(&play_);
        // Play owns the scene: the timeline's own playhead and preview step aside.
        timeline_.SetScenePlaying(true);
        viewport_.Redraw();
        updatePlayButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        // Compile failures and Start hook output happened just now: show them without waiting a frame.
        syncConsole();
        return true;
    }

    void EditorApp::PausePlay() {
        if(playMode_ == PlayMode::Playing) {
            playMode_ = PlayMode::Paused;
        } else if(playMode_ == PlayMode::Paused) {
            playMode_ = PlayMode::Playing;
        } else {
            return;
        }

        updatePlayButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
    }

    void EditorApp::StopPlay() {
        if(playMode_ == PlayMode::Stopped)
            return;

        // Tears the VMs down and puts the exact pre-play scene back.
        play_.Stop(scene_);
        viewport_.SetPlaySession(nullptr);

        playMode_ = PlayMode::Stopped;
        viewport_.SetEditEnabled(true);
        // Play input belongs to the session; nothing queued during it may fire on the next one.
        playInput_.ClearEdges();
        // Hand the viewport back to the editor: the timeline previews the frame it shows again.
        timeline_.SetScenePlaying(false);

        // The restored snapshot is the committed state; playing never dirties the scene.
        lastCommittedScene_ = scene_;
        gestureActive_ = false;
        gesturePushed_ = false;

        refreshPanels();
        viewport_.Redraw();
        syncDirtyFromScene();
        updatePlayButtons();
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
    }

    void EditorApp::showPlayError(const std::string &message) {
        Gorgon::UI::ShowMessage("Play", message.empty() ? "Play could not start." : message);
    }

    // ---- Entity actions -----------------------------------------------------

    void EditorApp::FocusSelectedEntity() {
        const EntityId id = viewport_.GetSelectedEntity();
        if(!scene_.HasEntity(id))
            return;

        const Transform world = scene_.GetWorldTransform(id);
        auto &cam = viewport_.GetCamera();
        cam.panX = world.x;
        cam.panY = world.y;
        viewport_.Redraw();
        updateStatusText(cam.panX, cam.panY, cam.zoom);
    }

    void EditorApp::DuplicateSelected() {
        if(playMode_ != PlayMode::Stopped)
            return;

        if(hierarchy_)
            hierarchy_->DuplicateSelected();
    }

    void EditorApp::DeleteSelected() {
        if(playMode_ != PlayMode::Stopped)
            return;

        if(hierarchy_)
            hierarchy_->DeleteSelected();
    }

    bool EditorApp::CopySelected() {
        if(playMode_ != PlayMode::Stopped)
            return false;

        const auto &selected = viewport_.GetSelectedEntities();
        if(selected.empty())
            return false;

        const auto payload = SceneSerializer::EntitiesToJson(scene_, selected);
        if(!payload.Has("entities") || !payload["entities"].IsArray() || payload["entities"].GetCount() == 0)
            return false;

        clipboardJson_ = Gorgon::Encoding::Json.Encode(payload, 2);
        pasteCount_ = 0;
        return true;
    }

    bool EditorApp::PasteClipboard() {
        if(playMode_ != PlayMode::Stopped)
            return false;
        if(clipboardJson_.empty())
            return false;

        const float step = static_cast<float>(scene_.settings.gridSize > 0 ? scene_.settings.gridSize : 16);
        const float offset = step * static_cast<float>(pasteCount_ + 1);

        std::vector<EntityId> pastedRoots;
        std::string err;
        if(!SceneSerializer::PasteEntitiesFromJsonString(
                clipboardJson_, scene_, &pastedRoots, offset, offset, &err) ||
           pastedRoots.empty()) {
            return false;
        }

        ++pasteCount_;
        viewport_.SetSelectedEntities(pastedRoots, false);
        refreshPanels();
        viewport_.Redraw();
        recordEdit(pastedRoots.size() > 1 ? "Paste Entities" : "Paste Entity");
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        return true;
    }

    // ---- Prefabs ------------------------------------------------------------

    std::string EditorApp::GetPrefabDirectory() const {
        if(!currentFilePath_.empty()) {
            const std::filesystem::path scenePath(currentFilePath_);
            if(scenePath.has_parent_path())
                return scenePath.parent_path().generic_string();
        }
        const std::string root = assetBrowser_.GetRoot();
        if(!root.empty())
            return root;
        std::error_code ec;
        return std::filesystem::current_path(ec).generic_string();
    }

    bool EditorApp::resolvePrefabPath(const std::string &prefabSource, const std::string &baseDir,
                                      std::string &outPath) const {
        if(prefabSource.empty())
            return false;

        std::filesystem::path p(prefabSource);
        if(p.is_absolute()) {
            outPath = p.generic_string();
            return std::filesystem::exists(outPath);
        }

        // Relative prefab paths resolve against the scene folder first, then the browser root.
        std::vector<std::string> bases;
        if(!baseDir.empty()) bases.push_back(baseDir);
        if(!assetBrowser_.GetRoot().empty()) bases.push_back(assetBrowser_.GetRoot());

        for(const auto &base : bases) {
            const auto candidate = (std::filesystem::path(base) / p).lexically_normal();
            if(std::filesystem::exists(candidate)) {
                outPath = candidate.generic_string();
                return true;
            }
        }

        if(!bases.empty()) {
            outPath = (std::filesystem::path(bases.front()) / p).lexically_normal().generic_string();
        } else {
            outPath = p.generic_string();
        }
        return false;
    }

    bool EditorApp::SaveEntityAsPrefab(EntityId entityId, const std::string &path, std::string *outError) {
        if(playMode_ != PlayMode::Stopped) {
            if(outError) *outError = "Stop play mode before saving a prefab.";
            return false;
        }
        if(!scene_.HasEntity(entityId)) {
            if(outError) *outError = "Select an entity to save as a prefab.";
            return false;
        }
        if(path.empty()) {
            if(outError) *outError = "A prefab file path is required.";
            return false;
        }

        const std::string finalPath = EnsurePrefabExtension(path);
        const std::filesystem::path parent = std::filesystem::path(finalPath).parent_path();
        if(!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        if(!SceneSerializer::SavePrefabToFile(scene_, entityId, finalPath, outError))
            return false;

        // Remember the link on the source entity so Apply/Revert work without retyping the path.
        if(Entity *entity = scene_.FindEntity(entityId)) {
            const std::string stored = assetBrowser_.GetLibrary().MakeSceneRelativePath(
                finalPath, viewport_.GetAssetBaseDir());
            const std::string link = stored.empty() ? finalPath : stored;
            if(entity->prefabSource != link) {
                entity->prefabSource = link;
                recordEdit("Link Prefab");
                if(inspector_)
                    inspector_->Refresh();
            }
        }

        assetBrowser_.Refresh();
        return true;
    }

    void EditorApp::onSavePrefabRequested(EntityId entityId) {
        const Entity *entity = scene_.FindEntity(entityId);
        if(!entity)
            return;

        std::string baseName = entity->name.empty() ? std::string("Prefab") : entity->name;
        for(char &c : baseName) {
            if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }
        std::string dir = GetPrefabDirectory();
        std::string defaultPath = dir.empty() ? (baseName + ".gprefab.json")
                                             : (dir + "/" + baseName + ".gprefab.json");

        Gorgon::UI::Input<std::string>(
            "Save as Prefab",
            "Write this entity and its children to a .gprefab.json file:",
            "Path",
            [this, entityId](const std::string &path) {
                std::string err;
                if(!SaveEntityAsPrefab(entityId, path, &err)) {
                    Gorgon::UI::ShowMessage("Save as Prefab", err.empty() ? "Could not write the prefab." : err);
                }
            },
            defaultPath,
            [](const std::string &s) { return !s.empty(); },
            Gorgon::UI::CloseOption::Cancel
        );
    }

    bool EditorApp::InstantiatePrefabFile(const std::string &path, float worldX, float worldY,
                                          EntityId *outRootId, std::string *outError) {
        if(playMode_ != PlayMode::Stopped) {
            if(outError) *outError = "Stop play mode before editing the scene.";
            return false;
        }
        if(path.empty()) {
            if(outError) *outError = "No prefab file given.";
            return false;
        }

        const std::string link = assetBrowser_.GetLibrary().MakeSceneRelativePath(
            path, viewport_.GetAssetBaseDir());

        std::vector<EntityId> created;
        EntityId rootId = InvalidEntityId;
        if(!SceneSerializer::InstantiatePrefabFromFile(
               path, scene_, worldX, worldY, link.empty() ? path : link, &rootId, &created, outError)) {
            return false;
        }

        viewport_.SetSelectedEntities(created, false);
        refreshPanels();
        viewport_.Redraw();
        recordEdit("Instantiate Prefab");
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);

        if(outRootId)
            *outRootId = rootId;
        return true;
    }

    bool EditorApp::ApplyPrefabToEntity(EntityId entityId, std::string *outError) {
        if(playMode_ != PlayMode::Stopped) {
            if(outError) *outError = "Stop play mode before editing the scene.";
            return false;
        }

        Entity *entity = scene_.FindEntity(entityId);
        if(!entity) {
            if(outError) *outError = "Selected entity no longer exists.";
            return false;
        }
        if(entity->prefabSource.empty()) {
            if(outError) *outError = "This entity is not linked to a prefab.";
            return false;
        }

        std::string resolved;
        if(!resolvePrefabPath(entity->prefabSource, viewport_.GetAssetBaseDir(), resolved)) {
            if(outError) *outError = "Prefab file not found: " + resolved;
            return false;
        }

        PrefabData prefab;
        if(!SceneSerializer::LoadPrefabFromFile(resolved, prefab, outError))
            return false;

        // Re-apply the prefab's own contents, keeping this instance's identity and placement.
        if(!SceneSerializer::RevertPrefabInstance(scene_, entityId, prefab, outError))
            return false;

        viewport_.ClearSpriteCache();
        refreshPanels();
        viewport_.Redraw();
        recordEdit("Apply Prefab");
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        return true;
    }

    bool EditorApp::RevertEntityToPrefab(EntityId entityId, std::string *outError) {
        // Revert and Apply share one implementation: both restore the instance from the file.
        return ApplyPrefabToEntity(entityId, outError);
    }

    void EditorApp::setupAssetBrowser() {
        assetBrowser_.OnAssetActivated = [this](const AssetEntry &entry) {
            // Double clicking a script opens it for editing; every other asset keeps the
            // "add to the scene" behaviour.
            if(!entry.isDirectory && entry.type == AssetType::Scripts) {
                std::string error;
                if(!OpenScriptEditor(entry.fullPath, &error)) {
                    Gorgon::UI::ShowMessage("Edit Script", error.empty() ? "Could not open the script." : error);
                }
                return;
            }

            std::string error;
            if(!insertAsset(entry, &error) && !error.empty()) {
                Gorgon::UI::ShowMessage("Add to Scene", error);
            }
        };
        assetBrowser_.OnAssetSelected = [this](const AssetEntry &) {
            updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        };
        assetBrowser_.OnAssetDragMove = [this](const AssetEntry &entry, Gorgon::Geometry::Point winPt) {
            if(playMode_ != PlayMode::Stopped) {
                viewport_.ClearDropPreview();
                return;
            }
            const auto vpLoc = viewportBox_.GetLayer().TranslateToTopLevel({0, 0});
            const auto vpSize = viewport_.GetViewportSize();
            const bool inside = (winPt.X >= vpLoc.X && winPt.X < vpLoc.X + vpSize.Width &&
                                 winPt.Y >= vpLoc.Y && winPt.Y < vpLoc.Y + vpSize.Height);
            if(inside) {
                viewport_.SetDropPreview(static_cast<float>(winPt.X - vpLoc.X),
                                         static_cast<float>(winPt.Y - vpLoc.Y),
                                         entry.name,
                                         entry.type == AssetType::Scripts);
            } else {
                viewport_.ClearDropPreview();
            }
        };
        assetBrowser_.OnAssetDragEnd = [this](const AssetEntry &entry, Gorgon::Geometry::Point winPt) {
            viewport_.ClearDropPreview();
            if(playMode_ != PlayMode::Stopped) return;
            const auto vpLoc = viewportBox_.GetLayer().TranslateToTopLevel({0, 0});
            const auto vpSize = viewport_.GetViewportSize();
            const bool inside = (winPt.X >= vpLoc.X && winPt.X < vpLoc.X + vpSize.Width &&
                                 winPt.Y >= vpLoc.Y && winPt.Y < vpLoc.Y + vpSize.Height);
            if(inside) {
                DropAssetAtViewportPoint(entry,
                                         static_cast<float>(winPt.X - vpLoc.X),
                                         static_cast<float>(winPt.Y - vpLoc.Y));
            }
        };

        std::error_code ec;
        std::string root;
        if(std::filesystem::is_directory("Examples", ec)) {
            root = std::filesystem::absolute("Examples", ec).generic_string();
        }
        if(root.empty() || ec) {
            root = std::filesystem::current_path(ec).generic_string();
        }
        if(!root.empty()) {
            assetBrowser_.SetRoot(root);
            fileWatcher_.SetRoot(root);
        }
        if(!assetBrowser_.GetRoot().empty()) {
            // Scenes without a path of their own resolve sprite paths against the browser root,
            // so a freshly inserted asset is visible straight away.
            viewport_.SetAssetBaseDir(assetBrowser_.GetRoot());
        }
    }

    bool EditorApp::insertAsset(const AssetEntry &entry, std::string *outError) {
        if(playMode_ != PlayMode::Stopped) {
            if(outError) *outError = "Stop play mode before editing the scene.";
            return false;
        }

        if(entry.isDirectory) {
            if(outError) *outError = "Folders cannot be added to the scene.";
            return false;
        }
        if(entry.type != AssetType::Images && entry.type != AssetType::Scripts && entry.type != AssetType::Prefabs) {
            if(outError) *outError = "Only image, script and prefab assets can be added to the scene.";
            return false;
        }

        // An unsaved scene resolves sprite paths against the browser root, so keep that in
        // sync when the user browses to a different project folder.
        if(currentFilePath_.empty() && !assetBrowser_.GetRoot().empty() &&
           viewport_.GetAssetBaseDir() != assetBrowser_.GetRoot()) {
            viewport_.SetAssetBaseDir(assetBrowser_.GetRoot());
        }

        const auto size = viewport_.GetViewportSize();
        const float viewW = static_cast<float>(size.Width);
        const float viewH = static_cast<float>(size.Height);
        const auto center = viewport_.GetCamera().ScreenToWorld(viewW * 0.5f, viewH * 0.5f, viewW, viewH);

        float wx = center.X;
        float wy = center.Y;
        if(chkGridSnap_.GetState()) {
            const float grid = static_cast<float>(std::max(1, scene_.settings.gridSize));
            wx = std::round(wx / grid) * grid;
            wy = std::round(wy / grid) * grid;
        }

        const EntityId id = InsertAssetAsEntity(scene_, entry, viewport_.GetAssetBaseDir(), wx, wy);
        if(id == InvalidEntityId) {
            if(outError) *outError = "Could not add this asset to the scene.";
            return false;
        }

        viewport_.SetSelectedEntity(id, true);
        viewport_.Redraw();
        recordEdit("Insert Asset");
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        return true;
    }

    bool EditorApp::InsertSelectedAssetIntoScene(std::string *outError) {
        const AssetEntry *entry = assetBrowser_.GetSelected();
        if(!entry) {
            if(outError) *outError = "No asset is selected in the Asset Browser.";
            return false;
        }

        return insertAsset(*entry, outError);
    }

    bool EditorApp::DropAssetAtViewportPoint(const AssetEntry &entry, float sx, float sy, std::string *outError) {
        if(playMode_ != PlayMode::Stopped) {
            if(outError) *outError = "Stop play mode before editing the scene.";
            return false;
        }
        if(entry.isDirectory) {
            if(outError) *outError = "Folders cannot be dropped into the scene.";
            return false;
        }
        if(entry.type != AssetType::Images && entry.type != AssetType::Scripts && entry.type != AssetType::Prefabs) {
            if(outError) *outError = "Only image, script and prefab assets can be dropped into the scene.";
            return false;
        }

        if(currentFilePath_.empty() && !assetBrowser_.GetRoot().empty() &&
           viewport_.GetAssetBaseDir() != assetBrowser_.GetRoot()) {
            viewport_.SetAssetBaseDir(assetBrowser_.GetRoot());
        }

        const AssetDropResult res = DropAssetOntoViewport(
            scene_, viewport_, entry, viewport_.GetAssetBaseDir(), sx, sy);
        if(!res.succeeded) {
            if(outError) *outError = "Could not drop this asset into the scene.";
            return false;
        }

        viewport_.SetSelectedEntity(res.entityId, false);
        refreshPanels();
        viewport_.Redraw();
        recordEdit(res.attachedScript ? "Attach Script" : "Drop Asset");
        updateStatusText(viewport_.GetCamera().panX, viewport_.GetCamera().panY, viewport_.GetCamera().zoom);
        return true;
    }

    // ---- Standalone export --------------------------------------------------

    std::string EditorApp::GetDefaultExportDirectory() const {
        std::string base = GetPrefabDirectory();
        std::string name = scene_.name.empty() ? std::string("Game") : scene_.name;
        for(char &c : name) {
            if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
               c == '<' || c == '>' || c == '|')
                c = '_';
        }

        if(base.empty())
            return "Export/" + name;
        return base + "/Export/" + name;
    }

    ExportResult EditorApp::ExportGame(const ExportOptions &requested) {
        ExportOptions options = requested;

        if(options.sceneBaseDir.empty())
            options.sceneBaseDir = viewport_.GetAssetBaseDir();
        if(options.title.empty())
            options.title = scene_.name.empty() ? std::string("Gorgon Game") : scene_.name;
        if(options.outputDir.empty())
            options.outputDir = GetDefaultExportDirectory();

        const ExportResult result = GameExporter::Export(scene_, options);

        if(!result.ok) {
            LogMessage(LogSeverity::Error,
                       "Export failed: " +
                           (result.error.empty() ? std::string("unknown error") : result.error),
                       "Export");
            return result;
        }

        LogMessage(LogSeverity::Info,
                   "Exported '" + options.title + "' to " + result.outputDir + " (" +
                       std::to_string(result.copiedAssets.size()) + " assets, " +
                       std::to_string(result.copiedRuntimeDlls.size()) + " runtime DLLs)",
                   "Export");

        if(!result.playerExecutablePath.empty())
            LogMessage(LogSeverity::Info, "Player: " + result.playerExecutablePath, "Export");
        else
            LogMessage(LogSeverity::Warn,
                       "No GorgonPlayer.exe was copied; build the GorgonPlayer target first.",
                       "Export");

        for(const auto &asset : result.missingAssets)
            LogMessage(LogSeverity::Warn, "Missing asset, not exported: " + asset, "Export");
        for(const auto &warning : result.warnings)
            LogMessage(LogSeverity::Warn, warning, "Export");

        return result;
    }

    void EditorApp::onExportClicked() {
        if(playMode_ != PlayMode::Stopped) {
            LogMessage(LogSeverity::Error, "Stop play mode before exporting.", "Export");
            Gorgon::UI::ShowMessage("Export", "Stop play mode before exporting a game.");
            return;
        }

        Gorgon::UI::Input<std::string>(
            "Export Game",
            "Write a standalone game bundle (game.json, scene, assets, GorgonPlayer.exe) to:",
            "Folder",
            [this](const std::string &path) {
                ExportOptions options;
                options.outputDir = path;
                ExportGame(options);

                // The log lines above are the result report; show them where they landed.
                ShowBottomTab(BottomTab::Console);
            },
            GetDefaultExportDirectory(),
            [](const std::string &s) { return !s.empty(); },
            Gorgon::UI::CloseOption::Cancel
        );
    }

    // ---- Console & script editor -------------------------------------------

    void EditorApp::setupScriptEditor() {
        // The console shows the play session's log: the same ring buffer that captures GScript
        // Log()/Print(), compile failures and runtime errors, and that outlives Stop().
        console_.SetSource(&play_.GetLog());
        console_.OnCleared = [this]() {
            LogMessage(LogSeverity::Info, "Console cleared", "Console");
        };

        // "Check Syntax" compiles through a PlaySession that is never started: it builds the same
        // libraries a running script gets, but only compiles, so nothing the script contains runs.
        scriptEditor_.OnCheckSyntax = [this](const std::string &source, const std::string &path,
                                             std::string &error) {
            const bool compiled = compiler_.CheckSyntax(source, path, &error);
            const std::string name = path.empty() ? std::string("<buffer>") : path;
            if(compiled)
                LogMessage(LogSeverity::Info, "Syntax OK: " + name, "Script Editor");
            else
                LogMessage(LogSeverity::Error, "Syntax error in " + name + ": " + error, "Script Editor");

            return compiled;
        };

        scriptEditor_.OnSaved = [this](const std::string &path) {
            LogMessage(LogSeverity::Info, "Saved script " + path, "Script Editor");
            assetBrowser_.Refresh();
        };

        scriptEditor_.OnOpened = [this](const std::string &path) {
            LogMessage(LogSeverity::Info, "Editing script " + path, "Script Editor");
        };

        scriptEditor_.OnClosed = [this]() {
            // Opening the editor disables scene editing (it covers the viewport); hand it back.
            viewport_.SetEditEnabled(playMode_ == PlayMode::Stopped);
            viewport_.Redraw();
        };

        updateBottomTabButtons();
    }

    void EditorApp::ShowBottomTab(BottomTab tab) {
        if(bottomTab_ == tab)
            return;

        bottomTab_ = tab;
        updateBottomTabButtons();
        LayoutPanes();
    }

    void EditorApp::updateBottomTabButtons() {
        btnAssetsTab_.SetText(bottomTab_ == BottomTab::Assets ? "[Assets]" : "Assets");
        btnConsoleTab_.SetText(bottomTab_ == BottomTab::Console ? "[Console]" : "Console");
        btnTimelineTab_.SetText(bottomTab_ == BottomTab::Timeline ? "[Timeline]" : "Timeline");
    }

    bool EditorApp::OpenScriptEditor(const std::string &path, std::string *outError) {
        if(path.empty()) {
            if(outError) *outError = "The entity has no script attached.";
            return false;
        }

        // A scene stores script paths relative to the scene folder (an unsaved scene uses the
        // asset browser root), not relative to the process working directory.
        std::string resolved = path;
        std::error_code ec;
        if(!std::filesystem::path(path).is_absolute() && !std::filesystem::exists(path, ec)) {
            std::vector<std::string> bases;
            const std::string sceneBase = viewport_.GetAssetBaseDir();
            if(!sceneBase.empty()) bases.push_back(sceneBase);
            if(!assetBrowser_.GetRoot().empty()) bases.push_back(assetBrowser_.GetRoot());

            for(const auto &base : bases) {
                const auto candidate = (std::filesystem::path(base) / path).lexically_normal();
                if(std::filesystem::exists(candidate, ec)) {
                    resolved = candidate.generic_string();
                    break;
                }
            }
        }

        if(!scriptEditor_.OpenFile(resolved, outError))
            return false;

        // The editor covers the viewport: a click must not move an entity underneath it.
        viewport_.SetEditEnabled(false);
        viewport_.ClearDropPreview();
        LayoutPanes();
        return true;
    }

    void EditorApp::LogMessage(LogSeverity severity, const std::string &message, const std::string &source) {
        play_.Log(severity, message, source);
        syncConsole();
    }

    std::size_t EditorApp::PollHotReload(double dtSeconds, bool force) {
        const std::string desiredRoot = !assetBrowser_.GetRoot().empty()
            ? assetBrowser_.GetRoot()
            : viewport_.GetAssetBaseDir();
        if(!desiredRoot.empty() && !FileWatcher::PathsEqual(desiredRoot, fileWatcher_.GetRoot())) {
            fileWatcher_.SetRoot(desiredRoot);
        }

        const std::vector<FileChange> changes = force
            ? fileWatcher_.Poll()
            : fileWatcher_.Tick(dtSeconds);
        if(changes.empty())
            return 0;

        handleDiskChanges(changes);
        return changes.size();
    }

    void EditorApp::handleDiskChanges(const std::vector<FileChange> &changes) {
        bool needBrowserRefresh = false;
        bool needViewportRedraw = false;

        for(const auto &change : changes) {
            const AssetType assetType = GetAssetTypeForExtension(change.extension);
            const std::string displayName = !change.relativePath.empty()
                ? change.relativePath
                : change.path;

            if(change.kind == FileChangeKind::Added || change.kind == FileChangeKind::Removed) {
                needBrowserRefresh = true;
                if(change.kind == FileChangeKind::Removed && assetType == AssetType::Images) {
                    viewport_.InvalidateSprite(change.path);
                    assetBrowser_.InvalidateThumbnail(change.path);
                    needViewportRedraw = true;
                }
                continue;
            }

            // FileChangeKind::Modified
            if(assetType == AssetType::Images) {
                viewport_.InvalidateSprite(change.path);
                assetBrowser_.InvalidateThumbnail(change.path);
                needViewportRedraw = true;
                LogMessage(LogSeverity::Info, "Reloaded image: " + displayName, "HotReload");
            } else if(change.extension == ".gs") {
                dirtyScripts_.insert(change.path);
                if(playMode_ == PlayMode::Playing || playMode_ == PlayMode::Paused) {
                    std::string error;
                    if(play_.ReloadScripts(scene_, change.path, &error)) {
                        dirtyScripts_.erase(change.path);
                        LogMessage(LogSeverity::Info, "Hot-reloaded script: " + displayName, "HotReload");
                    }
                } else {
                    std::ifstream in(change.path, std::ios::binary);
                    if(in) {
                        std::ostringstream ss;
                        ss << in.rdbuf();
                        std::string error;
                        if(compiler_.CheckSyntax(ss.str(), change.path, &error)) {
                            LogMessage(LogSeverity::Info, "Reloaded script: " + displayName, "HotReload");
                        } else {
                            LogMessage(LogSeverity::Error,
                                       "Script error in " + displayName + ": " + error,
                                       "HotReload");
                        }
                    }
                }
            }
        }

        if(needBrowserRefresh) {
            assetBrowser_.Refresh();
        }
        if(needViewportRedraw) {
            viewport_.Redraw();
        }
        syncConsole();
    }

    void EditorApp::syncConsole() {
        console_.Sync();
    }

    bool EditorApp::OnClosing() {
        if(!dirty_)
            return true;

        // Non-modal dialogs: refuse this close and close again from the confirmation callback.
        Gorgon::UI::Confirm(
            "Unsaved Changes",
            "The scene has unsaved changes. Close without saving?",
            [this]() { window_.Close(); });

        return false;
    }

} // namespace Gorgon::Editor
