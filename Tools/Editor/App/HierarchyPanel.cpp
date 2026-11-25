#include "HierarchyPanel.h"

#include <Gorgon/Input/Keyboard.h>
#include <Gorgon/UI/Dialog.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

namespace Gorgon :: Editor {

    namespace {
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
        panel_.Add(list_);

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

        list_.ChangedEvent.Register([this](long index) {
            if(suppressSelection_)
                return;

            using Mod = Gorgon::Input::Keyboard::Modifier;
            const bool shift = viewport_.IsShiftHeld() ||
                static_cast<bool>(Gorgon::Input::Keyboard::CurrentModifier & Mod(Mod::Shift));
            HandleRowClick(index, shift);
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

    void HierarchyPanel::rebuildRows() {
        rows_.clear();
        const bool showMulti = viewport_.GetSelectionCount() > 1;
        multiSelectMarkers_ = showMulti;

        // Depth-first walk so every child follows its parent.
        std::function<void(EntityId, int)> walk = [&](EntityId parentId, int depth) {
            for(EntityId childId : scene_.GetChildIds(parentId)) {
                const Entity *entity = scene_.FindEntity(childId);
                if(!entity)
                    continue;

                HierarchyRow row;
                row.id = childId;
                row.depth = depth;
                row.hasChildren = !scene_.GetChildIds(childId).empty();
                row.visible = entity->visible;
                row.locked = entity->locked;
                row.name = entity->name;
                row.text = ComposeRowText(*entity, depth, row.hasChildren, showMulti, viewport_.IsEntitySelected(childId));

                rows_.push_back(std::move(row));
                walk(childId, depth + 1);
            }
        };

        walk(InvalidEntityId, 0);
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

        suppressSelection_ = true;
        list_.Clear();
        for(const HierarchyRow &row : rows_)
            list_.Add(row);
        suppressSelection_ = false;

        rebuildParentChoices();
        SyncSelection();
        updateActionState();
        Layout();
    }

    void HierarchyPanel::SyncSelection() {
        const bool showMulti = viewport_.GetSelectionCount() > 1;
        if(showMulti || multiSelectMarkers_) {
            rebuildRows();
            suppressSelection_ = true;
            list_.Clear();
            for(const HierarchyRow &row : rows_)
                list_.Add(row);
            suppressSelection_ = false;
        }

        const EntityId selectedId = viewport_.GetSelectedEntity();

        long index = -1;
        for(std::size_t i = 0; i < rows_.size(); ++i) {
            if(rows_[i].id == selectedId) {
                index = long(i);
                break;
            }
        }

        suppressSelection_ = true;
        list_.SetSelectedIndex(index);
        suppressSelection_ = false;

        rebuildParentChoices();
        updateActionState();
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
        list_.Move(Pixels(pad, y));
        list_.Resize(Pixels(width, listHeight));
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
            // Text (visibility / lock markers) changed -> rebuild row labels but keep selection.
            const EntityId selectedId = viewport_.GetSelectedEntity();
            rebuildRows();

            suppressSelection_ = true;
            list_.Clear();
            for(const HierarchyRow &row : rows_)
                list_.Add(row);

            long index = -1;
            for(std::size_t i = 0; i < rows_.size(); ++i) {
                if(rows_[i].id == selectedId) {
                    index = long(i);
                    break;
                }
            }
            list_.SetSelectedIndex(index);
            suppressSelection_ = false;

            updateActionState();
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
