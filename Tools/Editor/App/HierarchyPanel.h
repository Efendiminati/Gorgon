#pragma once

#include "Core/Scene.h"
#include "Viewport.h"

#include <Gorgon/UI/Dimension.h>
#include <Gorgon/UI/Widget.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Dropdown.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Listbox.h>
#include <Gorgon/Widgets/Panel.h>

#include <functional>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// One visual row of the scene hierarchy tree.
    struct HierarchyRow {
        EntityId id = InvalidEntityId;
        int depth = 0;
        bool hasChildren = false;
        bool visible = true;
        bool locked = false;
        std::string name;
        std::string text;   // pre-composed display string (indent + markers + name)

        /// Widgets::SimpleListbox renders items through String::From, which picks this up
        /// for any type convertible to std::string.
        operator std::string() const {
            return text;
        }

        bool operator==(const HierarchyRow &other) const {
            return id == other.id && text == other.text;
        }
    };

    /**
     * Left dock: scene hierarchy tree plus entity CRUD / reparenting actions.
     *
     * The panel owns no selection of its own; the single source of truth is
     * EditorViewport::GetSelectedEntity(). Select in the tree -> viewport selection is
     * updated; select in the viewport -> EditorApp calls SyncSelection().
     */
    class HierarchyPanel {
    public:
        HierarchyPanel(Scene &scene, EditorViewport &viewport, Gorgon::Widgets::Panel &panel);
        ~HierarchyPanel();

        HierarchyPanel(const HierarchyPanel &) = delete;
        HierarchyPanel &operator=(const HierarchyPanel &) = delete;

        /// Rebuilds the row list, the reparent dropdown, and re-syncs the selection.
        void Rebuild();

        /// Re-syncs the listbox selection to the viewport's current selection.
        void SyncSelection();

        /// Positions every widget inside the dock from the panel's interior size.
        void Layout();

        /// Currently selected entity (mirrors the viewport).
        EntityId GetSelectedEntity() const;

        /// Number of rows currently shown (used by the smoke test).
        long GetRowCount() const;

        /// Fired after any structural change (add / child / duplicate / delete / reparent / rename).
        std::function<void()> OnStructureChanged;

        /// Fired when the user picks a different entity in the tree.
        std::function<void(EntityId)> OnSelectionChanged;

        /// Fired after a non-structural edit (visibility / lock) so the caller can redraw.
        std::function<void()> OnSceneModified;

    private:
        using Widget = Gorgon::UI::Widget;

        void build();
        void registerEvents();
        void rebuildRows();
        void rebuildParentChoices();
        void updateActionState();
        void notifyModified(bool structural);

        Entity *selectedEntity();

        void onAddEntity();
        void onAddChildEntity();
        void onDuplicate();
        void onDelete();
        void onRename();
        void onToggleVisible();
        void onToggleLocked();
        void onParentChosen(long index);

        Scene &scene_;
        EditorViewport &viewport_;
        Gorgon::Widgets::Panel &panel_;

        Gorgon::Widgets::Label header_{"Hierarchy", Gorgon::Widgets::Registry::Label_Bold};
        Gorgon::Widgets::Button btnAdd_{"+ Entity"};
        Gorgon::Widgets::Button btnAddChild_{"+ Child"};
        Gorgon::Widgets::Button btnDuplicate_{"Duplicate"};
        Gorgon::Widgets::Button btnDelete_{"Delete"};
        Gorgon::Widgets::Button btnRename_{"Rename"};
        Gorgon::Widgets::Button btnVisible_{"Hide"};
        Gorgon::Widgets::Button btnLocked_{"Lock"};
        Gorgon::Widgets::Label lblParent_{"Parent", Gorgon::Widgets::Registry::Label_Regular};
        Gorgon::Widgets::DropdownList<std::string> parentBox_;
        Gorgon::Widgets::SimpleListbox<HierarchyRow> list_;

        std::vector<HierarchyRow> rows_;
        std::vector<EntityId> parentChoices_;
        Gorgon::Event<Gorgon::UI::Widget>::Token boundsToken_ =
            Gorgon::Event<Gorgon::UI::Widget>::EmptyToken;

        bool suppressSelection_ = false;
        bool suppressParent_ = false;
    };

} // namespace Gorgon::Editor
