#include "HierarchyPanel.h"

#include <Gorgon/Graphics/Color.h>
#include <Gorgon/Input/Keyboard.h>
#include <Gorgon/UI/Dialog.h>
#include <Gorgon/Widgets/Registry.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

namespace Gorgon :: Editor {

    namespace {
        constexpr int kDragThresholdPx = 5;

        Gorgon::Graphics::RGBAf Col(float r, float g, float b, float a = 1.0f) {
            return Gorgon::Graphics::RGBAf(r, g, b, a);
        }

        void DrawOutline(Gorgon::Graphics::Layer &layer, float x, float y, float w, float h,
                         float thickness, Gorgon::Graphics::RGBAf color) {
            layer.Draw(x, y, w, thickness, color);
            layer.Draw(x, y + h - thickness, w, thickness, color);
            layer.Draw(x, y, thickness, h, color);
            layer.Draw(x + w - thickness, y, thickness, h, color);
        }

        std::string ComposeRowText(const Entity &entity, int depth, bool hasChildren,
                                   bool showMultiMarker, bool isSelected) {
            std::string text;
            if(showMultiMarker) {
                text += isSelected ? "[x] " : "[ ] ";
            }
            text.append(std::size_t(std::max(0, depth)) * 3, ' ');

            // visibility marker, lock marker, then the entity name and its id
            text += entity.visible ? "* " : "- ";
            text += entity.locked ? "L " : "";
            text += hasChildren ? "+ " : "  ";
            text += entity.name.empty() ? std::string("<unnamed>") : entity.name;
            text += "  #";
            text += std::to_string(entity.id);

            return text;
        }
    }

    std::vector<HierarchyRow> BuildHierarchyRows(const Scene &scene, const EditorViewport *viewport) {
        std::vector<HierarchyRow> rows;
        const bool showMulti = viewport && viewport->GetSelectionCount() > 1;

        std::function<void(EntityId, int)> walk = [&](EntityId parentId, int depth) {
            for(EntityId childId : scene.GetChildIds(parentId)) {
                const Entity *entity = scene.FindEntity(childId);
                if(!entity)
                    continue;

                HierarchyRow row;
                row.id = childId;
                row.depth = depth;
                row.hasChildren = !scene.GetChildIds(childId).empty();
                row.visible = entity->visible;
                row.locked = entity->locked;
                row.name = entity->name;
                const bool selected = viewport && viewport->IsEntitySelected(childId);
                row.text = ComposeRowText(*entity, depth, row.hasChildren, showMulti, selected);

                rows.push_back(std::move(row));
                walk(childId, depth + 1);
            }
        };

        walk(InvalidEntityId, 0);
        return rows;
    }

    int HitTestHierarchyRow(int rowCount, int rowHeight, int scrollOffset, int y) {
        if(rowCount <= 0 || rowHeight <= 0 || y < 0)
            return -1;

        const int adjustedY = y + std::max(0, scrollOffset);
        const int index = adjustedY / rowHeight;
        if(index < 0 || index >= rowCount)
            return -1;

        return index;
    }

    HierarchyDropTarget ResolveHierarchyDropTarget(const Scene &scene,
                                                   const std::vector<HierarchyRow> &rows,
                                                   EntityId draggedId,
                                                   int dropY,
                                                   int rowHeight,
                                                   int scrollOffset) {
        HierarchyDropTarget target;
        const Entity *dragged = scene.FindEntity(draggedId);
        if(!dragged)
            return target;

        const int hitRow = (dropY < 0)
            ? -1
            : HitTestHierarchyRow(static_cast<int>(rows.size()), rowHeight, scrollOffset, dropY);
        if(hitRow < 0) {
            target.isRoot = true;
            target.parentId = InvalidEntityId;
            target.rowIndex = -1;
            target.valid = (dragged->parentId != InvalidEntityId);
            return target;
        }

        const EntityId targetId = rows[static_cast<std::size_t>(hitRow)].id;
        target.isRoot = false;
        target.parentId = targetId;
        target.rowIndex = hitRow;
        target.valid = (dragged->parentId != targetId) && scene.CanReparent(draggedId, targetId);
        return target;
    }

    bool ApplyHierarchyDragReparent(Scene &scene, EntityId draggedId, EntityId targetParentId) {
        const Entity *dragged = scene.FindEntity(draggedId);
        if(!dragged)
            return false;
        if(dragged->parentId == targetParentId)
            return false;
        return scene.Reparent(draggedId, targetParentId);
    }

    HierarchyPanel::HierarchyPanel(Scene &scene, EditorViewport &viewport, Gorgon::Widgets::Panel &panel) :
        scene_(scene), viewport_(viewport), panel_(panel)
    {
        build();
        registerEvents();
        Rebuild();
    }

    HierarchyPanel::~HierarchyPanel() {
        panel_.BoundsChangedEvent.Unregister(boundsToken_);
    }

    void HierarchyPanel::build() {
        panel_.EnableScroll(true, false);

        panel_.Add(header_);
        panel_.Add(btnAdd_);
        panel_.Add(btnAddChild_);
        panel_.Add(btnDuplicate_);
        panel_.Add(btnDelete_);
        panel_.Add(btnRename_);
        panel_.Add(btnVisible_);
        panel_.Add(btnLocked_);
        panel_.Add(lblParent_);
        panel_.Add(parentBox_);
        panel_.Add(treeBox_);

        if(!treeAttached_) {
            auto &root = treeBox_.GetLayer();
            root.Add(treeGfx_);
            root.Add(treeInput_);

            treeInput_.SetDown([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
                HandleTreeMouseDown(pt, btn);
            });
            treeInput_.SetMove([this](Gorgon::Geometry::Point pt) {
                HandleTreeMouseMove(pt);
            });
            treeInput_.SetUp([this](Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
                HandleTreeMouseUp(pt, btn);
            });
            treeInput_.SetScroll([this](Gorgon::Geometry::Point pt, float amount) {
                return HandleTreeMouseScroll(pt, amount);
            });
            treeAttached_ = true;
        }

        boundsToken_ = panel_.BoundsChangedEvent.Register([this]() {
            Layout();
        });
    }

    void HierarchyPanel::registerEvents() {
        btnAdd_.ClickEvent.Register([this]() { onAddEntity(); });
        btnAddChild_.ClickEvent.Register([this]() { onAddChildEntity(); });
        btnDuplicate_.ClickEvent.Register([this]() { onDuplicate(); });
        btnDelete_.ClickEvent.Register([this]() { onDelete(); });
        btnRename_.ClickEvent.Register([this]() { onRename(); });
        btnVisible_.ClickEvent.Register([this]() { onToggleVisible(); });
        btnLocked_.ClickEvent.Register([this]() { onToggleLocked(); });

        parentBox_.ChangedEvent.Register([this](long index) {
            onParentChosen(index);
        });
    }

    Entity *HierarchyPanel::selectedEntity() {
        return scene_.FindEntity(viewport_.GetSelectedEntity());
    }

    EntityId HierarchyPanel::GetSelectedEntity() const {
        return viewport_.GetSelectedEntity();
    }

    long HierarchyPanel::GetRowCount() const {
        return long(rows_.size());
    }

    void HierarchyPanel::DuplicateSelected() {
        onDuplicate();
    }

    void HierarchyPanel::DeleteSelected() {
        onDelete();
    }

    void HierarchyPanel::AddEntity() {
        onAddEntity();
    }

    void HierarchyPanel::HandleRowClick(long index, bool shiftHeld) {
        EntityId id = InvalidEntityId;
        if(index >= 0 && std::size_t(index) < rows_.size())
            id = rows_[std::size_t(index)].id;

        if(shiftHeld) {
            if(id != InvalidEntityId) {
                viewport_.ToggleEntitySelection(id, false);
            }
        } else {
            if(id == InvalidEntityId) {
                if(viewport_.GetSelectionCount() == 0)
                    return;
                viewport_.ClearSelection(false);
            } else {
                if(viewport_.GetSelectionCount() == 1 && viewport_.GetSelectedEntity() == id)
                    return;
                viewport_.SetSelectedEntity(id, false);
            }
        }

        SyncSelection();
        if(OnSelectionChanged)
            OnSelectionChanged(viewport_.GetSelectedEntity());
    }

    Gorgon::Geometry::Size HierarchyPanel::GetTreeSize() const {
        if(treeAttached_) {
            const auto s = treeGfx_.GetCalculatedSize();
            if(s.Width > 0 && s.Height > 0)
                return s;
        }
        const auto bs = treeBox_.GetCurrentSize();
        if(bs.Width > 0 && bs.Height > 0)
            return bs;
        return {200, 300};
    }

    void HierarchyPanel::clampScroll() {
        const auto sz = GetTreeSize();
        const int contentH = static_cast<int>(rows_.size()) * kHierarchyRowHeight;
        const int maxScroll = std::max(0, contentH - std::max(0, sz.Height));
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll);
    }

    void HierarchyPanel::HandleTreeMouseDown(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button btn) {
        if(((static_cast<int>(btn) & static_cast<int>(Gorgon::Input::Mouse::Button::Left)) & 0x1FF) == 0) {
            return;
        }

        const int rowIdx = HitTestHierarchyRow(static_cast<int>(rows_.size()), kHierarchyRowHeight, scrollOffset_, pt.Y);
        dragCandidateIndex_ = rowIdx;
        dragSourceId_ = (rowIdx >= 0 && std::size_t(rowIdx) < rows_.size())
            ? rows_[std::size_t(rowIdx)].id
            : InvalidEntityId;
        dragging_ = false;
        dragStartPt_ = pt;
        dragTarget_ = HierarchyDropTarget{};

        if(rowIdx < 0) {
            HandleRowClick(-1, false);
        }
    }

    void HierarchyPanel::HandleTreeMouseMove(Gorgon::Geometry::Point pt) {
        if(dragCandidateIndex_ >= 0 && dragSourceId_ != InvalidEntityId && viewport_.IsEditEnabled()) {
            if(!dragging_) {
                if(std::abs(pt.X - dragStartPt_.X) >= kDragThresholdPx ||
                   std::abs(pt.Y - dragStartPt_.Y) >= kDragThresholdPx) {
                    dragging_ = true;
                }
            }
            if(dragging_) {
                dragTarget_ = ResolveHierarchyDropTarget(
                    scene_, rows_, dragSourceId_, pt.Y, kHierarchyRowHeight, scrollOffset_);
                Redraw();
                return;
            }
        }

        const int hover = HitTestHierarchyRow(static_cast<int>(rows_.size()), kHierarchyRowHeight, scrollOffset_, pt.Y);
        if(hover != hoverIndex_) {
            hoverIndex_ = hover;
            Redraw();
        }
    }

    void HierarchyPanel::HandleTreeMouseUp(Gorgon::Geometry::Point pt, Gorgon::Input::Mouse::Button /*btn*/) {
        const int candidateIdx = dragCandidateIndex_;
        const EntityId sourceId = dragSourceId_;
        const bool wasDragging = dragging_;

        dragCandidateIndex_ = -1;
        dragSourceId_ = InvalidEntityId;
        dragging_ = false;
        dragTarget_ = HierarchyDropTarget{};

        if(wasDragging && sourceId != InvalidEntityId && viewport_.IsEditEnabled()) {
            const HierarchyDropTarget target = ResolveHierarchyDropTarget(
                scene_, rows_, sourceId, pt.Y, kHierarchyRowHeight, scrollOffset_);
            if(target.valid) {
                DragReparentEntity(sourceId, target.parentId);
            } else {
                Redraw();
            }
            return;
        }

        if(candidateIdx >= 0) {
            using Mod = Gorgon::Input::Keyboard::Modifier;
            const bool shift = viewport_.IsShiftHeld() ||
                static_cast<bool>(Gorgon::Input::Keyboard::CurrentModifier & Mod(Mod::Shift));
            HandleRowClick(candidateIdx, shift);
        }
    }

    bool HierarchyPanel::HandleTreeMouseScroll(Gorgon::Geometry::Point /*pt*/, float amount) {
        if(amount == 0.0f)
            return false;

        scrollOffset_ -= static_cast<int>(std::lround(amount * static_cast<float>(kHierarchyRowHeight) * 2.0f));
        clampScroll();
        Redraw();
        return true;
    }

    bool HierarchyPanel::DragReparentEntity(EntityId draggedId, EntityId targetParentId) {
        if(!viewport_.IsEditEnabled())
            return false;

        if(!ApplyHierarchyDragReparent(scene_, draggedId, targetParentId))
            return false;

        viewport_.SetSelectedEntity(draggedId, false);
        notifyModified(true);
        return true;
    }

    void HierarchyPanel::rebuildRows() {
        multiSelectMarkers_ = viewport_.GetSelectionCount() > 1;
        rows_ = BuildHierarchyRows(scene_, &viewport_);
    }

    void HierarchyPanel::rebuildParentChoices() {
        suppressParent_ = true;

        parentBox_.List.Clear();
        parentChoices_.clear();

        parentBox_.List.Add(std::string("-- root --"));
        parentChoices_.push_back(InvalidEntityId);

        const EntityId selectedId = viewport_.GetSelectedEntity();
        const Entity *selected = scene_.FindEntity(selectedId);

        if(selected) {
            for(const Entity &candidate : scene_.GetEntities()) {
                if(candidate.id == selectedId)
                    continue;
                if(!scene_.CanReparent(selectedId, candidate.id))
                    continue;

                std::string label = candidate.name.empty() ? std::string("<unnamed>") : candidate.name;
                label += "  #";
                label += std::to_string(candidate.id);
                parentBox_.List.Add(label);
                parentChoices_.push_back(candidate.id);
            }
        }

        long current = 0;
        if(selected) {
            for(std::size_t i = 0; i < parentChoices_.size(); ++i) {
                if(parentChoices_[i] == selected->parentId) {
                    current = long(i);
                    break;
                }
            }
        }

        parentBox_.List.SetSelectedIndex(current);
        parentBox_.SetEnabled(selected != nullptr);

        suppressParent_ = false;
    }

    void HierarchyPanel::updateActionState() {
        const std::size_t selCount = viewport_.GetSelectionCount();
        const Entity *entity = selectedEntity();
        const bool hasSelection = selCount > 0;
        const bool singleSelection = selCount == 1 && entity != nullptr;

        btnAddChild_.SetEnabled(singleSelection);
        btnDuplicate_.SetEnabled(hasSelection);
        btnDelete_.SetEnabled(hasSelection);
        btnRename_.SetEnabled(singleSelection);
        btnVisible_.SetEnabled(hasSelection);
        btnLocked_.SetEnabled(hasSelection);
        lblParent_.SetEnabled(singleSelection);
        parentBox_.SetEnabled(singleSelection);

        btnVisible_.Text = (entity && entity->visible) ? "Hide" : "Show";
        btnLocked_.Text  = (entity && entity->locked)  ? "Unlock" : "Lock";
    }

    void HierarchyPanel::Rebuild() {
        rebuildRows();
        clampScroll();
        rebuildParentChoices();
        SyncSelection();
        updateActionState();
        Layout();
    }

    void HierarchyPanel::SyncSelection() {
        const bool showMulti = viewport_.GetSelectionCount() > 1;
        if(showMulti || multiSelectMarkers_) {
            rebuildRows();
        }

        rebuildParentChoices();
        updateActionState();
        Redraw();
    }

    void HierarchyPanel::Redraw() {
        if(!treeAttached_)
            return;

        treeGfx_.Clear();

        const auto size = GetTreeSize();
        if(size.Width <= 0 || size.Height <= 0)
            return;

        const float w = static_cast<float>(size.Width);
        const float h = static_cast<float>(size.Height);
        treeGfx_.Draw(0.0f, 0.0f, w, h, Col(0.11f, 0.12f, 0.15f));
        clampScroll();

        auto printer = Gorgon::Widgets::Registry::Active().Printer(Gorgon::Graphics::NamedFont::Small);

        for(std::size_t i = 0; i < rows_.size(); ++i) {
            const int ry = static_cast<int>(i) * kHierarchyRowHeight - scrollOffset_;
            if(ry + kHierarchyRowHeight <= 0)
                continue;
            if(ry >= size.Height)
                break;

            const HierarchyRow &row = rows_[i];
            const bool isSelected = viewport_.IsEntitySelected(row.id);
            const bool isHover = (static_cast<int>(i) == hoverIndex_);
            const bool isDragSource = dragging_ && (row.id == dragSourceId_);
            const bool isDropTarget = dragging_ && !dragTarget_.isRoot && (dragTarget_.rowIndex == static_cast<int>(i));

            Gorgon::Graphics::RGBAf bg = (i % 2 == 0) ? Col(0.13f, 0.14f, 0.18f) : Col(0.14f, 0.15f, 0.19f);
            if(isSelected) {
                bg = Col(0.22f, 0.45f, 0.85f, 0.90f);
            } else if(isHover) {
                bg = Col(0.20f, 0.22f, 0.27f);
            }
            if(isDragSource) {
                bg = Col(0.36f, 0.30f, 0.16f, 0.90f);
            }
            treeGfx_.Draw(0.0f, static_cast<float>(ry), w, static_cast<float>(kHierarchyRowHeight), bg);

            if(isDropTarget) {
                const auto fill = dragTarget_.valid ? Col(0.22f, 0.78f, 0.98f, 0.26f) : Col(0.92f, 0.28f, 0.28f, 0.26f);
                const auto border = dragTarget_.valid ? Col(0.32f, 0.86f, 1.0f, 0.98f) : Col(0.98f, 0.36f, 0.36f, 0.98f);
                treeGfx_.Draw(0.0f, static_cast<float>(ry), w, static_cast<float>(kHierarchyRowHeight), fill);
                DrawOutline(treeGfx_, 0.0f, static_cast<float>(ry), w, static_cast<float>(kHierarchyRowHeight), 2.0f, border);
            } else if(isDragSource) {
                DrawOutline(treeGfx_, 0.0f, static_cast<float>(ry), w, static_cast<float>(kHierarchyRowHeight), 1.5f,
                            Col(1.0f, 0.80f, 0.30f, 0.95f));
            }

            printer.SetColor(row.visible ? Col(0.92f, 0.93f, 0.96f) : Col(0.55f, 0.58f, 0.65f));
            const int textY = ry + std::max(0, (kHierarchyRowHeight - printer.GetHeight()) / 2);
            printer.PrintNoWrap(treeGfx_, row.text, 6, textY, std::max(10, size.Width - 12));
        }

        if(dragging_ && dragTarget_.isRoot && dragTarget_.valid) {
            const int barH = kHierarchyRowHeight;
            const int barY = std::min(static_cast<int>(rows_.size()) * kHierarchyRowHeight - scrollOffset_,
                                      std::max(0, size.Height - barH));
            const int drawY = std::max(0, barY);
            treeGfx_.Draw(0.0f, static_cast<float>(drawY), w, static_cast<float>(barH), Col(0.20f, 0.68f, 0.92f, 0.28f));
            DrawOutline(treeGfx_, 0.0f, static_cast<float>(drawY), w, static_cast<float>(barH), 2.0f,
                        Col(0.32f, 0.86f, 1.0f, 0.98f));
            printer.SetColor(Col(0.88f, 0.96f, 1.0f));
            const int textY = drawY + std::max(0, (barH - printer.GetHeight()) / 2);
            printer.PrintNoWrap(treeGfx_, "Drop to unparent to root", 6, textY, std::max(10, size.Width - 12),
                                Gorgon::Graphics::TextAlignment::Center);
        }

        DrawOutline(treeGfx_, 0.0f, 0.0f, w, h, 1.0f, Col(0.24f, 0.26f, 0.32f));
    }

    void HierarchyPanel::Layout() {
        using Gorgon::UI::Pixels;

        const auto interior = panel_.GetInteriorSize();
        if(interior.Width <= 0 || interior.Height <= 0)
            return;

        const int pad = std::max(4, panel_.GetSpacing());
        int unit = panel_.GetUnitSize();
        if(unit < 18)
            unit = 18;

        const int width = interior.Width - pad * 2;
        if(width < 40)
            return;

        int y = pad;

        header_.Move(Pixels(pad, y));
        header_.SetWidth(Pixels(width));
        header_.SetHeight(Pixels(unit));
        y += unit + 2;

        const int gap = 3;

        auto place = [&](Gorgon::UI::Widget &widget, int x, int w) {
            widget.Move(Pixels(x, y));
            widget.SetWidth(Pixels(w));
            widget.SetHeight(Pixels(unit));
        };

        // Row 1: entity CRUD
        int w4 = (width - gap * 3) / 4;
        place(btnAdd_,      pad,                    w4);
        place(btnAddChild_, pad + (w4 + gap),       w4);
        place(btnDuplicate_, pad + (w4 + gap) * 2,  w4);
        place(btnDelete_,   pad + (w4 + gap) * 3,   width - (w4 + gap) * 3);
        y += unit + gap;

        // Row 2: rename / visibility / lock
        int w3 = (width - gap * 2) / 3;
        place(btnRename_,  pad,                   w3);
        place(btnVisible_, pad + (w3 + gap),      w3);
        place(btnLocked_,  pad + (w3 + gap) * 2,  width - (w3 + gap) * 2);
        y += unit + gap;

        // Row 3: reparent dropdown
        int labelW = std::min(std::max(40, width / 3), 64);
        lblParent_.Move(Pixels(pad, y));
        lblParent_.SetWidth(Pixels(labelW));
        lblParent_.SetHeight(Pixels(unit));

        parentBox_.Move(Pixels(pad + labelW + 2, y));
        parentBox_.SetWidth(Pixels(width - labelW - 2));
        parentBox_.SetHeight(Pixels(unit));
        y += unit + pad;

        const int listHeight = std::max(unit * 3, interior.Height - y - pad);
        treeBox_.Move(Pixels(pad, y));
        treeBox_.Resize(Pixels(width, listHeight));
        Redraw();
    }

    void HierarchyPanel::notifyModified(bool structural) {
        if(OnSceneModified)
            OnSceneModified();

        if(structural) {
            Rebuild();
            if(OnStructureChanged)
                OnStructureChanged();
        }
        else {
            // Text (visibility / lock markers) changed -> rebuild row labels and redraw.
            rebuildRows();
            updateActionState();
            Redraw();
        }

        if(OnSelectionChanged)
            OnSelectionChanged(viewport_.GetSelectedEntity());
    }

    void HierarchyPanel::onAddEntity() {
        const EntityId newId = scene_.CreateEntity("Entity").id;
        viewport_.SetSelectedEntity(newId, false);
        notifyModified(true);
    }

    void HierarchyPanel::onAddChildEntity() {
        const EntityId parentId = viewport_.GetSelectedEntity();
        const EntityId newId = scene_.CreateEntity("Entity", parentId).id;
        viewport_.SetSelectedEntity(newId, false);
        notifyModified(true);
    }

    void HierarchyPanel::onDuplicate() {
        if(viewport_.DuplicateSelectedEntities(false).empty())
            return;

        notifyModified(true);
    }

    void HierarchyPanel::onDelete() {
        if(!viewport_.DeleteSelectedEntities(false))
            return;

        notifyModified(true);
    }

    void HierarchyPanel::onRename() {
        const EntityId id = viewport_.GetSelectedEntity();
        const Entity *entity = scene_.FindEntity(id);
        if(!entity)
            return;

        const std::string current = entity->name;

        Gorgon::UI::Input<std::string>(
            "Rename Entity",
            "Enter a new name for the selected entity:",
            "Name",
            [this, id](const std::string &value) {
                Entity *target = scene_.FindEntity(id);
                if(!target)
                    return;
                if(target->name == value)
                    return;

                target->name = value;
                notifyModified(true);
            },
            current,
            [](const std::string &value) { return !value.empty(); },
            Gorgon::UI::CloseOption::Cancel
        );
    }

    void HierarchyPanel::onToggleVisible() {
        Entity *entity = selectedEntity();
        if(!entity)
            return;

        const bool target = !entity->visible;
        for(EntityId id : viewport_.GetSelectedEntities()) {
            if(Entity *e = scene_.FindEntity(id))
                e->visible = target;
        }
        notifyModified(false);
    }

    void HierarchyPanel::onToggleLocked() {
        Entity *entity = selectedEntity();
        if(!entity)
            return;

        const bool target = !entity->locked;
        for(EntityId id : viewport_.GetSelectedEntities()) {
            if(Entity *e = scene_.FindEntity(id))
                e->locked = target;
        }
        notifyModified(false);
    }

    void HierarchyPanel::onParentChosen(long index) {
        if(suppressParent_)
            return;

        if(index < 0 || std::size_t(index) >= parentChoices_.size())
            return;

        const EntityId entityId = viewport_.GetSelectedEntity();
        const EntityId newParent = parentChoices_[std::size_t(index)];

        const Entity *entity = scene_.FindEntity(entityId);
        if(!entity || entity->parentId == newParent)
            return;

        if(!scene_.Reparent(entityId, newParent))
            return;

        notifyModified(true);
    }

} // namespace Gorgon::Editor
