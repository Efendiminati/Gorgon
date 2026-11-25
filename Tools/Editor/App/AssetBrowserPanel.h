#pragma once

#include "Core/Scene.h"

#include <Gorgon/Geometry/Point.h>
#include <Gorgon/Geometry/Rectangle.h>
#include <Gorgon/Geometry/Size.h>
#include <Gorgon/Graphics/Bitmap.h>
#include <Gorgon/Graphics/Layer.h>
#include <Gorgon/Input/Layer.h>
#include <Gorgon/Input/Mouse.h>
#include <Gorgon/UI/Window.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Layerbox.h>
#include <Gorgon/Widgets/Panel.h>
#include <Gorgon/Widgets/Textbox.h>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gorgon :: Editor {

    /// Asset categories used by the browser filter buttons.
    enum class AssetType {
        All = 0,
        Images,
        Audio,
        Fonts,
        Scripts,
        Prefabs,
        Resources
    };

    /// Number of entries in AssetType (All included).
    constexpr int AssetTypeCount = 7;

    /// Human readable name of an asset type ("All", "Images", ...).
    const char *GetAssetTypeName(AssetType type);

    /// Classifies a lowercase extension (no leading dot) into an asset type.
    /// Unknown extensions return AssetType::All.
    AssetType GetAssetTypeForExtension(const std::string &extension);

    /// True if the lowercase extension belongs to one of the supported asset groups.
    bool IsSupportedAssetExtension(const std::string &extension);

    /// A single file or folder listed by the browser.
    struct AssetEntry {
        std::string name;         ///< file or folder name, no directory part
        std::string fullPath;     ///< absolute path, forward slashes
        std::string relativePath; ///< path relative to the library root, forward slashes
        std::string extension;    ///< lowercase extension without dot (files only)
        AssetType type = AssetType::All; ///< classification (files only)
        bool isDirectory = false;
    };

    /// Builds the asset path that should be stored in a scene: relative to baseDir when
    /// possible (so a saved scene stays portable), else relative to the library root,
    /// else absolute. Always returns forward slashes.
    std::string MakeSceneRelativePath(const std::string &fullPath, const std::string &baseDir,
                                      const std::string &fallbackRelative = std::string());

    /// Widget free asset database: directory scanning, type filtering and text search.
    /// Kept independent of the UI so it can be exercised in --selftest without a window.
    class AssetLibrary {
    public:
        /// Points the library at a project root. Returns false (with a message) when the
        /// directory does not exist. Clears the current directory and rescans.
        bool SetRoot(const std::string &path, std::string *error = nullptr);
        const std::string &GetRoot() const { return root_; }

        /// Switches the browsed folder (absolute path or path relative to the root).
        void SetCurrentDirectory(const std::string &path);
        const std::string &GetCurrentDirectory() const { return current_; }

        /// Navigates into a sub directory of the current folder ("" returns to the root).
        bool NavigateTo(const std::string &dir);

        /// Moves one level up. Returns false when already at the root.
        bool NavigateUp();

        bool IsAtRoot() const { return current_ == root_ || current_.empty(); }

        /// Rescans the current directory.
        void Refresh();

        void SetFilter(AssetType type);
        AssetType GetFilter() const { return filter_; }

        void SetSearch(const std::string &text);
        const std::string &GetSearch() const { return search_; }

        /// Every entry of the current directory (folders first, then files, alphabetical).
        const std::vector<AssetEntry> &GetAll() const { return entries_; }

        /// Entries after the type filter and the text search are applied.
        const std::vector<AssetEntry> &GetVisible() const { return visible_; }
        int GetVisibleCount() const { return static_cast<int>(visible_.size()); }

        /// Selection is an index into GetVisible(). Out of range values clear the selection.
        void SetSelectedIndex(int index);
        int GetSelectedIndex() const { return selected_; }
        const AssetEntry *GetSelected() const;

        /// Path as it should be stored in a scene (see MakeSceneRelativePath).
        std::string MakeSceneRelativePath(const std::string &fullPath, const std::string &baseDir) const;

        /// "Examples / Simple Games / 2- Space Shooter" style breadcrumb of the current folder.
        std::string GetBreadcrumb(const std::string &separator = " / ") const;

    private:
        void refilter();

        std::string root_;
        std::string current_;
        std::vector<AssetEntry> entries_;
        std::vector<AssetEntry> visible_;
        AssetType filter_ = AssetType::All;
        std::string search_;
        int selected_ = -1;
    };

    /// Grid geometry of the thumbnail area (pure, unit testable).
    struct AssetGridMetrics {
        int tileWidth = 96;
        int tileHeight = 72;
        int thumbSize = 48;
        int padding = 6;
        int columns = 1;
        int contentHeight = 0;

        int RowCount(int itemCount) const {
            if(itemCount <= 0) return 0;
            return (itemCount + columns - 1) / columns;
        }
    };

    /// Computes tile/tumbnail sizes for a grid area of the given size.
    AssetGridMetrics ComputeAssetGridMetrics(int width, int height, int itemCount);

    /// Bounds of a tile in grid coordinates (scroll offset applied).
    Gorgon::Geometry::Rectangle GetAssetTileBounds(const AssetGridMetrics &metrics, int index, int scrollOffset);

    /// Index of the tile under the given grid coordinate, -1 when empty.
    int GetAssetTileAt(const AssetGridMetrics &metrics, int scrollOffset, int x, int y);

    /// Inserts an image or script asset into the scene as a new entity at the given world
    /// position. Images receive a SpriteComponent, scripts a ScriptComponent, both
    /// referencing the asset path as stored in a scene file. Returns InvalidEntityId when
    /// the asset cannot be inserted (folders, audio, fonts, resources).
    class EditorViewport;

    EntityId InsertAssetAsEntity(Scene &scene, const AssetEntry &entry, const std::string &assetBaseDir,
                                 float worldX, float worldY);

    /// Outcome of dropping an asset onto the viewport at viewport-local screen coordinates.
    struct AssetDropResult {
        bool succeeded = false;
        bool attachedScript = false;
        EntityId entityId = InvalidEntityId;
    };

    /// Drops an image or script asset onto the viewport at viewport-local screen coordinates (sx, sy).
    /// Converts (sx, sy) to world coordinates (respecting viewport grid snap). If the asset is a
    /// script and (sx, sy) hits an unlocked entity, attaches the script to that entity; otherwise
    /// spawns a new entity with the sprite or script at the snapped world coordinates.
    AssetDropResult DropAssetOntoViewport(Scene &scene, EditorViewport &viewport,
                                          const AssetEntry &entry, const std::string &assetBaseDir,
                                          float sx, float sy);

    /// Bottom dock asset browser: folder navigation, type filters, text search and an
    /// image thumbnail grid with double-click insert-to-scene.
    class AssetBrowserPanel {
    public:
        AssetBrowserPanel();

        /// Adds the dock panel and the thumbnail grid to the window.
        void Attach(Gorgon::UI::Window &window);

        /// Places the browser in the given window area and lays out its controls.
        void Layout(int x, int y, int width, int height);

        Gorgon::Widgets::Panel &GetPanel() { return panel_; }
        Gorgon::Widgets::Layerbox &GetGridBox() { return gridBox_; }

        AssetLibrary &GetLibrary() { return library_; }
        const AssetLibrary &GetLibrary() const { return library_; }

        /// Sets the scanned root folder and rescans.
        void SetRoot(const std::string &path);
        const std::string &GetRoot() const { return library_.GetRoot(); }

        /// Rescans the current folder, refreshes the labels and redraws the grid.
        void Refresh();

        /// Redraws the thumbnail grid.
        void Redraw();

        /// Size of the thumbnail grid area in pixels.
        Gorgon::Geometry::Size GetGridSize() const;

        /// Cached downscaled thumbnail of an image asset, nullptr when not an image or unreadable.
        const Gorgon::Graphics::Bitmap *GetThumbnail(const std::string &fullPath);
        /// Number of successfully generated thumbnails in the lazy cache.
        std::size_t GetThumbnailCount() const;
        /// Evicts a single image from the thumbnail cache and redraws if it was present.
        bool InvalidateThumbnail(const std::string &fullPath);
        /// Clears all cached thumbnails and redraws.
        void ClearThumbnails();

        /// Grid hit testing (also usable from tests).
        int HitTestTile(int x, int y) const;
        Gorgon::Geometry::Rectangle GetTileBounds(int index) const;
        int GetScrollOffset() const { return scrollOffset_; }

        void HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        void HandleMouseMove(Gorgon::Geometry::Point pt);
        void HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn);
        bool HandleMouseScroll(Gorgon::Geometry::Point pt, float amount);
        bool IsDraggingAsset() const { return dragging_; }

        /// Selects an entry by visible index (used by tests and the picker).
        void SelectIndex(int index);
        int GetSelectedIndex() const { return library_.GetSelectedIndex(); }
        const AssetEntry *GetSelected() const { return library_.GetSelected(); }

        /// Fires when the selection changes (single click). Phase 5 uses this for the
        /// Inspector sprite/script path picker.
        std::function<void(const AssetEntry &)> OnAssetSelected;

        /// Fires on double click or the "Add to Scene" button.
        std::function<void(const AssetEntry &)> OnAssetActivated;

        /// Fires while dragging a supported asset (image/script); point is in window coordinates.
        std::function<void(const AssetEntry &, Gorgon::Geometry::Point)> OnAssetDragMove;

        /// Fires when releasing a dragged asset; point is in window coordinates.
        std::function<void(const AssetEntry &, Gorgon::Geometry::Point)> OnAssetDragEnd;

        /// Switches the browser into picker mode for the Inspector. While picking,
        /// activation hands the asset back to the callback instead of inserting it.
        void BeginPicker(AssetType filter, std::function<void(const AssetEntry &)> callback);
        void EndPicker();
        bool IsPicking() const { return static_cast<bool>(pickerCallback_); }

    private:
        struct ThumbnailEntry {
            std::unique_ptr<Gorgon::Graphics::Bitmap> bitmap;
            bool attempted = false;
        };

        void buildControls();
        void layoutControls(int width, int height);
        void updateLabels();
        void updateFilterButtons();
        void activateIndex(int index);
        void clampScroll();
        void drawTile(int index, const Gorgon::Geometry::Rectangle &bounds, bool selected);
        void drawEmptyState(int width, int height);
        static int gridTopForHeight(int height);

        AssetLibrary library_;

        Gorgon::Widgets::Panel panel_{Gorgon::Widgets::Registry::Panel_Bottom};
        Gorgon::Widgets::Button btnUp_{"Up"};
        Gorgon::Widgets::Button btnRoot_{"Root..."};
        Gorgon::Widgets::Button btnRefresh_{"Refresh"};
        Gorgon::Widgets::Button btnAddToScene_{"Add to Scene"};
        Gorgon::Widgets::Label lblPath_{""};
        Gorgon::Widgets::Label lblSearch_{"Search:"};
        Gorgon::Widgets::Textbox txtSearch_;
        std::array<std::unique_ptr<Gorgon::Widgets::Button>, AssetTypeCount> filterButtons_;

        Gorgon::Widgets::Layerbox gridBox_{Gorgon::Widgets::Registry::Layerbox_Blank};
        Gorgon::Graphics::Layer gfxLayer_;
        Gorgon::Input::Layer inputLayer_;
        bool attached_ = false;
        bool gridAttached_ = false;

        AssetGridMetrics metrics_;
        int scrollOffset_ = 0;
        int hoverIndex_ = -1;

        // Double click detection
        int lastClickIndex_ = -1;
        std::chrono::steady_clock::time_point lastClickTime_{};

        // Asset drag-and-drop state
        int dragCandidateIndex_ = -1;
        bool dragging_ = false;
        Gorgon::Geometry::Point dragStartPt_{0, 0};
        AssetEntry dragEntry_;
        Gorgon::Geometry::Point toWindowPoint(Gorgon::Geometry::Point gridPt) const;

        std::unordered_map<std::string, ThumbnailEntry> thumbnails_;

        std::function<void(const AssetEntry &)> pickerCallback_;
        AssetType pickerFilter_ = AssetType::All;
    };

} // namespace Gorgon::Editor
