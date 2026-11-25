#include "App/AssetBrowserPanel.h"
#include "Core/FileWatcher.h"
#include "Core/SceneSerializer.h"
#include "Viewport.h"

#include <Gorgon/Filesystem.h>
#include <Gorgon/Filesystem/Iterator.h>
#include <Gorgon/Graphics/Font.h>
#include <Gorgon/String.h>
#include <Gorgon/UI/Dialog.h>
#include <Gorgon/UI/Dimension.h>
#include <Gorgon/Widgets/Registry.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace Gorgon :: Editor {

    namespace {

        constexpr int kControlHeight = 24;
        constexpr int kTileLabelHeight = 20;

        Gorgon::Graphics::RGBAf Col(float r, float g, float b, float a = 1.0f) {
            return Gorgon::Graphics::RGBAf(r, g, b, a);
        }

        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        /// Absolute, forward slash, no trailing separator (except for a drive root).
        std::string NormalizeDir(const std::string &path) {
            if(path.empty()) return {};

            std::filesystem::path p(path);
            std::error_code ec;
            auto abs = std::filesystem::absolute(p, ec);
            if(ec) abs = p;

            std::string result = abs.lexically_normal().generic_string();
            while(result.size() > 3 && !result.empty() && result.back() == '/') {
                result.pop_back();
            }
            return result;
        }

        std::string RelativeToRoot(const std::string &fullPath, const std::string &root) {
            if(root.empty()) return fullPath;
            std::error_code ec;
            auto rel = std::filesystem::path(fullPath).lexically_relative(std::filesystem::path(root));
            if(rel.empty()) return fullPath;
            return rel.generic_string();
        }

        bool StartsWithNoCase(const std::string &haystack, const std::string &needle) {
            return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::string::npos;
        }

        bool EntryLess(const AssetEntry &a, const AssetEntry &b) {
            if(a.isDirectory != b.isDirectory) return a.isDirectory;
            return ToLowerCopy(a.name) < ToLowerCopy(b.name);
        }

        void DrawOutline(Gorgon::Graphics::Layer &layer, float x, float y, float w, float h, float thickness,
                         Gorgon::Graphics::RGBAf color) {
            layer.Draw(x, y, w, thickness, color);
            layer.Draw(x, y + h - thickness, w, thickness, color);
            layer.Draw(x, y, thickness, h, color);
            layer.Draw(x + w - thickness, y, thickness, h, color);
        }

        /// Shortens text with a trailing ellipsis until it fits into maxWidth pixels.
        std::string Ellipsize(const Gorgon::Graphics::StyledPrinter &printer, const std::string &text, int maxWidth) {
            if(maxWidth <= 0) return {};
            if(printer.GetSize(text).Width <= maxWidth) return text;

            const std::string suffix = "...";
            std::string out = text;
            while(!out.empty()) {
                out.pop_back();
                if(printer.GetSize(out + suffix).Width <= maxWidth) {
                    return out + suffix;
                }
            }
            return suffix;
        }

    } // namespace

    // ---- Asset type helpers ----

    const char *GetAssetTypeName(AssetType type) {
        switch(type) {
            case AssetType::Images:    return "Images";
            case AssetType::Audio:     return "Audio";
            case AssetType::Fonts:     return "Fonts";
            case AssetType::Scripts:   return "Scripts";
            case AssetType::Prefabs:   return "Prefabs";
            case AssetType::Resources: return "Resources";
            case AssetType::All:
            default:                   return "All";
        }
    }

    AssetType GetAssetTypeForExtension(const std::string &extension) {
        std::string ext = ToLowerCopy(extension);
        if(!ext.empty() && ext.front() == '.') {
            ext.erase(0, 1);
        }

        if(ext == "png" || ext == "jpg" || ext == "jpeg")               return AssetType::Images;
        if(ext == "wav" || ext == "ogg" || ext == "flac")               return AssetType::Audio;
        if(ext == "ttf" || ext == "otf")                                return AssetType::Fonts;
        if(ext == "gs" || ext == "gscript")                             return AssetType::Scripts;
        if(ext == "gprefab.json" || ext == "gprefab")                 return AssetType::Prefabs;
        if(ext == "gor")                                                return AssetType::Resources;

        return AssetType::All;
    }

    bool IsSupportedAssetExtension(const std::string &extension) {
        return GetAssetTypeForExtension(extension) != AssetType::All;
    }

    std::string MakeSceneRelativePath(const std::string &fullPath, const std::string &baseDir,
                                      const std::string &fallbackRelative) {
        if(fullPath.empty()) return {};

        const std::filesystem::path full(fullPath);
        if(!baseDir.empty()) {
            auto rel = full.lexically_relative(std::filesystem::path(baseDir));
            if(!rel.empty() && rel != std::filesystem::path(".")) {
                return rel.generic_string();
            }
        }

        if(!fallbackRelative.empty()) return fallbackRelative;

        return full.generic_string();
    }

    // ---- AssetLibrary ----

    bool AssetLibrary::SetRoot(const std::string &path, std::string *error) {
        const std::string normalized = NormalizeDir(path);
        if(normalized.empty() || !Gorgon::Filesystem::IsDirectory(normalized)) {
            if(error) *error = "Not a directory: " + (path.empty() ? std::string("(empty path)") : path);
            return false;
        }

        root_ = normalized;
        current_ = normalized;
        selected_ = -1;
        Refresh();
        return true;
    }

    void AssetLibrary::SetCurrentDirectory(const std::string &path) {
        current_ = path.empty() ? root_ : NormalizeDir(path);
        selected_ = -1;
        Refresh();
    }

    bool AssetLibrary::NavigateTo(const std::string &dir) {
        if(root_.empty()) return false;

        std::string target;
        if(dir.empty()) {
            target = root_;
        } else {
            const std::filesystem::path p(dir);
            if(p.is_absolute()) {
                target = NormalizeDir(dir);
            } else {
                target = NormalizeDir(Gorgon::Filesystem::Join(current_, dir));
            }
        }

        if(target.empty() || !Gorgon::Filesystem::IsDirectory(target)) return false;

        current_ = target;
        selected_ = -1;
        Refresh();
        return true;
    }

    bool AssetLibrary::NavigateUp() {
        if(root_.empty() || IsAtRoot()) return false;

        const std::filesystem::path current(current_);
        const std::filesystem::path parent = current.parent_path();
        if(parent.empty()) return false;

        current_ = parent.generic_string();
        selected_ = -1;
        Refresh();
        return true;
    }

    void AssetLibrary::Refresh() {
        entries_.clear();

        if(!root_.empty()) {
            const std::string dir = current_.empty() ? root_ : current_;

            try {
                for(Gorgon::Filesystem::Iterator it(dir); it.IsValid(); it.Next()) {
                    const std::string name = it.Get();
                    if(name.empty() || name == "." || name == "..") continue;

                    AssetEntry entry;
                    entry.name = name;
                    entry.fullPath = Gorgon::Filesystem::Join(dir, name);
                    entry.relativePath = RelativeToRoot(entry.fullPath, root_);

                    if(Gorgon::Filesystem::IsDirectory(entry.fullPath)) {
                        entry.isDirectory = true;
                    } else {
                        const std::string lowerName = ToLowerCopy(name);
                        const std::string prefabSuffix = ".gprefab.json";
                        if(lowerName.size() > prefabSuffix.size() &&
                           lowerName.compare(lowerName.size() - prefabSuffix.size(), prefabSuffix.size(), prefabSuffix) == 0) {
                            entry.extension = "gprefab.json";
                        } else {
                            entry.extension = ToLowerCopy(Gorgon::Filesystem::GetExtension(name));
                        }
                        entry.type = GetAssetTypeForExtension(entry.extension);
                    }

                    entries_.push_back(std::move(entry));
                }
            } catch(const std::exception &) {
                // Unreadable directory: leave the list empty instead of failing the refresh.
            }

            std::stable_sort(entries_.begin(), entries_.end(), EntryLess);
        }

        refilter();
    }

    void AssetLibrary::SetFilter(AssetType type) {
        if(filter_ == type) return;
        filter_ = type;
        selected_ = -1;
        refilter();
    }

    void AssetLibrary::SetSearch(const std::string &text) {
        if(search_ == text) return;
        search_ = text;
        selected_ = -1;
        refilter();
    }

    void AssetLibrary::SetSelectedIndex(int index) {
        if(index < 0 || index >= static_cast<int>(visible_.size())) {
            selected_ = -1;
            return;
        }
        selected_ = index;
    }

    const AssetEntry *AssetLibrary::GetSelected() const {
        if(selected_ < 0 || selected_ >= static_cast<int>(visible_.size())) return nullptr;
        return &visible_[selected_];
    }

    std::string AssetLibrary::MakeSceneRelativePath(const std::string &fullPath, const std::string &baseDir) const {
        std::string fallback = RelativeToRoot(fullPath, root_);
        return Editor::MakeSceneRelativePath(fullPath, baseDir, fallback);
    }

    std::string AssetLibrary::GetBreadcrumb(const std::string &separator) const {
        if(root_.empty()) return "(no root)";
        if(current_.empty() || current_ == root_) {
            return std::filesystem::path(root_).filename().generic_string();
        }

        std::string relative = RelativeToRoot(current_, root_);
        if(relative == current_) return current_;

        std::string crumb = std::filesystem::path(root_).filename().generic_string();
        std::size_t start = 0;
        while(start <= relative.size()) {
            const std::size_t pos = relative.find('/', start);
            const std::string part = relative.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
            if(!part.empty()) {
                crumb += separator;
                crumb += part;
            }
            if(pos == std::string::npos) break;
            start = pos + 1;
        }
        return crumb;
    }

    void AssetLibrary::refilter() {
        visible_.clear();
        for(const auto &entry : entries_) {
            if(entry.isDirectory) {
                visible_.push_back(entry);
                continue;
            }

            if(filter_ == AssetType::All) {
                if(!IsSupportedAssetExtension(entry.extension)) continue;
            } else if(entry.type != filter_) {
                continue;
            }

            if(!search_.empty() && !StartsWithNoCase(entry.name, search_)) continue;

            visible_.push_back(entry);
        }

        if(selected_ >= static_cast<int>(visible_.size())) {
            selected_ = -1;
        }
    }

    // ---- Grid geometry ----

    AssetGridMetrics ComputeAssetGridMetrics(int width, int height, int itemCount) {
        AssetGridMetrics m;
        m.padding = 6;

        const int availableHeight = std::max(32, height - 2 * m.padding);

        // One row of tiles that fills the available height, capped so thumbnails stay sane.
        m.tileHeight = std::clamp(availableHeight, 54, 104);
        m.thumbSize = std::max(20, m.tileHeight - kTileLabelHeight - 8);
        m.tileWidth = std::clamp(m.thumbSize + 28, 66, 140);

        const int usableWidth = std::max(m.tileWidth, width - 2 * m.padding);
        m.columns = std::max(1, (usableWidth + m.padding) / (m.tileWidth + m.padding));

        const int rows = m.RowCount(itemCount);
        m.contentHeight = 2 * m.padding + rows * m.tileHeight + std::max(0, rows - 1) * m.padding;

        return m;
    }

    Gorgon::Geometry::Rectangle GetAssetTileBounds(const AssetGridMetrics &metrics, int index, int scrollOffset) {
        if(index < 0) return {0, 0, 0, 0};

        const int row = index / metrics.columns;
        const int col = index % metrics.columns;

        return {
            metrics.padding + col * (metrics.tileWidth + metrics.padding),
            metrics.padding + row * (metrics.tileHeight + metrics.padding) - scrollOffset,
            metrics.tileWidth,
            metrics.tileHeight
        };
    }

    int GetAssetTileAt(const AssetGridMetrics &metrics, int scrollOffset, int x, int y) {
        if(metrics.columns <= 0) return -1;

        const int strideX = metrics.tileWidth + metrics.padding;
        const int strideY = metrics.tileHeight + metrics.padding;

        const int xRel = x - metrics.padding;
        const int yRel = y + scrollOffset - metrics.padding;
        if(xRel < 0 || yRel < 0) return -1;

        const int col = xRel / strideX;
        const int row = yRel / strideY;
        if(col >= metrics.columns) return -1;
        if(xRel % strideX >= metrics.tileWidth) return -1;
        if(yRel % strideY >= metrics.tileHeight) return -1;

        return row * metrics.columns + col;
    }

    // ---- Scene insertion ----

    EntityId InsertAssetAsEntity(Scene &scene, const AssetEntry &entry, const std::string &assetBaseDir,
                                 float worldX, float worldY) {
        if(entry.isDirectory) return InvalidEntityId;
        if(entry.type != AssetType::Images && entry.type != AssetType::Scripts && entry.type != AssetType::Prefabs) {
            return InvalidEntityId;
        }

        const std::string assetPath = MakeSceneRelativePath(entry.fullPath, assetBaseDir, entry.relativePath);

        if(entry.type == AssetType::Prefabs) {
            EntityId rootId = InvalidEntityId;
            if(!SceneSerializer::InstantiatePrefabFromFile(
                   entry.fullPath, scene, worldX, worldY, assetPath, &rootId)) {
                return InvalidEntityId;
            }
            return rootId;
        }

        int maxZ = 0;
        for(const auto &existing : scene.GetEntities()) {
            maxZ = std::max(maxZ, existing.zOrder);
        }

        std::string entityName = Gorgon::Filesystem::GetBasename(entry.name);
        if(entityName.empty()) entityName = entry.name;

        Entity &entity = scene.CreateEntity(entityName);
        entity.transform.x = worldX;
        entity.transform.y = worldY;
        entity.zOrder = maxZ + 1;

        if(entry.type == AssetType::Images) {
            entity.sprite = SpriteComponent{assetPath, {255, 255, 255, 255}};
        } else {
            entity.script = ScriptComponent{assetPath};
        }

        return entity.id;
    }

    AssetDropResult DropAssetOntoViewport(Scene &scene, EditorViewport &viewport,
                                          const AssetEntry &entry, const std::string &assetBaseDir,
                                          float sx, float sy) {
        AssetDropResult res;
        if(entry.isDirectory) return res;
        if(entry.type != AssetType::Images && entry.type != AssetType::Scripts && entry.type != AssetType::Prefabs) {
            return res;
        }

        if(entry.type == AssetType::Scripts) {
            const EntityId hitId = viewport.HitTestEntity(sx, sy);
            if(hitId != InvalidEntityId) {
                if(Entity *target = scene.FindEntity(hitId); target && !target->locked) {
                    const std::string assetPath = MakeSceneRelativePath(entry.fullPath, assetBaseDir, entry.relativePath);
                    target->script = ScriptComponent{assetPath};
                    res.succeeded = true;
                    res.attachedScript = true;
                    res.entityId = hitId;
                    return res;
                }
            }
        }

        const auto wp = viewport.ScreenToWorldSnapped(sx, sy);
        const EntityId newId = InsertAssetAsEntity(scene, entry, assetBaseDir, wp.X, wp.Y);
        if(newId == InvalidEntityId) return res;

        res.succeeded = true;
        res.attachedScript = false;
        res.entityId = newId;
        return res;
    }

    // ---- AssetBrowserPanel ----

    AssetBrowserPanel::AssetBrowserPanel() {
        buildControls();
    }

    void AssetBrowserPanel::buildControls() {
        using Gorgon::UI::Pixels;

        panel_.EnableScroll(false, false);

        btnUp_.SetWidth(Pixels(46));
        btnRoot_.SetWidth(Pixels(72));
        btnRefresh_.SetWidth(Pixels(82));
        btnAddToScene_.SetWidth(Pixels(114));
        btnUp_.SetHeight(Pixels(kControlHeight));
        btnRoot_.SetHeight(Pixels(kControlHeight));
        btnRefresh_.SetHeight(Pixels(kControlHeight));
        btnAddToScene_.SetHeight(Pixels(kControlHeight));

        btnUp_.ClickEvent.Register([this]() {
            if(library_.NavigateUp()) {
                scrollOffset_ = 0;
                Refresh();
            }
        });

        btnRoot_.ClickEvent.Register([this]() {
            Gorgon::UI::Input<std::string>(
                "Asset Root",
                "Enter the project folder the asset browser should scan:",
                "Path",
                [this](const std::string &path) {
                    std::string error;
                    if(!library_.SetRoot(path, &error)) {
                        Gorgon::UI::ShowMessage("Invalid Folder", error);
                        return;
                    }
                    scrollOffset_ = 0;
                    Refresh();
                },
                library_.GetRoot(),
                [](const std::string &value) { return !value.empty(); },
                Gorgon::UI::CloseOption::Cancel
            );
        });

        btnRefresh_.ClickEvent.Register([this]() {
            library_.Refresh();
            Refresh();
        });

        btnAddToScene_.ClickEvent.Register([this]() {
            activateIndex(library_.GetSelectedIndex());
        });

        txtSearch_.SetWidth(Pixels(180));
        txtSearch_.SetHeight(Pixels(kControlHeight));
        txtSearch_.ChangedEvent.Register([this](const std::string &value) {
            library_.SetSearch(value);
            scrollOffset_ = 0;
            Refresh();
        });

        for(int i = 0; i < AssetTypeCount; ++i) {
            filterButtons_[i] = std::make_unique<Gorgon::Widgets::Button>(GetAssetTypeName(static_cast<AssetType>(i)));
            filterButtons_[i]->SetHeight(Pixels(kControlHeight));
            filterButtons_[i]->ClickEvent.Register([this, i]() {
                library_.SetFilter(static_cast<AssetType>(i));
                scrollOffset_ = 0;
                updateFilterButtons();
                Refresh();
            });
        }

        panel_.Add(btnUp_);
        panel_.Add(btnRoot_);
        panel_.Add(btnRefresh_);
        panel_.Add(btnAddToScene_);
        panel_.Add(lblPath_);
        panel_.Add(lblSearch_);
        panel_.Add(txtSearch_);
        for(auto &button : filterButtons_) {
            panel_.Add(*button);
        }

        updateFilterButtons();
        updateLabels();
    }

    void AssetBrowserPanel::Attach(Gorgon::UI::Window &window) {
        window.Add(panel_);
        window.Add(gridBox_);

        if(!gridAttached_) {
            gfxLayer_.EnableClipping();
            gridBox_.GetLayer().Add(gfxLayer_);
            gridBox_.GetLayer().Add(inputLayer_);

            inputLayer_.SetDown([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
                HandleMouseDown(pt, btn);
            });
            inputLayer_.SetMove([this](Gorgon::Geometry::Point pt) {
                HandleMouseMove(pt);
            });
            inputLayer_.SetUp([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
                HandleMouseUp(pt, btn);
            });
            inputLayer_.SetScroll([this](Gorgon::Geometry::Point pt, float amount) {
                return HandleMouseScroll(pt, amount);
            });

            gridAttached_ = true;
        }

        attached_ = true;
    }

    int AssetBrowserPanel::gridTopForHeight(int height) {
        // Two control rows on top of the thumbnail grid, compact when the dock is small.
        return height < 130 ? 54 : 62;
    }

    void AssetBrowserPanel::Layout(int x, int y, int width, int height) {
        using Gorgon::UI::Pixels;

        if(width <= 0 || height <= 0) return;

        panel_.Move(Pixels(x, y));
        panel_.Resize(Pixels(width, height));

        layoutControls(width, height);
        updateLabels();

        const int gridTop = gridTopForHeight(height);
        const int gridX = x + 6;
        const int gridY = y + gridTop;
        const int gridW = std::max(32, width - 12);
        const int gridH = std::max(24, height - gridTop - 6);

        gridBox_.Move(Pixels(gridX, gridY));
        gridBox_.Resize(Pixels(gridW, gridH));
        gridBox_.Refresh();

        Redraw();
    }

    void AssetBrowserPanel::layoutControls(int width, int height) {
        using Gorgon::UI::Pixels;

        const int compact = height < 130;
        const int row1 = compact ? 4 : 7;
        const int row2 = compact ? 28 : 34;

        int x = 6;
        btnUp_.Move(Pixels(x, row1));
        x += 46 + 6;
        btnRoot_.Move(Pixels(x, row1));
        x += 72 + 6;
        btnRefresh_.Move(Pixels(x, row1));
        x += 82 + 6;
        btnAddToScene_.Move(Pixels(x, row1));
        x += 114 + 10;

        lblPath_.Move(Pixels(x, row1 + 4));
        lblPath_.SetWidth(Pixels(std::max(60, width - x - 8)));

        lblSearch_.Move(Pixels(6, row2 + 4));
        lblSearch_.SetWidth(Pixels(52));
        txtSearch_.Move(Pixels(62, row2));

        int fx = 62 + 180 + 8;
        for(int i = 0; i < AssetTypeCount; ++i) {
            auto &button = *filterButtons_[i];
            const std::string label = GetAssetTypeName(static_cast<AssetType>(i));
            const int buttonWidth = std::max(44, static_cast<int>(label.size()) * 8 + 22);
            button.SetWidth(Pixels(buttonWidth));
            button.Move(Pixels(fx, row2));
            fx += buttonWidth + 5;
        }
    }

    void AssetBrowserPanel::SetRoot(const std::string &path) {
        std::string error;
        if(library_.SetRoot(path, &error)) {
            scrollOffset_ = 0;
            thumbnails_.clear();
            Refresh();
        }
    }

    void AssetBrowserPanel::Refresh() {
        library_.Refresh();
        scrollOffset_ = 0;
        updateLabels();
        updateFilterButtons();
        Redraw();
    }

    void AssetBrowserPanel::SelectIndex(int index) {
        library_.SetSelectedIndex(index);
        if(const AssetEntry *entry = library_.GetSelected()) {
            if(OnAssetSelected) OnAssetSelected(*entry);
        }
        Redraw();
    }

    void AssetBrowserPanel::BeginPicker(AssetType filter, std::function<void(const AssetEntry &)> callback) {
        pickerCallback_ = std::move(callback);
        pickerFilter_ = filter;
        library_.SetFilter(filter);
        scrollOffset_ = 0;
        updateFilterButtons();
        updateLabels();
        Redraw();
    }

    void AssetBrowserPanel::EndPicker() {
        pickerCallback_ = nullptr;
        updateLabels();
    }

    void AssetBrowserPanel::updateLabels() {
        std::string text = library_.GetBreadcrumb();
        const int count = library_.GetVisibleCount();
        text += "  (" + std::to_string(count) + (count == 1 ? " item)" : " items)");

        if(IsPicking()) {
            text = "[Picking " + std::string(GetAssetTypeName(pickerFilter_)) + "] " + text;
        }

        if(lblPath_.GetText() != text) {
            lblPath_.SetText(text);
        }
    }

    void AssetBrowserPanel::updateFilterButtons() {
        for(int i = 0; i < AssetTypeCount; ++i) {
            const std::string name = GetAssetTypeName(static_cast<AssetType>(i));
            const bool active = (static_cast<int>(library_.GetFilter()) == i);
            filterButtons_[i]->SetText(active ? "[" + name + "]" : name);
        }
    }

    Gorgon::Geometry::Size AssetBrowserPanel::GetGridSize() const {
        if(gridAttached_) {
            auto size = gfxLayer_.GetTargetSize();
            if(size.Width > 0 && size.Height > 0) return size;
        }
        return gridBox_.GetCurrentSize();
    }

    void AssetBrowserPanel::clampScroll() {
        const auto size = GetGridSize();
        const int maxScroll = std::max(0, metrics_.contentHeight - size.Height);
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll);
    }

    int AssetBrowserPanel::HitTestTile(int x, int y) const {
        const int index = GetAssetTileAt(metrics_, scrollOffset_, x, y);
        if(index < 0) return -1;
        if(index >= library_.GetVisibleCount()) return -1;
        return index;
    }

    Gorgon::Geometry::Rectangle AssetBrowserPanel::GetTileBounds(int index) const {
        return GetAssetTileBounds(metrics_, index, scrollOffset_);
    }

    const Gorgon::Graphics::Bitmap *AssetBrowserPanel::GetThumbnail(const std::string &fullPath) {
        if(fullPath.empty()) return nullptr;

        const auto existing = thumbnails_.find(fullPath);
        if(existing != thumbnails_.end()) {
            const auto &entry = existing->second;
            return entry.bitmap ? entry.bitmap.get() : nullptr;
        }

        ThumbnailEntry entry;
        entry.attempted = true;

        if(attached_) {
            auto bitmap = std::make_unique<Gorgon::Graphics::Bitmap>();
            if(bitmap->Import(fullPath) && bitmap->HasData()) {
                const auto size = bitmap->GetSize();
                if(size.Width > 0 && size.Height > 0) {
                    const int target = std::max(16, metrics_.thumbSize > 0 ? metrics_.thumbSize : 48);
                    float scale = std::min(static_cast<float>(target) / static_cast<float>(size.Width),
                                           static_cast<float>(target) / static_cast<float>(size.Height));
                    scale = std::min(scale, 2.0f);

                    const int newW = std::max(1, static_cast<int>(std::lround(size.Width * scale)));
                    const int newH = std::max(1, static_cast<int>(std::lround(size.Height * scale)));

                    auto scaled = std::make_unique<Gorgon::Graphics::Bitmap>(
                        newW == size.Width && newH == size.Height ? bitmap->Duplicate() : bitmap->Scale(newW, newH)
                    );
                    if(scaled->HasData()) {
                        scaled->Prepare();
                        entry.bitmap = std::move(scaled);
                    }
                }
            }
        }

        auto [inserted, _] = thumbnails_.emplace(fullPath, std::move(entry));
        return inserted->second.bitmap ? inserted->second.bitmap.get() : nullptr;
    }

    std::size_t AssetBrowserPanel::GetThumbnailCount() const {
        return static_cast<std::size_t>(std::count_if(thumbnails_.begin(), thumbnails_.end(),
            [](const auto &pair) { return static_cast<bool>(pair.second.bitmap); }));
    }

    bool AssetBrowserPanel::InvalidateThumbnail(const std::string &fullPath) {
        if(fullPath.empty() || thumbnails_.empty())
            return false;

        bool erased = false;
        for(auto it = thumbnails_.begin(); it != thumbnails_.end(); ) {
            if(FileWatcher::PathsEqual(it->first, fullPath)) {
                it = thumbnails_.erase(it);
                erased = true;
            } else {
                ++it;
            }
        }
        if(erased) {
            Redraw();
        }
        return erased;
    }

    void AssetBrowserPanel::ClearThumbnails() {
        thumbnails_.clear();
        Redraw();
    }

    void AssetBrowserPanel::HandleMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
        // Gorgon sets helper bits on button masks, so compare only the low button bits.
        if(((static_cast<int>(btn) & static_cast<int>(Gorgon::Input::Mouse::Button::Left)) & 0x1FF) == 0) {
            return;
        }

        const int index = HitTestTile(pt.X, pt.Y);
        if(index < 0) return;

        library_.SetSelectedIndex(index);
        if(const AssetEntry *entry = library_.GetSelected()) {
            if(OnAssetSelected) OnAssetSelected(*entry);
        }
        Redraw();

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime_).count();
        if(index == lastClickIndex_ && elapsed <= 400) {
            lastClickIndex_ = -1;
            activateIndex(index);
            return;
        }

        lastClickIndex_ = index;
        lastClickTime_ = now;

        // Arm drag-and-drop for image/script/prefab assets when not in picker mode.
        dragCandidateIndex_ = -1;
        dragging_ = false;
        if(!IsPicking()) {
            if(const AssetEntry *entry = library_.GetSelected();
               entry && !entry->isDirectory &&
               (entry->type == AssetType::Images || entry->type == AssetType::Scripts || entry->type == AssetType::Prefabs)) {
                dragCandidateIndex_ = index;
                dragStartPt_ = pt;
                dragEntry_ = *entry;
            }
        }
    }

    Gorgon::Geometry::Point AssetBrowserPanel::toWindowPoint(Gorgon::Geometry::Point gridPt) const {
        if(gridAttached_) {
            return inputLayer_.TranslateToTopLevel(gridPt);
        }
        return gridBox_.GetCurrentLocation() + gridPt;
    }

    void AssetBrowserPanel::HandleMouseMove(Gorgon::Geometry::Point pt) {
        if(dragCandidateIndex_ >= 0) {
            if(!dragging_) {
                if(std::abs(pt.X - dragStartPt_.X) >= 6 || std::abs(pt.Y - dragStartPt_.Y) >= 6) {
                    dragging_ = true;
                    lastClickIndex_ = -1;
                    Redraw();
                }
            }
            if(dragging_ && OnAssetDragMove) {
                OnAssetDragMove(dragEntry_, toWindowPoint(pt));
            }
            return;
        }

        const int hover = HitTestTile(pt.X, pt.Y);
        if(hover != hoverIndex_) {
            hoverIndex_ = hover;
            Redraw();
        }
    }

    void AssetBrowserPanel::HandleMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button /*btn*/) {
        if(dragCandidateIndex_ < 0) return;

        const bool wasDragging = dragging_;
        const AssetEntry entry = dragEntry_;
        dragCandidateIndex_ = -1;
        dragging_ = false;
        dragEntry_ = AssetEntry{};

        if(wasDragging) {
            Redraw();
            if(OnAssetDragEnd) {
                OnAssetDragEnd(entry, toWindowPoint(pt));
            }
        }
    }

    bool AssetBrowserPanel::HandleMouseScroll(Gorgon::Geometry::Point /*pt*/, float amount) {
        if(amount == 0.0f) return false;

        const int step = std::max(24, metrics_.tileHeight);
        scrollOffset_ -= static_cast<int>(std::lround(amount * step * 0.5f));
        clampScroll();
        Redraw();
        return true;
    }

    void AssetBrowserPanel::activateIndex(int index) {
        library_.SetSelectedIndex(index);
        const AssetEntry *entry = library_.GetSelected();
        if(!entry) return;

        if(entry->isDirectory) {
            if(library_.NavigateTo(entry->name)) {
                scrollOffset_ = 0;
                Refresh();
            }
            return;
        }

        AssetEntry copy = *entry;
        if(pickerCallback_) {
            auto callback = pickerCallback_;
            callback(copy);
            return;
        }

        if(OnAssetActivated) {
            OnAssetActivated(copy);
        }
    }

    void AssetBrowserPanel::Redraw() {
        if(!attached_ || !gridAttached_) return;

        gfxLayer_.Clear();

        const auto size = GetGridSize();
        if(size.Width <= 0 || size.Height <= 0) return;

        gfxLayer_.Draw(0.0f, 0.0f, static_cast<float>(size.Width), static_cast<float>(size.Height),
                       Col(0.10f, 0.11f, 0.14f));

        const int count = library_.GetVisibleCount();
        metrics_ = ComputeAssetGridMetrics(size.Width, size.Height, count);
        clampScroll();

        if(count == 0) {
            drawEmptyState(size.Width, size.Height);
            return;
        }

        for(int i = 0; i < count; ++i) {
            const auto bounds = GetAssetTileBounds(metrics_, i, scrollOffset_);
            if(bounds.Y + bounds.Height <= 0) continue;
            if(bounds.Y >= size.Height) break;

            drawTile(i, bounds, i == library_.GetSelectedIndex());
        }
    }

    void AssetBrowserPanel::drawEmptyState(int width, int height) {
        auto printer = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::Info);
        printer.SetColor(Col(0.55f, 0.58f, 0.65f));

        std::string text;
        if(library_.GetRoot().empty()) {
            text = "No folder selected - use Root... to pick a project folder";
        } else if(!library_.GetSearch().empty() || library_.GetFilter() != AssetType::All) {
            text = "No assets match the current filter";
        } else {
            text = "This folder has no supported assets";
        }

        printer.PrintNoWrap(gfxLayer_, text, 10, (height - printer.GetHeight()) / 2, width - 20,
                            Gorgon::Graphics::TextAlignment::Center);
    }

    void AssetBrowserPanel::drawTile(int index, const Gorgon::Geometry::Rectangle &bounds, bool selected) {
        const auto &visible = library_.GetVisible();
        if(index < 0 || index >= static_cast<int>(visible.size())) return;

        const AssetEntry &entry = visible[index];
        const float x = static_cast<float>(bounds.X);
        const float y = static_cast<float>(bounds.Y);
        const float w = static_cast<float>(bounds.Width);
        const float h = static_cast<float>(bounds.Height);

        const bool hover = (index == hoverIndex_);
        Gorgon::Graphics::RGBAf background = Col(0.16f, 0.18f, 0.22f);
        if(selected) {
            background = Col(0.23f, 0.51f, 0.96f, 0.95f);
        } else if(hover) {
            background = Col(0.22f, 0.25f, 0.30f);
        }

        gfxLayer_.Draw(x, y, w, h, background);
        const int thumb = metrics_.thumbSize;
        const float thumbX = x + (w - static_cast<float>(thumb)) * 0.5f;
        const float thumbY = y + 5.0f;

        bool drewImage = false;
        if(entry.type == AssetType::Images) {
            if(const auto *bitmap = GetThumbnail(entry.fullPath)) {
                const auto size = bitmap->GetSize();
                if(size.Width > 0 && size.Height > 0) {
                    const float scale = std::min({static_cast<float>(thumb) / size.Width,
                                                  static_cast<float>(thumb) / size.Height, 1.0f});
                    const float drawW = size.Width * scale;
                    const float drawH = size.Height * scale;
                    const float dx = x + (w - drawW) * 0.5f;
                    const float dy = thumbY + (thumb - drawH) * 0.5f;

                    gfxLayer_.Draw(*bitmap,
                        Gorgon::Geometry::Pointf{dx, dy},
                        Gorgon::Geometry::Pointf{dx + drawW, dy},
                        Gorgon::Geometry::Pointf{dx + drawW, dy + drawH},
                        Gorgon::Geometry::Pointf{dx, dy + drawH},
                        Gorgon::Graphics::RGBAf(1.0f));
                    drewImage = true;
                }
            }
        }

        auto printer = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::Small);

        if(!drewImage) {
            // Folder / non-image placeholder with a type tag in the middle.
            Gorgon::Graphics::RGBAf tag = Col(0.34f, 0.38f, 0.46f);
            std::string tagText = entry.extension.empty() ? std::string("?") : entry.extension;
            if(entry.isDirectory) {
                tag = Col(0.85f, 0.68f, 0.26f);
                tagText = "DIR";
            } else if(entry.type == AssetType::Scripts) {
                tag = Col(0.44f, 0.72f, 0.98f);
            } else if(entry.type == AssetType::Prefabs) {
                tag = Col(0.30f, 0.78f, 0.86f);
                tagText = "PFB";
            } else if(entry.type == AssetType::Audio) {
                tag = Col(0.72f, 0.50f, 0.92f);
            } else if(entry.type == AssetType::Fonts) {
                tag = Col(0.42f, 0.76f, 0.62f);
            } else if(entry.type == AssetType::Resources) {
                tag = Col(0.92f, 0.58f, 0.42f);
            }

            gfxLayer_.Draw(thumbX + 6.0f, thumbY + 6.0f, static_cast<float>(thumb) - 12.0f,
                           static_cast<float>(thumb) - 12.0f, tag);

            std::string upper = tagText;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            printer.SetColor(Col(0.08f, 0.09f, 0.11f));
            printer.PrintNoWrap(gfxLayer_, Ellipsize(printer, upper, thumb - 16),
                                static_cast<int>(thumbX) + 8, static_cast<int>(thumbY) + thumb / 2 - 6,
                                thumb - 16, Gorgon::Graphics::TextAlignment::Center);
        }

        printer.SetColor(selected ? Col(1.0f, 1.0f, 1.0f) : Col(0.83f, 0.86f, 0.91f));
        const std::string label = Ellipsize(printer, entry.name, static_cast<int>(w) - 8);
        printer.PrintNoWrap(gfxLayer_, label, static_cast<int>(x) + 4,
                            static_cast<int>(y + h) - kTileLabelHeight + 3,
                            static_cast<int>(w) - 8, Gorgon::Graphics::TextAlignment::Center);

        const bool isDragged = dragging_ && index == dragCandidateIndex_;
        DrawOutline(gfxLayer_, x, y, w, h, isDragged ? 2.0f : 1.0f,
                    isDragged ? Col(1.0f, 0.82f, 0.28f, 0.95f)
                              : (selected ? Col(0.55f, 0.75f, 1.0f, 0.9f) : Col(0.26f, 0.29f, 0.35f)));
    }

} // namespace Gorgon::Editor
