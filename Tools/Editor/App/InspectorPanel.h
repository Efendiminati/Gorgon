#pragma once

#include "Core/Scene.h"
#include "Viewport.h"

#include <Gorgon/Graphics/Color.h>
#include <Gorgon/UI/Dimension.h>
#include <Gorgon/UI/Widget.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Checkbox.h>
#include <Gorgon/Widgets/ColorPicker.h>
#include <Gorgon/Widgets/Dropdown.h>
#include <Gorgon/Widgets/GeometryBoxes.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Numberbox.h>
#include <Gorgon/Widgets/Panel.h>
#include <Gorgon/Widgets/Textbox.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Converts the editor's byte color into the widget layer's float color.
    inline Gorgon::Graphics::RGBAf ToColorF(const ColorRGBA &color) {
        return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    }

    /// Converts a widget color back into the editor's byte color.
    inline ColorRGBA FromColorF(const Gorgon::Graphics::RGBAf &color) {
        const auto quantize = [](float value) -> std::uint8_t {
            const int rounded = int(value * 255.0f + 0.5f);
            if(rounded < 0)
                return 0;
            if(rounded > 255)
                return 255;
            return std::uint8_t(rounded);
        };

        return {quantize(color.R), quantize(color.G), quantize(color.B), quantize(color.A)};
    }

    /**
     * Right dock: details / property inspector.
     *
     * Binds to the entity currently selected in the viewport; when nothing is selected it
     * shows the scene settings instead. Edits are written straight back into the Scene so
     * the viewport can redraw from it immediately. Values are only pushed back into a
     * widget that does not hold focus, so a running edit is never clobbered mid-typing.
     */
    class InspectorPanel {
    public:
        InspectorPanel(Scene &scene, EditorViewport &viewport, Gorgon::Widgets::Panel &panel);
        ~InspectorPanel();

        InspectorPanel(const InspectorPanel &) = delete;
        InspectorPanel &operator=(const InspectorPanel &) = delete;

        /// Full rebind: swaps between scene settings and the selected entity, then re-lays out.
        void Rebuild();

        /// Pulls the current Scene values into every widget that is not being edited.
        void Refresh();

        /// Repositions every row from the panel's interior size.
        void Layout();

        /// Fired after any edit that changes the scene.
        std::function<void()> OnSceneModified;

        /// Fired after any edit that changes the entity tree (component add/remove, rename).
        std::function<void()> OnStructureChanged;

        /// Fired when the user asks the Asset Browser to pick a sprite (false) or script (true)
        /// path for the selected entity. EditorApp routes this to AssetBrowserPanel::BeginPicker.
        std::function<void(bool isScript)> OnBrowseRequested;

        /// Applies a picked asset path to the selected entity's sprite or script component,
        /// creating the component when it is missing. Used by the asset-browser picker.
        void SetPickedAsset(const std::string &path, bool isScript);

    private:
        using Widget = Gorgon::UI::Widget;

        struct Row {
            std::vector<Widget *> widgets;
            std::vector<float> weights;
        };

        void build();
        void buildRows();
        void registerEvents();
        void updateVisibility();
        void pushValues();

        Entity *selectedEntity();
        void modified(bool structural = false);

        void addSprite();
        void removeSprite();
        void addCollider();
        void removeCollider();
        void addScript();
        void removeScript();
        void onColliderTypeChanged(long index);

        Scene &scene_;
        EditorViewport &viewport_;
        Gorgon::Widgets::Panel &panel_;

        // ---- Scene settings (shown when nothing is selected) -------------------
        Gorgon::Widgets::Label     lblScene_{"Scene", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblSceneName_{"Name"};
        Gorgon::Widgets::Textbox   sceneName_;
        Gorgon::Widgets::Label     lblGridSize_{"Grid Size"};
        Gorgon::Widgets::Integerbox gridSize_;
        Gorgon::Widgets::Label     lblBackground_{"Background"};
        Gorgon::Widgets::ColorPicker backgroundColor_;

        // ---- Entity ------------------------------------------------------------
        Gorgon::Widgets::Label     lblEntity_{"Entity", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblEntityName_{"Name"};
        Gorgon::Widgets::Textbox   entityName_;
        Gorgon::Widgets::Checkbox  chkVisible_{"Visible", true};
        Gorgon::Widgets::Checkbox  chkLocked_{"Locked", false};
        Gorgon::Widgets::Label     lblZOrder_{"Z Order"};
        Gorgon::Widgets::Integerbox zOrder_;

        // ---- Transform ---------------------------------------------------------
        Gorgon::Widgets::Label     lblTransform_{"Transform", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblPosition_{"Position"};
        Gorgon::Widgets::Pointfbox position_;
        Gorgon::Widgets::Label     lblScale_{"Scale"};
        Gorgon::Widgets::Pointfbox scale_;
        Gorgon::Widgets::Label     lblRotation_{"Rotation"};
        Gorgon::Widgets::Numberbox rotation_;

        // ---- Sprite ------------------------------------------------------------
        Gorgon::Widgets::Label     lblSprite_{"Sprite", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblSpriteAsset_{"Asset"};
        Gorgon::Widgets::Textbox   spriteAsset_;
        Gorgon::Widgets::Button    btnPickSprite_{"..."};
        Gorgon::Widgets::Label     lblSpriteTint_{"Tint"};
        Gorgon::Widgets::ColorPicker spriteTint_;
        Gorgon::Widgets::Button    btnAddSprite_{"+ Add Sprite"};
        Gorgon::Widgets::Button    btnRemoveSprite_{"- Remove Sprite"};

        // ---- Collider ----------------------------------------------------------
        Gorgon::Widgets::Label     lblCollider_{"Collider", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblColliderType_{"Type"};
        Gorgon::Widgets::DropdownList<std::string> colliderType_;
        Gorgon::Widgets::Label     lblColliderSize_{"W / H"};
        Gorgon::Widgets::Pointfbox colliderSize_;
        Gorgon::Widgets::Label     lblColliderRadius_{"Radius"};
        Gorgon::Widgets::Numberbox colliderRadius_;
        Gorgon::Widgets::Label     lblColliderOffset_{"Offset"};
        Gorgon::Widgets::Pointfbox colliderOffset_;
        Gorgon::Widgets::Button    btnAddCollider_{"+ Add Collider"};
        Gorgon::Widgets::Button    btnRemoveCollider_{"- Remove Collider"};

        // ---- Script ------------------------------------------------------------
        Gorgon::Widgets::Label     lblScript_{"Script", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblScriptPath_{"Path"};
        Gorgon::Widgets::Textbox   scriptPath_;
        Gorgon::Widgets::Button    btnPickScript_{"..."};
        Gorgon::Widgets::Button    btnAddScript_{"+ Add Script"};
        Gorgon::Widgets::Button    btnRemoveScript_{"- Remove Script"};

        std::vector<Row> rows_;
        bool updating_ = false;
        Gorgon::Event<Gorgon::UI::Widget>::Token boundsToken_ =
            Gorgon::Event<Gorgon::UI::Widget>::EmptyToken;
    };

} // namespace Gorgon::Editor
